#include <gtest/gtest.h>

#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QSemaphore>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <utility>

#include "proxy/KernelConfigMessages.h"
#include "proxy_probe/JoinWindow.h"
#include "proxy_probe/MihomoProbe.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/ProbeMessages.h"
#include "proxy_probe/ProbeTypes.h"
#include "proxy_probe/SingBoxProbe.h"
#include "proxy/TestUrls.h"
#include "proxy_probe/XrayProbe.h"

using namespace ProxyProbe;

namespace {

/// 期望文本一律写成 UTF-8 十六进制：这些字节由 node 直接从 Rust 字面量推导
/// （src/proxy_probe/dump-test-hex.mjs），手打中文在这个环境里会被静默丢字符。
QString fromHex(const char *hex)
{
    return QString::fromUtf8(QByteArray::fromHex(QByteArray(hex)));
}

/// 真实代理
QString realProxyText()
{
    return fromHex("E79C9FE5AE9EE4BBA3E79086");
}

/// 真实代理测试失败
QString realProxyTestFailed()
{
    return fromHex("E79C9FE5AE9EE4BBA3E79086E6B58BE8AF95E5A4B1E8B4A5");
}

/// 真实代理可用
QString realProxyAvailable()
{
    return fromHex("E79C9FE5AE9EE4BBA3E79086E58FAFE794A8");
}

}

TEST(ProbeConstants, MatchRustTuningValues)
{
    EXPECT_EQ(kProxyConcurrency, 96);
    EXPECT_EQ(kMihomoBatchSize, 256);
    EXPECT_EQ(kMihomoBatchConcurrency, 3);
    EXPECT_EQ(kValidateConcurrency, 8);
    EXPECT_EQ(kPersistentMihomoBatchSize, size_t(8192));
}

TEST(ProbeConstants, ManualProbeTargetRequiresGoogle)
{
    // Rust 的 manual_probe_target_requires_google。
    EXPECT_EQ(kProxyConnectivityTestUrl.toStdString(),
              std::string("https://www.google.com/generate_204"));
}

TEST(ProbeEncoding, PercentEncodesEveryNonAlphanumericByte)
{
    // Rust 用的是 NON_ALPHANUMERIC：连 '-' 与 '.' 都要编掉。
    EXPECT_EQ(percentEncodePath(QStringLiteral("probe-0-id")).toStdString(),
              std::string("probe%2D0%2Did"));
    EXPECT_EQ(percentEncodePath(QStringLiteral("a.b/c")).toStdString(),
              std::string("a%2Eb%2Fc"));
    // UTF-8 逐字节，大写十六进制。
    EXPECT_EQ(percentEncodePath(QString::fromUtf8(QByteArray::fromHex("E88A82E782B9")))
                  .toStdString(),
              std::string("%E8%8A%82%E7%82%B9"));
    EXPECT_EQ(percentEncodePath(QString()).toStdString(), std::string());
}

TEST(ProbeEncoding, FormEncodeKeysUrlUnreservedAndPlusForSpace)
{
    EXPECT_EQ(formEncodeQuery(QStringLiteral("https://www.google.com/generate_204")).toStdString(),
              std::string("https%3A%2F%2Fwww.google.com%2Fgenerate_204"));
    EXPECT_EQ(formEncodeQuery(QStringLiteral("a b*c-d_e.f")).toStdString(),
              std::string("a+b*c-d_e.f"));
    EXPECT_EQ(formEncodeQuery(QStringLiteral("3000")).toStdString(), std::string("3000"));
}

TEST(ProbeEncoding, DelayUrlMatchesControllerShape)
{
    const QString url = mihomoDelayUrl(QStringLiteral("probe-0-id"), 9090,
                                       kProxyConnectivityTestUrl, QStringLiteral("3000"));
    EXPECT_EQ(url.toStdString(),
              std::string("http://127.0.0.1:9090/proxies/probe%2D0%2Did/delay"
                          "?url=https%3A%2F%2Fwww.google.com%2Fgenerate_204&timeout=3000"));
}

TEST(ProbeMessages, DelaySuccessKeepsRustTemplate)
{
    // "{delay} ms (真实代理 · {target})"
    const QString expected = QStringLiteral("42 ms (") + realProxyText()
        + QStringLiteral(" ") + fromHex("C2B7") + QStringLiteral(" Google)");
    EXPECT_EQ(delaySuccessMessage(42, QStringLiteral("Google")), expected);
}

TEST(ProbeMessages, DelayFailureKeepsRustTemplate)
{
    // "真实代理测试失败: {error}"，error 已经带目标名前缀。
    const QString expected = realProxyTestFailed() + QStringLiteral(": Google: HTTP 500");
    EXPECT_EQ(delayFailureMessage(QStringLiteral("Google: HTTP 500")), expected);
}

TEST(ProbeMessages, MissingDelayValueNamesTheTarget)
{
    // "{target_name}: 未返回有效延迟"
    const QString expected =
        QStringLiteral("Google: ") + fromHex("E69CAAE8BF94E59B9EE69C89E69588E5BBB6E8BF9F");
    EXPECT_EQ(delayMissingValueMessage(QStringLiteral("Google")), expected);
}

TEST(ProbeMessages, SingBoxProbeResultMessages)
{
    ConnectivityCheck ok;
    ok.ok = true;
    const ProbeResult passed = makeSingBoxProbeResult(QStringLiteral("id-1"), ok, 0);
    ASSERT_TRUE(passed.ok);
    EXPECT_EQ(passed.id.toStdString(), std::string("id-1"));
    // as_millis().max(1)
    EXPECT_EQ(passed.latencyMs.value(), qint64(1));
    EXPECT_EQ(passed.message, QStringLiteral("Sing-box ") + realProxyAvailable());

    ConnectivityCheck failed;
    failed.ok = false;
    failed.error = QStringLiteral("HTTP 500");
    const ProbeResult rejected = makeSingBoxProbeResult(QStringLiteral("id-2"), failed, 17);
    ASSERT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.latencyMs.has_value());
    EXPECT_EQ(rejected.message,
              QStringLiteral("Sing-box ") + realProxyTestFailed()
                  + QStringLiteral(": Google: HTTP 500"));
}

TEST(ProbeMessages, XrayProbeResultMessagesHaveNoTargetPrefix)
{
    ConnectivityCheck ok;
    ok.ok = true;
    const ProbeResult passed = makeXrayProbeResult(QStringLiteral("id-1"), ok, 8);
    ASSERT_TRUE(passed.ok);
    EXPECT_EQ(passed.latencyMs.value(), qint64(8));
    EXPECT_EQ(passed.message, QStringLiteral("Xray ") + realProxyAvailable());

    ConnectivityCheck failed;
    failed.ok = false;
    failed.error = QStringLiteral("connection refused");
    const ProbeResult rejected = makeXrayProbeResult(QStringLiteral("id-2"), failed, 0);
    EXPECT_EQ(rejected.message,
              QStringLiteral("Xray ") + realProxyTestFailed()
                  + QStringLiteral(": connection refused"));
}

TEST(ProbeDiagnostic, CompactTakesLastNonEmptyLine)
{
    const QString raw = QStringLiteral("first line\n   \n  last error line  \n\n");
    EXPECT_EQ(compactDiagnostic(raw).toStdString(), std::string("last error line"));
    EXPECT_EQ(compactDiagnostic(QStringLiteral("one\r\ntwo\r\n")).toStdString(), std::string("two"));
}

TEST(ProbeDiagnostic, CompactFallsBackToUnknownConfigError)
{
    EXPECT_EQ(compactDiagnostic(QStringLiteral("   \n\n")),
              Messages::mihomoUnknownConfigError());
    EXPECT_EQ(compactDiagnostic(QString()), Messages::mihomoUnknownConfigError());
}

TEST(ProbeDiagnostic, CompactTruncatesByCodePointsNotUtf16Units)
{
    const QString longLine(300, QChar(0x6D4B));
    const QString truncated = compactDiagnostic(longLine);
    EXPECT_EQ(truncated.size(), 240);
    // 代理对：一个码点占两个 UTF-16 单元，240 个码点就是 480 个单元。
    QString pairs;
    for (int index = 0; index < 300; ++index) {
        pairs += QChar::highSurrogate(0x1F600);
        pairs += QChar::lowSurrogate(0x1F600);
    }
    const QString taken = takeCodePoints(pairs, 240);
    EXPECT_EQ(taken.size(), 480);
    EXPECT_EQ(takeCodePoints(pairs, 0).size(), 0);
}

TEST(ProbeTimeouts, ClampsAndStartupBudgetScaleLikeRust)
{
    EXPECT_EQ(clampDelayTimeoutMs(100), 1000);
    EXPECT_EQ(clampDelayTimeoutMs(5000), 5000);
    EXPECT_EQ(clampDelayTimeoutMs(20000), 15000);
    EXPECT_EQ(controllerStartupBudgetMs(0), 8000);
    EXPECT_EQ(controllerStartupBudgetMs(1000), 13000);
    EXPECT_EQ(controllerStartupBudgetMs(20000), 60000);
    EXPECT_EQ(probeLatencyMs(0), qint64(1));
    EXPECT_EQ(probeLatencyMs(1234), qint64(1234));
}

TEST(ProbeProgress, EmitsRustAccounting)
{
    QVector<ProbeProgress> seen;
    const ProgressFn progress = [&seen](const ProbeProgress &state) { seen.append(state); };
    emitProgress(progress, 5, 2, 10, 7, 1, 3);
    ASSERT_EQ(seen.size(), 1);
    EXPECT_EQ(seen.at(0).done, size_t(5));
    EXPECT_EQ(seen.at(0).total, size_t(10));
    EXPECT_EQ(seen.at(0).ok, size_t(2));
    // fail = done - ok（saturating）。
    EXPECT_EQ(seen.at(0).fail, size_t(3));
    EXPECT_EQ(seen.at(0).elapsedMs, qint64(7));
    ASSERT_TRUE(seen.at(0).batchIndex.has_value());
    EXPECT_EQ(*seen.at(0).batchIndex, size_t(1));
    ASSERT_TRUE(seen.at(0).batchTotal.has_value());
    EXPECT_EQ(*seen.at(0).batchTotal, size_t(3));

    emitProgress(progress, 1, 3, 10, 0);
    ASSERT_EQ(seen.size(), 2);
    // done < ok 时 fail 不会变成负数。
    EXPECT_EQ(seen.at(1).fail, size_t(0));
    EXPECT_FALSE(seen.at(1).batchIndex.has_value());

    // 空回调（Rust 的 None）不能炸。
    emitProgress(ProgressFn(), 1, 1, 1, 0);
    EXPECT_EQ(seen.size(), 2);
}

TEST(ProbeProgress, ReportsFromResultSlots)
{
    QVector<std::optional<ProbeResult>> resultSlots(4);
    ProbeResult passed;
    passed.ok = true;
    resultSlots[0] = passed;
    ProbeResult rejected;
    rejected.ok = false;
    resultSlots[1] = rejected;
    resultSlots[3] = passed;

    QVector<ProbeProgress> seen;
    const ProgressFn progress = [&seen](const ProbeProgress &state) { seen.append(state); };
    reportProgress(progress, resultSlots, 4, 9);
    ASSERT_EQ(seen.size(), 1);
    EXPECT_EQ(seen.at(0).done, size_t(3));
    EXPECT_EQ(seen.at(0).ok, size_t(2));
    EXPECT_EQ(seen.at(0).fail, size_t(1));
    EXPECT_EQ(countedResults(resultSlots), 3);

    reportProgress(ProgressFn(), resultSlots, 4, 9);
    EXPECT_EQ(seen.size(), 1);
}

TEST(ProbeProgress, CounterStaysIncremental)
{
    // 起点是建配/隔离阶段已经填好的槽位数，之后每个探针只加一次，不再重扫整张表。
    ProgressCounter counter;
    ProbeResult rejected;
    rejected.ok = false;
    counter.done = countedResults(
        QVector<std::optional<ProbeResult>>{rejected, std::nullopt, std::nullopt});
    EXPECT_EQ(counter.done, 1);
    counter.markResult(true);
    counter.markResult(false);
    counter.markResult(true);
    EXPECT_EQ(counter.done, 4);
    EXPECT_EQ(counter.ok, 2);
}

TEST(ProbeJoinWindow, CapsInFlightTasks)
{
    QSemaphore started(0);
    QSemaphore gate(0);
    std::atomic_int running(0);
    std::atomic_int peak(0);
    JoinWindow<int> window(2);

    const auto job = [&started, &gate, &running, &peak](int id) {
        return [id, &started, &gate, &running, &peak]() {
            const int now = ++running;
            // fetch_max 而不是 store(max(load(), now))：后者两个线程会互相覆盖
            // （A 读到 0 算出 1、B 读到 1 算出 2，A 后写就把 peak 打回 1），
            // 表现是这条用例偶发红在 EXPECT_EQ(peak, 2)。
            int best = peak.load(std::memory_order_relaxed);
            while (now > best && !peak.compare_exchange_weak(best, now, std::memory_order_relaxed)) {
            }
            started.release(1);
            gate.acquire(1);
            --running;
            return id;
        };
    };

    ASSERT_TRUE(window.trySpawn(job(1)));
    ASSERT_TRUE(window.trySpawn(job(2)));
    started.acquire(2);
    EXPECT_EQ(window.unfinished(), 2);
    // 窗口满了：第三个任务提交不进去（Rust 的 while tasks.len() < N 就退出）。
    EXPECT_FALSE(window.trySpawn(job(3)));

    gate.release(2);
    Joined<int> first;
    ASSERT_TRUE(window.joinNext(&first));
    Joined<int> second;
    ASSERT_TRUE(window.joinNext(&second));
    EXPECT_EQ(first.value + second.value, 3);
    EXPECT_FALSE(first.failed);
    EXPECT_FALSE(second.failed);
    EXPECT_EQ(window.unfinished(), 0);
    EXPECT_EQ(peak.load(), 2);
    // 全部 join 完 -> Rust 的 join_next() 返回 None。
    EXPECT_FALSE(window.joinNext(&first));
}

TEST(ProbeJoinWindow, JoinsInCompletionOrder)
{
    JoinWindow<int> window(4);
    for (int index = 0; index < 4; ++index) {
        // 步长要明显大于线程调度抖动：40ms 的间隔在本机稳，在慢机器上会被调度顺序颠倒
        // （这条测的是"按完成顺序 join"，不是"40ms 内可分辨"）。
        const int delay = (3 - index) * 250;
        ASSERT_TRUE(window.trySpawn([index, delay]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            return index;
        }));
    }
    QVector<int> order;
    Joined<int> joined;
    while (window.joinNext(&joined)) {
        EXPECT_FALSE(joined.failed);
        order.append(joined.value);
    }
    ASSERT_EQ(order.size(), 4);
    // 完成顺序，而不是提交顺序。
    EXPECT_EQ(order.at(0), 3);
    EXPECT_EQ(order.at(3), 0);
}

TEST(ProbeJoinWindow, SurfacesEscapingExceptionAsJoinFailure)
{
    JoinWindow<int> window(1);
    ASSERT_TRUE(window.trySpawn([]() -> int {
        throw std::runtime_error("boom");
        return 0;
    }));
    Joined<int> joined;
    ASSERT_TRUE(window.joinNext(&joined));
    EXPECT_TRUE(joined.failed);
    EXPECT_EQ(joined.error.toStdString(), std::string("boom"));
}

TEST(ProbePorts, ReservesDistinctEphemeralPortsAndReleases)
{
    PortReservations reservations;
    QVector<quint16> ports;
    ASSERT_TRUE(reservations.reserveMany(3, &ports).isOk());
    EXPECT_EQ(ports.size(), 3);
    EXPECT_EQ(reservations.count(), 3);
    QSet<quint16> unique;
    for (const quint16 port : ports) {
        EXPECT_GT(port, 0);
        unique.insert(port);
    }
    EXPECT_EQ(unique.size(), 3);

    QVector<quint16> more;
    ASSERT_TRUE(reservations.reserveMany(0, &more).isOk());
    EXPECT_TRUE(more.isEmpty());

    reservations.release();
    EXPECT_EQ(reservations.count(), 3);
    // 释放之后同一批端口可以再占一次（Rust 的 drop(reservations) 就是为了让内核 bind）。
    PortReservations again;
    ASSERT_TRUE(again.reserve().isOk());
}

TEST(ProbeWorkDir, CreatesAndRemovesPrefixedTempDirectory)
{
    Result<TempWorkDir> created = TempWorkDir::create(QStringLiteral("vpn-parser-probe-test"));
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    const QString root = created.value().root();
    EXPECT_TRUE(QFileInfo(root).isDir());
    EXPECT_TRUE(root.contains(QStringLiteral("vpn-parser-probe-test-")));
    EXPECT_EQ(QFileInfo(created.value().filePath(QStringLiteral("config.yaml"))).fileName()
                  .toStdString(),
              std::string("config.yaml"));
    ASSERT_TRUE(created.value().remove().isOk());
    EXPECT_FALSE(QFileInfo::exists(root));
}

TEST(ProbeWorkDir, MoveClearsSourceSoNothingIsDeletedTwice)
{
    Result<TempWorkDir> created = TempWorkDir::create(QStringLiteral("vpn-parser-probe-test"));
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    const QString root = created.value().root();
    TempWorkDir moved = std::move(created.value());
    EXPECT_FALSE(created.value().isValid());
    EXPECT_TRUE(moved.isValid());
    EXPECT_TRUE(QFileInfo::exists(root));
    moved.remove();
    EXPECT_FALSE(QFileInfo::exists(root));
}
