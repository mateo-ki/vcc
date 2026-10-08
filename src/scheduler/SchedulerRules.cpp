#include "scheduler/SchedulerRules.h"

#include "core/TimeUtil.h"

namespace Scheduler {

bool refillNeeded(const Models::AppSettings &settings, qint64 total, qint64 verified,
                  qint64 nowEpochSecs)
{
    if (!settings.speedtestAutoRefill || total == 0 || verified >= kSparseVerifiedFloor) {
        return false;
    }
    if (!settings.lastAutoSpeedtestAt.hasValue) {
        return true;
    }
    // Rust 的 DateTime::timestamp() 是向下取整的 Unix 秒；纳秒在这里恒为非负，
    // 整除结果与 floor 一致。
    const qint64 lastSeconds = settings.lastAutoSpeedtestAt.nanos / Q_INT64_C(1000000000);
    return nowEpochSecs - lastSeconds >= kRefillCooldownMin * 60;
}

Speedtest::SpeedTestOptions refillOpts(const Models::AppSettings &settings)
{
    Speedtest::SpeedTestOptions options;
    options.concurrency = kRefillConcurrency;
    options.timeoutMs = settings.speedtestTimeoutMs;
    options.measureDownload = false;
    options.downloadTestMs = settings.downloadTestMs;
    options.intensity = Speedtest::SpeedtestIntensity::Standard;
    options.parallelKernels = settings.speedtestParallelKernels;
    options.persistentKernel = settings.speedtestPersistentKernel;
    options.reuseVerifiedMinutes = settings.speedtestReuseMinutes;
    options.conditionalIsolation = settings.speedtestConditionalIsolation;
    options.downloadTopN = settings.speedtestDownloadTopN;
    return options;
}

}
