#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QPushButton>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>
#include <memory>

#include "app/AppContext.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "kernel/KernelManager.h"
#include "gui/Widgets.h"
#include "kernel/KernelManager.h"
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
                      const QString &country, qint64 ms)
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
    node.latencyMs = std::optional<qint64>(ms);
    node.latencyTestKind = QStringLiteral("google_proxy");
    node.createdAt = TimeUtil::OptionalUtc::now();
    node.updatedAt = TimeUtil::OptionalUtc::now();
    return node;
}

void seedNodes(App::AppContext &context, int count)
{
    Models::CreateSource input;
    input.name = QStringLiteral("perf-seed");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const Result<Models::Source> created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    const QString sourceId = created.value().id;

    QVector<Models::Node> nodes;
    const QStringList regions = {QStringLiteral("HK"), QStringLiteral("JP"), QStringLiteral("US"),
                                 QStringLiteral("SG")};
    for (int index = 0; index < count; ++index) {
        nodes.push_back(seedNode(QStringLiteral("n-%1").arg(index, 3, 10, QLatin1Char('0')),
                                 QStringLiteral("NODE-%1").arg(index),
                                 QStringLiteral("10.0.%1.%2").arg(index / 256).arg(index % 256),
                                 regions.at(index % regions.size()), qint64(20 + index)));
    }
    for (Models::Node &node : nodes) {
        node.sourceId = sourceId;
    }
    const Result<int> written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk()) << written.error().toStdString();
}

QPushButton *pillFor(ProxyPage &page, const QString &code)
{
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String("Pill")
            && button->property("region").toString() == code) {
            return button;
        }
    }
    return nullptr;
}

qint64 timedRefresh(ProxyPage &page, int times)
{
    QElapsedTimer clock;
    clock.start();
    for (int index = 0; index < times; ++index) {
        page.refresh();
    }
    return clock.elapsed() / times;
}

}

/// 切到控制台为什么卡：refresh() 同步跑在 GUI 线程，而 Nodes 栏以前每次都把整网格
/// 全删全建（他机器上的实测：tab=0 的 refresh 179-182 ms）。这条守两件事，
/// 都不依赖机器快慢：
///   1) 数据/筛选/列数/选中/代理状态都没变 —— 一张卡都不许重建；
///   2) 筛选真变了 —— 必须重画（否则 1) 会退化成"界面永远不更新"）。
TEST(ProxyPagePerf, unchangedRefreshRebuildsNothing)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context, 60);

    auto *page = new ProxyPage(*context);
    page->resize(1100, 700);
    page->show();
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 5000 && page->rowCount() != 60) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
    ASSERT_EQ(page->rowCount(), 60) << "可用节点没灌进来，计时和计数都没有意义";

    const int loaded = page->gridRebuildCount();
    const qint64 skipMs = timedRefresh(*page, 5);
    EXPECT_EQ(page->gridRebuildCount(), loaded) << "内容没变还是重建了整网格";

    // 一次真重建的成本：走真入口（点地区药丸），不往生产类里塞测试专用 setter。
    QPushButton *jp = pillFor(*page, QStringLiteral("JP"));
    ASSERT_TRUE(jp != nullptr);
    QElapsedTimer rebuildClock;
    rebuildClock.start();
    jp->click();
    QCoreApplication::processEvents();
    const qint64 rebuildMs = rebuildClock.elapsed();
    EXPECT_GT(page->gridRebuildCount(), loaded) << "换了地区筛选却没重画";
    EXPECT_EQ(page->rowCount(), 15) << "JP 那一档应该有 15 个节点";

    std::cerr << "控制台 refresh(): 数据不变 " << skipMs << " ms（重建 0 次）/ 一次真重建 "
              << rebuildMs << " ms / 卡片 " << page->rowCount() << " 张" << std::endl;
    RecordProperty("skipRefreshMs", int(skipMs));
    RecordProperty("rebuildRefreshMs", int(rebuildMs));

    delete page;
}

/// 分栏切换 / 规则表 / 下拉框的成本量在纸面上，别靠手感。
/// 规则表是"每行一颗真 QComboBox"，N 条规则就是 N 颗；这就是它和内核表、下拉框一起被
/// 报"卡"的共同结构，先量化再决定怎么改。
TEST(ProxyPagePerf, rulesTableKeepsRowsCheapAndEditorLazy)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new ProxyPage(*context);
    page->resize(1180, 820);
    page->show();
    page->selectPanel(ProxyPage::Panel::Rules);
    QCoreApplication::processEvents();

    Proxy::MihomoConfigOptions options;
    options.routingMode = Proxy::RoutingMode::Rule;
    for (int i = 0; i < 120; ++i) {
        Proxy::TrafficRule rule;
        rule.matcher = Proxy::RuleMatcher::simple(
            Proxy::RuleMatcher::Kind::DomainSuffix, QStringLiteral("a%1.com").arg(i));
        rule.action = Proxy::RuleAction::Proxy;
        options.rules.push_back(rule);
    }

    QElapsedTimer clock;
    clock.start();
    page->setRulesConfigForTest(options);
    QCoreApplication::processEvents();
    const qint64 rebuildMs = clock.elapsed();

    // 结构不变量：行上是"单元格 + 委托"，不是一行一颗常驻下拉框。
    // 上一版每行一颗 QComboBox，120 行光是建控件就 1.7 s（实测 14 ms/行：造控件 0.2
    // + 填三项 1.3 + 自带 CSS 4.8 + 塞进表格 5.6），这就是"点进分流规则卡一下"。
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("RulesTable"));
    ASSERT_TRUE(table != nullptr);
    EXPECT_EQ(table->rowCount(), 120);
    EXPECT_TRUE(table->findChildren<QComboBox *>().isEmpty())
        << "规则表又变成每行一颗常驻下拉框了：编辑时才该现造";
    EXPECT_NE(table->itemDelegateForColumn(1), nullptr) << "动作列没挂委托";
    EXPECT_TRUE(table->model()->index(0, 1).flags().testFlag(Qt::ItemIsEditable))
        << "动作格不可编辑，用户就没法改 DIRECT/PROXY/REJECT";

    // 时间地板：按"曾经 1663 ms"往严里取，别拿当前实测值当门槛（那等于没门）。
    const qint64 maxRebuildMs = (qEnvironmentVariableIsSet("CI") || qEnvironmentVariableIsSet("GITHUB_ACTIONS")) ? 1200 : 700;
    EXPECT_LT(rebuildMs, maxRebuildMs) << "120 条规则重灌一次的成本涨了：行结构或控件装饰又变贵了";
    RecordProperty("rulesRebuild120Ms", int(rebuildMs));
    std::cerr << "PROBE 120 条规则重灌: " << rebuildMs << " ms（改造前 1663 ms）" << std::endl;

    // 内核栏：进一次面板原本要当场跑三次 `--version` 子进程（这台机器实测 318 ms），
    // 现在整块挪进 runAsync。判据两件事：进栏这一步不许再冻，行必须在稍后落回来。
    QElapsedTimer entryClock;
    entryClock.start();
    page->selectPanel(ProxyPage::Panel::Kernels);
    page->refresh();
    QCoreApplication::processEvents();
    const qint64 entryMs = entryClock.elapsed();
    RecordProperty("kernelEntryMs", int(entryMs));

    QElapsedTimer waitClock;
    waitClock.start();
    while (page->kernelRowCount() == 0 && waitClock.elapsed() < 8000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    const qint64 landedMs = waitClock.elapsed();
    EXPECT_GT(page->kernelRowCount(), 0) << "后台探测没把行送回来";
    // 地板按"曾经同步 318 ms"往严里取：进栏这一步必须比那次冻的时长还短一个量级。
    EXPECT_LT(entryMs, 60) << "进内核管理又变成同步探测了（实测 " << entryMs << " ms）";
    std::cerr << "PROBE 进内核管理: 同步部分 " << entryMs << " ms / 行落回 " << landedMs
              << " ms（改造前整个 318 ms 都冻在 GUI 线程上）" << std::endl;

    delete page;
}

int main(int argc, char *argv[])
{
    // 页面样式来自应用级样式表；Theme::apply() 有"同 id 直接返回"的守卫，
    // 所以这里补一次 setStyleSheet，免得量到"根本没有 QSS"的状态。
    QApplication app(argc, argv);
    Gui::Theme::apply(QString());
    qApp->setStyleSheet(Gui::Theme::styleSheet(Gui::Theme::current()));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
