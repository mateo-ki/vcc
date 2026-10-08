#pragma once

#include <optional>

#include <QString>

#include "core/JsonUtil.h"
#include "core/TimeUtil.h"

namespace Models {

/// 内核下载走哪条路：与 kernel::KernelSource 的 id 取值一致（models 不反向依赖 kernel）。
inline constexpr const char *kDefaultKernelSource = "gh-proxy";
/// 界面配色主题 id，必须与 gui/theme 的 DEFAULT_THEME 一致。
inline constexpr const char *kDefaultUiTheme = "soft-cobalt";

/// 持久化在 SQLite settings 单行表里的应用设置。
struct AppSettings {
    bool autoFetchEnabled = false;
    /// 间隔分钟数（最小 5）。
    int autoFetchIntervalMin = 60;
    bool autoSpeedtestEnabled = false;
    /// 订阅拉取并发。
    int fetchConcurrency = 4;
    /// 单源请求总超时秒数。
    int fetchTimeoutSecs = 30;
    /// TCP 探测并发。
    int speedtestConcurrency = 128;
    /// TCP 连接超时毫秒。
    int speedtestTimeoutMs = 1500;
    /// 测速时是否测下载吞吐。
    bool speedtestDownload = false;
    /// 下载探测时长毫秒。
    int downloadTestMs = 800;
    bool httpServerEnabled = true;
    bool closeToTray = true;
    /// 关键词黑名单（逗号/分号/空格/换行分隔）：命中的节点不作为连接候选，
    /// 也不参与推送与导出。
    QString excludeKeywords = QString::fromUtf8("");
    /// 自动切换：当前节点真实测速延迟超过该值（ms）即视为劣化。实测走的是完整
    /// HTTPS 请求，境外节点正常也在 300-900ms，所以默认值要留足余量。
    int failoverLatencyMs = 1500;
    /// 测速优化①：三个内核并行验证。默认关闭（串行更省内存，峰值内存约 1/3）。
    bool speedtestParallelKernels = false;
    /// 测速优化②：复用一个常驻内核，用控制器接口切换待测节点。默认关闭。
    bool speedtestPersistentKernel = false;
    /// 测速优化③：N 分钟内已验证过的节点直接沿用结果，0 表示不启用。
    int speedtestReuseMinutes = 0;
    /// 测速优化④：只对"可疑"的失败节点做 `mihomo -t` 隔离复测。默认关闭（全部复测）。
    bool speedtestConditionalIsolation = false;
    /// 测速优化⑤：下载测速只对延迟前 N 名做（0 = 所有通过节点都测）。
    int speedtestDownloadTopN = 0;
    /// 带宽保护：当前节点持续低于阈值 KB/s 时，交给选路调度器按劣化处理。
    bool bandwidthGuard = false;
    int bandwidthMinKbps = 0;
    /// 发布/推送服务器地址与 token（空 = 未配置）。
    QString publisherApi = QString::fromUtf8("");
    QString publisherToken = QString::fromUtf8("");
    /// 界面配色主题 id。
    QString uiTheme = QString::fromLatin1(kDefaultUiTheme);
    QString kernelSource = QString::fromLatin1(kDefaultKernelSource);
    /// 启动软件时自动把自己和内核 exe 加进防火墙允许列表（默认关：它会改系统设置）。
    bool firewallAutoAllow = false;
    /// 「节点故障自动切换」。以前它只是界面里的一个内存字段、重启就回 false，
    /// 用户以为开着而实际没开。默认仍关：不擅自改行为。
    bool autoFailover = false;
    /// 测通的节点太少时，后台自动补一轮全量测速（默认关）。
    bool speedtestAutoRefill = false;
    /// 「预备」状态下自动把缺的内核下载下来。这条**默认开**：新装的机器上第一次启动
    /// 就该自己动起来，关了就等于没有这个状态。
    bool autoPrepareKernel = true;
    /// 新增订阅源后自动拉取它并只测这个源。同样默认开。
    bool speedtestAddedSource = true;
    TimeUtil::OptionalUtc lastAutoFetchAt;
    TimeUtil::OptionalUtc lastAutoSpeedtestAt;

    Json toJson() const;
    static AppSettings fromJson(const Json &value);

    /// 节点名是否命中排除关键词（大小写不敏感）。
    bool excludesName(const QString &name) const;
};

/// 全是可选值的部分更新结构：JSON 里出现过的键才改。
struct UpdateSettings {
    std::optional<bool> autoFetchEnabled;
    std::optional<qint64> autoFetchIntervalMin;
    std::optional<bool> autoSpeedtestEnabled;
    std::optional<qint64> fetchConcurrency;
    std::optional<qint64> fetchTimeoutSecs;
    std::optional<qint64> speedtestConcurrency;
    std::optional<qint64> speedtestTimeoutMs;
    std::optional<bool> speedtestDownload;
    std::optional<qint64> downloadTestMs;
    std::optional<bool> httpServerEnabled;
    std::optional<bool> closeToTray;
    std::optional<QString> excludeKeywords;
    std::optional<qint64> failoverLatencyMs;
    std::optional<bool> speedtestParallelKernels;
    std::optional<bool> speedtestPersistentKernel;
    std::optional<qint64> speedtestReuseMinutes;
    std::optional<bool> speedtestConditionalIsolation;
    std::optional<qint64> speedtestDownloadTopN;
    std::optional<bool> bandwidthGuard;
    std::optional<qint64> bandwidthMinKbps;
    std::optional<QString> publisherApi;
    std::optional<QString> publisherToken;
    std::optional<QString> uiTheme;
    std::optional<QString> kernelSource;
    std::optional<bool> firewallAutoAllow;
    std::optional<bool> autoFailover;
    std::optional<bool> speedtestAutoRefill;
    std::optional<bool> autoPrepareKernel;
    std::optional<bool> speedtestAddedSource;

    static UpdateSettings fromJson(const Json &value);
    /// 把出现过的键合入目标设置。
    void applyTo(AppSettings &target) const;
    Json toJson() const;
};

/// 节点名是否命中排除关键词（大小写不敏感）。
bool nameExcluded(const QString &name, const QString &keywords);

}
