#pragma once

#include <optional>

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "models/Node.h"

// 对应 Rust 的 export_clash / export_json（src/api/mod.rs 里那两个处理器纯构造
// 文档的部分）：proxies 来自 nodes_to_clash_proxies，再包一层最小可用的 Clash 档案，
// 这样客户端才会接受整份订阅而不是 proxies 片段。YAML 用 yaml-cpp 输出。
namespace Export {

/// export_clash 里的 json! 文档：port/socks-port/allow-lan/mode/log-level/
/// external-controller/proxies/proxy-groups/rules。
Json clashProxiesDocument(const QVector<Models::Node> &nodes);

/// 上面那份文档的 YAML 文本（对应 serde_yaml::to_string(&doc)）。
QString clashProxiesYaml(const QVector<Models::Node> &nodes);

/// 任意 JSON → YAML 文本，对应 Rust 的 serde_yaml::to_string(&value)。
/// 键序按 nlohmann 的 std::map（字节序）输出，和 serde_json 的 BTreeMap 一致。
QString jsonToYaml(const Json &value);

/// export_json 的响应体（不含 HTTP 信封）。maxLatencyMs/minDownloadKbps 传的是
/// Rust quality_params 里已经取过默认值（500 / 0.0）之后的结果，limit 为 nullopt
/// 时写 JSON null。
Json jsonExportDocument(const QVector<Models::Node> &nodes, bool qualityOnly, qint64 maxLatencyMs,
                        double minDownloadKbps, std::optional<quint32> limit);

}
