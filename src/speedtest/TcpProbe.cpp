#include "speedtest/TcpProbe.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostInfo>
#include <QMetaObject>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <vector>

#include "speedtest/SpeedtestMessages.h"
#include "speedtest/SpeedtestTypes.h"

namespace Speedtest {
namespace {

/// connect_fast 给 DNS 的预算上限（Rust: timeout.min(Duration::from_millis(800)))。
inline constexpr qint64 kDnsCapMs = 800;
/// measure_tcp_download 写请求的时限（Rust: Duration::from_millis(400)）。
inline constexpr qint64 kWriteTimeoutMs = 400;
/// 读缓冲 16 KiB（Rust: vec![0u8; 16 * 1024]）。
inline constexpr int kReadChunkBytes = 16 * 1024;
/// 只留前 512 字节用来判「是不是普通网站」。
inline constexpr int kHeadBytes = 512;

/// Rust 的 `SocketAddr.port()` 是 u16；models 里存的是 int，这里按 u16 截断保持一致。
quint16 toPort(int port)
{
    return quint16(port & 0xFFFF);
}

/// tokio 的 lookup_host 会把 IPv4 排在前面（Rust: sort_by_key(is_ipv4)），且是稳定排序。
void preferIPv4(QVector<QHostAddress> &addresses)
{
    std::stable_sort(addresses.begin(), addresses.end(),
                     [](const QHostAddress &left, const QHostAddress &right) {
                         const bool leftV4 = left.protocol() == QAbstractSocket::IPv4Protocol;
                         const bool rightV4 = right.protocol() == QAbstractSocket::IPv4Protocol;
                         return leftV4 && !rightV4;
                     });
}

}

Result<QVector<QHostAddress>> resolveAddresses(const QString &host, qint64 timeoutMs)
{
    QElapsedTimer started;
    started.start();
    // 与 Rust 的区别：QHostInfo::fromName 在本线程直接跑 getaddrinfo，不能中途取消，
    // 所以超预算是「事后」判定的（结果照样丢掉，报 dns timeout）。
    const QHostInfo info = QHostInfo::fromName(host);
    if (started.elapsed() >= timeoutMs) {
        return Result<QVector<QHostAddress>>::withError(QStringLiteral("dns timeout"));
    }
    if (info.error() != QHostInfo::NoError) {
        return Result<QVector<QHostAddress>>::withError(
            QStringLiteral("dns error: %1").arg(info.errorString()));
    }
    QVector<QHostAddress> addresses = info.addresses();
    preferIPv4(addresses);
    return Result<QVector<QHostAddress>>::withValue(std::move(addresses));
}

Result<Connected> connectFast(const QString &host, int port, qint64 timeoutMs)
{
    QElapsedTimer started;
    started.start();
    // 慢解析器不能把 worker 拖满整个超时预算（Rust: timeout.min(800ms)）。
    const qint64 dnsBudgetMs = qMin(timeoutMs, qint64(kDnsCapMs));
    Result<QVector<QHostAddress>> resolved = resolveAddresses(host, dnsBudgetMs);
    if (resolved.isErr()) {
        return Result<Connected>::withError(resolved.error());
    }
    const QVector<QHostAddress> addresses = resolved.takeValue();
    if (addresses.isEmpty()) {
        return Result<Connected>::withError(QStringLiteral("no address"));
    }

    // 预算内最多试 2 个地址。
    QString lastError = QStringLiteral("connect failed");
    const int attempts = int(qMin<qsizetype>(addresses.size(), 2));
    for (int index = 0; index < attempts; ++index) {
        const qint64 left = timeoutMs - started.elapsed();
        if (left <= 0) {
            break;
        }
        auto socket = std::make_unique<QTcpSocket>();
        socket->connectToHost(addresses.at(index), toPort(port));
        if (socket->waitForConnected(int(qMin<qint64>(left, INT_MAX)))) {
            Connected connected;
            connected.latencyMs = qMax<qint64>(1, started.elapsed());
            connected.socket = std::move(socket);
            return Result<Connected>::withValue(std::move(connected));
        }
        if (socket->error() == QAbstractSocket::SocketTimeoutError) {
            // Rust 在这里报的是整体超时值，不是剩余预算。
            lastError = QStringLiteral("timeout %1ms").arg(timeoutMs);
        } else {
            lastError = QStringLiteral("connect error: %1").arg(socket->errorString());
        }
    }
    return Result<Connected>::withError(lastError);
}

Result<Connected> connectResolved(const QVector<QHostAddress> &addresses, int port,
                                  qint64 timeoutMs)
{
    if (addresses.isEmpty()) {
        return Result<Connected>::withError(QStringLiteral("dns returned no addresses"));
    }

    QElapsedTimer started;
    started.start();

    // 对应 Rust 的 JoinSet + timeout_at：所有地址一起发起，谁先连上算谁。
    // 用局部事件循环而不是每地址一个线程，避免在 worker 线程池里再套一层等待。
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);

    // QList 要求元素可复制，持有独占 socket 只能用 std::vector。
    std::vector<std::unique_ptr<QTcpSocket>> sockets;
    sockets.reserve(size_t(addresses.size()));
    QVector<QMetaObject::Connection> guards;
    guards.reserve(addresses.size() * 2);
    int winner = -1;
    QString lastError = QStringLiteral("connect failed");

    for (int index = 0; index < addresses.size(); ++index) {
        auto socket = std::make_unique<QTcpSocket>();
        QTcpSocket *raw = socket.get();
        guards.append(QObject::connect(raw, &QAbstractSocket::connected, &loop,
                                       [&loop, index, &winner]() {
                                           if (winner < 0) {
                                               winner = index;
                                               loop.quit();
                                           }
                                       }));
        guards.append(QObject::connect(raw, &QAbstractSocket::errorOccurred, &loop,
                                       [raw, &lastError]() { lastError = raw->errorString(); }));
        raw->connectToHost(addresses.at(index), toPort(port));
        sockets.push_back(std::move(socket));
    }
    deadline.start(qMax<qint64>(1, timeoutMs));
    loop.exec();

    for (const QMetaObject::Connection &guard : guards) {
        QObject::disconnect(guard);
    }

    if (winner >= 0) {
        for (int index = 0; index < int(sockets.size()); ++index) {
            if (index != winner) {
                sockets[size_t(index)]->abort();
            }
        }
        Connected connected;
        connected.latencyMs = qMax<qint64>(1, started.elapsed());
        connected.socket = std::move(sockets[size_t(winner)]);
        return Result<Connected>::withValue(std::move(connected));
    }
    for (const std::unique_ptr<QTcpSocket> &socket : sockets) {
        socket->abort();
    }
    if (started.elapsed() >= timeoutMs) {
        return Result<Connected>::withError(QStringLiteral("timeout %1ms").arg(timeoutMs));
    }
    return Result<Connected>::withError(lastError);
}

DownloadMeasurement measureTcpDownload(QTcpSocket &socket, const QString &host, qint64 downloadMs)
{
    // 尽力而为：加密代理大多不会回应这个明文 GET，测不到就留空。
    const QByteArray request = QStringLiteral(
                                   "GET / HTTP/1.1\r\nHost: %1\r\nUser-Agent: "
                                   "vcc-speedtest/0.1\r\nAccept: */*\r\nConnection: "
                                   "close\r\n\r\n")
                                   .arg(host)
                                   .toUtf8();
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    socket.write(request);
    QElapsedTimer writeBudget;
    writeBudget.start();
    while (socket.bytesToWrite() > 0) {
        const qint64 left = kWriteTimeoutMs - writeBudget.elapsed();
        if (left <= 0 || !socket.waitForBytesWritten(int(left))) {
            return DownloadMeasurement{std::nullopt, QStringLiteral("write timeout")};
        }
    }

    QElapsedTimer deadline;
    deadline.start();
    quint64 total = 0;
    QByteArray head;
    head.reserve(512);
    const QElapsedTimer readStart;

    for (;;) {
        const qint64 left = downloadMs - deadline.elapsed();
        if (left <= 0) {
            break;
        }
        if (!socket.waitForReadyRead(int(left))) {
            // 超时和出错在 Rust 里都是 break（Ok(Err(_)) / Err(_)）。
            break;
        }
        const QByteArray chunk = socket.read(kReadChunkBytes);
        if (chunk.isEmpty()) {
            break;
        }
        if (head.size() < kHeadBytes) {
            head += chunk.left(int(kHeadBytes) - head.size());
        }
        total += quint64(chunk.size());
    }

    const double elapsedSeconds = qMax(0.001, readStart.elapsed() / 1000.0);
    if (total < 64) {
        return DownloadMeasurement{
            std::nullopt, Messages::format(Messages::onlyReceivedBytes(), {},
                                           {{"{total}", QString::number(total)}})};
    }
    if (looksLikePublicWebsite(head)) {
        return DownloadMeasurement{
            std::nullopt, Messages::format(Messages::publicWebsiteNote(), {},
                                           {{"{total}", QString::number(total)}})};
    }
    const double kbps = (double(total) / 1024.0) / elapsedSeconds;
    return DownloadMeasurement{kbps, QStringLiteral("%1 bytes").arg(total)};
}

bool looksLikePublicWebsite(const QByteArray &head)
{
    // 只按 7-bit ASCII 小写，和 Rust 的 to_ascii_lowercase 一致。
    const QByteArray lower = head.toLower();
    if (!lower.startsWith("http/1.") && !lower.startsWith("http/2")) {
        return false;
    }
    static const char *kStatuses[] = {" 200 ", " 301 ", " 302 ", " 307 ", " 308 ", " 403 ", " 404 "};
    bool statusOk = false;
    for (const char *status : kStatuses) {
        if (lower.contains(status)) {
            statusOk = true;
            break;
        }
    }
    if (!statusOk) {
        return false;
    }
    static const char *kMarkers[] = {"text/html",
                                     "text/plain",
                                     "application/json",
                                     "<!doctype html",
                                     "<html",
                                     "server: cloudflare",
                                     "server: nginx",
                                     "server: apache",
                                     "cf-ray:"};
    for (const char *marker : kMarkers) {
        if (lower.contains(marker)) {
            return true;
        }
    }
    return false;
}

}
