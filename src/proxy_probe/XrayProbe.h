#pragma once

#include <QString>
#include <QVector>

#include "core/Result.h"
#include "models/Node.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/ProbeTypes.h"

class QSemaphore;

namespace ProxyProbe {

/// Rust proxy/xray.rs 的运行时半边：Xray 进程编排 + 端口预留 + 实测循环。
/// 配置合并由 Proxy::buildXrayBatchProbePlan 提供，日志尾巴由 Proxy::lastXrayLogLine 提供。
///
/// 合约函数 `verifyNodesWithXray(nodes, timeoutMs, progress, limit, batchSize,
/// conditionalIsolation)` 对应 Rust 的 `verify_nodes_with_xray_progress_limited`。
/// Rust 的 kernel adapter 把 `_batch_size` / `_conditional_isolation` 直接丢掉，批大小恒为
/// 64，这里照抄。

/// Rust 的 `verify_nodes_with_xray`：无进度，自带 64 份网络额度。
Result<QVector<ProbeResult>> verifyNodesWithXray(const QVector<Models::Node> &nodes, int timeoutMs);
/// Rust 的 `verify_nodes_with_xray_progress`。
Result<QVector<ProbeResult>> verifyNodesWithXrayProgress(const QVector<Models::Node> &nodes,
                                                          int timeoutMs,
                                                          const ProgressFn &progress);

/// 一批 = 一个 xray 进程（Rust 的 probe_batch）。
Result<QVector<ProbeResult>> probeBatchWithXray(const QString &binary,
                                                const QVector<Models::Node> &nodes, int timeoutMs,
                                                QSemaphore &networkLimit);

/// `Xray 真实代理测试失败: <error>` / `Xray 真实代理可用`。Xray 那侧不拼目标名前缀
/// （Rust 的 xray 探针直接 `Err(format!("HTTP {}", status))`，没有 "{target_name}: "）。
QString xrayFailureMessage(const QString &error);
QString xraySuccessMessage();

/// 一次实测的判定（纯函数）：204 才算通，延迟至少 1ms。
ProbeResult makeXrayProbeResult(const QString &id, const ConnectivityCheck &check,
                                qint64 elapsedMs);

}
