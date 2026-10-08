#pragma once

#include <memory>

#include "api/ApiDepends.h"

namespace Collector {
class Collector;
}

namespace Proxy {
class ProxyManager;
}

namespace Api {

/// 已落地层的默认实现。父工程（app 层）拿这些把 AppState 填满：
///
///     state.proxy = makeProxyGateway(proxyManager);       // vccProxy 已有
///     state.kernel = makeKernelGateway();                 // detect_all 已有
///     state.collector = makeCollectorGateway(collector);  // vccCollector 已有
///     state.speedtest = ...;                              // speedtest 层落地后自行注入
///     state.tasks = taskTracker;                          // vccTasks 已有
///
/// 没注入的网关，对应 handler 回 500 "… is not wired"（Rust 侧这些字段不可能是空）。
std::shared_ptr<ProxyGateway> makeProxyGateway(std::shared_ptr<Proxy::ProxyManager> manager);
std::shared_ptr<CollectorGateway> makeCollectorGateway(
    std::shared_ptr<Collector::Collector> collector);

/// KernelGateway 的默认实现。detectAll 走 vccKernelManager；
/// checkUpdatesWithProxy / updateWithOptions 属于还没落地的 updater 层，
/// 在它们注入之前这两个方法返回错误（handler 因此回 500，而不是假装成功）。
class DefaultKernelGateway : public KernelGateway {
public:
    /// 注入真实的 Release 检查 / 更新执行体（updater 层落地后由父工程接线）。
    using ReleaseCheck = std::function<Result<QList<Kernel::ReleaseInfo>>(
        const std::optional<QString> &proxyUrl)>;
    using UpdateRun = std::function<Result<Kernel::UpdateResult>(
        Kernel::Kind kind, const std::optional<QString> &proxyUrl, Kernel::Source source)>;

    DefaultKernelGateway();

    void setReleaseCheck(ReleaseCheck check);
    void setUpdateRun(UpdateRun run);

    QList<Kernel::Info> detectAll() override;
    Result<QList<Kernel::ReleaseInfo>> checkUpdatesWithProxy(
        const std::optional<QString> &proxyUrl) override;
    Result<Kernel::UpdateResult> updateWithOptions(
        Kernel::Kind kind, const std::optional<QString> &proxyUrl, Kernel::Source source) override;

private:
    ReleaseCheck releaseCheck_;
    UpdateRun updateRun_;
};

std::shared_ptr<KernelGateway> makeKernelGateway();

} // namespace Api
