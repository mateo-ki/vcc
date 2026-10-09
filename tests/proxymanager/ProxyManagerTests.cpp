#include <gtest/gtest.h>

#include <Windows.h>
#include <tlhelp32.h>

#include <QCoreApplication>
#include <QDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <atomic>
#include <thread>

#include "core/Ids.h"
#include "kernel/KernelTypes.h"
#include "models/Node.h"
#include "proxy/MihomoConfig.h"
#include "proxy/ProxyManager.h"
#include "proxy/WorkDir.h"

namespace {

/// 对应 Rust 测试里的 `node(protocol, config)`。
Models::Node node(const QString &protocol, const Json &config)
{
    Models::Node result;
    result.id = Ids::newUuidV4();
    result.protocol = protocol;
    result.name = QStringLiteral("test-") + protocol;
    result.server = QString::fromStdString(config.at("server").get<std::string>());
    result.port = config.at("port").get<int>();
    result.raw = QStringLiteral("%1://%2:%3").arg(protocol, result.server).arg(result.port);
    result.config = config;
    return result;
}

/// 占一个端口再放开，拿到一个当下空闲的端口（Rust 测试用 TcpListener::bind(:0) + drop）。
std::optional<quint16> freePort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return std::nullopt;
    }
    const quint16 port = server.serverPort();
    server.close();
    return port;
}

Proxy::MihomoConfigOptions stoppedOptions(quint16 mixed, quint16 controller)
{
    Proxy::MihomoConfigOptions options;
    options.mixedPort = mixed;
    options.controllerPort = controller;
    // 空规则：默认规则里没有 GEOIP/GEOSITE，启动路径就不会去下载分流库（测试不许联网）。
    options.rules.clear();
    return options;
}

/// 有没有 mihomo/sing-box/xray 在跑。启动路径的端口重试第二年会调
/// Platform::killOrphanProxyCores()，而它按映像名杀进程（与 Rust 一致，不认父进程），
/// 所以本机只要有一个活的代理内核，这两个启动测试就必须跳过而不是跑。
bool anyProxyCoreRunning()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return true;  // 看不见就当有，宁可不跑。
    }
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL ok = Process32First(snapshot, &entry); ok; ok = Process32Next(snapshot, &entry)) {
        const QString image = QString::fromWCharArray(entry.szExeFile).toLower();
        if (image == QLatin1String("mihomo.exe") || image == QLatin1String("sing-box.exe")
            || image == QLatin1String("xray.exe") || image == QLatin1String("xray-ws.exe")) {
            found = true;
            break;
        }
    }
    CloseHandle(snapshot);
    return found;
}

#define SKIP_IF_PROXY_CORE_RUNNING()                                         if (anyProxyCoreRunning()) {                                                 GTEST_SKIP() << "本机有活的代理内核，启动路径会触发按映像名杀进程的清理";     }

/// 一个只会回 502 的假本地代理，跑在自己的线程里：verifyLocalProxy 会在测试线程上
/// 阻塞在 QNetworkAccessManager 的事件循环里，两边必须分开。
class RejectingProxy {
public:
    explicit RejectingProxy(quint16 port) : port_(port)
    {
        worker_ = std::thread([this]() { run(); });
    }

    ~RejectingProxy()
    {
        stopping_.store(true);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void run()
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, port_)) {
            return;
        }
        while (!stopping_.load()) {
            if (!server.waitForNewConnection(200)) {
                continue;
            }
            QTcpSocket *socket = server.nextPendingConnection();
            if (socket == nullptr) {
                continue;
            }
            socket->waitForReadyRead(500);
            socket->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n");
            socket->waitForBytesWritten(500);
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }

    quint16 port_;
    std::atomic_bool stopping_{false};
    std::thread worker_;
};

}

TEST(ProxyManagerState, startsInStoppedState)
{
    Proxy::ProxyManager manager{Proxy::MihomoConfigOptions()};
    const Proxy::ProxyRuntimeStatus status = manager.status();
    EXPECT_EQ(status.state, Proxy::ProxyState::Stopped);
    EXPECT_TRUE(status.nodeId.isNull());
    EXPECT_FALSE(manager.isRunning());
}

TEST(ProxyManagerState, updatesValidConfigWhileStopped)
{
    const std::optional<quint16> mixed = freePort();
    const std::optional<quint16> controller = freePort();
    ASSERT_TRUE(mixed.has_value());
    ASSERT_TRUE(controller.has_value());

    const Proxy::MihomoConfigOptions initialOptions = stoppedOptions(*mixed, *controller);
    Proxy::ProxyManager manager(initialOptions);
    Proxy::MihomoConfigOptions options = manager.config();
    options.mode = Proxy::ProxyMode::Tun;
    Proxy::TrafficRule rule;
    rule.matcher = Proxy::RuleMatcher::simple(Proxy::RuleMatcher::Kind::GeoSite,
                                              QStringLiteral("cn"));
    rule.action = Proxy::RuleAction::Direct;
    options.rules.push_back(rule);

    const Status updated = manager.updateConfig(options);
    ASSERT_TRUE(updated.isOk()) << updated.error().toStdString();

    const Proxy::MihomoConfigOptions stored = manager.config();
    EXPECT_EQ(stored.mode, Proxy::ProxyMode::Tun);
    ASSERT_EQ(stored.rules.size(), 1);
    EXPECT_EQ(stored.rules.at(0).toMihomoRule().value().toStdString(),
              std::string("GEOSITE,cn,DIRECT"));
    EXPECT_EQ(manager.status().mode, Proxy::ProxyMode::Tun);
}

TEST(ProxyManagerState, configIsOnlyEditableWhenNotRunning)
{
    // Rust 那条测试直接改私有 runtime 把状态拨成 Running 再断言 update_config 报错；
    // C++ 里 runtime 是私有的，而真起进程又会杀到别人的内核，所以这里只锁住判据本身：
    // "在跑 = 不许改配置"这条规则的唯一实现处就是 ProxyRuntimeStatus::configEditable()。
    Proxy::ProxyRuntimeStatus status =
        Proxy::ProxyRuntimeStatus::stopped(Proxy::MihomoConfigOptions());
    ASSERT_TRUE(status.configEditable());

    status.state = Proxy::ProxyState::Running;
    EXPECT_FALSE(status.configEditable());

    status.state = Proxy::ProxyState::Degraded;
    status.localProxyReady = true;
    EXPECT_FALSE(status.configEditable());

    status.localProxyReady = false;
    EXPECT_TRUE(status.configEditable());

    Proxy::ProxyManager manager{Proxy::MihomoConfigOptions()};
    Proxy::MihomoConfigOptions updated = Proxy::MihomoConfigOptions();
    updated.mixedPort = 18888;
    // 停止态允许改，并且改完立刻生效于内存等下次启动。
    ASSERT_TRUE(manager.updateConfig(updated).isOk());
    EXPECT_EQ(manager.config().mixedPort, 18888);
    EXPECT_EQ(manager.status().mixedPort, 18888);
}

TEST(ProxyManagerStart, missingMihomoExecutableSetsFailedStatus)
{
    SKIP_IF_PROXY_CORE_RUNNING();
    const std::optional<quint16> mixed = freePort();
    const std::optional<quint16> controller = freePort();
    ASSERT_TRUE(mixed.has_value());
    ASSERT_TRUE(controller.has_value());

    const Proxy::MihomoConfigOptions options = stoppedOptions(*mixed, *controller);
    Proxy::ProxyManager manager(options);
    const Models::Node selected = node(QStringLiteral("vless"), Json{
        {"server", "1.2.3.4"}, {"port", 443}, {"uuid", "11111111-1111-1111-1111-111111111111"}});
    const QString missing = QDir(QDir::tempPath())
                                .filePath(QStringLiteral("vpn-parser-missing-mihomo-%1.exe")
                                              .arg(Ids::newUuidV4Simple()));

    const Status started =
        manager.startWithKernelPath(selected, Kernel::Kind::Mihomo, missing);
    ASSERT_TRUE(started.isErr());
    EXPECT_TRUE(started.error().contains(QStringLiteral("启动"))) << started.error().toStdString();

    const Proxy::ProxyRuntimeStatus status = manager.status();
    EXPECT_EQ(status.state, Proxy::ProxyState::Failed);
    EXPECT_TRUE(status.message.contains(QStringLiteral("启动")));
    EXPECT_EQ(status.nodeId.toStdString(), selected.id.toStdString());
}

TEST(ProxyManagerStart, missingSingBoxExecutableKeepsSelectedKernelInStatus)
{
    SKIP_IF_PROXY_CORE_RUNNING();
    const std::optional<quint16> mixed = freePort();
    ASSERT_TRUE(mixed.has_value());

    const Proxy::MihomoConfigOptions options = stoppedOptions(*mixed, 19090);
    Proxy::ProxyManager manager(options);
    const Models::Node selected = node(QStringLiteral("anytls"), Json{
        {"server", "anytls.example.com"}, {"port", 443}, {"password", "secret"}});
    const QString missing = QDir(QDir::tempPath())
                                .filePath(QStringLiteral("vpn-parser-missing-sing-box-%1.exe")
                                              .arg(Ids::newUuidV4Simple()));

    const Status started = manager.startWithKernelPath(selected, Kernel::Kind::SingBox, missing);
    ASSERT_TRUE(started.isErr());
    EXPECT_TRUE(started.error().contains(QStringLiteral("Sing-box"))) << started.error().toStdString();

    const Proxy::ProxyRuntimeStatus status = manager.status();
    EXPECT_EQ(status.state, Proxy::ProxyState::Failed);
    ASSERT_TRUE(status.kernel.has_value());
    EXPECT_EQ(*status.kernel, Kernel::Kind::SingBox);
    EXPECT_TRUE(status.message.contains(QStringLiteral("Sing-box")));
}

TEST(ProxyManagerStart, startupProbeRejectsProxyThatCannotReachTestUrl)
{
    const std::optional<quint16> port = freePort();
    ASSERT_TRUE(port.has_value());
    RejectingProxy proxy(*port);

    const Status verified = Proxy::verifyLocalProxy(*port, 1500);
    ASSERT_TRUE(verified.isErr());
    // 502 而不是 204：这句话要和 Rust 的 `本地代理无法访问测试地址` 完全一致。
    EXPECT_TRUE(verified.error().contains(QStringLiteral("本地代理无法访问测试地址")))
        << verified.error().toStdString();
}

TEST(ProxyManagerContracts, startupBudgetsMatchRust)
{
    EXPECT_EQ(Proxy::kStartupTotalTimeoutMs, 5000);
    EXPECT_EQ(Proxy::kStartupVerifyTimeoutMs, 1500);
    EXPECT_EQ(Proxy::kStartupSystemProxyReserveMs, 250);
    EXPECT_EQ(Proxy::kStartupPortConnectTimeoutMs, 60);
    // 启动时就真验一次连通性，不是"端口通了就算起好了"。
    EXPECT_TRUE(Proxy::kVerifyConnectivityOnStart);
}

TEST(ProxyManagerContracts, kernelsExposePortsAndBatchSizes)
{
    Proxy::MihomoConfigOptions options;
    options.mixedPort = 9980;
    options.controllerPort = 9090;

    EXPECT_EQ(Proxy::kernelBatchSize(Kernel::Kind::Mihomo), 256);
    EXPECT_EQ(Proxy::kernelBatchSize(Kernel::Kind::SingBox), 64);
    EXPECT_EQ(Proxy::kernelBatchSize(Kernel::Kind::Xray), 64);

    EXPECT_EQ(Proxy::kernelReadinessPort(Kernel::Kind::Mihomo, options), 9090);
    EXPECT_EQ(Proxy::kernelReadinessPort(Kernel::Kind::SingBox, options), 9980);

    // mihomo 两个端口都要能占住；sing-box/xray 只用 mixed。
    const QVector<quint16> mihomoPorts =
        Proxy::kernelRequiredPorts(Kernel::Kind::Mihomo, options);
    EXPECT_EQ(mihomoPorts.size(), 2);
    EXPECT_TRUE(mihomoPorts.contains(9980));
    EXPECT_TRUE(mihomoPorts.contains(9090));
    EXPECT_EQ(Proxy::kernelRequiredPorts(Kernel::Kind::SingBox, options), QVector<quint16>{9980});
}

TEST(ProxyManagerContracts, workDirUsesKernelSpecificPathsAndCleansUp)
{
    struct Case {
        Kernel::Kind kernel;
        const char *configName;
        const char *logName;
    };
    const QVector<Case> cases = {
        {Kernel::Kind::Mihomo, "config.yaml", "mihomo.log"},
        {Kernel::Kind::SingBox, "config.json", "sing-box.log"},
        {Kernel::Kind::Xray, "config.json", "xray.log"},
    };
    for (const Case &entry : cases) {
        Result<Proxy::ProxyWorkDir> created = Proxy::ProxyWorkDir::create(entry.kernel);
        ASSERT_TRUE(created.isOk()) << created.error().toStdString();
        Proxy::ProxyWorkDir workDir = created.takeValue();
        const QString root = workDir.root();
        EXPECT_TRUE(QDir(root).exists());
        EXPECT_EQ(workDir.configPath().toStdString(),
                  QDir(root).filePath(QString::fromUtf8(entry.configName)).toStdString());
        EXPECT_EQ(workDir.logPath().toStdString(),
                  QDir(root).filePath(QString::fromUtf8(entry.logName)).toStdString());
        // mihomo 的 data root 固定在托管内核目录（geo 库放那儿），其它内核就是自己那层。
        if (entry.kernel == Kernel::Kind::Mihomo) {
            EXPECT_NE(workDir.dataRoot().indexOf(QStringLiteral("mihomo")), -1);
        } else {
            EXPECT_EQ(workDir.dataRoot().toStdString(), root.toStdString());
        }
        ASSERT_TRUE(workDir.cleanup().isOk());
        EXPECT_FALSE(QDir(root).exists());
    }
}

int main(int argc, char *argv[])
{
    // verifyLocalProxy 走 QNetworkAccessManager，没有 QCoreApplication 实例会直接
    // ASSERT 在 applicationstatic.h。
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
