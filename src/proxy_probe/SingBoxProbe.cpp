#include "proxy_probe/SingBoxProbe.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QSemaphore>
#include <QString>

#include <memory>
#include <utility>

#include "kernel/KernelManager.h"
#include "proxy/KernelConfigMessages.h"
#include "proxy/SingBoxConfig.h"
#include "proxy_probe/JoinWindow.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/TestUrls.h"

namespace ProxyProbe {
namespace {

/// Rust 的 wait_for_sing_box_port：5 秒预算、50ms 轮询。
constexpr int kSingBoxReadyBudgetMs = 5000;
constexpr int kSingBoxReadyPollMs = 50;
/// Rust 传给探针的目标名（&'static str "Google"）。
const QString kSingBoxTargetName = QStringLiteral("Google");

/// 一个节点探针任务的回传，对应 Rust JoinSet 里那个 ProxyProbeResult。
struct SingBoxProbeJoin {
    qsizetype index = -1;
    ProbeResult result;
};

ProbeResult makeProbeResult(const QString &id, const QString &message)
{
    ProbeResult result;
    result.id = id;
    result.ok = false;
    result.message = message;
    return result;
}

/// 单节点实测：拿额度（Rust 的 acquire_owned）→ 计时 → 经本地 mixed 端口打 204。
/// 计时起点在拿到额度**之后**，所以排队等额度的时间不会算进延迟。
ProbeResult probeSingBoxNode(const QString &id, quint16 port, int timeoutMs,
                             QSemaphore &networkLimit)
{
    QSemaphoreReleaser releaser(&networkLimit, 1);
    QElapsedTimer clock;
    clock.start();
    const ConnectivityCheck check =
        checkConnectivityThroughLocalProxy(port, kProxyConnectivityTestUrl, timeoutMs);
    return makeSingBoxProbeResult(id, check, clock.elapsed());
}

}

QString singBoxSuccessMessage()
{
    return Proxy::Messages::singBoxRealProxyOk();
}

QString singBoxFailureMessage(const QString &error)
{
    return Proxy::Messages::fill(Proxy::Messages::singBoxRealProxyFailed(),
                                 {{"{error}", error}});
}

ProbeResult makeSingBoxProbeResult(const QString &id, const ConnectivityCheck &check,
                                   qint64 elapsedMs)
{
    if (check.ok) {
        ProbeResult result;
        result.id = id;
        result.ok = true;
        result.latencyMs = probeLatencyMs(elapsedMs);
        result.message = singBoxSuccessMessage();
        return result;
    }
    // Rust 的 probe_connectivity_target 把目标名拼在错误里，再套进外层消息。
    return makeProbeResult(id, singBoxFailureMessage(
                                   QStringLiteral("%1: %2").arg(kSingBoxTargetName,
                                                                 check.error)));
}

Result<QVector<ProbeResult>> probeBatchWithSingBox(const QString &binary,
                                                   const QVector<Models::Node> &nodes,
                                                   int timeoutMs, QSemaphore &networkLimit)
{
    PortReservations reservations;
    QVector<quint16> ports;
    const Status reserved = reservations.reserveMany(nodes.size(), &ports);
    if (reserved.isErr()) {
        return Result<QVector<ProbeResult>>::withError(reserved.error());
    }

    const Result<Proxy::SingBoxBatchProbePlan> plan =
        Proxy::buildSingBoxBatchProbePlan(nodes, ports);
    if (plan.isErr()) {
        return Result<QVector<ProbeResult>>::withError(plan.error());
    }

    QVector<ProbeResult> results;
    results.reserve(nodes.size() * 2);
    for (qsizetype index = 0; index < plan.value().failures.size(); ++index) {
        const QString &failure = plan.value().failures.at(index);
        if (!failure.isNull()) {
            // null QString = 建配成功；非 null 已经是 Rust 的完整文案。
            results.append(makeProbeResult(nodes.at(index).id, failure));
        }
    }
    if (!plan.value().hasInbounds) {
        // Rust 的 `if inbounds.is_empty() { return Ok(immediate) }`：一个都没建成就不起进程。
        return Result<QVector<ProbeResult>>::withValue(results);
    }
    const quint16 readyPort = plan.value().readyPort;
    // 配置写完了才放端口：内核要 bind 它们（Rust 的 drop(reservations)）。
    reservations.release();

    Result<TempWorkDir> created = TempWorkDir::create(QStringLiteral("vpn-parser-sing-box-batch"));
    if (created.isErr()) {
        return Result<QVector<ProbeResult>>::withError(created.error());
    }
    TempWorkDir workDir = created.takeValue();
    const QString configPath = workDir.filePath(QStringLiteral("config.json"));
    const Status written = writeTextFile(
        configPath, QString::fromStdString(plan.value().config.dump(2)));
    if (written.isErr()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(written.error());
    }
    const QString logPath = workDir.filePath(QStringLiteral("sing-box.log"));
    Result<std::unique_ptr<QProcess>> started =
        startKernelProcess(binary,
                           {QStringLiteral("run"), QStringLiteral("-D"), workDir.root(),
                            QStringLiteral("-c"), configPath},
                           logPath);
    if (started.isErr()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(started.error());
    }
    std::unique_ptr<QProcess> child = started.takeValue();

    if (!waitUntilPortReady(*child, readyPort, kSingBoxReadyBudgetMs, kSingBoxReadyPollMs)) {
        killProcess(child.get());
        const QString detail = Proxy::lastSingBoxLogLine(readLogFile(logPath));
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(
            Proxy::Messages::fill(Proxy::Messages::singBoxStartupFailed(), {{"{}", detail}}));
    }

    // 注意：Rust 给**整批每个节点**都起探针任务，包括建配失败的那些。那些端口内核根本没
    // listen，于是同一个 id 会再多出一条"真实代理测试失败"——也就是建配失败的节点在结果里
    // 出现两次。这里逐字照抄（消费方按 id 落库，Rust 也是这个行为）。
    JoinWindow<SingBoxProbeJoin> window(nodes.size());
    qsizetype cursor = 0;
    for (;;) {
        while (cursor < nodes.size()) {
            const qsizetype index = cursor;
            const QString id = nodes.at(index).id;
            const quint16 port = ports.at(index);
            const bool spawned = window.trySpawn([id, port, timeoutMs, &networkLimit]() {
                SingBoxProbeJoin join;
                join.result = probeSingBoxNode(id, port, timeoutMs, networkLimit);
                return join;
            });
            if (!spawned) {
                break;
            }
            ++cursor;
        }
        Joined<SingBoxProbeJoin> joined;
        if (!window.joinNext(&joined)) {
            break;
        }
        if (joined.failed) {
            continue;
        }
        results.append(joined.value.result);
    }

    killProcess(child.get());
    child.reset();
    workDir.remove();
    return Result<QVector<ProbeResult>>::withValue(results);
}

Result<QVector<ProbeResult>> verifyNodesWithSingBox(const QVector<Models::Node> &nodes,
                                                    int timeoutMs, const ProgressFn &progress,
                                                    QSemaphore &networkLimit, size_t batchSize,
                                                    bool conditionalIsolation)
{
    Q_UNUSED(batchSize);
    Q_UNUSED(conditionalIsolation);
    if (nodes.isEmpty()) {
        return Result<QVector<ProbeResult>>::withValue(QVector<ProbeResult>());
    }
    const std::optional<QString> binary = Kernel::findKernelExecutable(Kernel::Kind::SingBox);
    if (!binary.has_value()) {
        return Result<QVector<ProbeResult>>::withError(Proxy::Messages::singBoxKernelNotFound());
    }
    const QString executable = *binary;
    const qsizetype total = nodes.size();
    QElapsedTimer clock;
    clock.start();
    QVector<ProbeResult> results;
    results.reserve(total);
    const QVector<QVector<Models::Node>> batches = chunkNodes(nodes, Proxy::kSingBoxBatchSize);
    const qsizetype batchTotal = Proxy::singBoxBatchCount(total);
    for (qsizetype batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
        const QVector<Models::Node> &batch = batches.at(batchIndex);
        const Result<QVector<ProbeResult>> outcome =
            probeBatchWithSingBox(executable, batch, timeoutMs, networkLimit);
        if (outcome.isOk()) {
            for (const ProbeResult &result : outcome.value()) {
                results.append(result);
            }
        } else {
            const QString message = Proxy::Messages::fill(Proxy::Messages::singBoxBatchFailed(),
                                                           {{"{error}", outcome.error()}});
            for (const Models::Node &node : batch) {
                results.append(makeProbeResult(node.id, message));
            }
        }
        emitProgress(progress, results.size(), countPassed(results), total, clock.elapsed(),
                     batchIndex + 1, batchTotal);
    }
    return Result<QVector<ProbeResult>>::withValue(results);
}

Result<QVector<ProbeResult>> verifyNodesWithSingBoxProgress(const QVector<Models::Node> &nodes,
                                                            int timeoutMs,
                                                            const ProgressFn &progress)
{
    QSemaphore networkLimit(Proxy::kSingBoxBatchSize);
    return verifyNodesWithSingBox(nodes, timeoutMs, progress, networkLimit,
                                  size_t(Proxy::kSingBoxBatchSize), false);
}

Result<QVector<ProbeResult>> verifyNodesWithSingBox(const QVector<Models::Node> &nodes,
                                                    int timeoutMs)
{
    return verifyNodesWithSingBoxProgress(nodes, timeoutMs, ProgressFn());
}

}
