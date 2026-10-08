#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

#include "core/Result.h"

namespace Net {

struct Header {
    QString name;
    QString value;
};

struct Request {
    QString url;
    QByteArray method = QByteArrayLiteral("GET");
    QByteArray body;
    QString contentType;
    QList<Header> headers;
    /// 单位 ms。整个请求（含重定向）的总时限。
    int timeoutMs = 30000;
    int connectTimeoutMs = 10000;
    bool followRedirects = true;
    /// 形如 `http://127.0.0.1:9980`；为空表示直连。
    QString proxyUrl;
    /// 条件请求：非空才发对应头。
    QString ifNoneMatch;
    QString ifModifiedSince;
    /// 每个调用方用的 UA 不同（订阅 / telegram / geo / 内核更新各自一个），
    /// 空则不发这个头。取值必须与 Rust 侧一致，有些服务器按 UA 放行。
    QString userAgent;
    /// 最多跟随多少跳重定向（Rust 侧 reqwest 默认 10）。
    int maxRedirects = 10;
};

struct Response {
    int status = 0;
    QByteArray body;
    QList<Header> headers;
    QString finalUrl;
    /// HTTP 层之外失败时才填（DNS/连接/TLS/超时）。
    QString error;

    QString headerValue(const QString &name) const;
    bool isSuccess() const { return status >= 200 && status < 300; }
};

/// 阻塞式请求：只在 worker 线程里调用（GUI 线程走 sendAsync）。
/// 网络层失败返回 Err（对应 Rust 的 reqwest Error），HTTP 4xx/5xx 是正常的 Response。
Result<Response> send(const Request &request);

/// 是否需要重试：连接类错误、超时、408/429/5xx。对应 collector 的重试判定。
bool isRetryable(const Result<Response> &result);

}
