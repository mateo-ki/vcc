#pragma once

#include <QString>
#include <QVector>

#include "core/Result.h"
#include "models/Node.h"

// parser/clash.rs：Clash / Clash Meta YAML 订阅 → 节点列表。
namespace Parser {

/// 对应 Rust `parse_clash_yaml`，返回 anyhow::Result<Vec<Node>> 的等价物。
Result<QVector<Models::Node>> parseClashYaml(const QString& content, const QString& sourceId);

} // namespace Parser
