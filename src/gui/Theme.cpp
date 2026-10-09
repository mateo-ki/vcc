#include "gui/Theme.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QMutex>
#include <QMutexLocker>
#include <QFrame>
#include <QWidget>

#include <cmath>

#include "gui/ThemeNames.h"

namespace {

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
    // 中性色：左侧菜单纯白，右侧内容页压深一档蓝灰，卡片再回到纯白 —— 分层靠
    // "页面比卡片深"这一条，不靠描边。用户 2026-10-06：卡片和周围背景快分不开了，
    // 菜单要更浅、内容要更深。
    ink.page = rgb(0xE8EEF7);
    ink.sidebar = rgb(0xFFFFFF);
    ink.panel = rgb(0xFFFFFF);
    ink.panelHover = rgb(0xF1F6FD);
    ink.border = rgb(0xE9F0FA);
    ink.text = rgb(0x1A2028);
    ink.textSecondary = rgb(0x3F4A57);
    ink.muted = rgb(0x64707E);
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
    {
        QMutexLocker locker(&g_themeMutex);
        g_current = pal;
        g_currentId = resolved;
    }
    // 应用级样式表：作用域覆盖所有原生控件，等价于 Rust 的 ctx.set_style。
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance())) {
        app->setStyleSheet(styleSheet(pal));
    }
}

QString styleSheet(const Palette &pal)
{
    QString css;
    css.reserve(4096);
    css += QStringLiteral("QWidget { background: %1; color: %2; }\n")
              .arg(hexColor(pal.pageBg), hexColor(pal.textPrimary));
    css += QStringLiteral(
               "QFrame#Sidebar { background: %1; border: none; border-right: 1px solid %2; }\n")
               .arg(hexColor(pal.sidebar), hexColor(pal.borderSubtle));
    css += QStringLiteral("QLabel#BrandTitle { background: transparent; color: %1; font-size: 17px; font-weight: 700; }\n")
               .arg(hexColor(pal.sidebarText));
    css += QStringLiteral("QLabel#BrandSubtitle { background: transparent; color: %1; font-size: 12px; }\n")
               .arg(hexColor(pal.sidebarMuted));
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
        ? QStringLiteral("QPushButton#Pill { background: %1; color: %2; border: 1px solid transparent;"
                         " border-radius: 12px; padding: 3px 10px; font-size: 12px; }")
              .arg(hexColor(pal.panel), hexColor(pal.muted))
        : QStringLiteral("QPushButton#Pill { background: transparent; color: %1;"
                         " border: 1px solid transparent; border-radius: 12px; padding: 3px 10px; }")
              .arg(hexColor(pal.muted));
    css += pillBase + QLatin1Char('\n');
    css += QStringLiteral("QPushButton#Pill:hover { background: %1; color: %2; }")
               .arg(hexColor(pal.panelHover), hexColor(pal.textPrimary))
        + QLatin1Char('\n');
    css += QStringLiteral("QPushButton#Pill:checked { background: %1; color: %2;"
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
    // 卡片：浅色主题不描边（靠阴影 + 页面底更深一档分层），深色主题留 1px 边。
    css += pal.light
               ? QStringLiteral("QFrame#Card { background: %1; border: none; border-radius: %2px; }\n")
                     .arg(hexColor(pal.panel))
                     .arg(kRadiusCard)
               : QStringLiteral("QFrame#Card { background: %1; border: 1px solid %2; border-radius: %3px; }\n")
                     .arg(hexColor(pal.panel), hexColor(pal.border))
                     .arg(kRadiusCard);
    css += QStringLiteral("QFrame#StatusStrip { background: %1; border-top: 1px solid %2; }\n")
               .arg(hexColor(pal.sidebar), hexColor(pal.borderSubtle));
    css += QStringLiteral(
               "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
               " padding: %5px 6px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border))
               .arg(kRadiusBtn)
               .arg(13);
    css += QStringLiteral("QPushButton:hover { background: %1; border-color: %2; }\n")
               .arg(hexColor(pal.panelHover), hexColor(pal.accent));
    css += QStringLiteral("QPushButton:pressed { background: %1; border-color: %2; }\n")
               .arg(hexColor(pal.panelHover), hexColor(pal.accentHover));
    css += QStringLiteral("QPushButton:disabled { color: %1; border-color: %2; }\n")
               .arg(hexColor(pal.muted), hexColor(pal.borderSubtle));
    css += QStringLiteral(
               "QPushButton#Primary { background: %1; color: %2; border: none; border-radius: %3px;"
               " padding: %5px 6px; font-weight: 700; }\n")
               .arg(hexColor(pal.primaryBtn), hexColor(pal.onPrimary)).arg(kRadiusBtn).arg(13);
    css += QStringLiteral("QPushButton#Primary:hover { background: %1; }\n").arg(hexColor(pal.primaryBtnHover));
    css += QStringLiteral("QPushButton#Primary:pressed { background: %1; }\n").arg(hexColor(pal.primaryBtnPress));
    css += QStringLiteral(
               "QPushButton#Danger { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; }\n")
               .arg(hexColor(pal.dangerBg), hexColor(pal.danger), hexColor(pal.dangerStroke))
               .arg(kRadiusBtn);
    css += QStringLiteral(
               "QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
               " background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 5px 8px; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border))
               .arg(kRadiusBtn - 6);
    css += QStringLiteral("QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: %1; }\n")
               .arg(hexColor(pal.accent));
    css += QStringLiteral("QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3; selection-background-color: %4; selection-color: %5; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border),
                    hexColor(pal.accentBg), hexColor(pal.accentStrong));
    css += QStringLiteral("QCheckBox, QRadioButton { background: transparent; color: %1; }\n")
               .arg(hexColor(pal.textPrimary));
    css += QStringLiteral("QGroupBox { background: transparent; color: %1; border: 1px solid %2; border-radius: %3px; margin-top: 8px; }\n")
               .arg(hexColor(pal.textSecondary), hexColor(pal.borderSubtle)).arg(kRadiusCard - 6);
    css += QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: 10px; }\n");
    css += QStringLiteral(
               "QTableView, QTreeView, QListWidget { background: %1; alternate-background-color: %2;"
               " color: %3; border: 1px solid %4; gridline-color: %5; selection-background-color: %6;"
               " selection-color: %7; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.panelHover), hexColor(pal.textPrimary),
                    hexColor(pal.border), hexColor(pal.borderSubtle), hexColor(pal.accentBg),
                    hexColor(pal.accentStrong));
    css += QStringLiteral("QHeaderView::section { background: %1; color: %2; border: none; padding: 6px 8px; font-weight: 700; }\n")
               .arg(hexColor(pal.panelHover), hexColor(pal.muted));
    css += QStringLiteral("QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }\n");
    css += QStringLiteral("QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }\n");
    css += QStringLiteral("QScrollBar::handle { background: %1; border-radius: 5px; min-height: 24px; }\n")
               .arg(pal.light ? rgbaColor(pal.textPrimary, 40) : hexColor(pal.border));
    css += QStringLiteral("QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }\n");
    css += QStringLiteral("QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }\n");
    css += QStringLiteral("QTabBar::tab { background: transparent; color: %1; padding: 6px 12px; border-radius: %2px; }\n")
               .arg(hexColor(pal.muted)).arg(kRadiusBtn);
    css += QStringLiteral("QTabBar::tab:selected { background: %1; color: %2; }\n")
               .arg(hexColor(pal.accentBg), hexColor(pal.accentStrong));
    css += QStringLiteral("QToolTip { background: %1; color: %2; border: 1px solid %3; }\n")
               .arg(hexColor(pal.panel), hexColor(pal.textPrimary), hexColor(pal.border));
    return css;
}

void decorateCard(QFrame *card)
{
    card->setObjectName(QStringLiteral("Card"));
    const Palette pal = current();
    // Qt 样式表没有 box-shadow：浅色主题的 0/6 blur24 rgba(24,44,78,16) 用图形效果实现。
    if (!pal.light) {
        card->setGraphicsEffect(nullptr);
        return;
    }
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setOffset(0, 6);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(24, 44, 78, 16));
    card->setGraphicsEffect(shadow);
}

}
}
