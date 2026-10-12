#pragma once

#include <QRunnable>
#include <QSemaphore>
#include <QThreadPool>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace Speedtest {

/// 对应 Rust 的 `tokio::task::JoinSet` + 「`while inflight.len() < concurrency` 补任务，
/// 再 `join_next().await` 收一个」那个循环。
///
/// 分工：`compute` 在 worker 线程里阻塞地跑（TCP/内核等待都在内核里睡），它返回一个
/// `applier` 闭包；`applier` 只在**驱动线程**（调用 joinNext 的那条线程）上执行。
/// 所以 results 追加、进度回调、计数这些有状态的操作天然是单线程的，和 Rust 把
/// join_next 的结果交给主任务处理一模一样，不需要给它们加锁。
///
/// 用专用 QThreadPool（不是全局池）：全局池的线程被别人占满时，这里再排队就会互相等死。
/// 对象必须是**活的**直到所有作业收尾（析构里 waitForDone 会等），所以只在栈上用，不外传指针。
class JobQueue {
public:
    /// 完成项：在驱动线程执行。
    using Applier = std::function<void()>;
    /// worker 线程算完返回的那个闭包。
    using Compute = std::function<Applier()>;

    explicit JobQueue(int workerThreads)
        : capacity_(qMax(1, workerThreads)), slots_(capacity_)
    {
        pool_.setMaxThreadCount(capacity_);
    }

    ~JobQueue() { pool_.waitForDone(); }

    JobQueue(const JobQueue &) = delete;
    JobQueue &operator=(const JobQueue &) = delete;

    /// 未收的作业达到并发上限时阻塞，直到有 worker 交回结果 —— 对应 Rust 的补任务循环。
    ///
    /// ⚠ 这里必须**顺手消费**已经交回的完成项：Rust 的节奏是「补到 concurrency 个就
    /// join_next().await」。原来只 acquire 信号量就返回，applier 全堆到 waitAll 才执行 ——
    /// 于是整轮测速的进度回调在跑完之前一个都到不了界面（15175 个节点时状态栏一直
    /// 停在"已完成 0/15175"）。
    void submit(const Compute &compute)
    {
        for (;;) {
            int delivered = 0;
            int outstanding = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                delivered = delivered_;
                outstanding = outstanding_;
            }
            if (delivered == 0 && outstanding < capacity_) {
                break;
            }
            joinNext();      // 有结果就先在驱动线程上执行掉（applier 里会发进度回调）
        }
        slots_.acquire();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ++outstanding_;
        }
        pool_.start(new Task(this, compute));
    }

    /// 取一个完成项并在调用线程执行；没有未完成作业时返回 false。
    bool joinNext()
    {
        Applier applier;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !appliers_.empty() || outstanding_ == 0; });
            if (appliers_.empty()) {
                return false;
            }
            applier = std::move(appliers_.front());
            appliers_.pop_front();
            --delivered_;
            --outstanding_;
        }
        applier();
        return true;
    }

    /// 收掉所有还在排队的完成项（对应 `while let Some(joined) = tasks.join_next().await`）。
    void waitAll()
    {
        while (joinNext()) {
        }
    }

    /// 还没收的完成项数量（对应 Rust 的 `inflight.len()`）。
    int outstanding() const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return outstanding_;
    }

    /// 并发上限（对应 Rust 的 `concurrency`）。
    int capacity() const { return capacity_; }

private:
    class Task : public QRunnable {
    public:
        Task(JobQueue *owner, const Compute &compute) : owner_(owner), compute_(compute)
        {
            setAutoDelete(true);
        }

        void run() override
        {
            Applier applier = compute_();
            owner_->publish(std::move(applier));
        }

    private:
        JobQueue *owner_;
        Compute compute_;
    };

    void publish(Applier applier)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            appliers_.push_back(std::move(applier));
            ++delivered_;
        }
        cv_.notify_one();
        slots_.release();
    }

    QThreadPool pool_;
    int capacity_ = 1;
    QSemaphore slots_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Applier> appliers_;
    int outstanding_ = 0;
    int delivered_ = 0;
    /// 已交回但还没被驱动线程执行的完成项数量（submit 的"先消费再提交"看它）。
};

}
