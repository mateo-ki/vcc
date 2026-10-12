#include "gui/Icons.h"

#include <QAbstractButton>
#include <QBrush>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QSize>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "gui/Theme.h"

namespace Gui::Icons {
namespace {

/// 压一档透明度用（控制台的另外两格）。Rust 那边是 `gamma_multiply(0.4)`，
/// 它 RGBA 四个通道一起乘；这里按他选的 C 画法只保留"淡一点"这一层意思。
QColor withAlpha(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

/// Rust 的 `Margin::symmetric` 之外，nav 图标只有两种配色，取自当前调色板，
/// 所以主题一换必须重画一遍（Theme::apply 会调 refreshNavIconsIn）。
QColor navIconColor(const QAbstractButton *button)
{
    const Theme::Palette pal = Theme::current();
    return button->isChecked() ? pal.accent : pal.sidebarMuted;
}

constexpr const char kIconKindProperty[] = "navIconKind";

} // namespace

void paint(QPainter *painter, const QRectF &rect, Kind kind, const QColor &color)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = rect.center();
    const qreal s = std::min(rect.width(), rect.height());
    // 2026-10-10 他看了对照表（tmp/navicon-sheet2.png）点了 C 那一列：五枚统一成
    // "一档笔画 + 圆头圆角"，并把齿轮换成滑杆、插头换成链接。
    // 这是对 Rust `icons.rs:42-186` 几何的有意偏离（原来那套 6 根细辐条在 17px 下读作
    // 雪花、实心闪电比邻居重一档、不闭合的托盘像没画完），别再"顺手对齐回去"。
    const qreal w = s * 0.085;
    QPen pen(color, w);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    switch (kind) {
    case Kind::Dashboard: {
        // 2x2 圆角格，对角实心、另两格压淡 —— 读作"面板"，且四格同尺寸不再有混合重量。
        const qreal pad = s * 0.06;
        const qreal half = s * 0.36;
        const qreal radius = s * 0.08;
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRoundedRect(QRectF(c.x() - half - pad, c.y() - half - pad, half, half),
                                 radius, radius);
        painter->drawRoundedRect(QRectF(c.x() + pad, c.y() + pad, half, half), radius, radius);
        painter->setBrush(withAlpha(color, 105));
        painter->drawRoundedRect(QRectF(c.x() + pad, c.y() - half - pad, half, half), radius,
                                 radius);
        painter->drawRoundedRect(QRectF(c.x() - half - pad, c.y() + pad, half, half), radius,
                                 radius);
        break;
    }
    case Kind::Lightning: {
        // 收窄、横摆更小的实心闪电：原来那枚太宽，17px 下比整列都重。
        QPolygonF bolt;
        bolt << QPointF(c.x() + s * 0.10, c.y() - s * 0.44)
           << QPointF(c.x() - s * 0.22, c.y() + s * 0.06)
           << QPointF(c.x() - s * 0.02, c.y() + s * 0.06)
           << QPointF(c.x() - s * 0.10, c.y() + s * 0.44)
           << QPointF(c.x() + s * 0.24, c.y() - s * 0.06)
           << QPointF(c.x() + s * 0.04, c.y() - s * 0.06);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawPolygon(bolt);
        break;
    }
    case Kind::Subscription: {
        // 三条订阅链接 + 一枚向下箭头（"拉取列表"），笔画和其他四枚同一档。
        painter->setBrush(Qt::NoBrush);
        painter->setPen(pen);
        painter->drawLine(QPointF(c.x() - s * 0.38, c.y() - s * 0.30),
                          QPointF(c.x() + s * 0.38, c.y() - s * 0.30));
        painter->drawLine(QPointF(c.x() - s * 0.38, c.y()), QPointF(c.x() + s * 0.05, c.y()));
        painter->drawLine(QPointF(c.x() - s * 0.38, c.y() + s * 0.30),
                          QPointF(c.x() + s * 0.38, c.y() + s * 0.30));
        painter->drawLine(QPointF(c.x() + s * 0.30, c.y() - s * 0.14),
                          QPointF(c.x() + s * 0.30, c.y() + s * 0.18));
        painter->drawLine(QPointF(c.x() + s * 0.16, c.y() + s * 0.04),
                          QPointF(c.x() + s * 0.30, c.y() + s * 0.18));
        painter->drawLine(QPointF(c.x() + s * 0.44, c.y() + s * 0.04),
                          QPointF(c.x() + s * 0.30, c.y() + s * 0.18));
        break;
    }
    case Kind::Gear: {
        // 三根滑杆（设置页的通用符号）。Rust 的齿轮是"圆 + 6 根细辐条"，
        // 17px 下辐条和圈糊成一团，读作雪花/原子。
        painter->setBrush(Qt::NoBrush);
        painter->setPen(pen);
        for (int i = 0; i < 3; ++i) {
            const qreal y = c.y() + (i - 1) * s * 0.28;
            painter->drawLine(QPointF(c.x() - s * 0.36, y), QPointF(c.x() + s * 0.36, y));
            const qreal knobX = c.x() + (i == 1 ? -s * 0.18 : s * 0.16);
            painter->drawEllipse(QPointF(knobX, y), s * 0.09, s * 0.09);
        }
        break;
    }
    case Kind::Plug: {
        // 两段互相钩住的弧 = "链接/接入"，外部接口页的通用符号；
        // 原来的插头（实心块 + 两脚 + 一根线）在 17px 下读作插座或 ¥。
        QPen link(color, s * 0.11);
        link.setCapStyle(Qt::RoundCap);
        link.setJoinStyle(Qt::RoundJoin);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(link);
        painter->drawArc(QRectF(c.x() - s * 0.46, c.y() - s * 0.20, s * 0.54, s * 0.40), 25 * 16,
                         290 * 16);
        painter->drawArc(QRectF(c.x() - s * 0.08, c.y() - s * 0.20, s * 0.54, s * 0.40), 205 * 16,
                         290 * 16);
        break;
    }
    }
}

QPixmap render(Kind kind, const QColor &color, int size, qreal dpr)
{
    // 画布按 dpr 放大、再把 devicePixelRatio 记上：QIcon 缩到逻辑尺寸时走的是同一套换算，
    // 100% 缩放的屏上不糊，200% 的屏上也不会变成马赛克。
    // 但喂给 paint 的必须是**逻辑尺寸**：QPainter 在带 DPR 的 QPixmap 上还会再把坐标乘
    // 一次 dpr，这里写 pixels 就会把图形整块推到画布外，界面上只剩左上四分之一 ——
    // 2026-10-10 他截图报的"菜单图标不完整"就是这个。
    const int pixels = qMax(1, int(size * dpr));
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paint(&painter, QRectF(0.0, 0.0, size, size), kind, color);
    painter.end();
    return pixmap;
}

void setNavIcon(QAbstractButton *button, Kind kind)
{
    button->setProperty(kIconKindProperty, int(kind));
    button->setIconSize(QSize(kNavIconSize, kNavIconSize));
    refreshNavIcon(button);
}

void refreshNavIcon(QAbstractButton *button)
{
    const QVariant stored = button->property(kIconKindProperty);
    if (!stored.canConvert<int>()) {
        return;
    }
    button->setIcon(QIcon(render(Kind(stored.toInt()), navIconColor(button), kNavIconSize)));
}

void refreshNavIconsIn(QWidget *root)
{
    if (root == nullptr) {
        return;
    }
    const QList<QAbstractButton *> buttons = root->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (button->property(kIconKindProperty).canConvert<int>()) {
            refreshNavIcon(button);
        }
    }
}

QString arrowImage(Arrow arrow, const QColor &color)
{
    // 文件名带颜色值：同一颜色只落一次盘，切主题自然指向另一个文件。
    // 注意**不能带 `#`**：那既是 QSS `url()` 里的片段分隔符，也会让"颜色只能来自 Theme"
    // 那两条守卫把路径里的 `#aarrggbb` 当成硬编码颜色。
    const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("vcc-icons"));
    QDir().mkpath(dir);
    const QString file = QStringLiteral("arrow-%1-%2.png")
                             .arg(arrow == Arrow::Up ? QStringLiteral("up") : QStringLiteral("down"))
                             .arg(color.rgba(), 8, 16, QLatin1Char('0'));
    const QString path = QDir(dir).filePath(file);
    if (QFile::exists(path)) {
        return QDir::fromNativeSeparators(path);
    }

    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    if (arrow == Arrow::Up) {
        painter.drawPolyline(QPolygonF() << QPointF(5, 13) << QPointF(10, 7) << QPointF(15, 13));
    } else {
        painter.drawPolyline(QPolygonF() << QPointF(5, 7) << QPointF(10, 13) << QPointF(15, 7));
    }
    painter.end();
    if (!pixmap.save(path)) {
        return QString();   // 存不下就宁可没箭头，也不画成黑块
    }
    return QDir::fromNativeSeparators(path);
}

} // namespace Gui::Icons
