#pragma once

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "core/Result.h"
#include "models/Node.h"
#include "proxy/MihomoConfig.h"

namespace Proxy {

/// 对应 Rust src/proxy/xray.rs：Xray 配置生成器（1:1 移植）。
///
/// Xray 的表达能力和 mihomo 差得最远，所以这里的错误路径是规约的一部分：
/// - TUN 模式直接报错（Rust: `Xray 暂不支持 TUN 模式`），不做任何降级；
/// - PROCESS-NAME 分流规则拒绝：Xray 的 field 规则里没有进程名字段，
///   静默丢掉就等于那条规则从未存在过；
/// - 分类（GEOSITE/GEOIP）交给 Xray 自带的 geosite.dat/geoip.dat，格式是
///   "geosite:cn" / "geoip:cn"，所以不需要远程 rule_set。

/// 单个 Xray 进程能同时挂多少个带 tag 的入站。
inline constexpr qsizetype kXrayBatchSize = 64;

/// 一个进程管多少批：`node_count.div_ceil(64)`，0 个节点是 0 批。
qsizetype xrayBatchCount(qsizetype nodeCount);

/// 对应 build_xray_config：单节点完整配置。
Result<Json> buildXrayConfig(const Models::Node &node, MihomoConfigOptions options);

/// 批量测速里"一个进程 + N 个带 tag 入站"的纯逻辑（对应 probe_batch 里 spawn 之前的部分）：
/// 端口由调用方预分配（Rust 侧是 TcpListener 占位后 drop）。
struct XrayBatchProbeEntry {
    /// 成功建配的节点在入参 nodes 里的下标。
    qsizetype nodeIndex = -1;
    /// 该节点的 HTTP 代理端口：http://127.0.0.1:<port>。
    quint16 port = 0;
    /// 入站 tag（probe-in-<i>）与出站 tag（probe-out-<i>）。
    QString inboundTag;
    QString outboundTag;
};

struct XrayBatchProbePlan {
    /// 合并后的 config.json 文档；仅 hasInbounds 为真时有意义。
    Json config;
    /// 与入参 nodes 等长：null = 建配成功，非 null = 已带 Rust
    /// `format!("Xray 配置不支持: {error}")` 的完整文案，可直接作 ProxyProbeResult.message。
    QVector<QString> failures;
    QVector<XrayBatchProbeEntry> entries;
    /// false 时调用方只需回报 failures（Rust 的 `if inbounds.is_empty() { return Ok(immediate) }`）。
    bool hasInbounds = false;
    /// 就绪探测端口：Rust 取 inbounds[0].port，缺省退回 ports[0]。
    quint16 readyPort = 0;
};

/// ports 必须与 nodes 一一对应（长度不符是调用方的 bug，直接返回错误而不是猜端口）。
Result<XrayBatchProbePlan> buildXrayBatchProbePlan(const QVector<Models::Node> &nodes,
                                                   const QVector<quint16> &ports);

/// 对应 probe_batch 里的 `detail.lines().last().unwrap_or("未知错误")`：
/// 注意是"最后一行"而不是"最后一行非空"，与 sing-box 那版语义不同。
QString lastXrayLogLine(const QString &text);

}
