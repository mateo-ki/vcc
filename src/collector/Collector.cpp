#include "collector/Collector.h"

#include <QSet>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include "collector/CollectorMessages.h"
#include "collector/FetchLimits.h"
#include "core/Crypto.h"
#include "core/Ids.h"
#include "core/Log.h"
#include "db/Database.h"
#include "parser/Common.h"
#include "parser/Subscription.h"

// 这里只放 collector/mod.rs 的东西；t.me 频道那条分支由 telegram.rs 的移植通过
// TelegramBridge 挂进来（见 Collector::setTelegramBridge）。
//
// 所有方法都是 const + 阻塞的（Rust 那边全是 &self）：可变状态都在 Database 和
// 共享的 Detail 里，所以 Collector 本身可以随手复制、跨线程共用。
namespace Collector {

namespace {

/// Rust 的 `Option<String> == Some(text)`：null QString 是 None，不能和空串混为一谈。
bool sameAsSome(const QString &nullable, const QString &text)
{
    return !nullable.isNull() && nullable == text;
}

/// `response.content_length()`：只有响应里真给了 Content-Length 才是 Some。
std::optional<qint64> declaredContentLength(const Net::Response &response)
{
    const QString value = response.headerValue(QStringLiteral("Content-Length"));
    if (value.isNull() || value.trimmed().isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const qint64 length = value.trimmed().toLongLong(&ok);
    return ok ? std::optional<qint64>(length) : std::nullopt;
}

/// hyper 的 `StatusCode` Display 用的标准 reason phrase（RFC 9110）。没有 reason 的
/// 自定义码只输出状态码，和 Rust 的 `format!("HTTP {status}")` 一致。
const char *reasonPhrase(int status)
{
    switch (status) {
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Content";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";
    default: return nullptr;
    }
}

/// `tokio::time::timeout(header_budget, request.send())` 的等价物：传输本身阻塞，所以把它
/// 扔到一条分离线程上，调用方只等到 deadline。deadline 到了就按 Rust 的 Elapsed 分支
/// 报"响应头超时"，那次请求继续在后台跑到自己的 timeoutMs 结束、结果无人认领
/// （Rust 那边是 abort 掉 future）。
struct DeadlineState {
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    Result<Net::Response> response;
};

/// 生产用的传输：直接走 Net::send（阻塞，所以只能在 worker 线程上被调到）。
Result<Net::Response> sendThroughHttpClient(const Net::Request &request)
{
    return Net::send(request);
}

/// 三源组（source_id / name / resolved_url），对应 Rust 的 source_metadata 元组（:909）。
struct SourceMeta {
    QString sourceId;
    QString sourceName;
    QString resolvedUrl;
};

} // namespace

QString urlHost(const QString &url)
{
    // :1011 —— Url::parse 失败或没有 host 都算 None。
    QString host;
    if (!parseAbsoluteUrl(url, host) || host.isEmpty()) {
        return QString();
    }
    // Rust 用 to_ascii_lowercase；这里再走一遍同一个函数，保证和它逐字节一致。
    return Parser::Common::toAsciiLower(host);
}

bool isNetworkFetchFailure(const QString &message)
{
    // :990-1001 —— 只有传输层失败才算"这台主机凉了"，404 / 解析失败不算。
    const QString lower = Parser::Common::toAsciiLower(message);
    static const QVector<QString> needles = {
        QStringLiteral("timeout"),
        QStringLiteral("timed out"),
        QStringLiteral("超时"),
        QStringLiteral("connect"),
        QStringLiteral("connection"),
        QStringLiteral("request failed"),
        QStringLiteral("error sending request"),
        QStringLiteral("body read failed"),
        QStringLiteral("read body failed"),
    };
    for (const QString &needle : needles) {
        if (lower.contains(needle)) {
            return true;
        }
    }
    return false;
}

bool isRetryableFetchFailure(const QString &message)
{
    // :1003-1009
    const QString lower = Parser::Common::toAsciiLower(message);
    if (isNetworkFetchFailure(message)) {
        return true;
    }
    if (lower.startsWith(QLatin1String("http 408"))
        || lower.startsWith(QLatin1String("http 429"))) {
        return true;
    }
    for (int status = 500; status <= 599; ++status) {
        if (lower.startsWith(QStringLiteral("http %1").arg(status))) {
            return true;
        }
    }
    return false;
}

bool sourceBodyTooLarge(const std::optional<qint64> &contentLength)
{
    // :983-985
    return contentLength.has_value() && contentLength.value() > kMaxSourceBytes;
}

QString bodyTooLargeMessage(double sizeMib)
{
    // :1041-1046 —— `{:.1}` 一位小数，limit 按 MAX_SOURCE_BYTES / 1024 / 1024。
    return QStringLiteral("response body too large: %1 MiB (limit %2 MiB)")
        .arg(QString::number(sizeMib, 'f', 1))
        .arg(kMaxSourceBytes / 1024 / 1024);
}

QString httpStatusMessage(int status)
{
    const char *reason = reasonPhrase(status);
    if (reason == nullptr) {
        return QStringLiteral("HTTP %1").arg(status);
    }
    return QStringLiteral("HTTP %1 %2").arg(status).arg(QLatin1String(reason));
}

QVector<FetchCandidate> fetchCandidates(const QString &resolvedUrl, const QString &winner)
{
    // :1018-1039 —— 直连 +（仅 GitHub raw 这类主机）镜像；上次赢家排第一，候选一个不丢。
    QVector<FetchCandidate> candidates;
    candidates.push_back(FetchCandidate{resolvedUrl, urlHost(resolvedUrl), false});
    if (kMirrorableRawHosts.contains(candidates[0].host)) {
        for (const QString &mirror : kGithubRawMirrors) {
            candidates.push_back(FetchCandidate{
                QStringLiteral("https://%1/%2").arg(mirror, resolvedUrl), mirror, true});
        }
    }
    if (!winner.isNull()) {
        int index = -1;
        for (int candidate = 0; candidate < candidates.size(); ++candidate) {
            if (candidates[candidate].host == winner) {
                index = candidate;
                break;
            }
        }
        if (index > 0) {
            const FetchCandidate preferred = candidates.takeAt(index);
            candidates.prepend(preferred);
        }
    }
    return candidates;
}

qint64 settingsFetchTimeoutMs(const Result<Models::AppSettings> &settings, qint64 fallbackMs)
{
    // :873-878 —— 只有 get_settings 失败才退回默认值；成功就按 secs 原样用（不 clamp）。
    if (settings.isErr()) {
        return fallbackMs;
    }
    return qint64(settings.value().fetchTimeoutSecs) * 1000;
}

// ---- Collector ----

struct Collector::Detail {
    std::shared_ptr<Database> db;
    Tasks::TaskTracker tasks;
    QString proxyUrl;
    bool viaProxy = false;
    Transport transport;
    TelegramBridge telegram;
};

Collector::Collector(std::shared_ptr<Database> db, Tasks::TaskTracker tasks)
    : detail_(std::make_shared<Detail>())
{
    detail_->db = std::move(db);
    detail_->tasks = std::move(tasks);
    // build_client(None)（:107-123）：UA、连接超时、5 跳重定向都在 download_candidate
    // 里按每次请求填（Rust 用 .timeout(budget) 覆盖 client 级默认总时限）。
    detail_->transport = sendThroughHttpClient;
}

Result<Collector> Collector::withProxy(const QString &proxyUrl) const
{
    // :136-143 —— Rust 只在 reqwest 解析代理地址失败时返回 Err。
    if (proxyUrl.isNull() || proxyUrl.trimmed().isEmpty()) {
        return Result<Collector>::withError(
            QStringLiteral("invalid proxy url: %1").arg(proxyUrl));
    }
    auto detail = std::make_shared<Detail>();
    detail->db = detail_->db;
    detail->tasks = detail_->tasks;
    detail->telegram = detail_->telegram;
    detail->transport = detail_->transport;
    detail->proxyUrl = proxyUrl;
    detail->viaProxy = true;
    Collector collector(detail->db, detail->tasks);
    collector.detail_ = detail;
    return Result<Collector>::withValue(std::move(collector));
}

bool Collector::viaProxy() const { return detail_->viaProxy; }
QString Collector::proxyUrl() const { return detail_->proxyUrl; }

void Collector::setTransport(Transport transport)
{
    detail_->transport = transport ? std::move(transport) : Transport(sendThroughHttpClient);
}

void Collector::setTelegramBridge(TelegramBridge bridge)
{
    detail_->telegram = std::move(bridge);
}

std::shared_ptr<Database> Collector::database() const { return detail_->db; }
const Tasks::TaskTracker &Collector::tasks() const { return detail_->tasks; }

void Collector::markSourceFetched(const QString &sourceId, const QString &status,
                                  qint64 nodeCount) const
{
    // :163-168 —— 写库失败只忽略。
    const Status written = detail_->db->markSourceFetched(sourceId, status, nodeCount);
    if (written.isErr()) {
        Log::warn(QStringLiteral("mark_source_fetched failed: %1").arg(written.error()));
    }
}

QString Collector::startTask(Tasks::Kind kind,
                             const QVector<std::pair<QString, QString>> &sources) const
{
    // :871-872 / :903-908
    return detail_->tasks.start(kind, sources);
}

void Collector::phase(const QString &taskId, const QString &sourceId, Tasks::Phase value) const
{
    detail_->tasks.phase(taskId, sourceId, value);
}

void Collector::timings(const QString &taskId, const QString &sourceId,
                        const std::optional<quint64> &downloadMs,
                        const std::optional<quint64> &parseMs,
                        const std::optional<quint64> &writeMs) const
{
    // Rust 的 timings 后两个可选耗时（speedtest / update）collector 全传 None（:225-233）。
    detail_->tasks.timings(taskId, sourceId, downloadMs, parseMs, writeMs, std::nullopt,
                           std::nullopt);
}

void Collector::nodes(const QString &taskId, const QString &sourceId, size_t count) const
{
    detail_->tasks.nodes(taskId, sourceId, count);
}

void Collector::complete(const QString &taskId, const QString &sourceId,
                         const QString &message) const
{
    detail_->tasks.complete(taskId, sourceId, message);
}

void Collector::fail(const QString &taskId, const QString &sourceId, const QString &error) const
{
    detail_->tasks.fail(taskId, sourceId, error);
}

/// 传输必须跑在 Qt 线程上：Qt 的定时器和 socket 通知只认 QThread 的事件分发器。
/// 裸 std::thread 里 Net::send 的分段超时一个都不响 —— 实测同一条"接了连接不回话"的请求，
/// 主线程 1030ms 回、QThread 990ms 回、裸 std::thread 到 12s 仍卡死（header 预算只有 1s）。
/// 所以以前每条死源都要烧满外层 deadline（budget+2s=32s）才回，分离线程本身永久泄漏。
namespace {

QThreadPool &transportPool()
{
    static QThreadPool pool;
    static const bool configured = [pool_ptr = &pool] {
        pool_ptr->setExpiryTimeout(30000);
        pool_ptr->setMaxThreadCount(qMax(16, QThread::idealThreadCount()));
        return true;
    }();
    Q_UNUSED(configured);
    return pool;
}

} // namespace

Collector::SendOutcome Collector::sendWithHeaderDeadline(const Net::Request &request,
                                                         qint64 deadlineMs) const
{
    SendOutcome outcome;
    // transport 按值抓走：传输任务可能在 Collector 析构之后才跑完（同 Rust 的 abort 语义，
    // 结果没人要，但对象必须还活着）。
    const Transport transport = detail_->transport;
    auto state = std::make_shared<DeadlineState>();
    transportPool().start([transport, request, state]() {
        Result<Net::Response> result = Result<Net::Response>::withError(
            QStringLiteral("request failed: transport produced no result"));
        try {
            result = transport(request);
        } catch (const std::exception &error) {
            result = Result<Net::Response>::withError(
                QStringLiteral("request failed: %1").arg(QString::fromUtf8(error.what())));
        } catch (...) {
            result = Result<Net::Response>::withError(
                QStringLiteral("request failed: unknown transport error"));
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->response = std::move(result);
            state->done = true;
        }
        state->condition.notify_all();
    });

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->condition.wait_for(lock, std::chrono::milliseconds(deadlineMs),
                                   [state] { return state->done; })) {
        outcome.timedOut = true;
        return outcome;
    }
    outcome.response = state->response;
    return outcome;
}

Result<std::optional<Downloaded>> Collector::downloadCandidate(const Models::Source &source,
                                                               const FetchCandidate &candidate,
                                                               const QString &taskId,
                                                               qint64 budgetMs,
                                                               qint64 headerBudgetMs,
                                                               const Net::CancelToken &cancel) const
{
    using Out = Result<std::optional<Downloaded>>;

    // :429-440 —— 等许可的时间不算进请求预算：先拿到闸，再起 deadline。
    Semaphore::Guard permit = candidate.mirror ? mirrorLimit()->acquireGuard()
                                              : hostLimit(candidate.url)->acquireGuard();
    // Rust: acquire_owned().await.map_err(|_| "…闸已关闭") —— tokio 的闸可以被 close，
    // 本实现的闸不会，这条分支只是把同样的话留在原地。
    if (!permit.holds()) {
        return Out::withError(candidate.mirror ? Messages::mirrorGateClosed()
                                               : Messages::hostGateClosed());
    }

    Net::Request request;
    request.url = candidate.url;
    request.method = QByteArrayLiteral("GET");
    request.timeoutMs = int(budgetMs);
    // :442 —— Rust 的 `header_budget = budget.min(header_budget)` 只作用在"等响应头"上；
    // 头一到，剩下的时间全给读体（每块 BODY_IDLE_TIMEOUT，整体仍是 budget）。
    request.headerTimeoutMs = int(qMin(budgetMs, headerBudgetMs));
    request.bodyIdleTimeoutMs = int(kBodyIdleTimeoutMs);
    // build_client：走代理时 CONNECT 隧道本身要几秒，直连的 3s 快速失败太紧（:109-112）。
    request.connectTimeoutMs = detail_->viaProxy ? int(kProxyConnectTimeoutMs)
                                                : int(kConnectTimeoutMs);
    request.userAgent = QString::fromLatin1(kUserAgent);
    request.maxRedirects = kMaxRedirects;
    request.followRedirects = true;
    request.proxyUrl = detail_->proxyUrl;
    // 竞速时这条腿的可取消凭据（非竞速的调用方传空 → 不可取消，行为不变）。
    request.cancel = cancel;

    // :442-451 —— 条件请求只对直连有意义：镜像的 ETag 语义不跟随上游。
    if (!candidate.mirror && sameAsSome(source.lastResolvedUrl, candidate.url)) {
        if (!source.lastEtag.isNull()) {
            request.ifNoneMatch = source.lastEtag;
        }
        if (!source.lastModified.isNull()) {
            request.ifModifiedSince = source.lastModified;
        }
    }

    // :452 —— Rust 的 `let header_budget = budget.min(header_budget);`
    const qint64 headerBudget = qMin(budgetMs, headerBudgetMs);
    // 响应头和响应体各自的预算现在都写进了 request（Net::send 分段计时，和 Rust 的
    // `timeout(header_budget, send())` + `chunk()` 空闲超时同一套），这里的外层
    // deadline 只是"分离线程整体卡死"的保险，比 budget 晚一点、不参与正常判定。
    const qint64 deadlineMs = qMax<qint64>(budgetMs + kTransportDeadlineSlackMs, 1);

    const SendOutcome sent = sendWithHeaderDeadline(request, deadlineMs);
    if (sent.timedOut) {
        // :454-459 —— 只有分离线程整体卡死才会走到这里（正常情况下分段超时由
        // Net::send 自己判定），文案仍按 Rust 的 header_budget.as_secs()。
        return Out::withError(Messages::format(Messages::responseHeaderTimeout(),
                                              {QString::number(headerBudget / 1000)}));
    }
    if (sent.response.isErr()) {
        // :460-468 —— reqwest 按 is_timeout()/is_connect() 分流；这里按 Net::send 的传输
        // 错误文本做同一套判断（net/HttpClient.cpp:51 TimeoutError → "连接超时"，
        // :39 timeoutMs 兜底 → "请求超时"）。
        const QString error = sent.response.error();
        if (error == Net::cancelledError()) {
            // 竞速里赢家已经出现，这条腿是被主动放手的：既不是连接失败也不是超时，
            // 原样把哨兵带回去，由 raceCandidates 决定"不进失败清单"。
            // 注意取消只是让这条腿更早返回，外层 deadlineMs（budget + 2s 余量）的语义
            // 一点没动 —— 它兜的仍然是"分离线程整体卡死"，包括不认取消的自定义 transport。
            return Out::withError(Net::cancelledError());
        }
        if (error == QStringLiteral("连接超时")) {
            // Rust 这条消息用的是常量 CONNECT_TIMEOUT，不是走代理时放宽的那个值。
            return Out::withError(Messages::format(
                Messages::connectTimeoutFailed(), {QString::number(kConnectTimeoutSecs)}));
        }
        if (error == QStringLiteral("请求超时")) {
            return Out::withError(
                QStringLiteral("request timeout after %1s").arg(budgetMs / 1000));
        }
        if (error == QStringLiteral("响应头超时")) {
            // Rust 的 Elapsed 分支：头没到，压根没进读体阶段。
            return Out::withError(Messages::format(Messages::responseHeaderTimeout(),
                                                  {QString::number(headerBudget / 1000)}));
        }
        if (error.startsWith(QStringLiteral("response body idle"))) {
            // Rust 的 read_source_body 空闲超时（:759-765）被 read_candidate 的调用点包成
            // "read body failed (content-length=…)"；这里响应头已随错误一起丢了，只能报
            // unknown（Rust 那边 content_length 是 Option，unknown 是它的合法取值之一）。
            return Out::withError(
                QStringLiteral("read body failed (content-length=unknown): %1").arg(error));
        }
        return Out::withError(QStringLiteral("request failed: %1").arg(error));
    }

    const Net::Response &response = sent.response.value();
    if (response.status == 304) {
        // :472-474 —— 服务端答 304，条件请求命中。
        return Out::withValue(std::nullopt);
    }
    if (!response.isSuccess()) {
        // :475-477 —— Rust 的 `format!("HTTP {status}")` 带 reason phrase。
        return Out::withError(httpStatusMessage(response.status));
    }

    const std::optional<qint64> contentLength = declaredContentLength(response);
    if (sourceBodyTooLarge(contentLength)) {
        // :478-483
        return Out::withError(
            bodyTooLargeMessage(double(contentLength.value_or(0)) / 1048576.0));
    }

    const QString finalUrl = response.finalUrl.isEmpty() ? candidate.url : response.finalUrl;
    const QString etag = response.headerValue(QStringLiteral("ETag"));
    const QString lastModified = response.headerValue(QStringLiteral("Last-Modified"));

    QElapsedTimer readStarted;
    readStarted.start();
    const Result<QByteArray> read = readSourceBody(response, taskId, source.id, readStarted);
    if (read.isErr()) {
        // :495-508
        const QString declared = contentLength.has_value()
            ? QString::number(contentLength.value())
            : QStringLiteral("unknown");
        return Out::withError(
            QStringLiteral("read body failed (content-length=%1): %2").arg(declared, read.error()));
    }
    const QByteArray body = read.value();
    if (qint64(body.size()) > kMaxSourceBytes) {
        // :509-511
        return Out::withError(bodyTooLargeMessage(double(body.size()) / 1048576.0));
    }

    Downloaded downloaded;
    downloaded.body = body;
    downloaded.finalUrl = finalUrl;
    downloaded.etag = etag;
    downloaded.lastModified = lastModified;
    // :517 —— via 只在走镜像时非空。
    downloaded.via = candidate.mirror ? candidate.host : QString();
    return Out::withValue(std::optional<Downloaded>(std::move(downloaded)));
}

Result<QByteArray> Collector::readSourceBody(const Net::Response &response, const QString &taskId,
                                             const QString &sourceId,
                                             const QElapsedTimer &started) const
{
    // :746-790 —— Rust 一边按 chunk 读（每块 BODY_IDLE_TIMEOUT、总量 MAX_SOURCE_BYTES）
    // 一边把 download_ms 回填给任务面板。Net::send 已经一次性把整个响应体带回来了，
    // 空闲超时折叠进 download_candidate 的看门狗，这里保留体积判断和耗时回填。
    // 原来这里还有一句 body.reserve(min(declaredContentLength, kMaxSourceBytes))：
    // `QByteArray body = response.body` 是隐式共享的浅拷贝，reserve 会立刻 detach 出
    // 一份完整深拷贝 —— 64MB 的订阅正文在那一瞬间是双份。正文已经整个在内存里了，
    // 预留容量没有任何收益。
    QByteArray body = response.body;
    if (qint64(body.size()) > kMaxSourceBytes) {
        // :770-775
        return Result<QByteArray>::withError(
            QStringLiteral("response body too large: more than %1 MiB")
                .arg(kMaxSourceBytes / 1024 / 1024));
    }
    // :777-787 —— 下载中的耗时回填（一次拉取至少刷一次，慢源在面板上是活的）。
    timings(taskId, sourceId, quint64(started.elapsed()), std::nullopt, std::nullopt);
    return Result<QByteArray>::withValue(std::move(body));
}

Result<std::optional<Downloaded>> Collector::downloadSource(const Models::Source &source,
                                                           const QString &resolvedUrl,
                                                           const QString &taskId, qint64 budgetMs,
                                                           qint64 headerBudgetMs) const
{
    // :361-377
    const QVector<FetchCandidate> candidates = fetchCandidates(resolvedUrl, source.lastFetchHost);
    if (candidates.size() == 1) {
        return downloadCandidate(source, candidates[0], taskId, budgetMs, headerBudgetMs);
    }
    return raceCandidates(source, candidates, taskId, budgetMs, headerBudgetMs);
}

Result<std::optional<Downloaded>> Collector::raceCandidates(
    const Models::Source &source, const QVector<FetchCandidate> &candidates, const QString &taskId,
    qint64 budgetMs, qint64 headerBudgetMs) const
{
    // :379-418 —— JoinSet 语义：所有候选并跑，第一个成功的赢、其余不管；全失败才把
    // 各条腿的失败拼起来。没有 fail-fast。
    struct RaceState {
        std::mutex mutex;
        std::condition_variable condition;
        int running = 0;
        bool haveWinner = false;
        Result<std::optional<Downloaded>> winner;
        QString winnerHost;
        QVector<QString> failures;
        /// 每条已起跑的腿一份取消凭据；赢家出现后逐个 cancel（Rust 的 abort_all）。
        QVector<Net::CancelToken> cancels;
    };

    auto state = std::make_shared<RaceState>();
    state->running = int(candidates.size());
    const Collector collector = *this;
    for (const FetchCandidate &candidate : candidates) {
        const FetchCandidate leg = candidate;
        const Models::Source legSource = source;
        const QString legTaskId = taskId;
        auto cancel = std::make_shared<Net::CancelFlag>();
        std::thread([collector, legSource, leg, legTaskId, budgetMs, headerBudgetMs, state,
                     cancel]() {
            Result<std::optional<Downloaded>> outcome = Result<std::optional<Downloaded>>::withError(
                QStringLiteral("request failed: candidate produced no result"));
            // 起跑前赢家可能已经出现（腿是一条条 spawn 的）：那就连请求都不必发。
            bool abandonedBeforeStart = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->haveWinner) {
                    abandonedBeforeStart = true;
                } else {
                    state->cancels.push_back(cancel);
                }
            }
            if (!abandonedBeforeStart) {
                try {
                    outcome = collector.downloadCandidate(legSource, leg, legTaskId, budgetMs,
                                                         headerBudgetMs, cancel);
                } catch (const std::exception &error) {
                    // :414 —— Rust 那边这是 join 错误（子任务 panic / 被取消）。
                    outcome = Result<std::optional<Downloaded>>::withError(Messages::format(
                        Messages::candidateTaskFailed(), {},
                        {{"{error}", QString::fromUtf8(error.what())}}));
                } catch (...) {
                    outcome = Result<std::optional<Downloaded>>::withError(Messages::format(
                        Messages::candidateTaskFailed(), {}, {{"{error}", QStringLiteral("unknown")}}));
                }
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            --state->running;
            // 被主动取消的腿没有"失败"这回事：Rust 那边 abort 掉的任务根本不会返回结果，
            // 把 "已取消" 记进 failures 会让"全失败"那条拼接文案里混进自己人。
            const bool cancelled = outcome.isErr() && outcome.error() == Net::cancelledError();
            if (outcome.isOk()) {
                if (!state->haveWinner) {
                    // 哪怕是 Ok(None)（304）也算这条腿答了。
                    state->haveWinner = true;
                    state->winner = std::move(outcome);
                    state->winnerHost = leg.host;
                    // :404-407 —— abort_all：一锤定音之后其余腿立刻放手，别占着镜像闸（6）
                    // 和同主机闸（2）继续跑完自己的 30s。
                    for (const Net::CancelToken &other : state->cancels) {
                        if (other != cancel) {
                            other->cancel();
                        }
                    }
                }
            } else if (!cancelled && !abandonedBeforeStart) {
                if (leg.mirror) {
                    // :409-413
                    state->failures.push_back(QStringLiteral("%1: %2").arg(leg.host, outcome.error()));
                } else {
                    state->failures.push_back(Messages::format(Messages::directCandidatePrefix(), {},
                                                               {{"{message}", outcome.error()}}));
                }
            }
            state->condition.notify_all();
        }).detach();
    }

    bool haveWinner = false;
    Result<std::optional<Downloaded>> winner;
    QString winnerHost;
    QVector<QString> failures;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->condition.wait(lock,
                              [state] { return state->haveWinner || state->running == 0; });
        haveWinner = state->haveWinner;
        winner = state->winner;
        winnerHost = state->winnerHost;
        failures = state->failures;
    }
    if (haveWinner) {
        // :404-407 —— 赢家那条腿（哪怕是 304）都要记下这次用的来源。落败的腿收到取消哨兵后
        // 自己放手（Net::Request::cancel），闸位随它们各自返回而归还。
        const Status recorded = detail_->db->setSourceFetchHost(source.id, winnerHost);
        if (recorded.isErr()) {
            Log::warn(QStringLiteral("set_source_fetch_host failed: %1").arg(recorded.error()));
        }
        return winner;
    }
    return Result<std::optional<Downloaded>>::withError(failures.join(QStringLiteral(" | ")));
}

Models::FetchResult Collector::fetchSourceTracked(const Models::Source &source,
                                                 const QString &taskId,
                                                 qint64 attemptBudgetMs) const
{
    using Models::FetchResult;
    const QString resolvedUrl = Parser::expandUrlTemplate(source.urlTemplate);

    // :179-184 —— t.me/s/<频道> 走单独一条通道：网页预览、翻页、只增不删。
    const TelegramBridge &bridge = detail_->telegram;
    if (bridge.channelName && bridge.fetch) {
        const QString channel = bridge.channelName(resolvedUrl);
        if (!channel.isNull()) {
            Log::info(QStringLiteral("fetching telegram channel name=%1 channel=%2")
                          .arg(source.name, channel));
            return bridge.fetch(*this, source, channel, taskId, attemptBudgetMs);
        }
    }

    Log::info(QStringLiteral("fetching source name=%1 url=%2").arg(source.name, resolvedUrl));
    phase(taskId, source.id, Tasks::Phase::Downloading);
    QElapsedTimer downloadStarted;
    downloadStarted.start();

    const Result<std::optional<Downloaded>> downloaded =
        downloadSource(source, resolvedUrl, taskId, attemptBudgetMs, kResponseHeaderTimeoutMs);
    if (downloaded.isErr()) {
        const QString message = downloaded.error();
        // :199-210
        Log::warn(QStringLiteral("fetch failed name=%1 error=%2").arg(source.name, message));
        markSourceFetched(source.id, message, 0);
        FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = resolvedUrl;
        result.success = false;
        result.message = message;
        result.nodeCount = 0;
        return result;
    }
    if (!downloaded.value().has_value()) {
        // :211-222 —— 304：不解析、不写库。
        markSourceFetched(source.id, QStringLiteral("not modified"), source.lastNodeCount);
        FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = resolvedUrl;
        result.success = true;
        result.message = QStringLiteral("not modified, skipped parse/write");
        result.nodeCount = int(qMax<qint64>(0, source.lastNodeCount));
        return result;
    }

    const Downloaded payload = downloaded.value().value();
    // :225-233
    timings(taskId, source.id, quint64(downloadStarted.elapsed()), std::nullopt, std::nullopt);

    // :241-244
    const QString viaNote = payload.via.isNull()
        ? QString()
        : Messages::format(Messages::viaMirrorNote(), {}, {{"{host}", payload.via}});
    const QString contentSha256 = Crypto::sha256Hex(payload.body);
    if (sameAsSome(source.lastResolvedUrl, resolvedUrl)
        && sameAsSome(source.contentSha256, contentSha256)) {
        // :246-269 —— 正文哈希没变（比如镜像给了新 ETag 但内容一样），同样不解析不写库。
        const Status cached = detail_->db->updateSourceCache(source.id, resolvedUrl, payload.etag,
                                                            payload.lastModified, contentSha256);
        if (cached.isErr()) {
            Log::warn(QStringLiteral("update_source_cache failed: %1").arg(cached.error()));
        }
        markSourceFetched(source.id, QStringLiteral("content unchanged"), source.lastNodeCount);
        FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = payload.finalUrl;
        result.success = true;
        result.message = QStringLiteral("content unchanged, skipped parse/write") + viaNote;
        result.nodeCount = int(qMax<qint64>(0, source.lastNodeCount));
        return result;
    }

    phase(taskId, source.id, Tasks::Phase::Parsing);
    QElapsedTimer parseStarted;
    parseStarted.start();
    QVector<Models::Node> parsedNodes;
    QString parseError;
    {
        // :273-279 —— PARSE_LIMIT 闸 + spawn_blocking：解析是 CPU 活，最多 4 份并发。
        const Semaphore::Guard permit = parseLimit()->acquireGuard();
        try {
            // Rust 的 String::from_utf8_lossy：坏字节换成 U+FFFD，QString::fromUtf8 同语义。
            parsedNodes = Parser::parseSubscription(QString::fromUtf8(payload.body), source.id);
        } catch (const std::exception &error) {
            parseError = QString::fromUtf8(error.what());
        } catch (...) {
            parseError = QStringLiteral("unknown");
        }
    }
    if (!parseError.isNull()) {
        // :282-293
        const QString message = QStringLiteral("parse task failed: %1").arg(parseError);
        markSourceFetched(source.id, message, 0);
        FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = resolvedUrl;
        result.success = false;
        result.message = message;
        result.nodeCount = 0;
        return result;
    }
    // :295-303
    timings(taskId, source.id, std::nullopt, quint64(parseStarted.elapsed()), std::nullopt);

    const int count = int(parsedNodes.size());
    nodes(taskId, source.id, count);
    phase(taskId, source.id, Tasks::Phase::Writing);
    QElapsedTimer writeStarted;
    writeStarted.start();
    const Result<int> inserted = detail_->db->replaceSourceNodes(source.id, parsedNodes);
    if (inserted.isOk()) {
        // :308-341
        timings(taskId, source.id, std::nullopt, std::nullopt, quint64(writeStarted.elapsed()));
        markSourceFetched(source.id, QStringLiteral("ok"), qint64(count));
        const Status cached =
            detail_->db->updateSourceCache(source.id, resolvedUrl, payload.etag,
                                          payload.lastModified, contentSha256);
        if (cached.isErr()) {
            Log::warn(QStringLiteral("update_source_cache failed: %1").arg(cached.error()));
        }
        FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = payload.finalUrl;
        result.success = true;
        result.message =
            QStringLiteral("ok, parsed=%1, stored=%2").arg(count).arg(inserted.value()) + viaNote;
        result.nodeCount = count;
        return result;
    }
    // :342-353
    const QString message = QStringLiteral("store failed: %1").arg(inserted.error());
    markSourceFetched(source.id, message, 0);
    FetchResult result;
    result.sourceId = source.id;
    result.sourceName = source.name;
    result.resolvedUrl = resolvedUrl;
    result.success = false;
    result.message = message;
    result.nodeCount = count;
    return result;
}

Models::FetchResult Collector::fetchAttempt(const Models::Source &source, const QString &taskId,
                                            qint64 totalBudgetMs, qint64 elapsedMs) const
{
    // :857-867 —— remaining = total_budget.saturating_sub(elapsed)，再兜底 500ms。
    const qint64 remaining = totalBudgetMs > elapsedMs ? totalBudgetMs - elapsedMs : 0;
    return fetchSourceTracked(source, taskId, qMax(remaining, kMinAttemptBudgetMs));
}

Models::FetchResult Collector::fetchSourceInTask(const Models::Source &source,
                                                 const QString &taskId, qint64 totalBudgetMs) const
{
    // :794-854 —— total_budget 是**所有 attempt 共享**的墙钟预算，不是每次一份额：
    // 三次满超时曾经能吃掉 90s 还一直占着主机闸。
    const int maxAttempts = source.failStreak >= kDegradedFailStreak ? 1 : kFetchMaxAttempts;
    QElapsedTimer started;
    started.start();
    int attempts = 1;
    Models::FetchResult result = fetchAttempt(source, taskId, totalBudgetMs, 0);
    for (int index = 0; index + 1 < maxAttempts && index < 2; ++index) {
        const qint64 delay = kFetchRetryDelaysMs[index]; // :33 —— [300ms, 800ms]
        if (result.success || !isRetryableFetchFailure(result.message)) {
            break;
        }
        if (started.elapsed() + delay >= totalBudgetMs) {
            break;
        }
        Log::warn(QStringLiteral(
                      "retrying transient subscription fetch failure source=%1 error=%2 delay_ms=%3")
                      .arg(source.name, result.message)
                      .arg(delay));
        QThread::msleep(static_cast<unsigned long>(delay));
        result = fetchAttempt(source, taskId, totalBudgetMs, qint64(started.elapsed()));
        attempts += 1;
    }
    if (attempts > 1 && !result.success && isRetryableFetchFailure(result.message)) {
        // :829-831
        result.message = Messages::format(Messages::attemptsSuffix(), {result.message},
                                          {{"{attempts}", QString::number(attempts)}});
    }
    // :832-846 —— 只有网络失败才推进 fail_streak；404 / 解析失败不证明主机凉了。
    const bool networkFailure = !result.success && isNetworkFetchFailure(result.message);
    const Result<qint64> streak =
        detail_->db->recordFetchOutcome(source.id, result.success, networkFailure);
    if (streak.isOk() && streak.value() >= kDegradedFailStreak
        && streak.value() != source.failStreak) {
        Log::warn(QStringLiteral("%1 source=%2 streak=%3")
                      .arg(Messages::degradedStreakWarn(), source.name)
                      .arg(streak.value()));
    } else if (streak.isErr()) {
        Log::warn(QStringLiteral("%1 source=%2 error=%3")
                      .arg(Messages::recordOutcomeFailed(), source.name, streak.error()));
    }
    if (result.success) {
        complete(taskId, source.id, result.message);
    } else {
        fail(taskId, source.id, result.message);
    }
    return result;
}

Models::FetchResult Collector::fetchSource(const Models::Source &source) const
{
    // :869-880
    const QString taskId = startTask(Tasks::Kind::Fetch, {{source.id, source.name}});
    const qint64 timeoutMs =
        settingsFetchTimeoutMs(detail_->db->getSettings(), kDefaultFetchTimeoutMs);
    return fetchSourceInTask(source, taskId, timeoutMs);
}

Result<Models::FetchResult> Collector::fetchOne(const QString &sourceId) const
{
    // :974-981
    const Result<Models::Source> fetched = detail_->db->getSource(sourceId);
    if (fetched.isErr()) {
        return Result<Models::FetchResult>::withError(fetched.error());
    }
    const Models::Source source = fetched.value();
    if (source.id.isNull() || source.id.isEmpty()) {
        return Result<Models::FetchResult>::withError(QStringLiteral("source not found"));
    }
    return Result<Models::FetchResult>::withValue(fetchSource(source));
}

Result<Models::FetchResult> Collector::fetchOneWithProxy(const QString &sourceId,
                                                        const QString &proxyUrl) const
{
    // :145-151
    Result<Collector> via = withProxy(proxyUrl);
    if (via.isErr()) {
        return Result<Models::FetchResult>::withError(via.error());
    }
    Collector collector = via.takeValue();
    return collector.fetchOne(sourceId);
}

Result<QVector<Models::FetchResult>> Collector::fetchAllEnabled() const
{
    // :882-885 —— Rust 传的是 None::<fn(usize, usize)>。
    return fetchAllEnabledWithProgress(nullptr);
}

Result<QVector<Models::FetchResult>> Collector::fetchAllEnabledWithProxy(
    const QString &proxyUrl, ProgressCallback progress) const
{
    // :153-161
    Result<Collector> via = withProxy(proxyUrl);
    if (via.isErr()) {
        return Result<QVector<Models::FetchResult>>::withError(via.error());
    }
    Collector collector = via.takeValue();
    return collector.fetchAllEnabledWithProgress(progress);
}

Result<QVector<Models::FetchResult>> Collector::fetchAllEnabledWithProgress(
    ProgressCallback progress) const
{
    // :887-972
    const Result<QVector<Models::Source>> listed = detail_->db->listSources();
    if (listed.isErr()) {
        return Result<QVector<Models::FetchResult>>::withError(listed.error());
    }
    QVector<Models::Source> sources;
    for (const Models::Source &source : listed.value()) {
        if (source.enabled) {
            sources.push_back(source);
        }
    }
    const int total = int(sources.size());
    const Models::AppSettings settings =
        detail_->db->getSettings().valueOr(Models::AppSettings());
    const int fetchConcurrency = std::clamp(settings.fetchConcurrency, 1, 8);
    const qint64 requestTimeoutMs = qint64(std::clamp(settings.fetchTimeoutSecs, 10, 120)) * 1000;

    QVector<std::pair<QString, QString>> taskSources;
    QVector<SourceMeta> metadata;
    taskSources.reserve(total);
    metadata.reserve(total);
    for (const Models::Source &source : sources) {
        taskSources.emplace_back(source.id, source.name);
        metadata.push_back(SourceMeta{source.id, source.name,
                                      Parser::expandUrlTemplate(source.urlTemplate)});
    }
    const QString taskId = startTask(Tasks::Kind::Fetch, taskSources);

    auto results = std::make_shared<QVector<std::optional<Models::FetchResult>>>(total);
    auto done = std::make_shared<std::atomic<qint64>>(0);
    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, fetchConcurrency));
    const Collector collector = *this;
    for (int index = 0; index < total; ++index) {
        const Models::Source source = sources[index];
        // :924-939 —— Rust 只保持 fetch_concurrency 条在飞，QThreadPool 的 maxThreadCount
        // 就是同一件事。
        pool.start([collector, source, taskId, index, requestTimeoutMs, results, done, progress,
                    total]() {
            Models::FetchResult result;
            bool ok = true;
            try {
                result = collector.fetchSourceInTask(source, taskId, requestTimeoutMs);
            } catch (...) {
                // 异常不许跨线程：真出了事就按"这一路没有结果"处理，最后补成失败。
                ok = false;
            }
            if (ok) {
                (*results)[index] = std::optional<Models::FetchResult>(std::move(result));
            }
            const qint64 finished = done->fetch_add(1) + 1;
            if (progress) {
                progress(finished, qint64(total));
            }
        });
    }
    pool.waitForDone();

    QVector<Models::FetchResult> ordered;
    ordered.reserve(total);
    for (int index = 0; index < total; ++index) {
        if ((*results)[index].has_value()) {
            ordered.push_back((*results)[index].value());
            continue;
        }
        // :956-969 —— 缺结果也要给出这一路的失败，不许整批塌掉。
        const SourceMeta &meta = metadata[index];
        const QString message = Messages::subtaskExited();
        fail(taskId, meta.sourceId, message);
        Models::FetchResult result;
        result.sourceId = meta.sourceId;
        result.sourceName = meta.sourceName;
        result.resolvedUrl = meta.resolvedUrl;
        result.success = false;
        result.message = message;
        result.nodeCount = 0;
        ordered.push_back(result);
    }
    return Result<QVector<Models::FetchResult>>::withValue(std::move(ordered));
}

// ---- Gist 发现 ----

Result<GistDiscoverySummary> Collector::discoverGists(const QString &query, int maxPages) const
{
    // :524-535 —— 搜索页只能直连或走本地代理，没有镜像可赛跑。
    return discoverGistsFrom(buildGistSearchUrl(query), QString::fromLatin1(kGistRawBase),
                             maxPages);
}

GistBatchTally Collector::registerGistBatch(const QVector<DiscoveredGist> &gists,
                                           const QString &rawBase, const QString &parentTaskId,
                                           QSet<QString> &seen) const
{
    // :664-744 —— 一页内的 gist 并发试拉（页大小就是并发上限），没有 fail-fast。
    // Rust 把 parentTaskId 带进每条腿但只用它派生 child_task，这里同样只是留着形参。
    (void)parentTaskId;
    struct LegState {
        std::mutex mutex;
        QVector<std::tuple<DiscoveredGist, qint64, GistOutcome>> outcomes;
    };
    auto state = std::make_shared<LegState>();
    std::vector<std::thread> legs; // QVector 不肯装 move-only 的 std::thread
    legs.reserve(gists.size());
    const Collector collector = *this;
    for (const DiscoveredGist &gist : gists) {
        const DiscoveredGist legGist = gist;
        const QString legRawBase = rawBase;
        legs.push_back(std::thread([collector, legGist, legRawBase, state]() {
            Models::CreateSource input;
            input.name = legGist.name;
            input.urlTemplate =
                QStringLiteral("%1/%2/%3/raw").arg(legRawBase, legGist.user, legGist.id);
            input.enabled = true;
            input.remark = Messages::gistRemarkAuto();
            const Result<Models::Source> created = collector.detail_->db->createSource(input);
            if (created.isErr()) {
                Log::warn(QStringLiteral("%1 gist=%2 error=%3")
                              .arg(Messages::gistRegisterFailed(), legGist.id, created.error()));
                std::lock_guard<std::mutex> lock(state->mutex);
                state->outcomes.push_back(std::make_tuple(legGist, qint64(0), GistOutcome::Failed));
                return;
            }
            const Models::Source source = created.value();
            const QString childTask = collector.startTask(
                Tasks::Kind::Fetch, {{source.id, source.name}});
            const Models::FetchResult result =
                collector.fetchSourceInTask(source, childTask, kDefaultFetchTimeoutMs);
            std::tuple<DiscoveredGist, qint64, GistOutcome> outcome{
                legGist, qint64(0), GistOutcome::Unusable};
            if (result.success && result.nodeCount > 0) {
                outcome = std::make_tuple(legGist, qint64(result.nodeCount), GistOutcome::Registered);
            } else if (isNetworkFetchFailure(result.message)) {
                // :703-706 —— 网络原因失败不算"无节点"，删掉源，下轮重试。
                collector.detail_->db->deleteSource(source.id);
                outcome = std::make_tuple(legGist, qint64(0), GistOutcome::Pending);
            } else {
                // :707-709
                collector.detail_->db->deleteSource(source.id);
                outcome = std::make_tuple(legGist, qint64(0), GistOutcome::Unusable);
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            state->outcomes.push_back(outcome);
        }));
    }
    for (std::thread &leg : legs) {
        leg.join();
    }

    GistBatchTally tally;
    for (const std::tuple<DiscoveredGist, qint64, GistOutcome> &leg : state->outcomes) {
        const DiscoveredGist &gist = std::get<0>(leg);
        switch (std::get<2>(leg)) {
        case GistOutcome::Registered:
            // :722-729 —— 台账记下"这条 gist 处理过了"，之后用户删掉源也不会被加回来。
            detail_->db->markGistSeen(gist.id, QStringLiteral("registered"));
            seen.insert(gist.id);
            tally.registered += 1;
            tally.nodes += std::get<1>(leg);
            break;
        case GistOutcome::Unusable:
            detail_->db->markGistSeen(gist.id, QStringLiteral("unusable"));
            seen.insert(gist.id);
            tally.rejected += 1;
            break;
        case GistOutcome::Pending:
            tally.pending += 1;
            break;
        case GistOutcome::Failed:
            break;
        }
    }
    return tally;
}

Result<GistDiscoverySummary> Collector::discoverGistsFrom(const QString &firstPageUrl,
                                                          const QString &rawBase,
                                                          int maxPages) const
{
    // :537-661
    const int pages = std::clamp(maxPages, 1, kGistMaxPagesHard);
    const Models::Source scratch = Models::Source::scratch(
        QString::fromLatin1(kGistDiscoverySourceId), Messages::gistDiscoveryName(), firstPageUrl);
    const QString taskId = startTask(
        Tasks::Kind::Fetch, {{scratch.id, Messages::gistDiscoveryName()}});

    Result<QSet<QString>> seenResult = detail_->db->seenGistIds();
    if (seenResult.isErr()) {
        return Result<GistDiscoverySummary>::withError(seenResult.error());
    }
    QSet<QString> seen = seenResult.takeValue();
    const Result<QVector<Models::Source>> listed = detail_->db->listSources();
    if (listed.isErr()) {
        return Result<GistDiscoverySummary>::withError(listed.error());
    }
    QVector<QString> knownTemplates;
    for (const Models::Source &existing : listed.value()) {
        knownTemplates.push_back(existing.urlTemplate);
    }

    GistDiscoverySummary summary;
    QString pageUrl = firstPageUrl;
    for (;;) {
        summary.pagesScanned += 1;
        const Result<std::optional<Downloaded>> page =
            downloadSource(scratch, pageUrl, taskId, kDefaultFetchTimeoutMs,
                           kGistSearchHeaderTimeoutMs);
        if (page.isErr()) {
            // :586-592
            summary.stoppedReason = Messages::format(
                Messages::gistPageFetchFailed(), {QString::number(summary.pagesScanned)},
                {{"{error}", page.error()}});
            break;
        }
        if (!page.value().has_value()) {
            // :581-585
            summary.stoppedReason = Messages::format(Messages::gistPageNotModified(),
                                                    {QString::number(summary.pagesScanned)});
            break;
        }
        const QVector<DiscoveredGist> gists = extractGists(QString::fromUtf8(page.value()->body));
        if (gists.isEmpty()) {
            // :594-598
            summary.stoppedReason = Messages::format(Messages::gistPageNoGists(),
                                                    {QString::number(summary.pagesScanned)});
            break;
        }
        const qint64 pageTotal = gists.size();
        QVector<DiscoveredGist> fresh;
        for (const DiscoveredGist &gist : gists) {
            // :600-609 —— 台账里有过、或者已经存在同一条 gist 的源，都算"已扫描过"。
            const QString marker = QStringLiteral("/%1/%2").arg(gist.user, gist.id);
            if (seen.contains(gist.id)) {
                continue;
            }
            bool known = false;
            for (const QString &templateText : knownTemplates) {
                if (templateText.contains(marker)) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                fresh.push_back(gist);
            }
        }
        summary.skipped += pageTotal - qint64(fresh.size());
        if (fresh.isEmpty()) {
            // :611-615
            summary.stoppedReason = Messages::format(Messages::gistPageAllSeen(),
                                                    {QString::number(summary.pagesScanned)});
            break;
        }
        const GistBatchTally tally = registerGistBatch(fresh, rawBase, taskId, seen);
        summary.registered += tally.registered;
        summary.nodes += tally.nodes;
        summary.rejected += tally.rejected;
        summary.pending += tally.pending;
        Log::info(QStringLiteral("%1 page=%2 registered=%3 nodes=%4 rejected=%5 pending=%6")
                      .arg(Messages::gistPageHandled())
                      .arg(summary.pagesScanned)
                      .arg(tally.registered)
                      .arg(tally.nodes)
                      .arg(tally.rejected)
                      .arg(tally.pending));
        if (summary.pagesScanned >= pages) {
            summary.stoppedReason = Messages::format(
                Messages::gistPageCap(), {}, {{"{max_pages}", QString::number(pages)}});
            break;
        }
        if (summary.registered >= kGistMaxNewPerRun) {
            summary.stoppedReason = Messages::format(
                Messages::gistRegisterCap(), {},
                {{"{GIST_MAX_NEW_PER_RUN}", QString::number(kGistMaxNewPerRun)}});
            break;
        }
        // :635-639 —— query_pairs_mut().append_pair("page", pages_scanned + 1)
        const QString next = appendQueryParam(pageUrl, QStringLiteral("page"),
                                             QString::number(summary.pagesScanned + 1));
        if (next.isNull()) {
            return Result<GistDiscoverySummary>::withError(Messages::format(
                Messages::gistSearchUrlUnparsable(), {},
                {{"{error}", QStringLiteral("invalid url: %1").arg(pageUrl)}}));
        }
        pageUrl = next;
    }
    if (summary.stoppedReason.isEmpty()) {
        summary.stoppedReason = Messages::gistScanComplete();
    }
    const QString message = Messages::format(
        Messages::gistDiscoveryFinished(),
        {QString::number(summary.pagesScanned), QString::number(summary.registered),
         QString::number(summary.nodes), QString::number(summary.skipped),
         QString::number(summary.rejected), QString::number(summary.pending),
         summary.stoppedReason});
    Log::info(QStringLiteral("gist discovery finished source=%1 message=%2")
                  .arg(QString::fromLatin1(kGistDiscoverySourceId), message));
    complete(taskId, QString::fromLatin1(kGistDiscoverySourceId), message);
    return Result<GistDiscoverySummary>::withValue(summary);
}

} // namespace Collector
