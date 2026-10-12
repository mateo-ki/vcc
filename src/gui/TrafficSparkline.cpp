#include "gui/TrafficSparkline.h"

#include "gui/Theme.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace Gui {

double chartScale(const QVector<double> &values)
{
    if (values.isEmpty()) {
        return 64.0;
    }
    QVector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const int index = qMin(sorted.size() * 92 / 100, sorted.size() - 1);
    return qMax(sorted.at(int(index)) * 1.25, 64.0);
}

QVector<QPointF> smoothPolyline(const QVector<QPointF> &points, int steps)
{
    if (points.size() < 3) {
        return points;
    }
    QVector<QPointF> out;
    out.reserve((points.size() - 1) * steps + 1);
    const auto mix = [](double a, double b, double c, double d, double t) {
        const double t2 = t * t;
        const double t3 = t2 * t;
        return 0.5 * ((2.0 * b) + (c - a) * t + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2
                      + (-a + 3.0 * b - 3.0 * c + d) * t3);
    };
    for (int i = 0; i < points.size() - 1; ++i) {
        const QPointF p0 = points.at(qMax(0, i - 1));
        const QPointF p1 = points.at(i);
        const QPointF p2 = points.at(i + 1);
        const QPointF p3 = points.at(qMin(points.size() - 1, i + 2));
        for (int s = 0; s < steps; ++s) {
            const double t = double(s) / double(steps);
            out.push_back(QPointF(mix(p0.x(), p1.x(), p2.x(), p3.x(), t),
                                  mix(p0.y(), p1.y(), p2.y(), p3.y(), t)));
        }
    }
    out.push_back(points.last());
    return out;
}

TrafficSparkline::TrafficSparkline(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(kHeight);
    setMouseTracking(true);
    updateTooltip(-1);
}

void TrafficSparkline::setHistory(const QVector<QPair<double, double>> &history)
{
    history_ = history;
    hoverIndex_ = -1;
    updateTooltip(-1);
    update();
}

void TrafficSparkline::setActive(bool active)
{
    if (active_ == active) {
        return;
    }
    active_ = active;
    updateTooltip(-1);
    update();
}

QSize TrafficSparkline::sizeHint() const
{
    return QSize(240, kHeight);
}

void TrafficSparkline::updateTooltip(int index)
{
    if (!active_ || history_.size() < 2) {
        setToolTip(active_ ? UiText::sparklineWaiting() : UiText::sparklineIdle());
        return;
    }
    if (index < 0 || index >= history_.size()) {
        setToolTip(QString());
        return;
    }
    const int last = history_.size() - 1;
    setToolTip(UiText::sparklineHover()
                   .arg(last - index)
                   .arg(formatSpeed(quint64(history_.at(index).first * 1024.0)))
                   .arg(formatSpeed(quint64(history_.at(index).second * 1024.0))));
}

void TrafficSparkline::paintEvent(QPaintEvent *)
{
    const Gui::Theme::Palette palette = Gui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double baseline = height() - 2.0;
    const double usable = kHeight - 9.0;

    QPen axis(1.0);
    axis.setColor(palette.border);
    painter.setPen(axis);
    painter.drawLine(QPointF(0.0, baseline), QPointF(width(), baseline));

    if (!active_ || history_.size() < 2) {
        return;
    }

    QVector<double> all;
    all.reserve(history_.size() * 2);
    for (const auto &point : history_) {
        all.push_back(point.first);
        all.push_back(point.second);
    }
    const double scale = chartScale(all);
    const int last = history_.size() - 1;
    const double dx = (width() - 6.0) / last;
    const auto at = [&](int index, double value) {
        const double ratio = qBound(0.0, value / scale, 1.0);
        return QPointF(3.0 + index * dx, baseline - ratio * usable);
    };
    const auto curve = [&](int field) {
        QVector<QPointF> raw;
        raw.reserve(history_.size());
        for (int index = 0; index < history_.size(); ++index) {
            raw.push_back(at(index, field == 0 ? history_.at(index).first : history_.at(index).second));
        }
        return smoothPolyline(raw, 4);
    };

    // 上传先画、下载压在它上面（Rust widgets.rs:435 的理由：下载大一个量级会盖住上传）。
    QPen upPen(1.3);
    upPen.setColor(palette.accent);
    upPen.setDashPattern({3.0, 2.5});
    painter.setPen(upPen);
    painter.drawPolyline(curve(1));

    QPen downPen(1.6);
    downPen.setColor(palette.success);
    painter.setPen(downPen);
    painter.drawPolyline(curve(0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.accent);
    painter.drawEllipse(at(last, history_.at(last).second), 1.8, 1.8);
    painter.setBrush(palette.success);
    painter.drawEllipse(at(last, history_.at(last).first), 2.2, 2.2);

    if (hoverIndex_ >= 0 && hoverIndex_ <= last) {
        const double x = at(hoverIndex_, 0.0).x();
        painter.setPen(axis);
        painter.drawLine(QPointF(x, 1.0), QPointF(x, baseline));
    }
}

void TrafficSparkline::mouseMoveEvent(QMouseEvent *event)
{
    if (history_.size() < 2 || !active_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const double dx = (width() - 6.0) / (history_.size() - 1);
    const int index = qBound(0, int((event->position().x() - 3.0) / dx + 0.5), history_.size() - 1);
    if (index != hoverIndex_) {
        hoverIndex_ = index;
        updateTooltip(index);
        update();
    }
}

void TrafficSparkline::leaveEvent(QEvent *event)
{
    hoverIndex_ = -1;
    updateTooltip(-1);
    update();
    QWidget::leaveEvent(event);
}

} // namespace Gui
