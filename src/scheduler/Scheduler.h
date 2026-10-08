#pragma once

#include <QMutex>
#include <QWaitCondition>

#include <atomic>
#include <memory>

#include "collector/Collector.h"
#include "scheduler/SchedulerRules.h"
#include "speedtest/SpeedTester.h"

class Database;

namespace Scheduler {

/// 自动拉取与可选自动测速的后台调度器，对应 Rust 的 `struct Scheduler`（:49）。
///
/// 与 Rust 的两点必要差别：
/// - `run_loop` 是个永不返回的 tokio 任务；这里 runLoop() 阻塞在调用线程上，靠
///   requestStop() 收尾（C++ 的线程不停下来，进程退出时 QThreadPool 会等死）。
/// - `Arc<AtomicBool>` 的可复制语义用 shared_ptr<atomic> 保持：拷贝共享同一个闸门。
class Scheduler {
public:
    Scheduler(std::shared_ptr<Database> db, Collector::Collector collector,
              Speedtest::SpeedTester speedtester);

    /// 对应 `run_loop`（:66）：每 30s 看一次该不该跑。只能在后台线程上调用。
    void runLoop();

    /// 让 runLoop() 尽快返回；调用后这个实例就作废了（Rust 靠进程退出，没有这一步）。
    void requestStop();
    bool isStopped() const { return wake_->stopped.load(std::memory_order_relaxed); }

    /// 30s 的检查间隔（Rust: tokio::time::interval(Duration::from_secs(30))）。
    inline static constexpr qint64 kTickIntervalMs = 30 * 1000;

private:
    /// runLoop 里一拍的工作，对应 Rust 循环体。
    void runTick();

    /// 停 loop 用的等待状态。放在共享指针里是为了让这个类保持可复制
    /// （Rust 的 `#[derive(Clone)]` + `Arc<AtomicBool>` 就是共享同一份闸门）。
    struct WakeState {
        QMutex mutex;
        QWaitCondition condition;
        std::atomic<bool> stopped{false};
    };

    std::shared_ptr<Database> db_;
    Collector::Collector collector_;
    Speedtest::SpeedTester speedtester_;
    /// 一轮任务在跑就不进第二拍（Rust 的 running: Arc<AtomicBool>）。
    std::shared_ptr<std::atomic<bool>> running_;
    std::shared_ptr<WakeState> wake_;
};

}
