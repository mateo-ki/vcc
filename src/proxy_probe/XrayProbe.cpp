#include "proxy_probe/XrayProbe.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QSemaphore>
#include <QString>

#include <memory>
#include <utility>

#include "kernel/KernelManager.h"
#include "proxy/KernelConfigMessages.h"
#include "proxy/XrayConfig.h"
#include "proxy_probe/JoinWindow.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/TestUrls.h"

namespace ProxyProbe {
namespace {

/// Rust 的 `tokio::time::timeout(Duration::from_secs(6), ..)` + 20ms 轮询。
constexpr int kXrayReadyBudgetMs = 6000;
constexpr int kXrayReadyPollMs = 20;

/// 一个节点探针任务的回传。
struct XrayProbeJoin {
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

/// 单节点实测：拿额度 → 计时 → 经本地 HTTP 入站打 204。
ProbeResult probeXrayNode(const QString &id, quint16 port, int timeoutMs, QSemaphore &networkLimit)
{
    QSemaphoreReleaser releaser(&networkLimit, 1);
    QElapsedTimer clock;
    clock.start();
    const ConnectivityCheck check =
        checkConnectivityThroughLocalProxy(port, kProxyConnectivityTestUrl, timeoutMs);
    return makeXrayProbeResult(id, check, clock.elapsed());
}

}

QString xraySuccessMessage()
{
    return Proxy::Messages::xrayRealProxyOk();
}

QString xrayFailureMessage(const QString &error)
{
    return Proxy::Messages::fill(Proxy::Messages::xrayRealProxyFailed(), {{"{error}", error}});
}

ProbeResult makeXrayProbeResult(const QString &id, const ConnectivityCheck &check,
                                qint64 elapsedMs)
{
    if (check.ok) {
        ProbeResult result;
        result.id = id;
        result.ok = true;
        result.latencyMs = probeLatencyMs(elapsedMs);
        result.message = xraySuccessMessage();
        return result;
    }
    return makeProbeResult(id, xrayFailureMessage(check.error));
}

Result<QVector<ProbeResult>> probeBatchWithXray(const QString &binary,
                                                const QVector<Models::Node> &nodes, int timeoutMs,
                                                QSemaphore &networkLimit)
{
    PortReservations reservations;
    QVector<quint16> ports;
    const Status reserved = reservations.reserveMany(nodes.size(), &ports);
    if (reserved.isErr()) {
        return Result<QVector<ProbeResult>>::withError(reserved.error());
    }

    const Result<Proxy::XrayBatchProbePlan> plan = Proxy::buildXrayBatchProbePlan(nodes, ports);
    if (plan.isErr()) {
        return Result<QVector<ProbeResult>>::withError(plan.error());
    }

    QVector<ProbeResult> results;
    results.reserve(nodes.size() * 2);
    for (qsizetype index = 0; index < plan.value().failures.size(); ++index) {
        const QString &failure = plan.value().failures.at(index);
        if (!failure.isNull()) {
            results.append(makeProbeResult(nodes.at(index).id, failure));
        }
    }
    if (!plan.value().hasInbounds) {
        return Result<QVector<ProbeResult>>::withValue(results);
    }
    const quint16 readyPort = plan.value().readyPort;
    reservations.release();

    Result<TempWorkDir> created = TempWorkDir::create(QStringLiteral("vpn-parser-xray-probe"));
    if (created.isErr()) {
        return Result<QVector<ProbeResult>>::withError(created.error());
    }
    TempWorkDir workDir = created.takeValue();
    const QString configPath = workDir.filePath(QStringLiteral("config.json"));
    const Status written = writeTextFile(configPath,
                                         QString::fromStdString(plan.value().config.dump(2)));
    if (written.isErr()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(written.error());
    }
    const QString logPath = workDir.filePath(QStringLiteral("xray.log"));
    // Rust 的批量测速只给 `run -c <file>`：既不 -D，也不设 XRAY_LOCATION_ASSET
    // （常驻代理那条路径才设，批量配置里没有 geo 规则所以够用）。照抄。
    Result<std::unique_ptr<QProcess>> started =
        startKernelProcess(binary, {QStringLiteral("run"), QStringLiteral("-c"), configPath},
                           logPath);
    if (started.isErr()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(started.error());
    }
    std::unique_ptr<QProcess> child = started.takeValue();

    if (!waitUntilPortReady(*child, readyPort, kXrayReadyBudgetMs, kXrayReadyPollMs)) {
        killProcess(child.get());
        const QString detail = Proxy::lastXrayLogLine(readLogFile(logPath));
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(
            Proxy::Messages::fill(Proxy::Messages::xrayBatchStartupFailed(), {{"{}", detail}}));
    }

    // 与 sing-box 同样的 Rust 行为：整批每个节点都起探针任务，建配失败的节点因此会在结果里
    // 出现两次（一条"配置不支持"、一条"真实代理测试失败"）。
    JoinWindow<XrayProbeJoin> window(nodes.size());
    qsizetype cursor = 0;
    for (;;) {
        while (cursor < nodes.size()) {
            const qsizetype index = cursor;
            const QString id = nodes.at(index).id;
            const quint16 port = ports.at(index);
            const bool spawned = window.trySpawn([id, port, timeoutMs, &networkLimit]() {
                XrayProbeJoin join;
                join.result = probeXrayNode(id, port, timeoutMs, networkLimit);
                return join;
            });
            if (!spawned) {
                break;
            }
            ++cursor;
        }
        Joined<XrayProbeJoin> joined;
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

Result<QVector<ProbeResult>> verifyNodesWithXray(const QVector<Models::Node> &nodes, int timeoutMs,
                                                 const ProgressFn &progress,
                                                 QSemaphore &networkLimit, size_t batchSize,
                                                 bool conditionalIsolation)
{
    Q_UNUSED(batchSize);
    Q_UNUSED(conditionalIsolation);
    if (nodes.isEmpty()) {
        return Result<QVector<ProbeResult>>::withValue(QVector<ProbeResult>());
    }
    const std::optional<QString> binary = Kernel::findKernelExecutable(Kernel::Kind::Xray);
    if (!binary.has_value()) {
        return Result<QVector<ProbeResult>>::withError(Proxy::Messages::xrayKernelNotFound());
    }
    const QString executable = *binary;
    const qsizetype total = nodes.size();
    QElapsedTimer clock;
    clock.start();
    QVector<ProbeResult> results;
    results.reserve(total);
    const QVector<QVector<Models::Node>> batches = chunkNodes(nodes, Proxy::kXrayBatchSize);
    const qsizetype batchTotal = Proxy::xrayBatchCount(total);
    for (qsizetype batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
        const QVector<Models::Node> &batch = batches.at(batchIndex);
        const Result<QVector<ProbeResult>> outcome =
            probeBatchWithXray(executable, batch, timeoutMs, networkLimit);
        if (outcome.isOk()) {
            for (const ProbeResult &result : outcome.value()) {
                results.append(result);
            }
        } else {
            const QString message = Proxy::Messages::fill(Proxy::Messages::xrayBatchFailed(),
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

Result<QVector<ProbeResult>> verifyNodesWithXrayProgress(const QVector<Models::Node> &nodes,
                                                          int timeoutMs, const ProgressFn &progress)
{
    QSemaphore networkLimit(Proxy::kXrayBatchSize);
    return verifyNodesWithXray(nodes, timeoutMs, progress, networkLimit,
                               size_t(Proxy::kXrayBatchSize), false);
}

Result<QVector<ProbeResult>> verifyNodesWithXray(const QVector<Models::Node> &nodes, int timeoutMs)
{
    return verifyNodesWithXrayProgress(nodes, timeoutMs, ProgressFn());
}

}
