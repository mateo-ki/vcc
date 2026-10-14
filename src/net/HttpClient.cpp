#include "net/HttpClient.h"

#include <QFile>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslError>
#include <QThreadStorage>
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
struct ThreadLocalHttpState {
    QNetworkAccessManager manager;
    QString currentProxyUrl;
    bool proxyConfigured = false;
};

static QThreadStorage<std::shared_ptr<ThreadLocalHttpState>> s_threadHttpState;

Result<Response> send(const Request &request)
{
    auto state = s_threadHttpState.localData();
    if (!state) {
        state = std::make_shared<ThreadLocalHttpState>();
        s_threadHttpState.setLocalData(state);
    }
    QNetworkAccessManager &manager = state->manager;
    QNetworkReply *reply = nullptr;
    // Qt 6.10 的 QNetworkRequest 没有 setProxy，只能设在 manager 上；
    // 这里每次调用一个 manager，正好等价于 reqwest 每次建一个带代理的 client。
    QNetworkProxy proxy;
    bool hasProxy = false;
    QNetworkRequest netRequest{QUrl(request.url)};
    QSslConfiguration sslConfig = netRequest.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    netRequest.setSslConfiguration(sslConfig);
    netRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    // reqwest 默认跟随重定向并拒绝"https→http"降级；NoLessSafeRedirectPolicy 是同一套规则。
    netRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                            QNetworkRequest::NoLessSafeRedirectPolicy);
    netRequest.setMaximumRedirectsAllowed(request.maxRedirects);
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
    if (!state->proxyConfigured || state->currentProxyUrl != request.proxyUrl) {
        state->currentProxyUrl = request.proxyUrl;
        state->proxyConfigured = true;
        manager.setProxy(hasProxy ? proxy : QNetworkProxy(QNetworkProxy::NoProxy));
    }
    if (!netRequest.hasRawHeader("Accept")) {
        netRequest.setRawHeader("Accept", "*/*");
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
    // reqwest 只有在 builder 里显式设了 UA 才发这个头，没设就一个都不发；这里不能自己
    // 编一个默认值，否则"不带 UA"的调用方（IpRisk 探针）发出去的就不是它要的请求。
    if (!request.userAgent.isEmpty()) {
        netRequest.setRawHeader("User-Agent", request.userAgent.toUtf8());
    }

    reply = manager.sendCustomRequest(netRequest, request.method, request.body);
    reply->ignoreSslErrors();
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
        reply->ignoreSslErrors();
    });
    QPointer<QNetworkReply> guard(reply);
    bool timedOut = false;
    bool headerTimedOut = false;
    bool connectTimedOut = false;
    bool cancelledByCaller = false;
    bool bodyIdleTimedOut = false;
    bool headersSeen = false;
    bool linkUp = false;
    qint64 receivedBytes = 0;

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        timedOut = true;
        if (guard) {
            guard->abort();
        }
    });
    timer.start(request.timeoutMs);

    // 响应头预算：到点就断，等价于 Rust 只给 `send()`（拿到头为止）的那段 timeout。
    QTimer headerTimer;
    headerTimer.setSingleShot(true);
    QObject::connect(&headerTimer, &QTimer::timeout, [&]() {
        headerTimedOut = true;
        if (guard) {
            guard->abort();
        }
    });
    if (request.headerTimeoutMs > 0) {
        headerTimer.start(request.headerTimeoutMs);
    }

    // 连接预算：对应 Rust 的 `Client::connect_timeout`（只管 DNS+TCP+TLS 建连那一段）。
    // 没有它时一个连不上的镜像要烧掉调用方的全部时限（geo 是 180s；实测 12s 预算也只会在
    // 12s 才回），而 Rust 6s 就放手。
    // Qt 6.10 既没有连接超时属性，也没有 QNetworkAccessManager::connected 信号，所以只能拿
    // "能证明链路已经活着"的最早信号停表：HTTPS 用 reply->encrypted（TLS 握手完成，正是
    // reqwest 放手的那一刻），响应头/进度/重定向兜底。表一停就不再管这一段，所以
    // "连上了但 8s 才回头"的服务器不会被误杀 —— 那归响应头预算和整体时限管。
    // **只对 https 生效**：明文 http 在 Qt 里没有任何"连接已建立"信号，硬撑这道预算会把
    // "接了连接、一个字都不回"的正常服务器误判成连不上（Rust 那边它 connect_timeout 早就
    // 过了，等的是响应头预算）。这类对端仍由 headerTimeoutMs / timeoutMs 管。
    QTimer connectTimer;
    connectTimer.setSingleShot(true);
    QObject::connect(&connectTimer, &QTimer::timeout, [&]() {
        if (linkUp) {
            return;
        }
        connectTimedOut = true;
        if (guard) {
            guard->abort();
        }
    });
    auto markLinkUp = [&]() {
        linkUp = true;
        connectTimer.stop();
    };
    QObject::connect(reply, &QNetworkReply::encrypted, &connectTimer, markLinkUp);
    QObject::connect(reply, &QNetworkReply::redirected, &connectTimer, markLinkUp);
    QObject::connect(reply, &QNetworkReply::uploadProgress, &connectTimer, markLinkUp);
    QObject::connect(reply, &QNetworkReply::downloadProgress, &connectTimer, markLinkUp);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &connectTimer, markLinkUp);
    if (request.connectTimeoutMs > 0 && netRequest.url().scheme() == QLatin1String("https")) {
        connectTimer.start(request.connectTimeoutMs);
    }

    // 取消轮询：调用方（竞速里的赢家）在别的线程上把标志一置，这里最多 40ms 后
    // 自己 abort。只有 reply 的所有线程碰 reply，所以跨线程传的只是一个原子标志。
    QTimer cancelPoll;
    cancelPoll.setInterval(40);
    QObject::connect(&cancelPoll, &QTimer::timeout, [&]() {
        if (request.cancel == nullptr || !request.cancel->isCancelled()) {
            return;
        }
        cancelledByCaller = true;
        cancelPoll.stop();
        if (guard) {
            guard->abort();
        }
    });
    if (request.cancel != nullptr) {
        cancelPoll.start();
    }

    // 读体阶段的块间空闲预算，每收到一块就重置。用 downloadProgress 而不是 readyRead：
    // 这里不中途 readAll，Qt 会把数据攒在缓冲里，readyRead 不一定逐块来。
    QTimer idleTimer;
    idleTimer.setSingleShot(true);
    QObject::connect(&idleTimer, &QTimer::timeout, [&]() {
        bodyIdleTimedOut = true;
        if (guard) {
            guard->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::metaDataChanged, [&]() {
        if (headersSeen) {
            return;
        }
        headersSeen = true;
        headerTimer.stop();
        if (request.bodyIdleTimeoutMs > 0) {
            idleTimer.start(request.bodyIdleTimeoutMs);
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 bytesReceived,
                                                                qint64 bytesTotal) {
        receivedBytes = bytesReceived;
        if (headersSeen && request.bodyIdleTimeoutMs > 0) {
            idleTimer.start(request.bodyIdleTimeoutMs);
        }
        if (request.downloadProgress) {
            // 总长直接取 Qt 算好的那个（重定向/分块编码下它比 Content-Length 可靠）。
            request.downloadProgress(bytesReceived, bytesTotal);
        }
    });

    QFile sink;
    QString sinkPath;
    bool sinkCommitted = false;
    if (!request.downloadToPath.isEmpty()) {
        sinkPath = request.downloadToPath;
        sink.setFileName(sinkPath);
        if (!sink.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            reply->deleteLater();
            return Result<Response>::withError(
                QStringLiteral("打不开下载目标文件：%1").arg(sink.errorString()));
        }
        // 边收边写：不排空的话 Qt 会把整条响应攒在自己的缓冲里，23MB 就真进内存了。
        QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
            sink.write(reply->readAll());
        });
    }
    // 失败不留残包：超时/取消/传输错/非 2xx 的提前 return 都会经过这个守卫把文件删掉，
    // 只有提交过（2xx 且写全）才留在盘上。
    struct SinkCleanup {
        QFile *file;
        QString path;
        const bool *committed;
        ~SinkCleanup()
        {
            if (file->isOpen()) {
                file->close();
            }
            if (!path.isEmpty() && !*committed) {
                QFile::remove(path);
            }
        }
    } sinkCleanup{ &sink, sinkPath, &sinkCommitted };

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (reply->isFinished()) {
        loop.quit();
    }
    loop.exec();
    timer.stop();
    headerTimer.stop();
    connectTimer.stop();
    cancelPoll.stop();
    idleTimer.stop();

    if (cancelledByCaller) {
        reply->deleteLater();
        // 必须排在其它判定之前：取消会让 Qt 自己报 OperationCanceled/超时一类错误，
        // 但那不是"请求失败"，是有人主动让它放手，调用方要能分清这两种。
        return Result<Response>::withError(cancelledError());
    }
    if (connectTimedOut) {
        reply->deleteLater();
        // 和 Qt 自己把 TimeoutError 翻出来的那句同一个串：Collector 已经按 Rust 的
        // is_connect() 分支认它（Collector.cpp:443），这里不能另造一个新哨兵。
        return Result<Response>::withError(QStringLiteral("连接超时"));
    }
    if (headerTimedOut) {
        reply->deleteLater();
        // 调用方按 Rust 的 Elapsed 分支拼"响应头超时: Ns"，这里只给判定用的哨兵。
        return Result<Response>::withError(QStringLiteral("响应头超时"));
    }
    if (bodyIdleTimedOut) {
        reply->deleteLater();
        // 逐字对齐 Rust 的 read_source_body 超时文本（mod.rs:759-765）。
        return Result<Response>::withError(
            QStringLiteral("response body idle for %1s after %2 bytes (overall limit %3s)")
                .arg(request.bodyIdleTimeoutMs / 1000)
                .arg(receivedBytes)
                .arg(request.timeoutMs / 1000));
    }

    Response response;
    response.finalUrl = reply->url().toString();
    response.headers = collectHeaders(reply);
    if (request.downloadToPath.isEmpty()) {
        response.body = reply->readAll();
    } else {
        sink.write(reply->readAll());
    }
    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    response.status = statusVariant.toInt();
    const QString transport = transportError(reply, timedOut);
    const QByteArray reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute)
                                  .toByteArray();
    reply->deleteLater();

    sinkCommitted = response.status >= 200 && response.status < 300;

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
