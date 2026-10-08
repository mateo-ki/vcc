#include "api/ApiGateways.h"

#include <QtConcurrent>

#include "collector/Collector.h"
#include "kernel/KernelManager.h"
#include "proxy/ProxyManager.h"

namespace Api {

namespace {

class ProxyGatewayAdapter : public ProxyGateway {
public:
    explicit ProxyGatewayAdapter(std::shared_ptr<Proxy::ProxyManager> manager)
        : manager_(std::move(manager))
    {
    }

    Proxy::ProxyRuntimeStatus status() override { return manager_->status(); }
    Proxy::MihomoConfigOptions config() override { return manager_->config(); }
    Status updateConfig(const Proxy::MihomoConfigOptions &options) override
    {
        return manager_->updateConfig(options);
    }
    Result<QString> recentLog(int maxChars) override { return manager_->recentLog(maxChars); }
    Status connect(const Models::Node &node, std::optional<Kernel::Preference> kernel) override
    {
        return manager_->connect(node, kernel);
    }
    Status stop() override { return manager_->stop(); }

private:
    std::shared_ptr<Proxy::ProxyManager> manager_;
};

class CollectorGatewayAdapter : public CollectorGateway {
public:
    explicit CollectorGatewayAdapter(std::shared_ptr<Collector::Collector> collector)
        : collector_(std::move(collector))
    {
    }

    Result<Models::FetchResult> fetchOne(const QString &sourceId) override
    {
        return collector_->fetchOne(sourceId);
    }

    Result<QVector<Models::FetchResult>> fetchAllEnabled() override
    {
        return collector_->fetchAllEnabled();
    }

    /// 对应 `tokio::spawn`：立刻返回，失败只写日志。
    void discoverGistsInBackground(const QString &query, int maxPages) override
    {
        Collector::Collector collector = *collector_; // 拷贝共享同一份状态（Rust 的 Clone）
        QtConcurrent::run([collector, query, maxPages]() mutable {
            const Result<Collector::GistDiscoverySummary> result =
                collector.discoverGists(query, maxPages);
            if (result.isErr()) {
                qWarning("gist discovery failed: %s", qUtf8Printable(result.error()));
            }
        });
    }

private:
    std::shared_ptr<Collector::Collector> collector_;
};

} // namespace

std::shared_ptr<ProxyGateway> makeProxyGateway(std::shared_ptr<Proxy::ProxyManager> manager)
{
    return std::make_shared<ProxyGatewayAdapter>(std::move(manager));
}

std::shared_ptr<CollectorGateway> makeCollectorGateway(
    std::shared_ptr<Collector::Collector> collector)
{
    return std::make_shared<CollectorGatewayAdapter>(std::move(collector));
}

DefaultKernelGateway::DefaultKernelGateway() = default;

void DefaultKernelGateway::setReleaseCheck(ReleaseCheck check)
{
    releaseCheck_ = std::move(check);
}

void DefaultKernelGateway::setUpdateRun(UpdateRun run)
{
    updateRun_ = std::move(run);
}

QList<Kernel::Info> DefaultKernelGateway::detectAll()
{
    Kernel::Manager manager;
    return manager.detectAll();
}

Result<QList<Kernel::ReleaseInfo>> DefaultKernelGateway::checkUpdatesWithProxy(
    const std::optional<QString> &proxyUrl)
{
    if (!releaseCheck_) {
        return Result<QList<Kernel::ReleaseInfo>>::withError(
            QStringLiteral("kernel updater is not wired"));
    }
    return releaseCheck_(proxyUrl);
}

Result<Kernel::UpdateResult> DefaultKernelGateway::updateWithOptions(
    Kernel::Kind kind, const std::optional<QString> &proxyUrl, Kernel::Source source)
{
    if (!updateRun_) {
        return Result<Kernel::UpdateResult>::withError(QStringLiteral("kernel updater is not wired"));
    }
    return updateRun_(kind, proxyUrl, source);
}

std::shared_ptr<KernelGateway> makeKernelGateway()
{
    return std::make_shared<DefaultKernelGateway>();
}

} // namespace Api
