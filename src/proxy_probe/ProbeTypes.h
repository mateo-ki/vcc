#pragma once

#include <QString>
#include <QVector>

#include <functional>
#include <optional>

#include <QSemaphore>

#include "core/Result.h"
#include "kernel/KernelTypes.h"
#include "models/Node.h"
#include "proxy_probe/TestUrls.h"

namespace ProxyProbe {

/// 常驻内核一轮装多少节点（Rust 的 PERSISTENT_MIHOMO_BATCH_SIZE）。
inline constexpr size_t kPersistentMihomoBatchSize = 8192;

/// 单个节点的判定结果，对应 Rust 的 ProxyProbeResult。
struct ProbeResult {
    QString id;
    bool ok = false;
    std::optional<qint64> latencyMs;
    QString message;
};

/// 进度，对应 Rust 的 ProxyProbeProgress。elapsedMs 是从整轮开始算的单调毫秒。
struct ProbeProgress {
    size_t done = 0;
    size_t total = 0;
    size_t ok = 0;
    size_t fail = 0;
    qint64 elapsedMs = 0;
    std::optional<size_t> batchIndex;
    std::optional<size_t> batchTotal;
};

using ProgressFn = std::function<void(const ProbeProgress &)>;

/// 下面三个都是阻塞实现：只在 worker 线程调用（GUI 线程经 QtConcurrent 进来）。
/// networkLimit 是跨内核共享的网络并发额度（对应 Rust 的 Arc<Semaphore>）。
/// progress 为空表示不要进度。
Result<QVector<ProbeResult>> verifyNodesWithMihomo(const QVector<Models::Node> &nodes,
                                                   int timeoutMs, const ProgressFn &progress,
                                                   QSemaphore &networkLimit, size_t batchSize,
                                                   bool conditionalIsolation);

Result<QVector<ProbeResult>> verifyNodesWithSingBox(const QVector<Models::Node> &nodes,
                                                    int timeoutMs, const ProgressFn &progress,
                                                    QSemaphore &networkLimit, size_t batchSize,
                                                    bool conditionalIsolation);

Result<QVector<ProbeResult>> verifyNodesWithXray(const QVector<Models::Node> &nodes, int timeoutMs,
                                                 const ProgressFn &progress,
                                                 QSemaphore &networkLimit, size_t batchSize,
                                                 bool conditionalIsolation);

/// 对应 proxy/mod.rs 的 verify_nodes_with_kernel_progress：按内核分派。
Result<QVector<ProbeResult>> verifyNodesWithKernel(Kernel::Kind kind,
                                                   const QVector<Models::Node> &nodes,
                                                   int timeoutMs, const ProgressFn &progress,
                                                   QSemaphore &networkLimit, size_t batchSize,
                                                   bool conditionalIsolation);

}
