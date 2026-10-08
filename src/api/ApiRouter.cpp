#include "api/ApiRouter.h"

#include <QByteArray>

#include "api/ApiMessages.h"
#include "db/Database.h"
#include "tasks/TaskTracker.h"

namespace Api {

namespace {

bool segmentIsParam(const QString &segment)
{
    return segment.startsWith(QLatin1Char('{')) && segment.endsWith(QLatin1Char('}'));
}

} // namespace

QStringList Router::splitPath(const QString &path)
{
    QStringList segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    while (!segments.isEmpty() && segments.first().isEmpty()) {
        segments.removeFirst();
    }
    return segments;
}

bool Router::matchRoute(const Route &route, const QStringList &segments, PathParams &params)
{
    if (route.segments.size() != segments.size()) {
        return false;
    }
    for (int index = 0; index < route.segments.size(); ++index) {
        const QString &pattern = route.segments.at(index);
        if (segmentIsParam(pattern)) {
            params.insert(pattern.mid(1, pattern.size() - 2), segments.at(index));
        } else if (pattern != segments.at(index)) {
            return false;
        }
    }
    return true;
}

int Router::paramCount(const Route &route)
{
    int count = 0;
    for (const QString &segment : route.segments) {
        if (segmentIsParam(segment)) {
            ++count;
        }
    }
    return count;
}

/// 对应 axum 的 MethodRouter 默认 fallback：405 空 body + Allow 头。
/// Allow 里 get 路由展开成 "GET,HEAD"（axum 把 get/head 成对注册）。
Response Router::methodNotAllowed(const Route &route)
{
    QStringList allowed;
    for (const auto &entry : route.handlers) {
        if (entry.first == QLatin1String("GET")) {
            allowed << QStringLiteral("GET") << QStringLiteral("HEAD");
        } else {
            allowed << entry.first;
        }
    }
    Response response = Response::empty(405);
    response.setHeader(QStringLiteral("allow"), allowed.join(QLatin1Char(',')));
    return response;
}

Router::Router(AppState state, std::optional<QString> apiToken)
    : state_(state), apiToken_(std::move(apiToken)), handlers_(state_),
      routes_(buildRoutes(handlers_))
{
}

std::optional<Response> Router::authenticate(const Request &request,
                                             const std::optional<QString> &expected)
{
    if (!expected.has_value()) {
        return std::nullopt;
    }
    std::optional<QString> fromHeader;
    const std::optional<QString> authorization =
        request.headerOrNull(QStringLiteral("authorization"));
    if (authorization.has_value() && authorization->startsWith(QLatin1String("Bearer "))) {
        fromHeader = authorization->mid(7);
    }
    // 只读 GET 额外接受 ?token=：导出订阅链接要在浏览器里直接打开。
    std::optional<QString> fromQuery;
    if (request.isGet()) {
        const std::optional<QString> provided = request.rawQueryValue(QStringLiteral("token"));
        if (provided.has_value() && !provided->isEmpty()) {
            fromQuery = provided;
        }
    }
    const bool authorized = (fromHeader.has_value() && fromHeader == expected)
        || (fromQuery.has_value() && fromQuery == expected);
    if (authorized) {
        return std::nullopt;
    }
    Response response = Http::error(401, Messages::authFailed());
    response.setHeader(QStringLiteral("www-authenticate"), QStringLiteral("Bearer"));
    return response;
}

QVector<Router::RouteSpec> Router::routeTable()
{
    // 顺序与 Rust 的 `fn router` 完全一致（29 条）。
    return {
        {QStringLiteral("/api/health"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/tasks"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/stats"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/settings"), {QStringLiteral("GET"), QStringLiteral("PUT")}},
        {QStringLiteral("/api/sources"), {QStringLiteral("GET"), QStringLiteral("POST")}},
        {QStringLiteral("/api/sources/{id}"),
         {QStringLiteral("GET"), QStringLiteral("PUT"), QStringLiteral("DELETE")}},
        {QStringLiteral("/api/sources/{id}/fetch"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/sources/fetch-all"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/gist/discover"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/nodes"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/nodes/{id}"), {QStringLiteral("GET"), QStringLiteral("DELETE")}},
        {QStringLiteral("/api/nodes/clear"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/nodes/delete-dead"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/nodes/delete-invalid"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/nodes/dedupe"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/nodes/{id}/speedtest"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/nodes/speedtest"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/kernels"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/kernels/releases"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/kernels/update"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/proxy/status"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/proxy/config"), {QStringLiteral("GET"), QStringLiteral("PUT")}},
        {QStringLiteral("/api/proxy/log"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/proxy/start"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/proxy/stop"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/proxy/restart"), {QStringLiteral("POST")}},
        {QStringLiteral("/api/export/json"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/export/clash"), {QStringLiteral("GET")}},
        {QStringLiteral("/api/export/uri"), {QStringLiteral("GET")}},
    };
}

Response Router::dispatch(const Request &request) const
{
    if (const std::optional<Response> denied = authenticate(request, apiToken_)) {
        return *denied;
    }

    const QStringList segments = splitPath(request.path);
    // axum 的 PathRouter 静态段优先：先按占位符个数最少挑，再按注册顺序。
    const Route *best = nullptr;
    int bestParams = 0;
    for (const Route &route : routes_) {
        PathParams probe;
        if (!matchRoute(route, segments, probe)) {
            continue;
        }
        const int params = paramCount(route);
        if (best == nullptr || params < bestParams) {
            best = &route;
            bestParams = params;
        }
    }
    if (best == nullptr) {
        // 对应 Router 的默认 fallback：404，空 body。
        return Response::empty(404);
    }

    QString method = request.method;
    bool headRequest = false;
    if (method == QLatin1String("HEAD")) {
        // axum 的 get 路由也吃 HEAD，但 hyper 会把 body 去掉。
        headRequest = true;
        method = QStringLiteral("GET");
    }
    HandlerFn handler;
    for (const auto &entry : best->handlers) {
        if (entry.first == method) {
            handler = entry.second;
            break;
        }
    }
    if (!handler) {
        return methodNotAllowed(*best);
    }

    PathParams params;
    matchRoute(*best, segments, params);
    Response response = handler(request, params);
    if (headRequest && !response.body.isEmpty()) {
        response.body.clear(); // content-length 保持不变，与 hyper 的 HEAD 处理一致
    }
    return response;
}

QVector<Router::Route> Router::buildRoutes(const Handlers &handlers)
{
    auto bind = [](const QString &pattern, const QVector<QPair<QString, HandlerFn>> &entries) {
        Route route;
        route.pattern = pattern;
        route.segments = Router::splitPath(pattern);
        route.handlers = entries;
        return route;
    };
    using Member = Response (Handlers::*)(const Request &, const PathParams &) const;
    auto wrap = [&handlers](Member member) -> HandlerFn {
        return [&handlers, member](const Request &request, const PathParams &params) {
            return (handlers.*member)(request, params);
        };
    };
    const QString get = QStringLiteral("GET");
    const QString post = QStringLiteral("POST");
    const QString put = QStringLiteral("PUT");
    const QString del = QStringLiteral("DELETE");

    QVector<Route> routes;
    routes.append(bind(QStringLiteral("/api/health"), {{get, wrap(&Handlers::health)}}));
    routes.append(bind(QStringLiteral("/api/tasks"), {{get, wrap(&Handlers::listTasks)}}));
    routes.append(bind(QStringLiteral("/api/stats"), {{get, wrap(&Handlers::stats)}}));
    routes.append(bind(QStringLiteral("/api/settings"),
                       {{get, wrap(&Handlers::getSettings)},
                        {put, wrap(&Handlers::updateSettings)}}));
    routes.append(bind(QStringLiteral("/api/sources"),
                       {{get, wrap(&Handlers::listSources)},
                        {post, wrap(&Handlers::createSource)}}));
    routes.append(bind(QStringLiteral("/api/sources/{id}"),
                       {{get, wrap(&Handlers::getSource)},
                        {put, wrap(&Handlers::updateSource)},
                        {del, wrap(&Handlers::deleteSource)}}));
    routes.append(
        bind(QStringLiteral("/api/sources/{id}/fetch"), {{post, wrap(&Handlers::fetchSource)}}));
    routes.append(bind(QStringLiteral("/api/sources/fetch-all"), {{post, wrap(&Handlers::fetchAll)}}));
    routes.append(
        bind(QStringLiteral("/api/gist/discover"), {{post, wrap(&Handlers::discoverGists)}}));
    routes.append(bind(QStringLiteral("/api/nodes"), {{get, wrap(&Handlers::listNodes)}}));
    routes.append(bind(QStringLiteral("/api/nodes/{id}"),
                       {{get, wrap(&Handlers::getNode)}, {del, wrap(&Handlers::deleteNode)}}));
    routes.append(bind(QStringLiteral("/api/nodes/clear"), {{post, wrap(&Handlers::clearNodes)}}));
    routes.append(
        bind(QStringLiteral("/api/nodes/delete-dead"), {{post, wrap(&Handlers::deleteDeadNodes)}}));
    routes.append(bind(QStringLiteral("/api/nodes/delete-invalid"),
                       {{post, wrap(&Handlers::deleteInvalidHosts)}}));
    routes.append(bind(QStringLiteral("/api/nodes/dedupe"), {{post, wrap(&Handlers::dedupeNodes)}}));
    routes.append(
        bind(QStringLiteral("/api/nodes/{id}/speedtest"), {{post, wrap(&Handlers::speedtestOne)}}));
    routes.append(
        bind(QStringLiteral("/api/nodes/speedtest"), {{post, wrap(&Handlers::speedtestAll)}}));
    routes.append(bind(QStringLiteral("/api/kernels"), {{get, wrap(&Handlers::kernels)}}));
    routes.append(bind(QStringLiteral("/api/kernels/releases"),
                       {{get, wrap(&Handlers::kernelReleases)}}));
    routes.append(
        bind(QStringLiteral("/api/kernels/update"), {{post, wrap(&Handlers::updateKernel)}}));
    routes.append(
        bind(QStringLiteral("/api/proxy/status"), {{get, wrap(&Handlers::proxyStatus)}}));
    routes.append(bind(QStringLiteral("/api/proxy/config"),
                       {{get, wrap(&Handlers::proxyConfig)},
                        {put, wrap(&Handlers::updateProxyConfig)}}));
    routes.append(bind(QStringLiteral("/api/proxy/log"), {{get, wrap(&Handlers::proxyLog)}}));
    routes.append(bind(QStringLiteral("/api/proxy/start"), {{post, wrap(&Handlers::proxyStart)}}));
    routes.append(bind(QStringLiteral("/api/proxy/stop"), {{post, wrap(&Handlers::proxyStop)}}));
    routes.append(
        bind(QStringLiteral("/api/proxy/restart"), {{post, wrap(&Handlers::proxyRestart)}}));
    routes.append(bind(QStringLiteral("/api/export/json"), {{get, wrap(&Handlers::exportJson)}}));
    routes.append(bind(QStringLiteral("/api/export/clash"), {{get, wrap(&Handlers::exportClash)}}));
    routes.append(bind(QStringLiteral("/api/export/uri"), {{get, wrap(&Handlers::exportUri)}}));
    return routes;
}

} // namespace Api
