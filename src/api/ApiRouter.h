#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

#include "api/ApiDepends.h"
#include "api/ApiHandlers.h"
#include "api/HttpMessage.h"

namespace Api {

/// handler 的统一签名：请求 + 已抽取的 `{id}` 路径参数 → 完整响应。
using HandlerFn = std::function<Response(const Request &, const PathParams &)>;

/// 对应 `fn router(state, api_token)`：29 条路由 + 一层全站鉴权中间件。
///
/// 故意不挂任何 CORS 头（Rust 注释：没有跨域头，第三方网页读不到任何响应体）。
/// dispatch() 只做纯函数式的路由 + 鉴权 + handler 调用，不碰 socket，所以路由表和
/// handler 都能直接单测。
class Router {
public:
    Router(AppState state, std::optional<QString> apiToken);

    /// 鉴权 → 匹配路由 → 调 handler。对应 axum 的整条中间件链。
    Response dispatch(const Request &request) const;

    /// 对应 `authenticate_request`：期望令牌为 nullopt 时全放行；
    /// 否则要求 `Authorization: Bearer <token>`，只读 GET 另外接受未解码的 `?token=`。
    /// 返回 nullopt 表示放行。
    static std::optional<Response> authenticate(const Request &request,
                                                const std::optional<QString> &expected);

    /// 路由表自省（单测拿它核对 29 条路由与 Rust 一致）。
    struct RouteSpec {
        QString pattern;
        QStringList methods; // 注册顺序，HEAD 不单独列出（get 路由隐含吃 HEAD）
    };
    static QVector<RouteSpec> routeTable();

    /// 把路径切成段，"api"/"sources"/"{id}"。
    static QStringList splitPath(const QString &path);

private:
    /// 一条路由：段列表 + 按注册顺序排列的 (method, handler)。
    struct Route {
        QString pattern;
        QStringList segments;
        QVector<QPair<QString, HandlerFn>> handlers;
    };

    /// 路由表的 handler 绑定：与 routeTable() 一一对应。
    static QVector<Route> buildRoutes(const Handlers &handlers);
    /// 匹配 + 抽取路径参数（`{id}` → params["id"]）。
    static bool matchRoute(const Route &route, const QStringList &segments, PathParams &params);
    static int paramCount(const Route &route);
    /// 对应 axum 的 MethodRouter 默认 fallback：405 空 body + Allow 头。
    static Response methodNotAllowed(const Route &route);

    AppState state_;
    std::optional<QString> apiToken_;
    Handlers handlers_;
    QVector<Route> routes_;
};

/// 对应 Rust 的 `serve` / `serve_with_shutdown`：绑定地址、跑事件循环直到优雅关停。
/// 在调用线程上阻塞，需要在后台线程里跑（ApiServerController 就是这么做的）。
Result<Status> serve(const AppState &state, const QString &listen,
                     const std::optional<QString> &apiToken);

/// shutdown 触发后退出事件循环并回 503 之前先在途请求。对应 oneshot::Receiver<()>。
Result<Status> serveWithShutdown(const AppState &state, const QString &listen,
                                 const std::optional<QString> &apiToken,
                                 const std::atomic_bool &shutdownRequested);

} // namespace Api
