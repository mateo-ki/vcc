#include <gtest/gtest.h>

#include "proxy/Diagnostics.h"
#include "proxy/MihomoConfig.h"

using namespace Proxy;

TEST(ProxyDiagnostics, StripAnsiRemovesColorSequences)
{
    const QString raw = QString::fromLatin1("\x1b[31merror\x1b[0m plain");
    EXPECT_EQ(stripAnsi(raw).toStdString(), std::string("error plain"));
    // ESC 后面不是 '[' 时只吃掉 ESC 本身。
    EXPECT_EQ(stripAnsi(QString::fromLatin1("a\x1b" "b")).toStdString(), std::string("ab"));
}

TEST(ProxyDiagnostics, LastNonemptyLineFallsBackToUnknown)
{
    EXPECT_EQ(lastNonemptyLine(QStringLiteral("first\n  \nlast line\n")).toStdString(),
              std::string("last line"));
    EXPECT_TRUE(lastNonemptyLine(QStringLiteral("   \n\n")).contains(QChar(0x672A)));
}

TEST(ProxyDiagnostics, FormatKernelDiagnosticClassifiesRawErrors)
{
    EXPECT_EQ(formatKernelDiagnostic(QString::fromLatin1(
                  "time=... level=error msg=\"dial tcp 1.2.3.4:443: connectex: An existing "
                  "connection was forcibly closed by the remote host.\""))
                  .contains(QChar(0x8282)),
              true);
    EXPECT_TRUE(formatKernelDiagnostic(QStringLiteral("i/o timeout")).contains(QChar(0x8D85)));
    EXPECT_TRUE(formatKernelDiagnostic(QStringLiteral("connection refused"))
                    .contains(QChar(0x7EDD)));
    EXPECT_TRUE(formatKernelDiagnostic(QStringLiteral("tls: handshake failure"))
                    .contains(QLatin1String("TLS")));
    EXPECT_TRUE(formatKernelDiagnostic(QStringLiteral("websocket: bad path")).contains(
        QLatin1String("WebSocket")));
    // msg="..." 命中时直接给里面的内容。
    EXPECT_EQ(formatKernelDiagnostic(QString::fromLatin1("level=error msg=\"plain reason\""))
                  .toStdString(),
              std::string("plain reason"));
}

TEST(ProxyDiagnostics, SelfReferentialSnapshotIsResetToOff)
{
    // 上次会话残留的"就是我们自己端口"的系统代理，不能当成还原目标。
    SysProxy::Snapshot leftover;
    leftover.enabled = true;
    leftover.server = QStringLiteral("127.0.0.1:9980");
    leftover.overrideList = QStringLiteral("<local>;localhost");

    const SysProxy::Snapshot cleaned = sanitizeSelfReferential(leftover,
                                                               QStringLiteral("127.0.0.1:9980"));
    EXPECT_FALSE(cleaned.enabled);
    EXPECT_TRUE(cleaned.server.isNull());
    EXPECT_TRUE(cleaned.overrideList.isNull());

    // 指向别人家的端口要原样保留。
    const SysProxy::Snapshot other =
        sanitizeSelfReferential(leftover, QStringLiteral("127.0.0.1:7890"));
    EXPECT_TRUE(other.enabled);
    EXPECT_EQ(other.server.toStdString(), std::string("127.0.0.1:9980"));
}

TEST(ProxyRuntimeStatus, StoppedUsesDefaultPorts)
{
    const MihomoConfigOptions options;
    const ProxyRuntimeStatus status = ProxyRuntimeStatus::stopped(options);
    EXPECT_EQ(status.state, ProxyState::Stopped);
    EXPECT_EQ(status.mixedPort, 9980);
    EXPECT_EQ(status.controllerPort, 9090);
    EXPECT_FALSE(status.localProxyReady);
    EXPECT_FALSE(status.systemProxyEnabled);
    EXPECT_TRUE(status.lastError.isNull());
    EXPECT_TRUE(status.configEditable());
    EXPECT_FALSE(status.localProxyActive());
}

TEST(ProxyRuntimeStatus, DegradedDistinguishesReadyAndWaiting)
{
    MihomoConfigOptions options;
    ProxyRuntimeStatus status = ProxyRuntimeStatus::stopped(options);
    status.state = ProxyState::Degraded;
    status.localProxyReady = true;
    EXPECT_TRUE(status.configEditable() == false);
    EXPECT_TRUE(status.localProxyActive());

    status.localProxyReady = false;
    EXPECT_TRUE(status.configEditable());
    EXPECT_FALSE(status.localProxyActive());

    status.state = ProxyState::Running;
    status.localProxyReady = true;
    EXPECT_TRUE(status.kernelInUse(Kernel::Kind::Mihomo) == false);
    status.kernel = Kernel::Kind::Mihomo;
    EXPECT_TRUE(status.kernelInUse(Kernel::Kind::Mihomo));
    EXPECT_FALSE(status.kernelInUse(Kernel::Kind::Xray));
}

TEST(ProxyConnectivity, OnlyNodeFailuresAreClassifiedAsSuch)
{
    EXPECT_TRUE(isNodeConnectivityFailure(
        QStringLiteral("节点连通性验证失败: dial tcp 1.1.1.1:443 timeout")));
    EXPECT_FALSE(isNodeConnectivityFailure(QStringLiteral("Mihomo 启动后端口 9980 未就绪")));
}
