#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QFrame>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QMap>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

#include <iostream>

#include <memory>

#include "app/AppContext.h"
#include "gui/Pages.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "gui/Widgets.h"

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

/// 出问题的控件要能认出是哪一类：把"祖先里第一个滚动区"以上的链一起写进标签。
QString describe(QWidget *widget)
{
    QString label = QStringLiteral("%1#%2").arg(QString::fromLatin1(widget->metaObject()->className()),
                                                widget->objectName());
    QStringList chain;
    for (QWidget *parent = widget->parentWidget(); parent != nullptr;
         parent = parent->parentWidget()) {
        chain.prepend(QStringLiteral("%1#%2").arg(
            QString::fromLatin1(parent->metaObject()->className()), parent->objectName()));
        if (parent->inherits("QScrollArea")) {
            label += QStringLiteral(" [滚动区内: %1]").arg(chain.join(QStringLiteral(" > ")));
            break;
        }
    }
    return label;
}

/// 没布局过的 QWidget::grab() 返回空图，量不到东西；先给个下限尺寸。
QImage snapshot(QWidget *widget)
{
    // 不强行 resize：给没布局过的控件凑个尺寸，量到的是"凑出来的假样子"
    //（上一版就是这么量出 0 边框像素的假故障）。
    widget->ensurePolished();
    const QPixmap pixmap = widget->grab();
    return pixmap.isNull() ? QImage() : pixmap.toImage();
}

/// 出现最多的不透明色 = 它实际铺的底色。单取边角像素不可信：
/// border-radius 会把圆角削成 alpha 0，看着像"没画背景"。
QColor dominantColor(const QImage &image)
{
    QMap<QRgb, int> counts;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixelColor(x, y).rgba();
            if (qAlpha(pixel) >= 250) {
                ++counts[pixel];
            }
        }
    }
    QColor best;
    int bestCount = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = QColor::fromRgba(it.key());
        }
    }
    return best;
}

void measure(QWidget *widget, const QColor &panel, int &checked, QStringList &offenders)
{
    if (!widget->isVisible()) {
        // 隐藏页里的控件 grab() 出来 0 个不透明像素，那是探针假象，不是没底色。
        return;
    }
    ++checked;
    const QImage image = snapshot(widget);
    const QColor background = dominantColor(image);
    if (image.isNull() || background.red() != panel.red() || background.green() != panel.green()
        || background.blue() != panel.blue()) {
        offenders.push_back(QStringLiteral("%1 主色 %2（%3x%4）应为 %5")
                                .arg(describe(widget), background.name(),
                                     QString::number(image.width()), QString::number(image.height()),
                                     panel.name()));
    }
}

/// 下拉框的外观只有一份，就在应用级 QSS：这里按像素验"底色是面板、边框看得见"。
/// （以前这条是"每颗控件都得自带一份 CSS"，实测那份重复每造一颗多花 ~4.8 ms，
///  而应用级自己就把埋在卡片里的下拉框画得一模一样，于是改成量像素。）
void measureSharedComboBoxCss(QWidget *widget, const Theme::Palette &pal, int &checked,
                              QStringList &offenders)
{
    if (!widget->isVisible() || widget->width() < 40) {
        return;
    }
    ++checked;
    const QImage image = snapshot(widget);
    const QColor background = dominantColor(image);
    if (image.isNull() || background != pal.panel) {
        offenders.push_back(QStringLiteral("%1 底色 %2 不是面板 %3（宽 %4）")
                                .arg(describe(widget), background.name(), pal.panel.name(),
                                     QString::number(image.width())));
        return;
    }
    // 边框：整张图里必须出现 border 那个色值，否则就是"看着一块白板"。
    bool bordered = false;
    for (int y = 0; y < image.height() && !bordered; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).name(QColor::HexRgb) == pal.border.name(QColor::HexRgb)) {
                bordered = true;
                break;
            }
        }
    }
    if (!bordered) {
        offenders.push_back(QStringLiteral("%1 没有可见边框（应用级 QSS 没作用到它）")
                                .arg(describe(widget)));
    }
}

void collectCombosFrom(QWidget *page, const Theme::Palette &pal, int &checked,
                       QStringList &offenders, QStringList &cssOffenders)
{
    for (QComboBox *combo : page->findChildren<QComboBox *>()) {
        measure(combo, pal.panel, checked, offenders);
        measureSharedComboBoxCss(combo, pal, checked, cssOffenders);
    }
}

void collectEditsFrom(QWidget *page, const QColor &panel, int &checked, QStringList &offenders)
{
    for (QLineEdit *edit : page->findChildren<QLineEdit *>()) {
        measure(edit, panel, checked, offenders);
    }
}

}

/// 下拉框和输入框都必须有"看得见的面板底色"。这类问题在滚动区里最容易漏：
/// 全局 `QWidget { background: 页面色 }`、卡片/容器上的 transparent 规则，和
/// QComboBox/QLineEdit 自己那条只有一级类型选择器的规则在抢，谁赢取决于特异度和顺序。
/// 所以只能逐栏打开、真渲染、按像素验收。
TEST(ComboTheme, comboBoxesHaveBorderAndPanelBackground)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    auto *console = createProxyPage(*context);
    QList<QWidget *> pages = {console, createNodesPage(*context), createSourcesPage(*context),
                              createSettingsPage(*context), createApiPage(*context)};
    for (QWidget *page : pages) {
        page->resize(1180, 820);
    }
    ProxyPage *proxy = qobject_cast<ProxyPage *>(console);
    ASSERT_TRUE(proxy != nullptr);

    // 表要贴在"页面都建好之后"：SettingsPage 构造时会把库里的 ui_theme 真的 apply 一次，
    // 先贴再建就会拿着一套色去量另一套色（这一版探针就是这么得出过 #0f172a 的假故障）。
    // apply() 的"同 id 直接返回"守卫也要绕开，所以直接贴算出来的表。
    const Theme::Palette pal = Theme::current();
    qApp->setStyleSheet(Theme::styleSheet(pal));
    const QColor panel = pal.panel;

    
    QStringList offenders;
    QStringList cssOffenders;
    QStringList editOffenders;
    int checked = 0;
    int editChecked = 0;
    // 六个分栏共用一个 QStackedWidget：一轮只有一栏可见，所以要轮着打开各量一遍。
    // 分栏是懒建的，第一次打开才建出控件。
    for (int index = 0; index <= int(ProxyPage::Panel::Logs); ++index) {
        // 每轮都重新钉一次：有的页面在 show/refresh 里会自己 apply() 一次主题，
        // 那样就变成"拿 A 套色去量 B 套表"，量到的主色对不上任何一条规则。
        qApp->setStyleSheet(Theme::styleSheet(pal));
        proxy->selectPanel(ProxyPage::Panel(index));
        for (QWidget *page : pages) {
            page->show();
        }
        QCoreApplication::processEvents();
        for (QWidget *page : pages) {
            collectCombosFrom(page, pal, checked, offenders, cssOffenders);
            collectEditsFrom(page, pal.panel, editChecked, editOffenders);
        }
        for (QWidget *page : pages) {
            page->hide();
        }
        QCoreApplication::processEvents();
    }

    EXPECT_EQ(Theme::current().panel.name().toStdString(), panel.name().toStdString())
        << "跑动中主题被换过：量到的主色不能归因到某条规则";
    RecordProperty("combosChecked", checked);
    RecordProperty("lineEditsStillUnstyled", editOffenders.size());
    EXPECT_GT(checked, 12) << "页面里应该真的建出了下拉框";
    if (!cssOffenders.isEmpty()) {
        const QString separator = QChar(10);
        ADD_FAILURE() << "下拉框外观没统一（应用级 QSS 没作用到这些）:" << separator.toStdString()
                      << cssOffenders.join(separator).toStdString();
    }
    if (!editOffenders.isEmpty()) {
        // 本轮只要求"全软件下拉框统一样式"；输入框的底色问题同源（应用级 QSS
        // 在这些容器里不铺底），先如实记数，别把没修的东西写成已通过。
        std::cerr << "仍未修的输入框底色(" << editOffenders.size() << "/" << editChecked
                  << "): " << editOffenders.join(QStringLiteral(" ; ")).toStdString() << std::endl;
    }
    if (!offenders.isEmpty()) {
        ADD_FAILURE() << "没有铺面板底色的字段控件:\n"
                      << offenders.join(QStringLiteral("\n")).toStdString();
    }
}

/// 应用级 QSS 里每个色值都必须是当前调色板的成员。
/// Why：`QColor::name(HexArgb)` 给的是 "#AARRGGBB"，以前有代码按前 7 个字符截它，
/// 于是正文 #0f172a 变成 #ff0f17、强调蓝 #2f6feb 变成 #ff2f6f —— 下拉框连边框带箭头
/// 一起泛红，而纯白 (#ffffff) 截完仍是纯白，所以"底色对不对"那套像素探针完全没抓到。
/// 下拉框的外观现在只有这一处来源（控件自带那份已撤），所以守卫也挪到这里。
TEST(ComboTheme, appStyleSheetOnlyUsesPaletteColors)
{
    const Theme::Palette pal = Theme::current();
    const QColor tokens[] = {
        pal.pageBg,           pal.sidebar,        pal.panel,          pal.panelHover,
        pal.border,           pal.borderSubtle,   pal.textPrimary,    pal.textSecondary,
        pal.muted,            pal.accent,         pal.accentHover,    pal.accentBg,
        pal.accentStrong,     pal.primaryBtn,     pal.primaryBtnHover, pal.primaryBtnPress,
        pal.onPrimary,        pal.success,        pal.successBg,      pal.warning,
        pal.warningBg,        pal.danger,         pal.dangerBg,       pal.dangerStroke,
        pal.sidebarText,      pal.sidebarMuted,     pal.logViewBg,        pal.logViewFg,
        pal.logViewBorder};
    QSet<QString> allowed;
    for (const QColor &color : tokens) {
        allowed.insert(color.name(QColor::HexRgb));
    }

    QString sheet = Theme::styleSheet(pal);
    // rgba(...) 形式的淡彩是从主题色算出来的，先归一化成 #rrggbb 再比。
    static const QRegularExpression rgbaPattern(
        QStringLiteral("rgba\\((\\d+),(\\d+),(\\d+),\\d+\\)"));
    QStringList rgba;
    auto it = rgbaPattern.globalMatch(sheet);
    while (it.hasNext()) {
        rgba.push_back(it.next().captured(0));
    }
    for (const QString &value : std::as_const(rgba)) {
        const auto m = rgbaPattern.match(value);
        sheet.replace(value, QColor(m.captured(1).toInt(), m.captured(2).toInt(),
                                   m.captured(3).toInt()).name(QColor::HexRgb));
    }

    QStringList offenders;
    int seen = 0;
    static const QRegularExpression hexPattern(QStringLiteral("#[0-9a-fA-F]{6}"));
    auto matches = hexPattern.globalMatch(sheet);
    while (matches.hasNext()) {
        const QString value = matches.next().captured().toLower();
        ++seen;
        if (!allowed.contains(value)) {
            offenders.push_back(value);
        }
    }
    EXPECT_GE(seen, 30) << "整张应用级样式表里没解析出色值：这条守卫什么都没测";
    EXPECT_TRUE(offenders.isEmpty())
        << "不属于主题的色值（alpha 串位就会长这样）: "
        << offenders.join(QStringLiteral(", ")).toStdString();
    // 下拉框这一段必须还在：应用级是"裸控件"也能画出来的那一层。
    EXPECT_TRUE(sheet.contains(QStringLiteral("QComboBox {"))) << "应用级没有 QComboBox 规则了";
    // 禁用态那一条在控件自带的那份模板里（应用级那条试过，被"容器透明"顶掉，没用）。
    QComboBox combo;
    Gui::decorateComboBox(&combo);
    EXPECT_TRUE(combo.styleSheet().contains(QStringLiteral("QComboBox:disabled")))
        << "禁用态没有边框那一条了：代理在跑时 DNS/TUN 那几个下拉框会退回原生画法";
}

namespace {

/// 从一段 QSS 里挑出所有 `*-arrow { image: url(...) }` 指向的文件，返回不在磁盘上的那些。
/// 一条都没写也算缺失 —— 箭头坏掉的唯一表现就是"没有箭头"，颜色探针看不见它。
QStringList missingArrowImages(const QString &css, const QString &where)
{
    static const QRegularExpression pattern(QStringLiteral(
        "(up|down)-arrow\\s*\\{[^}]*?image\\s*:\\s*url\\(\\s*([^)\\s]+)\\s*\\)"));
    QStringList missing;
    int seen = 0;
    auto matches = pattern.globalMatch(css);
    while (matches.hasNext()) {
        ++seen;
        const auto capture = matches.next();
        const QString path = capture.captured(2);
        if (!QFileInfo::exists(path)) {
            missing.push_back(QStringLiteral("%1::%2 -> %3").arg(where, capture.captured(1), path));
        }
    }
    if (seen == 0) {
        missing.push_back(where + QStringLiteral("::<没有 up/down-arrow 的 image 规则>"));
    }
    return missing;
}

} // namespace

/// 报告原话："下拉框的图标没显示向下的箭头"。Qt 的 QSS 里 `::down-arrow` 只认 `image:`，
/// 写 CSS 三角形是静默无效、写 `image: none` 是把原生箭头也关掉 —— 两种坏法都不报错，
/// 只能这样钉住：每条渲染路径解析出的 QSS 里，箭头文件必须真的在磁盘上。
TEST(ComboTheme, arrowImagesResolveToFiles)
{
    QStringList missing = missingArrowImages(Theme::styleSheet(Theme::current()),
                                             QStringLiteral("app"));

    QComboBox combo;
    Gui::decorateComboBox(&combo);
    missing += missingArrowImages(combo.styleSheet(), QStringLiteral("QComboBox"));

    QWidget container;
    auto *spin = new QSpinBox(&container);
    Gui::decorateFieldsIn(&container);
    missing += missingArrowImages(spin->styleSheet(), QStringLiteral("QSpinBox"));

    EXPECT_TRUE(missing.isEmpty()) << "箭头文件缺失: "
                                   << missing.join(QStringLiteral(" ; ")).toStdString();
}

int main(int argc, char **argv)
{
    // 控件必须有 QApplication 才存在；离屏平台由 ctest 的 ENVIRONMENT 给。
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
