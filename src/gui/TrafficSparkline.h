#pragma once

#include <QPair>
#include <QPointF>
#include <QVector>
#include <QWidget>

namespace Gui {

/// 流量环形缓冲的点数：满 150 丢最旧（Rust mod.rs:1208-1211）。
inline constexpr int kTrafficHistoryPoints = 150;

/// 纵轴上限：取这批点的第 92 百分位再放 1.25 倍，最低 64 KB/s。
/// 直接按最大值画的话，一次突发就把之后所有平时的流量压成一条贴底直线
/// （Rust `widgets.rs::chart_scale` 同一条规则）。
double chartScale(const QVector<double> &values);

/// Catmull-Rom 平滑：每段插 4 个点（Rust `widgets.rs::smooth_polyline`）。
/// 少于 3 个点没什么可弯的，原样返回。
QVector<QPointF> smoothPolyline(const QVector<QPointF> &points, int steps);

/// 状态卡第二行的实时上下行曲线，对应 Rust `widgets.rs::render_traffic_sparkline`。
/// 一个点 = 一次轮询，环形保留最近 150 个；上行画虚线先画、下行实线压在它上面
/// （下载通常大一个量级，反过来画上传会被盖住）。
class TrafficSparkline : public QWidget {
    Q_OBJECT

public:
    explicit TrafficSparkline(QWidget *parent = nullptr);

    /// (下行 KB/s, 上行 KB/s)，旧 → 新。
    void setHistory(const QVector<QPair<double, double>> &history);
    void setActive(bool active);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void updateTooltip(int index);

    static constexpr int kHeight = 32;
    QVector<QPair<double, double>> history_;
    bool active_ = false;
    int hoverIndex_ = -1;
};

} // namespace Gui
