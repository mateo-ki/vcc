#pragma once

#include <QVector>
#include <QWidget>

#include "db/DatabaseTelemetry.h"

namespace Gui {

/// 24 格小时桶，对应 Rust `widgets.rs::health_hour_buckets`：+1 = 该小时测通过，
/// -1 = 该小时有过不通，0 = 这一小时没实测。同一格里"不通"一旦落下就不被后来的
/// "通过"洗掉 —— 健康条要说的是这一小时里最坏的那一次。
/// nowNs 是 UTC 纳秒（TimeUtil::parseRfc3339 也返回纳秒，单位必须一致）；
/// 注入是为了测得动。
QVector<qint8> healthHourBuckets(const QVector<DbTelemetry::HealthRow> &rows, qint64 nowNs);

/// 状态卡第一行末尾那条健康条：一格一小时，最右是最近一小时。
/// 对应 Rust `widgets.rs::render_health_strip`（格子高 18、间隔 2，宽度按这一行还剩
/// 多少在 6..10 之间收 —— 宁可挤小也不让整条折到第二行去顶高卡片）。
class HealthStrip : public QWidget {
public:
    explicit HealthStrip(QWidget *parent = nullptr);

    void setBuckets(const QVector<qint8> &buckets);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int kCells = 24;
    static constexpr int kGap = 2;
    static constexpr int kCellHeight = 18;
    QVector<qint8> buckets_;
};

} // namespace Gui
