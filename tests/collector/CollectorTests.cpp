#include <gtest/gtest.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "collector/Collector.h"
#include "collector/CollectorMessages.h"
#include "collector/FetchLimits.h"
#include "collector/GistDiscovery.h"
#include "core/Ids.h"
#include "db/Database.h"
#include "models/Settings.h"
#include "parser/Common.h"
#include "tasks/TaskTracker.h"

// 对应 vpn-parser/src/collector/mod.rs 的 #[cfg(test)] mod tests：15 个用例名字保持
// Rust 原样（那个 #[ignore] 的真实网络用例移植成 DISABLED_，默认不跑）。
//
// 【测试不碰网络】Rust 那边起的是 axum 本地服务器，这里换成注入 transport seam：
// 断言的是 URL / 条件请求头 / 重试次数 / fail_streak / 并发闸这些可观察行为，
// canned response 的形状照抄 Rust 测试里的 fixture（同一条 ss:// 正文、同一套
// gist 搜索页 HTML、同样的 503→200 序列）。库文件一律开在 QTemporaryDir 里，
// 绝不碰 D:/software/VPN Parser/（那边有 Rust 实例在跑）。
namespace {

const char *kNodeBody = "ss://YWVzLTEyOC1nY206cGFzcw==@1.2.3.4:443#node";

/// 每个用例一个独立主机名：按主机闸是进程级共享的（Rust 那边用 [::1] 和 127.0.0.1
/// 区分），这里用随机子域保证用例之间不互相排队。
QString testHost(const QString &prefix)
{
    return QStringLiteral("%1-%2.invalid").arg(prefix, Ids::newUuidV4Simple().left(8));
}

QString testUrl(const QString &prefix, const QString &path = QStringLiteral("/subscription"))
{
    return QStringLiteral("http://%1%2").arg(testHost(prefix), path);
}

/// Rust 测试里的 axum Response::builder()：状态码 + 正文 + 头部。
Net::Response canned(int status, const QByteArray &body = QByteArray(),
                     const QList<Net::Header> &headers = {})
{
    Net::Response response;
    response.status = status;
    response.body = body;
    response.headers = headers;
    return response;
}

/// 注入 transport 的观察点：每个请求都记下来，同时统计并发峰值。
struct FakeState {
    std::mutex mutex;
    QVector<Net::Request> requests;
    int inFlight = 0;
    int peak = 0;
};

using Router = std::function<Result<Net::Response>(const Net::Request &request, FakeState &state)>;

Collector::Collector::Transport makeTransport(const std::shared_ptr<FakeState> &state, Router router)
{
    return [state, router](const Net::Request &request) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->requests.push_back(request);
            state->inFlight += 1;
            state->peak = std::max(state->peak, state->inFlight);
        }
        Result<Net::Response> result = router(request, *state);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->inFlight -= 1;
        }
        return result;
    };
}

/// 挂住请求但不挂死：等 release() 或等满 maxMs 自己退（分离线程不能在进程退出前卡住）。
struct Hold {
    std::mutex mutex;
    std::condition_variable condition;
    bool released = false;

    void wait(std::chrono::milliseconds max)
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, max, [this] { return released; });
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        condition.notify_all();
    }
};

std::shared_ptr<Database> openDatabase(const QTemporaryDir &dir)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("vpn-parser-test.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    EXPECT_TRUE(opened.isOk()) << opened.error().toStdString();
    return opened.valueOr(nullptr);
}

Models::Source createSource(const std::shared_ptr<Database> &db, const QString &name,
                            const QString &urlTemplate, bool enabled = true)
{
    Models::CreateSource input;
    input.name = name;
    input.urlTemplate = urlTemplate;
    input.enabled = enabled;
    Result<Models::Source> created = db->createSource(input);
    EXPECT_TRUE(created.isOk()) << created.error().toStdString();
    return created.valueOr(Models::Source());
}

/// 已有库记录的源重读一遍（对应 Rust 的 db.get_source(..).unwrap().unwrap()）。
Models::Source reload(const std::shared_ptr<Database> &db, const QString &id)
{
    Result<Models::Source> fetched = db->getSource(id);
    EXPECT_TRUE(fetched.isOk()) << fetched.error().toStdString();
    return fetched.takeValue();
}

int requestCount(const std::shared_ptr<FakeState> &state)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    return int(state->requests.size());
}

QVector<Net::Request> recordedRequests(const std::shared_ptr<FakeState> &state)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->requests;
}

/// "已有" —— Rust 的 gist 发现用例里那条已存在源的中文名（按码点构造，别手打）。
QString existingLabel()
{
    return Parser::Common::ucs4({0x5DF2, 0x770B});
}

// ---- 用例 ----

/// 对应 Rust `rejects_declared_source_body_over_limit`（:1140）。
TEST(CollectorTests, rejects_declared_source_body_over_limit)
{
    ASSERT_EQ(Collector::kDefaultFetchTimeoutMs, 30 * 1000);
    ASSERT_EQ(Collector::kConnectTimeoutSecs, 3);
    ASSERT_EQ(Collector::kBodyIdleTimeoutMs, 5 * 1000);
    ASSERT_EQ(Collector::kResponseHeaderTimeoutMs, 6 * 1000);
    ASSERT_EQ(Collector::kDegradedFailStreak, 3);
    ASSERT_FALSE(Collector::sourceBodyTooLarge(std::optional<qint64>(16 * 1024 * 1024)));
    ASSERT_FALSE(Collector::sourceBodyTooLarge(std::optional<qint64>(Collector::kMaxSourceBytes)));
    ASSERT_TRUE(
        Collector::sourceBodyTooLarge(std::optional<qint64>(Collector::kMaxSourceBytes + 1)));
    ASSERT_FALSE(Collector::sourceBodyTooLarge(std::nullopt));
}

/// 对应 Rust `retries_only_transient_fetch_failures`（:1153）。
TEST(CollectorTests, retries_only_transient_fetch_failures)
{
    for (const QString &message : {QStringLiteral("HTTP 408 Request Timeout"),
                                   QStringLiteral("HTTP 429 Too Many Requests"),
                                   QStringLiteral("HTTP 500 Internal Server Error"),
                                   QStringLiteral("request failed: connection reset"),
                                   QStringLiteral("body read failed: timeout")}) {
        ASSERT_TRUE(Collector::isRetryableFetchFailure(message)) << message.toStdString();
    }
    for (const QString &message : {QStringLiteral("HTTP 404 Not Found"),
                                   QStringLiteral("parse task failed"),
                                   QStringLiteral("no nodes parsed")}) {
        ASSERT_FALSE(Collector::isRetryableFetchFailure(message)) << message.toStdString();
    }
}

/// 对应 Rust `transient_http_failure_is_retried_and_task_finishes`（:1169）。
TEST(CollectorTests, transient_http_failure_is_retried_and_task_finishes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    auto served = std::make_shared<std::atomic<int>>(0);
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [served](const Net::Request &, FakeState &) {
        if (served->fetch_add(1) == 0) {
            return Result<Net::Response>::withValue(canned(503));
        }
        return Result<Net::Response>::withValue(
            canned(200, QByteArray(kNodeBody), QList<Net::Header>()));
    }));

    const Models::Source source = createSource(db, QStringLiteral("retry"),
                                               testUrl(QStringLiteral("retry")));
    const Models::FetchResult result = collector.fetchSource(source);

    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_EQ(served->load(), 2);
    const QVector<Tasks::Snapshot> snapshots = tracker.snapshots(1);
    ASSERT_EQ(snapshots.size(), 1);
    ASSERT_EQ(snapshots[0].successfulSources, 1u);
    ASSERT_TRUE(snapshots[0].finishedAtNanos.has_value());
}

/// 对应 Rust `only_network_failures_count_towards_the_fail_streak`（:1221）。
TEST(CollectorTests, only_network_failures_count_towards_the_fail_streak)
{
    const QVector<QString> networkFailures = {
        Collector::Messages::format(Collector::Messages::responseHeaderTimeout(),
                                    {QStringLiteral("6")}),
        QStringLiteral("request timeout after 30s"),
        QStringLiteral("request failed: error sending request for url"),
        QStringLiteral("read body failed: timeout"),
    };
    for (const QString &message : networkFailures) {
        ASSERT_TRUE(Collector::isNetworkFetchFailure(message)) << message.toStdString();
        ASSERT_TRUE(Collector::isRetryableFetchFailure(message)) << message.toStdString();
    }
    const QVector<QString> notNetwork = {
        QStringLiteral("HTTP 404 Not Found"),
        QStringLiteral("parse task failed"),
        QStringLiteral("no nodes parsed from subscription"),
        Collector::bodyTooLargeMessage(80.0),
    };
    for (const QString &message : notNetwork) {
        ASSERT_FALSE(Collector::isNetworkFetchFailure(message)) << message.toStdString();
    }
    ASSERT_EQ(Collector::bodyTooLargeMessage(80.0),
              QStringLiteral("response body too large: 80.0 MiB (limit 64 MiB)"));
}

/// 对应 Rust `hanging_source_is_capped_by_the_total_budget`（:1242）。
TEST(CollectorTests, hanging_source_is_capped_by_the_total_budget)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto hold = std::make_shared<Hold>();
    auto state = std::make_shared<FakeState>();
    auto served = std::make_shared<std::atomic<int>>(0);
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [served, hold](const Net::Request &, FakeState &) {
        served->fetch_add(1);
        // 收下连接但永远不答：connect_timeout 抓不到的那种挂死。
        hold->wait(std::chrono::seconds(2));
        return Result<Net::Response>::withValue(canned(200, QByteArray()));
    }));

    const Models::Source source = createSource(db, QStringLiteral("hang"),
                                               testUrl(QStringLiteral("hang")));
    const QString taskId = tracker.start(Tasks::Kind::Fetch, {{source.id, source.name}});
    const qint64 budgetMs = 600;

    QElapsedTimer started;
    started.start();
    const Models::FetchResult result = collector.fetchSourceInTask(source, taskId, budgetMs);

    hold->release();
    ASSERT_FALSE(result.success) << result.message.toStdString();
    ASSERT_LT(qint64(started.elapsed()), budgetMs + 800)
        << "总预算被突破: " << qint64(started.elapsed());
    ASSERT_EQ(served->load(), 1);
    ASSERT_EQ(reload(db, source.id).failStreak, 1);
}

/// 对应 Rust `network_failure_counts_one_streak_per_fetch`（:1303）。
TEST(CollectorTests, network_failure_counts_one_streak_per_fetch)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    // 端口死了 / 连不上：Rust 那边是 reqwest 的连接错误，这里直接给传输层 Err。
    collector.setTransport(makeTransport(state, [](const Net::Request &, FakeState &) {
        return Result<Net::Response>::withError(QStringLiteral("connection refused"));
    }));

    const Models::Source source = createSource(db, QStringLiteral("dead"),
                                               testUrl(QStringLiteral("dead")));
    const QString taskId = tracker.start(Tasks::Kind::Fetch, {{source.id, source.name}});
    const Models::FetchResult result = collector.fetchSourceInTask(source, taskId, 5000);

    ASSERT_FALSE(result.success) << result.message.toStdString();
    ASSERT_TRUE(result.message.contains(QLatin1String("request failed")))
        << result.message.toStdString();
    // 一轮拉取只推进一次连击，不管它试了几次。
    ASSERT_EQ(reload(db, source.id).failStreak, 1);
    Result<qint64> reset = db->recordFetchOutcome(source.id, true, false);
    ASSERT_TRUE(reset.isOk()) << reset.error().toStdString();
    ASSERT_EQ(reset.value(), 0);
}

/// 对应 Rust `degraded_source_gets_a_single_attempt`（:1354）。
TEST(CollectorTests, degraded_source_gets_a_single_attempt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    auto served = std::make_shared<std::atomic<int>>(0);
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [served](const Net::Request &, FakeState &) {
        served->fetch_add(1);
        return Result<Net::Response>::withValue(canned(503));
    }));

    const Models::Source source = createSource(db, QStringLiteral("flaky"),
                                               testUrl(QStringLiteral("flaky")));
    const QString taskId = tracker.start(Tasks::Kind::Fetch, {{source.id, source.name}});
    const qint64 budgetMs = 5000;

    Models::FetchResult result = collector.fetchSourceInTask(source, taskId, budgetMs);
    ASSERT_FALSE(result.success) << result.message.toStdString();
    ASSERT_EQ(served->load(), Collector::kFetchMaxAttempts);
    const QString attemptsNote = Collector::Messages::format(
        Collector::Messages::attemptsSuffix(), {QString()},
        {{"{attempts}", QString::number(Collector::kFetchMaxAttempts)}});
    ASSERT_TRUE(result.message.contains(attemptsNote)) << result.message.toStdString();
    // 5xx 是"暂时不行"，不是"这台凉了"：不许喂连击计数。
    ASSERT_EQ(reload(db, source.id).failStreak, 0);

    for (int index = 0; index < int(Collector::kDegradedFailStreak); ++index) {
        Result<qint64> bumped = db->recordFetchOutcome(source.id, false, true);
        ASSERT_TRUE(bumped.isOk()) << bumped.error().toStdString();
    }
    const Models::Source degraded = reload(db, source.id);
    ASSERT_EQ(degraded.failStreak, Collector::kDegradedFailStreak);
    result = collector.fetchSourceInTask(degraded, taskId, budgetMs);
    ASSERT_FALSE(result.success) << result.message.toStdString();
    ASSERT_EQ(served->load(), Collector::kFetchMaxAttempts + 1)
        << "降权后只应再请求一次";
}

/// 对应 Rust `github_raw_urls_get_mirror_candidates`（:1432）。
TEST(CollectorTests, github_raw_urls_get_mirror_candidates)
{
    const QString raw = QStringLiteral("https://raw.githubusercontent.com/anaer/Sub/main/clash.yaml");
    const QVector<Collector::FetchCandidate> candidates = Collector::fetchCandidates(raw, QString());
    ASSERT_EQ(candidates.size(), 1 + Collector::kGithubRawMirrors.size());
    ASSERT_FALSE(candidates[0].mirror);
    ASSERT_EQ(candidates[0].url, raw);
    ASSERT_EQ(candidates[1].url, QStringLiteral("https://%1/%2")
                                    .arg(Collector::kGithubRawMirrors[0], raw));
    ASSERT_TRUE(candidates[1].mirror);

    // 上次赢家排第一，但其余候选一个不少。
    const QString winner = Collector::kGithubRawMirrors[1];
    const QVector<Collector::FetchCandidate> reordered =
        Collector::fetchCandidates(raw, winner);
    ASSERT_EQ(reordered[0].host, winner);
    ASSERT_EQ(reordered.size(), candidates.size());

    const QVector<Collector::FetchCandidate> other =
        Collector::fetchCandidates(QStringLiteral("https://example.com/sub.yaml"), QString());
    ASSERT_EQ(other.size(), 1);
    ASSERT_FALSE(other[0].mirror);
}

/// 对应 Rust `race_keeps_the_first_candidate_that_answers`（:1456）。
TEST(CollectorTests, race_keeps_the_first_candidate_that_answers)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto hold = std::make_shared<Hold>();
    auto state = std::make_shared<FakeState>();
    const QString slowUrl = testUrl(QStringLiteral("race-slow"));
    const QString fastUrl = testUrl(QStringLiteral("race-fast"));
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [slowUrl, fastUrl, hold](
                                                   const Net::Request &request, FakeState &) {
        if (request.url == slowUrl) {
            // 收下连接但不回答：raw.githubusercontent.com 被墙时的样子。
            hold->wait(std::chrono::seconds(2));
            return Result<Net::Response>::withError(QStringLiteral("request failed: hung peer"));
        }
        return Result<Net::Response>::withValue(canned(200, QByteArray(kNodeBody)));
    }));

    const Models::Source source = createSource(db, QStringLiteral("race"), slowUrl);
    const QString taskId = tracker.start(Tasks::Kind::Fetch, {{source.id, source.name}});
    const QVector<Collector::FetchCandidate> candidates = {
        Collector::FetchCandidate{slowUrl, QStringLiteral("direct"), false},
        Collector::FetchCandidate{fastUrl, QStringLiteral("mirror.test"), true},
    };

    QElapsedTimer started;
    started.start();
    Result<std::optional<Collector::Downloaded>> downloaded = collector.raceCandidates(
        source, candidates, taskId, 6000, Collector::kResponseHeaderTimeoutMs);
    hold->release();

    ASSERT_TRUE(downloaded.isOk()) << downloaded.error().toStdString();
    ASSERT_TRUE(downloaded.value().has_value()) << "候选应返回响应体";
    ASSERT_FALSE(downloaded.value()->body.isEmpty());
    ASSERT_EQ(downloaded.value()->via, QStringLiteral("mirror.test"));
    // 竞速的意义就在于不等慢的那条腿跑完预算。
    ASSERT_LT(qint64(started.elapsed()), qint64(3000)) << qint64(started.elapsed());
    ASSERT_EQ(reload(db, source.id).lastFetchHost, QStringLiteral("mirror.test"));
}

/// 对应 Rust `extract_gists_reads_snippet_blocks`（:1539）。
TEST(CollectorTests, extract_gists_reads_snippet_blocks)
{
    const QString html = QString::fromLatin1(
        R"RE(<div class="gist-snippet"><a href="/u_existing/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa">2 files</a></div>)RE"
        R"RE(<div class="gist-snippet"><a href="/u_good/bbbbbbbbbbbbbbbbbbbbbbbbbbbb">clash.yaml</a></div>)RE"
        R"RE(<div class="gist-snippet"><a href="/u_empty/cccccccccccccccccccccccccccc">list.txt</a></div>)RE"
        // 子路径链接（forks/stars）不能当成 gist。
        R"RE(<div class="gist-snippet"><a href="/u_multi/dddddddddddddddddddddddddddd">1 files</a>)RE"
        R"RE(<a href="/u_multi/dddddddddddddddddddddddddddd/stargazers">3 stars</a>)RE"
        R"RE(<a href="/u_multi/dddddddddddddddddddddddddddd">nodes.list</a></div>)RE");
    const QVector<Collector::DiscoveredGist> gists = Collector::extractGists(html);
    ASSERT_EQ(gists.size(), 4);
    ASSERT_EQ(gists[0].user, QStringLiteral("u_existing"));
    ASSERT_EQ(gists[0].name, QStringLiteral("gist-u_existing-aaaaaaaa"));
    ASSERT_EQ(gists[1].name, QStringLiteral("clash.yaml"));
    ASSERT_EQ(gists[2].name, QStringLiteral("list.txt"));
    ASSERT_TRUE(gists[2].id.startsWith(QLatin1Char('c')) && gists[2].id.size() >= 16);
    // 同一块里的 /stargazers 子路径不会变成另一个 gist；文件名取块内的 .list。
    ASSERT_TRUE(gists[3].id.startsWith(QLatin1Char('d')) && gists[3].id.size() >= 16);
    ASSERT_EQ(gists[3].name, QStringLiteral("nodes.list"));
}

/// 对应 Rust `gist_discovery_keeps_only_sources_that_parse_nodes`（:1570）。
TEST(CollectorTests, gist_discovery_keeps_only_sources_that_parse_nodes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    const QString idA = QString(32, QLatin1Char('a'));
    const QString idB = QString(32, QLatin1Char('b'));
    const QString idC = QString(32, QLatin1Char('c'));
    const QString idD = QString(32, QLatin1Char('d'));
    const QString page1 = QString::fromLatin1(
        R"RE(<div class="gist-snippet"><a href="/u_existing/)RE")
        .append(idA)
        .append(R"RE(">2 files</a></div>)RE")
        .append(R"RE(<div class="gist-snippet"><a href="/u_good/)RE")
        .append(idB)
        .append(R"RE(">good.yaml</a></div>)RE")
        .append(R"RE(<div class="gist-snippet"><a href="/u_empty/)RE")
        .append(idC)
        .append(R"RE(">empty.txt</a></div>)RE");
    const QString page2 = QString::fromLatin1(
        R"RE(<div class="gist-snippet"><a href="/u_multi/)RE")
        .append(idD)
        .append(R"RE(">nodes.list</a></div>)RE");

    auto state = std::make_shared<FakeState>();
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    // 照抄 Rust 用例的路由：/u_empty/ 给一段解不出节点的文字，非 /raw 给搜索页，
    // /raw 给一条 ss:// 链接；page=2 是第二页，page=3 是空页。
    collector.setTransport(makeTransport(
        state, [page1, page2](const Net::Request &request, FakeState &) {
            const QString url = request.url;
            if (url.contains(QLatin1String("/u_empty/"))) {
                return Result<Net::Response>::withValue(
                    canned(200, QByteArray("not a subscription, just text")));
            }
            if (!url.contains(QLatin1String("/raw"))) {
                if (url.contains(QLatin1String("page=3"))) {
                    return Result<Net::Response>::withValue(canned(200, QByteArray()));
                }
                if (url.contains(QLatin1String("page=2"))) {
                    return Result<Net::Response>::withValue(
                        canned(200, page2.toUtf8()));
                }
                return Result<Net::Response>::withValue(canned(200, page1.toUtf8()));
            }
            return Result<Net::Response>::withValue(canned(200, QByteArray(kNodeBody)));
        }));

    // 已存在的源：同 id 的 gist 要被跳过。
    const QString base = testUrl(QStringLiteral("gist"), QString());
    createSource(db, existingLabel(),
                 QStringLiteral("https://gist.githubusercontent.com/u_existing/%1/raw").arg(idA));

    const QString firstPage = QStringLiteral("%1/search?q=vless%2F%2F").arg(base);
    Result<Collector::GistDiscoverySummary> summary =
        collector.discoverGistsFrom(firstPage, base, 100);
    ASSERT_TRUE(summary.isOk()) << summary.error().toStdString();
    Collector::GistDiscoverySummary page = summary.takeValue();

    ASSERT_EQ(page.pagesScanned, 3) << page.stoppedReason.toStdString();
    ASSERT_EQ(page.registered, 2);
    ASSERT_EQ(page.nodes, 2);
    ASSERT_EQ(page.skipped, 1);
    ASSERT_EQ(page.rejected, 1);
    ASSERT_TRUE(page.stoppedReason.contains(
        Collector::Messages::format(Collector::Messages::gistPageNoGists(), {QStringLiteral("3")})))
        << page.stoppedReason.toStdString();
    QStringList names;
    const Result<QVector<Models::Source>> afterFirst = db->listSources();
    ASSERT_TRUE(afterFirst.isOk());
    for (const Models::Source &existing : afterFirst.value()) {
        names.push_back(existing.name);
    }
    ASSERT_TRUE(names.contains(QStringLiteral("good.yaml")));
    ASSERT_TRUE(names.contains(QStringLiteral("nodes.list")));
    ASSERT_FALSE(names.contains(QStringLiteral("empty.txt")));
    ASSERT_EQ(db->seenGistIds().value().size(), 3);

    // 第二轮：整页都已见过，第 1 页就停。
    Result<Collector::GistDiscoverySummary> again =
        collector.discoverGistsFrom(firstPage, base, 100);
    ASSERT_TRUE(again.isOk()) << again.error().toStdString();
    Collector::GistDiscoverySummary second = again.takeValue();
    ASSERT_EQ(second.pagesScanned, 1);
    ASSERT_EQ(second.registered, 0);
    ASSERT_EQ(second.skipped, 3);
    ASSERT_TRUE(second.stoppedReason.contains(
        Collector::Messages::format(Collector::Messages::gistPageAllSeen(),
                                    {QStringLiteral("1")})))
        << second.stoppedReason.toStdString();
}

/// 对应 Rust `real_network_gist_discovery_via_local_proxy`（:1678，#[ignore]）：
/// 真实网络手测，需要一个能出境的本地代理在线。gtest 的 DISABLED_ 前缀 = 默认不跑，
/// 跑法是 `--gtest_also_run_disabled_tests`，对应 Rust 的
/// `cargo test real_network -- --ignored --nocapture`。
TEST(CollectorTests, DISABLED_real_network_gist_discovery_via_local_proxy)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);
    const QString proxy = [] {
        const QByteArray fromEnv = qgetenv("VCC_PROBE_PROXY");
        return fromEnv.isEmpty() ? QStringLiteral("http://127.0.0.1:9980")
                                 : QString::fromLocal8Bit(fromEnv);
    }();
    Result<Collector::Collector> base =
        Collector::Collector(db, Tasks::TaskTracker()).withProxy(proxy);
    ASSERT_TRUE(base.isOk()) << base.error().toStdString();
    Collector::Collector collector = base.takeValue();

    Result<Collector::GistDiscoverySummary> summary =
        collector.discoverGists(QString::fromLatin1(Collector::kGistDiscoveryDefaultQuery), 5);
    ASSERT_TRUE(summary.isOk()) << summary.error().toStdString();
    const Collector::GistDiscoverySummary page = summary.takeValue();
    std::cout << "pages=" << page.pagesScanned << " registered=" << page.registered
              << " nodes=" << page.nodes << " skipped=" << page.skipped
              << " rejected=" << page.rejected << " pending=" << page.pending
              << " reason=" << page.stoppedReason.toStdString() << std::endl;
    const Result<QVector<Models::Source>> created = db->listSources();
    ASSERT_TRUE(created.isOk());
    for (const Models::Source &row : created.value()) {
        std::cout << "  source: " << row.name.toStdString() << " -> "
                  << row.urlTemplate.toStdString() << std::endl;
    }
    ASSERT_GT(page.registered, 0);
    ASSERT_GT(page.nodes, 0);
}

/// 对应 Rust `fetch_all_enabled_runs_sources_concurrently_and_keeps_order`（:1715）。
TEST(CollectorTests, fetch_all_enabled_runs_sources_concurrently_and_keeps_order)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    const QString host = testHost(QStringLiteral("batch"));
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [](const Net::Request &, FakeState &) {
        QThread::msleep(150);
        return Result<Net::Response>::withValue(canned(200, QByteArray()));
    }));

    for (int index = 0; index < 4; ++index) {
        createSource(db, QStringLiteral("source-%1").arg(index),
                     QStringLiteral("http://%1/%2").arg(host).arg(index));
    }
    QVector<QString> expectedIds;
    Result<QVector<Models::Source>> listed = db->listSources();
    ASSERT_TRUE(listed.isOk());
    for (const Models::Source &source : listed.value()) {
        expectedIds.push_back(source.id);
    }

    QElapsedTimer started;
    started.start();
    Result<QVector<Models::FetchResult>> results = collector.fetchAllEnabled();
    ASSERT_TRUE(results.isOk()) << results.error().toStdString();

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ASSERT_GE(state->peak, 2) << "订阅拉取没有并发: " << state->peak;
    }
    ASSERT_LT(qint64(started.elapsed()), qint64(5000));
    QVector<QString> ids;
    for (const Models::FetchResult &result : results.value()) {
        ids.push_back(result.sourceId);
    }
    ASSERT_EQ(ids, expectedIds);
}

/// 对应 Rust `conditional_fetch_skips_parse_and_write_on_304`（:1781）。
TEST(CollectorTests, conditional_fetch_skips_parse_and_write_on_304)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [](const Net::Request &request, FakeState &) {
        if (request.ifNoneMatch == QStringLiteral("v1")) {
            return Result<Net::Response>::withValue(canned(304));
        }
        return Result<Net::Response>::withValue(
            canned(200, QByteArray(kNodeBody), {Net::Header{QStringLiteral("ETag"),
                                                            QStringLiteral("v1")}}));
    }));

    const QString url = testUrl(QStringLiteral("conditional"));
    const Models::Source source = createSource(db, QStringLiteral("conditional"), url);
    const Models::FetchResult first = collector.fetchSource(source);
    ASSERT_TRUE(first.success) << first.message.toStdString();
    const Models::Source cached = reload(db, source.id);
    ASSERT_EQ(cached.lastEtag, QStringLiteral("v1"));
    const Models::FetchResult second = collector.fetchSource(cached);

    ASSERT_TRUE(second.success) << second.message.toStdString();
    ASSERT_TRUE(second.message.contains(QLatin1String("skipped parse/write")))
        << second.message.toStdString();
    ASSERT_EQ(requestCount(state), 2);
    // 第二次确实带上了 If-None-Match。
    ASSERT_EQ(recordedRequests(state).back().ifNoneMatch, QStringLiteral("v1"));
}

/// 对应 Rust `unchanged_body_hash_skips_parse_and_write`（:1836）。
TEST(CollectorTests, unchanged_body_hash_skips_parse_and_write)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [](const Net::Request &, FakeState &) {
        return Result<Net::Response>::withValue(canned(200, QByteArray(kNodeBody)));
    }));

    const Models::Source source = createSource(db, QStringLiteral("hash"),
                                               testUrl(QStringLiteral("hash")));
    ASSERT_TRUE(collector.fetchSource(source).success);
    const Models::Source cached = reload(db, source.id);
    const Models::FetchResult second = collector.fetchSource(cached);

    ASSERT_TRUE(second.success) << second.message.toStdString();
    ASSERT_TRUE(second.message.contains(QLatin1String("content unchanged")))
        << second.message.toStdString();
}

// ---- 并发闸：brief 点名的两条行为 ----

/// 按主机闸上限是 2（Rust :41），而且等许可的时间不算进请求预算（:429 先 acquire，
/// :453 才起 timeout）。第三条腿要等约 250ms，但它自己的 300ms 预算必须还是完整的。
TEST(CollectorTests, host_gate_caps_at_two_and_waiting_for_a_permit_costs_no_budget)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [](const Net::Request &, FakeState &) {
        QThread::msleep(250);
        return Result<Net::Response>::withValue(canned(200, QByteArray(kNodeBody)));
    }));

    const QString url = testUrl(QStringLiteral("gate"));
    const Models::Source source = createSource(db, QStringLiteral("gate"), url);
    const QString taskId = tracker.start(Tasks::Kind::Fetch, {{source.id, source.name}});
    const Collector::FetchCandidate candidate{url, Collector::urlHost(url), false};

    QElapsedTimer started;
    started.start();
    std::vector<std::thread> legs;
    QVector<int> outcomes(3, 0);
    for (int index = 0; index < 3; ++index) {
        legs.push_back(std::thread([&, index] {
            Result<std::optional<Collector::Downloaded>> outcome =
                collector.downloadCandidate(source, candidate, taskId, 300,
                                            Collector::kResponseHeaderTimeoutMs);
            outcomes[index] = outcome.isOk() && outcome.value().has_value() ? 1 : 0;
        }));
    }
    for (std::thread &leg : legs) {
        leg.join();
    }
    const qint64 elapsed = started.elapsed();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ASSERT_EQ(state->peak, 2) << "按主机并发没有限住";
    }
    ASSERT_EQ(requestCount(state), 3);
    ASSERT_EQ(outcomes, QVector<int>({1, 1, 1})) << "等闸的时间被算进了请求预算";
    ASSERT_GE(elapsed, qint64(250)) << "三条腿根本没排队";
    ASSERT_LT(elapsed, qint64(1200)) << elapsed;
}

/// 批量拉取不许 fail-fast：一个源挂掉，其余源的结果照旧给全（Rust :920-971）。
TEST(CollectorTests, batch_fetch_keeps_every_result_when_one_source_fails)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::shared_ptr<Database> db = openDatabase(dir);
    ASSERT_NE(db, nullptr);

    auto state = std::make_shared<FakeState>();
    const QString deadUrl = testUrl(QStringLiteral("batch-dead"));
    Tasks::TaskTracker tracker;
    Collector::Collector collector(db, tracker);
    collector.setTransport(makeTransport(state, [deadUrl](const Net::Request &request,
                                                          FakeState &) {
        if (request.url == deadUrl) {
            return Result<Net::Response>::withValue(canned(404));
        }
        return Result<Net::Response>::withValue(canned(200, QByteArray(kNodeBody)));
    }));

    const QVector<QString> urls = {testUrl(QStringLiteral("batch-a")), deadUrl,
                                   testUrl(QStringLiteral("batch-b"))};
    for (int index = 0; index < urls.size(); ++index) {
        createSource(db, QStringLiteral("s%1").arg(index), urls[index]);
    }

    Result<QVector<Models::FetchResult>> results = collector.fetchAllEnabled();
    ASSERT_TRUE(results.isOk()) << results.error().toStdString();
    ASSERT_EQ(results.value().size(), 3);
    int succeeded = 0;
    int failed = 0;
    for (const Models::FetchResult &result : results.value()) {
        if (result.success) {
            ++succeeded;
        } else {
            ++failed;
            ASSERT_TRUE(result.message.contains(QLatin1String("HTTP 404")))
                << result.message.toStdString();
        }
    }
    ASSERT_EQ(succeeded, 2);
    ASSERT_EQ(failed, 1);
    // 404 不是"主机凉了"：这条源的连击计数不许动。
    const Result<QVector<Models::Source>> listed = db->listSources();
    ASSERT_TRUE(listed.isOk());
    for (const Models::Source &source : listed.value()) {
        if (source.urlTemplate == deadUrl) {
            ASSERT_EQ(source.failStreak, 0);
            ASSERT_TRUE(source.lastStatus.contains(QLatin1String("HTTP 404")))
                << source.lastStatus.toStdString();
        }
    }
    // 失败的那一路也要在任务台账里落成"失败"，不能悄悄消失。
    const QVector<Tasks::Snapshot> snapshots = tracker.snapshots(1);
    ASSERT_EQ(snapshots.size(), 1);
    ASSERT_EQ(snapshots[0].successfulSources, 2u);
    ASSERT_EQ(snapshots[0].failedSources, 1u);
}

} // namespace
