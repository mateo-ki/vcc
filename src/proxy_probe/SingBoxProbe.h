#pragma once

#include <QString>
#include <QVector>

#include "core/Result.h"
#include "models/Node.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/ProbeTypes.h"

class QSemaphore;

namespace ProxyProbe {

/// Rust proxy/sing_box.rs 的运行时半边：进程编排 + 端口预留 + 实测循环。
/// 配置合并（一个进程挂 N 个带 tag 的 mixed 入站）由 Proxy::buildSingBoxBatchProbePlan
/// 提供，日志尾巴由 Proxy::lastSingBoxLogLine 提供。
///
/// 合约函数 `verifyNodesWithSingBox(nodes, timeoutMs, progress, limit, batchSize,
/// conditionalIsolation)` 对应 Rust 的
/// `verify_nodes_with_sing_box_progress_limited`。Rust 侧那两个参数在 kernel adapter 里
/// 就被丢掉了（`_batch_size` / `_conditional_isolation`），批大小恒为 64，这里照抄。

/// Rust 的 `verify_nodes_with_sing_box`：无进度，自带 64 份网络额度。
Result<QVector<ProbeResult>> verifyNodesWithSingBox(const QVector<Models::Node> &nodes,
                                                    int timeoutMs);
/// Rust 的 `verify_nodes_with_sing_box_progress`。
Result<QVector<ProbeResult>> verifyNodesWithSingBoxProgress(const QVector<Models::Node> &nodes,
                                                            int timeoutMs,
                                                            const ProgressFn &progress);

/// 一批 = 一个 sing-box 进程（Rust 的 probe_batch_with_sing_box）。
Result<QVector<ProbeResult>> probeBatchWithSingBox(const QString &binary,
                                                   const QVector<Models::Node> &nodes,
                                                   int timeoutMs, QSemaphore &networkLimit);

/// 消息拼装的纯函数（测试直接断言，不需要真起内核）：
/// `Sing-box 真实代理测试失败: Google: <error>` / `Sing-box 真实代理可用`。
QString singBoxFailureMessage(const QString &error);
QString singBoxSuccessMessage();

/// 一次实测的判定（纯函数）：204 才算通，延迟取整轮计时且至少 1ms；
/// 失败时把 Rust 的 `Google: <detail>` 前缀拼上再套外层消息。
ProbeResult makeSingBoxProbeResult(const QString &id, const ConnectivityCheck &check,
                                   qint64 elapsedMs);

}
