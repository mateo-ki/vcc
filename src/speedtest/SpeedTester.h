#pragma once

#include <QString>
#include <QVector>

#include <memory>
#include <optional>

#include "core/Result.h"
#include "models/Node.h"
#include "models/Types.h"
#include "speedtest/SpeedtestTypes.h"
#include "tasks/TaskTracker.h"

class Database;
class QSemaphore;

/// src/speedtest/mod.rs 的移植：TCP 连通性预筛 + 真实内核代理握手，再写回分库。
///
/// 【全部入口都是阻塞的】内部用专用线程池并发跑探测，调用方（GUI / HTTP）得自己在
/// worker 线程上进来（Rust 那边是 async fn，同样的位置是 tokio 任务）。
/// 类可复制：`std::shared_ptr<Database>` + 共享的 TaskTracker，对应 Rust 的 `#[derive(Clone)]`。
namespace Speedtest {

/// 对应 Rust 的 `struct SpeedTester`（:51）。
class SpeedTester {
public:
    SpeedTester(std::shared_ptr<Database> db, Tasks::TaskTracker tasks = Tasks::TaskTracker());

    /// 延迟 + 可选下载吞吐，对应 `probe_full`（:125）。
    ///
    /// 这里测的是 **TCP 可达性**，不是真代理握手。免费列表里一堆死/诱饵端口照样开 TCP，
    /// 所以 VMess/VLESS/Trojan/SS 握手失败时，v2rayN 那类客户端仍然会显示延迟 -1。
    static ProbeOutcome probeFull(const QString &host, int port, int timeoutMs,
                                  bool measureDownload, int downloadTestMs);

    /// 对应 `probe_full_resolved`（:175）：地址已由调用方解析好，批量扫描共用一份 DNS。
    static ProbeOutcome probeFullResolved(const QString &host, int port, int timeoutMs,
                                          int downloadTestMs, bool measureDownload,
                                          const ResolvedAddresses &addresses);

    /// 对应 `test_one`（:220）。
    Result<Models::LatencyResult> testOne(const QString &nodeId, const SpeedTestOptions &options);

    /// 对应 `test_many`（:229）：TCP 预筛 → 真代理握手 → 经内核的 HTTP 延迟。
    Result<Models::SpeedTestSummary> testMany(const std::optional<QVector<QString>> &nodeIds,
                                              const SpeedTestOptions &options);

    /// 对应 `test_all_sources`（:239）：所有订阅库走一个去重后的全局工作队列。
    Result<Models::SpeedTestSummary> testAllSources(const SpeedTestOptions &options);

    /// 对应 `test_all_sources_with_progress`（:244）。
    Result<Models::SpeedTestSummary> testAllSourcesWithProgress(const SpeedTestOptions &options,
                                                                const CountProgressFn &progress);

    /// 对应 `test_many_with_progress`（:420）。
    Result<Models::SpeedTestSummary> testManyWithProgress(
        const std::optional<QVector<QString>> &nodeIds, const SpeedTestOptions &options,
        const ProgressFn &progress);

private:
    /// 对应 `test_many_with_progress_limited`（:431）。
    Result<Models::SpeedTestSummary> testManyWithProgressLimited(
        const std::optional<QVector<QString>> &nodeIds, const SpeedTestOptions &options,
        const ProgressFn &progress, QSemaphore &networkLimit);

    /// 对应 `run_speedtest`（:449）。
    Result<Models::SpeedTestSummary> runSpeedtest(const QVector<Models::Node> &nodes,
                                                  const SpeedTestOptions &options,
                                                  const ProgressFn &progress,
                                                  QSemaphore &networkLimit);

    /// 对应 `test_all_sources_globally`（:256）。
    Result<Models::SpeedTestSummary> testAllSourcesGlobally(const SpeedTestOptions &options,
                                                            const CountProgressFn &progress);

    std::shared_ptr<Database> db_;
    Tasks::TaskTracker tasks_;
};

/// UDP 类协议（hysteria/tuic/wireguard…）没有 TCP 可预筛：TCP 失败也不能把它剔掉。
/// 对应 Rust 的 `requires_proxy_without_tcp`（:1079）。
bool requiresProxyWithoutTcp(const QString &protocol);

/// 对应 Rust 的 `build_speedtest_summary`（:1029）：空结果集是错误（消息与 Rust 一致）。
Result<Models::SpeedTestSummary> buildSpeedtestSummary(const QVector<Models::LatencyResult> &results);

/// 对应 Rust 的 `report_speedtest_progress`（:1056）。
void reportSpeedtestProgress(const ProgressFn &progress, SpeedTestPhase phase, size_t done,
                             size_t total, size_t ok, qint64 elapsedMs);

}
