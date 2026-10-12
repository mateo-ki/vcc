// 内存归因夹具：把一次真实会话按阶段打点，报私有内存与工作集。
// 用复制出来的 data 目录（不碰正在跑的那份），所以节点数/订阅分库数都和你机器上一致。
//   跑法：QT_QPA_PLATFORM=offscreen build/Debug/vccMemProbe.exe --data <复制出来的 data 目录>
#include <gtest/gtest.h>

#include "app/MainWindow.h"

#include <QApplication>
#include <QThread>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

#include <functional>
#include <iostream>
#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/ApiPage.h"
#include "gui/NodesPage.h"
#include "gui/ProxyPage.h"
#include "gui/SettingsPage.h"
#include "gui/SourcesPage.h"
#include "gui/NodeCard.h"
#include "gui/Theme.h"
#include "platform/ProcessControl.h"

using namespace Gui;

namespace {

QString g_dataDir;

qint64 privateBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
        return -1;
    }
    return static_cast<qint64>(pmc.PrivateUsage);
#else
    return -1;
#endif
}

qint64 workingSetBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
        return -1;
    }
    return static_cast<qint64>(pmc.WorkingSetSize);
#else
    return -1;
#endif
}

qint64 mb(qint64 bytes) { return bytes < 0 ? -1 : bytes / (1024 * 1024); }

struct Step {
    QString name;
    qint64 privateMb;
    qint64 workingMb;
};

std::vector<Step> g_steps;

void mark(const QString &name)
{
    g_steps.push_back({name, mb(privateBytes()), mb(workingSetBytes())});
}

void pumpEvents(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents();
    }
}

}

TEST(MemProbe, attributesByStage)
{
    ASSERT_FALSE(g_dataDir.isEmpty());
    const QString dbPath = QDir(g_dataDir).filePath(QStringLiteral("vcc.db"));
    ASSERT_TRUE(QFileInfo::exists(dbPath));

    mark(QStringLiteral("A QApplication + Theme"));
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    mark(QStringLiteral("B Theme::apply 落应用级 QSS"));

    App::AppContext::Options options;
    options.dbPath = dbPath;
    const auto created = App::AppContext::create(options);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    auto context = created.value();
    mark(QStringLiteral("C AppContext（主库 + 迁移）"));

    // 先只碰数据库：把每个订阅源的节点库都开一遍连接池并读全部节点，
    // 一个控件都不造 —— 这样能量出 sqlite 扇出本身值多少内存。
    {
        auto &bare = context->database();
        const auto listed = bare.listSources();
        QStringList ids;
        if (listed.isOk()) {
            for (const auto &source : listed.value()) {
                ids.push_back(source.id);
            }
        }
        const auto summaries = bare.loadAvailableNodeSummaries(QString());
        mark(QStringLiteral("C2 只开分库连接池读完节点（%1 源 / %2 条）")
                  .arg(ids.size())
                  .arg(summaries.isOk() ? summaries.value().size() : -1));

        // 空过订阅分库的 idle_timeout（30s）再回收一次，量"只缩页缓存"到底能拿回多少。
        // 不构造任何控件，所以这一段和 C2 的差就是连接池那一层的净效果。
        QThread::msleep(32 * 1000);
        context->database().reapIdlePools();
        QThread::msleep(300);
        mark(QStringLiteral("C2b 分库连接空闲回收（shrink_memory）之后"));
    }

    auto *proxyPage = new ProxyPage(*context);
    proxyPage->resize(1200, 800);
    mark(QStringLiteral("D ProxyPage 构造（六个分栏全建）"));

    proxyPage->selectPanel(ProxyPage::Panel::Nodes);
    proxyPage->refresh();
    pumpEvents(1200);
    const int nodeRows = proxyPage->rowCount();
    mark(QStringLiteral("E 节点栅格真数据（卡片 %1 张）").arg(nodeRows));

    // 重建耗时：点地区药丸走的就是这条路（他报的"筛选地区特别卡"）。
    {
        QVector<QPushButton *> pills;
        for (auto *button : proxyPage->findChildren<QPushButton *>()) {
            if (button->property("region").isValid()) {
                pills.push_back(button);
            }
        }
        qint64 total = 0;
        qint64 worst = 0;
        int clicks = 0;
        QElapsedTimer clock;
        for (int round = 0; round < 4; ++round) {
            for (auto *pill : pills) {
                QCoreApplication::processEvents();   // 上一轮的 deferred delete 先落地
                QCoreApplication::processEvents();   // 上一轮 deferred delete 先落地
                clock.start();
                pill->click();
                const qint64 ms = clock.elapsed();
                if (round > 0) {        // 首轮含首次布局，不计入
                    total += ms;
                    worst = std::max(worst, ms);
                    ++clicks;
                }
            }
        }
        std::cerr << qPrintable(QStringLiteral("   点地区药丸重建：平均 %1 ms / 最坏 %2 ms"
                                                " / 卡片 %3 张 / 药丸 %4 个")
                                    .arg(clicks ? total / double(clicks) : 0.0, 0, 'f', 1)
                                    .arg(worst).arg(nodeRows).arg(pills.size()))
                  << std::endl;
        RecordProperty("pillClickAvgMs", static_cast<int>(clicks ? total / clicks : 0));
        RecordProperty("pillClickWorstMs", static_cast<int>(worst));
    }

    mark(QStringLiteral("E2 点完地区药丸之后"));
    auto *sourcesPage = new SourcesPage(*context);
    sourcesPage->resize(1200, 800);
    sourcesPage->refresh();
    pumpEvents(600);
    mark(QStringLiteral("F SourcesPage"));

    auto *nodesPage = new NodesPage(*context);
    nodesPage->resize(1200, 800);
    nodesPage->refresh();
    pumpEvents(600);
    mark(QStringLiteral("G NodesPage（节点表）"));

    auto *settingsPage = new SettingsPage(*context);
    settingsPage->resize(1200, 800);
    mark(QStringLiteral("H SettingsPage"));

    auto *apiPage = new ApiPage(*context);
    apiPage->resize(1200, 800);
    mark(QStringLiteral("I ApiPage"));

    // 分库扇出：把每个源的节点库都开一遍连接池。
    auto &db = context->database();
    const auto listed = db.listSources();
    const int sourceCount = listed.isOk() ? listed.value().size() : 0;
    mark(QStringLiteral("J 列出 %1 个订阅源后").arg(sourceCount));

    // 轮询模拟：刷新 300 次（连接/日志/规则各栏都会重画）。
    for (int i = 0; i < 300; ++i) {
        proxyPage->refresh();
        QCoreApplication::processEvents();
    }
    mark(QStringLiteral("K 300 次 refresh 之后"));

    for (int i = 0; i < 300; ++i) {
        proxyPage->selectPanel(ProxyPage::Panel::Nodes);
        proxyPage->refresh();
        proxyPage->selectPanel(ProxyPage::Panel::Rules);
        proxyPage->refresh();
        QCoreApplication::processEvents();
    }
    mark(QStringLiteral("L 600 次切页 refresh 之后"));

    pumpEvents(1500);
    mark(QStringLiteral("M 静置 1.5s（事件队列清空）"));

    for (int round = 0; round < 6; ++round) {
        for (int i = 0; i < 500; ++i) {
            proxyPage->selectPanel(ProxyPage::Panel::Nodes);
            proxyPage->refresh();
            proxyPage->selectPanel(ProxyPage::Panel::Rules);
            proxyPage->refresh();
            QCoreApplication::processEvents();
        }
        pumpEvents(200);
        mark(QStringLiteral("N%1 再跑 500 轮切页重建（累计 %2 轮）").arg(round + 1).arg((round + 1) * 500));
    }

    Platform::trimWorkingSet();
    mark(QStringLiteral("O trimWorkingSet 之后"));

    QString line;
    qint64 previous = -1;
    for (const auto &step : g_steps) {
        const qint64 delta = previous < 0 ? 0 : step.privateMb - previous;
        std::cerr << qPrintable(QStringLiteral("%1  private=%2MB  ws=%3MB  delta=%4MB")
                                     .arg(step.name)
                                     .arg(step.privateMb)
                                     .arg(step.workingMb)
                                     .arg(delta))
                  << std::endl;
        previous = step.privateMb;
    }
    RecordProperty("nodeCards", nodeRows);
    RecordProperty("sources", sourceCount);
    RecordProperty("privateMbEnd", static_cast<int>(mb(privateBytes())));
    RecordProperty("privateMbStart", static_cast<int>(g_steps.front().privateMb));

    delete apiPage;
    delete settingsPage;
    delete nodesPage;
    delete sourcesPage;
    delete proxyPage;
    SUCCEED();
}

// 隔离实验：245 张节点卡的内存到底花在哪 —— 每张自带一份 CSS、
// 一份共享 CSS（挂在容器上）、还是完全没有 CSS。
TEST(MemProbe, cardCostBreakdown)
{
    struct Variant {
        const char *name;
        bool ownCss;
        bool sharedCss;
        bool newCard;      // 改造后的真实卡片：自绘框 + QFont/QPalette 标签
    };
    const QVector<Variant> variants = {
        {"1) 每张卡各 setStyleSheet（改造前）", true, false, false},
        {"2) CSS 挂在容器上，卡片只有 objectName", false, true, false},
        {"3) 完全没有 CSS（纯控件）", false, false, false},
        {"4) 改造后的卡片（NodeCardFrame + NodeBadge）", false, false, true},
    };

    for (const auto &variant : variants) {
        auto *host = new QWidget(nullptr);
        host->resize(1200, 900);
        auto *grid = new QGridLayout(host);
        if (variant.sharedCss) {
            host->setStyleSheet(QStringLiteral(
                "QFrame#NodeCard { background-color: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; }"
                "QLabel#NodeName { font-size: 13px; font-weight: 600; }"));
        }

        const qint64 before = mb(privateBytes());
        QElapsedTimer buildClock;
        buildClock.start();
        for (int i = 0; i < 245; ++i) {
            QFrame *card = variant.newCard ? static_cast<QFrame *>(new NodeCardFrame(host))
                                           : static_cast<QFrame *>(new QFrame(host));
            card->setObjectName(QStringLiteral("NodeCard"));
            if (!variant.newCard) {
                card->setAttribute(Qt::WA_StyledBackground, true);
            }
            auto *box = new QVBoxLayout(card);
            auto *top = new QHBoxLayout();
            auto *flag = new QLabel(QStringLiteral("HK 香港"), card);
            auto *badge = new QLabel(QStringLiteral("● 运行中"), card);
            auto *name = new QLabel(QStringLiteral("node-%1").arg(i), card);
            name->setObjectName(QStringLiteral("NodeName"));
            auto *server = new QLabel(QStringLiteral("1.2.3.4:443"), card);
            auto *proto = new QLabel(QStringLiteral("ss"), card);
            top->addWidget(flag);
            top->addWidget(badge);
            box->addLayout(top);
            box->addWidget(name);
            box->addWidget(server);
            box->addWidget(proto);
            if (variant.ownCss) {
                card->setStyleSheet(QStringLiteral(
                    "QFrame#NodeCard { background-color: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; }"));
                flag->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #475569;"));
                badge->setStyleSheet(QStringLiteral("font-size: 11px; color: #2563eb;"));
                name->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: #0f172a;"));
                server->setStyleSheet(QStringLiteral("font-size: 11px; color: #64748b;"));
                proto->setStyleSheet(QStringLiteral("font-size: 11px; color: #334155;"));
            }
            if (variant.newCard) {
                // 探针自己造"旧式带子控件的卡"，样式就地给（生产的 NodeCardFrame
                // 现在什么都不挂：四段文字在 paintEvent 里画）。
                const auto paintStyle = [](QLabel *label, int pixelSize, QFont::Weight weight,
                                           const QColor &color) {
                    QFont font = label->font();
                    font.setPixelSize(pixelSize);
                    font.setWeight(weight);
                    label->setFont(font);
                    QPalette palette = label->palette();
                    palette.setColor(QPalette::WindowText, color);
                    label->setPalette(palette);
                };
                paintStyle(flag, 12, QFont::DemiBold, QColor(0x47, 0x55, 0x69));
                paintStyle(name, 14, QFont::Bold, QColor(0x1d, 0x4e, 0xd8));
                paintStyle(server, 11, QFont::Normal, QColor(0x64, 0x74, 0x8b));
                auto *runBadge = new NodeBadge(QStringLiteral("RUN"), card);
                runBadge->setTone(NodeBadge::Tone::Running);
                auto *protoBadge = new NodeBadge(QStringLiteral("ss"), card);
                protoBadge->setTone(NodeBadge::Tone::Protocol);
            }
            grid->addWidget(card, i / 4, i % 4);
        }
        host->ensurePolished();
        const qint64 buildMs = buildClock.elapsed();
        const qint64 after = mb(privateBytes());

        int widgets = 0;
        for (auto *child : host->findChildren<QWidget *>()) {
            Q_UNUSED(child);
            ++widgets;
        }
        std::cerr << qPrintable(QStringLiteral("   %1  private=%2MB delta=%3MB build=%4ms"
                                                " (%5 ms/卡) widgets=%6")
                                     .arg(QString::fromUtf8(variant.name))
                                     .arg(after).arg(after - before).arg(buildMs)
                                     .arg(buildMs / 245.0, 0, 'f', 2)
                                     .arg(widgets))
                  << std::endl;
        delete host;
        pumpEvents(150);
    }
    SUCCEED();
}

/// ①② 两个计时点：启动到首屏的分解，和 1.5 s 轮询每一轮的真实耗时。
/// 只打印不断言（这是取证，不是回归；断言由别的测试负责）。
TEST(MemTimings, startupStagesAndPollCost)
{
    ASSERT_FALSE(g_dataDir.isEmpty());
    const QString dbPath = QDir(g_dataDir).filePath(QStringLiteral("vcc.db"));
    ASSERT_TRUE(QFileInfo::exists(dbPath));

    QElapsedTimer clock;

    clock.start();
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    const qint64 themeMs = clock.elapsed();

    clock.restart();
    App::AppContext::Options options;
    options.dbPath = dbPath;
    const auto created = App::AppContext::create(options);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    auto context = created.value();
    const qint64 contextMs = clock.elapsed();

    clock.restart();
    auto *page = new ProxyPage(*context);
    page->resize(1200, 800);
    const qint64 pageMs = clock.elapsed();

    // 轮询一轮 = refreshStatusCard + （按当前分栏）连接/日志重灌。跑 40 轮取分布。
    qint64 pollTotal = 0;
    qint64 pollWorst = 0;
    page->runPollCycleOnce();  // 第一轮热缓存，不计
    for (int i = 0; i < 40; ++i) {
        clock.restart();
        page->runPollCycleOnce();
        const qint64 ms = clock.elapsed();
        pollTotal += ms;
        pollWorst = std::max(pollWorst, ms);
    }

    clock.restart();
    auto *window = new MainWindow(*context);
    const qint64 mainWindowMs = clock.elapsed();

    clock.restart();
    window->show();
    QCoreApplication::processEvents();
    const qint64 firstFrameMs = clock.elapsed();

    std::cerr << "TIMING Theme::apply=" << themeMs << "ms"
              << "  AppContext(开库+迁移+自动规则)=" << contextMs << "ms"
              << "  ProxyPage 六栏全建=" << pageMs << "ms"
              << "  MainWindow(五页+侧栏+状态条)=" << mainWindowMs << "ms"
              << "  show+首轮事件=" << firstFrameMs << "ms" << std::endl;
    std::cerr << "TIMING 轮询一轮 均=" << double(pollTotal) / 40.0 << "ms 最坏=" << pollWorst
              << "ms（预算 1500ms）" << std::endl;

    delete window;
    delete page;
    context.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// UI 指纹：Debug 和 Release 两个构建各跑一遍，把"界面到底长什么样"的客观量打出来对差。
/// 他报"Release 的 UI 大变了"，我看不见界面，只能这样对齐。
TEST(MemTimings, uiFingerprint)
{
    ASSERT_FALSE(g_dataDir.isEmpty());
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);

    App::AppContext::Options fpOptions;
    fpOptions.dbPath = QDir(g_dataDir).filePath(QStringLiteral("vcc.db"));
    const auto fpCreated = App::AppContext::create(fpOptions);
    ASSERT_TRUE(fpCreated.isOk()) << fpCreated.error().toStdString();
    auto fpContext = fpCreated.value();

    auto *window = new MainWindow(*fpContext);
    window->resize(1200, 800);
    window->show();
    QCoreApplication::processEvents();

    const QString styleName = QApplication::style() ? QApplication::style()->objectName() : QStringLiteral("<null>");
    const QPalette pal = window->palette();
    std::cerr << "FP style=" << styleName.toStdString()
              << " appQssLen=" << qApp->styleSheet().size()
              << " window=" << pal.color(QPalette::Window).name(QColor::HexRgb).toStdString()
              << " base=" << pal.color(QPalette::Base).name(QColor::HexRgb).toStdString()
              << " button=" << pal.color(QPalette::Button).name(QColor::HexRgb).toStdString()
              << " highlight=" << pal.color(QPalette::Highlight).name(QColor::HexRgb).toStdString()
              << std::endl;

    int cards = 0;
    int cardsWithOwnCss = 0;
    for (QFrame *card : window->findChildren<QFrame *>()) {
        if (card->objectName() != QLatin1String("Card")) {
            continue;
        }
        ++cards;
        if (!card->property("paletteCss").toString().isEmpty()) {
            ++cardsWithOwnCss;
        }
        if (cards == 1) {
            const QImage image = card->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            std::cerr << "FP firstCard size=" << image.width() << "x" << image.height()
                      << " fill@center=" << QColor(image.pixel(image.width() / 2, image.height() / 2))
                                             .name(QColor::HexRgb).toStdString()
                      << " edge@3=" << QColor(image.pixel(image.width() / 2, 3))
                                           .name(QColor::HexRgb).toStdString() << std::endl;
        }
    }
    std::cerr << "FP Card 数量=" << cards << "  自带底色模板的=" << cardsWithOwnCss << std::endl;

    delete window;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("vcc memory attribution probe"));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringLiteral("data"),
                                  QStringLiteral("data 目录（里面有 vcc.db 与 subscriptions）"),
                                  QStringLiteral("path"));
    parser.addOption(dataOption);
    parser.process(app);
    g_dataDir = parser.value(QStringLiteral("data"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
