#pragma once

#include <QColor>
#include <QPixmap>
#include <QRectF>

class QAbstractButton;
class QPainter;
class QWidget;

namespace Gui::Icons {

/// 对应 Rust gui/icons.rs 的 `Icon` 枚举。这里只移植侧栏导航真正会画的那五枚
/// （Dashboard / Lightning / Subscription / Gear / Plug，见 sidebar.rs 的五个 nav_item）；
/// 其余 25 枚等有用到再说，别把 900 行 painter 代码搬进来却一笔都不画。
/// Info 是**第六枚、Rust 没有**：侧栏「关于」那行需要一枚自己的符号，
/// 借齿轮（系统设置）或插头（外部接口）都会让人点错页。
enum class Kind { Dashboard, Lightning, Subscription, Gear, Plug, Info };

/// Rust 的 `nav_item` 给的是 17.0；他连着两轮要『图标大一点』，现在放到 20
/// （有意偏离，别当移植不准对回去）。
inline constexpr int kNavIconSize = 20;

/// 侧栏五枚图标的画法。几何最初照 Rust `icons.rs:42-186` 逐笔移植过，2026-10-10 他看了
/// 对照表选了另一套（统一一档笔画 + 齿轮换滑杆 + 插头换链接）—— 现在是对 Rust 的
/// 有意偏离，别当"移植不准"改回去。
/// 不画成 SVG 资源是刻意的：`make-dist.mjs` 会剪掉 Qt6Svg.dll 和 qsvgicon 插件。
void paint(QPainter *painter, const QRectF &rect, Kind kind, const QColor &color);

/// 把一枚图标渲成 pixmap（按 dpr 放大画布、记下设备像素比，交给 QIcon 缩到逻辑尺寸）。
QPixmap render(Kind kind, const QColor &color, int size, qreal dpr = 2.0);

/// 给导航按钮挂图标，并把 kind 记在属性 `navIconKind` 上 —— 换主题时靠它重画。
void setNavIcon(QAbstractButton *button, Kind kind);

/// 选中 = theme::accent()，未选中 = theme::sidebar_muted()（Rust nav_item 的那两个色）。
void refreshNavIcon(QAbstractButton *button);

/// 遍历一棵子树，把所有挂了 `navIconKind` 的按钮按当前调色板重画一遍。
void refreshNavIconsIn(QWidget *root);

/// QSS 的下拉/旋钮箭头方向。
enum class Arrow { Up, Down };

/// Qt 的 QSS 里 `::down-arrow` / `::up-arrow` **只认 `image:`**：浏览器那套
/// "width:0;height:0 + border 拼三角形"在这里画不出三角，`image: none` 又会把原生箭头
/// 关掉（表现：下拉框完全没有箭头，或者一个实心黑块）。所以箭头按当前调色板渲成 PNG
/// 落到临时目录，文件名带颜色 —— 同一份颜色只写一次，切主题就是另一个文件。
/// 返回值已经是 `/` 分隔，可以直接塞进 `url(...)`。
QString arrowImage(Arrow arrow, const QColor &color);

} // namespace Gui::Icons
