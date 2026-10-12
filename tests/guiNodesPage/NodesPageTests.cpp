#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <iostream>

#include "app/AppContext.h"
#include "db/Database.h"
#include "core/TimeUtil.h"
#include "gui/NodeCard.h"
#include "gui/NodesPage.h"
#include "gui/ProxyPage.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "gui/Theme.h"
#include "models/Node.h"
#include "models/Source.h"

using namespace Gui;

namespace {

std::shared_ptr<App::AppContext> makeContext(const QString &directory)
{
    App::AppContext::Options options;
    options.dbPath = QDir(directory).filePath(QStringLiteral("vpn-parser.db"));
    const Result<std::shared_ptr<App::AppContext>> created = App::AppContext::create(options);
    if (created.isErr()) {
        ADD_FAILURE() << created.error().toStdString();
        return nullptr;
    }
    return created.value();
}

Models::Node seedNode(const QString &id, const QString &name, const QString &server,
                      const QString &country, std::optional<qint64> ms)
{
    Models::Node node;
    node.id = id;
    node.protocol = QStringLiteral("ss");
    node.name = name;
    node.server = server;
    node.port = 443;
    node.country = country;
    node.raw = QStringLiteral("ss://a@%1:443").arg(server);
    node.config = Json::object();
    node.latencyOk = std::optional<bool>(true);
    node.latencyMs = ms;
    node.latencyTestKind = QStringLiteral("google_proxy");
    node.createdAt = TimeUtil::OptionalUtc::now();
    node.updatedAt = TimeUtil::OptionalUtc::now();
    return node;
}

void seedSampleNodes(App::AppContext &context)
{
    Models::CreateSource input;
    input.name = QStringLiteral("test-src");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const Result<Models::Source> created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk());
    const QString sourceId = created.value().id;

    QVector<Models::Node> nodes;
    nodes << seedNode(QStringLiteral("n1"), QStringLiteral("HK Node 1"), QStringLiteral("1.1.1.1"), QStringLiteral("HK"), 100)
          << seedNode(QStringLiteral("n2"), QStringLiteral("US Node 2"), QStringLiteral("2.2.2.2"), QStringLiteral("US"), 150)
          << seedNode(QStringLiteral("n3"), QStringLiteral("JP Node 3"), QStringLiteral("3.3.3.3"), QStringLiteral("JP"), 80);
    for (auto &n : nodes) {
        n.sourceId = sourceId;
    }
    const Result<int> written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk());
}

/// 灌 N 颗编号节点（测"卡片高度不该随数量变化"这类布局断言用）。
void seedNumberedNodes(App::AppContext &context, int count)
{
    Models::CreateSource input;
    input.name = QStringLiteral("counted-src");
    input.urlTemplate = QStringLiteral("https://example.com/counted");
    const Result<Models::Source> created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk());
    const QString sourceId = created.value().id;

    QVector<Models::Node> nodes;
    for (int index = 0; index < count; ++index) {
        nodes << seedNode(QStringLiteral("c%1").arg(index),
                          QStringLiteral("Node %1").arg(index + 1),
                          QStringLiteral("10.0.0.%1").arg(index + 1), QStringLiteral("HK"),
                          100 + index);
    }
    for (auto &node : nodes) {
        node.sourceId = sourceId;
    }
    const Result<int> written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk());
}

}

TEST(NodesPageTests, initialEmptyState)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    EXPECT_EQ(page->currentPage(), 1);
    EXPECT_EQ(page->loadedNodeCount(), 0);
    EXPECT_EQ(page->totalNodeCount(), 0);
    delete page;
}

TEST(NodesPageTests, loadsSeededNodes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    seedSampleNodes(*context);

    auto direct = context->database().listNodes(std::nullopt, QString(), std::nullopt, std::nullopt, std::nullopt, 1, 50, QStringLiteral("latency"));
    ASSERT_TRUE(direct.isOk()) << direct.error().toStdString();
    ASSERT_EQ(direct.value().items.size(), 3) << "Direct listNodes items count mismatch";

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    page->show();

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000 && page->loadedNodeCount() != 3) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();

    EXPECT_EQ(page->loadedNodeCount(), 3);
    EXPECT_EQ(page->totalNodeCount(), 3);
    EXPECT_EQ(page->currentPage(), 1);

    delete page;
}

TEST(NodesPageTests, filteringByKeywordNarrowsResults)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    seedSampleNodes(*context);

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    page->show();

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000 && page->loadedNodeCount() != 3) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    auto lineEdits = page->findChildren<QLineEdit *>();
    ASSERT_GE(lineEdits.size(), 2);
    QLineEdit *keyEdit = lineEdits.at(1);
    keyEdit->setText(QStringLiteral("HK"));

    auto buttons = page->findChildren<QPushButton *>();
    for (auto *btn : buttons) {
        if (btn->text() == QStringLiteral("查询")) {
            btn->click();
            break;
        }
    }

    clock.restart();
    while (clock.elapsed() < 4000 && page->loadedNodeCount() != 1) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    EXPECT_EQ(page->loadedNodeCount(), 1);
    EXPECT_EQ(page->totalNodeCount(), 1);

    delete page;
}

/// 只有一颗节点时卡片不许被拉高（2026-10-11 他报的"节点池只有一个节点的时候卡片特别大"）。
/// 一个测试只建一个 context + 一个 page：两个同进程会踩到另一条竞态（见任务里的记录）。
TEST(NodesPageTests, singleCardHeightMatchesManyCards)
{
    const auto measure = [](int nodeCount) {
        QTemporaryDir directory;
        EXPECT_TRUE(directory.isValid());
        auto context = makeContext(directory.path());
        EXPECT_TRUE(context != nullptr);
        seedNumberedNodes(*context, nodeCount);

        auto *page = new NodesPage(*context);
        page->resize(1200, 800);
        page->show();
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 12000 && page->loadedNodeCount() != nodeCount) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        QCoreApplication::processEvents();
        EXPECT_EQ(page->loadedNodeCount(), nodeCount) << "页面没读出这 " << nodeCount << " 颗节点";

        int height = -1;
        int cards = 0;
        const QList<QFrame *> frames = page->findChildren<QFrame *>();
        for (QFrame *frame : frames) {
            if (!frame->property("nodeId").isValid()) {
                continue;
            }
            ++cards;
            height = frame->height();
        }
        EXPECT_EQ(cards, nodeCount);
        delete page;
        return height;
    };

    const int single = measure(1);
    EXPECT_GT(single, 0);
    if (single > 0) {
        // 一张卡也不许吃掉整屏：上限按"两行卡"的高度算。
        EXPECT_LE(single, 120) << "单卡高度 " << single << "px，把空白摊进来了";
    }
}

namespace {

QPushButton *buttonNamed(QWidget *root, const QString &text)
{
    for (QPushButton *btn : root->findChildren<QPushButton *>()) {
        if (btn->text() == text) {
            return btn;
        }
    }
    return nullptr;
}

/// 灌一个订阅源 + 两颗节点，并把页面刷到"分组标签已经建好"的状态。
NodesPage *pageWithOneSource(App::AppContext &context)
{
    Models::CreateSource input;
    input.name = QStringLiteral("组甲");
    input.urlTemplate = QStringLiteral("https://example.com/sub.txt");
    input.enabled = true;
    const auto created = context.database().createSource(input);
    EXPECT_TRUE(created.isOk());
    for (const QString &name : {QStringLiteral("HK-01"), QStringLiteral("JP-02")}) {
        auto node = seedNode(QStringLiteral("n-") + name, name, QStringLiteral("203.0.113.10"),
                             name.left(2), std::nullopt);
        node.sourceId = created.value().id;
        EXPECT_TRUE(context.database().importNodes({node}).isOk());
    }
    auto *page = new NodesPage(context);
    page->resize(1000, 800);
    page->show();
    QCoreApplication::processEvents();
    page->refresh();
    // reloadGroups 是 runAsync 的，要把事件循环转到分组标签真的建出来为止。
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000 && buttonNamed(page, QStringLiteral("测速当前订阅")) == nullptr) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    for (int attempt = 0; attempt < 80; ++attempt) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return page;
}

void pumpUntil(const std::function<bool()> &done, qint64 budgetMs = 8000)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < budgetMs && !done()) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
}

} // namespace

/// 每一拍的状态栏文案 = Rust `gui/widgets.rs:726 format_speedtest_progress`。
/// 他要的"实时一点"就在这条线上：阶段名、完成/总数、百分比（一位小数）、成功/失败、
/// 已经跑了多少秒 —— 之前 vcc 自己编了一句"正在测速当前订阅: 1 / 300 (0%) [可用..]"，
/// 既没有阶段也没有耗时，切换阶段时界面看不出有任何变化。
TEST(NodesPageSpeedtest, progressLineCarriesPhaseCountsPercentAndElapsed)
{
    Speedtest::SpeedTestProgress p;
    p.phase = Speedtest::SpeedTestPhase::TcpPrefilter;
    p.done = 7;
    p.total = 300;
    p.ok = 5;
    p.fail = 2;
    p.elapsedMs = 12340;
    const QString tcp = formatSpeedtestProgress(p);
    EXPECT_TRUE(tcp.contains(UiText::phaseTcpPrefilter())) << tcp.toStdString();
    EXPECT_TRUE(tcp.contains(QStringLiteral("7/300"))) << tcp.toStdString();
    EXPECT_TRUE(tcp.contains(QStringLiteral("2.3%"))) << tcp.toStdString();
    EXPECT_TRUE(tcp.contains(QStringLiteral("12.3s"))) << tcp.toStdString();
    EXPECT_TRUE(tcp.contains(QStringLiteral("5"))) << tcp.toStdString();

    p.phase = Speedtest::SpeedTestPhase::RealProxy;
    p.kernel = Kernel::Kind::Mihomo;
    p.batchIndex = 3;
    p.batchTotal = 12;
    const QString proxy = formatSpeedtestProgress(p);
    EXPECT_TRUE(proxy.contains(UiText::phaseRealProxy())) << proxy.toStdString();
    EXPECT_TRUE(proxy.contains(QStringLiteral("Mihomo"))) << proxy.toStdString();
    EXPECT_TRUE(proxy.contains(QStringLiteral("3/12"))) << proxy.toStdString();
    // 阶段名必须跟着换：同一条线在两个阶段不能长得一模一样（那正是"看不出在动"的来源）。
    EXPECT_NE(tcp.contains(UiText::phaseRealProxy()), proxy.contains(UiText::phaseRealProxy()));

    // total==0 时 Rust 取 100.0%，不是除零。
    Speedtest::SpeedTestProgress empty;
    empty.total = 0;
    empty.done = 0;
    EXPECT_TRUE(formatSpeedtestProgress(empty).contains(QStringLiteral("100.0%")));
}

/// 收尾文案（Rust SpeedDone）：平均延迟取整带 ms、平均下载留一位小数，缺值是 "-"。
TEST(NodesPageSpeedtest, doneLineSummarisesCountsAndAverages)
{
    Models::SpeedTestSummary summary;
    summary.total = 300;
    summary.ok = 218;
    summary.avgMs = 187.4;
    summary.avgDownloadKbps = 12.34;
    const QString text = speedDoneText(summary);
    EXPECT_TRUE(text.contains(QStringLiteral("218/300"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("187ms"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("12.3KB/s"))) << text.toStdString();

    Models::SpeedTestSummary bare;
    bare.total = 4;
    const QString noAvg = speedDoneText(bare);
    EXPECT_EQ(noAvg.count(QStringLiteral("-")), 2) << noAvg.toStdString();
}

/// 状态栏文案必须是 Rust 那一套（含阶段/百分比/耗时），并且"没匹配到节点"要说实话。
/// 这条不碰网络：关键字筛成空集，所以 worker 在 listNodeIdsForGroup 之后就回来了。
TEST(NodesPageSpeedtest, statusLinesFollowRustsWordingAndReportEmptyGroup)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    NodesPage *page = pageWithOneSource(*context);

    QStringList messages;
    QObject::connect(page, &NodesPage::statusMessageRequested,
                     [&messages](const QString &msg) { messages << msg; });

    // 筛一个不存在的关键字：Rust 在这种情况下报"当前分组没有符合筛选条件的节点"。
    for (QLineEdit *edit : page->findChildren<QLineEdit *>()) {
        if (edit->placeholderText().contains(QStringLiteral("搜索")) || edit->objectName().isEmpty()) {
            edit->setText(QStringLiteral("zzz-不存在"));
            break;
        }
    }

    page->speedtestSource(QStringLiteral("no-such-source-at-all"));
    QCoreApplication::processEvents();
    pumpUntil([&messages] { return messages.size() >= 2; });

    // sourceId 不存在时走的是"分组没有匹配节点"这一支（不是静默 return）。
    ASSERT_GE(messages.size(), 2) << "一条状态消息都没发出来";
    // 第一句必须逐字等于 Rust 的模板（"正在测速订阅“{name}”（{n} 个节点）..."），
    // 不是"包含三个点"这种松判据 —— 松判据连自己编的文案都拦不住。
    EXPECT_EQ(messages.first(), UiText::speedGroupStatus()
                                    .arg(QStringLiteral("no-such-source-at-all"))
                                    .arg(0))
        << messages.first().toStdString();
    EXPECT_TRUE(messages.last().contains(UiText::speedGroupEmpty()))
        << messages.last().toStdString();
    delete page;
}

/// busy 期间按钮要灰（Rust 是 add_enabled(!busy, ...)）。之前是静默 return：
/// 再点一次什么都没发生，状态栏也停在旧文案上，看起来就是"卡住"。
TEST(NodesPageSpeedtest, speedtestButtonsGreyOutWhileBusyAndWithoutSelection)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    NodesPage *page = pageWithOneSource(*context);

    QPushButton *groupBtn = buttonNamed(page, QStringLiteral("测速当前订阅"));
    QPushButton *allBtn = buttonNamed(page, QStringLiteral("测速全部"));
    ASSERT_TRUE(groupBtn != nullptr);
    ASSERT_TRUE(allBtn != nullptr);

    // 没选中订阅：当前订阅灰、全部亮（Rust 的 !busy && selected_group.is_some()）。
    EXPECT_FALSE(groupBtn->isEnabled());
    EXPECT_TRUE(allBtn->isEnabled());

    QStringList messages;
    QObject::connect(page, &NodesPage::statusMessageRequested,
                     [&messages](const QString &msg) { messages << msg; });
    page->speedtestSource(QStringLiteral("some-source"));
    QCoreApplication::processEvents();
    EXPECT_FALSE(allBtn->isEnabled()) << "测速进行中，『测速全部』还亮着（点了会被静默吞掉）";
    EXPECT_FALSE(groupBtn->isEnabled()) << "测速进行中，『测速当前订阅』还亮着";

    pumpUntil([&messages] { return messages.size() >= 2; });
    EXPECT_TRUE(allBtn->isEnabled()) << "跑完没把按钮放回来（busy_ 卡住了）";
    delete page;
}

/// 他 2026-10-12：「节点池的卡片和控制台的卡片不一样」。Rust 那边本来就是**同一张卡**
/// —— `widgets.rs::compact_node_card` 被 proxy.rs（控制台）与 views/nodes.rs（节点池）
/// 同时调用。现在两边都是 Gui::NodeCardFrame：同一类型、同一行结构、同一高度，
/// 而且节点池不再"每卡挂一份 QSS"（那是实测过的一笔内存）。
TEST(NodesPageTests, nodePoolAndConsoleShareOneCardDesign)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedSampleNodes(*context);
    // 故意再灌两颗名字长度悬殊的：长名字会把那一列的 minimumSizeHint 撑开
    // （他 2026-10-12 报的"控制台第一列比其他列宽"就是这个）。
    {
        const auto sources = context->database().listSources();
        ASSERT_TRUE(sources.isOk());
        ASSERT_FALSE(sources.value().isEmpty());
        const QString sourceId = sources.value().first().id;
        QVector<Models::Node> extra;
        for (const QString &name : {QString(200, QChar('L')), QStringLiteral("x")}) {
            Models::Node node;
            node.id = QStringLiteral("odd-%1").arg(name.size());
            node.sourceId = sourceId;
            node.protocol = QStringLiteral("ss");
            node.name = name;
            node.server = QStringLiteral("203.0.113.%1").arg(name.size());
            node.port = 443;
            node.raw = QStringLiteral("ss://a@%1:443").arg(node.server);
            node.country = QStringLiteral("HK");
            node.latencyMs = std::nullopt;
            node.latencyOk = std::nullopt;
            node.latencyTestKind = QString();
            node.latencyKernel = QString();
            node.latencyTestedAt = TimeUtil::OptionalUtc();
            node.createdAt = TimeUtil::OptionalUtc::now();
            node.updatedAt = TimeUtil::OptionalUtc::now();
            extra.push_back(node);
        }
        ASSERT_TRUE(context->database().importNodes(extra).isOk());
    }

    auto *nodes = new NodesPage(*context);
    nodes->resize(1100, 800);
    nodes->show();
    QCoreApplication::processEvents();
    nodes->refresh();
    for (int i = 0; i < 80; ++i) { QCoreApplication::processEvents(); QThread::msleep(10); }

    auto *console = new ProxyPage(*context);
    console->resize(1100, 800);
    console->show();
    for (int i = 0; i < 80; ++i) { QCoreApplication::processEvents(); QThread::msleep(10); }

    const QList<NodeCardFrame *> pool = nodes->findChildren<NodeCardFrame *>();
    const QList<NodeCardFrame *> grid = console->findChildren<NodeCardFrame *>();
    ASSERT_FALSE(pool.isEmpty()) << "节点池没再产出 NodeCardFrame，统一又回来了";
    ASSERT_FALSE(grid.isEmpty());

    // 卡高跨页面必须相等（高度由内容决定，同一张卡就该同一高度）；
    // 宽度只在**同一张网格里**要求相等 —— 两张页面的容器宽度本来就不同
    // （控制台 3 列 / 节点池 4 列），跨页面比宽度会把合法的差异判成红。
    auto within = [](const QList<NodeCardFrame *> &cards, int *first, int *odd) {
        int width = -1;
        int mismatched = 0;
        int visible = 0;
        for (NodeCardFrame *card : cards) {
            if (!card->isVisible()) {
                continue;
            }
            ++visible;
            if (width < 0) {
                width = card->width();
            } else if (qAbs(card->width() - width) > 1) {
                // 容 1px：QGridLayout 把奇数余量分给前几列，3 列时会出 347/346/347。
                // 这条守卫要抓的是"405 / 404 / 231"那种整列没拉伸的，不是取整。
                ++mismatched;
            }
        }
        *first = width;
        *odd = mismatched;
        return visible;
    };
    int poolWidth = -1;
    int consoleWidth = -1;
    int poolOdd = 0;
    int consoleOdd = 0;
    const int poolVisible = within(pool, &poolWidth, &poolOdd);
    const int consoleVisible = within(grid, &consoleWidth, &consoleOdd);
    ASSERT_GT(poolVisible, 1);
    ASSERT_GT(consoleVisible, 1);
    EXPECT_EQ(poolOdd, 0) << "节点池同一网格里的卡宽不一致";
    EXPECT_EQ(consoleOdd, 0) << "控制台同一网格里的卡宽不一致（列最小宽没清零时，长名字会把那一列撑宽）";

    // 跨页面比高度（高度由内容决定，同一张卡就该同一高度）。
    int height = -1;
    for (NodeCardFrame *card : pool + grid) {
        if (!card->isVisible()) {
            continue;
        }
        if (height < 0) {
            height = card->height();
        }
        EXPECT_EQ(card->height(), height) << "两张页面的卡高不一致";
    }

    // 卡片是"零子控件 + 零样式表 + 文字画在 paintEvent 里"。这条同时钉住两件事：
    // ① 他 2026-10-12 报的"字体背景没统一"（子 QLabel 会被应用级 `QWidget{color}`
    //    在 polish 时刷成 textPrimary，还各自糊一块底色）；② 这张卡当初的性能前提。
    const Theme::Palette pal = Theme::current();
    NodeCardFrame *sample = nullptr;
    for (NodeCardFrame *card : pool + grid) {
        if (card->isVisible()) {
            sample = card;
            break;
        }
    }
    ASSERT_TRUE(sample != nullptr);
    EXPECT_TRUE(sample->findChildren<QWidget *>().isEmpty())
        << "卡片里又长出子控件了：" << sample->findChildren<QWidget *>().size();
    EXPECT_TRUE(sample->styleSheet().isEmpty());

    // 取色要跨**所有**可见卡统计：未测速的那颗延迟是 textSecondary（"未测速"），
    // 只有测通过的才会画 success 绿 —— 盯着一张卡看会误判。
    QImage image(sample->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    sample->render(&image);
    // 变量别叫 slots —— 那是 Qt 的 moc 关键字，会被展开成空。
    const QColor slotColors[] = {pal.textPrimary, pal.muted, pal.textSecondary, pal.success};
    const char *names[] = {"textPrimary(名字)", "muted(协议)", "textSecondary(国家)", "success(延迟)"};
    const auto nearest = [](const QColor &a, const QColor &b) {
        const int dr = a.red() - b.red();
        const int dg = a.green() - b.green();
        const int db = a.blue() - b.blue();
        return dr * dr + dg * dg + db * db;
    };
    int hits[4] = {0, 0, 0, 0};
    const auto scan = [&](const QImage &source) {
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const QColor pixel = source.pixelColor(x, y);
            if (pixel.alpha() < 200) {
                continue;
            }
            int best = 0;
            int bestDistance = 1 << 30;
            for (int index = 0; index < 4; ++index) {
                const int distance = nearest(pixel, slotColors[index]);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = index;
                }
            }
            if (bestDistance < 1200) {        // 只数"确实是这个颜色的墨迹"，抗锯齿边缘不算
                ++hits[best];
            }
        }
    }
    };
    scan(image);
    for (NodeCardFrame *card : pool + grid) {
        if (!card->isVisible()) {
            continue;
        }
        QImage cardImage(card->size(), QImage::Format_ARGB32);
        cardImage.fill(Qt::transparent);
        card->render(&cardImage);
        scan(cardImage);
    }
    for (int index = 0; index < 4; ++index) {
        EXPECT_GT(hits[index], 20) << names[index] << " 没画出来（四种取色必须各自可见）";
    }

    EXPECT_GE(height, 64);

    delete nodes;
    delete console;
}

/// 卡片右键菜单也并成一个组件了：两条页面共用 `Gui::addNodeCardMenu`，
/// 条目文字只能来自 Rust 生成的 UiText，顺序按各自视图（Rust 里两个视图的顺序本来就不同）。
TEST(NodesPageTests, sharedCardMenuFollowsEachViewsRustOrder)
{
    QMenu pool;
    const Gui::NodeCardMenu poolActions = Gui::addNodeCardMenu(&pool, true, false, true, false);
    QStringList poolTexts;
    for (QAction *action : pool.actions()) {
        if (!action->isSeparator()) {
            poolTexts << action->text();
        }
    }
    EXPECT_EQ(poolTexts, (QStringList{UiText::menuSpeedtest(), UiText::menuDetails(),
                                      UiText::menuIpRisk(), UiText::menuDeleteNode()}));
    EXPECT_FALSE(poolActions.speedtest->isEnabled()) << "busy 时『测速节点』该置灰（Rust add_enabled(!busy)）";
    EXPECT_TRUE(poolActions.remove != nullptr);
    EXPECT_TRUE(poolActions.lockEgress == nullptr);

    QMenu console;
    const Gui::NodeCardMenu consoleActions = Gui::addNodeCardMenu(&console, false, true, false, true);
    QStringList consoleTexts;
    for (QAction *action : console.actions()) {
        if (!action->isSeparator()) {
            consoleTexts << action->text();
        }
    }
    EXPECT_EQ(consoleTexts, (QStringList{UiText::menuDetails(), UiText::menuSpeedtest(),
                                         UiText::menuIpRisk(), UiText::menuLockEgress()}));
    EXPECT_TRUE(consoleActions.speedtest->isEnabled());
    EXPECT_TRUE(consoleActions.remove == nullptr);
    EXPECT_TRUE(consoleActions.lockEgress != nullptr);
}

/// 点订阅那一排里的某一颗只该换选中项 + 重读节点，**不该把这一排本身重建**。
/// 旧代码在点击分支里连 `reloadGroups()` 一起调：整排先被 `clearLayout()` 清空、
/// 再等一次异步 `listNodeGroups` 才回来 —— 就是他报的"一点击订阅列表就刷新"。
/// Rust `views/nodes.rs:343` 的点击分支只有 `selected + page=1 + reload_nodes`，
/// 而且 `selectable_label(...).clicked() && !selected` —— 点已经选中的那颗什么都不做。
TEST(NodesPageTests, clickingSourcePillSelectsItWithoutRebuildingThePillRow)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    seedSampleNodes(*context);       // 源 A：3 颗
    seedNumberedNodes(*context, 53); // 源 B：53 颗 → 两页

    const auto pillsOf = [](NodesPage *p) {
        QList<QPushButton *> found;
        const QList<QPushButton *> buttons = p->findChildren<QPushButton *>();
        for (QPushButton *button : buttons) {
            // 订阅标签的判据是"带 sourceId 属性"（全部那颗带的是空串）：
            // 筛选栏的「查询」也挂着 objectName "Pill"，按名字摸会把它算进来。
            if (button->objectName() == QLatin1String("Pill")
                && button->property("sourceId").isValid()) {
                found << button;
            }
        }
        return found;
    };

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    page->show();

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000 && pillsOf(page).size() != 3) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QList<QPushButton *> pills = pillsOf(page);
    if (pills.size() != 3) {
        QStringList texts;
        for (QPushButton *pill : pills) {
            texts << pill->text();
        }
        ADD_FAILURE() << "订阅排该有 3 颗（全部 + 两个源），实际 "
                      << pills.size() << " 颗：" << texts.join(QStringLiteral(" | ")).toStdString();
        delete page;
        return;
    }

    QPushButton *counted = nullptr;
    for (QPushButton *pill : pills) {
        if (pill->text().contains(QStringLiteral("counted-src"))) {
            counted = pill;
        }
    }
    if (counted == nullptr) {
        ADD_FAILURE() << "找不到 53 颗那一源的标签";
        delete page;
        return;
    }

    const QList<QPushButton *> before = pills;
    counted->click();
    // 旧写法在点击分支里连 reloadGroups() 一起调 —— 它是 runAsync：clearLayout +
    // 重建全发生在稍后的 applier 里，所以"点完立刻比指针"什么都抓不到（第一版就是这么
    // 假绿的）。必须先把事件泵下去，让那轮重建真的跑完，再比对象身份。
    clock.restart();
    while (clock.elapsed() < 4000 && page->totalNodeCount() != 53) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
    const QList<QPushButton *> afterClick = pillsOf(page);
    EXPECT_EQ(afterClick, before) << "点一下订阅标签就把整排重建了";
    if (afterClick != before) {
        // 那一排已经被删掉了，`counted` 现在是悬垂指针 —— 再往下摸就是
        // SEH 0xc0000005，而且崩点会漂到下一条用例。当场停手。
        ADD_FAILURE() << "订阅排被重建，后面的标签指针全部失效，用例到此为止";
        delete page;
        return;
    }
    EXPECT_EQ(page->totalNodeCount(), 53) << "换了订阅却没重读节点";
    EXPECT_EQ(page->loadedNodeCount(), 50);
    EXPECT_EQ(page->currentPage(), 1);
    EXPECT_TRUE(counted->isChecked());
    for (QPushButton *pill : pillsOf(page)) {
        if (pill != counted) {
            EXPECT_FALSE(pill->isChecked());
        }
    }

    // 翻到第二页，再点一次**已经选中**的那颗：Rust 的 `&& !selected` 让整条分支跳过，
    // 所以页码不许被复位（这条同时能抓住"点击仍走 reloadGroups/reloadNodes"的写法）。
    QPushButton *next = nullptr;
    const QList<QPushButton *> buttons = page->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == QStringLiteral("下一页")) {
            next = button;
        }
    }
    if (next == nullptr) {
        ADD_FAILURE() << "找不到下一页按钮";
        delete page;
        return;
    }
    next->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->currentPage(), 2);

    counted->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->currentPage(), 2) << "点已经选中的订阅标签不该把页码复位（Rust 是 no-op）";
    EXPECT_EQ(pillsOf(page), before);

    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

