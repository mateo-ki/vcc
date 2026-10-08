#include "models/Types.h"

namespace Models {

using JsonUtil::nullableText;
using JsonUtil::optBool;
using JsonUtil::optDouble;
using JsonUtil::optInt;
using JsonUtil::optText;
using JsonUtil::optTextValue;

Json ProtocolStat::toJson() const
{
    Json value;
    value["protocol"] = protocol.toStdString();
    value["count"] = count;
    return value;
}

Json Stats::toJson() const
{
    Json value;
    value["source_count"] = sourceCount;
    value["enabled_source_count"] = enabledSourceCount;
    value["node_count"] = nodeCount;
    value["tested_count"] = testedCount;
    value["alive_count"] = aliveCount;
    Json entries = Json::array();
    for (const ProtocolStat &stat : protocolStats) {
        entries.push_back(stat.toJson());
    }
    value["protocol_stats"] = entries;
    return value;
}

Json FetchResult::toJson() const
{
    Json value;
    value["source_id"] = sourceId.toStdString();
    value["source_name"] = sourceName.toStdString();
    value["resolved_url"] = resolvedUrl.toStdString();
    value["success"] = success;
    value["message"] = message.toStdString();
    value["node_count"] = nodeCount;
    return value;
}

Json LatencyResult::toJson() const
{
    Json value;
    value["id"] = id.toStdString();
    value["name"] = name.toStdString();
    value["server"] = server.toStdString();
    value["port"] = port;
    value["ok"] = ok;
    value["latency_ms"] = latencyMs.has_value() ? Json(*latencyMs) : Json();
    value["download_kbps"] = downloadKbps.has_value() ? Json(*downloadKbps) : Json();
    value["latency_kernel"] = optText(latencyKernel);
    value["latency_test_kind"] = optText(latencyTestKind);
    value["message"] = message.toStdString();
    return value;
}

Json SpeedTestSummary::toJson() const
{
    Json value;
    value["total"] = total;
    value["ok"] = ok;
    value["fail"] = fail;
    value["avg_ms"] = avgMs.has_value() ? Json(*avgMs) : Json();
    value["avg_download_kbps"] = avgDownloadKbps.has_value() ? Json(*avgDownloadKbps) : Json();
    Json entries = Json::array();
    for (const LatencyResult &result : results) {
        entries.push_back(result.toJson());
    }
    value["results"] = entries;
    return value;
}

NodeQuery NodeQuery::fromJson(const Json &value)
{
    NodeQuery query;
    query.protocol = optTextValue(value, "protocol");
    query.sourceId = optTextValue(value, "source_id");
    query.keyword = optTextValue(value, "keyword");
    query.latency = optTextValue(value, "latency");
    query.country = optTextValue(value, "country");
    query.page = optInt(value, "page");
    query.pageSize = optInt(value, "page_size");
    query.sort = optTextValue(value, "sort");
    // 空串在 Rust 侧也是 None：`?protocol=` 这种查询参数不该把结果筛空。
    if (query.protocol && query.protocol->isEmpty()) {
        query.protocol = std::nullopt;
    }
    if (query.sourceId && query.sourceId->isEmpty()) {
        query.sourceId = std::nullopt;
    }
    if (query.keyword && query.keyword->isEmpty()) {
        query.keyword = std::nullopt;
    }
    if (query.latency && query.latency->isEmpty()) {
        query.latency = std::nullopt;
    }
    if (query.country && query.country->isEmpty()) {
        query.country = std::nullopt;
    }
    if (query.sort && query.sort->isEmpty()) {
        query.sort = std::nullopt;
    }
    return query;
}

ExportFilter ExportFilter::fromJson(const Json &value)
{
    ExportFilter filter;
    filter.protocol = optTextValue(value, "protocol");
    filter.sourceId = optTextValue(value, "source_id");
    filter.maxLatencyMs = optInt(value, "max_latency_ms");
    filter.minDownloadKbps = optDouble(value, "min_download_kbps");
    filter.qualityOnly = optBool(value, "quality_only");
    if (filter.protocol && filter.protocol->isEmpty()) {
        filter.protocol = std::nullopt;
    }
    if (filter.sourceId && filter.sourceId->isEmpty()) {
        filter.sourceId = std::nullopt;
    }
    return filter;
}

Json SourceHomogeneityReport::toJson() const
{
    Json value;
    value["source_id"] = sourceId.toStdString();
    value["source_name"] = sourceName.toStdString();
    value["total_nodes"] = totalNodes;
    value["alive_nodes"] = aliveNodes;
    value["duplicate_nodes"] = duplicateNodes;
    value["unique_nodes"] = uniqueNodes;
    value["unique_alive_nodes"] = uniqueAliveNodes;
    value["overlap_rate_pct"] = double(overlapRatePct);
    value["is_redundant"] = isRedundant;
    return value;
}

Json PruneHomogeneityResult::toJson() const
{
    Json value;
    value["pruned_count"] = prunedCount;
    Json names = Json::array();
    for (const QString &name : affectedNames) {
        names.push_back(name.toStdString());
    }
    value["affected_names"] = names;
    value["deleted_records"] = deletedRecords;
    return value;
}

}
