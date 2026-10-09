#pragma once

#include <QString>

#include "models/Node.h"

namespace Gui {

/// 节点卡片右下角的延迟文案，对应 Rust `widgets.rs::latency_text`。
/// role 是 Theme 里已有的 QSS objectName（空 = 用正文默认色），不新造颜色。
struct LatencyDisplay {
    QString text;
    QString role;
};

LatencyDisplay latencyText(const Models::NodeSummary &node);

/// 延迟分级只保留"快 = 绿"，其余是更深的墨：慢不慢看数字，不靠红黄。
QString latencyRole(qint64 ms);

/// 测速内核在文案里的显示名（Rust 那段 match）。
QString latencyKernelLabel(const QString &kernel);

}
