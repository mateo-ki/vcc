#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QWaitCondition>
#include <QString>

#include <algorithm>
#include <deque>
#include <functional>
#include <utility>


namespace ProxyProbe {

/// 一次 join 的结果，对应 Rust 的 `Result<T, JoinError>`：failed 就是 JoinError
/// （任务 panic 或被取消），调用方按 Rust 的写法决定是冒泡还是丢弃。
template <typename T>
struct Joined {
    bool failed = false;
    QString error;
    T value{};
};

/// tokio `JoinSet` 的等价物：有界并发窗口 + 按"完成顺序"join。
///
/// 对应关系：
///   - `trySpawn` 就是 Rust 的 `while tasks.len() < N { tasks.spawn(..) }`：
///     窗口（已提交未被 join 的任务数）满了就返回 false，什么都不做；
///   - `joinNext` 就是 `tasks.join_next().await`：拿最先完成的那个，不是提交顺序；
///     没有更多任务时返回 false（Rust 的 `None` -> `break`）。
///
/// 与 Rust 的差别（必须知道）：tokio 的一个任务只是一段状态机，96 个并发探针只占几个
/// OS 线程；QtConcurrent 没有 async，这里一个在跑的任务占一条线程，所以线程数被钉在
/// capacity 上，并且用**专用** QThreadPool —— 全局池只有 idealThreadCount 条线程，
/// 借它跑 96 个阻塞探针会把并发度压成个位数，还会抢掉别的层的线程。
///
/// T 必须可默认构造（任务异常逃逸时 Joined<T>::value 是空的）。
template <typename T>
class JoinWindow {
public:
    using Job = std::function<T()>;

    explicit JoinWindow(qsizetype capacity)
        : capacity_(std::max<qsizetype>(1, capacity))
    {
        pool_.setExpiryTimeout(-1);
        pool_.setMaxThreadCount(int(capacity_));
    }

    ~JoinWindow()
    {
        // 任务可能引用调用栈上的东西（比如 results 槽位数组的拥有者），
        // 窗口对象离开作用域前必须让所有在跑的活儿结束。
        pool_.waitForDone(-1);
    }

    JoinWindow(const JoinWindow &) = delete;
    JoinWindow &operator=(const JoinWindow &) = delete;

    qsizetype capacity() const { return capacity_; }

    /// 对应 Rust 的 `tasks.len()`。
    qsizetype unfinished() const
    {
        QMutexLocker locker(&mutex_);
        return spawned_ - joined_;
    }

    bool trySpawn(const Job &job)
    {
        if (!job) {
            return false;
        }
        {
            QMutexLocker locker(&mutex_);
            if (spawned_ - joined_ >= capacity_) {
                return false;
            }
            ++spawned_;
        }
        pool_.start([this, job]() {
            complete(job);
        });
        return true;
    }

    /// 阻塞直到有结果可取；全部任务都已 join 完时返回 false。
    bool joinNext(Joined<T> *out)
    {
        if (out == nullptr) {
            return false;
        }
        QMutexLocker locker(&mutex_);
        // 等待条件不能写成 unfinished() == 0：那个数把还躺在 ready_ 里的结果也算成
        // 未完成，结果就是永远等不到"全部完成"。看"已归还数 == 已提交数"才对。
        while (ready_.empty() && returned_ < spawned_) {
            condition_.wait(&mutex_);
        }
        if (ready_.empty()) {
            return false;
        }
        *out = std::move(ready_.front());
        ready_.pop_front();
        ++joined_;
        return true;
    }

private:
    void complete(const Job &job)
    {
        Joined<T> entry;
        try {
            entry.value = job();
        } catch (const std::exception &error) {
            // Rust 侧只有 panic/取消会走到 JoinError；这里对应"任务体抛异常"。
            entry.failed = true;
            entry.error = QString::fromUtf8(error.what());
        } catch (...) {
            entry.failed = true;
            entry.error = QStringLiteral("task aborted");
        }
        {
            QMutexLocker locker(&mutex_);
            ready_.push_back(std::move(entry));
            ++returned_;
        }
        condition_.wakeAll();
    }

    qsizetype capacity_ = 1;
    QThreadPool pool_;
    mutable QMutex mutex_;
    QWaitCondition condition_;
    std::deque<Joined<T>> ready_;
    qsizetype spawned_ = 0;
    qsizetype returned_ = 0;
    qsizetype joined_ = 0;
};

}
