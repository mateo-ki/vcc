// 一次性取证工具：量"我们的 HTTP 客户端在真网络上到底多快"，以及 connectTimeoutMs 现在
// 管不管用。跑法：VCC_NET_PROBE_URL=<要下的 url> 跑 vccNetTests（直连，不走系统代理）。
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <psapi.h>
#endif

#include <iostream>

#include "geo/Geo.h"
#include "net/HttpClient.h"

namespace {

struct Sample
{
    qint64 elapsedMs = 0;
    qint64 bytes = 0;
    int status = 0;
    QString error;
};

Sample download(const QString &url, int timeoutMs, int connectTimeoutMs = 0,
                int headerTimeoutMs = 0, int bodyIdleTimeoutMs = 0)
{
    Net::Request request;
    request.url = url;
    request.timeoutMs = timeoutMs;
    request.connectTimeoutMs = connectTimeoutMs;
    request.headerTimeoutMs = headerTimeoutMs;
    request.bodyIdleTimeoutMs = bodyIdleTimeoutMs;
    request.proxyUrl = qEnvironmentVariable("VCC_NET_PROBE_PROXY");
    request.userAgent = QStringLiteral("vcc geo-updater");
    // 故意给一个比整个下载还短的建连预算：只有"链路已建立"的信号真的把停表掐掉，
    // 这次下载才可能成功 —— 用来证明 https 的连接预算不会误杀正常的慢下载。
    request.connectTimeoutMs = connectTimeoutMs > 0
                                   ? connectTimeoutMs
                                   : qEnvironmentVariableIntValue("VCC_NET_PROBE_CONNECT_MS");

    Sample sample;
    QElapsedTimer clock;
    clock.start();
    const Result<Net::Response> result = Net::send(request);
    sample.elapsedMs = clock.elapsed();
    if (result.isErr()) {
        sample.error = result.error();
        return sample;
    }
    sample.status = result.value().status;
    sample.bytes = result.value().body.size();
    return sample;
}

#ifdef Q_OS_WIN
qint64 privateBytes()
{
    // PrivateUsage 只在 _EX 那份结构里（WorkingSetSize 是基类那份就有的）。
    PROCESS_MEMORY_COUNTERS_EX counters;
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters)))
    {
        return -1;
    }
    return qint64(counters.PrivateUsage);
}
#endif

void print(const char *label, const Sample &sample)
{
    const double mb = sample.bytes / 1048576.0;
    const double seconds = sample.elapsedMs / 1000.0;
    std::cout << "PROBE " << label << ": status=" << sample.status
              << " bytes=" << sample.bytes << " (" << mb << "MB)"
              << " elapsed_ms=" << sample.elapsedMs << " speed="
              << (seconds > 0 ? mb / seconds : 0.0) << "MB/s"
              << (sample.error.isEmpty() ? "" : " err=") << sample.error.toStdString()
              << std::endl;
}

/// 同一个镜像连下三次：第一次含 DNS+TCP+TLS，后两次如果客户端复用连接就该明显更快。
/// curl 的基线（本机实测）：geosite.dat 11141017B / 1.34s，geoip.dat 16583067B / 1.92s。
TEST(NetProbe, realMirrorThroughput)
{
    const QString url = qEnvironmentVariable("VCC_NET_PROBE_URL");
    if (url.isEmpty()) {
        GTEST_SKIP() << "no VCC_NET_PROBE_URL";
    }
    for (int attempt = 1; attempt <= 3; ++attempt) {
        print(("direct-attempt-" + QString::number(attempt)).toLocal8Bit().constData(),
              download(url, 60000));
    }
}

/// 一个"接不上也没人回话"的地址：Rust 的 reqwest 有 6s connect_timeout，会在这里 6s 放手；
/// Qt 没有这个属性（HttpClient.cpp:89 是空实现），所以只会等到整体 timeoutMs。
/// 这条量的是"死镜像要烧掉多少秒"——geo 那三个镜像的整体时限是 180s。
TEST(NetProbe, connectBudgetOnUnreachableHost)
{
    print("https-connectTimeout3s-total12s",
          download(QStringLiteral("https://192.0.2.1/geo.dat"), 12000, 3000));
    print("http-connectTimeout3s-total12s",
          download(QStringLiteral("http://192.0.2.1/geo.dat"), 12000, 3000));
    std::cout << "PROBE verdict: 3000 => 连接预算生效, ~12000 => 只能靠整体时限" << std::endl;
}

/// 真跑一遍"一键更新 geo（代理没在跑）"那条路：Geo::provision(dir, "", force=true)。
/// 这里量的是端到端：三个镜像串行、每个 6s 连接预算 + 180s 整体时限、两个文件。
TEST(NetProbe, geoProvisionFullRun)
{
    const QString dir = qEnvironmentVariable("VCC_GEO_PROBE_DIR");
    if (dir.isEmpty()) {
        GTEST_SKIP() << "no VCC_GEO_PROBE_DIR";
    }
    QElapsedTimer clock;
    clock.start();
    const Result<QString> report = Geo::provision(dir, QString(), true);
    const qint64 elapsed = clock.elapsed();
    quint64 bytes = 0;
    for (const QString &name : Geo::kGeoFiles) {
        const QFileInfo info(QDir(dir).filePath(name));
        if (info.exists()) {
            bytes += quint64(info.size());
        }
    }
    std::cout << "PROBE geo-provision: elapsed_ms=" << elapsed << " on_disk_mb="
              << (bytes / 1048576.0) << " result="
              << (report.isOk() ? report.value() : report.error()).toStdString() << std::endl;
}

/// 一个"接了连接就不回话"的明文服务器，把 Qt 在建立链路这一步到底发哪些信号打出来。
/// 实测结论（Qt 6.10）：QNetworkAccessManager 已经没有 connected/… 那对信号，明文 http
/// 在"连接已建立"上没有任何可用信号 —— 所以连接预算只能对 https（有 reply->encrypted）生效。
TEST(NetProbe, signalTimelineOnStallingServer)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress(QHostAddress::LocalHost), 0));
    QList<QTcpSocket *> held;   // 接了连接、读完请求，一个字节都不回
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&held, &server] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            held.push_back(socket);
        }
    });

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QStringLiteral("http://127.0.0.1:%1/x").arg(server.serverPort()))};
    QElapsedTimer wall;
    wall.start();
    QObject::connect(&manager, &QNetworkAccessManager::encrypted, &manager,
                     [&wall](QNetworkReply *) {
                         std::cout << "SIGNAL manager::encrypted @" << wall.elapsed() << "ms"
                                   << std::endl;
                     });
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [&wall, reply] {
        std::cout << "SIGNAL metaDataChanged @" << wall.elapsed() << "ms status="
                  << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                  << std::endl;
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [&wall](qint64 got, qint64) {
                         std::cout << "SIGNAL downloadProgress " << got << " @"
                                   << wall.elapsed() << "ms" << std::endl;
                     });
    QTimer::singleShot(4000, reply, [reply] { reply->abort(); });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    std::cout << "SIGNAL final error=" << int(reply->error()) << " @" << wall.elapsed()
              << "ms" << std::endl;
    reply->deleteLater();
}

#ifdef Q_OS_WIN
/// 每条请求都要新建一个 QNetworkAccessManager + QNetworkReply，而 `reply->deleteLater()`
/// 是在局部事件循环**退出之后**才调的 —— 跑在 QtConcurrent 工作线程上，那些线程没有事件循环。
/// 这条量的是"500 条请求打完，私有提交内存回没回来"：回不来就是每条漏一个 reply/QNAM。
TEST(NetProbe, repeatedSendsDoNotGrowPrivateBytes)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress(QHostAddress::LocalHost), 0));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
                socket->flush();
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        }
    });

    Net::Request request;
    request.url = QStringLiteral("http://127.0.0.1:%1/ping").arg(server.serverPort());
    request.timeoutMs = 3000;

    auto sendBurst = [&request](int count) {
        int ok = 0;
        for (int i = 0; i < count; ++i) {
            const Result<Net::Response> result = Net::send(request);
            if (result.isOk() && result.value().status == 200) {
                ++ok;
            }
        }
        return ok;
    };

    ASSERT_EQ(sendBurst(50), 50) << "假服务器没答上来，后面的量数没有意义";
    HANDLE self = GetCurrentProcess();
    DWORD handlesBefore = 0;
    GetProcessHandleCount(self, &handlesBefore);
    const qint64 bytesBefore = privateBytes();
    const int second = sendBurst(500);
    DWORD handlesAfter = 0;
    DWORD privateAfter = 0;
    GetProcessHandleCount(self, &handlesAfter);
    const qint64 bytesAfter = privateBytes();
    std::cout << "PROBE repeated-sends: ok=" << second << "/500 private_before_mb="
              << (bytesBefore / 1048576.0) << " private_after_mb=" << (bytesAfter / 1048576.0)
              << " delta_kb=" << ((bytesAfter - bytesBefore) / 1024)
              << " handles=" << handlesBefore << "->" << handlesAfter << std::endl;
    ASSERT_EQ(second, 500);
    // 500 条请求若每条漏一个 reply（含缓冲），量级会是几百 KB 到几 MB；给 1MB 容差。
    EXPECT_LT(bytesAfter - bytesBefore, 1024 * 1024) << "每条请求都在漏";
    // 每请求一个 QNetworkAccessManager 最容易漏的是句柄（socket/event/mutex）：
    // 500 条若每条留一个，句柄数会净涨几百个。
    EXPECT_LT(qint64(handlesAfter) - qint64(handlesBefore), 50) << "句柄在净涨";
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
