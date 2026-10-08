#pragma once

#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

#include "core/Result.h"
#include "kernel/KernelRelease.h"
#include "kernel/KernelSource.h"
#include "kernel/KernelTypes.h"
#include "models/Settings.h"
#include "models/Source.h"
#include "models/Types.h"
#include "proxy/MihomoConfig.h"

class Database;

namespace Tasks {
class TaskTracker;
}

// src/api/mod.rs 的 AppState 是一堆 `Clone` 的具体类型（Database / Collector /
// SpeedTester / TaskTracker / KernelManager / ProxyManager）。C++ 侧这几个层里
// collector、speedtest、updater 还在并行移植中，所以 API 只认下面这几个接口：
// 已落地的层由 DefaultGateways.* 提供包装，没落地的由父工程在装配时注入实现。
//
// 【所有方法都是阻塞的】只能在 worker 线程调用（GUI 线程一律经 QtConcurrent 派发），
// 这与 Rust 侧 handler 全在 tokio 上跑、这里换成"每个请求一个 worker 任务"对应。
namespace Api {

/// 对应 Rust 的 `SpeedTestOptions`（speedtest/mod.rs:57）。API 两个 handler 都把
/// intensity 固定成 Standard，所以这里不带这个字段；实现方（speedtest 层）按 Standard
/// 的语义执行即可。
struct SpeedtestOptions {
    quint32 concurrency = 128;
    quint32 timeoutMs = 1500;
    bool measureDownload = false;
    quint32 downloadTestMs = 800;
    bool parallelKernels = false;
    bool persistentKernel = false;
    quint32 reuseVerifiedMinutes = 0;
    bool conditionalIsolation = false;
    quint32 downloadTopN = 0;

    /// 对应 Rust speedtest_one 里从设置拼 options、concurrency 固定 1 的那一段。
    static SpeedtestOptions forSingleTest(const Models::AppSettings &settings);
    /// 对应 Rust speedtest_all 里 concurrency 取设置值的那一段。
    static SpeedtestOptions forBatchTest(const Models::AppSettings &settings);
};

/// collector/mod.rs 的 Collector。取不到实现时 handler 回 500（Rust 不可能为空）。
class CollectorGateway
{
public:
    virtual ~CollectorGateway() = default;

    virtual Result<Models::FetchResult> fetchOne(const QString &sourceId) = 0;
    virtual Result<QVector<Models::FetchResult>> fetchAllEnabled() = 0;
    /// 对应 `tokio::spawn(collector.discover_gists(..))`：立刻返回，失败只写日志。
    virtual void discoverGistsInBackground(const QString &query, int maxPages) = 0;
};

/// speedtest/mod.rs 的 SpeedTester。
class SpeedtestGateway
{
public:
    virtual ~SpeedtestGateway() = default;

    virtual Result<Models::LatencyResult> testOne(const QString &nodeId,
                                                  const SpeedtestOptions &options) = 0;
    /// nullopt / 空列表 = 测全部（Rust 的 `Option<Vec<String>>`）。
    virtual Result<Models::SpeedTestSummary> testMany(
        const std::optional<QVector<QString>> &nodeIds, const SpeedtestOptions &options) = 0;
};

/// kernel/mod.rs 的 KernelManager：detect_all 已落地（vccKernelManager），
/// 联网查 Release 与下载安装在 updater 层（还没落地），所以整体走接口。
class KernelGateway
{
public:
    virtual ~KernelGateway() = default;

    /// 顺序固定 mihomo → xray → sing-box（Rust detect_all）。
    virtual QList<Kernel::Info> detectAll() = 0;
    virtual Result<QList<Kernel::ReleaseInfo>> checkUpdatesWithProxy(
        const std::optional<QString> &proxyUrl) = 0;
    /// 对应 Rust `update_with_options(kind, proxy_url, source, None, cancelled=false, |_| {})`：
    /// 没有本地文件、没有取消标志、不回报进度。
    virtual Result<Kernel::UpdateResult> updateWithOptions(
        Kernel::Kind kind, const std::optional<QString> &proxyUrl, Kernel::Source source) = 0;
};

/// proxy/mod.rs 的 ProxyManager。
class ProxyGateway
{
public:
    virtual ~ProxyGateway() = default;

    virtual Proxy::ProxyRuntimeStatus status() = 0;
    virtual Proxy::MihomoConfigOptions config() = 0;
    virtual Status updateConfig(const Proxy::MihomoConfigOptions &options) = 0;
    virtual Result<QString> recentLog(int maxChars) = 0;
    virtual Status connect(const Models::Node &node,
                           std::optional<Kernel::Preference> kernel) = 0;
    virtual Status stop() = 0;
};

/// 对应 Rust 的 `AppState`（Clone：所有成员共享同一份状态）。
struct AppState {
    std::shared_ptr<Database> db;
    std::shared_ptr<CollectorGateway> collector;
    std::shared_ptr<SpeedtestGateway> speedtest;
    std::shared_ptr<Tasks::TaskTracker> tasks;
    std::shared_ptr<KernelGateway> kernel;
    std::shared_ptr<ProxyGateway> proxy;
};

/// 把 Rust 里 `AppState::proxy_manager.status().local_proxy_active()` 那一段
/// 换成 http://127.0.0.1:{mixed_port} 的代理 URL（kernel_releases / update_kernel 用）。
std::optional<QString> localProxyUrl(const Proxy::ProxyRuntimeStatus &status);

} // namespace Api
