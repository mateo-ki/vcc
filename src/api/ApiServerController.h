#pragma once

#include <QString>

#include <atomic>
#include <memory>
#include <optional>

#include "api/ApiDepends.h"
#include "core/Result.h"

namespace Api {

/// 对应 Rust 的 `ApiServerController`：持有一份 AppState + 监听地址 + 令牌，
/// start/stop 动态开关 HTTP API（GUI 的「外部接口」开关就是它）。
///
/// 拷贝共享同一个内部状态（Rust 的 `#[derive(Clone)]` + `Arc`）。
/// start() 立刻返回：绑定失败只写日志、running 自己回落，和 Rust 的
/// `tokio::spawn(serve_with_shutdown(..))` 一样不把错误抛回调用方。
class ApiServerController {
public:
    ApiServerController(AppState state, QString listen, std::optional<QString> apiToken);
    ApiServerController(const ApiServerController &) = default;
    ApiServerController &operator=(const ApiServerController &) = default;

    /// 对应 `is_running`（AtomicBool::load）。
    bool isRunning() const;
    /// 对应 `listen_addr`：配置里那个字符串，不是实际绑定结果。
    QString listenAddr() const;
    /// 对应 `token`：nullopt = 未启用鉴权。
    std::optional<QString> token() const;

    /// 对应 `start`：已在跑就直接返回成功。
    Status start();
    /// 对应 `stop`：通知关停并立刻返回（不等线程真退出，对应 Rust 的 oneshot send）。
    Status stop();

private:
    struct Inner;
    std::shared_ptr<Inner> inner_;
};

} // namespace Api
