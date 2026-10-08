#pragma once

#include <QString>
#include <QVector>

#include <optional>

#include "core/Result.h"

// 单实例互斥量，对应 Rust src/platform/mod.rs 的 SingleInstanceGuard / acquire_single_instance。
//
// 名字是一个写死的 GUID，不是按 exe 路径推导的：这台机器上可能同时装着 Rust 建的
// vpn-parser.exe 和 C++ 建的实例，两边必须共用同一把锁，所以必须逐字一致。
namespace Platform {

/// Rust 的 `SINGLE_INSTANCE_MUTEX_NAME`，含 `Local\` 前缀。
QString singleInstanceMutexName();

/// 互斥量句柄的 RAII 包装：析构时 CloseHandle，锁随之释放（交接给提权实例就靠这个）。
class SingleInstanceGuard {
public:
    SingleInstanceGuard() = default;
    explicit SingleInstanceGuard(void *handle)
        : handle_(handle)
    {
    }
    SingleInstanceGuard(const SingleInstanceGuard &) = delete;
    SingleInstanceGuard &operator=(const SingleInstanceGuard &) = delete;
    SingleInstanceGuard(SingleInstanceGuard &&other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }
    SingleInstanceGuard &operator=(SingleInstanceGuard &&other) noexcept
    {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~SingleInstanceGuard();

    void *handle() const { return handle_; }
    bool isValid() const { return handle_ != nullptr; }

private:
    void close();
    void *handle_ = nullptr;
};

/// 拿一把指定名字的互斥量。Ok(empty) = 已经有别的实例持着（此时会去唤醒那个实例），
/// Err = CreateMutexW 本身失败。对应 Rust `acquire_single_instance`。
Result<std::optional<SingleInstanceGuard>> acquireSingleInstance();

/// 同上但允许指定名字，测试用它造一把只有测试进程知道的锁。对应 `acquire_single_instance_named`。
Result<std::optional<SingleInstanceGuard>> acquireSingleInstanceNamed(const QString &name);

/// 提权交接的轮询参数：最多 30 次 × 100ms = 3 秒（Rust main.rs 的 `for attempt in 1..=30`）。
constexpr int kHandoffMaxAttempts = 30;
constexpr int kHandoffAttemptDelayMs = 100;

/// 纯函数：交接轮询的行动计划（第几次尝试、失败后睡多久），便于不碰真锁就验证节奏。
struct HandoffPollStep {
    int attempt = 0;
    int sleepMsAfterFailure = 0;
};
QVector<HandoffPollStep> handoffPollPlan();

/// 提权实例走的接管路径：轮询等父进程释放互斥量（最多 3 秒），拿不到就清理重复实例再抢一次。
/// 对应 Rust main.rs 里 `if elevated_handoff { ... }` 那段。会杀进程，别在测试里调。
Result<std::optional<SingleInstanceGuard>> acquireSingleInstanceForHandoff();

}
