// 真实代理验证（内核泳道）的耗时取证工具。故意不进 ctest（和 vccNetProbeTests 同一套
// 做法）：它会真的起一颗 mihomo、并经由一个活着的出口代理发外网请求。
//
// 用法（Git Bash，在项目根目录下；MIHOMO_PATH 指到你本机那颗 mihomo）：
//   MIHOMO_PATH="$PWD/data/kernels/mihomo/mihomo.exe" \
//   VCC_PROBE_SOCKS=127.0.0.1:9980 VCC_PROBE_NODES=8 VCC_PROBE_BATCH=256 \
//   VCC_PROBE_ISOLATION=full ./build-ninja/vccSpeedtestProbeTests.exe > probe.log 2>&1
//
// 要回答的问题只有一个：整轮测速里唯一没有逐节点回调的那一段（内核冷启动 + `mihomo -t`
// 二分隔离 + 控制器 /delay）到底吃掉多少时间，以及时间住在哪一格。
#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QString>
#include <QStringList>
#include <QVector>
#include <iostream>

#include "core/JsonUtil.h"
#include "core/TimeUtil.h"
#include "kernel/KernelTypes.h"
#include "models/Node.h"
#include "proxy_probe/ProbeTypes.h"

namespace {

int envInt(const char *name, int fallback)
{
    const QByteArray raw = qgetenv(name);
    if (raw.isEmpty()) {
        return fallback;
    }
    bool ok = false;
    const int value = raw.toInt(&ok);
    return ok ? value : fallback;
}

QString envText(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

QVector<Models::Node> makeNodes(const QString &endpoint, int count, int badCount)
{
    const QStringList parts = endpoint.split(QLatin1Char(':'));
    QVector<Models::Node> nodes;
    for (int index = 0; index < count + badCount; ++index) {
        const bool bad = index >= count;
        Models::Node node;
        node.id = QStringLiteral("probe-%1").arg(index);
        node.sourceId = QString();
        node.protocol = bad ? QStringLiteral("ss") : QStringLiteral("socks5");
        node.name = node.id;
        node.server = parts.value(0);
        node.port = parts.value(1).toInt();
        node.raw = QStringLiteral("socks://%1").arg(endpoint);
        node.country = QString();
        Json config = Json::object();
        if (bad) {
            // 故意给一个 mihomo 不认的 cipher：配置层就非法 → 整批 `mihomo -t` 会挂，
            // 逼出二分隔离那条路径（全 good 的批次永远不会split，量不到它的成本）。
            config["type"] = "ss";
            config["server"] = parts.value(0).toStdString();
            config["port"] = parts.value(1).toInt();
            config["cipher"] = "bogus-cipher-not-real";
            config["password"] = "x";
            node.server = parts.value(0);
        } else {
            config["type"] = "socks5";
            config["server"] = parts.value(0).toStdString();
            config["port"] = parts.value(1).toInt();
        }
        node.config = config;
        node.latencyMs = std::nullopt;
        node.latencyOk = std::nullopt;
        node.downloadKbps = std::nullopt;
        node.latencyKernel = QString();
        node.latencyTestKind = QString();
        node.latencyTestedAt = TimeUtil::OptionalUtc::none();
        node.createdAt = TimeUtil::OptionalUtc::now();
        node.updatedAt = TimeUtil::OptionalUtc::now();
        nodes.append(node);
    }
    return nodes;
}

}

TEST(SpeedtestProbe, DISABLED_kernelLaneTiming)
{
    const QString endpoint = envText("VCC_PROBE_SOCKS");
    if (endpoint.isEmpty()) {
        std::cout << "VCC_PROBE_SOCKS 没设 —— 取证工具不跑（例如 127.0.0.1:9980）" << std::endl;
        GTEST_SKIP() << "no probe endpoint";
    }
    const int nodes = envInt("VCC_PROBE_NODES", 8);
    const int badNodes = envInt("VCC_PROBE_BAD", 0);
    const int timeoutMs = envInt("VCC_PROBE_TIMEOUT_MS", 2500);
    const size_t batchSize = size_t(envInt("VCC_PROBE_BATCH", 256));
    // full = 默认那套（先对整批跑 `mihomo -t` 二分隔离）；conditional = 先直接测，
    // 只把失败的送去隔离。两者的差就是"隔离那一轮"的成本。
    const bool conditional = envText("VCC_PROBE_ISOLATION") == QStringLiteral("conditional");

    const QVector<Models::Node> list = makeNodes(endpoint, nodes, badNodes);
    QSemaphore networkLimit(512);
    QElapsedTimer clock;
    clock.start();
    qint64 firstTickMs = -1;
    int ticks = 0;
    const ProxyProbe::ProgressFn progress = [&](const ProxyProbe::ProbeProgress &state) {
        if (firstTickMs < 0) {
            firstTickMs = clock.elapsed();
        }
        ++ticks;
        std::cout << "tick elapsed_ms=" << state.elapsedMs << " wall_ms=" << clock.elapsed()
                  << " done=" << state.done << "/" << state.total << " ok=" << state.ok
                  << " batch=" << (state.batchIndex ? qint64(*state.batchIndex) : -1) << "/"
                  << (state.batchTotal ? qint64(*state.batchTotal) : -1) << std::endl;
    };

    const Result<QVector<ProxyProbe::ProbeResult>> outcome = ProxyProbe::verifyNodesWithMihomo(
        list, timeoutMs, progress, networkLimit, batchSize, conditional);
    const qint64 totalMs = clock.elapsed();

    std::cout << "CONFIG endpoint=" << endpoint.toStdString() << " good=" << nodes
              << " bad=" << badNodes << " timeout_ms=" << timeoutMs << " batch=" << batchSize
              << " isolation=" << (conditional ? "conditional" : "full")
              << " mihomo=" << qgetenv("MIHOMO_PATH").toStdString() << std::endl;
    if (outcome.isErr()) {
        std::cout << "RESULT error: " << outcome.error().toStdString() << std::endl;
        std::cout << "TOTAL wall_ms=" << totalMs << " ticks=" << ticks << std::endl;
        // 泳道报错会把整轮测速中止 —— 这条本身就是答案，别让它静默通过。
        ADD_FAILURE() << outcome.error().toStdString();
        return;
    }
    int ok = 0;
    for (const ProxyProbe::ProbeResult &result : outcome.value()) {
        if (result.ok) {
            ++ok;
        } else {
            std::cout << "FAIL id=" << result.id.toStdString()
                      << " msg=" << result.message.toStdString() << std::endl;
        }
    }
    std::cout << "RESULT ok=" << ok << "/" << outcome.value().size() << " ticks=" << ticks
              << " first_tick_ms=" << firstTickMs << " total_wall_ms=" << totalMs << std::endl;
    // 取证用，不做阈值断言：数字打出来就行，判红留给有判据的守卫。
}

int main(int argc, char *argv[])
{
    // Net::send 需要 app 实例（局部事件循环），但它不需要 GUI —— QCoreApplication 就够。
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
