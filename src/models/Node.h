#pragma once

#include <optional>

#include <QString>

#include "core/JsonUtil.h"
#include "core/TimeUtil.h"

namespace Models {

/// 通过 Google generate_204 真实代理请求验证过的节点。
inline constexpr const char *kGoogleProxyTestKind = "google_proxy";

/// 一个解析出来的代理节点。
struct Node {
    QString id;
    QString sourceId;
    QString protocol;
    QString name;
    QString server;
    int port = 0;
    /// 原始分享链接或配置片段。
    QString raw;
    /// 面向 API 消费者的归一化 JSON 配置。
    Json config = Json::object();
    QString country;
    /// TCP 连接延迟（ms）；无值 = 未测速。
    std::optional<qint64> latencyMs;
    /// true=通, false=超时/失败, 无值=未测速。
    std::optional<bool> latencyOk;
    /// 可测得时的下载吞吐 KB/s。
    std::optional<double> downloadKbps;
    QString latencyKernel;
    QString latencyTestKind;
    TimeUtil::OptionalUtc latencyTestedAt;
    TimeUtil::OptionalUtc createdAt;
    TimeUtil::OptionalUtc updatedAt;

    Json toJson() const;
    static Node fromJson(const Json &value);
};

/// 列表视图用的轻量节点（不含 raw/config 大字段）。
struct NodeSummary {
    QString id;
    QString sourceId;
    QString protocol;
    QString name;
    QString server;
    int port = 0;
    QString country;
    std::optional<qint64> latencyMs;
    std::optional<bool> latencyOk;
    std::optional<double> downloadKbps;
    QString latencyKernel;
    QString latencyTestKind;
    TimeUtil::OptionalUtc latencyTestedAt;
    TimeUtil::OptionalUtc updatedAt;

    Json toJson() const;
};

/// 订阅 Tab 上的 "可用 N / 共 M"。
struct NodeGroupSummary {
    QString sourceId;
    QString sourceName;
    qint64 total = 0;
    qint64 alive = 0;

    Json toJson() const;
};

}
