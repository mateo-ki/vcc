#include "scheduler/Scheduler.h"

#include <QDateTime>
#include <QMutexLocker>

#include <optional>
#include <utility>

#include "core/Log.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "models/Settings.h"
#include "models/Types.h"
#include "scheduler/SchedulerMessages.h"
#include "speedtest/SpeedTester.h"
#include "speedtest/SpeedtestTypes.h"

namespace Scheduler {
namespace {

/// Rust 日志里的 `avg = ?sum.avg_ms`：Option<f64> 的 Debug 形态，只进日志。
QString debugDouble(const std::optional<double> &value)
{
    if (!value.has_value()) {
        return QStringLiteral("None");
    }
    return QStringLiteral("Some(%1)").arg(*value);
}

/// chrono::Utc::now().timestamp()。
qint64 nowEpochSeconds()
{
    return TimeUtil::nowUtcNanos() / Q_INT64_C(1000000000);
}

}

Scheduler::Scheduler(std::shared_ptr<Database> db, Collector::Collector collector,
                     Speedtest::SpeedTester speedtester)
    : db_(std::move(db))
    , collector_(std::move(collector))
    , speedtester_(std::move(speedtester))
    , running_(std::make_shared<std::atomic<bool>>(false))
    , wake_(std::make_shared<WakeState>())
{
}

void Scheduler::requestStop()
{
    {
        QMutexLocker lock(&wake_->mutex);
        wake_->stopped.store(true, std::memory_order_relaxed);
    }
    wake_->condition.wakeAll();
}

void Scheduler::runLoop()
{
    // tokio::time::interval(30s) + MissedTickBehavior::Skip：interval 的第一拍是立刻
    // 触发的，Rust 先 tick() 吃掉它，所以这里也从「30s 之后」开始。
    qint64 nextTickAtMs = QDateTime::currentMSecsSinceEpoch() + kTickIntervalMs;
    for (;;) {
        bool stopped = false;
        {
            QMutexLocker lock(&wake_->mutex);
            while (!wake_->stopped.load(std::memory_order_relaxed)) {
                const qint64 remaining = nextTickAtMs - QDateTime::currentMSecsSinceEpoch();
                if (remaining <= 0) {
                    break;
                }
                wake_->condition.wait(&wake_->mutex, ulong(remaining));
            }
            stopped = wake_->stopped.load(std::memory_order_relaxed);
        }
        if (stopped) {
            return;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now < nextTickAtMs) {
            continue;   // 假唤醒
        }
        // Skip 语义：落后了很多拍也只补跑一次，把基准直接推到未来。
        while (nextTickAtMs <= now) {
            nextTickAtMs += kTickIntervalMs;
        }
        runTick();
    }
}

void Scheduler::runTick()
{
    if (running_->load(std::memory_order_relaxed)) {
        return;
    }

    const Result<Models::AppSettings> loaded = db_->getSettings();
    if (loaded.isErr()) {
        Log::warn(QStringLiteral("scheduler: load settings failed error=%1").arg(loaded.error()));
        return;
    }
    const Models::AppSettings settings = loaded.value();
    const qint64 now = nowEpochSeconds();

    // 替补池空了就先补一轮。这条刻意放在下面 auto_fetch_enabled 那个 early return
    // **之前**：它服务的是故障转移，不是订阅更新，不该被拉取开关门掉。
    if (settings.speedtestAutoRefill) {
        const Result<QPair<qint64, qint64>> counts = db_->nodeVerificationCounts();
        if (counts.isOk()
            && refillNeeded(settings, counts.value().first, counts.value().second, now)) {
            running_->store(true, std::memory_order_relaxed);
            Log::info(QStringLiteral("%1 verified=%2 total=%3 floor=%4 concurrency=%5")
                          .arg(Messages::refillSparseLog())
                          .arg(counts.value().second)
                          .arg(counts.value().first)
                          .arg(kSparseVerifiedFloor)
                          .arg(kRefillConcurrency));
            const Result<Models::SpeedTestSummary> summary =
                speedtester_.testAllSources(refillOpts(settings));
            if (summary.isOk()) {
                Log::info(QStringLiteral("%1 ok=%2 fail=%3 avg=%4")
                              .arg(Messages::refillDoneLog())
                              .arg(summary.value().ok)
                              .arg(summary.value().fail)
                              .arg(debugDouble(summary.value().avgMs)));
            } else {
                Log::warn(QStringLiteral("%1 error=%2")
                              .arg(Messages::refillFailedLog())
                              .arg(summary.error()));
            }
            // 成败都要记这个时间戳。环境坏掉的时候这一条每次都返回 Err（测速内核起不来），
            // 时间戳一直是 NULL，refill_needed 就永远成立 —— 于是每 30 秒无条件重跑一轮
            // 全量测速。冷却是给失败用的，不是只给成功用的。
            db_->touchAutoSpeedtest();
            running_->store(false, std::memory_order_relaxed);
        } else if (counts.isErr()) {
            Log::warn(QStringLiteral("%1 error=%2")
                          .arg(Messages::refillCountFailedLog())
                          .arg(counts.error()));
        }
    }

    if (!settings.autoFetchEnabled) {
        return;
    }

    const qint64 intervalMs = qMax<qint64>(5, settings.autoFetchIntervalMin) * 60 * 1000;
    bool due = true;
    if (settings.lastAutoFetchAt.hasValue) {
        // signed_duration_since(...).to_std().unwrap_or(ZERO)：时钟倒挂当成「没到点」。
        const qint64 elapsedNanos = TimeUtil::nowUtcNanos() - settings.lastAutoFetchAt.nanos;
        due = elapsedNanos >= 0 && elapsedNanos / Q_INT64_C(1000000) >= intervalMs;
    }
    if (!due) {
        return;
    }

    running_->store(true, std::memory_order_relaxed);
    Log::info(QStringLiteral("scheduler: auto-fetch starting interval_min=%1")
                  .arg(settings.autoFetchIntervalMin));

    const Result<QVector<Models::FetchResult>> fetched = collector_.fetchAllEnabled();
    if (fetched.isOk()) {
        int ok = 0;
        qint64 nodes = 0;
        for (const Models::FetchResult &result : fetched.value()) {
            if (result.success) {
                ++ok;
            }
            nodes += result.nodeCount;
        }
        Log::info(QStringLiteral("scheduler: auto-fetch done ok=%1 total=%2 nodes=%3")
                      .arg(ok)
                      .arg(qint64(fetched.value().size()))
                      .arg(nodes));
        db_->touchAutoFetch();

        if (settings.autoSpeedtestEnabled && nodes > 0) {
            Log::info(QStringLiteral("scheduler: auto-speedtest starting"));
            Speedtest::SpeedTestOptions options;
            options.concurrency = settings.speedtestConcurrency;
            options.timeoutMs = settings.speedtestTimeoutMs;
            options.measureDownload = settings.speedtestDownload;
            options.downloadTestMs = settings.downloadTestMs;
            options.intensity = Speedtest::SpeedtestIntensity::Standard;
            options.parallelKernels = settings.speedtestParallelKernels;
            options.persistentKernel = settings.speedtestPersistentKernel;
            options.reuseVerifiedMinutes = settings.speedtestReuseMinutes;
            options.conditionalIsolation = settings.speedtestConditionalIsolation;
            options.downloadTopN = settings.speedtestDownloadTopN;
            const Result<Models::SpeedTestSummary> summary =
                speedtester_.testAllSources(options);
            if (summary.isOk()) {
                Log::info(QStringLiteral("scheduler: auto-speedtest done ok=%1 fail=%2 avg=%3 avg_dl=%4")
                              .arg(summary.value().ok)
                              .arg(summary.value().fail)
                              .arg(debugDouble(summary.value().avgMs))
                              .arg(debugDouble(summary.value().avgDownloadKbps)));
                db_->touchAutoSpeedtest();
            } else {
                Log::warn(QStringLiteral("scheduler: auto-speedtest failed error=%1")
                              .arg(summary.error()));
            }
        }
    } else {
        Log::warn(QStringLiteral("scheduler: auto-fetch failed error=%1").arg(fetched.error()));
    }

    running_->store(false, std::memory_order_relaxed);
}

}
