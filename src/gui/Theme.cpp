#include "gui/Theme.h"
#include "gui/Icons.h"
#include "gui/Widgets.h"

#include <QApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QFrame>
#include <QRegularExpression>
#include <QVector>
#include <QWidget>

#include <cmath>

#include "gui/ThemeNames.h"

namespace {

/// 挂在控件上的动态属性名：存"带 {槽} 的样式表模板"，`apply()` 切主题时靠它重挂。
constexpr const char kCssTemplateProperty[] = "paletteCss";

/// 按少量锚点色派生一整套语义色，避免为每个主题手写 24 个 hex。
struct Ink {
    QColor page;
    QColor sidebar;
    QColor panel;
    QColor panelHover;
    QColor border;
    QColor text;
    QColor textSecondary;
    QColor muted;
    QColor accent;
    QColor onPrimary;
    QColor primaryBtn;
    QColor success;
    QColor warning;
    QColor danger;
    bool light = true;
};

QColor rgb(int hex)
{
    return QColor::fromRgb(hex);
}

QString hexColor(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

QString rgbaColor(const QColor &color, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

double luminance(const QColor &color)
{
    const auto channel = [](int value) {
        const double s = double(value) / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green())
         + 0.0722 * channel(color.blue());
}

/// 把 `base` 逐步压深，直到它压在 `bg` 上能到 `target`:1（WCAG AA 正文要 4.5）。
///
/// 为什么不手挑：亮色当按钮底色时白字只有 2.9:1 左右（Raycast 红实测），手挑"深一点的同色"
/// 每加一个强调色都要人再核一遍；这里直接算，加色只写一个 hex。对已经够深的颜色是幂等的。
QColor readableOn(const QColor &base, const QColor &bg, double target)
{
    const QColor black = QColor::fromRgb(0, 0, 0);
    double t = 0.0;
    QColor value = base;
    while (Gui::Theme::contrast(value, bg) < target && t < 0.9) {
        t += 0.02;
        value = Gui::Theme::mix(base, black, t);
    }
    return value;
}

QColor readableOnWhite(const QColor &accent)
{
    return readableOn(accent, QColor::fromRgb(255, 255, 255), 4.6);
}

Gui::Theme::Palette fromInk(const Ink &ink)
{
    using namespace Gui::Theme;
    Palette out;
    const QColor accentHover = ink.light ? darken(ink.accent, 0.16) : lighten(ink.accent, 0.16);
    const QColor button = ink.primaryBtn;
    QColor softBtn;
    QColor softBtnHover;
    QColor softBtnPress;
    if (ink.light) {
        softBtn = mix(ink.page, ink.accent, 0.20);
        softBtnHover = mix(ink.page, ink.accent, 0.30);
        softBtnPress = mix(ink.page, ink.accent, 0.38);
    } else {
        softBtn = button;
        softBtnHover = mix(button, ink.text, 0.14);
        softBtnPress = darken(button, 0.18);
    }
    const QColor softBtnText = ink.light ? readableOn(ink.accent, softBtn, 4.6) : ink.onPrimary;

    out.pageBg = ink.page;
    out.sidebar = ink.sidebar;
    out.panel = ink.panel;
    out.panelHover = ink.panelHover;
    out.border = ink.border;
    out.borderSubtle = mix(ink.border, ink.panel, 0.45);
    out.textPrimary = ink.text;
    out.textSecondary = ink.textSecondary;
    out.muted = ink.muted;
    out.accent = ink.accent;
    out.accentHover = accentHover;
    out.accentBg = mix(ink.page, ink.accent, ink.light ? 0.12 : 0.2);
    out.accentStrong = readableOnWhite(ink.accent);
    // 浅色主题下主按钮不再用实心深蓝：淡蓝底 + 压深后的强调色字。
    // 文字必须按**这块淡蓝底**去压深，不是按纯白（按纯白算出来只有 3.49:1）。
    out.primaryBtn = softBtn;
    out.primaryBtnHover = softBtnHover;
    out.primaryBtnPress = softBtnPress;
    out.onPrimary = softBtnText;
    out.success = ink.success;
    out.successBg = mix(ink.page, ink.success, ink.light ? 0.12 : 0.18);
    out.warning = ink.warning;
    out.warningBg = mix(ink.page, ink.warning, ink.light ? 0.14 : 0.2);
    out.danger = ink.danger;
    out.dangerBg = mix(ink.page, ink.danger, ink.light ? 0.12 : 0.2);
    out.dangerStroke = mix(ink.page, ink.danger, ink.light ? 0.55 : 0.42);
    out.logViewBg = rgb(0x161A22);
    out.logViewFg = rgb(0xE6EDF3);
    out.logViewBorder = rgb(0x30363D);
    out.sidebarText = ink.text;
    out.sidebarMuted = ink.textSecondary;
    out.light = ink.light;
    return out;
}

QMutex g_themeMutex;
Gui::Theme::Palette g_current = Gui::Theme::palette(Gui::Theme::kDefaultThemeId);
QString g_currentId = Gui::Theme::kDefaultThemeId;

}

namespace Gui {
namespace Theme {

const QVector<Definition> &themes()
{
    static const QVector<Definition> kThemes = {
        Definition{QStringLiteral("soft-cobalt"), ThemeNames::softCobalt(), 0x2F6FEB},
        Definition{QStringLiteral("soft-teal"), ThemeNames::softTeal(), 0x00A78D},
        Definition{QStringLiteral("soft-terracotta"), ThemeNames::softTerracotta(), 0xD9772E},
        Definition{QStringLiteral("soft-indigo"), ThemeNames::softIndigo(), 0x5B5BD6},
        Definition{QStringLiteral("soft-violet"), ThemeNames::softViolet(), 0x7C5CFF},
    };
    return kThemes;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    const auto lerp = [t](int x, int y) {
        const double value = double(x) * (1.0 - t) + double(y) * t;
        return qBound(0, int(std::lround(value)), 255);
    };
    return QColor::fromRgb(lerp(a.red(), b.red()), lerp(a.green(), b.green()),
                           lerp(a.blue(), b.blue()));
}

QColor darken(const QColor &color, double t)
{
    return mix(color, QColor::fromRgb(0, 0, 0), t);
}

QColor lighten(const QColor &color, double t)
{
    return mix(color, QColor::fromRgb(255, 255, 255), t);
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    const double hi = qMax(la, lb);
    const double lo = qMin(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

Palette palette(const QString &id)
{
    Ink ink;
    ink.accent = rgb(themeById(id).accent);
    // 中性色对齐 Rust `theme.rs` 的软钴蓝那一套：页面只比卡片深"一档冷灰蓝"，
    // 不是深灰。（0xDEE5EF 是我这边自己压深的，他看着就是"背景发灰"；Rust 用
    // 0xE8EEF7，与白卡的分层比 1.173，仍然过 ShellTests 那条 ≥1.12 的地板。）
    // 文字那几档（0x0F172A/0x334155/0x64748B）先留着：全站还有一批按控件写死的
    // 同值 CSS/自绘色（NodeCard 的标题就是 0x0f172a），只换锚点会让两边对不上。
    ink.page = rgb(0xE8EEF7);
    ink.sidebar = rgb(0xFFFFFF);
    ink.panel = rgb(0xFFFFFF);
    ink.panelHover = rgb(0xF1F6FD);
    ink.border = rgb(0xCBD5E1);
    ink.text = rgb(0x0F172A);
    ink.textSecondary = rgb(0x334155);
    ink.muted = rgb(0x64748B);
    ink.onPrimary = rgb(0xFFFFFF);
    ink.primaryBtn = readableOnWhite(ink.accent);
    ink.success = rgb(0x0E6B4C);
    // 用户 2026-10-06：页面上不要红色和黄色。警告/危险改成石墨蓝灰，
    // 严重度靠"字重 + 图标 + 措辞"表达，不靠颜色。绿色留给"实测正常/连通"。
    ink.warning = rgb(0x6B7480);
    ink.danger = rgb(0x2E353D);
    ink.light = true;
    return fromInk(ink);
}

const Definition &themeById(const QString &id)
{
    for (const Definition &theme : themes()) {
        if (theme.id == id) {
            return theme;
        }
    }
    // 认不出的存值（旧主题 id、手改过的值）落到默认那套，而不是随便挑一套。
    return themes().first();
}

QString currentId()
{
    QMutexLocker locker(&g_themeMutex);
    return g_currentId;
}

Palette current()
{
    QMutexLocker locker(&g_themeMutex);
    return g_current;
}

bool isLight()
{
    return current().light;
}

void apply(const QString &id)
{
    const QString resolved = themeById(id).id;
    const Palette pal = palette(resolved);
    const QString wanted = styleSheet(pal);
    QApplication *app = qobject_cast<QApplication *>(QApplication::instance());
    {
        QMutexLocker locker(&g_themeMutex);
        // 必须是"主题没变 **且** 应用级样式表真的挂上了"才算无事可做。
        // 以前这里只看 g_currentId，而它是静态初值、默认就等于默认主题 id ——
        // 于是"首启用默认主题"这条最常见的路径被整个跳过：应用级 QSS 从没挂上，
        // 界面退回原生样式（卡片间距、页面底色全丢），只剩每块区域自带的那份底。
        if (g_currentId == resolved && app != nullptr && app->styleSheet() == wanted) {
            return;
        }
        g_current = pal;
        g_currentId = resolved;
    }
    // 应用级样式表：切换期间关掉顶层窗口绘制，避免数百个控件同步 polish 造成界面卡顿。
    if (app != nullptr) {
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget *top : tops) {
            top->setUpdatesEnabled(false);
        }
        app->setStyleSheet(wanted);
        // 按控件挂的那一份 Qt 不会替我们重解析：把带模板的控件按新调色板再挂一遍，
        // 否则切到"陶土橙"之后，聚焦边框/选中卡片还留着上一个主题的钴蓝。
        for (QWidget *top : tops) {
            const QList<QWidget *> descendants = top->findChildren<QWidget *>();
            for (QWidget *child : descendants) {
                const QVariant pattern = child->property(kCssTemplateProperty);
                if (pattern.canConvert<QString>()) {
                    child->setStyleSheet(cssWithPalette(pattern.toString()));
                }
                // 自绘的控件（节点卡片和它的徽章）颜色是在 paintEvent 里取的，
                // 换主题后必须催一次重画，否则它沿用上一套主题画出来的像素。
                child->update();
            }
            // 侧栏导航的图标不是 QSS 画的，是渲成 QIcon 的位图（对应 Rust nav_item 里
            // painter 画的那一枚）—— 调色板一变就得按新色重渲一遍。
            Icons::refreshNavIconsIn(top);
        }
        for (QWidget *top : tops) {
            top->setUpdatesEnabled(true);
        }
    }
}

QString styleSheet(const Palette &pal)
{
    QString css;
    css.reserve(4096);
    css += QStringLiteral("QWidget { background: %1; color: %2; }\n")
              .arg(hexColor(pal.pageBg), hexColor(pal.textPrimary));
    // 左侧菜单栏：浅色纯白背景，右侧 1px 分割线
    css += QStringLiteral(
               "QFrame#Sidebar, QWidget#Sidebar {\n"
               "    background-color: %1;\n"
               "    border: none;\n"
               "    border-right: 1px solid %2;\n"
               "}\n"
               "QFrame#Sidebar QLabel {\n"
               "    background: transparent;\n"
               "}\n")
               .arg(hexColor(pal.sidebar), hexColor(pal.border));
    // 右侧内容画布与页面容器：深色冷灰背景
    css += QStringLiteral(
               "QWidget#ContentArea, QStackedWidget#PagesStack, Gui--Page {\n"
               "    background-color: %1;\n"
               "}\n"
               "QScrollArea { background: transparent; border: none; }\n"
               "QScrollArea > QWidget > QWidget { background: transparent; }\n")
               .arg(hexColor(pal.pageBg));
    css += QStringLiteral("QLabel#BrandTitle { background: transparent; color: %1; font-size: 17px; font-weight: 700; }\n")
               .arg(hexColor(pal.sidebarText));
    css += QStringLiteral("QLabel#BrandSubtitle { background: transparent; color: %1; font-size: 12px; }\n")
               .arg(hexColor(pal.sidebarMuted));
    // 品牌行与第一条导航项之间的分隔线，对应 Rust sidebar 里那条 hline(border)。
    css += QStringLiteral("QFrame#SidebarRule { background: transparent; border: none;"
                          " border-bottom: 1px solid %1; max-height: 0px; }\n")
               .arg(hexColor(pal.border));
    css += QStringLiteral(
               "QPushButton#Nav { background: transparent; color: %1; border: none; text-align: left;"
               " padding: 10px %3px; border-radius: %4px; font-size: 14px; }\n")
               .arg(hexColor(pal.sidebarText))
               .arg(kPadNavX)
               .arg(kRadiusNav);
    css += QStringLiteral("QPushButton#Nav:hover { background: %1; }\n").arg(hexColor(pal.panelHover));
    css += QStringLiteral("QPushButton#Nav:checked { background: %1; color: %2; font-weight: 700; }\n")
               .arg(hexColor(pal.accentBg), hexColor(pal.accentStrong));
    // 筛选条上的小药丸（地区/排序）：选中态是淡强调底 + 压深后的字 + 强调描边，
    // 与 Rust 的 proxy_panel_tab / 地区药丸同一套形状。
    const QString pillBase = pal.light
        ? QStringLiteral("QPushButton#Pill { background-color: %1; color: %2; border: 1px solid %3;"
                         " border-radius: 12px; padding: 4px 12px; font-size: 12px; }")
              .arg(hexColor(pal.panel), hexColor(pal.textSecondary), hexColor(pal.border))
        : QStringLiteral("QPushButton#Pill { background-color: transparent; color: %1;"
                         " border: 1px solid %2; border-radius: 12px; padding: 4px 12px; font-size: 12px; }")
              .arg(hexColor(pal.muted), hexColor(pal.border));
    css += pillBase + QLatin1Char('\n');
    css += QStringLiteral("QPushButton#Pill:hover { background-color: %1; color: %2; border-color: %3; }")
               .arg(hexColor(pal.accentBg), hexColor(pal.accentStrong), hexColor(pal.accent))
        + QLatin1Char('\n');
    css += QStringLiteral("QPushButton#Pill:checked { background-color: %1; color: %2;"
                          " border: 1px solid %3; font-weight: 700; }")
               .arg(hexColor(pal.accentBg), hexColor(pal.accentStrong), hexColor(pal.accent))
        + QLatin1Char('\n');
    css += QStringLiteral("QLabel#PageTitle { background: transparent; color: %1; font-size: 20px; font-weight: 700; }\n")
               .arg(hexColor(pal.textPrimary));
    css += QStringLiteral("QLabel#Title { background: transparent; color: %1; font-weight: 700; }\n")
               .arg(hexColor(pal.textPrimary));
    css += QStringLiteral("QLabel#Subtitle { background: transparent; color: %1; }\n")
               .arg(hexColor(pal.textSecondary));
    css += QStringLiteral("QLabel#Muted { background: transparent; color: %1; }\n").arg(hexColor(pal.muted));
    css += QStringLiteral("QLabel#Accent { background: transparent; color: %1; font-weight: 700; }\n")
               .arg(hexColor(pal.accentStrong));
    css += QStringLiteral("QLabel#Success { background: transparent; color: %1; font-weight: 700; }\n")
               .arg(hexColor(pal.success));
    css += QStringLiteral("QLabel#Warning { background: transparent; color: %1; font-weight: 700; }\n")
               .arg(hexColor(pal.warning));
    css += QStringLiteral("QLabel#Danger { background: transparent; color: %1; font-weight: 700; }\n")
               .arg(hexColor(pal.danger));
    css += QStringLiteral("QLabel#SuccessPill { background: %1; color: %2; border-radius: %3px; padding: 2px 10px; }\n")
               .arg(hexColor(pal.successBg), hexColor(pal.textPrimary)).arg(kRadiusBtn);
    css += QStringLiteral("QLabel#WarningPill { background: %1; color: %2; border-radius: %3px; padding: 2px 10px; }\n")
               .arg(hexColor(pal.warningBg), hexColor(pal.textPrimary)).arg(kRadiusBtn);
    css += QStringLiteral("QLabel#DangerPill { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 2px 10px; }\n")
               .arg(hexColor(pal.dangerBg), hexColor(pal.textPrimary), hexColor(pal.dangerStroke))
               .arg(kRadiusBtn);
    // 卡片：统一清晰描边 + 背景色，确保每一个面板和卡片都具有独立、清晰的块状感。
    // 内层的 InnerBlock 用 panelHover 作底、borderSubtle 描边 —— 和 Rust
    // `widgets.rs:206`（内框 stroke 用 border_subtle）、`views/proxy.rs:849`（内框 fill
    // 用 panel_hover）同一对口径；以前这里写死 #f8fafc/#e2e8f0，主题里没有这两个值。
    css += QStringLiteral("QFrame#Card { background: %1; border: 1px solid %2; border-radius: 12px; }\n"
                          "QFrame#InnerBlock { background-color: %3; border: 1px solid %4;"
                          " border-radius: 8px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.border), hexColor(pal.panelHover),
                    hexColor(pal.borderSubtle));
    css += QStringLiteral("QFrame#StatusStrip { background: %1; border-top: 1px solid %2; min-height: 40px; }\n")
               .arg(hexColor(pal.sidebar), hexColor(pal.border));
    css += QStringLiteral("QLabel#StatusLabel { background: transparent; color: %1; font-size: 13px; font-weight: 500; }\n")
               .arg(hexColor(pal.textPrimary));
    css += QStringLiteral(
               "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
               " padding: 6px 14px; font-size: 13px; font-weight: 500; min-height: 20px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border));
    css += QStringLiteral("QPushButton:hover { background-color: %1; border-color: %2; color: %3; }\n")
               .arg(hexColor(pal.accentBg), hexColor(pal.accent), hexColor(pal.accentStrong));
    css += QStringLiteral("QPushButton:pressed { background-color: %1; border-color: %2; }\n")
               .arg(hexColor(pal.panelHover), hexColor(pal.accentHover));
    css += QStringLiteral("QPushButton:disabled { color: %1; border-color: %2; background-color: %3; }\n")
               .arg(hexColor(pal.muted), hexColor(pal.borderSubtle), hexColor(pal.panelHover));
    css += QStringLiteral(
               "QPushButton#Primary { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
               " padding: 6px 16px; font-size: 13px; font-weight: 700; min-height: 20px; }\n")
               .arg(hexColor(pal.primaryBtn), hexColor(pal.onPrimary), hexColor(pal.primaryBtnHover));
    css += QStringLiteral("QPushButton#Primary:hover { background-color: %1; border-color: %1; }\n").arg(hexColor(pal.primaryBtnHover));
    css += QStringLiteral("QPushButton#Primary:pressed { background-color: %1; border-color: %1; }\n").arg(hexColor(pal.primaryBtnPress));
    css += QStringLiteral(
               "QPushButton#Danger { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
               " padding: 6px 14px; font-size: 13px; font-weight: 700; min-height: 20px; }\n")
               .arg(hexColor(pal.dangerBg), hexColor(pal.danger), hexColor(pal.dangerStroke));
    css += QStringLiteral("QPushButton#Danger:hover { background-color: %1; border-color: %2; color: %2; }\n")
               .arg(hexColor(pal.panelHover), hexColor(pal.danger));
    css += QStringLiteral(
               "QPushButton#Small { padding: 3px 8px; border: 1px solid %1; border-radius: 6px; font-size: 12px; min-height: 16px; }\n")
               .arg(hexColor(pal.border));
    css += QStringLiteral("QPushButton#Small:hover { background-color: %1; border-color: %2; color: %3; }\n")
               .arg(hexColor(pal.accentBg), hexColor(pal.accent), hexColor(pal.accentStrong));
    css += QStringLiteral(
               "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox {"
               " background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 10px; font-size: 13px; min-height: 20px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border));
    css += QStringLiteral("QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus { border-color: %1; }\n")
               .arg(hexColor(pal.accent));
    css += QStringLiteral(
               "QComboBox {\n"
               " background-color: %1;\n"
               " color: %2;\n"
               " border: 1px solid %3;\n"
               " border-radius: 6px;\n"
               " padding: 4px 28px 4px 10px;\n"
               " font-size: 13px;\n"
               " min-height: 22px;\n"
               "}\n"
               "QComboBox:hover { border-color: %4; }\n"
               "QComboBox:focus { border-color: %4; }\n"
               "QComboBox::drop-down {\n"
               " subcontrol-origin: padding;\n"
               " subcontrol-position: top right;\n"
               " width: 24px;\n"
               " border-left: 1px solid %3;\n"
               " border-top-right-radius: 6px;\n"
               " border-bottom-right-radius: 6px;\n"
               " background-color: %5;\n"
               "}\n"
               // Qt 的 ::down-arrow 只认 image:，CSS 三角形会被整个忽略 —— 之前这里
               // 写了三角形却没写 image，结果未走 decorateComboBox 的下拉框一个箭头都没有。
               "QComboBox::down-arrow {\n"
               " image: url(%8);\n"
               " width: 10px; height: 10px;\n"
               "}\n"
               "QComboBox QAbstractItemView {\n"
               " background-color: %1;\n"
               " color: %2;\n"
               " border: 1px solid %3;\n"
               " border-radius: 6px;\n"
               " selection-background-color: %6;\n"
               " selection-color: %7;\n"
               " padding: 4px;\n"
               "}\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border),
                    hexColor(pal.accent), hexColor(pal.panelHover), hexColor(pal.accentBg),
                    hexColor(pal.accentStrong))
               .arg(Icons::arrowImage(Icons::Arrow::Down, pal.textSecondary));
    css += QStringLiteral("QCheckBox, QRadioButton { background: transparent; color: %1; spacing: 8px; font-size: 13px; }\n")
               .arg(hexColor(pal.textPrimary));
    css += QStringLiteral(
               "QRadioButton::indicator {\n"
               " width: 14px; height: 14px;\n"
               " border-radius: 8px;\n"
               " border: 2px solid %1;\n"
               " background-color: %2;\n"
               "}\n"
               "QRadioButton::indicator:hover {\n"
               " border-color: %3;\n"
               "}\n"
               "QRadioButton::indicator:checked {\n"
               " border: 2px solid %3;\n"
               " background-color: %3;\n"
               "}\n"
               "QRadioButton::indicator:disabled {\n"
               " border-color: %4;\n"
               " background-color: %5;\n"
               "}\n"
               "QCheckBox::indicator {\n"
               " width: 14px; height: 14px;\n"
               " border-radius: 4px;\n"
               " border: 1px solid %1;\n"
               " background-color: %2;\n"
               "}\n"
               "QCheckBox::indicator:hover {\n"
               " border-color: %3;\n"
               "}\n"
               "QCheckBox::indicator:checked {\n"
               " border-color: %3;\n"
               " background-color: %3;\n"
               "}\n")
               .arg(hexColor(pal.border), hexColor(pal.panel), hexColor(pal.accent),
                    hexColor(pal.borderSubtle), hexColor(pal.panelHover));
    css += QStringLiteral("QGroupBox { background: transparent; color: %1; border: 1px solid %2; border-radius: 10px; margin-top: 10px; padding-top: 8px; }\n")
               .arg(hexColor(pal.textSecondary), hexColor(pal.borderSubtle));
    css += QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }\n");
    css += QStringLiteral(
               "QTableView, QTableWidget, QTreeView, QListWidget { background: %1; alternate-background-color: %2;"
               " color: %3; border: 1px solid %4; border-radius: 8px; gridline-color: transparent; selection-background-color: %5;"
               " selection-color: %6; outline: none; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.panelHover), hexColor(pal.textPrimary),
                    hexColor(pal.border), hexColor(pal.accentBg), hexColor(pal.accentStrong));
    css += QStringLiteral("QTableView::item, QTableWidget::item { padding: 6px 8px; border: none; }\n");
    css += QStringLiteral("QHeaderView::section { background: %1; color: %2; border: none; border-bottom: 1px solid %3; padding: 8px 10px; font-weight: 700; font-size: 12px; }\n")
               .arg(hexColor(pal.panelHover), hexColor(pal.muted), hexColor(pal.border));
    css += QStringLiteral("QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }\n");
    css += QStringLiteral("QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }\n");
    css += QStringLiteral("QScrollBar::handle { background: %1; border-radius: 5px; min-height: 24px; }\n")
               .arg(pal.light ? rgbaColor(pal.textPrimary, 40) : hexColor(pal.border));
    css += QStringLiteral("QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }\n");
    css += QStringLiteral("QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }\n");
    css += QStringLiteral("QTabBar { background: transparent; border: none; qproperty-drawBase: 0; }\n");
    css += QStringLiteral("QTabBar::base { border: none; height: 0px; background: transparent; }\n");
    css += QStringLiteral("QTabBar::tab { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 7px 18px; margin-right: 8px; margin-top: 2px; margin-bottom: 6px; font-weight: 600; font-size: 13px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.muted), hexColor(pal.borderSubtle));
    css += QStringLiteral("QTabBar::tab:hover { background: %1; color: %2; border-color: %3; }\n")
               .arg(hexColor(pal.accentBg), hexColor(pal.accentStrong), hexColor(pal.accent));
    css += QStringLiteral("QTabBar::tab:selected { background: %1; color: %2; border: 1px solid %3; font-weight: 700; }\n")
               .arg(hexColor(pal.accentBg), hexColor(pal.accentStrong), hexColor(pal.accent));
    // 运行日志的"深色终端"三件套取自调色板字段（见 fromInk），不是这里的裸色值。
    css += QStringLiteral("QPlainTextEdit#LogView { background: %1; color: %2;"
                          " font-family: 'Cascadia Code', 'Consolas', 'Courier New', monospace;"
                          " font-size: 12px; border: 1px solid %3; border-radius: 8px; padding: 10px; }\n")
               .arg(hexColor(pal.logViewBg), hexColor(pal.logViewFg), hexColor(pal.logViewBorder));
    css += QStringLiteral("QToolTip { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px 8px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border));
    return css;
}

void decorateCard(QFrame *card)
{
    if (card == nullptr) {
        return;
    }
    card->setObjectName(QStringLiteral("Card"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setGraphicsEffect(nullptr);
    setWidgetCss(card, QStringLiteral("QFrame#Card { background-color: {panel};"
                                      " border: 1px solid {border}; border-radius: 12px; }"));
}

void decorateInnerBlock(QFrame *block)
{
    if (block == nullptr) {
        return;
    }
    block->setObjectName(QStringLiteral("InnerBlock"));
    block->setAttribute(Qt::WA_StyledBackground, true);
    setWidgetCss(block, QStringLiteral("QFrame#InnerBlock { background-color: {panelHover};"
                                       " border: 1px solid {borderSubtle}; border-radius: 8px; }"));
}

QString cssWithPalette(const QString &pattern)
{
    const Palette pal = current();
    // 取色只能要 HexRgb：`name(QColor::HexArgb)` 给的是 "#AARRGGBB"，以前有代码按
    // 前 7 个字符截它，于是 #0f172a 的正文变成 #ff0f17、#2f6feb 的强调蓝变成
    // #ff2f6f —— 全站下拉框连边框带箭头一起泛红，而纯白 (#ffffff) 截完仍是纯白，
    // 所以"底色对不对"那套像素探针完全没抓到。这里统一走 HexRgb。
    // 名字与颜色按同一个顺序配对。这里不用"成员指针表"：MSVC 解析不了那种嵌套
    // 花括号初始化（C2059）。
    static const QStringList kSlots = {
        QStringLiteral("pageBg"),          QStringLiteral("sidebar"),
        QStringLiteral("panel"),           QStringLiteral("panelHover"),
        QStringLiteral("border"),          QStringLiteral("borderSubtle"),
        QStringLiteral("textPrimary"),     QStringLiteral("textSecondary"),
        QStringLiteral("muted"),           QStringLiteral("accent"),
        QStringLiteral("accentHover"),     QStringLiteral("accentBg"),
        QStringLiteral("accentStrong"),    QStringLiteral("primaryBtn"),
        QStringLiteral("primaryBtnHover"), QStringLiteral("primaryBtnPress"),
        QStringLiteral("onPrimary"),       QStringLiteral("success"),
        QStringLiteral("successBg"),       QStringLiteral("warning"),
        QStringLiteral("warningBg"),       QStringLiteral("danger"),
        QStringLiteral("dangerBg"),        QStringLiteral("dangerStroke"),
        QStringLiteral("sidebarText"),     QStringLiteral("sidebarMuted"),
    };
    const QList<QColor> colors = {
        pal.pageBg,          pal.sidebar,        pal.panel,
        pal.panelHover,      pal.border,         pal.borderSubtle,
        pal.textPrimary,     pal.textSecondary,  pal.muted,
        pal.accent,          pal.accentHover,    pal.accentBg,
        pal.accentStrong,    pal.primaryBtn,     pal.primaryBtnHover,
        pal.primaryBtnPress, pal.onPrimary,      pal.success,
        pal.successBg,       pal.warning,        pal.warningBg,
        pal.danger,          pal.dangerBg,       pal.dangerStroke,
        pal.sidebarText,     pal.sidebarMuted,
    };

    QString out = pattern;
    static const QRegularExpression slotPattern(QStringLiteral("\\{([A-Za-z]+)\\}"));
    QVector<QRegularExpressionMatch> matches;
    auto it = slotPattern.globalMatch(pattern);
    while (it.hasNext()) {
        // 从后往前替换：替换会把后面的偏移挪掉。
        matches.push_front(it.next());
    }
    for (const QRegularExpressionMatch &match : std::as_const(matches)) {
        const int index = kSlots.indexOf(match.captured(1));
        if (index < 0) {
            continue;   // 原样留着，守卫测试会把没换掉的 {槽名} 点名
        }
        out.replace(match.capturedStart(), match.capturedLength(),
                    colors.at(index).name(QColor::HexRgb));
    }
    // 两个**非颜色**槽：下拉框/旋钮的箭头图片路径。颜色循环不认识它们（会原样留着，
    // 未解析槽守卫也不该报它），在这里按当前调色板补上。
    if (out.contains(QStringLiteral("{arrowDown}"))) {
        out.replace(QStringLiteral("{arrowDown}"), Icons::arrowImage(Icons::Arrow::Down,
                                                                     pal.textSecondary));
    }
    if (out.contains(QStringLiteral("{arrowUp}"))) {
        out.replace(QStringLiteral("{arrowUp}"), Icons::arrowImage(Icons::Arrow::Up,
                                                                   pal.textSecondary));
    }
    return out;
}

void setWidgetCss(QWidget *widget, const QString &pattern)
{
    if (widget == nullptr) {
        return;
    }
    widget->setProperty(kCssTemplateProperty, pattern);
    widget->setStyleSheet(cssWithPalette(pattern));
}

}
}
