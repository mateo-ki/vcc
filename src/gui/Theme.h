#pragma once

#include <QColor>
#include <QRgb>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QFrame;
class QWidget;
QT_END_NAMESPACE

namespace Gui {
namespace Theme {

/// 一处语义色。字段与 Rust `gui/theme.rs` 的 `Palette` 一一对应。
struct Palette {
    QColor pageBg;
    QColor sidebar;
    QColor panel;
    QColor panelHover;
    QColor border;
    QColor borderSubtle;
    QColor textPrimary;
    QColor textSecondary;
    QColor muted;
    QColor accent;
    QColor accentHover;
    QColor accentBg;
    /// 淡强调底色之上的文字色：浅色主题下 accent 本身太亮（实测 2.78:1），要压深一档。
    QColor accentStrong;
    QColor primaryBtn;
    QColor primaryBtnHover;
    QColor primaryBtnPress;
    /// 主按钮上的文字色：浅底按钮要用深色字，深底按钮要用浅色字。
    QColor onPrimary;
    QColor success;
    QColor successBg;
    QColor warning;
    QColor warningBg;
    QColor danger;
    QColor dangerBg;
    /// 危险按钮的描边：从 danger 与底色推出来，保证浅底主题下也不会糊成一片。
    QColor dangerStroke;
    /// 运行日志那一块的"深色终端"三件套。Rust 侧日志只是普通面板上的 monospace 文本
    /// （`views/proxy.rs::ui_proxy_logs`），这层深底是 Qt 侧自己加的观感；既然留着，
    /// 就把它收进调色板当语义字段，别让它当应用级样式表里仅剩的几个裸色值 ——
    /// `ComboTheme.appStyleSheetOnlyUsesPaletteColors` 就是抓这个的。
    QColor logViewBg;
    QColor logViewFg;
    QColor logViewBorder;

    QColor sidebarText;
    QColor sidebarMuted;
    bool light = true;
};

/// id + 界面显示名 + 强调色。一套主题 = 一行：中性色全家族共用，
/// 按钮底色、淡彩底、描边色都是从强调色派生出来的。
struct Definition {
    QString id;
    QString name;
    QRgb accent;
};

/// 顺序就是设置页里的顺序。深色那几套（obsidian / latte / midnight / mocha / rosepine /
/// linear）2026-10-06 按要求全部删掉，只留"清爽浅白"这一个家族。
const QVector<Definition> &themes();

inline const QString kDefaultThemeId = QStringLiteral("soft-cobalt");

/// 结构（圆角 / 内边距 / 分层方式）是全局一份，颜色是每套主题各一份。
/// Rust 侧是 egui 的逻辑像素单位，这里原值当 px 用。
constexpr int kRadiusCard = 20;
constexpr int kRadiusNav = 14;
constexpr int kRadiusBtn = 16;
constexpr int kPadCard = 22;
constexpr int kPadNavX = 13;

/// 线性混色：`t=0` 全 a，`t=1` 全 b。状态的淡底（*_bg）都靠它从主题自己的颜色推出来。
QColor mix(const QColor &a, const QColor &b, double t);
QColor darken(const QColor &color, double t);
QColor lighten(const QColor &color, double t);

/// WCAG 2.x 对比度比值（1.0 = 完全同色，21.0 = 黑/白）。
double contrast(const QColor &a, const QColor &b);

/// 认不出的存值（旧主题 id、手改过的值）落到默认那套，而不是随便挑一套。
Palette palette(const QString &id);
const Definition &themeById(const QString &id);

/// 当前主题：切换时读它，避免每次刷新都解析字符串。
QString currentId();
Palette current();
bool isLight();

/// 装主题：写静态调色板 + 把整份 QSS 下发给 QApplication（原生控件的底色/描边/选中色
/// 必须跟着走，否则主题只改了自绘部分）。
void apply(const QString &id);
QString styleSheet(const Palette &palette);

/// 按控件挂的样式表用语义槽写颜色，别在页面里写死 hex。
/// `Theme::cssWithPalette("color: {muted}; border: 1px solid {borderSubtle};")`
/// 里的 `{...}` 会换成当前调色板的同名色（#rrggbb）。
/// Why：页面里散着的 `#ef4444`/`#10b981`/`#f59e0b` 有三宗罪——① Rust 那侧
/// `theme.rs` 早就定了"页面上不许出现红和黄"，写死的色值把这条约束绕过去了；
/// ② 切主题时应用级 QSS 会重来一遍，按控件写死的这份不会；③ 一个语义色有好几种
/// 近似写法（#64748b/#888888/#94a3b8 都当"次要文字"在用），谁也改不动谁。
/// 认不出的槽名保持原样，好让守卫测试把漏字的地方直接指出来。
QString cssWithPalette(const QString &pattern);

/// 用 `cssWithPalette` 给某个控件挂样式表，并把原始模板记在控件上。
/// Why：应用级 QSS 在切换主题时由 Qt 自己重解析，但**按控件挂的那一份不会**——
/// 里面只要用到随主题变的槽（accent / accentBg / primaryBtn 这一族），切完主题
/// 就会停在上一个颜色上（实测：切到"陶土橙"后下拉框的聚焦边框还是钴蓝）。
/// `apply()` 会遍历所有顶层窗口，把带模板的控件按新调色板重挂一遍。
/// 只写中性槽（muted / panel / border / text 这几族）的可以直接 `setStyleSheet`：
/// 五套主题共用同一份中性 ramp，这是 ShellTests 里钉住的不变量。
void setWidgetCss(QWidget *widget, const QString &pattern);

/// 卡片：白底 + 1px 描边 + 12px 圆角。**背景必须由控件自己带一份**——
/// 应用级那条 `QFrame#Card` 实测铺不上（离屏量卡片自己的内边距：只有应用级规则时是
/// #efefef，控件带一份才是 #ffffff），所以以前散在各页的"每卡一份 QFrame#Card{...}"
/// 不是冗余，删不得；这里把它收成一处（颜色是语义槽，切主题由 apply() 重挂）。
void decorateCard(QFrame *card);

/// 卡片里的内块：panelHover 底 + borderSubtle 描边 + 8px 圆角（同一套道理，也得自带一份）。
/// 对应 Rust `views/proxy.rs:849` 的内框 fill=panel_hover、`widgets.rs:206` 的 stroke=border_subtle。
void decorateInnerBlock(QFrame *block);

}
}
