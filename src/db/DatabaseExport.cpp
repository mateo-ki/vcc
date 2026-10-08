#include "db/DatabaseExport.h"

#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

#include "core/HostUtil.h"
#include "db/Database.h"
#include "db/TextOrder.h"
#include "models/Settings.h"
#include "models/Source.h"

namespace {

/// Rust 的 `sort_by_key(|node| (latency_ms.unwrap_or(i64::MAX), name.clone()))`：
/// 未测速排最后，同延迟按名字字节序（码点序）；`sort_by_key` 是稳定排序，所以用
/// stable_sort，键完全相同的两个节点才保持原有顺序。
bool exportLess(const Models::Node &left, const Models::Node &right)
{
    const qint64 leftLatency
        = left.latencyMs.has_value() ? *left.latencyMs : std::numeric_limits<qint64>::max();
    const qint64 rightLatency
        = right.latencyMs.has_value() ? *right.latencyMs : std::numeric_limits<qint64>::max();
    if (leftLatency != rightLatency) {
        return leftLatency < rightLatency;
    }
    return TextOrder::less(left.name, right.name);
}

/// 质量导出的准入条件，逐条照抄 Rust 的 retain 闭包（`latency_ms > 1` 是严格大于）。
bool isQualityExportable(const Models::Node &node, qint64 maxLatency, double minDownload)
{
    return node.latencyOk.has_value() && *node.latencyOk
        && !node.latencyTestKind.isNull()
        && node.latencyTestKind == QLatin1String(Models::kGoogleProxyTestKind)
        && node.latencyMs.has_value() && *node.latencyMs > 1 && *node.latencyMs <= maxLatency
        && (minDownload <= 0.0 || (node.downloadKbps.has_value() && *node.downloadKbps >= minDownload))
        && HostUtil::isQualityExportHost(node.server);
}

}

namespace DbExport {

Result<QVector<Models::Node>> exportNodes(Database &database, const std::optional<QString> &protocol,
                                          const std::optional<QString> &sourceId)
{
    return exportNodesFiltered(database, protocol, sourceId, false, std::nullopt, std::nullopt,
                               std::nullopt);
}

Result<QVector<Models::Node>> exportNodesFiltered(
    Database &database, const std::optional<QString> &protocol,
    const std::optional<QString> &sourceId, bool qualityOnly,
    const std::optional<qint64> &maxLatencyMs, const std::optional<double> &minDownloadKbps,
    const std::optional<quint32> &limit)
{
    // 和 Rust 一样：先按 source_id 全量加载分库节点，再在内存里过 protocol（关键字/延迟/
    // 国家三项这里恒为 None）。
    Result<QVector<Models::Node>> loaded
        = database.loadNodes(sourceId.has_value() ? *sourceId : QString());
    if (loaded.isErr()) {
        return Result<QVector<Models::Node>>::withError(loaded.error());
    }
    QVector<Models::Node> nodes = Database::filterNodes(loaded.takeValue(), protocol, std::nullopt,
                                                        std::nullopt, std::nullopt);
    if (qualityOnly) {
        const qint64 maxLatency = maxLatencyMs.value_or(2000);
        const double minDownload = minDownloadKbps.value_or(0.0);
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                   [&](const Models::Node &node) {
                                       return !isQualityExportable(node, maxLatency, minDownload);
                                   }),
                    nodes.end());
    }
    std::stable_sort(nodes.begin(), nodes.end(), exportLess);
    if (limit.has_value()) {
        // truncate(limit.max(1))：limit=0 也当 1 用。Qt6 的 QList 没有 truncate，
        // resize 会在变长时补默认元素，所以只在超出上限时收缩。
        const qsizetype capped = qsizetype(std::max<quint32>(*limit, 1));
        if (nodes.size() > capped) {
            nodes.resize(capped);
        }
    }
    return Result<QVector<Models::Node>>::withValue(nodes);
}

Result<QPair<QVector<Models::Node>, size_t>> exportPushableNodes(
    Database &database, const std::optional<qint64> &maxLatencyMs, const QString &excludeKeywords)
{
    Result<QVector<Models::Source>> sources = database.listSources();
    if (sources.isErr()) {
        return Result<QPair<QVector<Models::Node>, size_t>>::withError(sources.error());
    }
    QSet<QString> excluded;
    for (const Models::Source &source : sources.value()) {
        if (source.pushDisabled) {
            excluded.insert(source.id);
        }
    }

    Result<QVector<Models::Node>> nodes
        = exportNodesFiltered(database, std::nullopt, std::nullopt, true, maxLatencyMs, std::nullopt,
                              std::nullopt);
    if (nodes.isErr()) {
        return Result<QPair<QVector<Models::Node>, size_t>>::withError(nodes.error());
    }
    QVector<Models::Node> candidates = nodes.takeValue();
    if (excluded.isEmpty() && excludeKeywords.trimmed().isEmpty()) {
        return Result<QPair<QVector<Models::Node>, size_t>>::withValue(
            {std::move(candidates), size_t(0)});
    }

    QVector<Models::Node> kept;
    kept.reserve(candidates.size());
    size_t skipped = 0;
    for (Models::Node &node : candidates) {
        const bool fromDisabledSource
            = !node.sourceId.isNull() && excluded.contains(node.sourceId);
        if (fromDisabledSource || Models::nameExcluded(node.name, excludeKeywords)) {
            skipped += 1;
        } else {
            kept.append(std::move(node));
        }
    }
    return Result<QPair<QVector<Models::Node>, size_t>>::withValue({std::move(kept), skipped});
}

}
