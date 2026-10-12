#include <gtest/gtest.h>

#include <Windows.h>
#include <tlhelp32.h>

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <atomic>
#include <chrono>
#include <future>
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
    explicit RejectingProxy(quint16 port = 0,
                            const char *reply = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n")
        : requested_(port), reply_(reply)
    {
        worker_ = std::thread([this]() { run(); });
        // 等监听真的就绪再返回：端口给 0 时由系统挑，避免"先占一次再放开"那个窗口里
        // 端口被别的 socket 抢走 —— 那样 fake 会静默 bind 失败，测试只是超时，看不出原因。
        for (int spin = 0; spin < 200 && !ready_.load(); ++spin) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    quint16 port() const { return port_.load(); }

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
        if (!server.listen(QHostAddress::LocalHost, requested_)) {
            ready_.store(true);
            return;
        }
        port_.store(server.serverPort());
        ready_.store(true);
        QVector<QTcpSocket *> live;
        while (!stopping_.load()) {
            if (!server.waitForNewConnection(50)) {
                continue;
            }
            QTcpSocket *socket = server.nextPendingConnection();
            if (socket == nullptr) {
                continue;
            }
            socket->waitForReadyRead(500);
            socket->write(reply_);
            socket->flush();
            // 这条线程没有事件循环，所以三件事都不能干：
            // ① deleteLater() —— 永远不生效，"200 + Content-Length: 0" 那种响应客户端
            //    等不到结束，只能超时；② abort() —— 那是 RST，实测约 5 次里 2 次会让
            //    客户端把已经收到的完整响应判成连接失败（假失败）；③ waitForDisconnected()
            //    —— 客户端跑 keep-alive，它的 FIN 要等自己析构 socket 才发，这条循环就被
            //    按住 500ms，下一次尝试连都连不上（同样是假失败）。
            // 正解：响应写完就留着连接，等测试结束统一 abort+delete。
            live.push_back(socket);
        }
        for (QTcpSocket *socket : live) {
            socket->abort();
            delete socket;
        }
    }

    quint16 requested_ = 0;
    const char *reply_;
    std::atomic<quint16> port_{0};
    std::atomic_bool ready_{false};
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
    RejectingProxy proxy;
    ASSERT_NE(proxy.port(), 0);

    const Status verified = Proxy::verifyLocalProxy(proxy.port(), 1500);
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

/// 只认 204：透明网关/登录页经常回 200，放过去就等于把不通的节点判成通。
/// vcc 之前既多带一条测试 URL 又接受 200，Rust 两个都不是这样，所以两边都要钉住。
/// 这里用 http:// 而不是真地址：HTTPS 下代理回 200 是"隧道建好了"，不是站点响应码。
TEST(ProxyManagerStart, verifyRejectsNon204Success)
{
    RejectingProxy proxy200(0, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    ASSERT_NE(proxy200.port(), 0);

    const Status verified = Proxy::verifyLocalProxyAgainst(
        proxy200.port(), QStringLiteral("http://proxy.invalid/probe"), 2000);
    ASSERT_TRUE(verified.isErr()) << "200 绝不能算通过";
    // 最后一发 attempt 往往只剩几毫秒、来不及拿到响应，于是错误会被那次的超时覆盖 ——
    // Rust 完全一样（proxy/mod.rs:1484 `last_errors = attempt_errors` 是整组替换），
    // 所以"必须含 预期 204"这条断言本质上会随机红（实测约 8 次红 2 次）。
    // 钉住真正有意义的那两件事：绝不能算通；报出来必须是 Rust 那两种形状之一。
    EXPECT_TRUE(verified.error().startsWith(QStringLiteral("本地代理无法访问测试地址: ")))
        << verified.error().toStdString();
    EXPECT_TRUE(verified.error().contains(QStringLiteral("预期 204"))
                || verified.error().contains(QStringLiteral("请求超时")))
        << verified.error().toStdString();
}

TEST(ProxyManagerStart, verifyAcceptsARealTwoOhFour)
{
    RejectingProxy proxy204(0, "HTTP/1.1 204 No Content\r\n\r\n");
    ASSERT_NE(proxy204.port(), 0);

    const Status verified = Proxy::verifyLocalProxyAgainst(
        proxy204.port(), QStringLiteral("http://proxy.invalid/probe"), 1500);
    EXPECT_TRUE(verified.isOk()) << verified.error().toStdString();
}

TEST(ProxyManagerContracts, verifyAttemptBudgetsMatchRust)
{
    // Rust: attempt_timeout = remaining.min(900ms)、connect_timeout = 600ms、轮间睡 40ms。
    EXPECT_EQ(Proxy::kVerifyAttemptTimeoutMs, 900);
    EXPECT_EQ(Proxy::kVerifyAttemptConnectTimeoutMs, 600);
    EXPECT_EQ(Proxy::kVerifyAttemptRetrySleepMs, 40);
}

/// 对应 Rust 的 degraded_status_distinguishes_ready_and_waiting_states：
/// Degraded 有两种，"本地端口就绪但外面不通"和"还在等"，靠 localProxyReady 分开。
TEST(ProxyManagerContracts, degradedStatusDistinguishesReadyAndWaitingStates)
{
    Proxy::ProxyRuntimeStatus degraded;
    degraded.state = Proxy::ProxyState::Degraded;
    degraded.kernel = Kernel::Kind::Mihomo;

    degraded.localProxyReady = true;
    EXPECT_TRUE(degraded.localProxyActive());
    EXPECT_FALSE(degraded.configEditable());
    EXPECT_TRUE(degraded.kernelInUse(Kernel::Kind::Mihomo));

    degraded.localProxyReady = false;
    EXPECT_FALSE(degraded.localProxyActive());
    EXPECT_TRUE(degraded.configEditable());
    EXPECT_FALSE(degraded.kernelInUse(Kernel::Kind::Mihomo));
}

/// url-test 的生成层要一直有测试罩着（Rust 的 set_url_test_candidates_reaches_generated_config）：
/// UI 开关虽然撤了，代码路径还在，改动时不能悄悄坏掉。
TEST(ProxyManagerContracts, urlTestCandidatesReachGeneratedGroups)
{
    Proxy::MihomoConfigOptions options = stoppedOptions(9980, 9090);
    options.routingMode = Proxy::RoutingMode::Rule;
    options.urlTest = true;
    options.urlTestSize = 2;
    options.urlTestNodes = {Json{
        {"name", "cand-2"}, {"type", "vless"}, {"server", "9.9.9.9"}, {"port", 443},
        {"uuid", "22222222-2222-2222-2222-222222222222"}}};

    const Models::Node current = node(QStringLiteral("vless"), Json{
        {"server", "1.2.3.4"}, {"port", 443}, {"uuid", "11111111-1111-1111-1111-111111111111"}});

    const Result<Json> built = Proxy::buildMihomoConfig(current, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    const Json &config = built.value();

    EXPECT_EQ(config.at("proxies").size(), 2u);
    ASSERT_EQ(config.at("proxy-groups").size(), 3u);
    const Json groups = config.at("proxy-groups");
    EXPECT_EQ(groups.at(0).at("name"), "PROXY");
    // PROXY 的第一个成员必须是「自动选择」：默认让内核自己挑。
    EXPECT_EQ(groups.at(0).at("proxies").at(0), Proxy::kUrlTestGroup.toStdString());
    EXPECT_EQ(groups.at(1).at("name"), Proxy::kUrlTestGroup.toStdString());
    EXPECT_EQ(groups.at(1).at("type"), "url-test");
    EXPECT_EQ(groups.at(1).at("interval"), static_cast<int>(Proxy::kUrlTestIntervalSecs));
    EXPECT_EQ(groups.at(1).at("tolerance"), static_cast<int>(Proxy::kUrlTestToleranceMs));
    EXPECT_EQ(groups.at(1).at("timeout"), static_cast<int>(Proxy::kUrlTestTimeoutMs));
    EXPECT_EQ(groups.at(1).at("max-failed"), 3);
    EXPECT_EQ(groups.at(1).at("lazy"), true);
    EXPECT_EQ(groups.at(2).at("name"), Proxy::kFinalGroup.toStdString());

    // 兜底不能直接指 PROXY，要指「漏网之鱼」，用户才有地方改。
    const Json rules = config.at("rules");
    EXPECT_EQ(rules.at(rules.size() - 1),
              QStringLiteral("MATCH,%1").arg(Proxy::kFinalGroup).toStdString());
}

/// 候选不足时必须退回单节点组而不是生成一颗空的面板（Rust 里那条 warn 的同一路径）。
TEST(ProxyManagerContracts, singleCandidateFallsBackToPlainProxyGroup)
{
    Proxy::MihomoConfigOptions options = stoppedOptions(9980, 9090);
    options.urlTest = true;
    options.urlTestSize = 2;
    const Models::Node current = node(QStringLiteral("vless"), Json{
        {"server", "1.2.3.4"}, {"port", 443}, {"uuid", "11111111-1111-1111-1111-111111111111"}});

    const Result<Json> built = Proxy::buildMihomoConfig(current, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    EXPECT_EQ(built.value().at("proxy-groups").size(), 1u);
    const Json rules = built.value().at("rules");
    EXPECT_EQ(rules.at(rules.size() - 1), "MATCH,PROXY");
}

/// 界面上已经没有 url-test 开关，所以配置进 UI 状态的唯一入口必须把 true 归成 false；
/// 但生成层仍按传入值办事（两边不能混：归一化只发生在 UI 入口）。
TEST(ProxyManagerContracts, uiNeverSeesUrlTestEnabled)
{
    Proxy::MihomoConfigOptions options = stoppedOptions(9980, 9090);
    options.urlTest = true;
    options.urlTestSize = 7;

    const Proxy::MihomoConfigOptions forUi = Proxy::configForUi(options);
    EXPECT_FALSE(forUi.urlTest);
    EXPECT_EQ(forUi.urlTestSize, 7);      // 只归一化开关，别把大小也顺手改了
    EXPECT_TRUE(options.urlTest);         // 传进来的对象不动
}

/// GUI 把 proxy().start() 丢进 QtConcurrent 的线程池，而线程池的工作线程不跑事件循环。
/// QProcess 在这类线程上能不能真的把子进程拉起来，就是"点节点卡在启动中"的分水岭：
/// Rust 那边是 tokio::process（不需要每线程事件循环），所以这条必须在这边钉住。
TEST(ProxyManagerThreading, qprocessCanStartOnTheThreadTheGuiHandsWith)
{
    std::promise<bool> started;
    auto future = started.get_future();
    QtConcurrent::run([&started]() {
        QProcess probe;
        probe.setProgram(QStringLiteral("cmd.exe"));
        probe.setArguments({QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("0")});
        probe.start();
        started.set_value(probe.waitForStarted(3000));
    });
    ASSERT_EQ(future.wait_for(std::chrono::seconds(15)), std::future_status::ready);
    EXPECT_TRUE(future.get())
        << "线程池里没有事件循环：QProcess 起不了内核，点节点就会永远停在启动中";
}

int main(int argc, char *argv[])
{
    // verifyLocalProxy 走 QNetworkAccessManager，没有 QCoreApplication 实例会直接
    // ASSERT 在 applicationstatic.h。
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
