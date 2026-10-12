#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QPixmap>
#include <QSize>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "app/AppContext.h"
#include "app/MainWindow.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "gui/Icons.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "speedtest/SpeedtestTypes.h"

namespace {

/// 离屏跑：这台机器上 mihomo 内核可能是别的实例在用的，而 main() 会先清孤儿内核，
/// 所以这里绕开 main()，只验证窗口结构与主题装配。
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

}

TEST(GuiShell, windowMatchesRustNativeOptions)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    EXPECT_EQ(window.windowTitle().toStdString(), std::string("VCC"));
    // eframe NativeOptions：inner 1330x800，min 960x600。
    EXPECT_EQ(window.width(), 1330);
    EXPECT_EQ(window.height(), 800);
    EXPECT_EQ(window.minimumWidth(), 960);
    EXPECT_EQ(window.minimumHeight(), 600);
}

TEST(GuiShell, sidebarListsFiveTabsInRustOrder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    const QList<QPushButton *> nav = window.findChildren<QPushButton *>();
    QList<QPushButton *> items;
    for (QPushButton *button : nav) {
        if (button->objectName() == QLatin1String("Nav")) {
            items.push_back(button);
        }
    }
    ASSERT_EQ(items.size(), 5);
    // 顺序抄自 views/sidebar.rs：控制台、节点池、订阅源、系统设置、外部接口。
    const QStringList expected = {Gui::UiText::navConsole(),  Gui::UiText::navNodePool(),
                                  Gui::UiText::navSubscriptions(),
                                  Gui::UiText::navSystemSettings(), Gui::UiText::navExternalApi()};
    for (int index = 0; index < items.size(); ++index) {
        EXPECT_EQ(items.at(index)->text().toStdString(), expected.at(index).toStdString());
        EXPECT_EQ(items.at(index)->property("tab").toInt(), index);
        EXPECT_TRUE(items.at(index)->isCheckable());
        EXPECT_EQ(items.at(index)->isChecked(), index == 0);
    }
}

TEST(GuiShell, selectingTabSwitchesStackAndCheckedItem)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    const QList<QPushButton *> nav = window.findChildren<QPushButton *>();
    ASSERT_GE(nav.size(), Gui::kTabCount);

    window.selectTab(Gui::Tab::Api);
    EXPECT_EQ(window.currentTab(), Gui::Tab::Api);
    int checked = 0;
    for (QPushButton *button : nav) {
        if (button->objectName() == QLatin1String("Nav") && button->isChecked()) {
            checked += 1;
        }
    }
    EXPECT_EQ(checked, 1);
    // Rust 默认进控制台，其它页都在这之后按 nav 点击切换。
    window.selectTab(Gui::Tab::Proxy);
    EXPECT_EQ(window.currentTab(), Gui::Tab::Proxy);
}

TEST(GuiShell, themeDerivesOnePalettePerAccentAndFallsBackToDefault)
{
    using namespace Gui;
    ASSERT_EQ(Theme::themes().size(), 5);
    EXPECT_EQ(Theme::kDefaultThemeId.toStdString(), std::string(Theme::themes().first().id.toStdString()));

    // 未知 id（旧主题被删过）必须回到默认那套，而不是随机挑一套。
    for (const char *legacy : {"obsidian", "latte", "midnight", "mocha", "rosepine", "linear", ""}) {
        EXPECT_EQ(Theme::palette(QString::fromLatin1(legacy)).accent.rgb(),
                  Theme::palette(Theme::kDefaultThemeId).accent.rgb())
            << legacy;
    }

    const Theme::Palette base = Theme::palette(Theme::kDefaultThemeId);
    EXPECT_TRUE(base.light);
    for (const Theme::Definition &theme : Theme::themes()) {
        const Theme::Palette pal = Theme::palette(theme.id);
        // 结构共用一份：改一套配色只改强调色。
        EXPECT_EQ(pal.pageBg.rgb(), base.pageBg.rgb()) << theme.id.toStdString();
        EXPECT_EQ(pal.panel.rgb(), base.panel.rgb()) << theme.id.toStdString();
        EXPECT_EQ(pal.textPrimary.rgb(), base.textPrimary.rgb()) << theme.id.toStdString();
        EXPECT_EQ(pal.success.rgb(), base.success.rgb()) << theme.id.toStdString();
        EXPECT_EQ(Theme::contrast(pal.onPrimary, pal.primaryBtn) >= 4.5, true)
            << theme.id.toStdString();
        // 页面上不许有红和黄：严重度靠字重与措辞，不靠颜色。
        EXPECT_LE(pal.danger.red(), 90) << theme.id.toStdString();
        EXPECT_LE(pal.danger.green(), 110) << theme.id.toStdString();
        EXPECT_LE(pal.warning.red(), 130) << theme.id.toStdString();
        EXPECT_GE(Theme::contrast(pal.panel, pal.pageBg), 1.12) << theme.id.toStdString();
        for (std::pair<const char *, QColor> slot :
             {std::pair{"success_bg", pal.successBg}, {"warning_bg", pal.warningBg},
              {"danger_bg", pal.dangerBg}}) {
            EXPECT_GE(Theme::contrast(pal.textPrimary, slot.second), 3.0)
                << theme.id.toStdString() << " " << slot.first;
        }
    }
    // 五套强调色两两得分得出来。
    for (int index = 0; index < Theme::themes().size(); ++index) {
        for (int other = index + 1; other < Theme::themes().size(); ++other) {
            const QColor a = Theme::palette(Theme::themes().at(index).id).accent;
            const QColor b = Theme::palette(Theme::themes().at(other).id).accent;
            const int delta = qAbs(a.red() - b.red()) + qAbs(a.green() - b.green())
                            + qAbs(a.blue() - b.blue());
            EXPECT_GT(delta, 60) << Theme::themes().at(index).id.toStdString() << " vs "
                                 << Theme::themes().at(other).id.toStdString();
        }
    }
}

TEST(GuiShell, applyingThemeInstallsStylesheetForWholeApp)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::Theme::apply(QStringLiteral("soft-teal"));
    EXPECT_EQ(Gui::Theme::currentId().toStdString(), std::string("soft-teal"));
    auto *app = qobject_cast<QApplication *>(QApplication::instance());
    ASSERT_TRUE(app != nullptr);
    const QString css = app->styleSheet();
    EXPECT_FALSE(css.isEmpty());
    // 淡蓝底 + 压深后的字：这两条是 Rust 那边守了两个真 bug 之后的形状。
    EXPECT_NE(css.indexOf(QStringLiteral("QFrame#Card")), -1);
    EXPECT_NE(css.indexOf(QStringLiteral("QPushButton#Primary")), -1);

    QFrame card;
    Gui::Theme::decorateCard(&card);
    EXPECT_EQ(card.objectName().toStdString(), std::string("Card"));
    // 分层靠样式表里 QFrame#Card 的描边 + 页面底色差，不再挂 QGraphicsDropShadowEffect：
    // 每张卡一个图形效果会让 Qt 离屏合成整棵子树，节点网格几十张卡 + 轮询重绘 = 界面卡死，
    // 而且 blur 溢出到卡片间距外会糊到邻居（用户 2026-10-09 报的"超级卡 + 互相挡住"）。
    EXPECT_TRUE(card.graphicsEffect() == nullptr);
    // 深色那套（现在只剩浅色，这里是防御）。
    Gui::Theme::apply(QStringLiteral("soft-cobalt"));
}

/// 侧栏那两处间距（用户 2026-10-10：控制台离上面图标太近、图标左边也要留白）。
/// 本机截不到图，所以直接量几何：图标左边缘的窗口坐标 = 侧栏 12px 内边距 + Rust
/// 品牌行里的 add_space(8)，第一条导航项与图标之间必须留出 add_space(14)+分隔线
/// +add_space(10) 那一段。
TEST(GuiShell, sidebarKeepsBrandSpacingFromRust)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QLabel *logo = window.findChild<QLabel *>(QStringLiteral("BrandLogo"));
    ASSERT_TRUE(logo != nullptr);
    const QList<QPushButton *> navs = window.findChildren<QPushButton *>(QStringLiteral("Nav"));
    ASSERT_FALSE(navs.isEmpty());

    const QPoint logoAt = logo->mapTo(&window, QPoint(0, 0));
    const QPoint firstNavAt = navs.first()->mapTo(&window, QPoint(0, 0));
    const int gap = firstNavAt.y() - (logoAt.y() + logo->height());

    EXPECT_GE(logoAt.x(), 20) << "图标左边没有留白";
    EXPECT_GE(gap, 24) << "第一条导航项（控制台）贴着品牌行";

    // 他要的"图标大一点、上下间距多一点"（对 Rust 32px/4px 的有意偏离）：钉住下限，
    // 以后有人为了对齐 Rust 把它们缩回去，这里会红。
    EXPECT_GE(logo->width(), 40) << "品牌图标被缩回 Rust 的 32px";
    EXPECT_GE(logo->height(), 40) << "品牌图标被缩回 Rust 的 32px";
    ASSERT_GE(navs.size(), 2);
    const int navGap = navs.at(1)->mapTo(&window, QPoint(0, 0)).y()
        - (navs.first()->mapTo(&window, QPoint(0, 0)).y() + navs.first()->height());
    // 他这一轮要的是"上下菜单的间距小一点点"：10 → 6。钉成区间，既不许被人为了
    // 对齐 Rust 的 4 压回零，也不许有人再把它加回松垮的 10。
    // 间距他来回调过三次：10（"多一点"）→ 6（"小一点点"）→ 9（"再多点"）。
    // 区间两头都拦：不许压回 Rust 的 3-4，也不许再涨回 10 以上。
    EXPECT_GE(navGap, 8) << "导航条目之间的间距又被压小了";
    EXPECT_LE(navGap, 10) << "导航条目间距又涨回松垮的那一档";
}

namespace {

/// 图标是渲成位图的，所以断言只能落在像素上：有墨的像素数、最多的那一个实心色、
/// 以及"哪些位置有墨"的形状指纹（五枚挂成同一枚时指纹会撞）。
int inkCount(const QImage &image)
{
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                ++ink;
            }
        }
    }
    return ink;
}

QRgb dominantInk(const QImage &image)
{
    QHash<QRgb, int> counts;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 255) {
                counts[line[x]] += 1;
            }
        }
    }
    QRgb best = 0;
    int bestCount = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return best;
}

quint64 maskHash(const QImage &image)
{
    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            hash = (hash ^ quint64(qAlpha(line[x]) > 8 ? 1 : 0)) * 1099511628211ULL;
        }
    }
    return hash;
}

/// 有墨像素的包围盒。它守的是"图标被裁掉一块"这一类：render() 里如果把 DPR 坐标算错，
/// 图形会整块落到画布外，界面上只剩左上四分之一 —— 墨量、指纹、取色三条全都还是绿的
/// （2026-10-10 他截图才发现）。包围盒宽度不到画布一半就是被裁了。
QRect inkBounds(const QImage &image)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                bounds |= QRect(x, y, 1, 1);
            }
        }
    }
    return bounds;
}

int channelSpread(QRgb left, QRgb right)
{
    return std::max({std::abs(qRed(left) - qRed(right)), std::abs(qGreen(left) - qGreen(right)),
                     std::abs(qBlue(left) - qBlue(right))});
}

} // namespace

/// Rust 的五条 nav_item 各带一枚矢量图标（views/sidebar.rs:61-65 的
/// Icon::Dashboard / Lightning / Subscription / Gear / Plug）。这里钉三件事：挂的是不是
/// 那五枚（顺序 + 互不相同）、有没有真的画出墨来、颜色是不是按 Rust nav_item 的
/// "选中=accent / 未选中=sidebar_muted" 走，并且换主题会重渲。
TEST(GuiShell, sidebarNavItemsCarryRustsFiveIcons)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::Theme::apply(QStringLiteral("soft-teal"));
    Gui::MainWindow window(*context);
    window.show();
    QCoreApplication::processEvents();

    const QList<QPushButton *> items = window.findChildren<QPushButton *>(QStringLiteral("Nav"));
    ASSERT_EQ(items.size(), 5);
    const Gui::Theme::Palette first = Gui::Theme::current();

    QVector<quint64> masks;
    for (int index = 0; index < items.size(); ++index) {
        QPushButton *button = items.at(index);
        // kind 的取值就是 Icons::Kind 的枚举序（Dashboard=0…Plug=4），顺序抄 Rust。
        ASSERT_TRUE(button->property("navIconKind").canConvert<int>())
            << "第 " << index << " 条导航没有图标";
        ASSERT_EQ(button->property("navIconKind").toInt(), index);
        ASSERT_FALSE(button->icon().isNull());
        const int iconEdge = Gui::Icons::kNavIconSize;
        ASSERT_EQ(button->iconSize(), QSize(iconEdge, iconEdge))
            << "导航图标尺寸和 Icons::kNavIconSize 不一致";

        // 按 dpr 取图：`pixmap(size)` 那个重载只会给你 size 那么大的设备像素（DPR 变 1），
        // 要验"高清渲染"必须走 pixmap(size, devicePixelRatio)。
        const QPixmap pixmap = button->icon().pixmap(QSize(iconEdge, iconEdge), 2.0);
        ASSERT_EQ(pixmap.devicePixelRatio(), 2.0);
        ASSERT_EQ(pixmap.width(), iconEdge * 2);
        const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        ASSERT_GE(inkCount(image), 40) << "第 " << index << " 枚图标几乎是空的";
        // 墨的包围盒必须落在画布正中：DPR 坐标一算错，图形会整块偏到某个角
        // （被裁成左上四分之一时包围盒中心在 25% 处，这条直接红）。
        const QRect bounds = inkBounds(image);
        const qreal wantX = image.width() / 2.0, wantY = image.height() / 2.0;
        ASSERT_LE(std::abs(bounds.center().x() - wantX), image.width() * 0.08)
            << "第 " << index << " 枚图标的墨偏了：bbox=" << bounds.x() << ".." << bounds.right()
            << " 画布宽 " << image.width();
        ASSERT_LE(std::abs(bounds.center().y() - wantY), image.height() * 0.08)
            << "第 " << index << " 枚图标的墨偏了：bbox=" << bounds.y() << ".." << bounds.bottom()
            << " 画布高 " << image.height();
        masks.push_back(maskHash(image));

        const QRgb expected = button->isChecked() ? first.accent.rgba() : first.sidebarMuted.rgba();
        EXPECT_LE(channelSpread(dominantInk(image), expected), 2)
            << "第 " << index << " 枚图标的颜色没跟着选中态走";
    }
    for (int left = 0; left < masks.size(); ++left) {
        for (int right = left + 1; right < masks.size(); ++right) {
            EXPECT_NE(masks.at(left), masks.at(right)) << "第 " << left << " 与 " << right
                                                      << " 条挂的是同一枚图标";
        }
    }

    // 五套主题只有强调色不同（sidebarMuted 是中性色派生的，跨主题一样），所以"换主题
    // 要重渲图标"这条只能拿选中那枚来验：它的色必须跟着新主题的 accent 走。
    Gui::Theme::apply(QStringLiteral("soft-cobalt"));
    QCoreApplication::processEvents();
    const Gui::Theme::Palette second = Gui::Theme::current();
    ASSERT_NE(second.accent.rgba(), first.accent.rgba());
    {
        const QImage image = items.first()->icon().pixmap(QSize(Gui::Icons::kNavIconSize, Gui::Icons::kNavIconSize), 2.0).toImage()
                                 .convertToFormat(QImage::Format_ARGB32);
        EXPECT_LE(channelSpread(dominantInk(image), second.accent.rgba()), 2)
            << "切主题后选中的导航图标还留着上一个主题的强调色";
    }

    // 选中态一变，两枚图标的颜色要立刻换角色（Rust 的 nav_item 每次重绘都重新取色）。
    items.at(1)->setChecked(true);
    QCoreApplication::processEvents();
    const QImage selected = items.at(1)->icon().pixmap(QSize(Gui::Icons::kNavIconSize, Gui::Icons::kNavIconSize), 2.0).toImage()
                                .convertToFormat(QImage::Format_ARGB32);
    const QImage idle = items.first()->icon().pixmap(QSize(Gui::Icons::kNavIconSize, Gui::Icons::kNavIconSize), 2.0).toImage()
                            .convertToFormat(QImage::Format_ARGB32);
    EXPECT_LE(channelSpread(dominantInk(selected), second.accent.rgba()), 2)
        << "刚被选中的导航项没有换成 accent";
    EXPECT_LE(channelSpread(dominantInk(idle), second.sidebarMuted.rgba()), 2)
        << "被取消选中的导航项没有退回 sidebar_muted";
    Gui::Theme::apply(QStringLiteral("soft-teal"));
}

/// 拉完自动测速的判据：开关关着不测；开着但这次一个节点都没解析出来也不测
/// （对着空池跑一轮测速只会让状态条转几十秒）。
TEST(GuiShell, autoSpeedtestOnlyFiresWhenEnabledAndNodesArrive)
{
    EXPECT_FALSE(Gui::MainWindow::shouldAutoSpeedtest(false, 354));
    EXPECT_FALSE(Gui::MainWindow::shouldAutoSpeedtest(true, 0));
    EXPECT_FALSE(Gui::MainWindow::shouldAutoSpeedtest(true, -1));
    EXPECT_TRUE(Gui::MainWindow::shouldAutoSpeedtest(true, 1));
}

/// 底部状态栏那颗「测速全部」必须跟着节点池的强度下拉框走。Rust 全应用只有
/// 一个 `speedtest_intensity`，每一条测速入口都从同一个 `speed_opts()` 取参数；
/// 我们这边一旦把它钉成默认档，同一句「测速全部」就会在两个入口用两个超时 ——
/// 表现正是"另一个客户端测得到的节点，vcc 测不到"（挂在 Cloudflare Worker 后面的
/// 出口冷启动可达数秒，2500ms 的标准档必挂）。
TEST(GuiShell, bottomBarTestAllFollowsTheNodesPageIntensityKnob)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    QComboBox *knob = nullptr;
    const QList<QComboBox *> boxes = window.findChildren<QComboBox *>();
    for (QComboBox *box : boxes) {
        if (box->count() == 3
            && box->itemData(0).toInt() == int(Speedtest::SpeedtestIntensity::Fast)
            && box->itemData(2).toInt() == int(Speedtest::SpeedtestIntensity::Deep)) {
            knob = box;
            break;
        }
    }
    ASSERT_TRUE(knob != nullptr) << "找不到那颗测速强度下拉框，这条测试就没在守任何东西";

    knob->setCurrentIndex(2);
    const Speedtest::SpeedTestOptions deep = window.speedtestAllOptions();
    EXPECT_EQ(deep.intensity, Speedtest::SpeedtestIntensity::Deep);
    EXPECT_GE(deep.timeoutMs, 3500);

    knob->setCurrentIndex(0);
    const Speedtest::SpeedTestOptions fast = window.speedtestAllOptions();
    EXPECT_EQ(fast.intensity, Speedtest::SpeedtestIntensity::Fast);
    EXPECT_LE(fast.timeoutMs, 800);

    EXPECT_GT(deep.timeoutMs, fast.timeoutMs);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
