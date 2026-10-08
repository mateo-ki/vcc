#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

#include "core/JsonUtil.h"
#include "core/Result.h"

// src/api/mod.rs 里 axum 负责的那一段（请求行 + 头 + body + 响应信封）在这里手工实现：
// 这棵 vcpkg 树里没有 Qt HTTP server 模块，只能拿 QTcpServer 自己搭 HTTP/1.1。
//
// 语义对齐清单（都是 Rust 侧的行为，不是 RFC 想当然）：
// - JSON 响应的 Content-Type 是 `application/json`（axum `Json` 用 mime::APPLICATION_JSON，
//   不带 charset）。
// - 文本响应（401 之外的 extractor 拒绝、404/405 空 body）走 `text/plain; charset=utf-8`，
//   因为 axum 的 `(StatusCode, &str)` 就是这个类型。
// - 401 额外带 `www-authenticate: Bearer`；405 带 `allow`。
// - header 名一律小写输出（hyper 的 HTTP/1.1 序列化就是这样）。
namespace Api {

/// 一个已解析的 HTTP/1.1 请求。
struct Request {
    QString method;                          // 大写 ASCII
    QString version = QStringLiteral("HTTP/1.1"); // 请求行里的版本，keep-alive 判定要用
    QString target;                          // 原始 request target（含 query）
    QString path;                            // 百分号解码后的路径
    QString rawPath;                         // 未解码的路径
    QString rawQuery;                        // '?' 之后、未解码的查询串（可能为空）
    QVector<QPair<QString, QString>> queryPairs; // 已按 form_urlencoded 解码，保序
    QHash<QString, QString> headers;         // 键一律小写
    QByteArray body;

    /// 缺失返回空 QString（调用方自己区分"没给"和"给了空串"用 headerOrNull）。
    QString header(const QString &lowerName) const;
    std::optional<QString> headerOrNull(const QString &lowerName) const;
    /// 同名键重复时后出现的赢，和 serde 派生 Deserialize 的行为一致。
    std::optional<QString> query(const QString &key) const;
    /// 未解码、先出现的赢：对应 authenticate_request 里 `query.split('&')` + `find_map`。
    std::optional<QString> rawQueryValue(const QString &key) const;
    bool isGet() const { return method == QLatin1String("GET"); }
};

/// 一个待发送的 HTTP/1.1 响应。
struct Response {
    int status = 200;
    QVector<QPair<QString, QString>> headers;
    QByteArray body;

    void setHeader(const QString &name, const QString &value);
    bool hasHeader(const QString &lowerName) const;
    QString header(const QString &lowerName) const;
    /// 序列化成线上字节。includeBody=false 用于 HEAD（axum 的 get 路由也吃 HEAD，
    /// 但 hyper 会把 body 去掉，头里的 content-length 保持不变）。
    QByteArray toBytes(bool includeBody = true) const;

    static Response withJsonBody(int status, const Json &payload);
    static Response withTextBody(int status, const QString &contentType, const QString &text);
    static Response empty(int status);
};

namespace Http {

/// `{"ok":true,"message":"ok","data":...}`。serde 的 ApiResponse 是 struct，
/// 字段按声明顺序（ok/message/data）写出；nlohmann 的 map 按字节序排，
/// 所以这里的键序与 Rust 不同，键与取值完全一致（GUI 按键取值）。
Json okEnvelope(const Json &data);
/// 对应 `ApiResponse::<()>::err(message)`：data 键整个省略。
Json errorEnvelope(const QString &message);

/// 200 + ok 信封。
Response ok(const Json &data);
/// 指定状态的 ok 信封（create_source 用 201）。
Response okWithStatus(int status, const Json &data);
/// 指定状态的错误信封（500 = error_resp）。
Response error(int status, const QString &message);
/// extractor 拒绝：axum 的 (status, 文本) 响应。
Response rejection(int status, const QString &detail);

/// 判断 Content-Type 是否是 application/json（axum 只看 type/subtype，允许带参数）。
bool isJsonContentType(const QString &contentType);

/// 增量解析：Ok(nullopt) = 数据还不完整（*consumed = 0）；Ok(request) = 读掉 *consumed 字节；
/// Err = 报文非法（调用方回 400 并断开，对应 hyper 的解析失败）。
Result<std::optional<Request>> parseRequest(const QByteArray &buffer, int *consumed);

/// form_urlencoded 语义的解码：'+' → 空格，%XX → 字节，非法序列原样保留。
QString decodeFormValue(const QString &raw);
/// 路径段的百分号解码（'+' 不是空格）。
QString decodePathSegment(const QString &raw);
/// 状态码 → hyper 的 reason phrase。
QString reasonPhrase(int status);

/// 请求体上限：axum 的 DefaultBodyLimit 默认 2 MiB。
inline constexpr qint64 kDefaultBodyLimitBytes = 2 * 1024 * 1024;
/// 头部块上限：没有对应 Rust 常量（hyper 也没有），只是别让人拿半个请求把内存吃掉。
inline constexpr int kMaxHeaderBlockBytes = 64 * 1024;

} // namespace Http

} // namespace Api
