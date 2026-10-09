#include <gtest/gtest.h>

#include <QByteArray>

#include "ApiTestSupport.h"
#include "api/ApiMessages.h"
#include "api/ApiRouter.h"
#include "db/Database.h"
#include "models/Settings.h"
#include "tasks/TaskTracker.h"

using Api::Request;
using Api::Response;
using Api::Router;

namespace {

class HandlerFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        fixture = std::make_shared<ApiTest::Fixture>(QStringLiteral("handler"));
        ASSERT_TRUE(fixture->db != nullptr);
        router = std::make_shared<Router>(fixture->state, std::nullopt);
    }

    /// 不带令牌的 Router（鉴权本身在 RouterTests 里单独测）。
    Response call(const QString &method, const QString &target, const QByteArray &body = QByteArray(),
                  const QString &contentType = QStringLiteral("application/json"))
    {
        Request request = ApiTest::makeRequest(method, target, body,
                                               method == QLatin1String("GET") ? QString() : contentType);
        return router->dispatch(request);
    }

    QString createSource(const QString &name = QStringLiteral("src"),
                         const QString &url = QStringLiteral("https://example.com/sub"))
    {
        return ApiTest::createSource(fixture->db, name, url);
    }

    std::shared_ptr<ApiTest::Fixture> fixture;
    std::shared_ptr<Router> router;
};

// ---------------------------------------------------------------------------
// health / tasks / stats
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, Health)
{
    const Response response = call(QStringLiteral("GET"), QStringLiteral("/api/health"));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(response).at("status").get<std::string>(), "up");
}

TEST_F(HandlerFixture, TasksDefaultLimitAndTypedQuery)
{
    auto tracker = std::make_shared<Tasks::TaskTracker>();
    fixture->state.tasks = tracker;
    for (int index = 0; index < 3; ++index) {
        tracker->start(Tasks::Kind::Fetch, {{QStringLiteral("s%1").arg(index), QStringLiteral("n")}});
    }
    Router routerWithTasks(fixture->state, std::nullopt);

    const Response defaulted =
        routerWithTasks.dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/tasks")));
    EXPECT_EQ(defaulted.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(defaulted).size(), 3);

    const Response limited = routerWithTasks.dispatch(
        ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/tasks?limit=2")));
    EXPECT_EQ(ApiTest::envelopeData(limited).size(), 2);

    // ?limit=abc 是 Query 提取失败：400 + text/plain（axum 的拒绝响应）。
    const Response bad = routerWithTasks.dispatch(
        ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/tasks?limit=abc")));
    EXPECT_EQ(bad.status, 400);
    EXPECT_EQ(bad.header("content-type").toStdString(), "text/plain; charset=utf-8");
    EXPECT_TRUE(ApiTest::bodyText(bad).contains("Failed to deserialize query string"));
    EXPECT_TRUE(ApiTest::bodyText(bad).contains("invalid digit found in string"));

    // usize 不收负号。
    EXPECT_EQ(routerWithTasks.dispatch(ApiTest::makeRequest(QStringLiteral("GET"),
                                                           QStringLiteral("/api/tasks?limit=-1")))
                  .status,
              400);
}

TEST_F(HandlerFixture, StatsReflectsSeededRows)
{
    createSource();
    const Response response = call(QStringLiteral("GET"), QStringLiteral("/api/stats"));
    ASSERT_EQ(response.status, 200);
    const Json data = ApiTest::envelopeData(response);
    EXPECT_EQ(data.at("source_count").get<int>(), 1);
    EXPECT_EQ(data.at("enabled_source_count").get<int>(), 1);
    EXPECT_TRUE(data.contains("protocol_stats"));
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, HttpServerFlagRoundTripsThroughTheApi)
{
    // 开关这一栏只有"改得动、读得回"才有意义：默认 false，PUT true 之后必须回 true。
    EXPECT_FALSE(ApiTest::envelopeData(call(QStringLiteral("GET"), QStringLiteral("/api/settings")))
                     .at("http_server_enabled")
                     .get<bool>());
    const Response saved = call(QStringLiteral("PUT"), QStringLiteral("/api/settings"),
                                QByteArray("{\"http_server_enabled\": true}"));
    ASSERT_EQ(saved.status, 200);
    EXPECT_TRUE(ApiTest::envelopeData(saved).at("http_server_enabled").get<bool>());
    EXPECT_TRUE(ApiTest::envelopeData(call(QStringLiteral("GET"), QStringLiteral("/api/settings")))
                    .at("http_server_enabled")
                    .get<bool>());
}

TEST_F(HandlerFixture, GetSettingsReturnsPersistedShape)
{
    const Response response = call(QStringLiteral("GET"), QStringLiteral("/api/settings"));
    ASSERT_EQ(response.status, 200);
    const Json data = ApiTest::envelopeData(response);
    // Rust 的 AppSettings::default() 里 http_server_enabled 就是 false，
    // 所以新库读回来必须是 false —— 断言 true 的那版是测试写错，不是实现错。
    EXPECT_FALSE(data.at("http_server_enabled").get<bool>());
    EXPECT_EQ(data.at("kernel_source").get<std::string>(), "gh-proxy");
    EXPECT_TRUE(data.contains("speedtest_concurrency"));
    EXPECT_TRUE(data.contains("exclude_keywords"));
}

TEST_F(HandlerFixture, UpdateSettingsAppliesPartialPatch)
{
    const Response updated = call(QStringLiteral("PUT"), QStringLiteral("/api/settings"),
                                 QByteArray("{\"speedtest_timeout_ms\":2500,\"close_to_tray\":false}"));
    ASSERT_EQ(updated.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(updated).at("speedtest_timeout_ms").get<int>(), 2500);
    EXPECT_FALSE(ApiTest::envelopeData(updated).at("close_to_tray").get<bool>());
    // 没出现的键保持原值。
    EXPECT_EQ(ApiTest::envelopeData(updated).at("kernel_source").get<std::string>(), "gh-proxy");

    const Response reread = call(QStringLiteral("GET"), QStringLiteral("/api/settings"));
    EXPECT_EQ(ApiTest::envelopeData(reread).at("speedtest_timeout_ms").get<int>(), 2500);
}

TEST_F(HandlerFixture, UpdateSettingsRejectsWrongTypeWith422)
{
    const Response response = call(QStringLiteral("PUT"), QStringLiteral("/api/settings"),
                                  QByteArray("{\"close_to_tray\":\"nope\"}"));
    EXPECT_EQ(response.status, 422);
    EXPECT_TRUE(ApiTest::bodyText(response).contains("Failed to deserialize the JSON body"));
    EXPECT_TRUE(ApiTest::bodyText(response).contains("invalid type: string \"nope\""));
}

TEST_F(HandlerFixture, SettingsBodyMustBeJson)
{
    EXPECT_EQ(call(QStringLiteral("PUT"), QStringLiteral("/api/settings"), QByteArray("{}"),
                  QStringLiteral("text/plain"))
                  .status,
              415);
    // 三条都是"内容类型对、body 不成 JSON"：axum 0.8 一律 422（415 只给内容类型不对的）。
    EXPECT_EQ(call(QStringLiteral("PUT"), QStringLiteral("/api/settings"), QByteArray()).status, 422);
    EXPECT_EQ(call(QStringLiteral("PUT"), QStringLiteral("/api/settings"), QByteArray("[]")).status,
              422);
    EXPECT_EQ(call(QStringLiteral("PUT"), QStringLiteral("/api/settings"),
                  QByteArray("{\"close_to_tray\": tru e}"))
                  .status,
              422);
}

// ---------------------------------------------------------------------------
// sources
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, SourceCrud)
{
    const Response created = call(QStringLiteral("POST"), QStringLiteral("/api/sources"),
                                 QByteArray("{\"name\":\"demo\",\"url_template\":\"https://e.com/{Ymd}\"}"));
    ASSERT_EQ(created.status, 201);
    const QString id = QString::fromStdString(ApiTest::envelopeData(created).at("id").get<std::string>());
    EXPECT_FALSE(id.isEmpty());

    const Response listed = call(QStringLiteral("GET"), QStringLiteral("/api/sources"));
    ASSERT_EQ(listed.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(listed).size(), 1);

    const Response fetched =
        call(QStringLiteral("GET"), QStringLiteral("/api/sources/%1").arg(id));
    ASSERT_EQ(fetched.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(fetched).at("name").get<std::string>(), "demo");

    const Response updated = call(QStringLiteral("PUT"), QStringLiteral("/api/sources/%1").arg(id),
                                 QByteArray("{\"enabled\":false,\"remark\":\"note\"}"));
    ASSERT_EQ(updated.status, 200);
    EXPECT_FALSE(ApiTest::envelopeData(updated).at("enabled").get<bool>());
    EXPECT_EQ(ApiTest::envelopeData(updated).at("remark").get<std::string>(), "note");

    const Response deleted = call(QStringLiteral("DELETE"), QStringLiteral("/api/sources/%1").arg(id));
    ASSERT_EQ(deleted.status, 200);
    EXPECT_TRUE(ApiTest::envelopeData(deleted).at("deleted").get<bool>());

    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/sources/%1").arg(id)).status, 404);
    EXPECT_EQ(call(QStringLiteral("DELETE"), QStringLiteral("/api/sources/%1").arg(id)).status, 404);
}

TEST_F(HandlerFixture, CreateSourceValidation)
{
    const Response blank = call(QStringLiteral("POST"), QStringLiteral("/api/sources"),
                               QByteArray("{\"name\":\"  \",\"url_template\":\"https://e.com\"}"));
    ASSERT_EQ(blank.status, 400);
    EXPECT_EQ(ApiTest::bodyJson(blank).at("message").get<std::string>(), "name and url_template required");

    // 缺必填字段是 serde 的 missing field → 422，不是 handler 的 400。
    const Response missing = call(QStringLiteral("POST"), QStringLiteral("/api/sources"),
                                 QByteArray("{\"name\":\"demo\"}"));
    EXPECT_EQ(missing.status, 422);
    EXPECT_TRUE(ApiTest::bodyText(missing).contains("missing field `url_template`"));
}

TEST_F(HandlerFixture, FetchSourceAndFetchAll)
{
    const QString id = createSource();
    const Response response =
        call(QStringLiteral("POST"), QStringLiteral("/api/sources/%1/fetch").arg(id));
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(fixture->collector->fetchOneCalls, 1);
    EXPECT_EQ(fixture->collector->lastFetchId, id);
    EXPECT_TRUE(ApiTest::envelopeData(response).at("success").get<bool>());

    fixture->collector->fetchOneFails = true;
    const Response failed =
        call(QStringLiteral("POST"), QStringLiteral("/api/sources/%1/fetch").arg(id));
    EXPECT_EQ(failed.status, 500);
    EXPECT_EQ(ApiTest::bodyJson(failed).at("message").get<std::string>(), "source not found");

    const Response all = call(QStringLiteral("POST"), QStringLiteral("/api/sources/fetch-all"));
    EXPECT_EQ(all.status, 200);
    EXPECT_EQ(fixture->collector->fetchAllCalls, 1);
    EXPECT_EQ(ApiTest::envelopeData(all).size(), 1);
}

TEST_F(HandlerFixture, DiscoverGistsDefaultsAndOverrides)
{
    const Response defaulted = call(QStringLiteral("POST"), QStringLiteral("/api/gist/discover"),
                                   QByteArray("{}"));
    ASSERT_EQ(defaulted.status, 200);
    EXPECT_EQ(fixture->collector->discoverCalls, 1);
    EXPECT_EQ(fixture->collector->lastQuery.toStdString(), "vless//");
    EXPECT_EQ(fixture->collector->lastMaxPages, 100);
    const Json data = ApiTest::envelopeData(defaulted);
    EXPECT_TRUE(data.at("started").get<bool>());
    EXPECT_EQ(data.at("max_pages").get<int>(), 100);
    EXPECT_EQ(data.at("message").get<std::string>(), Api::Messages::gistStartedMessage().toStdString());

    const Response custom = call(QStringLiteral("POST"), QStringLiteral("/api/gist/discover"),
                                QByteArray("{\"query\":\"ss//\",\"max_pages\":7}"));
    ASSERT_EQ(custom.status, 200);
    EXPECT_EQ(fixture->collector->lastQuery.toStdString(), "ss//");
    EXPECT_EQ(fixture->collector->lastMaxPages, 7);

    // 给了空串就用空串（Rust 的 Option<String>：Some("") 不回填默认值）。
    call(QStringLiteral("POST"), QStringLiteral("/api/gist/discover"), QByteArray("{\"query\":\"\"}"));
    EXPECT_TRUE(fixture->collector->lastQuery.isEmpty());

    EXPECT_EQ(call(QStringLiteral("POST"), QStringLiteral("/api/gist/discover"),
                  QByteArray("{\"max_pages\":\"x\"}"))
                  .status,
              422);
}

// ---------------------------------------------------------------------------
// nodes
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, NodeListGetAndDelete)
{
    const QString sourceId = createSource();
    const QVector<Models::Node> nodes = {
        ApiTest::makeNode(QStringLiteral("n-1"), sourceId, QStringLiteral("ss"),
                          QStringLiteral("10.0.0.1"), 8388),
        ApiTest::makeNode(QStringLiteral("n-2"), sourceId, QStringLiteral("vmess"),
                          QStringLiteral("10.0.0.2"), 443),
    };
    ASSERT_TRUE(fixture->db->replaceSourceNodes(sourceId, nodes).isOk());

    const Response listed = call(QStringLiteral("GET"),
                                QStringLiteral("/api/nodes?source_id=%1").arg(sourceId));
    ASSERT_EQ(listed.status, 200);
    const Json page = ApiTest::envelopeData(listed);
    EXPECT_EQ(page.at("total").get<int>(), 2);
    EXPECT_EQ(page.at("page").get<int>(), 1);
    EXPECT_EQ(page.at("page_size").get<int>(), 50);
    EXPECT_EQ(page.at("items").size(), 2);
    EXPECT_TRUE(page.at("items").at(0).contains("latency_ms"));

    const Response filtered = call(QStringLiteral("GET"),
                                  QStringLiteral("/api/nodes?protocol=ss&source_id=%1").arg(sourceId));
    ASSERT_EQ(filtered.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(filtered).at("total").get<int>(), 1);

    const Response single =
        call(QStringLiteral("GET"), QStringLiteral("/api/nodes/%1").arg(sourceId));
    EXPECT_EQ(single.status, 404);

    const Response deleted =
        call(QStringLiteral("DELETE"), QStringLiteral("/api/nodes/%1").arg(sourceId));
    EXPECT_EQ(deleted.status, 404);
}

TEST_F(HandlerFixture, NodeListQueryErrors)
{
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/nodes?page=abc")).status, 400);
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/nodes?page_size=0")).status, 200);
}

TEST_F(HandlerFixture, NodeBulkOperations)
{
    const QString sourceId = createSource();
    QVector<Models::Node> nodes;
    for (int index = 0; index < 3; ++index) {
        nodes.append(ApiTest::makeNode(QStringLiteral("bulk-%1").arg(index), sourceId,
                                      QStringLiteral("ss"), QStringLiteral("203.0.113.%1").arg(index),
                                      8388));
    }
    ASSERT_TRUE(fixture->db->replaceSourceNodes(sourceId, nodes).isOk());

    const Json seeded = ApiTest::envelopeData(
        call(QStringLiteral("GET"), QStringLiteral("/api/nodes?source_id=%1").arg(sourceId)));
    EXPECT_EQ(seeded.at("total").get<int>(), 3);

    const Response dedupe = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/dedupe"));
    ASSERT_EQ(dedupe.status, 200);
    EXPECT_TRUE(ApiTest::envelopeData(dedupe).contains("deleted"));

    const Response dead = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/delete-dead"));
    EXPECT_EQ(dead.status, 200);

    const Response invalid = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/delete-invalid"));
    EXPECT_EQ(invalid.status, 200);

    const Response cleared = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/clear"));
    ASSERT_EQ(cleared.status, 200);
    EXPECT_GE(ApiTest::envelopeData(cleared).at("deleted").get<int>(), 0);

    const Response after = call(QStringLiteral("GET"), QStringLiteral("/api/nodes"));
    EXPECT_EQ(ApiTest::envelopeData(after).at("total").get<int>(), 0);
}

// ---------------------------------------------------------------------------
// speedtest
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, SpeedtestOneUsesSettingsWithConcurrencyOne)
{
    call(QStringLiteral("PUT"), QStringLiteral("/api/settings"),
         QByteArray("{\"speedtest_timeout_ms\":2222}"));
    const Response response =
        call(QStringLiteral("POST"), QStringLiteral("/api/nodes/node-7/speedtest"));
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(fixture->speedtest->oneCalls, 1);
    EXPECT_EQ(fixture->speedtest->lastConcurrency, 1u);
    EXPECT_EQ(fixture->speedtest->lastTimeoutMs, 2222u);
    EXPECT_EQ(ApiTest::envelopeData(response).at("id").get<std::string>(), "node-7");
    EXPECT_EQ(ApiTest::envelopeData(response).at("latency_ms").get<int>(), 42);

    fixture->speedtest->fails = true;
    EXPECT_EQ(call(QStringLiteral("POST"), QStringLiteral("/api/nodes/node-7/speedtest")).status, 500);
}

TEST_F(HandlerFixture, SpeedtestAllTreatsBadBodyAsNoIds)
{
    // Option<Json<SpeedtestBody>>：body 非法也照样测全部。
    const Response noBody = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/speedtest"));
    EXPECT_EQ(noBody.status, 200);
    EXPECT_FALSE(fixture->speedtest->lastIds.has_value());

    const Response badJson = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/speedtest"),
                                 QByteArray("{oops"));
    EXPECT_EQ(badJson.status, 200);
    EXPECT_FALSE(fixture->speedtest->lastIds.has_value());

    const Response withIds = call(QStringLiteral("POST"), QStringLiteral("/api/nodes/speedtest"),
                                 QByteArray("{\"ids\":[\"a\",\"b\"]}"));
    ASSERT_EQ(withIds.status, 200);
    ASSERT_TRUE(fixture->speedtest->lastIds.has_value());
    EXPECT_EQ(fixture->speedtest->lastIds->size(), 2);
    EXPECT_EQ(fixture->speedtest->lastBatchConcurrency, 128u);
    EXPECT_EQ(ApiTest::envelopeData(withIds).at("total").get<int>(), 2);
}

// ---------------------------------------------------------------------------
// kernels
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, KernelsAndReleases)
{
    const Response kernels = call(QStringLiteral("GET"), QStringLiteral("/api/kernels"));
    ASSERT_EQ(kernels.status, 200);
    const Json data = ApiTest::envelopeData(kernels);
    ASSERT_EQ(data.size(), 2);
    EXPECT_EQ(data.at(0).at("kind").get<std::string>(), "mihomo");
    EXPECT_TRUE(data.at(0).at("available").get<bool>());
    EXPECT_FALSE(data.at(1).at("available").get<bool>());

    // 代理没起 → 不带 proxy_url。
    const Response releases = call(QStringLiteral("GET"), QStringLiteral("/api/kernels/releases"));
    ASSERT_EQ(releases.status, 200);
    EXPECT_FALSE(fixture->kernel->lastProxyUrl.has_value());
    EXPECT_EQ(ApiTest::envelopeData(releases).size(), 1);
    EXPECT_TRUE(ApiTest::envelopeData(releases).at(0).at("update_available").get<bool>());

    fixture->proxy->currentStatus.state = Proxy::ProxyState::Running;
    fixture->proxy->currentStatus.localProxyReady = true;
    fixture->proxy->currentStatus.mixedPort = 9980;
    call(QStringLiteral("GET"), QStringLiteral("/api/kernels/releases"));
    ASSERT_TRUE(fixture->kernel->lastProxyUrl.has_value());
    EXPECT_EQ(fixture->kernel->lastProxyUrl->toStdString(), "http://127.0.0.1:9980");

    fixture->kernel->releasesFail = true;
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/kernels/releases")).status, 500);
}

TEST_F(HandlerFixture, UpdateKernelRequiresStoppedProxyAndValidKind)
{
    const Response ok = call(QStringLiteral("POST"), QStringLiteral("/api/kernels/update"),
                            QByteArray("{\"kind\":\"mihomo\"}"));
    ASSERT_EQ(ok.status, 200);
    EXPECT_EQ(fixture->kernel->updateCalls, 1);
    EXPECT_EQ(ApiTest::envelopeData(ok).at("version").get<std::string>(), "v1.19.0");

    const Response badKind = call(QStringLiteral("POST"), QStringLiteral("/api/kernels/update"),
                                 QByteArray("{\"kind\":\"zap\"}"));
    EXPECT_EQ(badKind.status, 422);
    EXPECT_TRUE(ApiTest::bodyText(badKind).contains("unknown variant `zap`"));

    EXPECT_EQ(call(QStringLiteral("POST"), QStringLiteral("/api/kernels/update"),
                  QByteArray("{}"))
                  .status,
              422);

    fixture->proxy->currentStatus.state = Proxy::ProxyState::Running;
    fixture->proxy->currentStatus.kernel = Kernel::Kind::Mihomo;
    fixture->proxy->currentStatus.localProxyReady = true;
    const Response conflict = call(QStringLiteral("POST"), QStringLiteral("/api/kernels/update"),
                                  QByteArray("{\"kind\":\"mihomo\"}"));
    EXPECT_EQ(conflict.status, 409);
    const QString expected =
        Api::Messages::kernelUpdateBlocked().replace(QStringLiteral("{}"),
                                                    Kernel::kindDisplayName(Kernel::Kind::Mihomo));
    EXPECT_EQ(ApiTest::bodyJson(conflict).at("message").get<std::string>(), expected.toStdString());
}

// ---------------------------------------------------------------------------
// proxy
// ---------------------------------------------------------------------------
TEST_F(HandlerFixture, ProxyStatusAndConfig)
{
    const Response status = call(QStringLiteral("GET"), QStringLiteral("/api/proxy/status"));
    ASSERT_EQ(status.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(status).at("state").get<std::string>(), "stopped");

    const Response config = call(QStringLiteral("GET"), QStringLiteral("/api/proxy/config"));
    ASSERT_EQ(config.status, 200);
    EXPECT_EQ(ApiTest::envelopeData(config).at("mixed_port").get<int>(), 9980);
}

TEST_F(HandlerFixture, ProxyConfigUpdateRoundTripAndFailure)
{
    const Response updated = call(QStringLiteral("PUT"), QStringLiteral("/api/proxy/config"),
                                 QByteArray("{\"mode\":\"tun\",\"mixed_port\":9999,"
                                            "\"controller_port\":9090,\"allow_lan\":false}"));
    ASSERT_EQ(updated.status, 200) << ApiTest::bodyText(updated).toStdString();
    EXPECT_EQ(fixture->proxy->updateConfigCalls, 1);
    EXPECT_EQ(fixture->proxy->lastAppliedConfig.mixedPort, 9999);
    // data 是请求体本身（Rust 的 Json(ApiResponse::ok(body))）。
    EXPECT_EQ(ApiTest::envelopeData(updated).at("mixed_port").get<int>(), 9999);

    fixture->proxy->updateConfigFails = true;
    const Response rejected = call(QStringLiteral("PUT"), QStringLiteral("/api/proxy/config"),
                                  QByteArray("{\"mixed_port\":9998}"));
    EXPECT_EQ(rejected.status, 400);
    EXPECT_FALSE(rejected.header("content-type").isEmpty());
}

TEST_F(HandlerFixture, ProxyLogClampsMaxChars)
{
    EXPECT_EQ(fixture->proxy->lastMaxChars, 0);
    const Response defaulted = call(QStringLiteral("GET"), QStringLiteral("/api/proxy/log"));
    ASSERT_EQ(defaulted.status, 200);
    EXPECT_EQ(fixture->proxy->lastMaxChars, 8000);
    EXPECT_EQ(ApiTest::envelopeData(defaulted).at("log").get<std::string>(), "log tail");

    call(QStringLiteral("GET"), QStringLiteral("/api/proxy/log?max_chars=1"));
    EXPECT_EQ(fixture->proxy->lastMaxChars, 100);
    call(QStringLiteral("GET"), QStringLiteral("/api/proxy/log?max_chars=99999999"));
    EXPECT_EQ(fixture->proxy->lastMaxChars, 100000);

    fixture->proxy->logFails = true;
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/proxy/log")).status, 500);
}

TEST_F(HandlerFixture, ProxyStartValidatesNodeAndKernel)
{
    // 测试助手默认带 application/json，所以空 body 走的是语法拒绝（422），不是 415。
    const Response noBody = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"));
    EXPECT_EQ(noBody.status, 422);

    const Response missingField = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"),
                                      QByteArray("{}"));
    EXPECT_EQ(missingField.status, 422);
    EXPECT_TRUE(ApiTest::bodyText(missingField).contains("missing field `node_id`"));

    const Response badKernel = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"),
                                   QByteArray("{\"node_id\":\"x\",\"kernel\":\"zap\"}"));
    EXPECT_EQ(badKernel.status, 422);

    const Response notFound = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"),
                                  QByteArray("{\"node_id\":\"ghost\"}"));
    ASSERT_EQ(notFound.status, 404);
    EXPECT_EQ(ApiTest::bodyJson(notFound).at("message").get<std::string>(), "node not found");

    const QString sourceId = createSource();
    const Models::Node node =
        ApiTest::makeNode(QStringLiteral("real-1"), sourceId, QStringLiteral("ss"),
                          QStringLiteral("203.0.113.9"), 8388);
    ASSERT_TRUE(fixture->db->replaceSourceNodes(sourceId, {node}).isOk());

    const Response started = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"),
                                 QByteArray("{\"node_id\":\"real-1\",\"kernel\":\"xray\"}"));
    ASSERT_EQ(started.status, 200) << ApiTest::bodyText(started).toStdString();
    EXPECT_EQ(fixture->proxy->connectCalls, 1);
    EXPECT_EQ(fixture->proxy->lastConnectedNodeId.toStdString(), "real-1");
    ASSERT_TRUE(fixture->proxy->lastRequestedKernel.has_value());
    EXPECT_EQ(Kernel::preferenceToText(*fixture->proxy->lastRequestedKernel).toStdString(), "xray");
    EXPECT_EQ(ApiTest::envelopeData(started).at("state").get<std::string>(), "running");

    // restart 与 start 是同一套逻辑（Rust 里两者逐字相同）。
    const Response restarted = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/restart"),
                                   QByteArray("{\"node_id\":\"real-1\"}"));
    EXPECT_EQ(restarted.status, 200);
    EXPECT_EQ(fixture->proxy->connectCalls, 2);
}

TEST_F(HandlerFixture, ProxyStartMarksNodeOnConnectivityFailure)
{
    const QString sourceId = createSource();
    const Models::Node node =
        ApiTest::makeNode(QStringLiteral("dead-1"), sourceId, QStringLiteral("ss"),
                          QStringLiteral("203.0.113.10"), 8388);
    ASSERT_TRUE(fixture->db->replaceSourceNodes(sourceId, {node}).isOk());

    // 其它失败：原样 500，不碰节点。
    fixture->proxy->connectFails = true;
    fixture->proxy->connectError = QStringLiteral("kernel executable missing");
    const Response plain = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"),
                               QByteArray("{\"node_id\":\"dead-1\"}"));
    ASSERT_EQ(plain.status, 500);
    EXPECT_EQ(ApiTest::bodyJson(plain).at("message").get<std::string>(), "kernel executable missing");

    // 节点连不通：顺手把节点标成不可用，并把中文后缀拼到消息里。
    fixture->proxy->connectError =
        QStringLiteral("%1: %2").arg(QString::fromLatin1("xxx"),
                                     ApiTest::connectivityFailureMarker());
    fixture->proxy->currentStatus.kernel = Kernel::Kind::Xray;
    const Response marked = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/start"),
                                QByteArray("{\"node_id\":\"dead-1\"}"));
    ASSERT_EQ(marked.status, 500);
    const QString message = QString::fromStdString(ApiTest::bodyJson(marked).at("message").get<std::string>());
    EXPECT_TRUE(message.endsWith(Api::Messages::markedUnavailableSuffix()));

    const Result<Models::Node> stored = fixture->db->getNode(QStringLiteral("dead-1"));
    ASSERT_TRUE(stored.isOk());
    ASSERT_TRUE(stored.value().latencyOk.has_value());
    EXPECT_FALSE(*stored.value().latencyOk);
    EXPECT_EQ(stored.value().latencyTestKind.toStdString(), "google_proxy");
    EXPECT_EQ(stored.value().latencyKernel.toStdString(), "xray");
}

TEST_F(HandlerFixture, ProxyStop)
{
    const Response stopped = call(QStringLiteral("POST"), QStringLiteral("/api/proxy/stop"));
    ASSERT_EQ(stopped.status, 200);
    EXPECT_EQ(fixture->proxy->stopCalls, 1);
    EXPECT_EQ(ApiTest::envelopeData(stopped).at("state").get<std::string>(), "stopped");

    fixture->proxy->stopFails = true;
    EXPECT_EQ(call(QStringLiteral("POST"), QStringLiteral("/api/proxy/stop")).status, 500);
}

// ---------------------------------------------------------------------------
// export
// ---------------------------------------------------------------------------
class ExportFixture : public HandlerFixture {
protected:
    void SetUp() override
    {
        HandlerFixture::SetUp();
        sourceId = createSource();
        QVector<Models::Node> nodes;
        nodes.append(ApiTest::makeNode(QStringLiteral("ex-1"), sourceId, QStringLiteral("ss"),
                                      QStringLiteral("203.0.113.20"), 8388));
        nodes.append(ApiTest::makeNode(QStringLiteral("ex-2"), sourceId, QStringLiteral("vmess"),
                                      QStringLiteral("203.0.113.21"), 443));
        ASSERT_TRUE(fixture->db->replaceSourceNodes(sourceId, nodes).isOk());
    }

    QString sourceId;
};

TEST_F(ExportFixture, ExportJsonShapeAndQualityDefaults)
{
    const Response response =
        call(QStringLiteral("GET"), QStringLiteral("/api/export/json?quality_only=false"));
    ASSERT_EQ(response.status, 200);
    const Json data = ApiTest::envelopeData(response);
    EXPECT_EQ(data.at("count").get<int>(), 2);
    EXPECT_FALSE(data.at("quality_only").get<bool>());
    EXPECT_EQ(data.at("max_latency_ms").get<int>(), 500);
    EXPECT_TRUE(data.contains("min_download_kbps"));
    EXPECT_TRUE(data.at("limit").is_null());
    EXPECT_EQ(data.at("note").get<std::string>(),
              "Export uses stored test results only and does not run network tests. limit is the "
              "maximum number of nodes returned.");
    EXPECT_EQ(data.at("nodes").size(), 2);
    EXPECT_EQ(data.at("nodes").at(0).at("port").get<int>(), 8388);

    // limit 生效（quality_only=false 时直接截断）。
    const Response limited =
        call(QStringLiteral("GET"), QStringLiteral("/api/export/json?quality_only=false&limit=1"));
    EXPECT_EQ(ApiTest::envelopeData(limited).at("count").get<int>(), 1);
    EXPECT_EQ(ApiTest::envelopeData(limited).at("limit").get<int>(), 1);

    // 类型不对 → 400（Query 提取失败）。
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/export/json?quality_only=yes")).status,
              400);
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/export/json?limit=abc")).status, 400);
    EXPECT_EQ(call(QStringLiteral("GET"), QStringLiteral("/api/export/json?min_download_kbps=x"))
                  .status,
              400);
}

TEST_F(ExportFixture, ExportClashIsRawYaml)
{
    const Response response =
        call(QStringLiteral("GET"), QStringLiteral("/api/export/clash?quality_only=false"));
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.header("content-type").toStdString(), "text/yaml; charset=utf-8");
    const QString yaml = ApiTest::bodyText(response);
    EXPECT_TRUE(yaml.contains("proxies:"));
    EXPECT_TRUE(yaml.contains("proxy-groups:"));
    EXPECT_TRUE(yaml.contains("MATCH,PROXY"));
}

TEST_F(ExportFixture, ExportUriBase64ByDefault)
{
    const Response encoded =
        call(QStringLiteral("GET"), QStringLiteral("/api/export/uri?quality_only=false"));
    ASSERT_EQ(encoded.status, 200);
    EXPECT_EQ(encoded.header("content-type").toStdString(), "text/plain; charset=utf-8");
    const QByteArray decoded = QByteArray::fromBase64(encoded.body);
    EXPECT_FALSE(decoded.isEmpty());

    const Response plain =
        call(QStringLiteral("GET"),
             QStringLiteral("/api/export/uri?quality_only=false&base64=false"));
    ASSERT_EQ(plain.status, 200);
    EXPECT_TRUE(plain.body.contains("://"));
    EXPECT_NE(plain.body, encoded.body);
}

// ---------------------------------------------------------------------------
// 依赖没接线时的行为（Rust 不可能为空，这里必须明确报错而不是静默成功）
// ---------------------------------------------------------------------------
TEST(UnwiredState, HandlersReturnServerError)
{
    Api::AppState empty;
    Router router(empty, std::nullopt);
    EXPECT_EQ(router.dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/stats")))
                      .status,
              500);
    EXPECT_EQ(
        router.dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/kernels")))
            .status,
        500);
    EXPECT_EQ(
        router.dispatch(ApiTest::makeRequest(QStringLiteral("GET"), QStringLiteral("/api/health")))
            .status,
        200);
}

} // namespace
