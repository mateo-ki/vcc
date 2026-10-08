#include <gtest/gtest.h>

#include "api/HttpMessage.h"

using Api::Request;
using Api::Response;

namespace {

Result<std::optional<Request>> parse(const QByteArray &raw, int *consumed)
{
    return Api::Http::parseRequest(raw, consumed);
}

TEST(HttpMessage, ParsesRequestLineHeadersAndBody)
{
    int consumed = 0;
    const QByteArray raw = "POST /api/proxy/start HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n"
                           "Content-Type: application/json\r\n"
                           "Authorization: Bearer abc123\r\n"
                           "Content-Length: 5\r\n"
                           "\r\n"
                           "{\"a\":1}xx";
    const Result<std::optional<Request>> parsed = parse(raw, &consumed);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    ASSERT_TRUE(parsed.value().has_value());
    const Request &request = *parsed.value();
    EXPECT_EQ(request.method.toStdString(), "POST");
    EXPECT_EQ(request.version.toStdString(), "HTTP/1.1");
    EXPECT_EQ(request.path.toStdString(), "/api/proxy/start");
    EXPECT_TRUE(request.rawQuery.isEmpty());
    EXPECT_EQ(request.body.constData(), QByteArray("{\"a\":1}").constData());
    // 只吃掉这一个请求，剩下的留给下一个（keep-alive）。
    EXPECT_EQ(consumed, raw.size() - 2);
    // header 名一律小写存下来，axum 的 headers.get(AUTHORIZATION) 才拿得到。
    EXPECT_EQ(request.header("authorization").toStdString(), "Bearer abc123");
    EXPECT_EQ(request.header("content-type").toStdString(), "application/json");
    EXPECT_TRUE(request.header("host").toLower().toStdString() == "127.0.0.1");
}

TEST(HttpMessage, IncompleteRequestYieldsNoValue)
{
    int consumed = -1;
    const Result<std::optional<Request>> headers =
        parse("GET /api/health HTTP/1.1\r\nHost: x\r\n", &consumed);
    ASSERT_TRUE(headers.isOk());
    EXPECT_FALSE(headers.value().has_value());
    EXPECT_EQ(consumed, 0);

    consumed = -1;
    const Result<std::optional<Request>> body =
        parse("PUT /api/settings HTTP/1.1\r\nContent-Length: 10\r\n\r\n{}", &consumed);
    ASSERT_TRUE(body.isOk());
    EXPECT_FALSE(body.value().has_value());
    EXPECT_EQ(consumed, 0);
}

TEST(HttpMessage, RejectsMalformedRequests)
{
    int consumed = 0;
    EXPECT_TRUE(parse("GET /api/health\r\n\r\n", &consumed).isErr());
    EXPECT_TRUE(parse("GET /api/health FTP/1.0\r\n\r\n", &consumed).isErr());
    EXPECT_TRUE(parse("GET /api/health HTTP/1.1\r\nbroken-line\r\n\r\n", &consumed).isErr());
    EXPECT_TRUE(
        parse("GET /api/health HTTP/1.1\r\nContent-Length: nope\r\n\r\n", &consumed).isErr());
}

TEST(HttpMessage, RejectsChunkedRequestBody)
{
    // 任务约定：不支持 chunked 请求体，明确报错而不是悄悄吞掉。
    int consumed = 0;
    const Result<std::optional<Request>> parsed =
        parse("POST /api/settings HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
              &consumed);
    ASSERT_TRUE(parsed.isErr());
    EXPECT_TRUE(parsed.error().contains("chunked"));
}

TEST(HttpMessage, RejectsBodyOverAxumDefaultLimit)
{
    int consumed = 0;
    const QByteArray raw =
        QByteArray("POST /api/settings HTTP/1.1\r\nContent-Type: application/json\r\n"
                   "Content-Length: 3000000\r\n\r\n");
    const Result<std::optional<Request>> parsed = parse(raw, &consumed);
    ASSERT_TRUE(parsed.isErr());
    EXPECT_TRUE(parsed.error().contains("payload too large"));
}

TEST(HttpMessage, FirstDuplicateHeaderWinsLikeHyper)
{
    int consumed = 0;
    const Result<std::optional<Request>> parsed = parse(
        "GET /api/health HTTP/1.1\r\nAuthorization: Bearer first\r\n"
        "Authorization: Bearer second\r\n\r\n",
        &consumed);
    ASSERT_TRUE(parsed.isOk() && parsed.value().has_value());
    EXPECT_EQ(parsed.value()->header("authorization").toStdString(), "Bearer first");
}

TEST(HttpMessage, QueryPairsAreFormDecoded)
{
    int consumed = 0;
    const Result<std::optional<Request>> parsed = parse(
        "GET /api/nodes?keyword=hello+world&country=JP%20Tokyo&page=2&page=3 HTTP/1.1\r\n\r\n",
        &consumed);
    ASSERT_TRUE(parsed.isOk() && parsed.value().has_value());
    const Request &request = *parsed.value();
    EXPECT_EQ(request.query("keyword").value_or(QString()).toStdString(), "hello world");
    EXPECT_EQ(request.query("country").value_or(QString()).toStdString(), "JP Tokyo");
    // 重复键后出现的赢，和 serde 派生 Deserialize 一致。
    EXPECT_EQ(request.query("page").value_or(QString()).toStdString(), "3");
    EXPECT_FALSE(request.query("missing").has_value());
    // 空值也算"给了"：?protocol= 是 Some("")。
    EXPECT_TRUE(request.query("protocol").has_value());
}

TEST(HttpMessage, RawQueryValueKeepsPercentAndTakesFirst)
{
    // 对应 authenticate_request 里的 token 取法：不解码、只取第一个 token=。
    int consumed = 0;
    const Result<std::optional<Request>> parsed = parse(
        "GET /api/export/uri?token=abc%20def&token=second HTTP/1.1\r\n\r\n", &consumed);
    ASSERT_TRUE(parsed.isOk() && parsed.value().has_value());
    const Request &request = *parsed.value();
    EXPECT_EQ(request.rawQueryValue("token").value_or(QString()).toStdString(), "abc%20def");
    EXPECT_EQ(request.query("token").value_or(QString()).toStdString(), "abc def");
}

TEST(HttpMessage, PathPercentDecodingKeepsPlusLiteral)
{
    int consumed = 0;
    const Result<std::optional<Request>> parsed =
        parse("GET /api/nodes/a%2Fb+c HTTP/1.1\r\n\r\n", &consumed);
    ASSERT_TRUE(parsed.isOk() && parsed.value().has_value());
    EXPECT_EQ(parsed.value()->rawPath.toStdString(), "/api/nodes/a%2Fb+c");
    EXPECT_EQ(parsed.value()->path.toStdString(), "/api/nodes/a/b+c");
}

TEST(HttpMessage, ResponseWireFormat)
{
    Response response = Api::Http::ok(Json{{"status", "up"}});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.header("content-type").toStdString(), "application/json");
    const QByteArray wire = response.toBytes();
    EXPECT_TRUE(wire.startsWith("HTTP/1.1 200 OK\r\n"));
    EXPECT_TRUE(wire.contains("content-type: application/json\r\n"));
    EXPECT_TRUE(wire.contains("\r\n\r\n{\"data\":{\"status\":\"up\"},\"message\":\"ok\",\"ok\":true}"));
}

TEST(HttpMessage, EmptyResponseSerializesWithZeroLength)
{
    const Response response = Response::empty(404);
    EXPECT_TRUE(response.body.isEmpty());
    EXPECT_EQ(response.header("content-length").toStdString(), "0");
    EXPECT_TRUE(response.toBytes().startsWith("HTTP/1.1 404 Not Found\r\n"));
}

TEST(HttpMessage, EnvelopeMatchesSerdeApiResponse)
{
    // {"ok":true,"message":"ok","data":...} / {"ok":false,"message":"..."}（data 键整个省略）
    const Json ok = Api::Http::okEnvelope(Json{{"n", 1}});
    EXPECT_TRUE(ok.at("ok").get<bool>());
    EXPECT_EQ(ok.at("message").get<std::string>(), "ok");
    EXPECT_EQ(ok.at("data").at("n").get<int>(), 1);

    const Json err = Api::Http::errorEnvelope(QStringLiteral("boom"));
    EXPECT_FALSE(err.at("ok").get<bool>());
    EXPECT_EQ(err.at("message").get<std::string>(), "boom");
    EXPECT_FALSE(err.contains("data"));
}

TEST(HttpMessage, JsonContentTypeCheckMatchesAxum)
{
    EXPECT_TRUE(Api::Http::isJsonContentType(QStringLiteral("application/json")));
    EXPECT_TRUE(Api::Http::isJsonContentType(QStringLiteral("application/json; charset=utf-8")));
    EXPECT_TRUE(Api::Http::isJsonContentType(QStringLiteral("APPLICATION/JSON")));
    // axum 只认 type/subtype，+json 后缀不认。
    EXPECT_FALSE(Api::Http::isJsonContentType(QStringLiteral("application/problem+json")));
    EXPECT_FALSE(Api::Http::isJsonContentType(QStringLiteral("text/plain")));
    EXPECT_FALSE(Api::Http::isJsonContentType(QString()));
}

} // namespace
