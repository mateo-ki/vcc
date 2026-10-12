#include "core/JsonUtil.h"
#include "core/TimeUtil.h"
#include "db/Database.h"

Result<Models::AppSettings> Database::getSettings()
{
    static const char *kSelect = R"(SELECT auto_fetch_enabled, auto_fetch_interval_min, auto_speedtest_enabled,
                       fetch_concurrency, fetch_timeout_secs,
                       speedtest_concurrency, speedtest_timeout_ms,
                       speedtest_download, download_test_ms,
                       http_server_enabled, close_to_tray,
                       exclude_keywords, failover_latency_ms,
                       speedtest_parallel_kernels, speedtest_persistent_kernel,
                       speedtest_reuse_minutes, speedtest_conditional_isolation,
                       speedtest_download_top_n, bandwidth_guard, bandwidth_min_kbps,
                       publisher_api, publisher_token, ui_theme, kernel_source, firewall_auto_allow,
                       auto_failover, speedtest_auto_refill, failover_probe_urls,
                       auto_prepare_kernel, speedtest_added_source,
                       last_auto_fetch_at, last_auto_speedtest_at
                FROM settings WHERE id = 1)";

    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(QString::fromLatin1(kSelect));
    if (prepared.isErr()) {
        return Result<Models::AppSettings>::withError(prepared.error());
    }
    Statement row = prepared.takeValue();
    const Result<bool> stepped = row.step();
    if (stepped.isErr()) {
        return Result<Models::AppSettings>::withError(stepped.error());
    }
    if (!stepped.value()) {
        return Result<Models::AppSettings>::withValue(Models::AppSettings());
    }

    Models::AppSettings settings;
    settings.autoFetchEnabled = row.flag("auto_fetch_enabled");
    settings.autoFetchIntervalMin = int(row.int64("auto_fetch_interval_min"));
    settings.autoSpeedtestEnabled = row.flag("auto_speedtest_enabled");
    settings.fetchConcurrency =
        std::clamp<int>(int(row.isNull("fetch_concurrency") ? 4 : row.int64("fetch_concurrency")),
                        1, 8);
    settings.fetchTimeoutSecs =
        int(row.isNull("fetch_timeout_secs") ? 30 : row.int64("fetch_timeout_secs"));
    settings.speedtestConcurrency = int(row.int64("speedtest_concurrency"));
    settings.speedtestTimeoutMs = int(row.int64("speedtest_timeout_ms"));
    settings.speedtestDownload = row.flag("speedtest_download", false);
    settings.downloadTestMs = int(row.isNull("download_test_ms") ? 800 : row.int64("download_test_ms"));
    settings.httpServerEnabled = row.flag("http_server_enabled", true);
    settings.closeToTray = row.flag("close_to_tray", true);
    settings.excludeKeywords = row.text("exclude_keywords");
    if (settings.excludeKeywords.isNull()) {
        settings.excludeKeywords = QString::fromUtf8("");
    }
    settings.failoverLatencyMs =
        int(row.isNull("failover_latency_ms") ? 1500 : row.int64("failover_latency_ms"));
    settings.speedtestParallelKernels = row.flag("speedtest_parallel_kernels", false);
    settings.speedtestPersistentKernel = row.flag("speedtest_persistent_kernel", false);
    settings.speedtestReuseMinutes =
        int(row.isNull("speedtest_reuse_minutes") ? 0 : row.int64("speedtest_reuse_minutes"));
    settings.speedtestConditionalIsolation = row.flag("speedtest_conditional_isolation", false);
    settings.speedtestDownloadTopN =
        int(row.isNull("speedtest_download_top_n") ? 0 : row.int64("speedtest_download_top_n"));
    settings.bandwidthGuard = row.flag("bandwidth_guard", false);
    settings.bandwidthMinKbps =
        int(row.isNull("bandwidth_min_kbps") ? 0 : row.int64("bandwidth_min_kbps"));
    settings.publisherApi = row.text("publisher_api");
    if (settings.publisherApi.isNull()) {
        settings.publisherApi = QString::fromUtf8("");
    }
    settings.publisherToken = row.text("publisher_token");
    if (settings.publisherToken.isNull()) {
        settings.publisherToken = QString::fromUtf8("");
    }
    settings.uiTheme = row.text("ui_theme").isNull()
        ? QString::fromLatin1(Models::kDefaultUiTheme)
        : row.text("ui_theme");
    settings.kernelSource = row.text("kernel_source").isNull()
        ? QString::fromLatin1(Models::kDefaultKernelSource)
        : row.text("kernel_source");
    settings.firewallAutoAllow = row.flag("firewall_auto_allow", false);
    settings.autoFailover = row.flag("auto_failover", false);
    settings.speedtestAutoRefill = row.flag("speedtest_auto_refill", false);
    settings.failoverProbeUrls = row.text("failover_probe_urls");
    // 这两条缺列时按"开"读：它们是默认开的行为开关，不能因为老库没这列就变成关。
    settings.autoPrepareKernel = row.flag("auto_prepare_kernel", true);
    settings.speedtestAddedSource = row.flag("speedtest_added_source", true);
    settings.lastAutoFetchAt = TimeUtil::OptionalUtc::fromText(row.text("last_auto_fetch_at"));
    settings.lastAutoSpeedtestAt =
        TimeUtil::OptionalUtc::fromText(row.text("last_auto_speedtest_at"));
    return Result<Models::AppSettings>::withValue(settings);
}

Result<Models::AppSettings> Database::updateSettings(const Models::UpdateSettings &patch)
{
    Result<Models::AppSettings> loaded = getSettings();
    if (loaded.isErr()) {
        return loaded;
    }
    Models::AppSettings settings = loaded.takeValue();

    if (patch.autoFetchEnabled) {
        settings.autoFetchEnabled = *patch.autoFetchEnabled;
    }
    if (patch.autoFetchIntervalMin) {
        settings.autoFetchIntervalMin =
            std::clamp<int64_t>(*patch.autoFetchIntervalMin, 5, 24 * 60);
    }
    if (patch.autoSpeedtestEnabled) {
        settings.autoSpeedtestEnabled = *patch.autoSpeedtestEnabled;
    }
    if (patch.fetchConcurrency) {
        settings.fetchConcurrency = std::clamp<int64_t>(*patch.fetchConcurrency, 1, 8);
    }
    if (patch.fetchTimeoutSecs) {
        settings.fetchTimeoutSecs = std::clamp<int64_t>(*patch.fetchTimeoutSecs, 10, 120);
    }
    if (patch.speedtestConcurrency) {
        settings.speedtestConcurrency = std::clamp<int64_t>(*patch.speedtestConcurrency, 1, 512);
    }
    if (patch.speedtestTimeoutMs) {
        settings.speedtestTimeoutMs = std::clamp<int64_t>(*patch.speedtestTimeoutMs, 200, 15000);
    }
    if (patch.speedtestDownload) {
        settings.speedtestDownload = *patch.speedtestDownload;
    }
    if (patch.downloadTestMs) {
        settings.downloadTestMs = std::clamp<int64_t>(*patch.downloadTestMs, 200, 5000);
    }
    if (patch.httpServerEnabled) {
        settings.httpServerEnabled = *patch.httpServerEnabled;
    }
    if (patch.closeToTray) {
        settings.closeToTray = *patch.closeToTray;
    }
    if (patch.excludeKeywords) {
        settings.excludeKeywords = QString(*patch.excludeKeywords).left(400);
    }
    if (patch.failoverLatencyMs) {
        settings.failoverLatencyMs = std::clamp<int64_t>(*patch.failoverLatencyMs, 200, 15000);
    }
    if (patch.speedtestParallelKernels) {
        settings.speedtestParallelKernels = *patch.speedtestParallelKernels;
    }
    if (patch.speedtestPersistentKernel) {
        settings.speedtestPersistentKernel = *patch.speedtestPersistentKernel;
    }
    if (patch.speedtestReuseMinutes) {
        settings.speedtestReuseMinutes = std::min<int64_t>(*patch.speedtestReuseMinutes, 1440);
    }
    if (patch.speedtestConditionalIsolation) {
        settings.speedtestConditionalIsolation = *patch.speedtestConditionalIsolation;
    }
    if (patch.speedtestDownloadTopN) {
        settings.speedtestDownloadTopN = std::min<int64_t>(*patch.speedtestDownloadTopN, 5000);
    }
    if (patch.bandwidthGuard) {
        settings.bandwidthGuard = *patch.bandwidthGuard;
    }
    if (patch.bandwidthMinKbps) {
        settings.bandwidthMinKbps = std::min<int64_t>(*patch.bandwidthMinKbps, 100000);
    }
    if (patch.publisherApi) {
        settings.publisherApi = patch.publisherApi->trimmed();
    }
    if (patch.publisherToken) {
        settings.publisherToken = patch.publisherToken->trimmed();
    }
    if (patch.uiTheme) {
        const QString theme = patch.uiTheme->trimmed();
        if (!theme.isEmpty()) {
            settings.uiTheme = theme;
        }
    }
    if (patch.kernelSource) {
        const QString source = patch.kernelSource->trimmed();
        if (!source.isEmpty()) {
            settings.kernelSource = source;
        }
    }
    if (patch.firewallAutoAllow) {
        settings.firewallAutoAllow = *patch.firewallAutoAllow;
    }
    if (patch.autoFailover) {
        settings.autoFailover = *patch.autoFailover;
    }
    if (patch.failoverProbeUrls) {
        settings.failoverProbeUrls = patch.failoverProbeUrls->trimmed();
    }
    if (patch.speedtestAutoRefill) {
        settings.speedtestAutoRefill = *patch.speedtestAutoRefill;
    }
    if (patch.autoPrepareKernel) {
        settings.autoPrepareKernel = *patch.autoPrepareKernel;
    }
    if (patch.speedtestAddedSource) {
        settings.speedtestAddedSource = *patch.speedtestAddedSource;
    }

    static const char *kUpdate = R"(UPDATE settings SET
                 auto_fetch_enabled=?,
                 auto_fetch_interval_min=?,
                 auto_speedtest_enabled=?,
                 fetch_concurrency=?,
                 fetch_timeout_secs=?,
                 speedtest_concurrency=?,
                 speedtest_timeout_ms=?,
                 speedtest_download=?,
                 download_test_ms=?,
                 http_server_enabled=?,
                 close_to_tray=?,
                 exclude_keywords=?,
                 failover_latency_ms=?,
                 speedtest_parallel_kernels=?,
                 speedtest_persistent_kernel=?,
                 speedtest_reuse_minutes=?,
                 speedtest_conditional_isolation=?,
                 speedtest_download_top_n=?,
                 bandwidth_guard=?,
                 bandwidth_min_kbps=?,
                 publisher_api=?,
                 publisher_token=?,
                 ui_theme=?,
                 kernel_source=?,
                 firewall_auto_allow=?,
                 auto_failover=?,
                 speedtest_auto_refill=?,
                 auto_prepare_kernel=?,
                 speedtest_added_source=?,
                 failover_probe_urls=?
               WHERE id=1)";

    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(QString::fromLatin1(kUpdate));
    if (prepared.isErr()) {
        return Result<Models::AppSettings>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    int index = 1;
    statement.bindBool(index++, settings.autoFetchEnabled);
    statement.bindInt64(index++, settings.autoFetchIntervalMin);
    statement.bindBool(index++, settings.autoSpeedtestEnabled);
    statement.bindInt64(index++, settings.fetchConcurrency);
    statement.bindInt64(index++, settings.fetchTimeoutSecs);
    statement.bindInt64(index++, settings.speedtestConcurrency);
    statement.bindInt64(index++, settings.speedtestTimeoutMs);
    statement.bindBool(index++, settings.speedtestDownload);
    statement.bindInt64(index++, settings.downloadTestMs);
    statement.bindBool(index++, settings.httpServerEnabled);
    statement.bindBool(index++, settings.closeToTray);
    statement.bindText(index++, settings.excludeKeywords);
    statement.bindInt64(index++, settings.failoverLatencyMs);
    statement.bindBool(index++, settings.speedtestParallelKernels);
    statement.bindBool(index++, settings.speedtestPersistentKernel);
    statement.bindInt64(index++, settings.speedtestReuseMinutes);
    statement.bindBool(index++, settings.speedtestConditionalIsolation);
    statement.bindInt64(index++, settings.speedtestDownloadTopN);
    statement.bindBool(index++, settings.bandwidthGuard);
    statement.bindInt64(index++, settings.bandwidthMinKbps);
    statement.bindText(index++, settings.publisherApi);
    statement.bindText(index++, settings.publisherToken);
    statement.bindText(index++, settings.uiTheme);
    statement.bindText(index++, settings.kernelSource);
    statement.bindBool(index++, settings.firewallAutoAllow);
    statement.bindBool(index++, settings.autoFailover);
    statement.bindBool(index++, settings.speedtestAutoRefill);
    statement.bindBool(index++, settings.autoPrepareKernel);
    statement.bindBool(index++, settings.speedtestAddedSource);
    statement.bindText(index, settings.failoverProbeUrls);

    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<Models::AppSettings>::withError(stepped.error());
    }
    return getSettings();
}

/// 只写发布服务器两项，避免和"整套设置"的读写互相覆盖。
Result<Models::AppSettings> Database::updatePublisher(const QString &api, const QString &token)
{
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(
        QStringLiteral("UPDATE settings SET publisher_api=?, publisher_token=? WHERE id=1"));
    if (prepared.isErr()) {
        return Result<Models::AppSettings>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, api.trimmed());
    statement.bindText(2, token.trimmed());
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<Models::AppSettings>::withError(stepped.error());
    }
    return getSettings();
}

Status Database::touchAutoFetch()
{
    const QString now = TimeUtil::nowRfc3339();
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("UPDATE settings SET last_auto_fetch_at=? WHERE id=1"));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, now);
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}

Status Database::touchAutoSpeedtest()
{
    const QString now = TimeUtil::nowRfc3339();
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("UPDATE settings SET last_auto_speedtest_at=? WHERE id=1"));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, now);
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}

Result<QString> Database::getProxyConfigJsonText()
{
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("SELECT config_json FROM proxy_settings WHERE id = 1"));
    if (prepared.isErr()) {
        return Result<QString>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<QString>::withError(stepped.error());
    }
    if (!stepped.value()) {
        return Result<QString>::withValue(QString::fromUtf8(""));
    }
    QString text = statement.text("config_json");
    if (text.isNull()) {
        text = QString::fromUtf8("");
    }
    return Result<QString>::withValue(text);
}

Status Database::setProxyConfigJsonText(const QString &json)
{
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("UPDATE proxy_settings SET config_json = ? WHERE id = 1"));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, json);
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}
