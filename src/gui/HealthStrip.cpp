#include "gui/HealthStrip.h"

#include "core/TimeUtil.h"
#include "gui/Theme.h"
#include "gui/UiText.h"

#include <QPainter>

namespace Gui {

QVector<qint8> healthHourBuckets(const QVector<DbTelemetry::HealthRow> &rows, qint64 nowNs)
{
    QVector<qint8> buckets(24, 0);
    for (const DbTelemetry::HealthRow &row : rows) {
        const Result<qint64> at = TimeUtil::parseRfc3339(row.at);
        if (at.isErr()) {
            continue;
        }
        const qint64 hoursAgo = (nowNs - at.value()) / (60LL * 60LL * 1000000000LL);
        if (hoursAgo < 0 || hoursAgo >= buckets.size()) {
            continue;
        }
        const qint8 flag = row.ok == 0 ? qint8(-1) : qint8(1);
        if (buckets.at(int(hoursAgo)) != -1) {
            buckets[int(hoursAgo)] = flag;
        }
    }
    return buckets;
}

HealthStrip::HealthStrip(QWidget *parent) : QWidget(parent)
{
    // 高度钉死（Rust 的 CELL_H），宽度交给布局：sizeHint 是每格 10px，minimumSizeHint
    // 是每格 6px，paintEvent 按实到宽度在两者之间收 —— 和 Rust 的 clamp 同一条规则。
    setFixedHeight(kCellHeight);
    setToolTip(UiText::healthStripHover().arg(0).arg(0).arg(kCells));
}

void HealthStrip::setBuckets(const QVector<qint8> &buckets)
{
    buckets_ = buckets;
    int passed = 0;
    int failed = 0;
    for (const qint8 state : buckets_) {
        if (state == 1) ++passed;
        else if (state == -1) ++failed;
    }
    setToolTip(UiText::healthStripHover().arg(passed).arg(failed).arg(kCells - passed - failed));
    update();
}

QSize HealthStrip::sizeHint() const
{
    return QSize(kCells * (10 + kGap), kCellHeight);
}

QSize HealthStrip::minimumSizeHint() const
{
    return QSize(kCells * (6 + kGap), kCellHeight);
}

void HealthStrip::paintEvent(QPaintEvent *)
{
    // Rust: cell_w = ((available - 16) / 24 - GAP).clamp(6, 10)，然后从右往左摆。
    const int cellWidth = qBound(6, int((width() - 16) / double(kCells) - kGap), 10);
    const Gui::Theme::Palette palette = Gui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int index = 0; index < kCells; ++index) {
        const int x = width() - (index + 1) * (cellWidth + kGap);
        const QRectF cell(x, 0, cellWidth, kCellHeight);
        const qint8 state = index < buckets_.size() ? buckets_.at(index) : qint8(0);
        QColor fill = palette.border;
        if (state == -1) {
            fill = palette.textPrimary;
        } else if (state == 1) {
            fill = palette.success;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(cell, 1.0, 1.0);
    }
}

} // namespace Gui
