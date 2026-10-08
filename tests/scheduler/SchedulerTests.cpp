// src/scheduler/mod.rs 的移植测试：Rust 的 `mod refill_tests` 四条全搬，
// 断言消息用的是生成的 tests/scheduler/SchedulerTestMessages.h（码点表，勿手改）。
//
// 【不碰网络/进程】这里只测 refill_needed / refill_opts 两个纯函数——Rust 那边把判定和
// 副作用拆开就是为了让这条规则能离线测，runLoop 本体要 DB + 内核 + 网络，不在测试范围内。
#include <gtest/gtest.h>

#include <optional>

#include "core/TimeUtil.h"
#include "models/Settings.h"
#include "scheduler/SchedulerRules.h"
#include "scheduler/SchedulerTestMessages.h"
#include "speedtest/SpeedtestTypes.h"

namespace {

/// Rust 的 `const NOW: i64 = 1_700_000_000`（Unix 秒）。
constexpr qint64 kNow = 1700000000;

/// 对应 Rust 测试里的 `fn settings(refill, last_min_ago)`。
Models::AppSettings settings(bool refill, std::optional<qint64> lastMinutesAgo)
{
    Models::AppSettings value;
    value.speedtestAutoRefill = refill;
    value.lastAutoSpeedtestAt = lastMinutesAgo.has_value()
        ? TimeUtil::OptionalUtc::of((kNow - *lastMinutesAgo * 60) * Q_INT64_C(1000000000))
        : TimeUtil::OptionalUtc::none();
    return value;
}

}

// ---- Rust: off_means_never_running ----
TEST(SchedulerRefill, OffMeansNeverRunning)
{
    EXPECT_FALSE(Scheduler::refillNeeded(settings(false, std::nullopt), 5000, 0, kNow));
}

// ---- Rust: nothing_to_test_or_a_full_bench_means_no_run ----
TEST(SchedulerRefill, NothingToTestOrAFullBenchMeansNoRun)
{
    // 库里一个节点都没有：跑了也是空转。
    EXPECT_FALSE(Scheduler::refillNeeded(settings(true, std::nullopt), 0, 0, kNow));
    // 测通的已经够挑替补了。
    EXPECT_FALSE(Scheduler::refillNeeded(settings(true, std::nullopt), 5000,
                                         Scheduler::kSparseVerifiedFloor, kNow));
    EXPECT_FALSE(Scheduler::refillNeeded(settings(true, std::nullopt), 5000, 900, kNow));
    // 差一个就低于地板 -> 跑。
    EXPECT_TRUE(Scheduler::refillNeeded(settings(true, std::nullopt), 5000,
                                        Scheduler::kSparseVerifiedFloor - 1, kNow));
}

// ---- Rust: cooldown_is_measured_against_the_last_auto_speedtest ----
TEST(SchedulerRefill, CooldownIsMeasuredAgainstTheLastAutoSpeedtest)
{
    EXPECT_FALSE(Scheduler::refillNeeded(settings(true, 1), 5000, 0, kNow));
    EXPECT_FALSE(Scheduler::refillNeeded(settings(true, Scheduler::kRefillCooldownMin - 1), 5000, 0,
                                         kNow));
    EXPECT_TRUE(Scheduler::refillNeeded(settings(true, Scheduler::kRefillCooldownMin), 5000, 0,
                                        kNow));
    EXPECT_TRUE(Scheduler::refillNeeded(settings(true, 600), 5000, 0, kNow));
    // 从来没测过（时间戳为 NULL）= 立刻可以补一轮。
    EXPECT_TRUE(Scheduler::refillNeeded(settings(true, std::nullopt), 5000, 0, kNow));
}

// ---- Rust: refill_options_are_the_cheap_kind ----
TEST(SchedulerRefill, RefillOptionsAreTheCheapKind)
{
    Models::AppSettings base;
    base.speedtestTimeoutMs = 2500;
    base.speedtestReuseMinutes = 30;
    base.speedtestDownload = true;
    const Speedtest::SpeedTestOptions options = Scheduler::refillOpts(base);
    EXPECT_EQ(options.concurrency, Scheduler::kRefillConcurrency)
        << SchedulerTests::concurrencyNotGreedy().toStdString();
    EXPECT_EQ(options.concurrency, 8);
    EXPECT_FALSE(options.measureDownload)
        << SchedulerTests::noDownloadThroughput().toStdString();
    EXPECT_EQ(options.timeoutMs, 2500)
        << SchedulerTests::timeoutFollowsSettings().toStdString();
    EXPECT_EQ(options.reuseVerifiedMinutes, 30)
        << SchedulerTests::reuseRecentlyVerified().toStdString();
    // 其余四项仍然跟随设置，档位固定标准。
    EXPECT_EQ(options.intensity, Speedtest::SpeedtestIntensity::Standard);
    EXPECT_EQ(options.downloadTestMs, base.downloadTestMs);
    EXPECT_EQ(options.parallelKernels, base.speedtestParallelKernels);
    EXPECT_EQ(options.persistentKernel, base.speedtestPersistentKernel);
    EXPECT_EQ(options.conditionalIsolation, base.speedtestConditionalIsolation);
    EXPECT_EQ(options.downloadTopN, base.speedtestDownloadTopN);
}
