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

/// 卡片分层：浅色主题靠阴影分层所以不描边；深色主题阴影会糊成一团脏的，反过来留一条 1px 边。
/// Qt 的样式表没有 box-shadow，用 QGraphicsDropShadowEffect 实现同一件事。
void decorateCard(QFrame *card);

}
}
