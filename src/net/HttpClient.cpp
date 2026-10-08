#include "net/HttpClient.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace Net {

QString Response::headerValue(const QString &name) const
{
    for (const Header &header : headers) {
        if (header.name.compare(name, Qt::CaseInsensitive) == 0) {
            return header.value;
        }
    }
    return QString();
}

namespace {

QList<Header> collectHeaders(const QNetworkReply *reply)
{
    QList<Header> headers;
    const auto pairs = reply->rawHeaderPairs();
    for (const auto &pair : pairs) {
        headers.append({QString::fromUtf8(pair.first), QString::fromUtf8(pair.second)});
    }
    return headers;
}

/// 把 Qt 的网络错误翻译成 Rust 侧那种"传输层失败"文本。HTTP 状态码本身不算失败。
QString transportError(const QNetworkReply *reply, bool timedOut)
{
    if (timedOut) {
        return QStringLiteral("请求超时");
    }
    switch (reply->error()) {
    case QNetworkReply::NoError:
        return QString();
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("远程主机关闭连接");
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝");
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("找不到主机");
    case QNetworkReply::TimeoutError:
        return QStringLiteral("连接超时");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("TLS 握手失败");
    case QNetworkReply::ProxyConnectionRefusedError:
        return QStringLiteral("代理拒绝连接");
    case QNetworkReply::ProxyConnectionClosedError:
        return QStringLiteral("代理关闭连接");
    case QNetworkReply::ProxyTimeoutError:
        return QStringLiteral("代理超时");
    case QNetworkReply::InternalServerError:
        return QStringLiteral("HTTP 内部错误");
    default:
        return reply->errorString();
    }
}

}

/// 阻塞式请求。只能在 worker 线程调用：内部跑一个局部事件循环。
Result<Response> send(const Request &request)
{
    QNetworkAccessManager manager;
    QNetworkReply *reply = nullptr;
    // Qt 6.10 的 QNetworkRequest 没有 setProxy，只能设在 manager 上；
    // 这里每次调用一个 manager，正好等价于 reqwest 每次建一个带代理的 client。
    QNetworkProxy proxy;
    bool hasProxy = false;
    QNetworkRequest netRequest{QUrl(request.url)};
    // reqwest 默认跟随重定向并拒绝"https→http"降级；NoLessSafeRedirectPolicy 是同一套规则。
    netRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                            QNetworkRequest::NoLessSafeRedirectPolicy);
    netRequest.setMaximumRedirectsAllowed(request.maxRedirects);
    if (request.connectTimeoutMs > 0) {
        // Qt 6.10 没有连接超时属性，只能靠下面的整体时限兜住。
    }
    if (!request.proxyUrl.isEmpty()) {
        QUrl proxyUrl = QUrl(request.proxyUrl);
        if (proxyUrl.scheme().isEmpty()) {
            proxyUrl = QUrl(QStringLiteral("http://%1").arg(request.proxyUrl));
        }
        const int port = proxyUrl.port(proxyUrl.scheme() == QLatin1String("socks5") ? 1080 : 80);
        proxy.setType(proxyUrl.scheme() == QLatin1String("socks5")
                          ? QNetworkProxy::Socks5Proxy
                          : QNetworkProxy::HttpProxy);
        proxy.setHostName(proxyUrl.host());
        proxy.setPort(port);
        hasProxy = true;
    }
    if (hasProxy) {
        manager.setProxy(proxy);
    }
    if (!request.contentType.isEmpty()) {
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, request.contentType);
    }
    for (const Header &header : request.headers) {
        netRequest.setRawHeader(header.name.toUtf8(), header.value.toUtf8());
    }
    if (!request.ifNoneMatch.isEmpty()) {
        netRequest.setRawHeader("If-None-Match", request.ifNoneMatch.toUtf8());
    }
    if (!request.ifModifiedSince.isEmpty()) {
        netRequest.setRawHeader("If-Modified-Since", request.ifModifiedSince.toUtf8());
    }
    if (!request.userAgent.isEmpty()) {
        netRequest.setRawHeader("User-Agent", request.userAgent.toUtf8());
    }

    reply = manager.sendCustomRequest(netRequest, request.method, request.body);
    QPointer<QNetworkReply> guard(reply);
    bool timedOut = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        timedOut = true;
        if (guard) {
            guard->abort();
        }
    });
    timer.start(request.timeoutMs);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (reply->isFinished()) {
        loop.quit();
    }
    loop.exec();
    timer.stop();

    Response response;
    response.finalUrl = reply->url().toString();
    response.headers = collectHeaders(reply);
    response.body = reply->readAll();
    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    response.status = statusVariant.toInt();
    const QString transport = transportError(reply, timedOut);
    const QByteArray reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute)
                                  .toByteArray();
    reply->deleteLater();

    if (transport.isEmpty() || response.status > 0) {
        // 4xx/5xx 是响应，不是传输失败：状态码交给调用方判断。
        if (response.status == 0) {
            return Result<Response>::withError(transport.isEmpty()
                                                   ? QStringLiteral("请求失败")
                                                   : transport);
        }
        return Result<Response>::withValue(std::move(response));
    }
    QString message = transport;
    if (!reason.isEmpty()) {
        message += QStringLiteral("（%1）").arg(QString::fromUtf8(reason));
    }
    return Result<Response>::withError(message);
}

/// 连接错误、超时、408/429/5xx 值得重试；404/403 这类是确定的失败。
bool isRetryable(const Result<Response> &result)
{
    if (result.isErr()) {
        return true;
    }
    const int status = result.value().status;
    return status == 408 || status == 429 || status >= 500;
}

}
