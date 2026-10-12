#pragma once

#include <QString>

#include <functional>

#include "core/Result.h"

namespace App {

/// Rust 的 `on_exit` 给 `proxy_manager.stop()` 的那条上限（gui/mod.rs:4657-4660）。
inline constexpr int kExitProxyStopTimeoutMs = 8000;

struct ExitStopOutcome {
    /// false = 到点还没停完。Rust 的对应日志是 "proxy stop on GUI exit timed out after 8s;
    /// orphan core is cleaned up on next start" —— 残留内核不在这儿收，交给下次启动。
    bool completed = false;
    /// stop() 自己给出的失败原因；空 = 没失败（或根本没等到）。
    QString error;
};

/// 退出路径上同步等一次"关代理"：闭包在后台线程执行，本函数最多等 `timeoutMs`。
/// 线程抓的是闭包（里面通常是一份 shared_ptr<AppContext>），所以超时后不等它、让它自己跑完
/// 也不会碰到已析构的对象 —— 这正是 Rust 用 `tokio::time::timeout` 包住 stop() 的原因。
ExitStopOutcome stopProxyOnExit(const std::function<Status()> &stop,
                                int timeoutMs = kExitProxyStopTimeoutMs);

} // namespace App
