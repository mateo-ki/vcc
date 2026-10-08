#pragma once

#include <optional>

#include <QPair>
#include <QString>
#include <QVector>

#include "core/Result.h"
#include "models/Node.h"

class Database;

/// 节点导出（对应 db/export.rs）。导出走的是分库全量加载 + 内存过滤，
/// 和 Rust 一样不在 SQL 里筛质量，所以判定条件必须逐条对齐。
namespace DbExport {

/// `protocol` / `sourceId` 为 nullopt 表示 Rust 的 None（不限）。
Result<QVector<Models::Node>> exportNodes(Database &database,
                                          const std::optional<QString> &protocol,
                                          const std::optional<QString> &sourceId);

/// 带质量筛选的导出。quality_only 的含义：
/// - 此前 TCP 连通过（latency_ok=1）
/// - latency 落在 (0, max_latency_ms]
/// - host 是公网地址且不是已知的网站/CDN 诱饵
///
/// min_download_kbps 是可选的，默认 0 = 不要求下载速度。对加密代理（ss/vmess/vless/trojan）
/// 要求下载速度通常是错的：探测不会说这些协议，而打开网页看起来"很快"。
Result<QVector<Models::Node>> exportNodesFiltered(
    Database &database, const std::optional<QString> &protocol,
    const std::optional<QString> &sourceId, bool qualityOnly,
    const std::optional<qint64> &maxLatencyMs, const std::optional<double> &minDownloadKbps,
    const std::optional<quint32> &limit);

/// "推送可用节点"用的导出：同 `export_nodes_filtered(quality_only=true)`，
/// 但排除标记为不推送的源、以及命中排除关键词的节点。
/// first = 保留节点，second = 被排除数量。
Result<QPair<QVector<Models::Node>, size_t>> exportPushableNodes(
    Database &database, const std::optional<qint64> &maxLatencyMs, const QString &excludeKeywords);

}
