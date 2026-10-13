#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

#include "net/HttpClient.h"

// Net::send 的分段超时：响应头预算只管"等头"（Rust 的 timeout(header_budget, send())），
// 头到了以后按块间空闲计时（Rust 的 BODY_IDLE_TIMEOUT），整体仍受 timeoutMs 约束。
// 这条契约是 vcc 拉取"比 Rust 慢半拍的大订阅"被掐死的根因，全部用 127.0.0.1 假服务器验。
namespace {

/// 慢而活的正文：12 × 32KiB，块间 150ms ≈ 1.8s，头 0.xs 就到。
constexpr int kChunks = 12;
constexpr int kChunkBytes = 32 * 1024; // 12 × 32KiB = 393216
constexpr int kChunkGapMs = 150;

enum class Behaviour {
    Silent,        // 接了连接、读完请求，一个字都不回
    HeadersThenStall, // 头 + 半截体，然后断死
    SlowButLiving, // 头很快，正文一块块慢慢来（始终不断流）
};

class FakeServer : public QObject {
public:
    explicit FakeServer(Behaviour behaviour) : behaviour_(behaviour) {}

    bool start()
    {
        server_ = new QTcpServer(this);
        if (!server_->listen(QHostAddress(QHostAddress::LocalHost), 0)) {
            return false;
        }
        port_ = server_->serverPort();
        connect(server_, &QTcpServer::newConnection, this, [this] { accept(); });
        return true;
    }

    quint16 port() const { return port_; }

private:
    void accept()
    {
        while (QTcpSocket *socket = server_->nextPendingConnection()) {
            sockets_.push_back(socket);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { respond(socket); });
        }
    }

    void respond(QTcpSocket *socket)
    {
        if (answered_.contains(socket)) {
            return; // 请求体为空，首个可读事件就当整条请求到位
        }
        answered_.insert(socket);
        switch (behaviour_) {
        case Behaviour::Silent:
            return;
        case Behaviour::HeadersThenStall:
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 200\r\n\r\n");
            socket->write(QByteArray(20, 'x'));
            socket->flush();
            return;
        case Behaviour::SlowButLiving: {
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 393216\r\n\r\n");
            socket->flush();
            auto sent = std::make_shared<int>(0);
            auto pump = std::make_shared<std::function<void()>>();
            *pump = [socket, sent, pump] {
                if (*sent >= kChunks) {
                    socket->flush();
                    socket->disconnectFromHost();
                    return;
                }
                ++*sent;
                socket->write(QByteArray(kChunkBytes, 'y'));
                socket->flush();
                QTimer::singleShot(kChunkGapMs, socket, [pump] { (*pump)(); });
            };
            QTimer::singleShot(kChunkGapMs, socket, [pump] { (*pump)(); });
            return;
        }
        }
    }

    Behaviour behaviour_;
    QTcpServer *server_ = nullptr;
    quint16 port_ = 0;
    std::vector<QTcpSocket *> sockets_;
    QSet<QTcpSocket *> answered_;
};

Net::Request requestFor(const FakeServer &server, int timeoutMs, int headerTimeoutMs = 0,
                        int bodyIdleTimeoutMs = 0)
{
    Net::Request request;
    request.url = QStringLiteral("http://127.0.0.1:%1/subscription").arg(server.port());
    request.timeoutMs = timeoutMs;
    request.headerTimeoutMs = headerTimeoutMs;
    request.bodyIdleTimeoutMs = bodyIdleTimeoutMs;
    request.userAgent = QStringLiteral("vpn-parser/1.0 (+https://localhost)");
    return request;
}

TEST(HttpClientTests, aPeerThatNeverAnswersHeadersFailsOnTheHeaderBudgetNotTheWholeOne)
{
    FakeServer server(Behaviour::Silent);
    ASSERT_TRUE(server.start());

    QElapsedTimer started;
    started.start();
    const Result<Net::Response> result = Net::send(requestFor(server, 8000, 500, 500));
    const qint64 elapsed = started.elapsed();

    ASSERT_TRUE(result.isErr());
    ASSERT_EQ(result.error(), QStringLiteral("响应头超时"));
    ASSERT_GE(elapsed, 400) << elapsed;
    ASSERT_LE(elapsed, 2500) << "没有按响应头预算放手: " << elapsed;
}

TEST(HttpClientTests, aBodyThatStopsMidwayIsReportedWithRustsIdleMessage)
{
    FakeServer server(Behaviour::HeadersThenStall);
    ASSERT_TRUE(server.start());

    // 头预算要给够：这条测的是"正文半途停"的落点，不是头预算。假服务器和客户端跑在
    // 同一个事件循环里，400ms 的余量在本机够、在慢机器上会先被"响应头超时"抢掉。
    const Result<Net::Response> result = Net::send(requestFor(server, 8000, 3000, 1000));

    ASSERT_TRUE(result.isErr());
    // Rust read_source_body（mod.rs:759-765）的原话形状。
    ASSERT_TRUE(result.error().startsWith(
        QStringLiteral("response body idle for 1s after "))) << result.error().toStdString();
    ASSERT_TRUE(result.error().contains(QStringLiteral("(overall limit 8s)")))
        << result.error().toStdString();
}

/// 这一条就是"粘贴订阅拉取失败、手点又成功"的根因回归：头 0.3s 就到，正文要 1.8s 才滴完。
/// 把响应头和响应体合成一个 11s 看门狗时它还能侥幸，头预算一收紧到 6s 就会被误杀；
/// 分段之后它必须稳稳成功。
TEST(HttpClientTests, aSlowButLivingBodyIsNotCutOffByTheHeaderBudget)
{
    FakeServer server(Behaviour::SlowButLiving);
    ASSERT_TRUE(server.start());

    QElapsedTimer started;
    started.start();
    // 头预算 500ms 在慢机器上会被"进程内假服务器还没来得及写头"先吃掉（CI 实测 890ms
    // 报响应头超时），那就变成在测调度而不是测正文续命。抬到 1500ms：正文 12×150ms
    // ≈1.8s 仍然明显比头预算活得久，这条的论点不变。
    const Result<Net::Response> result = Net::send(requestFor(server, 8000, 1500, 1500));
    const qint64 elapsed = started.elapsed();

    ASSERT_TRUE(result.isOk()) << result.error().toStdString();
    ASSERT_EQ(result.value().status, 200);
    ASSERT_EQ(result.value().body.size(), kChunks * kChunkBytes);
    ASSERT_GT(elapsed, 600) << "正文比头预算活得久，这条才有意义: " << elapsed;
    ASSERT_LE(elapsed, 6000) << elapsed;
}

/// 不设分段预算的调用方（IP 风险探针、内核下载等）行为不变：只有整体 timeoutMs。
TEST(HttpClientTests, withoutSegmentBudgetsTheOverallTimeoutStillRules)
{
    FakeServer server(Behaviour::Silent);
    ASSERT_TRUE(server.start());

    const Result<Net::Response> result = Net::send(requestFor(server, 600));

    ASSERT_TRUE(result.isErr());
    ASSERT_EQ(result.error(), QStringLiteral("请求超时"));
}

/// 连接预算（Rust 的 reqwest connect_timeout）：https 下 TLS 握手迟迟不完成，就要在
/// connectTimeoutMs 放手，而不是把调用方的整体时限烧光 —— geo 那三个镜像给的是
/// 6s 建连 + 180s 整体，缺了这条时一个连不上的镜像要吃掉整整 180s（实测）。
/// 这里用"接了 TCP、对 ClientHello 一个字都不回"的本地假服务器复现握手卡死。
TEST(HttpClientTests, aTlsHandshakeThatNeverCompletesIsReleasedByTheConnectBudget)
{
    FakeServer server(Behaviour::Silent);
    ASSERT_TRUE(server.start());

    Net::Request request = requestFor(server, 8000);
    request.url = QStringLiteral("https://127.0.0.1:%1/geo.dat").arg(server.port());
    request.connectTimeoutMs = 800;

    QElapsedTimer started;
    started.start();
    const Result<Net::Response> result = Net::send(request);
    const qint64 elapsed = started.elapsed();

    ASSERT_TRUE(result.isErr());
    ASSERT_EQ(result.error(), QStringLiteral("连接超时")) << elapsed;
    ASSERT_GE(elapsed, 700) << elapsed;
    ASSERT_LE(elapsed, 3000) << "没有按建连预算放手: " << elapsed;
}

/// 另一半：明文 http 在 Qt 里没有任何"连接已建立"信号（实测：对端接了连接不回话时，
/// 直到 abort 都只有一个 downloadProgress(0) ），拿不到建连的证据就不能瞎判超时 ——
/// 否则"接了连接、一个字都不回"的服务器会被误报成连不上，而 Rust 那边它 connect_timeout
/// 早过了，等的是响应头预算。所以这条必须还是"响应头超时"。
TEST(HttpClientTests, aPlainHttpPeerThatAcceptsButNeverAnswersIsNotReportedAsDeadHost)
{
    FakeServer server(Behaviour::Silent);
    ASSERT_TRUE(server.start());

    Net::Request request = requestFor(server, 8000, 2500);
    request.connectTimeoutMs = 600;

    QElapsedTimer started;
    started.start();
    const Result<Net::Response> result = Net::send(request);
    const qint64 elapsed = started.elapsed();

    ASSERT_TRUE(result.isErr());
    ASSERT_EQ(result.error(), QStringLiteral("响应头超时")) << elapsed;
    ASSERT_GE(elapsed, 2000) << "建连预算越界把明文 http 也管了: " << elapsed;
}

/// 取消通道（Rust 那边是 JoinSet::abort_all）：对端接了连接不回话时，只要有人把标志
/// 一置，send 必须在一个轮询周期内自己放手并报"已取消" —— 而不是等满整体 timeoutMs。
/// 竞速里落败的腿就走这条路，所以"取消"必须和"失败"可区分：Collector 不许把它记进
/// 失败清单（那条拼接文案里混进自己人就等于谎报上游挂了）。
TEST(HttpClientTests, aCancelledRequestLetsGoAndIsNotReportedAsAFailure)
{
    FakeServer server(Behaviour::Silent);
    ASSERT_TRUE(server.start());

    Net::Request request = requestFor(server, 8000);
    request.cancel = std::make_shared<Net::CancelFlag>();
    QTimer::singleShot(400, qApp, [flag = request.cancel] { flag->cancel(); });

    QElapsedTimer started;
    started.start();
    const Result<Net::Response> result = Net::send(request);
    const qint64 elapsed = started.elapsed();

    ASSERT_TRUE(result.isErr());
    ASSERT_EQ(result.error(), Net::cancelledError()) << elapsed;
    ASSERT_GE(elapsed, 350) << elapsed;
    ASSERT_LE(elapsed, 2500) << "没按取消信号放手: " << elapsed;
}

} // namespace

int main(int argc, char *argv[])
{
    // QNetworkAccessManager 需要 QCoreApplication 实例，否则直接崩。
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
