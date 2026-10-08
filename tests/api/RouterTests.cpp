#include <gtest/gtest.h>

#include "ApiTestSupport.h"
#include "api/ApiMessages.h"
#include "api/ApiRouter.h"

using Api::Request;
using Api::Response;
using Api::Router;

namespace {

/// 带假网关 + 临时库的 Router，专门用来打路由表和鉴权中间件（不绑 socket）。
class ProbeFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        fixture = std::make_shared<ApiTest::Fixture>(QStringLiteral("router"));
        ASSERT_TRUE(fixture->db != nullptr);
        router = std::make_shared<Router>(fixture->state, QStringLiteral("sekret"));
    }

    Response dispatch(const Request &request) const { return router->dispatch(request); }

    static Request authorized(const QString &method, const QString &target)
    {
        Request request = ApiTest::makeRequest(method, target);
        request.headers.insert(QStringLiteral("authorization"), QStringLiteral("Bearer sekret"));
        return request;
    }

    std::shared_ptr<ApiTest::Fixture> fixture;
    std::shared_ptr<Router> router;
};

TEST(RouteTable, MatchesRustExactly)
{
    const QVector<Router::RouteSpec> table = Router::routeTable();
    ASSERT_EQ(table.size(), 29);

    QHash<QString, QStringList> patterns;
    for (const Router::RouteSpec &route : table) {
        patterns.insert(route.pattern, route.methods);
    }
    // 逐条对照 src/api/mod.rs 的 `fn router`。
    EXPECT_EQ(patterns["/api/health"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/tasks"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/stats"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/settings"],
              (QStringList{QStringLiteral("GET"), QStringLiteral("PUT")}));
    EXPECT_EQ(patterns["/api/sources"],
              (QStringList{QStringLiteral("GET"), QStringLiteral("POST")}));
    EXPECT_EQ(patterns["/api/sources/{id}"],
              (QStringList{QStringLiteral("GET"), QStringLiteral("PUT"), QStringLiteral("DELETE")}));
    EXPECT_EQ(patterns["/api/sources/{id}/fetch"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/sources/fetch-all"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/gist/discover"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/nodes"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/nodes/{id}"],
              (QStringList{QStringLiteral("GET"), QStringLiteral("DELETE")}));
    EXPECT_EQ(patterns["/api/nodes/clear"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/nodes/delete-dead"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/nodes/delete-invalid"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/nodes/dedupe"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/nodes/{id}/speedtest"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/nodes/speedtest"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/kernels"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/kernels/releases"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/kernels/update"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/proxy/status"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/proxy/config"],
              (QStringList{QStringLiteral("GET"), QStringLiteral("PUT")}));
    EXPECT_EQ(patterns["/api/proxy/log"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/proxy/start"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/proxy/stop"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/proxy/restart"], QStringList{QStringLiteral("POST")});
    EXPECT_EQ(patterns["/api/export/json"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/export/clash"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(patterns["/api/export/uri"], QStringList{QStringLiteral("GET")});
    EXPECT_EQ(table.first().pattern.toStdString(), "/api/health");
    EXPECT_EQ(table.last().pattern.toStdString(), "/api/export/uri");
}

TEST_F(ProbeFixture, HealthIsOkWhenAuthorized)
{
    const Response response = dispatch(authorized(QStringLiteral("GET"), QStringLiteral("/api/health")));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.header("content-type").toStdString(), "application/json");
    EXPECT_EQ(ApiTest::bodyText(response).toStdString(),
              "{\"data\":{\"status\":\"up\"},\"message\":\"ok\",\"ok\":true}");
}

TEST_F(ProbeFixture, UnknownPathIs404WithEmptyBody)
{
    const Response response = dispatch(authorized(QStringLiteral("GET"), QStringLiteral("/api/nope")));
    EXPECT_EQ(response.status, 404);
    EXPECT_TRUE(response.body.isEmpty());
    EXPECT_EQ(response.header("content-length").toStdString(), "0");
}

TEST_F(ProbeFixture, TrailingSlashIs404LikeAxum)
{
    EXPECT_EQ(dispatch(authorized(QStringLiteral("GET"), QStringLiteral("/api/health/"))).status, 404);
}

TEST_F(ProbeFixture, WrongMethodIs405WithAllowHeader)
{
    const Response response =
        dispatch(authorized(QStringLiteral("DELETE"), QStringLiteral("/api/health")));
    EXPECT_EQ(response.status, 405);
    EXPECT_TRUE(response.body.isEmpty());
    EXPECT_EQ(response.header("allow").toStdString(), "GET,HEAD");

    const Response sources =
        dispatch(authorized(QStringLiteral("PATCH"), QStringLiteral("/api/sources/abc")));
    EXPECT_EQ(sources.status, 405);
    EXPECT_EQ(sources.header("allow").toStdString(), "GET,HEAD,PUT,DELETE");

    const Response posts = dispatch(authorized(QStringLiteral("GET"), QStringLiteral("/api/proxy/stop")));
    EXPECT_EQ(posts.status, 405);
    EXPECT_EQ(posts.header("allow").toStdString(), "POST");
}

TEST_F(ProbeFixture, HeadUsesGetHandlerAndDropsBody)
{
    const Response get = dispatch(authorized(QStringLiteral("GET"), QStringLiteral("/api/health")));
    const Response head = dispatch(authorized(QStringLiteral("HEAD"), QStringLiteral("/api/health")));
    EXPECT_EQ(head.status, 200);
    EXPECT_TRUE(head.body.isEmpty());
    // hyper 的 HEAD：头还在（含 content-length），body 不发。
    EXPECT_EQ(head.header("content-length").toStdString(), std::to_string(get.body.size()));
}

TEST_F(ProbeFixture, StaticSegmentBeatsParamSegment)
{
    // POST /api/sources/fetch-all 必须命中 fetch-all，而不是被 /api/sources/{id} 吃掉后回 405。
    const Response response =
        dispatch(authorized(QStringLiteral("POST"), QStringLiteral("/api/sources/fetch-all")));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(fixture->collector->fetchAllCalls, 1);

    // /api/nodes/{id}/speedtest 与 /api/nodes/speedtest 也不串台。
    const Response literal =
        dispatch(authorized(QStringLiteral("POST"), QStringLiteral("/api/nodes/speedtest")));
    EXPECT_EQ(literal.status, 415); // 走到 speedtest_all 的 body 提取：没给 Content-Type
    EXPECT_EQ(fixture->speedtest->oneCalls, 0);

    const Response templated = dispatch(
        authorized(QStringLiteral("POST"), QStringLiteral("/api/nodes/node-9/speedtest")));
    EXPECT_EQ(templated.status, 200);
    EXPECT_EQ(fixture->speedtest->oneCalls, 1);
}

TEST_F(ProbeFixture, PathParamsAreExtracted)
{
    const Response missing =
        dispatch(authorized(QStringLiteral("GET"), QStringLiteral("/api/sources/nope")));
    EXPECT_EQ(missing.status, 404);
    EXPECT_EQ(ApiTest::bodyJson(missing).at("message").get<std::string>(), "not found");

    const Response node =
        dispatch(authorized(QStringLiteral("DELETE"), QStringLiteral("/api/nodes/missing-id")));
    EXPECT_EQ(node.status, 404);
}

TEST_F(ProbeFixture, AuthRejectsMissingBearer)
{
    const Response response =
        dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/health")));
    EXPECT_EQ(response.status, 401);
    EXPECT_EQ(response.header("www-authenticate").toStdString(), "Bearer");
    EXPECT_EQ(response.header("content-type").toStdString(), "application/json");
    const Json envelope = ApiTest::bodyJson(response);
    EXPECT_FALSE(envelope.at("ok").get<bool>());
    EXPECT_FALSE(envelope.contains("data"));
    EXPECT_EQ(envelope.at("message").get<std::string>(), Api::Messages::authFailed().toStdString());
}

TEST_F(ProbeFixture, AuthAlsoGuardsUnknownPaths)
{
    // 中间件在路由之前：没令牌时连 404 都轮不到。
    const Response response =
        dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/nope")));
    EXPECT_EQ(response.status, 401);
}

TEST_F(ProbeFixture, QueryTokenOnlyAcceptedForGet)
{
    EXPECT_EQ(dispatch(ApiTest::makeRequest(QStringLiteral("GET"),
                                            QStringLiteral("/api/health?token=sekret")))
                  .status,
              200);
    EXPECT_EQ(dispatch(ApiTest::makeRequest(QStringLiteral("POST"),
                                            QStringLiteral("/api/gist/discover?token=sekret")))
                  .status,
              401);
    EXPECT_EQ(dispatch(ApiTest::makeRequest(QStringLiteral("GET"),
                                            QStringLiteral("/api/health?token=nope")))
                  .status,
              401);
    // 空 token 参数不参与比较（Rust 的 .filter(|v| !v.is_empty())）。
    EXPECT_EQ(dispatch(ApiTest::makeRequest(QStringLiteral("GET"),
                                            QStringLiteral("/api/health?token=")))
                  .status,
              401);
}

TEST_F(ProbeFixture, BearerPrefixIsRequired)
{
    Request request = ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/health"));
    request.headers.insert(QStringLiteral("authorization"), QStringLiteral("sekret"));
    EXPECT_EQ(dispatch(request).status, 401);

    request.headers.insert(QStringLiteral("authorization"), QStringLiteral("Basic sekret"));
    EXPECT_EQ(dispatch(request).status, 401);

    request.headers.insert(QStringLiteral("authorization"), QStringLiteral("Bearer sekret"));
    EXPECT_EQ(dispatch(request).status, 200);
}

TEST(AuthWithoutToken, MiddlewareIsSkippedEntirely)
{
    Api::AppState state;
    Router open(state, std::nullopt);
    EXPECT_EQ(open.dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/health")))
                      .status,
              200);
    EXPECT_FALSE(Router::authenticate(ApiTest::makeRequest(QStringLiteral("GET"),
                                                            QStringLiteral("/api/health")),
                                      std::nullopt)
                     .has_value());
}

TEST(PathHelpers, SplitPathDropsLeadingSlash)
{
    EXPECT_EQ(Router::splitPath(QStringLiteral("/api/sources/fetch-all")),
              (QStringList{QStringLiteral("api"), QStringLiteral("sources"),
                           QStringLiteral("fetch-all")}));
    EXPECT_EQ(Router::splitPath(QStringLiteral("/api/nodes/x/speedtest")).size(), 4);
}

TEST(Messages, RustCjkLiteralsAreReproducedExactly)
{
    // 逐字校验由生成脚本 + node tools/verify-cjk-literals.mjs 负责；这里锁住 ASCII 骨架和
    // 长度，防止有人手改生成文件时悄悄掉字。
    const QString authFailed = Api::Messages::authFailed();
    EXPECT_EQ(authFailed.size(), 24);
    EXPECT_TRUE(authFailed.endsWith(QStringLiteral(" Bearer Token")));

    const QString blocked = Api::Messages::kernelUpdateBlocked();
    EXPECT_EQ(blocked.size(), 12);
    EXPECT_TRUE(blocked.endsWith(QStringLiteral(" {}")));

    const QString marked = Api::Messages::markedUnavailableSuffix();
    EXPECT_EQ(marked.size(), 19);
    EXPECT_TRUE(marked.startsWith(QStringLiteral("; ")));

    const QString markFailure = Api::Messages::markFailurePrefix();
    EXPECT_EQ(markFailure.size(), 27);
    EXPECT_TRUE(markFailure.contains(QStringLiteral("{update_error}")));

    const QString gistStarted = Api::Messages::gistStartedMessage();
    EXPECT_EQ(gistStarted.size(), 45);
    EXPECT_TRUE(gistStarted.startsWith(QStringLiteral("Gist ")));
    EXPECT_TRUE(gistStarted.contains(QStringLiteral("/api/sources")));
    EXPECT_TRUE(gistStarted.contains(QStringLiteral("/api/nodes")));
}

} // namespace
