#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "collector/GistDiscovery.h"
#include "core/Result.h"
#include "models/Settings.h"
#include "models/Source.h"
#include "models/Types.h"
#include "net/HttpClient.h"
#include "tasks/TaskTracker.h"

class Database;

// collector/mod.rs 的移植：订阅拉取（条件请求 → 竞速候选 → 解析 → 入库）、
// 重试与降权、按主机限流，以及 Gist 发现的编排。
//
// 【全部方法都是阻塞的】Net::send 与并发闸的等待都不返回控制权，所以这些入口
// 只能在 worker 线程上调用（GUI/HTTP 层用 QtConcurrent::run 包一层再等结果）。
// 类本身可复制：复制共享同一份状态，对应 Rust 的 `#[derive(Clone)]`（Arc 语义）。
namespace Collector {

// ---- 常量（行号都是 Rust collector/mod.rs 里的定义处）----
/// DEFAULT_FETCH_TIMEOUT = 30s（:19）
inline constexpr qint64 kDefaultFetchTimeoutMs = 30 * 1000;
/// MAX_SOURCE_BYTES = 64 MiB（:20）
inline constexpr qint64 kMaxSourceBytes = 64LL * 1024 * 1024;
/// CONNECT_TIMEOUT = 3s（:21）；走代理时 CONNECT 隧道本身要几秒，放宽到 10s（:110）。
inline constexpr qint64 kConnectTimeoutSecs = 3;
inline constexpr qint64 kConnectTimeoutMs = kConnectTimeoutSecs * 1000;
inline constexpr qint64 kProxyConnectTimeoutSecs = 10;
inline constexpr qint64 kProxyConnectTimeoutMs = kProxyConnectTimeoutSecs * 1000;
/// RESPONSE_HEADER_TIMEOUT = 6s（:25）
inline constexpr qint64 kResponseHeaderTimeoutMs = 6 * 1000;
/// GIST_SEARCH_HEADER_TIMEOUT = 15s（:28）
inline constexpr qint64 kGistSearchHeaderTimeoutMs = 15 * 1000;
/// BODY_IDLE_TIMEOUT = 5s（:31）
inline constexpr qint64 kBodyIdleTimeoutMs = 5 * 1000;
/// FETCH_MAX_ATTEMPTS = 3（:32）
inline constexpr int kFetchMaxAttempts = 3;
/// FETCH_RETRY_DELAYS = [300ms, 800ms]（:33）——只在 max_attempts-1 的范围内取用。
inline constexpr qint64 kFetchRetryDelaysMs[2] = {300, 800};
/// DEGRADED_FAIL_STREAK = 3（:36）：连续这么多轮网络失败后只给一次短尝试。
inline constexpr qint64 kDegradedFailStreak = 3;
/// PARSE_LIMIT = 4（:38）
inline constexpr int kParseLimit = 4;
/// PER_HOST_FETCH_CONCURRENCY = 2（:41）
inline constexpr int kPerHostFetchConcurrency = 2;
/// MIRROR_LIMIT = 6（:78）
inline constexpr int kMirrorLimit = 6;
/// reqwest 的 UA（:117），有些服务器按 UA 放行，必须逐字一致。
inline constexpr const char *kUserAgent = "vpn-parser/1.0 (+https://localhost)";
/// reqwest redirect Policy::limited(5)（:118）
inline constexpr int kMaxRedirects = 5;
/// fetch_attempt 里"至少也要给这一点点时间"的下限（:865 `max(Duration::from_millis(500))`）。
inline constexpr qint64 kMinAttemptBudgetMs = 500;

inline constexpr const char *kGithubRawHost = "raw.githubusercontent.com"; // :43
inline constexpr const char *kGistRawHost = "gist.githubusercontent.com";  // :49
/// GitHub raw 加速镜像（:46）
inline const QVector<QString> kGithubRawMirrors = {QStringLiteral("gh-proxy.com"),
                                                   QStringLiteral("ghfast.top")};
/// 这些主机的 raw 内容可以被镜像前缀代理（:49）
inline const QVector<QString> kMirrorableRawHosts = {QString::fromLatin1(kGithubRawHost),
                                                     QString::fromLatin1(kGistRawHost)};

/// 一次成功下载的结果（还没解析/入库），对应 Rust 的 `struct Downloaded`（:88）。
/// etag/lastModified/via 用 null QString 表示 Rust 的 None；via 非空 = 走了哪个镜像。
struct Downloaded {
    QByteArray body;
    QString finalUrl;
    QString etag;
    QString lastModified;
    QString via;
};

/// FetchCandidate（:81）。
struct FetchCandidate {
    QString url;
    QString host;
    bool mirror = false;
};

/// 任务面板用的是 Tasks::TaskTracker（对应 Rust 的 `tasks: TaskTracker`），
/// 阶段/耗时取值就是 Tasks::Phase 与 timings() 的五个可选值，这里不再自定义一套。

/// telegram.rs 的移植通过这两个函数把 t.me 频道分支挂回来：
/// `channel_name`（:59）与 `Collector::fetch_telegram_source`（:263）。
/// 没注入时 Collector 按普通订阅源处理，不会因为缺 telegram 而失败。
struct TelegramBridge {
    /// 返回 null QString 表示这个 URL 不是频道链接。
    std::function<QString(const QString &resolvedUrl)> channelName;
    /// budgetMs 是这一次 attempt 的总预算（Rust 的 attempt_budget）。
    std::function<Models::FetchResult(const class Collector &collector,
                                      const Models::Source &source, const QString &channel,
                                      const QString &taskId, qint64 budgetMs)> fetch;
};

/// 对应 Rust `register_gist_batch`（:664）的返回值 `(registered, nodes, rejected, pending)`。
struct GistBatchTally {
    qint64 registered = 0;
    qint64 nodes = 0;
    qint64 rejected = 0;
    qint64 pending = 0;
};

class Collector {
public:
    /// 传输 seam：生产用 Net::send，测试注入 canned response。
    /// 实现必须自己守住 request.timeoutMs，并且**不能抛异常**（错误走 Result）。
    using Transport = std::function<Result<Net::Response>(const Net::Request &)>;
    /// fetch_all_enabled_with_progress 的进度回调（Rust 的 `impl Fn(usize, usize)`）。
    using ProgressCallback = std::function<void(qint64 done, qint64 total)>;

    /// 对应 Rust `Collector::new(db, tasks)`（:126）。tasks 默认构造就行（Rust 那边
    /// 测试也这么写：`Collector::new(db.clone(), TaskTracker::default())`）。
    explicit Collector(std::shared_ptr<Database> db, Tasks::TaskTracker tasks = Tasks::TaskTracker());
    Collector(const Collector &other) = default;
    Collector &operator=(const Collector &other) = default;

    /// 对应 Rust `with_proxy`（:136）：换出站代理，db / 任务台账 / 并发闸仍然共享。
    Result<Collector> withProxy(const QString &proxyUrl) const;
    /// 这一份 collector 的出站是不是显式走代理（:104 的 via_proxy，telegram 要用）。
    bool viaProxy() const;
    /// 空 QString = 直连。
    QString proxyUrl() const;

    void setTransport(Transport transport);
    void setTelegramBridge(TelegramBridge bridge);
    std::shared_ptr<Database> database() const;
    /// 对应 Rust 的 `self.tasks`：界面轮询它拿进度快照。
    const Tasks::TaskTracker &tasks() const;

    // ---- 对外入口（阻塞；只在 worker 线程调用）----
    /// 对应 Rust `fetch_one`（:974）。找不到源返回 Err("source not found")。
    Result<Models::FetchResult> fetchOne(const QString &sourceId) const;
    /// 对应 Rust `fetch_one_with_proxy`（:145）。
    Result<Models::FetchResult> fetchOneWithProxy(const QString &sourceId,
                                                  const QString &proxyUrl) const;
    /// 对应 Rust `fetch_all_enabled`（:882）。
    Result<QVector<Models::FetchResult>> fetchAllEnabled() const;
    /// 对应 Rust `fetch_all_enabled_with_progress`（:887）。
    Result<QVector<Models::FetchResult>> fetchAllEnabledWithProgress(
        ProgressCallback progress) const;
    /// 对应 Rust `fetch_all_enabled_with_proxy`（:153）。
    Result<QVector<Models::FetchResult>> fetchAllEnabledWithProxy(const QString &proxyUrl,
                                                                  ProgressCallback progress) const;
    /// 对应 Rust `fetch_source`（:869）：自己开一个任务，预算取 settings.fetch_timeout_secs
    /// （注意这里**不 clamp**，和 fetch_all 那条 clamp(10,120) 不一样）。
    Models::FetchResult fetchSource(const Models::Source &source) const;
    /// 对应 Rust `fetch_source_in_task`（:794）：重试 + fail_streak 记账都在这一层，
    /// total_budget_ms 是**所有 attempt 共享的墙钟预算**，不是每次一份额。
    Models::FetchResult fetchSourceInTask(const Models::Source &source, const QString &taskId,
                                          qint64 totalBudgetMs) const;
    /// 对应 Rust `discover_gists`（:524）。
    Result<GistDiscoverySummary> discoverGists(const QString &query, int maxPages) const;
    /// 对应 Rust `discover_gists_from`（:537）：测试注入 canned 搜索页/raw 内容用这个入口。
    Result<GistDiscoverySummary> discoverGistsFrom(const QString &firstPageUrl,
                                                   const QString &rawBase, int maxPages) const;

    // ---- Rust 侧私有的编排，公开出来好测（同 parser 模块的做法）----
    /// 对应 Rust `fetch_source_tracked`（:170）：下载 → 条件请求短路 → 解析 → 入库。
    Models::FetchResult fetchSourceTracked(const Models::Source &source, const QString &taskId,
                                           qint64 attemptBudgetMs) const;
    /// 对应 Rust `download_source`（:361）。Ok(空 optional) = 服务端答了 304。
    Result<std::optional<Downloaded>> downloadSource(const Models::Source &source,
                                                     const QString &resolvedUrl,
                                                     const QString &taskId, qint64 budgetMs,
                                                     qint64 headerBudgetMs) const;
    /// 对应 Rust `race_candidates`（:379）：并发跑所有候选，第一个成功的赢，其余不管。
    Result<std::optional<Downloaded>> raceCandidates(const Models::Source &source,
                                                     const QVector<FetchCandidate> &candidates,
                                                     const QString &taskId, qint64 budgetMs,
                                                     qint64 headerBudgetMs) const;
    /// 对应 Rust `download_candidate`（:421）。
    Result<std::optional<Downloaded>> downloadCandidate(const Models::Source &source,
                                                        const FetchCandidate &candidate,
                                                        const QString &taskId, qint64 budgetMs,
                                                        qint64 headerBudgetMs) const;
    /// 对应 Rust `read_source_body`（:746）：体积上限 + 下载中的耗时回填。
    Result<QByteArray> readSourceBody(const Net::Response &response, const QString &taskId,
                                      const QString &sourceId,
                                      const QElapsedTimer &started) const;
    /// 对应 Rust `register_gist_batch`（:664）：一页内的 gist 并发试拉（页大小就是并发上限）。
    GistBatchTally registerGistBatch(const QVector<DiscoveredGist> &gists, const QString &rawBase,
                                     const QString &parentTaskId, QSet<QString> &seen) const;

private:
    /// 对应 Rust `mark_source_fetched`（:163）：写库失败只忽略，不影响拉取结果。
    void markSourceFetched(const QString &sourceId, const QString &status,
                           qint64 nodeCount) const;
    QString startTask(Tasks::Kind kind, const QVector<std::pair<QString, QString>> &sources) const;
    void phase(const QString &taskId, const QString &sourceId, Tasks::Phase value) const;
    void timings(const QString &taskId, const QString &sourceId,
                 const std::optional<quint64> &downloadMs, const std::optional<quint64> &parseMs,
                 const std::optional<quint64> &writeMs) const;
    void nodes(const QString &taskId, const QString &sourceId, size_t count) const;
    void complete(const QString &taskId, const QString &sourceId, const QString &message) const;
    void fail(const QString &taskId, const QString &sourceId, const QString &error) const;
    /// 一次 attempt，受这份源总预算的剩余部分约束（对应 fetch_attempt，:857）。
    Models::FetchResult fetchAttempt(const Models::Source &source, const QString &taskId,
                                     qint64 totalBudgetMs, qint64 elapsedMs) const;
    /// 带响应头看门狗地调一次传输（对应 Rust 的 timeout(header_budget, request.send())）。
    struct SendOutcome {
        bool timedOut = false;
        Result<Net::Response> response = Result<Net::Response>::withError(QString());
    };
    SendOutcome sendWithHeaderDeadline(const Net::Request &request, qint64 deadlineMs) const;

    struct Detail;
    std::shared_ptr<Detail> detail_;
};

// ---- 自由函数（Rust 侧是模块级私有 fn，这里公开好测）----

/// url_host（:1011）：解析不出或没有 host 时返回 null QString。
QString urlHost(const QString &url);

/// is_network_fetch_failure（:990）：只有传输层失败才算"这个主机凉了"，404 / 解析失败不算。
bool isNetworkFetchFailure(const QString &message);

/// is_retryable_fetch_failure（:1003）：网络失败 + 408 / 429 / 5xx。
bool isRetryableFetchFailure(const QString &message);

/// source_body_too_large（:983）：Content-Length 声明超了就不读。
bool sourceBodyTooLarge(const std::optional<qint64> &contentLength);

/// body_too_large_message（:1041）。
QString bodyTooLargeMessage(double sizeMib);

/// fetch_candidates（:1018）：直连 + （仅 GitHub raw）镜像；上次赢家排第一但不丢其他候选。
QVector<FetchCandidate> fetchCandidates(const QString &resolvedUrl, const QString &winner);

/// HTTP 状态码 → Rust `format!("HTTP {status}")` 的文本（含 StatusCode 的 reason phrase）。
QString httpStatusMessage(int status);

/// get_settings().fetch_timeout_secs 那条链路的默认值（:873-878 的 unwrap_or_default 语义）。
qint64 settingsFetchTimeoutMs(const Result<Models::AppSettings> &settings, qint64 fallbackMs);

} // namespace Collector
