#pragma once

#include <QString>

#include <condition_variable>
#include <memory>
#include <mutex>

// collector/mod.rs 里的三个并发闸（Rust 用 tokio::sync::Semaphore，这里等价实现）：
// * 按主机名限流：PER_HOST_FETCH_CONCURRENCY（:41）
// * 镜像总并发：MIRROR_LIMIT（:78）
// * 解析并发：PARSE_LIMIT（:38）
//
// acquire() 是阻塞的，所以这些闸只能在 worker 线程上用（GUI 线程会卡住）。
// 对应 Rust 的 `acquire_owned().await`：拿不到许可就睡在条件变量上，
// 等待期间**不占用**请求超时预算——Collector 是先拿到许可、后开始计时的。
namespace Collector {

class Semaphore;
using SemaphorePtr = std::shared_ptr<class Semaphore>;

/// 计数信号量。许可由 Guard 持有，析构归还（对应 Rust 的 OwnedPermit）。
class Semaphore : public std::enable_shared_from_this<Semaphore> {
public:
    explicit Semaphore(int permits);

    /// 阻塞直到拿到一个许可。
    void acquire();
    /// 不阻塞：拿到返回 true（Rust 的 try_acquire）。
    bool tryAcquire();
    void release();
    /// 当前可用许可数（只用于断言/调试）。
    int available();

    /// RAII：构造时 acquire，析构时 release。不可复制，可移动。
    class Guard {
    public:
        Guard() = default;
        explicit Guard(SemaphorePtr semaphore);
        Guard(Guard &&other) noexcept;
        Guard &operator=(Guard &&other) noexcept;
        Guard(const Guard &other) = delete;
        Guard &operator=(const Guard &other) = delete;
        ~Guard();
        bool holds() const { return semaphore_ != nullptr; }

    private:
        SemaphorePtr semaphore_;
    };

    /// 拿到一个许可并包成 RAII（对应 `semaphore.clone().acquire_owned()`）。
    Guard acquireGuard();

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    int permits_;
};

/// 镜像总并发闸（MIRROR_LIMIT = 6，:78）。进程级共享，对应 Rust 的 static Lazy。
SemaphorePtr mirrorLimit();
/// 解析闸（PARSE_LIMIT = 4，:38）。
SemaphorePtr parseLimit();
/// 按 URL 主机名取（必要时建）限流闸，对应 `fetch_host_limit(&url)`（:1110）。
/// 键是 host 小写；解析不出 host 时整条 URL 小写当键（Rust 同一条 `unwrap_or_else`）。
/// 只还有活着的引用（有许可在飞）的闸才留在表里，和 Rust 的 Weak + retain 一样。
SemaphorePtr hostLimit(const QString &url);
/// `fetch_host_limit` 的键计算，单独暴露是为了能测到"同一主机共用一个闸"这条。
QString hostKey(const QString &url);
/// 表里当前还活着的按主机闸数量（测试用，验证 Weak 语义没漏）。
int liveHostLimitCount();

} // namespace Collector
