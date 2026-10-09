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

/// 经本地 mixed 端口真取一次 generate_204：只有 204 才算通。Rust 侧是私有的
/// `verify_local_proxy`；这里公开是因为 C++ 没有 cfg(test)，而 API/GUI 也要单探一次。
Status verifyLocalProxy(quint16 port, int timeoutMs);

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
    /// 自动切换用的轻量健康探测：经正在运行的 mixed 端口取一次 generate_204。
    std::pair<bool, quint64> probeHealth(int timeoutMs);
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
