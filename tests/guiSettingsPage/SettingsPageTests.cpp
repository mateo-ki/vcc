#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLineEdit>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <iostream>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/Pages.h"
#include "gui/ProxyPage.h"
#include "gui/SettingsPage.h"
#include "gui/SourcesPage.h"
#include "models/Source.h"
#include "gui/Theme.h"
#include "models/Settings.h"

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

} // namespace

TEST(SettingsPageTests, initialSettingsLoaded)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SettingsPage(*context);
    page->resize(900, 600);
    std::cout << "theme: " << context->settings().uiTheme.toStdString() << std::endl;
    std::cout << "autoPrepareKernel: " << context->settings().autoPrepareKernel << std::endl;
    std::cout << "speedtestAddedSource: " << context->settings().speedtestAddedSource << std::endl;
    std::cout << "speedtestDownload: " << context->settings().speedtestDownload << std::endl;
    std::cout << "httpServerEnabled: " << context->settings().httpServerEnabled << std::endl;
    std::cout << "closeToTray: " << context->settings().closeToTray << std::endl;
    std::cout << "fetchConcurrency: " << context->settings().fetchConcurrency << std::endl;
    std::cout << "fetchTimeoutSecs: " << context->settings().fetchTimeoutSecs << std::endl;
    std::cout << "speedtestConcurrency: " << context->settings().speedtestConcurrency << std::endl;
    std::cout << "speedtestTimeoutMs: " << context->settings().speedtestTimeoutMs << std::endl;
    EXPECT_FALSE(page->isDirty());
    delete page;
}

TEST(SettingsPageTests, saveAndReloadSettings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SettingsPage(*context);
    page->resize(900, 600);

    // Patch settings directly and refresh
    Models::UpdateSettings patch;
    patch.autoFetchEnabled = true;
    patch.autoFetchIntervalMin = 45;
    auto res = context->saveSettings(patch);
    ASSERT_TRUE(res.isOk());

    page->refresh();
    EXPECT_FALSE(page->isDirty());
    EXPECT_TRUE(context->settings().autoFetchEnabled);
    EXPECT_EQ(context->settings().autoFetchIntervalMin, 45);

    delete page;
}

namespace {

/// 按肉眼口径给像素分档：偏红/偏黄/偏绿。中性灰白与蓝色（强调色）都不算。
const char *tintOf(const QColor &c)
{
    const int r = c.red();
    const int g = c.green();
    const int b = c.blue();
    if (r > 140 && r > g + 40 && r > b + 40) {
        return "red";
    }
    if (r > 140 && g > 110 && g > b + 40 && r > b + 40) {
        return "yellow";
    }
    if (g > 110 && g > r + 25 && g > b + 25) {
        return "green";
    }
    return nullptr;
}

void census(const QString &tag, QWidget *root)
{
    const QImage image = root->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QMap<QString, int> counts;
    QMap<QString, QString> firstSeen;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            const char *tint = tintOf(color);
            if (tint == nullptr) {
                continue;
            }
            const QString key = QString::fromLatin1(tint);
            ++counts[key];
            if (!firstSeen.contains(key)) {
                QWidget *child = root->childAt(x, y);
                QString sheet;
                if (child != nullptr) {
                    for (QWidget *walk = child; walk != nullptr && walk != root;
                         walk = walk->parentWidget()) {
                        if (!walk->styleSheet().isEmpty()) {
                            sheet = QStringLiteral("[%1 css=%2]")
                                        .arg(walk->metaObject()->className())
                                        .arg(walk->styleSheet().left(70));
                            break;
                        }
                    }
                }
                firstSeen.insert(
                    key, QStringLiteral("first=%1 at (%2,%3) over=%4 %5")
                                .arg(color.name(), QStringLiteral("(%1,%2)").arg(x).arg(y))
                                .arg(child != nullptr ? child->metaObject()->className()
                                                      : QStringLiteral("<root>"))
                                .arg(sheet));
            }
        }
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        std::cerr << qPrintable(QStringLiteral("CENSUS %1 %2: %3 px  %4\n")
                                     .arg(tag, it.key())
                                     .arg(it.value())
                                     .arg(firstSeen.value(it.key())));
    }
    const Theme::Palette pal = Theme::current();
    std::cerr << qPrintable(QStringLiteral("CENSUS theme page=%1 panel=%2 border=%3 accent=%4 "
                                           "accentStrong=%5 accentBg=%6 muted=%7 success=%8 "
                                           "warning=%9 danger=%10\n")
                                 .arg(pal.pageBg.name(), pal.panel.name(), pal.border.name(),
                                      pal.accent.name(), pal.accentStrong.name(),
                                      pal.accentBg.name(), pal.muted.name(), pal.success.name(),
                                      pal.warning.name(), pal.danger.name()));
}

} // namespace

TEST(SettingsPageTests, colorCensusProbe)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SettingsPage(*context);
    page->resize(1000, 900);
    page->show();
    QCoreApplication::processEvents();
    census(QStringLiteral("settings"), page);

    auto *combo = page->findChild<QComboBox *>();
    ASSERT_TRUE(combo != nullptr);
    combo->showPopup();
    QCoreApplication::processEvents();
    if (combo->view() != nullptr && combo->view()->window() != nullptr) {
        census(QStringLiteral("popup"), combo->view()->window());
        combo->hidePopup();
    }
    delete page;
}

/// 一个页面里所有"按控件挂的样式表"里出现的 #rrggbb，都必须是当前调色板的成员。
/// Why：这些页以前各自写死过 #10b981（亮绿）/#ef4444（红）/#f59e0b（琥珀）/#888888（灰），
/// 而 Rust 的 `theme.rs` 早就定了"页面上不许出现红色和黄色"——warning 是石墨蓝灰、
/// danger 是近黑、绿色只留给"实测正常/连通"。写死的色值既绕开这条约束，也不会随主题
/// 变；现在页面只写 `{槽}`，填色由 `Theme::setWidgetCss` 做，所以量到的必须全是主题色。
QSet<QString> paletteColors()
{
    const Theme::Palette pal = Theme::current();
    const QColor tokens[] = {
        pal.pageBg,           pal.sidebar,        pal.panel,          pal.panelHover,
        pal.border,           pal.borderSubtle,   pal.textPrimary,    pal.textSecondary,
        pal.muted,            pal.accent,         pal.accentHover,    pal.accentBg,
        pal.accentStrong,     pal.primaryBtn,     pal.primaryBtnHover, pal.primaryBtnPress,
        pal.onPrimary,        pal.success,        pal.successBg,      pal.warning,
        pal.warningBg,        pal.danger,         pal.dangerBg,       pal.dangerStroke,
        pal.sidebarText,      pal.sidebarMuted};
    QSet<QString> allowed;
    for (const QColor &color : tokens) {
        allowed.insert(color.name(QColor::HexRgb));
    }
    return allowed;
}

/// 返回 <违规色值, 挂了几份样式表>。
QStringList auditWidgetSheets(QWidget *root, const QSet<QString> &allowed, int &sheets)
{
    QStringList offenders;
    static const QRegularExpression hexPattern(QStringLiteral("#[0-9a-fA-F]{6}"));
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const QString sheet = widget->styleSheet();
        if (sheet.isEmpty()) {
            continue;
        }
        ++sheets;
        auto match = hexPattern.globalMatch(sheet);
        while (match.hasNext()) {
            const QString value = match.next().captured().toLower();
            if (!allowed.contains(value)) {
                offenders.push_back(QStringLiteral("%1 %2 <%3>")
                                        .arg(QString::fromLatin1(widget->metaObject()->className()),
                                             value, sheet.left(50)));
            }
        }
    }
    return offenders;
}

TEST(AllPageColors, noHardcodedHexOutsideThePalette)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    const QSet<QString> allowed = paletteColors();
    int sheets = 0;
    QStringList offenders;

    QVector<QWidget *> pages;
    pages << createSettingsPage(*context) << createNodesPage(*context)
          << createSourcesPage(*context) << createApiPage(*context);

    // 订阅源那一页的状态徽章是**表格里逐行建**的（启用/停用两种配色）。库里没行的
    // 话这一轮就什么都审不到 —— 上一版就是这么漏掉一处写死的红的，所以先灌两条。
    auto *sources = qobject_cast<SourcesPage *>(pages.at(2));
    ASSERT_TRUE(sources != nullptr);
    for (const bool enabled : {true, false}) {
        Models::CreateSource input;
        input.name = QStringLiteral("audit-%1").arg(enabled ? 1 : 2);
        input.urlTemplate = QStringLiteral("https://example.com/sub.txt");
        input.enabled = enabled;
        const auto created = context->database().createSource(input);
        ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    }
    sources->refresh();

    for (QWidget *page : std::as_const(pages)) {
        page->resize(1000, 900);
        page->show();
        QCoreApplication::processEvents();
        offenders << auditWidgetSheets(page, allowed, sheets);
    }

    // 控制台的分栏是懒建的：不挨个打开，这一轮就只审到默认那一栏。
    auto *console = qobject_cast<ProxyPage *>(createProxyPage(*context));
    ASSERT_TRUE(console != nullptr);
    console->resize(1100, 800);
    console->show();
    for (int index = 0; index <= int(ProxyPage::Panel::Logs); ++index) {
        console->selectPanel(ProxyPage::Panel(index));
        QCoreApplication::processEvents();
    }
    offenders << auditWidgetSheets(console, allowed, sheets);

    EXPECT_GE(sheets, 60) << "审到的按控件样式太少，这条断言等于什么都没测";
    EXPECT_TRUE(offenders.isEmpty())
        << "写死的非主题色: " << offenders.join(QStringLiteral(" ; ")).toStdString();

    for (QWidget *page : pages) {
        delete page;
    }
    delete console;
}

/// 他 2026-10-12 报："系统设置的点击主窗口关闭按钮 X 时的选项的选择状态很奇怪，不是实心的样式吗"。
/// 根因不是那条全局 QSS 写错了配色，而是**应用级 QSS 铺不进卡片里的控件**（这条在控制台
/// 那边已经踩过并守住了，见 `ProxyPageAudit.everyInputControlCarriesItsOwnTemplate`）：
/// 系统设置页漏了 `Gui::decorateFieldsIn`，那两个单选钮于是只剩全局那条
/// `indicator:checked { border: 4px solid accent; background-color: panel }` —— 空心圆环；
/// 全应用统一的实心点住在 `Widgets.cpp` 的 kRadioCss 里。
/// 三段断言：① 每个输入控件都带 `paletteCss`，旋钮/下拉框的内嵌 QLineEdit **必须不带**；
/// ② 选中的那颗指示器中心像素被填成 accent（空心环时它是 panel，这条会当场红）；
/// ③ 只靠应用级样式表的裸单选钮也必须是实心 —— 守 `Theme.cpp` 那处一起改的规则。
TEST(SettingsPageTests, everyInputControlCarriesItsOwnTemplateAndCheckedRadioIsSolid)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SettingsPage(*context);
    page->resize(1000, 900);
    page->show();
    QCoreApplication::processEvents();

    int checked = 0;
    QStringList offenders;
    const QList<QWidget *> widgets = page->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const bool isField = qobject_cast<QComboBox *>(widget) != nullptr
            || qobject_cast<QLineEdit *>(widget) != nullptr
            || qobject_cast<QSpinBox *>(widget) != nullptr
            || qobject_cast<QCheckBox *>(widget) != nullptr
            || qobject_cast<QRadioButton *>(widget) != nullptr;
        if (!isField) {
            continue;
        }
        if (qobject_cast<QLineEdit *>(widget) != nullptr
            && (qobject_cast<QSpinBox *>(widget->parentWidget()) != nullptr
                || qobject_cast<QComboBox *>(widget->parentWidget()) != nullptr)) {
            EXPECT_TRUE(widget->property("paletteCss").toString().isEmpty())
                << "旋钮/下拉框的内嵌输入框被单独装饰了，会画成两层框";
            continue;
        }
        ++checked;
        if (widget->property("paletteCss").toString().isEmpty()) {
            offenders << QStringLiteral("%1(%2)")
                             .arg(QString::fromLatin1(widget->metaObject()->className()),
                                  widget->objectName());
        }
    }
    EXPECT_GE(checked, 10) << "系统设置页审到的输入控件太少，这条断言等于没测";
    EXPECT_TRUE(offenders.isEmpty())
        << "没有自带模板的控件: " << offenders.join(QStringLiteral(" ; ")).toStdString();

    // ---- 像素：选中的单选钮必须是实心 ----
    QRadioButton *radio = page->findChild<QRadioButton *>();
    ASSERT_TRUE(radio != nullptr);
    radio->setChecked(true);
    QCoreApplication::processEvents();

    QStyleOptionButton option;
    option.initFrom(radio);
    const QRect indicator = radio->style()->subElementRect(QStyle::SE_RadioButtonIndicator, &option, radio);
    ASSERT_TRUE(indicator.isValid());
    ASSERT_GE(indicator.width(), 8);

    QImage image(radio->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    radio->render(&image);
    const QColor center = image.pixelColor(indicator.center());

    const Theme::Palette pal = Theme::current();
    const auto distance = [](const QColor &a, const QColor &b) {
        const int dr = a.red() - b.red();
        const int dg = a.green() - b.green();
        const int db = a.blue() - b.blue();
        return dr * dr + dg * dg + db * db;
    };
    std::cout << "checked radio indicator center: " << center.name().toStdString()
              << " accent=" << pal.accent.name().toStdString()
              << " panel=" << pal.panel.name().toStdString() << std::endl;
    EXPECT_LT(distance(center, pal.accent), distance(center, pal.panel))
        << "选中的单选钮中心还是空的（画成了空心圆环）";
    // 外圈也得是 accent，否则是"实心灰点"这种另一个方向的走形。
    EXPECT_LT(distance(image.pixelColor(indicator.center() + QPoint(0, 1)), pal.accent),
              distance(image.pixelColor(indicator.center() + QPoint(0, 1)), pal.panel));

    // 第二处修法也要守住：应用级那条 `indicator:checked` 原来是
    // `border: 4px solid accent; background-color: panel`（空心圆环），现在改成实心。
    // 这里量的是"只靠全局样式表、没带自己模板"的一颗裸单选钮 —— 它以前是空的。
    // 两条断言各守一处改动：上面的 paletteCss 清点守 `Gui::decorateFieldsIn`，这条守
    // `Theme.cpp` 那条规则，谁退回旧写法都会红。
    // （`main()` 已经 `Theme::apply()` 过，全局样式表本来就在，这里不要再动它 ——
    //   setStyleSheet("") 会把后面用例的底色一起拆掉。）
    QRadioButton bare(QStringLiteral("对照"));
    bare.resize(140, 28);
    bare.setChecked(true);
    bare.show();
    QCoreApplication::processEvents();

    QStyleOptionButton bareOption;
    bareOption.initFrom(&bare);
    const QRect bareIndicator = bare.style()->subElementRect(QStyle::SE_RadioButtonIndicator, &bareOption, &bare);
    ASSERT_TRUE(bareIndicator.isValid());
    QImage bareImage(bare.size(), QImage::Format_ARGB32);
    bareImage.fill(Qt::transparent);
    bare.render(&bareImage);
    const QColor bareCenter = bareImage.pixelColor(bareIndicator.center());
    std::cout << "bare radio (app-level QSS only) center: " << bareCenter.name().toStdString() << std::endl;
    EXPECT_LT(distance(bareCenter, pal.accent), distance(bareCenter, pal.panel))
        << "只靠全局样式表时又画回空心圆环了（Theme.cpp 那条 :checked）";

    delete page;
}


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();}
