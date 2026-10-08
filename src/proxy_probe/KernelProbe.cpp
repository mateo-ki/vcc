#include "proxy_probe/ProbeTypes.h"

#include "proxy_probe/MihomoProbe.h"
#include "proxy_probe/SingBoxProbe.h"
#include "proxy_probe/XrayProbe.h"

namespace ProxyProbe {

Result<QVector<ProbeResult>> verifyNodesWithKernel(Kernel::Kind kind,
                                                   const QVector<Models::Node> &nodes,
                                                   int timeoutMs, const ProgressFn &progress,
                                                   QSemaphore &networkLimit, size_t batchSize,
                                                   bool conditionalIsolation)
{
    // 对应 Rust 的 kernel_adapter(kernel).verify_nodes(..)：三个内核拿到同一组参数，
    // 但只有 mihomo 真的用 batch_size / conditional_isolation（sing-box 与 xray 的
    // adapter 形参就叫 _batch_size / _conditional_isolation）。
    switch (kind) {
    case Kernel::Kind::Mihomo:
        return verifyNodesWithMihomo(nodes, timeoutMs, progress, networkLimit, batchSize,
                                     conditionalIsolation);
    case Kernel::Kind::SingBox:
        return verifyNodesWithSingBox(nodes, timeoutMs, progress, networkLimit, batchSize,
                                      conditionalIsolation);
    case Kernel::Kind::Xray:
        return verifyNodesWithXray(nodes, timeoutMs, progress, networkLimit, batchSize,
                                   conditionalIsolation);
    }
    return Result<QVector<ProbeResult>>::withError(QStringLiteral("unknown kernel kind"));
}

}
