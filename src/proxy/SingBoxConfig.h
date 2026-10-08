#pragma once

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "core/Result.h"
#include "models/Node.h"
#include "proxy/MihomoConfig.h"

namespace Proxy {

/// 对应 Rust src/proxy/sing_box.rs：sing-box 配置生成器（1:1 移植）。
///
/// 与 mihomo 的差别：sing-box 没有"规则行"文本，分流规则是 route.rules 里的 JSON 对象；
/// GEOSITE/GEOIP 分类要靠 remote rule-set（下面四个 .srs 地址），而 sing-box 的官方规则集
/// 里没有 gfw/ads 之外的分类，所以未收录的分类必须报错，不能静默丢掉一条规则——
/// 丢规则等于"该走代理的流量直连出去"。

/// Rust: GEOSITE_CN_URL / GEOSITE_ADS_URL / GEOSITE_GFW_URL / GEOIP_CN_URL。
extern const QString kSingBoxGeoSiteCnUrl;
extern const QString kSingBoxGeoSiteAdsUrl;
extern const QString kSingBoxGeoSiteGfwUrl;
extern const QString kSingBoxGeoIpCnUrl;
/// remote rule_set 的 tag（与 route.rules[].rule_set 引用同名）。
extern const QString kSingBoxGeoSiteCnTag;
extern const QString kSingBoxGeoSiteAdsTag;
extern const QString kSingBoxGeoSiteGfwTag;
extern const QString kSingBoxGeoIpCnTag;

/// 单个 sing-box 进程能同时挂多少个带 tag 的 mixed 入站。
inline constexpr qsizetype kSingBoxBatchSize = 64;

/// 一个进程管多少批：`node_count.div_ceil(64)`，0 个节点是 0 批。
qsizetype singBoxBatchCount(qsizetype nodeCount);

/// 对应 build_sing_box_config：单节点完整配置。
Result<Json> buildSingBoxConfig(const Models::Node &node, MihomoConfigOptions options);

/// 批量测速里"一个进程 + N 个带 tag 入站"的那部分纯逻辑（对应 probe_batch_with_sing_box
/// 里 spawn 之前的全部代码）：端口由调用方预分配（Rust 侧是 TcpListener 占位后 drop）。
struct SingBoxBatchProbeEntry {
    /// 成功建配的节点在入参 nodes 里的下标。
    qsizetype nodeIndex = -1;
    /// 该节点的 HTTP 代理端口：http://127.0.0.1:<port>。
    quint16 port = 0;
    /// 入站 tag（probe-in-<i>）与出站/endpoint tag（probe-out-<i>）；测速失败时日志里要用。
    QString inboundTag;
    QString outboundTag;
};

struct SingBoxBatchProbePlan {
    /// 合并后的 config.json 文档；仅 hasInbounds 为真时有意义。
    Json config;
    /// 与入参 nodes 等长：null = 该节点建配成功，非 null = 已经带上 Rust
    /// `format!("Sing-box 配置不支持: {error}")` 的完整文案，可直接作 ProxyProbeResult.message。
    QVector<QString> failures;
    QVector<SingBoxBatchProbeEntry> entries;
    /// false 时调用方只需回报 failures（Rust 的 `if inbounds.is_empty() { return Ok(immediate) }`）。
    bool hasInbounds = false;
    /// 就绪探测端口：Rust 取 inbounds[0].listen_port，缺省退回 ports[0]。
    quint16 readyPort = 0;
};

/// ports 必须与 nodes 一一对应（长度不符是调用方的 bug，直接返回错误而不是猜端口）。
Result<SingBoxBatchProbePlan> buildSingBoxBatchProbePlan(const QVector<Models::Node> &nodes,
                                                         const QVector<quint16> &ports);

/// 对应 last_sing_box_log_line：内核日志最后一行非空文本，全空时给 Rust 的兜底文案。
QString lastSingBoxLogLine(const QString &text);

}
