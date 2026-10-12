#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <QHash>
#include <QImage>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabBar>
#include <QPixmap>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/FlowLayout.h"
#include "core/TimeUtil.h"
#include "db/DatabaseTelemetry.h"
#include "gui/CountryNames.h"
#include "gui/HealthStrip.h"
#include "gui/ProxyPage.h"
#include "gui/Widgets.h"
#include <QMenu>
#include "gui/TrafficSparkline.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "models/Node.h"
#include "models/Source.h"
#include "proxy/ProxyManager.h"
#include "sysproxy/Format.h"

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

/// 灌一个源 + 四个"已测通"的节点。可用节点那条查询要求 latency_ok = 1 且
/// latency_test_kind = 'google_proxy'，缺任何一个这一页就一条都取不到。
void seedNodes(App::AppContext &context)
{
    Models::CreateSource input;
    input.name = QStringLiteral("seed");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const Result<Models::Source> created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    const QString sourceId = created.value().id;
    QVector<Models::Node> nodes;
    nodes << seedNode(QStringLiteral("n-hk1"), QStringLiteral("HK-1"), QStringLiteral("1.1.1.1"),
                      QStringLiteral("HK"), qint64(90))
          << seedNode(QStringLiteral("n-hk2"), QStringLiteral("HK-2"), QStringLiteral("1.1.1.2"),
                      QStringLiteral("HK"), qint64(20))
          << seedNode(QStringLiteral("n-jp"), QStringLiteral("JP-1"), QStringLiteral("2.2.2.2"),
                      QStringLiteral("JP"), qint64(50))
          << seedNode(QStringLiteral("n-other"), QStringLiteral("VN-1"), QStringLiteral("3.3.3.3"),
                      QStringLiteral("VN"), qint64(60));
    for (Models::Node &node : nodes) {
        node.sourceId = sourceId;
    }
    const Result<int> written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk()) << written.error().toStdString();
}

/// 一条 minutesAgo 分钟前的出口实测（健康桶的输入就是库里的原文本）。
DbTelemetry::HealthRow healthRow(qint64 minutesAgo, bool ok)
{
    DbTelemetry::HealthRow row;
    row.at = TimeUtil::formatRfc3339(TimeUtil::nowUtcNanos() - minutesAgo * 60LL * 1000000000LL, false);
    row.ok = ok ? 1 : 0;
    return row;
}


ProxyPage *shownPage(std::shared_ptr<App::AppContext> &context, int rows)
{
    auto *page = new ProxyPage(*context);
    page->resize(900, 600);
    page->show();
    QElapsedTimer clock;
    clock.start();
    // 取数走 AppContext::runAsync：worker 读库、结果排队回 GUI 线程，
    // 所以要转事件循环等到行数到位，不能假设它是同步的。
    while (clock.elapsed() < 4000 && page->rowCount() != rows) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(page->rowCount(), rows) << "栅格没等到预期行数：可用节点查询没接上";
    return page;
}

QVector<QPushButton *> regionPills(ProxyPage &page)
{
    QVector<QPushButton *> out;
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String("Pill") && button->property("region").isValid()) {
            out.push_back(button);
        }
    }
    return out;
}

QPushButton *pillFor(ProxyPage &page, const QString &code)
{
    for (QPushButton *button : regionPills(page)) {
        if (button->property("region").toString() == code) {
            return button;
        }
    }
    return nullptr;
}

QPushButton *sortPill(ProxyPage &page, const QString &label)
{
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String("Pill") && !button->property("region").isValid()
            && button->text() == label) {
            return button;
        }
    }
    return nullptr;
}

}

TEST(ProxyPageNodes, loadsVerifiedNodesAndShowsEightRegionPillsInOrder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    const QVector<QPushButton *> pills = regionPills(*page);
    ASSERT_EQ(pills.size(), 8);
    const QStringList codes = page->regionPillCodes();
    const QStringList labels = {UiText::regionAll(), UiText::regionHK(),  UiText::regionJP(),
                                UiText::regionUS(),  UiText::regionSG(),  UiText::regionTW(),
                                UiText::regionKR(),  UiText::regionOther()};
    for (int index = 0; index < codes.size(); ++index) {
        EXPECT_EQ(pills.at(index)->property("region").toString(), codes.at(index));
        EXPECT_EQ(pills.at(index)->text(), labels.at(index));
    }
    EXPECT_TRUE(pills.at(0)->isChecked());
    EXPECT_EQ(page->regionFilter().toStdString(), std::string("all"));
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, pillAndSearchNarrowTheGrid)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    pillFor(*page, QStringLiteral("HK"))->click();
    EXPECT_EQ(page->rowCount(), 2);
    EXPECT_EQ(page->regionFilter().toStdString(), std::string("HK"));
    // 选中态只有一个是"地区"药丸亮着。
    int checked = 0;
    for (QPushButton *pill : regionPills(*page)) {
        checked += pill->isChecked() ? 1 : 0;
    }
    EXPECT_EQ(checked, 1);

    auto *search = page->findChild<QLineEdit *>();
    ASSERT_TRUE(search != nullptr);
    search->setText(QStringLiteral("hk-2"));
    QCoreApplication::processEvents();
    EXPECT_EQ(page->rowCount(), 1);
    search->clear();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->rowCount(), 2);
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, sortPillsToggleTheSortKey)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    EXPECT_EQ(page->sortBy(), ProxySortBy::Latency);
    sortPill(*page, UiText::sortByName())->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->sortBy(), ProxySortBy::Name);
    sortPill(*page, UiText::sortByLatency())->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->sortBy(), ProxySortBy::Latency);
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, countLabelReportsTheLoadedTotalNotTheFilteredRows)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    const QString four = SysProxy::fillTemplate(UiText::nodeCountTotal(), {}, {QStringLiteral("4")});
    EXPECT_EQ(page->countText(), four);
    pillFor(*page, QStringLiteral("JP"))->click();
    EXPECT_EQ(page->rowCount(), 1);
    // "共 N 个节点"跟着的是手里这批，不是过滤后剩几行。
    EXPECT_EQ(page->countText(), four);
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, regionRowWrapsInsteadOfBeingClipped)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    QWidget *regionRow = regionPills(*page).first()->parentWidget();
    auto *layout = dynamic_cast<FlowLayout *>(regionRow->layout());
    ASSERT_TRUE(layout != nullptr);
    const int wide = layout->heightForWidth(1400);
    const int narrow = layout->heightForWidth(240);
    // Rust 用 horizontal_wrapped 的理由：窄了不换行就是"被裁掉就等于没有"。
    EXPECT_GT(narrow, wide);

    page->resize(260, 500);
    QCoreApplication::processEvents();
    for (QPushButton *pill : regionPills(*page)) {
        EXPECT_LE(pill->geometry().right(), regionRow->width() + 2)
            << pill->text().toStdString();
    }
    page->hide();
    delete page;
}

/// 健康条的桶语义（移植 Rust widgets.rs 的同名测试）：小时槽、失败优先、窗口边界。
TEST(HealthStripBuckets, hourlyFailureWinsAndWindowBound)
{
    const qint64 now = TimeUtil::nowUtcNanos();
    EXPECT_EQ(healthHourBuckets({}, now), QVector<qint8>(24, 0)) << "没数据就是 24 格全空";

    const QVector<qint8> one = healthHourBuckets({healthRow(10, true)}, now);
    EXPECT_EQ(one.at(0), 1) << "十分钟前的实测落在当前这一格";
    EXPECT_EQ(one.at(1), 0);

    EXPECT_EQ(healthHourBuckets({healthRow(90, false)}, now).at(1), -1) << "一个半小时前是上一格";
    EXPECT_EQ(healthHourBuckets({healthRow(5, true), healthRow(20, false)}, now).at(0), -1)
        << "同一小时里失败优先，绿不能把红盖掉";
    EXPECT_EQ(healthHourBuckets({healthRow(24 * 60 + 5, false)}, now), QVector<qint8>(24, 0))
        << "超出 24 小时的行不该挤进最老那一格";
    // chrono 的 num_hours() 向零截断：一小时以内的时钟漂移落进当前格，超过一小时才丢。
    EXPECT_EQ(healthHourBuckets({healthRow(-30, false)}, now).at(0), -1)
        << "半小时内飘到未来的行落在当前格，不是越界";
    EXPECT_EQ(healthHourBuckets({healthRow(-90, false)}, now), QVector<qint8>(24, 0))
        << "一小时以上的未来行必须丢，否则索引是负数";
}

/// #32：状态卡上「节点详情」和「延迟:」两行以前是死的，现在跟着选中节点走。
/// 取数用整表而不是筛选后的网格：选中的节点被地区药丸筛掉时这两行照样要说真话
/// （Rust views/proxy.rs:126-140、216-228 用的就是 self.nodes 全量）。
TEST(ProxyPageStatusCard, selectedNodeFillsDetailAndLatency)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    EXPECT_EQ(page->statusLatencyText(), QStringLiteral("延迟: -"));
    EXPECT_TRUE(page->statusNodeDetailText().isEmpty()) << "没选中节点时这一段在 Rust 里不渲染";

    page->setSelectedNode(QStringLiteral("n-hk1"), QStringLiteral("HK-1"));
    const CountryDisplay hk = countryFlagAndName(QStringLiteral("HK"));
    EXPECT_EQ(page->statusNodeDetailText(),
              QStringLiteral("%1 %2 · 1.1.1.1:443").arg(hk.flag, hk.name));
    EXPECT_EQ(page->statusLatencyText(), QStringLiteral("延迟: 90 ms"));

    // 把网格筛到日本：HK-1 不在视图里了，顶栏那两行不许跟着变空。
    pillFor(*page, QStringLiteral("JP"))->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->regionFilter(), QStringLiteral("JP"));
    EXPECT_EQ(page->statusNodeDetailText(),
              QStringLiteral("%1 %2 · 1.1.1.1:443").arg(hk.flag, hk.name));
    EXPECT_EQ(page->statusLatencyText(), QStringLiteral("延迟: 90 ms"));

    // 没在跑：健康条整条收起（Rust 只在 Running/Degraded 才画）。
    EXPECT_FALSE(page->healthStripVisible());
    delete page;
}


/// 曲线数学的两个纯函数（Rust widgets.rs::chart_scale / smooth_polyline）。
TEST(TrafficSparklineMath, scaleIsP92HeadroomAndFloor)
{
    EXPECT_DOUBLE_EQ(chartScale({}), 64.0) << "没有点也要有一个能画的轴";
    EXPECT_DOUBLE_EQ(chartScale({1.0, 2.0}), 64.0) << "峰值再小也不把轴缩到 64 以下";
    // 100 个点 1..100：p92 取第 92 个（0 基索引 92 = 值 93），再放 1.25 倍。
    QVector<double> hundred;
    for (int i = 1; i <= 100; ++i) hundred.push_back(i);
    EXPECT_DOUBLE_EQ(chartScale(hundred), 93.0 * 1.25);
}

TEST(TrafficSparklineMath, smoothingKeepsEndpointsAndSubdivides)
{
    const QVector<QPointF> two = {QPointF(0, 0), QPointF(10, 4)};
    EXPECT_EQ(smoothPolyline(two, 4), two) << "两个点没什么可弯的";

    const QVector<QPointF> three = {QPointF(0, 0), QPointF(10, 8), QPointF(20, 1)};
    const QVector<QPointF> out = smoothPolyline(three, 4);
    EXPECT_EQ(out.size(), 2 * 4 + 1) << "每段插 4 个点，末尾再补最后一个原点";
    EXPECT_DOUBLE_EQ(out.first().x(), 0.0);
    EXPECT_DOUBLE_EQ(out.last().x(), 20.0) << "末点必须是原末点，否则曲线会缩一头";
}

/// #29：状态卡第二行右侧的实时曲线 + 第三行的「查看切换记录」入口。
TEST(ProxyPageStatusCard, trafficCurveAndSwitchLink)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    ASSERT_TRUE(page->findChild<TrafficSparkline *>() != nullptr);
    // 代理没在跑：一个点都不采（Rust mod.rs:1301 只认严格 Running）。
    EXPECT_EQ(page->trafficPointCount(), 0);
    // 切换记录是点一下才查库，所以初始就是"今日 0 次"，不是没这一行。
    EXPECT_TRUE(page->switchLinkText().contains(QStringLiteral("今日 0 次")))
        << page->switchLinkText().toStdString();
    // 代理没在跑的时候灌历史：整条要被抹平，不许留旧曲线冒充实时（Rust mod.rs:1301-1313）。
    QVector<QPair<double, double>> points;
    for (int i = 0; i < 60; ++i) {
        points.push_back(QPair<double, double>(200.0 + 120.0 * ((i / 3) % 7), 40.0 + (i % 5) * 18.0));
    }
    page->setTrafficHistoryForTest(points);
    EXPECT_EQ(page->trafficPointCount(), 0) << "离开 Running 必须把历史和读数一起清零";

    // 读数在曲线的下一行（第二行整条让给图）。
    ASSERT_GT(page->statusTrafficY(), 0);
    ASSERT_GT(page->statusRatesY(), 0);
    EXPECT_GT(page->statusRatesY(), page->statusTrafficY());
    delete page;
}

/// 他 2026-10-12 点名：「查看切换记录（今日 N 次）」要换绿色字体。
/// 为什么只看像素：`<a>` 的取色走 Qt 富文本的链接色，QSS 上那句 `color: {success}`
/// **压不住它**（我先把槽从 accent 改成 success，界面上仍是蓝的）。判据也不能写成
/// "底色对不对"那种像素比较 —— 卡片是浅色、链接色是彩色，所以这里只统计**有彩度**的
/// 墨迹像素，按"离 success 近还是离 accent 近"分类（同 `AllPageColors` 的语义判据教训）。
TEST(ProxyPageStatusCard, switchLinkActuallyPaintsGreenNotTheLinkBlue)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    QWidget *link = page->switchLinkWidget();
    ASSERT_TRUE(link != nullptr);
    // 取色用 render() 而不是 grab()：grab 可能抓到调色板自动铺的背景（NodeCard 踩过）。
    QImage image(link->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    link->render(&image);

    const QColor success = Theme::current().success;
    const QColor accent = Theme::current().accent;
    const auto distance = [](const QColor &a, const QColor &b) {
        const int dr = a.red() - b.red();
        const int dg = a.green() - b.green();
        const int db = a.blue() - b.blue();
        return dr * dr + dg * dg + db * db;
    };
    int chromatic = 0;
    int green = 0;
    int blue = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 40) {
                continue;
            }
            const int chroma = std::max({pixel.red(), pixel.green(), pixel.blue()})
                - std::min({pixel.red(), pixel.green(), pixel.blue()});
            if (chroma < 40) {
                continue;   // 卡片底、正文这类近中性像素不算"链接的颜色"
            }
            ++chromatic;
            if (distance(pixel, success) < distance(pixel, accent)) {
                ++green;
            } else {
                ++blue;
            }
        }
    }
    std::cout << "switch link pixels: chromatic=" << chromatic
              << " green=" << green << " blue=" << blue << std::endl;
    EXPECT_GT(chromatic, 30) << "一个字都没画出来，这条测试等于没测";
    EXPECT_GT(green, blue * 4) << "切换记录入口没画成绿色";

    // 反向把锚点 style 里的色去掉，上面的像素断言当场红；这条守的是"色值确实来自调色板"，
    // 免得有人把十六进制写死进 HTML（那样切主题就不跟了）。
    EXPECT_TRUE(page->switchLinkText().contains(success.name(QColor::HexRgb), Qt::CaseInsensitive))
        << page->switchLinkText().toStdString();
    delete page;
}

/// 对照用例（同 #12 的判据）：同一夹具里造一颗**只写 `<a href>`、没有 inline style** 的
/// 标签，量出来必须"不是绿色"。上面那条 `green > blue*4` 只有在判据真的能分辨
/// "Qt 富文本的链接色"和"调色板的 success"时才有意义 —— 否则它就是永远绿的空断言。
TEST(ProxyPageStatusCard, plainAnchorStillFallsBackToTheLinkBlue)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    const QColor success = Theme::current().success;
    const QColor accent = Theme::current().accent;
    const auto distance = [](const QColor &a, const QColor &b) {
        const int dr = a.red() - b.red();
        const int dg = a.green() - b.green();
        const int db = a.blue() - b.blue();
        return dr * dr + dg * dg + db * db;
    };

    // 不挂到 page 上：栈上的 QObject 一旦有父控件，而我们又先 `delete page`，父控件会
    // 替它析构，作用域结束时就是第二次销毁（debug heap 断言 `_CrtIsValidHeapBlock`）。
    QLabel control;
    control.setTextFormat(Qt::RichText);
    control.setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(success.name(QColor::HexRgb)));
    control.setText(QStringLiteral("<a href=\"switch\">%1</a>")
                        .arg(UiText::switchLogLink().arg(0)));
    control.adjustSize();
    control.show();
    QCoreApplication::processEvents();
    ASSERT_GE(control.width(), 20) << "对照标签没量出尺寸，像素判据无从谈起";
    QImage controlImage(control.size(), QImage::Format_ARGB32);
    controlImage.fill(Qt::transparent);
    control.render(&controlImage);
    int controlChromatic = 0;
    int controlGreen = 0;
    for (int y = 0; y < controlImage.height(); ++y) {
        for (int x = 0; x < controlImage.width(); ++x) {
            const QColor pixel = controlImage.pixelColor(x, y);
            if (pixel.alpha() < 40) {
                continue;
            }
            const int chroma = std::max({pixel.red(), pixel.green(), pixel.blue()})
                - std::min({pixel.red(), pixel.green(), pixel.blue()});
            if (chroma < 40) {
                continue;
            }
            ++controlChromatic;
            if (distance(pixel, success) < distance(pixel, accent)) {
                ++controlGreen;
            }
        }
    }
    std::cout << "control (bare <a>) pixels: chromatic=" << controlChromatic
              << " green=" << controlGreen << std::endl;
    ASSERT_GT(controlChromatic, 30);
    EXPECT_LT(controlGreen * 4, controlChromatic)
        << "对照标签也画成绿色了 —— 像素判据分辨不出链接色，那条绿色守卫不可信";
    delete page;
}


/// 他 2026-10-12：「所有卡片都正常大小，不要不一样」。
/// 根因是名字那行开了 wordWrap：长名字的卡换成两行，同一网格里卡高就齐不了
/// （Rust 的 compact_node_card 是 `add_sized([w, 20.0], Label::truncate())` —— 单行截断，
/// 完整名字走 hover）。现在两边都是 ElidedLabel + min-height 58。
TEST(ProxyPageNodes, nodeCardsKeepOneUniformHeight)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    // 一颗名字极长（会换行的那种）、一颗极短：正是要让它们撞出高度差。
    Models::CreateSource input;
    input.name = QStringLiteral("seed");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const auto created = context->database().createSource(input);
    ASSERT_TRUE(created.isOk());
    const QString sourceId = created.value().id;
    QVector<Models::Node> nodes;
    nodes << seedNode(QStringLiteral("n-tiny"), QStringLiteral("HK-1"), QStringLiteral("1.1.1.1"),
                      QStringLiteral("HK"), qint64(90))
           << seedNode(QStringLiteral("long-name"),
                       QStringLiteral("A") + QString(160, QChar('x')) + QStringLiteral("-tail"),
                       QStringLiteral("203.0.113.77"), QStringLiteral("HK"), qint64(30));
    nodes[1].sourceId = sourceId;
    nodes[0].sourceId = sourceId;
    ASSERT_TRUE(context->database().replaceSourceNodes(sourceId, nodes).isOk());

    ProxyPage *page = shownPage(context, 2);
    QList<NodeCardFrame *> cards;
    for (NodeCardFrame *card : page->findChildren<NodeCardFrame *>()) {
        if (card->isVisible()) {
            cards.push_back(card);
        }
    }
    ASSERT_GE(cards.size(), 2);
    int height = cards.first()->height();
    ASSERT_GT(height, 0);
    for (NodeCardFrame *card : cards) {
        EXPECT_EQ(card->height(), height)
            << card->property("nodeName").toString().left(12).toStdString() << " 高度不一样";
    }

    // 长名字那颗：卡内不许长出子控件（四段文字画在 paintEvent 里），但完整名字必须
    // 还在属性上 —— 截断只发生在绘制时，菜单/详情/测速都还要用全名。
    NodeCardFrame *longCard = nullptr;
    for (NodeCardFrame *card : cards) {
        if (card->property("nodeId").toString() == QStringLiteral("long-name")) {
            longCard = card;
        }
    }
    ASSERT_TRUE(longCard != nullptr);
    EXPECT_TRUE(longCard->findChildren<QWidget *>().isEmpty());
    EXPECT_EQ(longCard->property("nodeName").toString(), nodes[1].name);
    EXPECT_LT(longCard->sizeHint().width(), longCard->width())
        << "卡比内容还窄，说明列宽不是等分出来的";
    delete page;
}

/// 他 2026-10-12：「控制台的卡片要像节点池一样可以右击」。Rust proxy.rs:700 的
/// context_menu 就是那四条：查看详情 / 测速节点 / IP 检测 /（自动选路开着才给）锁定为出口。
/// 这里只 build 不 exec —— exec 会阻塞测试，也会把菜单拍到他的桌面上。
TEST(ProxyPageNodes, nodeCardsCarryRustsRightClickMenu)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    NodeCardFrame *card = nullptr;
    for (NodeCardFrame *candidate : page->findChildren<NodeCardFrame *>()) {
        if (candidate->isVisible()) {
            card = candidate;
            break;
        }
    }
    ASSERT_TRUE(card != nullptr);
    const QString nodeId = card->property("nodeId").toString();
    ASSERT_FALSE(nodeId.isEmpty());

    // hover 提示要告诉他右键有东西（Rust 的两条 on_hover_text）。
    EXPECT_EQ(card->toolTip(), UiText::cardHoverPlain()) << card->toolTip().toStdString();

    QMenu *menu = page->buildNodeCardMenu(card);
    ASSERT_TRUE(menu != nullptr);
    QStringList actions;
    for (QAction *action : menu->actions()) {
        if (!action->isSeparator()) {
            actions << action->text();
        }
    }
    // 自动选路（url-test）没开，所以"锁定为出口"不该出现。
    EXPECT_EQ(actions, (QStringList{UiText::menuDetails(), UiText::menuSpeedtest(),
                                    UiText::menuIpRisk()}));

    QStringList details;
    QStringList speedtests;
    QStringList risks;
    QObject::connect(page, &ProxyPage::nodeDetailsRequested,
            [&details](const QString &id) { details << id; });
    QObject::connect(page, &ProxyPage::nodeSpeedtestRequested,
            [&speedtests](const QString &id) { speedtests << id; });
    QObject::connect(page, &ProxyPage::nodeIpRiskRequested,
            [&risks](const QString &id) { risks << id; });
    for (QAction *action : menu->actions()) {
        action->trigger();
    }
    EXPECT_EQ(details, QStringList() << nodeId);
    EXPECT_EQ(speedtests, QStringList() << nodeId);
    EXPECT_EQ(risks, QStringList() << nodeId);
    delete menu;
    delete page;
}

/// 池化复用"只藏不删"之后，必须把复用的那几张重新显示出来 —— 否则会出现
/// "计数有数、卡片一张都没有"（他 2026-10-11 报的现象）。
/// 现场时序：全量测速中途 tcp 预筛把 latency_test_kind 全改成 tcp_prefilter，可用节点查询
///  momentarily 返回 0 行 → 那一轮把 50 张卡全 hide；测速跑完 33 颗回来 → 卡片被重新摆进
/// 布局但没人 show，于是计数显示 33 而界面空白。
TEST(ProxyPageNodes, cardsBecomeVisibleAgainAfterThePoolEmptiesOnce)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);          // HK x2 / JP x1 / VN x1，没有 US
    ProxyPage *page = shownPage(context, 4);
    ASSERT_EQ(page->visibleCardCount(), 4);

    // 先点一个筛不出任何行的药丸：这一轮 rebuildNodeGrid 会把池里的卡全部 hide()。
    pillFor(*page, QStringLiteral("US"))->click();
    QCoreApplication::processEvents();
    ASSERT_EQ(page->rowCount(), 0);
    ASSERT_EQ(page->visibleCardCount(), 0);

    // 再点回来：卡片被重新摆进布局，但"在布局里"不等于"看得见"。
    pillFor(*page, QStringLiteral("HK"))->click();
    QCoreApplication::processEvents();
    const int rows = page->rowCount();
    const int inLayout = page->nodePageCardCount();
    const int visible = page->visibleCardCount();
    delete page;
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(inLayout, 2) << "卡片没被重新摆进网格";
    EXPECT_EQ(visible, 2) << "计数有 " << rows << " 个数、网格里有 " << inLayout
                          << " 张卡，但它们全都还藏着 —— 界面就是空白一片";
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
