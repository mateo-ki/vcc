#pragma once

#include <optional>

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "models/Node.h"

namespace Models {

struct ProtocolStat {
    QString protocol;
    qint64 count = 0;

    Json toJson() const;
};

struct Stats {
    qint64 sourceCount = 0;
    qint64 enabledSourceCount = 0;
    qint64 nodeCount = 0;
    qint64 testedCount = 0;
    qint64 aliveCount = 0;
    QVector<ProtocolStat> protocolStats;

    Json toJson() const;
};

struct FetchResult {
    QString sourceId;
    QString sourceName;
    QString resolvedUrl;
    bool success = false;
    QString message;
    int nodeCount = 0;

    Json toJson() const;
};

struct LatencyResult {
    QString id;
    QString name;
    QString server;
    int port = 0;
    bool ok = false;
    std::optional<qint64> latencyMs;
    /// 测得时的下载速度 KB/s。
    std::optional<double> downloadKbps;
    QString latencyKernel;
    QString latencyTestKind;
    QString message;

    Json toJson() const;
};

struct SpeedTestSummary {
    int total = 0;
    int ok = 0;
    int fail = 0;
    std::optional<double> avgMs;
    std::optional<double> avgDownloadKbps;
    QVector<LatencyResult> results;

    Json toJson() const;
};

/// 节点列表查询条件。
struct NodeQuery {
    std::optional<QString> protocol;
    std::optional<QString> sourceId;
    std::optional<QString> keyword;
    /// alive | dead | untested | all
    std::optional<QString> latency;
    std::optional<QString> country;
    std::optional<qint64> page;
    std::optional<qint64> pageSize;
    /// updated | latency | name | download
    std::optional<QString> sort;

    static NodeQuery fromJson(const Json &value);
};

/// 导出端点的质量筛选条件。
struct ExportFilter {
    std::optional<QString> protocol;
    std::optional<QString> sourceId;
    /// 只保留 latency_ok=1 且 latency_ms <= 该值（默认 800）。
    std::optional<qint64> maxLatencyMs;
    /// 只保留 download_kbps >= 该值（默认 50）。
    std::optional<double> minDownloadKbps;
    /// true（默认）应用质量筛选；false 导出全部。
    std::optional<bool> qualityOnly;

    static ExportFilter fromJson(const Json &value);

    qint64 effectiveMaxLatencyMs() const { return maxLatencyMs.value_or(800); }
    double effectiveMinDownloadKbps() const { return minDownloadKbps.value_or(50.0); }
    bool appliesQualityFilter() const { return qualityOnly.value_or(true); }
};

/// 订阅源同质化与重合度分析报告。
struct SourceHomogeneityReport {
    QString sourceId;
    QString sourceName;
    int totalNodes = 0;
    int aliveNodes = 0;
    int duplicateNodes = 0;
    int uniqueNodes = 0;
    int uniqueAliveNodes = 0;
    float overlapRatePct = 0.0f;
    bool isRedundant = false;

    Json toJson() const;
};

/// 反向剔除同质化源的执行结果。
struct PruneHomogeneityResult {
    int prunedCount = 0;
    QVector<QString> affectedNames;
    bool deletedRecords = false;

    Json toJson() const;
};

template <typename T>
struct Page {
    qint64 total = 0;
    quint32 pageNumber = 1;
    quint32 pageSize = 50;
    QVector<T> items;

    Json toJson() const
    {
        Json value;
        value["total"] = total;
        value["page"] = pageNumber;
        value["page_size"] = pageSize;
        Json entries = Json::array();
        for (const T &item : items) {
            entries.push_back(item.toJson());
        }
        value["items"] = entries;
        return value;
    }
};

/// 本地 API 的统一信封：data 为 None 时整个键省略（serde 的 skip_serializing_if）。
template <typename T>
struct ApiResponse {
    bool ok = true;
    QString message;
    std::optional<T> data;

    static ApiResponse withData(T payload)
    {
        ApiResponse response;
        response.ok = true;
        response.message = QStringLiteral("ok");
        response.data = std::move(payload);
        return response;
    }

    static ApiResponse<T> failure(const QString &message)
    {
        ApiResponse<T> response;
        response.ok = false;
        response.message = message;
        return response;
    }

    Json toJson() const
    {
        Json value;
        value["ok"] = ok;
        value["message"] = message.toStdString();
        if (data.has_value()) {
            value["data"] = data->toJson();
        }
        return value;
    }
};

/// 无 data 的信封形状。
inline Json emptyApiResponse(bool ok, const QString &message)
{
    Json value;
    value["ok"] = ok;
    value["message"] = message.toStdString();
    return value;
}

}
