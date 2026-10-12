#pragma once

#include <QMutex>
#include <QProcess>
#include <QString>

#include <memory>
#include <optional>

#include "core/Result.h"
#include "kernel/KernelTypes.h"
#include "models/Node.h"
#include "proxy/MihomoConfig.h"
#include "proxy/WorkDir.h"
#include "sysproxy/SystemProxy.h"

namespace Proxy {

/// 启动阶段的时间预算（与 Rust 侧同一组常量）。
inline constexpr int kStartupTotalTimeoutMs = 5000;
inline constexpr int kStartupVerifyTimeoutMs = 1500;
inline constexpr int kStartupSystemProxyReserveMs = 250;
inline constexpr int kStartupPortConnectTimeoutMs = 60;
inline constexpr bool kVerifyConnectivityOnStart = true;
/// 单次验证尝试的时间片（Rust: remaining.min(900ms)，整轮预算不变）。
inline constexpr int kVerifyAttemptTimeoutMs = 900;
inline constexpr int kVerifyAttemptConnectTimeoutMs = 600;
inline constexpr int kVerifyAttemptRetrySleepMs = 40;

/// 经本地 mixed 端口真取一次 generate_204：只有 204 才算通。Rust 侧是私有的
/// `verify_local_proxy`；这里公开是因为 C++ 没有 cfg(test)，而 API/GUI 也要单探一次。
Status verifyLocalProxy(quint16 port, int timeoutMs);
/// 同一套判定，但测试地址由调用方给：启动时用的是 Google 的 generate_204，测试里要能
/// 拿一个"回 200 的假代理"和一个"回 204 的假代理"分别验两侧（HTTPS 地址做不到这点 ——
/// CONNECT 回 200 是隧道建好，不是站点响应码）。
Status verifyLocalProxyAgainst(quint16 port, const QString &url, int timeoutMs);

/// 启动前确认一个端口空闲。第二次重试会**清残留代理内核进程**，
/// 所以只许在启动路径上调用，别拿它当普通探测用。
Status ensurePortAvailable(quint16 port);

/// 各内核的批次上限：mihomo 一批 256，sing-box/xray 一批 64。
int kernelBatchSize(Kernel::Kind kind);
/// 就绪判定用的端口（mihomo 看控制器端口，其它看 mixed 端口）。
quint16 kernelReadinessPort(Kernel::Kind kind, const MihomoConfigOptions &options);
/// 启动前必须能占住的端口集合。
QVector<quint16> kernelRequiredPorts(Kernel::Kind kind, const MihomoConfigOptions &options);
/// 生成 + 序列化为落盘文本。
Result<Json> buildKernelConfig(Kernel::Kind kind, const Models::Node &node,
                              MihomoConfigOptions options);
Result<QString> serializeKernelConfig(Kernel::Kind kind, const Json &config);

/// 常驻代理的运行编排。对应 Rust 的 `ProxyManager`。
///
/// 所有方法都是阻塞的：只能在 worker 线程调用（GUI/API 通过 QtConcurrent 派发）。
/// runtime 锁与 operation 锁都是普通 QMutex（不可重入），所以内部函数一律接
/// `Runtime &`，绝不在持锁时再进公开方法。
/// 一次出口实测的结果。`error` 只在 ok=false 时有值，且带真实原因（HTTP 状态码或
/// 传输错误），它会被写进 egress_health.error 并显示在状态卡那行「实测 …」上。
struct HealthProbeResult {
    bool ok = false;
    quint64 latencyMs = 0;
    QString error;
};

class ProxyManager {
public:
    explicit ProxyManager(MihomoConfigOptions options);

    ProxyRuntimeStatus status();
    MihomoConfigOptions config();
    /// 停止或失败状态才允许改配置；返回后新配置已生效于内存，等待下次启动。
    Status updateConfig(const MihomoConfigOptions &options);
    /// 运行中且是 mihomo：写配置 + PUT /configs 热重载。返回是否真的热重载成功。
    Result<bool> reloadConfig(const MihomoConfigOptions &options);
    /// 注入 url-test 候选（不持久化），返回真正能转成 clash proxy 的条数。
    int setUrlTestCandidates(const QVector<Models::Node> &nodes);

    Status start(const Models::Node &node);
    /// 统一的"连接/切换"入口：在跑就重启，没在跑就启动。
    Status connect(const Models::Node &node,
                   std::optional<Kernel::Preference> kernelOverride = std::nullopt);
    Status stop();
    /// 换节点：先试 mihomo 热重载平滑切换，不行再重启进程。
    Status switchNode(const Models::Node &node);

    Result<QString> recentLog(int maxChars);
    /// 自动切换用的轻量健康探测：经正在运行的 mixed 端口取一次基准站点。
    /// `url` 空 = 内置的 Google generate_204；设置里可以换成自己的基准站点。
    /// 判定是**必须 2xx**：403/404 这类"到得了但被拒"也算故障（用户 2026-10-12 点名），
    /// 所以失败原因必须带回来 —— 否则状态卡只能说一句"不通"，查不到是被谁拒的。
    /// `urls` 是基准站点列表：**任一拿到 2xx 就算通**，全部失败才记故障（要求全部可达的
    /// 话，列表里有一个被对方 WAF 拒的站就会让这一路永远判故障）。空列表 = 内置那一条。
    HealthProbeResult probeHealth(int timeoutMs, const QStringList &urls = QStringList());
    /// 内核进程在跑但外部验证失败时是 Degraded，这时控制器端口仍然可用。
    bool isRunning() const;

private:
    struct Runtime {
        ProxyRuntimeStatus status;
        MihomoConfigOptions options;
        std::unique_ptr<QProcess> child;
        std::optional<ProxyWorkDir> workDir;
        std::optional<SysProxy::Snapshot> systemProxySnapshot;
        std::optional<Models::Node> currentNode;

        /// 界面传来的配置里一次性密钥与候选都是空的，空值沿用当前运行时的。
        void applyOptions(MihomoConfigOptions incoming);
    };

    void markFailed(Runtime &runtime, const QString &message);
    void setFailed(const QString &message);
  public:
    /// 指定内核与可执行文件路径启动。对应 Rust 的 `start_with_kernel_path`：
    /// 提权交接与测试都要这条路，Rust 用 pub(crate)，C++ 只能公开。
    Status startWithKernelPath(const Models::Node &node, Kernel::Kind kernel,
                               const QString &executablePath);

  private:
    Status startWithPreference(const Models::Node &node,
                               std::optional<SysProxy::Snapshot> preservedSnapshot,
                               std::optional<Kernel::Preference> kernelOverride);
    Status startWithPath(const Models::Node &node, Kernel::Kind kernel,
                         const QString &executablePath,
                         std::optional<SysProxy::Snapshot> preservedSnapshot);
    Result<std::optional<SysProxy::Snapshot>> stopWithSystemProxyRestore(bool restoreProxy);
    Status restart(const Models::Node &node, std::optional<Kernel::Preference> kernelOverride);
    Result<bool> tryHotReloadMihomo(const Models::Node &node);
    /// 热重载用的公共部分：返回是否成功 PUT。
    Result<bool> putConfigs(const QString &configPath, const QString &configText,
                           quint16 controllerPort, const QString &controllerSecret,
                           QString *failure);

    mutable QMutex runtimeMutex_;
    QMutex operationMutex_;
    Runtime runtime_;
};

}
