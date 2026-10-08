#include "api/HttpServer.h"

#include <QHostAddress>
#include <QPointer>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QElapsedTimer>

#include <QtConcurrent>

#include "api/ApiRouter.h"
#include "api/HttpMessage.h"
#include "core/Log.h"

// Qt 6.10 这棵树里没有 HTTP server 模块，所以这里是 QTcpServer 上的最小 HTTP/1.1：
// 请求行 + 头 + 固定长度 body（不做 chunked），keep-alive 复用连接，
// handler 在 QtConcurrent 的工作线程里跑（DB/网络都是阻塞实现，绝不能占住事件循环），
// 结果用队列调用回投到本线程写 socket。
namespace Api {

namespace {

constexpr int kIdleTimeoutMs = 75 * 1000;
constexpr int kShutdownPollMs = 50;
/// 优雅关停时等在途请求写完的上限（Rust 的 graceful shutdown 是无上限等待）。
constexpr int kShutdownDrainMs = 2000;

/// 单个 TCP 连接的状态机。只在所属服务器线程上访问；socket_ 是子对象，随本对象销毁。
///
/// 生命周期规则（防悬垂）：有请求在 worker 线程里跑时 inFlight_ > 0，本对象绝不销毁；
/// worker 回来时先 --inFlight_ 再决定关闭，所以排队的回调一定能碰到活对象。
class Connection : public QObject {
public:
    Connection(QTcpSocket *socket, const std::shared_ptr<Router> &router)
        : router_(router), socket_(socket)
    {
        socket_->setParent(this);
        idleTimer_ = new QTimer(this);
        idleTimer_->setInterval(kIdleTimeoutMs);
        idleTimer_->setSingleShot(true);
        QObject::connect(socket_, &QTcpSocket::readyRead, this, [this] { onReadyRead(); });
        QObject::connect(socket_, &QTcpSocket::disconnected, this, [this] { onDisconnected(); });
        QObject::connect(socket_, &QTcpSocket::errorOccurred, this,
                         [this] { onDisconnected(); });
        QObject::connect(idleTimer_, &QTimer::timeout, this, [this] { closeSocket(); });
        idleTimer_->start();
    }

    bool isIdle() const { return inFlight_ == 0; }

    /// 关停：空闲就立刻断，正在处理请求则写完这一个再断。
    void beginShutdown()
    {
        shutdownRequested_ = true;
        if (inFlight_ == 0) {
            closeSocket();
        }
    }

private:
    void onReadyRead()
    {
        buffer_.append(socket_->readAll());
        idleTimer_->start();
        drain();
    }

    void onDisconnected()
    {
        disconnected_ = true;
        if (inFlight_ == 0) {
            deleteLater();
        }
    }

    void drain()
    {
        if (inFlight_ > 0 || disconnected_ || shutdownRequested_) {
            return;
        }
        int consumed = 0;
        const Result<std::optional<Request>> parsed = Http::parseRequest(buffer_, &consumed);
        if (parsed.isErr()) {
            rejectAndClose(parsed.error());
            return;
        }
        if (!parsed.value().has_value()) {
            return; // 数据还没收全
        }
        Request request = std::move(*parsed.value());
        buffer_.remove(0, consumed);
        beginRequest(std::move(request));
    }

    static bool wantsKeepAlive(const Request &request)
    {
        const QString connection = request.header(QStringLiteral("connection")).toLower();
        if (connection.contains(QLatin1String("close"))) {
            return false;
        }
        if (request.version == QLatin1String("HTTP/1.0")) {
            return connection.contains(QLatin1String("keep-alive"));
        }
        return true;
    }

    void beginRequest(Request request)
    {
        const bool keepAlive = wantsKeepAlive(request) && !shutdownRequested_;
        currentMethodIsHead_ = request.method == QLatin1String("HEAD");
        ++inFlight_;
        std::shared_ptr<Router> router = router_;
        // 返回值（QFuture）不需要：结果通过队列调用回到本线程。
        QThreadPool::globalInstance()->start([router, request, keepAlive, this]() {
            Response response = router->dispatch(request);
            QMetaObject::invokeMethod(
                this, [this, response, keepAlive] { finishRequest(response, keepAlive); },
                Qt::QueuedConnection);
        });
    }

    void finishRequest(const Response &response, bool keepAlive)
    {
        --inFlight_;
        writeResponse(response, keepAlive);
        if (!keepAlive || disconnected_ || shutdownRequested_) {
            closeSocket();
            return;
        }
        drain();
    }

    void writeResponse(const Response &response, bool keepAlive)
    {
        if (disconnected_ || socket_ == nullptr
            || socket_->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        Response copy = response;
        if (!keepAlive) {
            copy.setHeader(QStringLiteral("connection"), QStringLiteral("close"));
        }
        // HEAD：hyper 保留头（含 content-length）但不发 body。
        const bool head = currentMethodIsHead_;
        socket_->write(copy.toBytes(!head));
        socket_->flush();
    }

    void rejectAndClose(const QString &reason)
    {
        // 报文非法 → 400（hyper 的解析失败）；body 超过 axum 的 2 MiB 默认上限 → 413。
        const bool tooLarge = reason.contains(QLatin1String("payload too large"));
        QByteArray payload = "HTTP/1.1 " + QByteArray::number(tooLarge ? 413 : 400)
            + (tooLarge ? " Payload Too Large" : " Bad Request") + "\r\ncontent-type: "
            + (tooLarge ? "text/plain; charset=utf-8" : "application/json") + "\r\n";
        QByteArray body;
        if (tooLarge) {
            body = QByteArray("Failed to buffer the request body");
        } else {
            body = QByteArray::fromStdString(Http::errorEnvelope(QStringLiteral("Malformed request")).dump());
        }
        payload += "content-length: " + QByteArray::number(body.size())
            + "\r\nconnection: close\r\n\r\n" + body;
        if (socket_ && socket_->state() == QAbstractSocket::ConnectedState) {
            socket_->write(payload);
            socket_->flush();
        }
        closeSocket();
    }

    void closeSocket()
    {
        if (socket_ != nullptr && socket_->state() == QAbstractSocket::ConnectedState) {
            socket_->disconnectFromHost();
        }
        if (inFlight_ == 0) {
            deleteLater();
        }
    }

    std::shared_ptr<Router> router_;
    QTcpSocket *socket_ = nullptr;
    QTimer *idleTimer_ = nullptr;
    QByteArray buffer_;
    int inFlight_ = 0;
    bool disconnected_ = false;
    bool shutdownRequested_ = false;
    bool currentMethodIsHead_ = false;
};

struct ListenAddress {
    bool ok = false;
    QHostAddress address;
    quint16 port = 0;
    QString error;
};

/// 对应 Rust 的 `listen.parse::<SocketAddr>()`：只接受 IP 字面量 + 端口。
ListenAddress parseListenAddress(const QString &listen)
{
    ListenAddress result;
    const int separator = listen.lastIndexOf(QLatin1Char(':'));
    if (separator <= 0 || separator == listen.size() - 1) {
        result.error = QStringLiteral("invalid socket address: %1").arg(listen);
        return result;
    }
    const QString host = listen.left(separator);
    bool portOk = false;
    const uint port = listen.mid(separator + 1).toUInt(&portOk);
    QHostAddress address;
    if (!portOk || port == 0 || port > 65535 || !address.setAddress(host)) {
        result.error = QStringLiteral("invalid socket address: %1").arg(listen);
        return result;
    }
    result.ok = true;
    result.address = address;
    result.port = static_cast<quint16>(port);
    return result;
}

/// 事件循环里的服务器本体：监听 + 连接表。
class Server : public QObject {
public:
    Server(const AppState &state, const std::optional<QString> &apiToken)
        : router_(std::make_shared<Router>(state, apiToken))
    {
        QObject::connect(&listener_, &QTcpServer::newConnection, this, [this] { onNewConnection(); });
    }

    Result<Status> bind(const QString &listen)
    {
        const ListenAddress parsed = parseListenAddress(listen);
        if (!parsed.ok) {
            return Result<Status>::withError(parsed.error);
        }
        if (!listener_.listen(parsed.address, parsed.port)) {
            return Result<Status>::withError(
                QStringLiteral("failed to bind %1: %2").arg(listen, listener_.errorString()));
        }
        boundAddress_ = QStringLiteral("%1:%2")
                            .arg(listener_.serverAddress().toString())
                            .arg(listener_.serverPort());
        return Result<Status>::withValue(Status::succeeded());
    }

    QString boundAddress() const { return boundAddress_; }

    /// 停止接受新连接，等在途请求写完（最多 kShutdownDrainMs），然后关掉所有连接。
    void shutdown()
    {
        if (shutDown_) {
            return;
        }
        shutDown_ = true;
        listener_.close();
        for (const QPointer<Connection> &connection : connections_) {
            if (connection) {
                connection->beginShutdown();
            }
        }
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < kShutdownDrainMs) {
            bool busy = false;
            for (const QPointer<Connection> &connection : connections_) {
                if (connection && !connection->isIdle()) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        for (const QPointer<Connection> &connection : connections_) {
            if (connection) {
                connection->beginShutdown();
            }
        }
    }

private:
    void onNewConnection()
    {
        while (QTcpSocket *socket = listener_.nextPendingConnection()) {
            auto *connection = new Connection(socket, router_);
            QObject::connect(socket, &QTcpSocket::disconnected, connection,
                             [this, connection] { dropConnection(connection); });
            connections_.append(QPointer<Connection>(connection));
            pruneConnections();
        }
    }

    void dropConnection(Connection *connection)
    {
        for (int index = connections_.size() - 1; index >= 0; --index) {
            if (connections_.at(index).data() == connection) {
                connections_.removeAt(index);
            }
        }
    }

    void pruneConnections()
    {
        for (int index = connections_.size() - 1; index >= 0; --index) {
            if (connections_.at(index).isNull()) {
                connections_.removeAt(index);
            }
        }
    }

    std::shared_ptr<Router> router_;
    QTcpServer listener_;
    QVector<QPointer<Connection>> connections_;
    QString boundAddress_;
    bool shutDown_ = false;
};

} // namespace

Result<Status> serveWithShutdown(const AppState &state, const QString &listen,
                                 const std::optional<QString> &apiToken,
                                 const std::atomic_bool &shutdownRequested)
{
    Server server(state, apiToken);
    const Result<Status> bound = server.bind(listen);
    if (bound.isErr()) {
        return bound;
    }
    Log::info(QStringLiteral("HTTP API listening on %1").arg(server.boundAddress()));

    QEventLoop loop;
    QTimer watcher;
    watcher.setInterval(kShutdownPollMs);
    QObject::connect(&watcher, &QTimer::timeout, [&loop, &shutdownRequested] {
        if (shutdownRequested.load(std::memory_order_acquire)) {
            loop.quit();
        }
    });
    watcher.start();
    loop.exec();
    watcher.stop();

    server.shutdown();
    Log::info(QStringLiteral("HTTP API server stopped (%1)")
                  .arg(server.boundAddress()));
    return Result<Status>::withValue(Status::succeeded());
}

Result<Status> serve(const AppState &state, const QString &listen,
                     const std::optional<QString> &apiToken)
{
    // 对应 Rust 的 `serve`：没有 shutdown 通道，一直跑到进程结束。
    static const std::atomic_bool neverShutdown{false};
    return serveWithShutdown(state, listen, apiToken, neverShutdown);
}

} // namespace Api
