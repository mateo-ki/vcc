#include "speedtest/SpeedTester.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QSemaphore>
#include <QString>
#include <QVector>

#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "core/HostUtil.h"
#include "core/Log.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "db/TextOrder.h"
#include "kernel/KernelManager.h"
#include "kernel/KernelTypes.h"
#include "models/Node.h"
#include "parser/Common.h"
#include "parser/Fingerprint.h"
#include "proxy_probe/ProbeTypes.h"
#include "speedtest/JobQueue.h"
#include "speedtest/SpeedtestMessages.h"
#include "speedtest/SpeedtestTypes.h"
#include "speedtest/TcpProbe.h"

namespace Speedtest {
namespace {

/// 一次 DNS 查询的预算（Rust 在解析阶段和 connect_fast 里都是 800ms）。
inline constexpr qint64 kDnsLookupTimeoutMs = 800;
/// 真代理握手默认批次（Rust: 非常驻内核时用 256）。
inline constexpr size_t kDefaultProxyBatchSize = 256;
/// 写库的分片大小（Rust: chunks(500)）。
inline constexpr int kDbChunk = 500;
/// 下载速率的合理上限，超过它的一定不是真吞吐。
inline constexpr double kMaxPlausibleKbps = 20480.0;

/// 对应 Rust 的 `network_limit.acquire_owned().await`：整个探测期间持有额度。
/// Qt 的 QSemaphore 没有 close()，所以 Rust 里那条「测速并发控制已关闭」的错误分支
/// 在 C++ 这边不可达（见 Messages::networkGateClosed()，文本仍与 Rust 一致）。
class Permit {
public:
    explicit Permit(QSemaphore &semaphore) : semaphore_(semaphore) { semaphore_.acquire(1); }
    Permit(const Permit &) = delete;
    Permit &operator=(const Permit &) = delete;
    ~Permit() { semaphore_.release(1); }

private:
    QSemaphore &semaphore_;
};

/// (host, port) 的映射键：主机名里不会出现 0x1F，所以拼接无歧义（Rust 用元组键）。
QString targetKey(const QString &host, int port)
{
    return QString::number(port) + QChar(0x1F) + host;
}

/// 对应 Rust 的 `resolved.get(&(host, port)).cloned().unwrap_or_else(|| Err("dns result missing"))`。
ResolvedAddresses resolutionFor(const QHash<QString, ResolvedAddresses> &resolved,
                                const QString &host, int port)
{
    const auto found = resolved.constFind(targetKey(host, port));
    if (found == resolved.constEnd()) {
        return ResolvedAddresses::withError(QStringLiteral("dns result missing"));
    }
    return found.value();
}

/// probe_full 与 probe_full_resolved 共用的后半段（Rust 里是两份一模一样的分支）。
ProbeOutcome finishProbe(QTcpSocket &socket, qint64 latencyMs, const QString &host,
                         bool measureDownload, int downloadTestMs)
{
    if (!measureDownload) {
        ProbeOutcome outcome;
        outcome.ok = true;
        outcome.latencyMs = latencyMs;
        outcome.message = QStringLiteral("%1 ms (TCP)").arg(latencyMs);
        return outcome;
    }
    const qint64 downloadMs =
        qBound<qint64>(Q_INT64_C(200), qint64(downloadTestMs), Q_INT64_C(5000));
    const DownloadMeasurement measured = measureTcpDownload(socket, host, downloadMs);
    std::optional<double> kbps;
    if (measured.kbps.has_value() && std::isfinite(*measured.kbps) && *measured.kbps > 0.0
        && *measured.kbps < kMaxPlausibleKbps) {
        kbps = measured.kbps;
    }
    ProbeOutcome outcome;
    outcome.ok = true;
    outcome.latencyMs = latencyMs;
    outcome.downloadKbps = kbps;
    if (kbps.has_value()) {
        outcome.message = Messages::format(
            QStringLiteral("{latency_ms} ms / {value:.1} KB/s"), {},
            {{"{latency_ms}", QString::number(latencyMs)},
             {"{value:.1}", QString::number(*kbps, 'f', 1)}});
    } else {
        outcome.message =
            Messages::format(Messages::downloadUnmeasurable(), {},
                             {{"{latency_ms}", QString::number(latencyMs)},
                              {"{detail}", measured.detail}});
    }
    return outcome;
}

size_t countOk(const QVector<ProxyProbe::ProbeResult> &results)
{
    return size_t(std::count_if(results.begin(), results.end(),
                                [](const ProxyProbe::ProbeResult &r) { return r.ok; }));
}

/// done - ok，对应 Rust 的 saturating_sub。
size_t saturatingSub(size_t left, size_t right)
{
    return left > right ? left - right : 0;
}

/// Rust 的 `results.len().saturating_sub(ok)`，两边都是 usize。
int saturatingCount(qint64 left, qint64 right)
{
    return int(left > right ? left - right : 0);
}

}

QString intensityToText(SpeedtestIntensity intensity)
{
    switch (intensity) {
    case SpeedtestIntensity::Fast:
        return QStringLiteral("fast");
    case SpeedtestIntensity::Standard:
        return QStringLiteral("standard");
    case SpeedtestIntensity::Deep:
        return QStringLiteral("deep");
    }
    return QStringLiteral("standard");
}

SpeedtestIntensity intensityFromText(const QString &text)
{
    if (text == QLatin1String("fast")) {
        return SpeedtestIntensity::Fast;
    }
    if (text == QLatin1String("deep")) {
        return SpeedtestIntensity::Deep;
    }
    return SpeedtestIntensity::Standard;
}

QString intensityLabel(SpeedtestIntensity intensity)
{
    switch (intensity) {
    case SpeedtestIntensity::Fast:
        return Messages::fastLabel();
    case SpeedtestIntensity::Standard:
        return Messages::standardLabel();
    case SpeedtestIntensity::Deep:
        return Messages::deepLabel();
    }
    return Messages::standardLabel();
}

QString intensityDescription(SpeedtestIntensity intensity)
{
    switch (intensity) {
    case SpeedtestIntensity::Fast:
        return Messages::fastDescription();
    case SpeedtestIntensity::Standard:
        return Messages::standardDescription();
    case SpeedtestIntensity::Deep:
        return Messages::deepDescription();
    }
    return Messages::standardDescription();
}

qint64 prefilterTimeoutMs(const SpeedTestOptions &options)
{
    switch (options.intensity) {
    case SpeedtestIntensity::Fast:
        return qMin<qint64>(options.timeoutMs, 500);
    case SpeedtestIntensity::Standard:
        return qMin<qint64>(options.timeoutMs, kTcpPrefilterTimeoutMs);
    case SpeedtestIntensity::Deep:
        return qMax<qint64>(options.timeoutMs, 1200);
    }
    return kTcpPrefilterTimeoutMs;
}

qint64 proxyTimeoutMs(const SpeedTestOptions &options)
{
    switch (options.intensity) {
    case SpeedtestIntensity::Fast:
        return 1800;
    case SpeedtestIntensity::Standard:
        return qBound<qint64>(Q_INT64_C(2500), qint64(options.timeoutMs), Q_INT64_C(15000));
    case SpeedtestIntensity::Deep:
        return qBound<qint64>(Q_INT64_C(4000), qint64(options.timeoutMs), Q_INT64_C(20000));
    }
    return 1500;
}

bool requiresProxyWithoutTcp(const QString &protocol)
{
    const QString lowered = Parser::Common::toAsciiLower(protocol);
    return lowered == QLatin1String("hysteria") || lowered == QLatin1String("hysteria2")
        || lowered == QLatin1String("hy2") || lowered == QLatin1String("tuic")
        || lowered == QLatin1String("wireguard") || lowered == QLatin1String("wg");
}

Result<Models::SpeedTestSummary> buildSpeedtestSummary(const QVector<Models::LatencyResult> &results)
{
    if (results.isEmpty()) {
        return Result<Models::SpeedTestSummary>::withError(Messages::noNodesOrAllFailed());
    }
    Models::SpeedTestSummary summary;
    summary.total = results.size();
    for (const Models::LatencyResult &result : results) {
        if (result.ok) {
            ++summary.ok;
        }
    }
    summary.fail = summary.total - summary.ok;
    qint64 latencySum = 0;
    int latencyCount = 0;
    double downloadSum = 0.0;
    int downloadCount = 0;
    for (const Models::LatencyResult &result : results) {
        if (result.latencyMs.has_value()) {
            latencySum += *result.latencyMs;
            ++latencyCount;
        }
        if (result.downloadKbps.has_value()) {
            downloadSum += *result.downloadKbps;
            ++downloadCount;
        }
    }
    if (latencyCount > 0) {
        summary.avgMs = double(latencySum) / double(latencyCount);
    }
    if (downloadCount > 0) {
        summary.avgDownloadKbps = downloadSum / double(downloadCount);
    }
    summary.results = results;
    return Result<Models::SpeedTestSummary>::withValue(std::move(summary));
}

void reportSpeedtestProgress(const ProgressFn &progress, SpeedTestPhase phase, size_t done,
                             size_t total, size_t ok, qint64 elapsedMs)
{
    if (!progress) {
        return;
    }
    SpeedTestProgress state;
    state.phase = phase;
    state.done = done;
    state.total = total;
    state.ok = ok;
    state.fail = saturatingSub(done, ok);
    state.elapsedMs = elapsedMs;
    progress(state);
}

SpeedTester::SpeedTester(std::shared_ptr<Database> db, Tasks::TaskTracker tasks)
    : db_(std::move(db)), tasks_(std::move(tasks))
{
}

ProbeOutcome SpeedTester::probeFull(const QString &host, int port, int timeoutMs,
                                    bool measureDownload, int downloadTestMs)
{
    if (!HostUtil::isPublicProxyHost(host)) {
        ProbeOutcome outcome;
        outcome.message = QStringLiteral("skip invalid/private host: %1").arg(host);
        return outcome;
    }
    if (port == 0) {
        ProbeOutcome outcome;
        outcome.message = QStringLiteral("invalid port 0");
        return outcome;
    }
    const qint64 timeout = qMax<qint64>(200, timeoutMs);
    Result<Connected> connected = connectFast(host, port, timeout);
    if (connected.isErr()) {
        ProbeOutcome outcome;
        outcome.message = connected.error();
        return outcome;
    }
    Connected stream = connected.takeValue();
    return finishProbe(*stream.socket, stream.latencyMs, host, measureDownload, downloadTestMs);
}

ProbeOutcome SpeedTester::probeFullResolved(const QString &host, int port, int timeoutMs,
                                            int downloadTestMs, bool measureDownload,
                                            const ResolvedAddresses &addresses)
{
    if (!HostUtil::isPublicProxyHost(host) || port == 0) {
        ProbeOutcome outcome;
        outcome.message = QStringLiteral("skip invalid/private host: %1").arg(host);
        return outcome;
    }
    if (addresses.isErr()) {
        ProbeOutcome outcome;
        outcome.message = addresses.error();
        return outcome;
    }
    const qint64 timeout = qMax<qint64>(200, timeoutMs);
    Result<Connected> connected = connectResolved(addresses.value(), port, timeout);
    if (connected.isErr()) {
        ProbeOutcome outcome;
        outcome.message = connected.error();
        return outcome;
    }
    Connected stream = connected.takeValue();
    return finishProbe(*stream.socket, stream.latencyMs, host, measureDownload, downloadTestMs);
}

Result<Models::LatencyResult> SpeedTester::testOne(const QString &nodeId,
                                                   const SpeedTestOptions &options)
{
    QVector<QString> ids;
    ids.append(nodeId);
    Result<Models::SpeedTestSummary> summary = testMany(std::optional<QVector<QString>>(ids), options);
    if (summary.isErr()) {
        return Result<Models::LatencyResult>::withError(summary.error());
    }
    QVector<Models::LatencyResult> results = summary.takeValue().results;
    if (results.isEmpty()) {
        return Result<Models::LatencyResult>::withError(QStringLiteral("node not found"));
    }
    return Result<Models::LatencyResult>::withValue(results.takeLast());
}

Result<Models::SpeedTestSummary> SpeedTester::testAllSources(const SpeedTestOptions &options)
{
    return testAllSourcesGlobally(options, CountProgressFn());
}

Result<Models::SpeedTestSummary> SpeedTester::testAllSourcesWithProgress(
    const SpeedTestOptions &options, const CountProgressFn &progress)
{
    return testAllSourcesGlobally(options, progress);
}

Result<Models::SpeedTestSummary> SpeedTester::testMany(const std::optional<QVector<QString>> &nodeIds,
                                                       const SpeedTestOptions &options)
{
    return testManyWithProgress(nodeIds, options, ProgressFn());
}

Result<Models::SpeedTestSummary> SpeedTester::testManyWithProgress(
    const std::optional<QVector<QString>> &nodeIds, const SpeedTestOptions &options,
    const ProgressFn &progress)
{
    QSemaphore networkLimit(qBound(1, options.concurrency, 512));
    return testManyWithProgressLimited(nodeIds, options, progress, networkLimit);
}

Result<Models::SpeedTestSummary> SpeedTester::testManyWithProgressLimited(
    const std::optional<QVector<QString>> &nodeIds, const SpeedTestOptions &options,
    const ProgressFn &progress, QSemaphore &networkLimit)
{
    // 一次批量取，别再 for id in ids { get_node(id) }（那是每个 id 扇出一遍全部分库）。
    Result<QVector<Models::Node>> nodes = nodeIds.has_value()
        ? db_->getNodes(*nodeIds)
        : db_->listNodesForSpeedtest();
    if (nodes.isErr()) {
        return Result<Models::SpeedTestSummary>::withError(nodes.error());
    }
    return runSpeedtest(nodes.value(), options, progress, networkLimit);
}

Result<Models::SpeedTestSummary> SpeedTester::testAllSourcesGlobally(
    const SpeedTestOptions &options, const CountProgressFn &progress)
{
    Result<QVector<Models::NodeGroupSummary>> groups =
        db_->listNodeGroups(std::nullopt, std::nullopt, std::nullopt);
    if (groups.isErr()) {
        return Result<Models::SpeedTestSummary>::withError(groups.error());
    }

    // 订阅源经常发布同一批节点：分开测等于把 TCP 探测和内核启动次数都放大一遍。
    // 这里按指纹去重，代表测一次，再把它的严格 Google 结果回写给所有含同指纹的订阅。
    QVector<QString> sourceKeys;
    QVector<std::pair<QString, QString>> taskSources;
    for (const Models::NodeGroupSummary &group : groups.value()) {
        const QString key = group.sourceId.isNull() || group.sourceId.isEmpty()
            ? kUngroupedSourceId
            : group.sourceId;
        sourceKeys.append(key);
        taskSources.append({key, group.sourceName});
    }
    const QString taskId = tasks_.start(Tasks::Kind::Speedtest, taskSources);

    Result<QVector<Models::Node>> allNodes = db_->listNodesForSpeedtest();
    if (allNodes.isErr()) {
        return Result<Models::SpeedTestSummary>::withError(allNodes.error());
    }
    if (allNodes->isEmpty()) {
        return Result<Models::SpeedTestSummary>::withError(Messages::noNodesToTest());
    }

    QHash<QString, QVector<Models::Node>> copies;
    QVector<QString> copyOrder;
    QHash<QString, size_t> sourceCounts;
    QVector<QString> sourceCountOrder;
    for (const Models::Node &node : allNodes.value()) {
        const QString key =
            node.sourceId.isNull() || node.sourceId.isEmpty() ? kUngroupedSourceId : node.sourceId;
        if (!sourceCounts.contains(key)) {
            sourceCounts.insert(key, 0);
            sourceCountOrder.append(key);
        }
        sourceCounts[key] += 1;
        const QString fingerprint = Parser::nodeFingerprint(node);
        if (!copies.contains(fingerprint)) {
            copies.insert(fingerprint, QVector<Models::Node>());
            copyOrder.append(fingerprint);
        }
        copies[fingerprint].append(node);
    }
    for (const QString &sourceId : sourceCountOrder) {
        tasks_.nodes(taskId, sourceId, sourceCounts.value(sourceId));
        tasks_.phase(taskId, sourceId, Tasks::Phase::TcpPrefilter);
    }

    QVector<Models::Node> representatives;
    representatives.reserve(copyOrder.size());
    QHash<QString, QString> representativeFingerprints;   // 代表 id -> 指纹
    QVector<QString> representativeOrder;
    for (const QString &fingerprint : copyOrder) {
        const Models::Node representative = copies.value(fingerprint).first();
        representativeFingerprints.insert(representative.id, fingerprint);
        representativeOrder.append(representative.id);
        representatives.append(representative);
    }
    qint64 totalNodes = 0;
    for (const size_t count : sourceCounts) {
        totalNodes += qint64(count);
    }
    Log::info(QStringLiteral(
                  "starting globally deduplicated speedtest total_nodes=%1 unique_nodes=%2 duplicates=%3 concurrency=%4")
                  .arg(totalNodes)
                  .arg(qint64(representatives.size()))
                  .arg(totalNodes - qint64(representatives.size()))
                  .arg(qBound(1, options.concurrency, 512)));

    const QElapsedTimer speedtestStarted;
    ProgressFn wrapped;
    if (progress) {
        wrapped = [this, taskId, sourceKeys, progress](const SpeedTestProgress &state) {
            const Tasks::Phase phase = state.phase == SpeedTestPhase::TcpPrefilter
                ? Tasks::Phase::TcpPrefilter
                : Tasks::Phase::GoogleVerify;
            // 每个节点都刷全部源的阶段太浪费（一轮 19000 节点 = 62 万次加锁+扫描），
            // 每 200 个节点或收尾时刷一次即可。
            if (state.done % 200 == 0 || state.done >= state.total) {
                for (const QString &sourceId : sourceKeys) {
                    tasks_.phase(taskId, sourceId, phase);
                }
            }
            progress(state.done, state.total);
        };
    }
    QSemaphore networkLimit(qBound(1, options.concurrency, 512));
    Result<Models::SpeedTestSummary> summary =
        runSpeedtest(representatives, options, wrapped, networkLimit);
    if (summary.isErr()) {
        return summary;
    }

    QHash<QString, Models::LatencyResult> representativeResults;
    for (const Models::LatencyResult &result : summary.value().results) {
        representativeResults.insert(result.id, result);
    }

    QVector<Models::LatencyResult> results;
    QHash<QString, QPair<size_t, size_t>> sourceStats;   // source -> (ok, fail)
    QVector<Database::SpeedUpdate> duplicateUpdates;
    for (const QString &representativeId : representativeOrder) {
        const QString fingerprint = representativeFingerprints.value(representativeId);
        const auto found = representativeResults.constFind(representativeId);
        if (found == representativeResults.constEnd()) {
            continue;
        }
        const Models::LatencyResult &result = found.value();
        for (const Models::Node &node : copies.value(fingerprint)) {
            const QString key =
                node.sourceId.isNull() || node.sourceId.isEmpty() ? kUngroupedSourceId
                                                                  : node.sourceId;
            QPair<size_t, size_t> &entry = sourceStats[key];
            if (result.ok) {
                ++entry.first;
            } else {
                ++entry.second;
            }
            Models::LatencyResult copy = result;
            copy.id = node.id;
            copy.name = node.name;
            copy.server = node.server;
            copy.port = node.port;
            if (copy.id != representativeId) {
                Database::SpeedUpdate update;
                update.id = copy.id;
                update.ok = copy.ok;
                update.latencyMs = copy.latencyMs;
                update.downloadKbps = copy.downloadKbps;
                update.latencyKernel = copy.latencyKernel;
                update.latencyTestKind = copy.latencyTestKind;
                duplicateUpdates.append(update);
            }
            results.append(copy);
        }
    }
    const QElapsedTimer updateStarted;
    for (int offset = 0; offset < duplicateUpdates.size(); offset += kDbChunk) {
        const Status written =
            db_->updateNodeSpeedsBatch(duplicateUpdates.mid(offset, kDbChunk));
        if (written.isErr()) {
            return Result<Models::SpeedTestSummary>::withError(written.error());
        }
    }
    for (const QString &sourceId : sourceKeys) {
        tasks_.phase(taskId, sourceId, Tasks::Phase::Updating);
        tasks_.timings(taskId, sourceId, std::nullopt, std::nullopt, std::nullopt,
                       quint64(speedtestStarted.elapsed()), quint64(updateStarted.elapsed()));
        const QPair<size_t, size_t> stats =
            sourceStats.value(sourceId, qMakePair(size_t(0), size_t(0)));
        tasks_.complete(taskId, sourceId,
                        Messages::format(Messages::googleVerifyDone(), {},
                                         {{"{ok}", QString::number(stats.first)},
                                          {"{fail}", QString::number(stats.second)}}));
    }
    if (progress) {
        progress(size_t(sourceKeys.size()), size_t(sourceKeys.size()));
    }
    return buildSpeedtestSummary(results);
}

Result<Models::SpeedTestSummary> SpeedTester::runSpeedtest(const QVector<Models::Node> &inputNodes,
                                                           const SpeedTestOptions &options,
                                                           const ProgressFn &progress,
                                                           QSemaphore &networkLimit)
{
    // 「沿用已验证结果」：N 分钟内真验证过的节点直接不进本轮，避免每轮都在重测同一批
    // 节点（这是全库重测里最大的一笔重复开销）。
    QVector<Models::Node> nodes = inputNodes;
    if (options.reuseVerifiedMinutes > 0) {
        const qint64 cutoffNanos = TimeUtil::nowUtcNanos()
            - qint64(options.reuseVerifiedMinutes) * 60LL * 1000000000LL;
        QVector<Models::Node> pending;
        pending.reserve(nodes.size());
        qsizetype reused = 0;
        for (const Models::Node &node : nodes) {
            const bool recentlyVerified = node.latencyOk.has_value() && node.latencyTestedAt.hasValue
                && node.latencyTestedAt.nanos >= cutoffNanos;
            if (recentlyVerified) {
                ++reused;
            } else {
                pending.append(node);
            }
        }
        if (reused > 0) {
            Log::info(QStringLiteral("%1 reused=%2").arg(Messages::reusedVerifiedLog()).arg(reused));
        }
        nodes = pending;
    }

    const size_t total = size_t(nodes.size());
    QHash<QString, Models::Node> proxyNodes;
    proxyNodes.reserve(nodes.size());
    for (const Models::Node &node : nodes) {
        proxyNodes.insert(node.id, node);
    }

    const int concurrency = qBound(1, options.concurrency, 512);
    const qint64 prefilterMs = prefilterTimeoutMs(options);
    const qint64 proxyMs = proxyTimeoutMs(options);
    Log::info(QStringLiteral(
                  "starting speedtest total=%1 concurrency=%2 intensity=%3 timeout_ms=%4 download=%5")
                  .arg(qint64(total))
                  .arg(concurrency)
                  .arg(intensityToText(options.intensity))
                  .arg(options.timeoutMs)
                  .arg(options.measureDownload ? 1 : 0));

    const QElapsedTimer started;

    // ---- 解析阶段：每个不同的 (host, port) 只查一次 ----
    QHash<QString, ResolvedAddresses> resolved;
    {
        JobQueue resolver(concurrency);
        QSet<QString> uniqueTargets;
        for (const Models::Node &node : nodes) {
            if (!HostUtil::isPublicProxyHost(node.server)) {
                continue;
            }
            const QString key = targetKey(node.server, node.port);
            if (uniqueTargets.contains(key)) {
                continue;
            }
            uniqueTargets.insert(key);
            const QString host = node.server;
            resolver.submit([&resolver, &networkLimit, &resolved, host, key]() {
                Permit permit(networkLimit);
                const ResolvedAddresses outcome = resolveAddresses(host, kDnsLookupTimeoutMs);
                return [&resolved, key, outcome]() { resolved.insert(key, outcome); };
            });
        }
        resolver.waitAll();
    }

    // ---- TCP 预筛 ----
    QVector<Models::LatencyResult> results;
    results.reserve(int(total));
    size_t done = 0;
    size_t okCount = 0;
    {
        JobQueue inflight(concurrency);
        const int downloadTestMs = options.downloadTestMs;
        // 设了「只做前 N 名」时主扫描不测下载，排完序再补测那一批。
        const bool measureDownload = options.measureDownload && options.downloadTopN == 0;
        for (const Models::Node &node : nodes) {
            const QString id = node.id;
            const QString name = node.name;
            const QString host = node.server;
            const int port = node.port;
            const ResolvedAddresses addresses = resolutionFor(resolved, host, port);
            inflight.submit(
                [&inflight, &networkLimit, &results, &done, &okCount, &progress,
                 &started, total, id, name, host, port, prefilterMs, downloadTestMs,
                 measureDownload, addresses]() {
                    Permit permit(networkLimit);
                    const ProbeOutcome outcome = SpeedTester::probeFullResolved(
                        host, port, int(prefilterMs), downloadTestMs, measureDownload, addresses);
                    return [&results, &done, &okCount, &progress, &started, total, id, name, host,
                            port, outcome]() {
                        Models::LatencyResult row;
                        row.id = id;
                        row.name = name;
                        row.server = host;
                        row.port = port;
                        row.ok = outcome.ok;
                        row.latencyMs = outcome.latencyMs;
                        row.downloadKbps = outcome.downloadKbps;
                        row.latencyKernel = QString();
                        row.latencyTestKind =
                            outcome.ok ? QString() : QStringLiteral("tcp_prefilter");
                        row.message = outcome.message;
                        results.append(row);
                        ++done;
                        if (outcome.ok) {
                            ++okCount;
                        }
                        reportSpeedtestProgress(progress, SpeedTestPhase::TcpPrefilter, done, total,
                                                okCount, started.elapsed());
                        // 运行中的成功计数：进度回调是每个节点一次的，不能再回头扫 results。
                        if (done % 500 == 0 || done == total) {
                            Log::info(QStringLiteral("speedtest progress done=%1 total=%2 ok=%3 "
                                                    "elapsed_ms=%4")
                                          .arg(qint64(done))
                                          .arg(qint64(total))
                                          .arg(qint64(okCount))
                                          .arg(started.elapsed()));
                        }
                    };
                });
        }
        inflight.waitAll();
    }

    // ---- 按延迟排名补测下载速率 ----
    if (options.measureDownload && options.downloadTopN > 0) {
        QVector<int> ranked;
        for (int index = 0; index < results.size(); ++index) {
            if (results.at(index).ok) {
                ranked.append(index);
            }
        }
        std::stable_sort(ranked.begin(), ranked.end(), [&results](int left, int right) {
            return results.at(left).latencyMs.value_or(std::numeric_limits<qint64>::max())
                < results.at(right).latencyMs.value_or(std::numeric_limits<qint64>::max());
        });
        size_t measured = 0;
        {
            JobQueue downloads(concurrency);
            const int limit = qBound(0, options.downloadTopN, int(ranked.size()));
            for (int position = 0; position < limit; ++position) {
                const int index = ranked.at(position);
                const auto found = proxyNodes.constFind(results.at(index).id);
                if (found == proxyNodes.constEnd()) {
                    continue;
                }
                const Models::Node node = found.value();
                const ResolvedAddresses addresses = resolutionFor(resolved, node.server, node.port);
                downloads.submit([&downloads, &networkLimit, &results, &measured, node, index,
                                  prefilterMs, options, addresses]() {
                    Permit permit(networkLimit);
                    const ProbeOutcome outcome = SpeedTester::probeFullResolved(
                        node.server, node.port, int(prefilterMs), options.downloadTestMs, true,
                        addresses);
                    std::optional<double> kbps = outcome.downloadKbps;
                    return [&results, &measured, index, kbps]() {
                        if (!kbps.has_value()) {
                            return;
                        }
                        results[index].downloadKbps = kbps;
                        ++measured;
                    };
                });
            }
            downloads.waitAll();
        }
        Log::info(QStringLiteral("%1 measured=%2").arg(Messages::downloadTopMeasuredLog())
                      .arg(qint64(measured)));
    }

    // ---- 真实代理验证 ----
    QVector<Models::Node> proxyCandidates;
    for (const Models::LatencyResult &result : results) {
        const auto found = proxyNodes.find(result.id);
        if (found == proxyNodes.end()) {
            continue;
        }
        const Models::Node node = found.value();
        proxyNodes.erase(found);
        if (result.ok || requiresProxyWithoutTcp(node.protocol)) {
            proxyCandidates.append(node);
        }
    }

    QVector<Database::SpeedUpdate> prefilterFailures;
    for (const Models::LatencyResult &result : results) {
        if (result.ok) {
            continue;
        }
        Database::SpeedUpdate update;
        update.id = result.id;
        update.ok = false;
        update.latencyMs = result.latencyMs;
        update.latencyKernel = QString();
        update.latencyTestKind = QStringLiteral("tcp_prefilter");
        prefilterFailures.append(update);
    }
    for (int offset = 0; offset < prefilterFailures.size(); offset += kDbChunk) {
        const Status written =
            db_->updateNodeSpeedsBatch(prefilterFailures.mid(offset, kDbChunk));
        if (written.isErr()) {
            return Result<Models::SpeedTestSummary>::withError(written.error());
        }
    }

    Log::info(QStringLiteral("starting real proxy verification candidates=%1")
                  .arg(qint64(proxyCandidates.size())));
    // 「常驻内核」= 一整轮只冷启动一个内核实例（大批次）；「条件隔离」见开关说明。
    const size_t proxyBatchSize =
        options.persistentKernel ? ProxyProbe::kPersistentMihomoBatchSize : kDefaultProxyBatchSize;
    const size_t proxyTotal = size_t(proxyCandidates.size());
    QVector<Models::Node> mihomoNodes;
    QVector<Models::Node> singBoxNodes;
    QVector<Models::Node> xrayNodes;
    QVector<ProxyProbe::ProbeResult> proxyResults;
    QHash<QString, Kernel::Kind> resultKernels;
    for (const Models::Node &node : proxyCandidates) {
        const Result<Kernel::Kind> selected = Kernel::selectProxyKernel(node.protocol);
        if (selected.isErr()) {
            ProxyProbe::ProbeResult rejected;
            rejected.id = node.id;
            rejected.ok = false;
            rejected.message = selected.error();
            proxyResults.append(rejected);
            continue;
        }
        resultKernels.insert(node.id, selected.value());
        switch (selected.value()) {
        case Kernel::Kind::Mihomo:
            mihomoNodes.append(node);
            break;
        case Kernel::Kind::SingBox:
            singBoxNodes.append(node);
            break;
        case Kernel::Kind::Xray:
            xrayNodes.append(node);
            break;
        }
    }
    const size_t rejectedDone = size_t(proxyResults.size());
    const size_t rejectedOk = countOk(proxyResults);

    if (options.parallelKernels) {
        // 三个内核的端口与工作目录本来就互相独立：并行可把墙钟压到约 1/3，代价是峰值
        // 内存约 3 倍，并且批次级进度只剩汇总（不再逐个内核上报）。
        // tokio::join! -> 专用线程池上的三个任务（不能用全局池：调用方自己可能就是全局池的线程）。
        struct Lane {
            Kernel::Kind kind;
            QVector<Models::Node> nodes;
        };
        QVector<Lane> lanes = {{Kernel::Kind::Mihomo, mihomoNodes},
                               {Kernel::Kind::SingBox, singBoxNodes},
                               {Kernel::Kind::Xray, xrayNodes}};
        QThreadPool kernelPool;
        kernelPool.setMaxThreadCount(3);
        QVector<QFuture<Result<QVector<ProxyProbe::ProbeResult>>>> futures;
        for (Lane &lane : lanes) {
            futures.append(QtConcurrent::run(
                &kernelPool,
                [&lane, &networkLimit, proxyMs, proxyBatchSize, &options]() {
                    return ProxyProbe::verifyNodesWithKernel(
                        lane.kind, lane.nodes, int(proxyMs), ProxyProbe::ProgressFn(), networkLimit,
                        proxyBatchSize, options.conditionalIsolation);
                }));
        }
        QVector<Result<QVector<ProxyProbe::ProbeResult>>> outcomes;
        for (QFuture<Result<QVector<ProxyProbe::ProbeResult>>> &future : futures) {
            future.waitForFinished();
            outcomes.append(future.result());
        }
        for (const Result<QVector<ProxyProbe::ProbeResult>> &outcome : outcomes) {
            if (outcome.isErr()) {
                return Result<Models::SpeedTestSummary>::withError(outcome.error());
            }
            proxyResults += outcome.value();
        }
    } else {
        const auto runLane = [this, &networkLimit, &progress, &started, proxyMs, proxyBatchSize,
                              options, proxyTotal](
                                 Kernel::Kind kind, const QVector<Models::Node> &laneNodes,
                                 size_t baseDone, size_t baseOk)
            -> Result<QVector<ProxyProbe::ProbeResult>> {
            ProxyProbe::ProgressFn laneProgress;
            if (progress) {
                laneProgress = [&progress, &started, baseDone, baseOk, proxyTotal, kind](
                                   const ProxyProbe::ProbeProgress &proxy) {
                    SpeedTestProgress state;
                    state.phase = SpeedTestPhase::RealProxy;
                    state.done = baseDone + proxy.done;
                    state.total = proxyTotal;
                    state.ok = baseOk + proxy.ok;
                    state.fail = saturatingSub(state.done, state.ok);
                    state.elapsedMs = started.elapsed();
                    state.kernel = kind;
                    state.batchIndex = proxy.batchIndex;
                    state.batchTotal = proxy.batchTotal;
                    progress(state);
                };
            }
            return ProxyProbe::verifyNodesWithKernel(kind, laneNodes, int(proxyMs), laneProgress,
                                                     networkLimit, proxyBatchSize,
                                                     options.conditionalIsolation);
        };

        Result<QVector<ProxyProbe::ProbeResult>> mihomoResults =
            runLane(Kernel::Kind::Mihomo, mihomoNodes, rejectedDone, rejectedOk);
        if (mihomoResults.isErr()) {
            return Result<Models::SpeedTestSummary>::withError(mihomoResults.error());
        }
        const size_t mihomoDone = rejectedDone + size_t(mihomoResults.value().size());
        const size_t mihomoOk = rejectedOk + countOk(mihomoResults.value());
        proxyResults += mihomoResults.value();

        Result<QVector<ProxyProbe::ProbeResult>> singBoxResults =
            runLane(Kernel::Kind::SingBox, singBoxNodes, mihomoDone, mihomoOk);
        if (singBoxResults.isErr()) {
            return Result<Models::SpeedTestSummary>::withError(singBoxResults.error());
        }
        proxyResults += singBoxResults.value();

        const size_t beforeXrayDone = size_t(proxyResults.size());
        const size_t beforeXrayOk = countOk(proxyResults);
        Result<QVector<ProxyProbe::ProbeResult>> xrayResults =
            runLane(Kernel::Kind::Xray, xrayNodes, beforeXrayDone, beforeXrayOk);
        if (xrayResults.isErr()) {
            return Result<Models::SpeedTestSummary>::withError(xrayResults.error());
        }
        proxyResults += xrayResults.value();
    }

    if (progress) {
        SpeedTestProgress state;
        state.phase = SpeedTestPhase::RealProxy;
        state.done = size_t(proxyResults.size());
        state.total = proxyTotal;
        state.ok = countOk(proxyResults);
        state.fail = saturatingSub(state.done, state.ok);
        state.elapsedMs = started.elapsed();
        progress(state);
    }

    QHash<QString, ProxyProbe::ProbeResult> proxyById;
    proxyById.reserve(proxyResults.size() * 2);
    for (const ProxyProbe::ProbeResult &result : proxyResults) {
        proxyById.insert(result.id, result);
    }
    for (Models::LatencyResult &result : results) {
        const auto found = proxyById.find(result.id);
        if (found == proxyById.end()) {
            continue;
        }
        const ProxyProbe::ProbeResult proxy = found.value();
        proxyById.erase(found);
        result.ok = proxy.ok;
        result.latencyMs = proxy.latencyMs;
        result.downloadKbps = std::nullopt;
        const auto kernel = resultKernels.constFind(result.id);
        if (kernel != resultKernels.end()) {
            result.latencyKernel = Kernel::kindToText(*kernel);
            result.latencyTestKind = QString::fromLatin1(Models::kGoogleProxyTestKind);
        }
        result.message = proxy.message;
    }

    QVector<Database::SpeedUpdate> updates;
    updates.reserve(results.size());
    for (const Models::LatencyResult &result : results) {
        Database::SpeedUpdate update;
        update.id = result.id;
        update.ok = result.ok;
        update.latencyMs = result.latencyMs;
        update.downloadKbps = result.downloadKbps;
        update.latencyKernel = result.latencyKernel;
        update.latencyTestKind = result.latencyTestKind;
        updates.append(update);
    }
    for (int offset = 0; offset < updates.size(); offset += kDbChunk) {
        const Status written = db_->updateNodeSpeedsBatch(updates.mid(offset, kDbChunk));
        if (written.isErr()) {
            return Result<Models::SpeedTestSummary>::withError(written.error());
        }
    }

    Models::SpeedTestSummary summary;
    summary.total = results.size();
    for (const Models::LatencyResult &result : results) {
        if (result.ok) {
            ++summary.ok;
        }
    }
    summary.fail = saturatingCount(qint64(summary.total), qint64(summary.ok));
    {
        qint64 latencySum = 0;
        int latencyCount = 0;
        double downloadSum = 0.0;
        int downloadCount = 0;
        for (const Models::LatencyResult &result : results) {
            if (result.latencyMs.has_value()) {
                latencySum += *result.latencyMs;
                ++latencyCount;
            }
            if (result.downloadKbps.has_value()) {
                downloadSum += *result.downloadKbps;
                ++downloadCount;
            }
        }
        if (latencyCount > 0) {
            summary.avgMs = double(latencySum) / double(latencyCount);
        }
        if (downloadCount > 0) {
            summary.avgDownloadKbps = downloadSum / double(downloadCount);
        }
    }

    std::stable_sort(results.begin(), results.end(),
                     [](const Models::LatencyResult &a, const Models::LatencyResult &b) {
                         if (a.ok && b.ok) {
                             if (a.latencyMs.has_value() && b.latencyMs.has_value()) {
                                 return *a.latencyMs < *b.latencyMs;
                             }
                             if (a.latencyMs.has_value()) {
                                 return true;
                             }
                             if (b.latencyMs.has_value()) {
                                 return false;
                             }
                             return TextOrder::less(a.name, b.name);
                         }
                         if (a.ok) {
                             return true;
                         }
                         if (b.ok) {
                             return false;
                         }
                         return TextOrder::less(a.name, b.name);
                     });
    summary.results = results;

    Log::info(QStringLiteral(
                  "speedtest finished ok=%1 fail=%2 avg_ms=%3 avg_download_kbps=%4 elapsed_ms=%5")
                  .arg(qint64(summary.ok))
                  .arg(qint64(summary.fail))
                  .arg(summary.avgMs.has_value() ? QStringLiteral("Some(%1)").arg(*summary.avgMs)
                                                 : QStringLiteral("None"))
                  .arg(summary.avgDownloadKbps.has_value()
                           ? QStringLiteral("Some(%1)").arg(*summary.avgDownloadKbps)
                           : QStringLiteral("None"))
                  .arg(started.elapsed()));

    return Result<Models::SpeedTestSummary>::withValue(std::move(summary));
}

}
