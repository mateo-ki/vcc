// src/speedtest/mod.rs 的移植测试。
//
// Rust 的 `mod tests` 有 4 条，这里 1:1 搬过来（注释标了对应的 Rust 测试名），再加几条
// 只在 C++ 侧需要锁住的行为：并发完成队列、纯函数化的档位超时映射、以及不碰网络的拒绝路径。
//
// 【绝不碰网络】所有会走到 socket 的用例用的都是 127.0.0.1/私有地址，它们在
// HostUtil::isPublicProxyHost 那一步就被拒掉，一个包都不会发出去；也不会起内核进程
// （预筛全挂的节点进不了真代理验证，ProxyProbe 那三个调用拿到的是空列表，与 Rust 一致）。
#include <gtest/gtest.h>

#include <QHostAddress>
#include <QString>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QVector>

#include <atomic>
#include <iostream>
#include <set>

#include "core/TimeUtil.h"
#include "db/Database.h"
#include "models/Node.h"
#include "models/Settings.h"
#include "models/Types.h"
#include "speedtest/JobQueue.h"
#include "speedtest/SpeedTester.h"
#include "speedtest/SpeedtestMessages.h"
#include "speedtest/SpeedtestTypes.h"
#include "speedtest/TcpProbe.h"
#include "tasks/TaskTracker.h"

using Speedtest::JobQueue;
using Speedtest::SpeedTester;
using Speedtest::SpeedTestOptions;
using Speedtest::SpeedTestPhase;
using Speedtest::SpeedTestProgress;
using Speedtest::SpeedtestIntensity;

// ---- Rust: udp_proxy_protocols_skip_tcp_prefilter ----
TEST(SpeedtestPort, UdpProxyProtocolsSkipTcpPrefilter)
{
    for (const char *protocol : {"hysteria", "hysteria2", "hy2", "tuic", "wireguard", "wg"}) {
        EXPECT_TRUE(Speedtest::requiresProxyWithoutTcp(QLatin1String(protocol))) << protocol;
    }
    for (const char *protocol : {"vless", "vmess", "trojan", "ss"}) {
        EXPECT_FALSE(Speedtest::requiresProxyWithoutTcp(QLatin1String(protocol))) << protocol;
    }
    // 大小写不敏感（Rust 用的是 to_ascii_lowercase）。
    EXPECT_TRUE(Speedtest::requiresProxyWithoutTcp(QStringLiteral("HY2")));
    EXPECT_FALSE(Speedtest::requiresProxyWithoutTcp(QStringLiteral("VLESS")));
}

// ---- Rust: speedtest_intensity_labels_and_modes ----
TEST(SpeedtestPort, SpeedtestIntensityLabelsAndModes)
{
    EXPECT_EQ(Speedtest::intensityFromText(QStringLiteral("standard")),
              SpeedtestIntensity::Standard);
    for (const SpeedtestIntensity intensity :
         {SpeedtestIntensity::Fast, SpeedtestIntensity::Standard, SpeedtestIntensity::Deep}) {
        EXPECT_FALSE(Speedtest::intensityLabel(intensity).isEmpty())
            << Speedtest::intensityToText(intensity).toStdString();
        EXPECT_FALSE(Speedtest::intensityDescription(intensity).isEmpty())
            << Speedtest::intensityToText(intensity).toStdString();
    }
    // serde 的 snake_case 取值（GUI/API 拿它当档位 id）。
    EXPECT_EQ(Speedtest::intensityToText(SpeedtestIntensity::Fast).toStdString(), "fast");
    EXPECT_EQ(Speedtest::intensityToText(SpeedtestIntensity::Standard).toStdString(), "standard");
    EXPECT_EQ(Speedtest::intensityToText(SpeedtestIntensity::Deep).toStdString(), "deep");
    EXPECT_EQ(Speedtest::intensityFromText(QStringLiteral("deep")), SpeedtestIntensity::Deep);
    EXPECT_EQ(Speedtest::intensityFromText(QStringLiteral("fast")), SpeedtestIntensity::Fast);
    // 认不出来的文本落回默认档，对应 #[default]。
    EXPECT_EQ(Speedtest::intensityFromText(QStringLiteral("turbo")), SpeedtestIntensity::Standard);
    // Rust 的 #[default] 是 Standard，而 C++ 的零初始化会落到 Fast：这条锁住的是那个陷阱。
    EXPECT_EQ(Speedtest::defaultIntensity(), SpeedtestIntensity::Standard);
}

// ---- Rust: probe_rejects_private_host ----
TEST(SpeedtestPort, ProbeRejectsPrivateHost)
{
    const Speedtest::ProbeOutcome outcome =
        SpeedTester::probeFull(QStringLiteral("127.0.0.1"), 80, 500, false, 200);
    EXPECT_FALSE(outcome.ok);
    EXPECT_FALSE(outcome.latencyMs.has_value());
    EXPECT_FALSE(outcome.downloadKbps.has_value());
    EXPECT_TRUE(outcome.message.contains(QLatin1String("skip"))) << outcome.message.toStdString();
}

// ---- Rust: persists_tcp_prefilter_failure_as_unavailable ----
TEST(SpeedtestPort, PersistsTcpPrefilterFailureAsUnavailable)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/prefilter-failure.db");

    const Result<std::shared_ptr<Database>> opened = Database::connect(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    const std::shared_ptr<Database> db = opened.value();

    const TimeUtil::OptionalUtc now = TimeUtil::OptionalUtc::now();
    Models::Node node;
    node.id = QStringLiteral("prefilter-failure");
    node.sourceId = QString();                      // Rust: None
    node.protocol = QStringLiteral("ss");
    node.name = QStringLiteral("prefilter failure");
    node.server = QStringLiteral("127.0.0.1");
    node.port = 80;
    node.raw = QStringLiteral("ss://prefilter-failure");
    node.country = QString();                       // Rust: None
    node.latencyMs = std::nullopt;
    node.latencyOk = std::nullopt;
    node.downloadKbps = std::nullopt;
    node.latencyKernel = QString();
    node.latencyTestKind = QString();
    node.latencyTestedAt = TimeUtil::OptionalUtc::none();
    node.createdAt = now;
    node.updatedAt = now;

    const QVector<Models::Node> incoming = {node};
    const Result<int> imported = db->importNodes(incoming);
    ASSERT_TRUE(imported.isOk()) << imported.error().toStdString();

    SpeedTester tester(db, Tasks::TaskTracker());
    QVector<QString> ids;
    ids.append(QStringLiteral("prefilter-failure"));
    const Result<Models::SpeedTestSummary> summary =
        tester.testMany(std::optional<QVector<QString>>(ids), Speedtest::defaultOptions());
    ASSERT_TRUE(summary.isOk()) << summary.error().toStdString();
    EXPECT_EQ(summary.value().ok, 0);
    EXPECT_EQ(summary.value().fail, 1);
    ASSERT_EQ(summary.value().results.size(), 1);

    const Result<Models::Node> stored = db->getNode(QStringLiteral("prefilter-failure"));
    ASSERT_TRUE(stored.isOk()) << stored.error().toStdString();
    ASSERT_TRUE(stored.value().latencyOk.has_value());
    EXPECT_FALSE(*stored.value().latencyOk);
    EXPECT_EQ(stored.value().latencyTestKind.toStdString(), "tcp_prefilter");
    // 预筛挂掉的节点不该留下一个假的下载速率，也不该盖上内核名。
    EXPECT_FALSE(stored.value().downloadKbps.has_value());
    EXPECT_TRUE(stored.value().latencyKernel.isNull());
}

// ---- 下面几条是 C++ 侧额外锁住的行为 ----

TEST(SpeedtestPort, DefaultOptionsMatchRust)
{
    const SpeedTestOptions options = Speedtest::defaultOptions();
    EXPECT_EQ(options.concurrency, 128);
    EXPECT_EQ(options.timeoutMs, 1500);
    EXPECT_FALSE(options.measureDownload);
    EXPECT_EQ(options.downloadTestMs, 800);
    EXPECT_EQ(options.intensity, SpeedtestIntensity::Standard);
    EXPECT_FALSE(options.parallelKernels);
    EXPECT_FALSE(options.persistentKernel);
    EXPECT_EQ(options.reuseVerifiedMinutes, 0);
    EXPECT_FALSE(options.conditionalIsolation);
    EXPECT_EQ(options.downloadTopN, 0);
}

TEST(SpeedtestPort, IntensityPicksBothTimeouts)
{
    SpeedTestOptions options = Speedtest::defaultOptions();   // timeout_ms = 1500
    // Fast：预筛 min(1500,500)=500，真代理固定 1800。
    options.intensity = SpeedtestIntensity::Fast;
    EXPECT_EQ(Speedtest::prefilterTimeoutMs(options), 500);
    EXPECT_EQ(Speedtest::proxyTimeoutMs(options), 1800);
    // Standard：预筛 min(1500, TCP_PREFILTER_TIMEOUT_MS=800)=800，真代理 clamp 到 2500。
    options.intensity = SpeedtestIntensity::Standard;
    EXPECT_EQ(Speedtest::prefilterTimeoutMs(options), Speedtest::kTcpPrefilterTimeoutMs);
    EXPECT_EQ(Speedtest::proxyTimeoutMs(options), 2500);
    // Deep：预筛 max(1500,1200)=1500，真代理 clamp(1500,4000,20000)=4000。
    options.intensity = SpeedtestIntensity::Deep;
    EXPECT_EQ(Speedtest::prefilterTimeoutMs(options), 1500);
    EXPECT_EQ(Speedtest::proxyTimeoutMs(options), 4000);
    // 高容忍的长超时只在 Deep 下保留，Standard 的预筛仍被 800ms 卡住。
    options.timeoutMs = 30000;
    EXPECT_EQ(Speedtest::prefilterTimeoutMs(options), 30000);
    EXPECT_EQ(Speedtest::proxyTimeoutMs(options), 20000);
    options.intensity = SpeedtestIntensity::Standard;
    EXPECT_EQ(Speedtest::prefilterTimeoutMs(options), 800);
    EXPECT_EQ(Speedtest::proxyTimeoutMs(options), 15000);
}

TEST(SpeedtestPort, ProbeFullRejectsPortZeroWithoutTouchingDns)
{
    // 端口 0 在解析/连接之前就被拒，所以这条用例不会有任何 DNS 或 socket 行为。
    // 主机名要挑一个 isPublicProxyHost 认过的：example.com 那类占位域名会被更早一步拦掉。
    const Speedtest::ProbeOutcome outcome =
        SpeedTester::probeFull(QStringLiteral("hk3.proxy-provider.net"), 0, 500, false, 200);
    EXPECT_FALSE(outcome.ok);
    EXPECT_EQ(outcome.message.toStdString(), "invalid port 0");

    // 占位域名（example.com 那类坏订阅垃圾）在 HostUtil 那一步就被拒，报的是 host 消息。
    const Speedtest::ProbeOutcome placeholder =
        SpeedTester::probeFull(QStringLiteral("example.com"), 443, 500, false, 200);
    EXPECT_FALSE(placeholder.ok);
    EXPECT_EQ(placeholder.message.toStdString(), "skip invalid/private host: example.com");
}

TEST(SpeedtestPort, ProbeFullResolvedSkipsPrivateHostWithHostMessage)
{
    const Speedtest::ResolvedAddresses addresses =
        Speedtest::ResolvedAddresses::withValue(QVector<QHostAddress>());
    const Speedtest::ProbeOutcome outcome = SpeedTester::probeFullResolved(
        QStringLiteral("10.0.0.5"), 0, 500, 200, false, addresses);
    EXPECT_FALSE(outcome.ok);
    // Rust 的 probe_full_resolved 即使是因为 port==0 也统一报 host 那条消息。
    EXPECT_EQ(outcome.message.toStdString(), "skip invalid/private host: 10.0.0.5");
}

TEST(SpeedtestPort, ProbeFullResolvedPropagatesDnsError)
{
    const Speedtest::ResolvedAddresses addresses =
        Speedtest::ResolvedAddresses::withError(QStringLiteral("dns timeout"));
    const Speedtest::ProbeOutcome outcome = SpeedTester::probeFullResolved(
        QStringLiteral("hk3.proxy-provider.net"), 443, 500, 200, false, addresses);
    EXPECT_FALSE(outcome.ok);
    EXPECT_EQ(outcome.message.toStdString(), "dns timeout");
}

TEST(SpeedtestPort, ConnectResolvedRejectsEmptyAddressList)
{
    // 空地址表在发起任何连接之前就返回，不会碰网络。
    const Result<Speedtest::Connected> connected =
        Speedtest::connectResolved(QVector<QHostAddress>(), 8080, 200);
    ASSERT_TRUE(connected.isErr());
    EXPECT_EQ(connected.error().toStdString(), "dns returned no addresses");
}

TEST(SpeedtestPort, LooksLikePublicWebsiteNeedsHttpHeadAndHtmlHint)
{
    EXPECT_FALSE(Speedtest::looksLikePublicWebsite(QByteArray()));
    EXPECT_FALSE(Speedtest::looksLikePublicWebsite(
        QByteArray("VMess handshake noise 200 OK text/html")));
    EXPECT_TRUE(Speedtest::looksLikePublicWebsite(
        QByteArray("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html></html>")));
    EXPECT_TRUE(Speedtest::looksLikePublicWebsite(
        QByteArray("HTTP/1.0 404 Not Found\r\nServer: nginx\r\n\r\n")));
    EXPECT_TRUE(Speedtest::looksLikePublicWebsite(
        QByteArray("HTTP/2 302 \r\nlocation: /x\r\ncontent-type: text/html\r\n\r\n")));
    // Rust 的状态码是带空格匹配的（" 301 "），行尾没有空格的裸状态码不算。
    EXPECT_FALSE(Speedtest::looksLikePublicWebsite(QByteArray("HTTP/2 301\r\nlocation: /x\r\n")));
    // 状态码不在表里（比如 500）就不算普通网站：那可能是代理真实吐出的流量。
    EXPECT_FALSE(Speedtest::looksLikePublicWebsite(
        QByteArray("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\n\r\n")));
}

TEST(SpeedtestPort, EmptySummaryIsAnErrorWithRustText)
{
    const QVector<Models::LatencyResult> empty;
    const Result<Models::SpeedTestSummary> summary = Speedtest::buildSpeedtestSummary(empty);
    ASSERT_TRUE(summary.isErr());
    EXPECT_EQ(summary.error().toStdString(),
              Speedtest::Messages::noNodesOrAllFailed().toStdString());
}

TEST(SpeedtestPort, SummaryAveragesOnlyCountPresentValues)
{
    QVector<Models::LatencyResult> results;
    Models::LatencyResult first;
    first.id = QStringLiteral("a");
    first.ok = true;
    first.latencyMs = 100;
    first.downloadKbps = 2000.0;
    Models::LatencyResult second;
    second.id = QStringLiteral("b");
    second.ok = false;
    second.latencyMs = 300;
    results.append(first);
    results.append(second);

    const Result<Models::SpeedTestSummary> summary = Speedtest::buildSpeedtestSummary(results);
    ASSERT_TRUE(summary.isOk()) << summary.error().toStdString();
    EXPECT_EQ(summary.value().total, 2);
    EXPECT_EQ(summary.value().ok, 1);
    EXPECT_EQ(summary.value().fail, 1);
    ASSERT_TRUE(summary.value().avgMs.has_value());
    EXPECT_DOUBLE_EQ(*summary.value().avgMs, 200.0);
    ASSERT_TRUE(summary.value().avgDownloadKbps.has_value());
    EXPECT_DOUBLE_EQ(*summary.value().avgDownloadKbps, 2000.0);
    EXPECT_TRUE(summary.value().results.isEmpty() == false);
}

TEST(SpeedtestPort, ProgressReportsFailAsDoneMinusOk)
{
    QVector<SpeedTestProgress> seen;
    const Speedtest::ProgressFn progress = [&seen](const SpeedTestProgress &state) {
        seen.append(state);
    };
    Speedtest::reportSpeedtestProgress(progress, SpeedTestPhase::TcpPrefilter, 10, 20, 4, 1234);
    ASSERT_EQ(seen.size(), 1);
    EXPECT_EQ(seen.first().phase, SpeedTestPhase::TcpPrefilter);
    EXPECT_EQ(seen.first().done, size_t(10));
    EXPECT_EQ(seen.first().total, size_t(20));
    EXPECT_EQ(seen.first().ok, size_t(4));
    EXPECT_EQ(seen.first().fail, size_t(6));
    EXPECT_EQ(seen.first().elapsedMs, qint64(1234));
    EXPECT_FALSE(seen.first().kernel.has_value());
    EXPECT_FALSE(seen.first().batchIndex.has_value());
    EXPECT_FALSE(seen.first().batchTotal.has_value());

    // None（空回调）必须直接返回，而不是炸。
    Speedtest::reportSpeedtestProgress(Speedtest::ProgressFn(), SpeedTestPhase::RealProxy, 1, 1, 1,
                                       0);
}

TEST(SpeedtestPort, TestAllSourcesOnEmptyDatabaseKeepsRustErrorText)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const Result<std::shared_ptr<Database>> opened =
        Database::connect(directory.path() + QStringLiteral("/empty.db"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();

    SpeedTester tester(opened.value(), Tasks::TaskTracker());
    const Result<Models::SpeedTestSummary> summary =
        tester.testAllSources(Speedtest::defaultOptions());
    ASSERT_TRUE(summary.isErr());
    EXPECT_EQ(summary.error().toStdString(),
              Speedtest::Messages::noNodesToTest().toStdString());
}

TEST(SpeedtestPort, JobQueueRunsComputesOffThreadAndAppliesOnDriver)
{
    // 对应 Rust 的 JoinSet：worker 只算，记账（进度、results 追加）都回到驱动线程。
    const int workers = 8;
    const int jobs = 64;
    const Qt::HANDLE driverThread = QThread::currentThreadId();

    std::atomic<int> offThreadComputes{0};
    std::atomic<int> onThreadComputes{0};
    QSet<int> applied;
    int appliersOnDriver = 0;
    {
        JobQueue queue(workers);
        EXPECT_EQ(queue.capacity(), workers);
        for (int index = 0; index < jobs; ++index) {
            queue.submit([&queue, &offThreadComputes, &onThreadComputes, &applied,
                          &appliersOnDriver, index, driverThread]() {
                if (QThread::currentThreadId() == driverThread) {
                    ++onThreadComputes;
                } else {
                    ++offThreadComputes;
                }
                return [index, &applied, &appliersOnDriver, driverThread]() {
                    applied.insert(index);
                    if (QThread::currentThreadId() == driverThread) {
                        ++appliersOnDriver;
                    }
                };
            });
        }
        queue.waitAll();
    }
    EXPECT_EQ(offThreadComputes.load(), jobs);
    EXPECT_EQ(onThreadComputes.load(), 0);
    EXPECT_EQ(applied.size(), jobs);
    EXPECT_EQ(appliersOnDriver, jobs);
}

/// 对应 Rust `gui/mod.rs:468 speed_opts()`：强度档同时改并发与超时，设置里那一组开关
/// 必须原样带进这一轮测速。之前节点池三处入口各自手搓 `opt.timeoutMs = 5000`，
/// 于是强度下拉框与 并发/超时/下载/常驻内核/并行内核/复用分钟/条件隔离 全被忽略 ——
/// 他报的"点测速很慢"第一处就出在这里（默认档 Rust 是 1500ms，我们写死 5000ms）。
TEST(SpeedtestPort, SpeedOptionsComeFromSettingsAndIntensity)
{
    Models::AppSettings settings;
    settings.speedtestConcurrency = 128;
    settings.speedtestTimeoutMs = 1500;
    settings.speedtestDownload = true;
    settings.downloadTestMs = 900;
    settings.speedtestParallelKernels = true;
    settings.speedtestPersistentKernel = true;
    settings.speedtestReuseMinutes = 45;
    settings.speedtestConditionalIsolation = true;
    settings.speedtestDownloadTopN = 20;

    using Speedtest::SpeedtestIntensity;
    const Speedtest::SpeedTestOptions fast =
        Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Fast);
    EXPECT_EQ(fast.concurrency, 256);        // max(128, 256)
    EXPECT_EQ(fast.timeoutMs, 800);          // min(1500, 800)

    const Speedtest::SpeedTestOptions standard =
        Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Standard);
    EXPECT_EQ(standard.concurrency, 128);
    EXPECT_EQ(standard.timeoutMs, 1500);

    const Speedtest::SpeedTestOptions deep =
        Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Deep);
    EXPECT_EQ(deep.concurrency, 64);         // clamp(32, 128/2, 64)
    EXPECT_EQ(deep.timeoutMs, 3500);         // max(1500, 3500)

    // 开关是"照抄设置"，三档都一样。
    for (const Speedtest::SpeedTestOptions *options : {&fast, &standard, &deep}) {
        EXPECT_TRUE(options->measureDownload);
        EXPECT_EQ(options->downloadTestMs, 900);
        EXPECT_TRUE(options->parallelKernels);
        EXPECT_TRUE(options->persistentKernel);
        EXPECT_EQ(options->reuseVerifiedMinutes, 45);
        EXPECT_TRUE(options->conditionalIsolation);
        EXPECT_EQ(options->downloadTopN, 20);
    }

    // clamp 的两端都要拦住：并发太小不能被 Deep 档压成 0，太大不能被 Fast 档抬过。
    settings.speedtestConcurrency = 10;
    EXPECT_EQ(Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Deep).concurrency, 32);
    EXPECT_EQ(Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Fast).concurrency, 256);
    settings.speedtestConcurrency = 400;
    EXPECT_EQ(Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Deep).concurrency, 64);
    EXPECT_EQ(Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Fast).concurrency, 400);
    // 标准档不许偷偷改用户的数。
    EXPECT_EQ(Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Standard).concurrency, 400);
    EXPECT_EQ(Speedtest::speedOptionsFor(settings, SpeedtestIntensity::Standard).timeoutMs, 1500);
}

/// JobQueue 必须**边提交边消费**完成项。原来 submit 只抢信号量就返回，applier 全堆到
/// waitAll 才执行 —— 测速的进度回调都在 applier 里，于是 15175 个节点那一轮里状态栏
/// 一直停在"已完成 0/15175"，跑完才瞬间跳到末尾（他 2026-10-12 报的第一条）。
TEST(SpeedtestPort, JobQueueRunsAppliersWhileStillSubmitting)
{
    JobQueue queue(2);
    std::atomic<int> executed{0};

    for (int index = 0; index < 12; ++index) {
        queue.submit([&executed]() {
            QThread::msleep(15);
            return [&executed]() { executed.fetch_add(1); };
        });
    }
    const int duringSubmit = executed.load();
    queue.waitAll();

    EXPECT_EQ(executed.load(), 12) << "有作业没被执行完";
    // 12 个作业 / 2 条 worker / 每个 15ms：submit 从第 3 个起就必须先消费一个完成项，
    // 所以最后一次 submit 返回时至少该跑掉大半。取 4 是保守下限（旧行为是 0）。
    std::cout << "appliers executed before waitAll: " << duringSubmit << " / 12" << std::endl;
    EXPECT_GE(duringSubmit, 4)
        << "最后一次 submit 返回时几乎没消费完成项 —— 进度会整轮憋到结束才爆发";
}

/// 「配置里的并发」和「同时在跑的探针数」是不是同一件事。
///
/// 这条不碰网络：直接问 JobQueue —— 测速整条链路（TCP 预筛、按 N 名补测下载、以及
/// 每个内核泳道里的 JoinWindow）都建在它上面，`pool_.setMaxThreadCount(capacity)` 是
/// 唯一保证"并发 256 就是 256 条线程在跑"的地方。256 个各睡 200ms 的作业：
/// 真并发 → 墙钟 ≈ 200ms；要是实际只有 24 条线程，就是 ≈ 2.1 秒，一个数量级。
/// （Rust 那侧是 tokio 任务，天生不受线程数限制 —— 这就是两边唯一可能的结构性差距。）
TEST(SpeedtestPort, JobQueueRunsEveryConfiguredSlotInParallel)
{
    constexpr int jobs = 256;
    constexpr int sleepMs = 200;

    std::atomic<int> running{0};
    std::atomic<int> peak{0};
    JobQueue queue(jobs);
    QElapsedTimer clock;
    clock.start();
    for (int index = 0; index < jobs; ++index) {
        queue.submit([&running, &peak]() {
            const int now = ++running;
            // peak 必须用 CAS 循环更新：`store(max(load(), now))` 两条线程会互相覆盖。
            int previous = peak.load();
            while (now > previous
                   && !peak.compare_exchange_weak(previous, now)) {
            }
            QThread::msleep(sleepMs);
            --running;
            return []() {};
        });
    }
    queue.waitAll();
    const qint64 elapsed = clock.elapsed();

    std::cout << "jobqueue parallelism jobs=" << jobs << " peak=" << peak.load()
              << " elapsed_ms=" << elapsed << std::endl;
    EXPECT_GE(peak.load(), jobs / 2)
        << "配置了 " << jobs << " 并发，实际同时在跑的最只有 " << peak.load() << " 条";
    // 一轮的理论值是 sleepMs；给 3 轮 + 2s 的线程创建/调度余量。
    EXPECT_LE(elapsed, sleepMs * 3 + 2000)
        << jobs << " 个 " << sleepMs << "ms 的作业跑了 " << elapsed
        << "ms —— 并发没真开起来";
}

