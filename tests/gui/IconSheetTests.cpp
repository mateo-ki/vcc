// 一次性取证工具：给侧栏五枚图标各出 3 个候选画法，渲成一张对照表 PNG。
// 跑法：VCC_ICON_SHEET=<输出png> 带着这个环境变量跑 vccGuiTests，只跑 IconSheet 那条。
// 标签一律 ASCII：这台机器的默认字体会把中文渲成方块，出图工具不需要中文。
#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QString>

#include <algorithm>
#include <cmath>

#include "gui/Icons.h"
#include "gui/Theme.h"

namespace {

using Gui::Icons::Kind;

constexpr int kIconCount = 6;
constexpr int kVariantCount = 3;

QColor withAlpha(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

void strokePen(QPainter *p, const QColor &color, qreal width)
{
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
}

// 每枚图标三个候选；variant 0 就是现在包里的画法（Rust 的几何）。
void drawCandidate(QPainter *p, const QRectF &rect, Kind kind, int variant, const QColor &ink)
{
    p->setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = rect.center();
    const qreal s = std::min(rect.width(), rect.height());
    const qreal w = s * 0.085;   // 一档固定笔画比例，不再随尺寸在 1.2-2.4 之间跳

    if (variant == 0) {
        Gui::Icons::paint(p, rect, kind, ink);
        return;
    }

    switch (kind) {
    case Kind::Dashboard: {
        const qreal pad = s * 0.06;
        const qreal half = s * 0.36;
        const QRectF tl(c.x() - half - pad, c.y() - half - pad, half, half);
        const QRectF tr(c.x() + pad, c.y() - half - pad, half, half);
        const QRectF bl(c.x() - half - pad, c.y() + pad, half, half);
        const QRectF br(c.x() + pad, c.y() + pad, half, half);
        const qreal radius = s * 0.08;
        if (variant == 1) {   // 四格同权：全部细描边
            strokePen(p, ink, w);
            p->drawRoundedRect(tl, radius, radius);
            p->drawRoundedRect(tr, radius, radius);
            p->drawRoundedRect(bl, radius, radius);
            p->drawRoundedRect(br, radius, radius);
            return;
        }
        p->setPen(Qt::NoPen);   // 2x2 实心，对角压淡
        p->setBrush(ink);
        p->drawRoundedRect(tl, radius, radius);
        p->drawRoundedRect(br, radius, radius);
        p->setBrush(withAlpha(ink, 105));
        p->drawRoundedRect(tr, radius, radius);
        p->drawRoundedRect(bl, radius, radius);
        return;
    }
    case Kind::Lightning: {
        QPolygonF bolt;
        if (variant == 2) {   // 更立、横摆更小的闪电
            bolt << QPointF(c.x() + s * 0.10, c.y() - s * 0.44)
                 << QPointF(c.x() - s * 0.22, c.y() + s * 0.06)
                 << QPointF(c.x() - s * 0.02, c.y() + s * 0.06)
                 << QPointF(c.x() - s * 0.10, c.y() + s * 0.44)
                 << QPointF(c.x() + s * 0.24, c.y() - s * 0.06)
                 << QPointF(c.x() + s * 0.04, c.y() - s * 0.06);
        } else {
            bolt << QPointF(c.x() + s * 0.06, c.y() - s * 0.44)
                 << QPointF(c.x() - s * 0.30, c.y() + s * 0.04)
                 << QPointF(c.x() - s * 0.04, c.y() + s * 0.04)
                 << QPointF(c.x() - s * 0.12, c.y() + s * 0.44)
                 << QPointF(c.x() + s * 0.32, c.y() - s * 0.04)
                 << QPointF(c.x() + s * 0.04, c.y() - s * 0.04);
        }
        if (variant == 1) {   // 描边闪电：和其他四枚同一视觉重量
            strokePen(p, ink, w);
            p->drawPolygon(bolt);
            return;
        }
        p->setPen(Qt::NoPen);
        p->setBrush(ink);
        p->drawPolygon(bolt);
        return;
    }
    case Kind::Subscription: {
        const qreal bx1 = c.x() - s * 0.38, bx2 = c.x() + s * 0.38;
        if (variant == 1) {   // 闭合的收件箱 + 盒内向下箭头
            strokePen(p, ink, w);
            p->drawRoundedRect(QRectF(bx1, c.y() - s * 0.02, bx2 - bx1, s * 0.38), s * 0.10,
                               s * 0.10);
            p->drawLine(QPointF(c.x(), c.y() - s * 0.40), QPointF(c.x(), c.y() + s * 0.14));
            p->drawLine(QPointF(c.x() - s * 0.16, c.y() - s * 0.02),
                        QPointF(c.x(), c.y() + s * 0.14));
            p->drawLine(QPointF(c.x() + s * 0.16, c.y() - s * 0.02),
                        QPointF(c.x(), c.y() + s * 0.14));
            return;
        }
        strokePen(p, ink, w);   // 三条订阅链接 + 一个下载箭头
        p->drawLine(QPointF(bx1, c.y() - s * 0.30), QPointF(bx2, c.y() - s * 0.30));
        p->drawLine(QPointF(bx1, c.y()), QPointF(c.x() + s * 0.05, c.y()));
        p->drawLine(QPointF(bx1, c.y() + s * 0.30), QPointF(bx2, c.y() + s * 0.30));
        p->drawLine(QPointF(c.x() + s * 0.30, c.y() - s * 0.14),
                    QPointF(c.x() + s * 0.30, c.y() + s * 0.18));
        p->drawLine(QPointF(c.x() + s * 0.16, c.y() + s * 0.04),
                    QPointF(c.x() + s * 0.30, c.y() + s * 0.18));
        p->drawLine(QPointF(c.x() + s * 0.44, c.y() + s * 0.04),
                    QPointF(c.x() + s * 0.30, c.y() + s * 0.18));
        return;
    }
    case Kind::Gear: {
        if (variant == 1) {   // 8 根短齿 + 更粗的圈：齿和圈分得开
            const qreal rRing = s * 0.22, rTooth = s * 0.38;
            strokePen(p, ink, s * 0.10);
            p->drawEllipse(c, rRing, rRing);
            for (int i = 0; i < 8; ++i) {
                const qreal angle = i * 3.14159265358979 / 4.0;
                p->drawLine(QPointF(c.x() + (rRing + s * 0.05) * std::cos(angle),
                                    c.y() + (rRing + s * 0.05) * std::sin(angle)),
                            QPointF(c.x() + rTooth * std::cos(angle),
                                    c.y() + rTooth * std::sin(angle)));
            }
            return;
        }
        strokePen(p, ink, w);   // 换成三根滑杆：设置页的通用画法
        for (int i = 0; i < 3; ++i) {
            const qreal y = c.y() + (i - 1) * s * 0.28;
            p->drawLine(QPointF(c.x() - s * 0.36, y), QPointF(c.x() + s * 0.36, y));
            const qreal knobX = c.x() + (i == 1 ? -s * 0.18 : s * 0.16);
            p->drawEllipse(QPointF(knobX, y), s * 0.09, s * 0.09);
        }
        return;
    }
    case Kind::Plug: {
        if (variant == 1) {   // 去掉下面那根线，只留插头本体 + 两脚
            const QRectF body(c.x() - s * 0.24, c.y() - s * 0.06, s * 0.48, s * 0.36);
            strokePen(p, ink, w);
            p->drawRoundedRect(body, s * 0.10, s * 0.10);
            p->drawLine(QPointF(c.x() - s * 0.12, body.top()),
                        QPointF(c.x() - s * 0.12, body.top() - s * 0.26));
            p->drawLine(QPointF(c.x() + s * 0.12, body.top()),
                        QPointF(c.x() + s * 0.12, body.top() - s * 0.26));
            p->drawLine(QPointF(c.x(), body.bottom()), QPointF(c.x(), c.y() + s * 0.44));
            return;
        }
        strokePen(p, ink, s * 0.11);   // 换成"链接"：API 接入的通用符号
        p->drawArc(QRectF(c.x() - s * 0.46, c.y() - s * 0.20, s * 0.54, s * 0.40), 25 * 16,
                   290 * 16);
        p->drawArc(QRectF(c.x() - s * 0.08, c.y() - s * 0.20, s * 0.54, s * 0.40), 205 * 16,
                   290 * 16);
        return;
    }
    case Kind::Info:
        // 第六枚是我们自己加的（Rust 的 icons.rs 没有"关于"这枚），只有一版，
        // 所以三个格都画同一张：对照表在这里的作用是确认它和那一列一样重。
        Gui::Icons::paint(p, rect, kind, ink);
        return;
    }
}

TEST(IconSheet, writesAComparisonPng)
{
    const QString out = qEnvironmentVariable("VCC_ICON_SHEET");
    if (out.isEmpty()) {
        GTEST_SKIP() << "no VCC_ICON_SHEET, nothing to render";
    }
    const Gui::Theme::Palette pal = Gui::Theme::current();
    const char *titles[kIconCount] = {"1 Dashboard", "2 Lightning", "3 Subscription", "4 Gear",
                                      "5 Plug", "6 Info (ours, for About)"};

    const int cellW = 240, cellH = 116, headH = 40;
    QImage image(cellW * kVariantCount, headH + cellH * kIconCount + 26, QImage::Format_ARGB32);
    image.fill(pal.sidebar);
    QPainter p(&image);
    QFont font = p.font();
    font.setPixelSize(12);
    p.setFont(font);

    for (int variant = 0; variant < kVariantCount; ++variant) {
        p.setPen(pal.textPrimary);
        p.drawText(QRect(variant * cellW, 8, cellW, 20), Qt::AlignHCenter,
                   QString::fromUtf8(variant == 0 ? "A = current (Rust geometry)"
                                 : variant == 1 ? "B = unified thin strokes"
                                                : "C = clearer / new metaphor"));
        p.setPen(pal.textSecondary);
        p.drawText(QRect(variant * cellW, 24, cellW, 14), Qt::AlignHCenter,
                   "big = 4x    small = 17px real size");
    }

    for (int row = 0; row < kIconCount; ++row) {
        const int y = headH + row * cellH;
        const bool checked = row % 2 == 0;
        const QColor bg = checked ? pal.accentBg : pal.sidebar;
        const QColor ink = checked ? pal.accent : pal.sidebarMuted;
        for (int variant = 0; variant < kVariantCount; ++variant) {
            QRectF cell(variant * cellW, y, cellW, cellH);
            p.fillRect(cell, bg);
            p.setPen(pal.border);
            p.setBrush(Qt::NoBrush);
            p.drawRect(cell);

            QRectF at17(cell.right() - 152, cell.center().y() - Gui::Icons::kNavIconSize / 2.0, Gui::Icons::kNavIconSize, Gui::Icons::kNavIconSize);
            // A 列必须走**应用真正用的那条路**（render() → QIcon），否则 DPR 一错，
            // 表里看着完整、界面上却是残缺的（2026-10-10 就这么骗过一次）。
            if (variant == 0) {
                const QRect big(18 + variant * cellW, 20 + y, 76, 76);
                p.drawPixmap(big, Gui::Icons::render(Kind(row), ink, big.width(), 2.0));
                const QRect real(int(at17.left()), int(at17.top()), Gui::Icons::kNavIconSize, Gui::Icons::kNavIconSize);
                p.drawPixmap(real, Gui::Icons::render(Kind(row), ink, Gui::Icons::kNavIconSize, 2.0));
            } else {
                drawCandidate(&p, QRectF(cell.left() + 18, cell.top() + 20, 76, 76), Kind(row),
                              variant, ink);
                drawCandidate(&p, at17, Kind(row), variant, ink);
            }
            p.setPen(checked ? pal.textPrimary : pal.textSecondary);
            p.drawText(QRectF(at17.right() + 8, cell.top(), 140, cellH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromUtf8("%1  %2").arg(QChar('A' + variant)).arg(titles[row]));
        }
    }
    p.setPen(pal.textSecondary);
    p.drawText(QRect(12, image.height() - 22, image.width() - 24, 16), Qt::AlignLeft,
               "rows 1/3/5 = selected (accent on accentBg)    rows 2/4 = idle (sidebarMuted)");
    p.end();

    if (!image.save(out)) {
        ADD_FAILURE() << "write failed: " << out.toStdString();
        return;
    }
    EXPECT_GT(image.width(), 0);
}

} // namespace
