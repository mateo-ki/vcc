#include "models/Settings.h"

#include <QRegularExpression>

namespace Models {

namespace {

using JsonUtil::optBool;
using JsonUtil::optInt;
using JsonUtil::optTextValue;

void applyBool(const std::optional<bool> &source, bool &target)
{
    if (source.has_value()) {
        target = *source;
    }
}

void applyInt(const std::optional<qint64> &source, int &target)
{
    if (source.has_value()) {
        target = int(*source);
    }
}

void applyText(const std::optional<QString> &source, QString &target)
{
    if (source.has_value()) {
        target = *source;
    }
}

Json optionalBool(const std::optional<bool> &value)
{
    return value.has_value() ? Json(*value) : Json();
}

Json optionalInt(const std::optional<qint64> &value)
{
    return value.has_value() ? Json(*value) : Json();
}

Json optionalText(const std::optional<QString> &value)
{
    return value.has_value() ? Json(value->toStdString()) : Json();
}

}

bool nameExcluded(const QString &name, const QString &keywords)
{
    static const QRegularExpression separators(QStringLiteral("[,，;； \n\r\t]"));
    const QString lowerName = name.toLower();
    const QStringList parts = keywords.split(separators, Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString keyword = part.trimmed().toLower();
        if (!keyword.isEmpty() && lowerName.contains(keyword)) {
            return true;
        }
    }
    return false;
}

bool AppSettings::excludesName(const QString &name) const
{
    return nameExcluded(name, excludeKeywords);
}

Json AppSettings::toJson() const
{
    Json value;
    value["auto_fetch_enabled"] = autoFetchEnabled;
    value["auto_fetch_interval_min"] = autoFetchIntervalMin;
    value["auto_speedtest_enabled"] = autoSpeedtestEnabled;
    value["fetch_concurrency"] = fetchConcurrency;
    value["fetch_timeout_secs"] = fetchTimeoutSecs;
    value["speedtest_concurrency"] = speedtestConcurrency;
    value["speedtest_timeout_ms"] = speedtestTimeoutMs;
    value["speedtest_download"] = speedtestDownload;
    value["download_test_ms"] = downloadTestMs;
    value["http_server_enabled"] = httpServerEnabled;
    value["close_to_tray"] = closeToTray;
    value["exclude_keywords"] = excludeKeywords.toStdString();
    value["failover_latency_ms"] = failoverLatencyMs;
    value["speedtest_parallel_kernels"] = speedtestParallelKernels;
    value["speedtest_persistent_kernel"] = speedtestPersistentKernel;
    value["speedtest_reuse_minutes"] = speedtestReuseMinutes;
    value["speedtest_conditional_isolation"] = speedtestConditionalIsolation;
    value["speedtest_download_top_n"] = speedtestDownloadTopN;
    value["bandwidth_guard"] = bandwidthGuard;
    value["bandwidth_min_kbps"] = bandwidthMinKbps;
    value["publisher_api"] = publisherApi.toStdString();
    value["publisher_token"] = publisherToken.toStdString();
    value["ui_theme"] = uiTheme.toStdString();
    value["kernel_source"] = kernelSource.toStdString();
    value["firewall_auto_allow"] = firewallAutoAllow;
    value["auto_failover"] = autoFailover;
    value["failover_probe_urls"] = failoverProbeUrls.toStdString();
    value["speedtest_auto_refill"] = speedtestAutoRefill;
    value["auto_prepare_kernel"] = autoPrepareKernel;
    value["speedtest_added_source"] = speedtestAddedSource;
    value["last_auto_fetch_at"] = JsonUtil::optText(lastAutoFetchAt.toJsonText());
    value["last_auto_speedtest_at"] = JsonUtil::optText(lastAutoSpeedtestAt.toJsonText());
    return value;
}

AppSettings AppSettings::fromJson(const Json &value)
{
    AppSettings settings;
    settings.autoFetchEnabled = JsonUtil::boolOr(value, "auto_fetch_enabled", false);
    settings.autoFetchIntervalMin = int(JsonUtil::intOr(value, "auto_fetch_interval_min", 60));
    settings.autoSpeedtestEnabled = JsonUtil::boolOr(value, "auto_speedtest_enabled", false);
    settings.fetchConcurrency = int(JsonUtil::intOr(value, "fetch_concurrency", 4));
    settings.fetchTimeoutSecs = int(JsonUtil::intOr(value, "fetch_timeout_secs", 30));
    settings.speedtestConcurrency = int(JsonUtil::intOr(value, "speedtest_concurrency", 128));
    settings.speedtestTimeoutMs = int(JsonUtil::intOr(value, "speedtest_timeout_ms", 1500));
    settings.speedtestDownload = JsonUtil::boolOr(value, "speedtest_download", false);
    settings.downloadTestMs = int(JsonUtil::intOr(value, "download_test_ms", 800));
    settings.httpServerEnabled = JsonUtil::boolOr(value, "http_server_enabled", true);
    settings.closeToTray = JsonUtil::boolOr(value, "close_to_tray", true);
    settings.excludeKeywords = JsonUtil::textOrEmpty(value, "exclude_keywords");
    settings.failoverLatencyMs = int(JsonUtil::intOr(value, "failover_latency_ms", 1500));
    settings.speedtestParallelKernels =
        JsonUtil::boolOr(value, "speedtest_parallel_kernels", false);
    settings.speedtestPersistentKernel =
        JsonUtil::boolOr(value, "speedtest_persistent_kernel", false);
    settings.speedtestReuseMinutes = int(JsonUtil::intOr(value, "speedtest_reuse_minutes", 0));
    settings.speedtestConditionalIsolation =
        JsonUtil::boolOr(value, "speedtest_conditional_isolation", false);
    settings.speedtestDownloadTopN = int(JsonUtil::intOr(value, "speedtest_download_top_n", 0));
    settings.bandwidthGuard = JsonUtil::boolOr(value, "bandwidth_guard", false);
    settings.bandwidthMinKbps = int(JsonUtil::intOr(value, "bandwidth_min_kbps", 0));
    settings.publisherApi = JsonUtil::textOrEmpty(value, "publisher_api");
    settings.publisherToken = JsonUtil::textOrEmpty(value, "publisher_token");
    const QString theme = JsonUtil::textOrEmpty(value, "ui_theme");
    settings.uiTheme = theme.isEmpty() ? QString::fromLatin1(kDefaultUiTheme) : theme;
    const QString source = JsonUtil::textOrEmpty(value, "kernel_source");
    settings.kernelSource = source.isEmpty() ? QString::fromLatin1(kDefaultKernelSource) : source;
    settings.firewallAutoAllow = JsonUtil::boolOr(value, "firewall_auto_allow", false);
    settings.autoFailover = JsonUtil::boolOr(value, "auto_failover", false);
    settings.failoverProbeUrls = JsonUtil::textOrEmpty(value, "failover_probe_urls");
    settings.speedtestAutoRefill = JsonUtil::boolOr(value, "speedtest_auto_refill", false);
    settings.autoPrepareKernel = JsonUtil::boolOr(value, "auto_prepare_kernel", true);
    settings.speedtestAddedSource = JsonUtil::boolOr(value, "speedtest_added_source", true);
    settings.lastAutoFetchAt =
        TimeUtil::OptionalUtc::fromText(JsonUtil::nullableText(value, "last_auto_fetch_at"));
    settings.lastAutoSpeedtestAt =
        TimeUtil::OptionalUtc::fromText(JsonUtil::nullableText(value, "last_auto_speedtest_at"));
    return settings;
}

UpdateSettings UpdateSettings::fromJson(const Json &value)
{
    UpdateSettings update;
    update.autoFetchEnabled = optBool(value, "auto_fetch_enabled");
    update.autoFetchIntervalMin = optInt(value, "auto_fetch_interval_min");
    update.autoSpeedtestEnabled = optBool(value, "auto_speedtest_enabled");
    update.fetchConcurrency = optInt(value, "fetch_concurrency");
    update.fetchTimeoutSecs = optInt(value, "fetch_timeout_secs");
    update.speedtestConcurrency = optInt(value, "speedtest_concurrency");
    update.speedtestTimeoutMs = optInt(value, "speedtest_timeout_ms");
    update.speedtestDownload = optBool(value, "speedtest_download");
    update.downloadTestMs = optInt(value, "download_test_ms");
    update.httpServerEnabled = optBool(value, "http_server_enabled");
    update.closeToTray = optBool(value, "close_to_tray");
    update.excludeKeywords = optTextValue(value, "exclude_keywords");
    update.failoverLatencyMs = optInt(value, "failover_latency_ms");
    update.speedtestParallelKernels = optBool(value, "speedtest_parallel_kernels");
    update.speedtestPersistentKernel = optBool(value, "speedtest_persistent_kernel");
    update.speedtestReuseMinutes = optInt(value, "speedtest_reuse_minutes");
    update.speedtestConditionalIsolation = optBool(value, "speedtest_conditional_isolation");
    update.speedtestDownloadTopN = optInt(value, "speedtest_download_top_n");
    update.bandwidthGuard = optBool(value, "bandwidth_guard");
    update.bandwidthMinKbps = optInt(value, "bandwidth_min_kbps");
    update.publisherApi = optTextValue(value, "publisher_api");
    update.publisherToken = optTextValue(value, "publisher_token");
    update.uiTheme = optTextValue(value, "ui_theme");
    update.kernelSource = optTextValue(value, "kernel_source");
    update.firewallAutoAllow = optBool(value, "firewall_auto_allow");
    update.autoFailover = optBool(value, "auto_failover");
    update.failoverProbeUrls = optTextValue(value, "failover_probe_urls");
    update.speedtestAutoRefill = optBool(value, "speedtest_auto_refill");
    update.autoPrepareKernel = optBool(value, "auto_prepare_kernel");
    update.speedtestAddedSource = optBool(value, "speedtest_added_source");
    return update;
}

void UpdateSettings::applyTo(AppSettings &target) const
{
    applyBool(autoFetchEnabled, target.autoFetchEnabled);
    applyInt(autoFetchIntervalMin, target.autoFetchIntervalMin);
    applyBool(autoSpeedtestEnabled, target.autoSpeedtestEnabled);
    applyInt(fetchConcurrency, target.fetchConcurrency);
    applyInt(fetchTimeoutSecs, target.fetchTimeoutSecs);
    applyInt(speedtestConcurrency, target.speedtestConcurrency);
    applyInt(speedtestTimeoutMs, target.speedtestTimeoutMs);
    applyBool(speedtestDownload, target.speedtestDownload);
    applyInt(downloadTestMs, target.downloadTestMs);
    applyBool(httpServerEnabled, target.httpServerEnabled);
    applyBool(closeToTray, target.closeToTray);
    applyText(excludeKeywords, target.excludeKeywords);
    applyInt(failoverLatencyMs, target.failoverLatencyMs);
    applyBool(speedtestParallelKernels, target.speedtestParallelKernels);
    applyBool(speedtestPersistentKernel, target.speedtestPersistentKernel);
    applyInt(speedtestReuseMinutes, target.speedtestReuseMinutes);
    applyBool(speedtestConditionalIsolation, target.speedtestConditionalIsolation);
    applyInt(speedtestDownloadTopN, target.speedtestDownloadTopN);
    applyBool(bandwidthGuard, target.bandwidthGuard);
    applyInt(bandwidthMinKbps, target.bandwidthMinKbps);
    applyText(publisherApi, target.publisherApi);
    applyText(publisherToken, target.publisherToken);
    applyText(uiTheme, target.uiTheme);
    applyText(kernelSource, target.kernelSource);
    applyBool(firewallAutoAllow, target.firewallAutoAllow);
    applyBool(autoFailover, target.autoFailover);
    applyText(failoverProbeUrls, target.failoverProbeUrls);
    applyBool(speedtestAutoRefill, target.speedtestAutoRefill);
    applyBool(autoPrepareKernel, target.autoPrepareKernel);
    applyBool(speedtestAddedSource, target.speedtestAddedSource);
}

Json UpdateSettings::toJson() const
{
    Json value;
    value["auto_fetch_enabled"] = optionalBool(autoFetchEnabled);
    value["auto_fetch_interval_min"] = optionalInt(autoFetchIntervalMin);
    value["auto_speedtest_enabled"] = optionalBool(autoSpeedtestEnabled);
    value["fetch_concurrency"] = optionalInt(fetchConcurrency);
    value["fetch_timeout_secs"] = optionalInt(fetchTimeoutSecs);
    value["speedtest_concurrency"] = optionalInt(speedtestConcurrency);
    value["speedtest_timeout_ms"] = optionalInt(speedtestTimeoutMs);
    value["speedtest_download"] = optionalBool(speedtestDownload);
    value["download_test_ms"] = optionalInt(downloadTestMs);
    value["http_server_enabled"] = optionalBool(httpServerEnabled);
    value["close_to_tray"] = optionalBool(closeToTray);
    value["exclude_keywords"] = optionalText(excludeKeywords);
    value["failover_latency_ms"] = optionalInt(failoverLatencyMs);
    value["speedtest_parallel_kernels"] = optionalBool(speedtestParallelKernels);
    value["speedtest_persistent_kernel"] = optionalBool(speedtestPersistentKernel);
    value["speedtest_reuse_minutes"] = optionalInt(speedtestReuseMinutes);
    value["speedtest_conditional_isolation"] = optionalBool(speedtestConditionalIsolation);
    value["speedtest_download_top_n"] = optionalInt(speedtestDownloadTopN);
    value["bandwidth_guard"] = optionalBool(bandwidthGuard);
    value["bandwidth_min_kbps"] = optionalInt(bandwidthMinKbps);
    value["publisher_api"] = optionalText(publisherApi);
    value["publisher_token"] = optionalText(publisherToken);
    value["ui_theme"] = optionalText(uiTheme);
    value["kernel_source"] = optionalText(kernelSource);
    value["firewall_auto_allow"] = optionalBool(firewallAutoAllow);
    value["auto_failover"] = optionalBool(autoFailover);
    value["failover_probe_urls"] = optionalText(failoverProbeUrls);
    value["speedtest_auto_refill"] = optionalBool(speedtestAutoRefill);
    value["auto_prepare_kernel"] = optionalBool(autoPrepareKernel);
    value["speedtest_added_source"] = optionalBool(speedtestAddedSource);
    return value;
}

}
