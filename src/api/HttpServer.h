#pragma once

#include <QString>

#include <atomic>
#include <memory>
#include <optional>

#include "api/ApiDepends.h"
#include "core/Result.h"

namespace Api {

class Router;

/// 对应 Rust 的 `serve` / `serve_with_shutdown`：在**调用线程**上绑定监听地址并跑事件
/// 循环，直到 shutdown 标志被置起才返回。阻塞语义与 Rust 的 async 版本一致（都要放到
/// 后台线程里调用，ApiServerController 就是这么包装的）。
Result<Status> serve(const AppState &state, const QString &listen,
                     const std::optional<QString> &apiToken);

/// shutdownRequested 被置为 true 后：停止接受新连接，等在途请求写完，退出事件循环。
/// 对应 `axum::serve(..).with_graceful_shutdown(..)`。
Result<Status> serveWithShutdown(const AppState &state, const QString &listen,
                                 const std::optional<QString> &apiToken,
                                 const std::atomic_bool &shutdownRequested);

} // namespace Api
