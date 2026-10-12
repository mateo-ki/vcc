// 由 tools/gen-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。
// 这些提示文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）。
#include "gui/UiText.h"

#include <initializer_list>

namespace Gui::UiText {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

// brandSubtitle <- gui/views/sidebar.rs:-1
// ASCII 骨架: ""
QString brandSubtitle()
{
    return fromCodePoints({
        0x7F51, 0x7EDC, 0x4EE3, 0x7406, 0x63A7, 0x5236, 0x53F0,
    });
}

// navConsole <- gui/views/sidebar.rs:-1
// ASCII 骨架: ""
QString navConsole()
{
    return fromCodePoints({
        0x63A7, 0x5236, 0x53F0,
    });
}

// navNodePool <- gui/views/sidebar.rs:-1
// ASCII 骨架: ""
QString navNodePool()
{
    return fromCodePoints({
        0x8282, 0x70B9, 0x6C60,
    });
}

// navSubscriptions <- gui/views/sidebar.rs:-1
// ASCII 骨架: ""
QString navSubscriptions()
{
    return fromCodePoints({
        0x8BA2, 0x9605, 0x6E90,
    });
}

// navSystemSettings <- gui/views/sidebar.rs:-1
// ASCII 骨架: ""
QString navSystemSettings()
{
    return fromCodePoints({
        0x7CFB, 0x7EDF, 0x8BBE, 0x7F6E,
    });
}

// navExternalApi <- gui/views/sidebar.rs:-1
// ASCII 骨架: ""
QString navExternalApi()
{
    return fromCodePoints({
        0x5916, 0x90E8, 0x63A5, 0x53E3,
    });
}

// panelNodes <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString panelNodes()
{
    return fromCodePoints({
        0x53EF, 0x7528, 0x8282, 0x70B9,
    });
}

// panelConnections <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString panelConnections()
{
    return fromCodePoints({
        0x6D3B, 0x8DC3, 0x8FDE, 0x63A5,
    });
}

// panelConnectionsCounted <- gui/views/proxy.rs:-1 ({} -> %n)
// ASCII 骨架: " (%1)"
QString panelConnectionsCounted()
{
    return fromCodePoints({
        0x6D3B, 0x8DC3, 0x8FDE, 0x63A5, 0x0020, 0x0028, 0x0025, 0x0031,
        0x0029,
    });
}

// healthStripHover <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: " 24 %1   %2   %3 "
QString healthStripHover()
{
    return fromCodePoints({
        0x6700, 0x8FD1, 0x0020, 0x0032, 0x0034, 0x0020, 0x5C0F, 0x65F6,
        0x51FA, 0x53E3, 0x5B9E, 0x6D4B, 0xFF1A, 0x0025, 0x0031, 0x0020,
        0x5C0F, 0x65F6, 0x6B63, 0x5E38, 0x0020, 0x00B7, 0x0020, 0x0025,
        0x0032, 0x0020, 0x5C0F, 0x65F6, 0x6709, 0x4E0D, 0x901A, 0x0020,
        0x00B7, 0x0020, 0x0025, 0x0033, 0x0020, 0x5C0F, 0x65F6, 0x6CA1,
        0x6709, 0x5B9E, 0x6D4B,
    });
}

// switchLogLink <- gui/views/proxy.rs:-1 ({} -> %n)
// ASCII 骨架: " %1 "
QString switchLogLink()
{
    return fromCodePoints({
        0x67E5, 0x770B, 0x5207, 0x6362, 0x8BB0, 0x5F55, 0xFF08, 0x4ECA,
        0x65E5, 0x0020, 0x0025, 0x0031, 0x0020, 0x6B21, 0xFF09,
    });
}

// switchLogTitle <- gui/mod.rs:-1
// ASCII 骨架: ""
QString switchLogTitle()
{
    return fromCodePoints({
        0x51FA, 0x53E3, 0x5207, 0x6362, 0x8BB0, 0x5F55,
    });
}

// switchLogSummary <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: " %1    %2    %3 "
QString switchLogSummary()
{
    return fromCodePoints({
        0x5171, 0x0020, 0x0025, 0x0031, 0x0020, 0x6761, 0x0020, 0x00B7,
        0x0020, 0x4ECA, 0x5929, 0x0020, 0x0025, 0x0032, 0x0020, 0x6B21,
        0x0020, 0x00B7, 0x0020, 0x4FDD, 0x7559, 0x0020, 0x0025, 0x0033,
        0x0020, 0x5929, 0xFF08, 0x91CD, 0x542F, 0x4E0D, 0x4E22, 0xFF09,
    });
}

// switchLogEmpty <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: ""
QString switchLogEmpty()
{
    return fromCodePoints({
        0x8FD8, 0x6CA1, 0x6709, 0x5207, 0x6362, 0x8BB0, 0x5F55, 0x3002,
        0x624B, 0x52A8, 0x6362, 0x51FA, 0x53E3, 0x3001, 0x81EA, 0x52A8,
        0x5207, 0x6362, 0x3001, 0x5207, 0x6362, 0x5931, 0x8D25, 0x90FD,
        0x4F1A, 0x8BB0, 0x5728, 0x8FD9, 0x91CC, 0x3002,
    });
}

// switchLogHidden <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: " %1 "
QString switchLogHidden()
{
    return fromCodePoints({
        0x66F4, 0x65E9, 0x7684, 0x0020, 0x0025, 0x0031, 0x0020, 0x6761,
        0x672A, 0x5728, 0x6B64, 0x663E, 0x793A, 0x3002,
    });
}

// switchLogTodayDay <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "%1  "
QString switchLogTodayDay()
{
    return fromCodePoints({
        0x0025, 0x0031, 0x0020, 0x00B7, 0x0020, 0x4ECA, 0x5929,
    });
}

// switchLogNoSwitch <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: ""
QString switchLogNoSwitch()
{
    return fromCodePoints({
        0x6CA1, 0x5207, 0x6210,
    });
}

// failoverSearching <- gui/mod.rs:-1
// ASCII 骨架: ""
QString failoverSearching()
{
    return fromCodePoints({
        0x66FF, 0x8865, 0x590D, 0x68C0, 0x8FD8, 0x5728, 0x8FDB, 0x884C,
        0x4E2D, 0xFF0C, 0x8FD9, 0x4E00, 0x8F6E, 0x7684, 0x5224, 0x5B9A,
        0x5148, 0x4E0D, 0x91CD, 0x590D, 0x89E6, 0x53D1,
    });
}

// failoverNoSecondCandidate <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "%1..."
QString failoverNoSecondCandidate()
{
    return fromCodePoints({
        0x0025, 0x0031, 0xFF0C, 0x63A7, 0x5236, 0x53F0, 0x5019, 0x9009,
        0x91CC, 0x6CA1, 0x6709, 0x7B2C, 0x4E8C, 0x4E2A, 0x53EF, 0x7528,
        0x8282, 0x70B9, 0xFF0C, 0x6539, 0x4ECE, 0x5E93, 0x91CC, 0x6700,
        0x65B0, 0x6D4B, 0x901A, 0x7684, 0x91CC, 0x73B0, 0x67E5, 0x002E,
        0x002E, 0x002E,
    });
}

// failoverTcpRecheck <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "%1 %2  TCP ..."
QString failoverTcpRecheck()
{
    return fromCodePoints({
        0x0025, 0x0031, 0xFF0C, 0x6B63, 0x5728, 0x5BF9, 0x0020, 0x0025,
        0x0032, 0x0020, 0x4E2A, 0x66FF, 0x8865, 0x8282, 0x70B9, 0x505A,
        0x0020, 0x0054, 0x0043, 0x0050, 0x0020, 0x590D, 0x68C0, 0x002E,
        0x002E, 0x002E,
    });
}

// failoverAllUnreachable <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "TCP  %1 "
QString failoverAllUnreachable()
{
    return fromCodePoints({
        0x0054, 0x0043, 0x0050, 0x0020, 0x590D, 0x68C0, 0x8FC7, 0x0020,
        0x0025, 0x0031, 0x0020, 0x4E2A, 0x5019, 0x9009, 0xFF0C, 0x6CA1,
        0x6709, 0x4E00, 0x4E2A, 0x7AEF, 0x53E3, 0x8FDE, 0x5F97, 0x4E0A,
    });
}

// failoverNoneVerified <- gui/mod.rs:-1
// ASCII 骨架: ""
QString failoverNoneVerified()
{
    return fromCodePoints({
        0x5E93, 0x91CC, 0x4E5F, 0x627E, 0x4E0D, 0x51FA, 0x7B2C, 0x4E8C,
        0x4E2A, 0x6D4B, 0x901A, 0x8FC7, 0x7684, 0x8282, 0x70B9,
    });
}

// failoverSwitchedTo <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "   %1   %2 "
QString failoverSwitchedTo()
{
    return fromCodePoints({
        0x590D, 0x68C0, 0x901A, 0x8FC7, 0x0020, 0x2192, 0x0020, 0x5DF2,
        0x5207, 0x5230, 0x0020, 0x0025, 0x0031, 0x0020, 0x00B7, 0x0020,
        0x672C, 0x6B21, 0x8FD0, 0x884C, 0x7B2C, 0x0020, 0x0025, 0x0032,
        0x0020, 0x6B21,
    });
}

// failoverButReason <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "%1"
QString failoverButReason()
{
    return fromCodePoints({
        0x5224, 0x5B9A, 0x8BE5, 0x6362, 0x51FA, 0x53E3, 0xFF0C, 0x4F46,
        0x0025, 0x0031,
    });
}

// failoverMissPrefix <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "%1"
QString failoverMissPrefix()
{
    return fromCodePoints({
        0x6CA1, 0x6709, 0x53EF, 0x7528, 0x66FF, 0x8865, 0xFF1A, 0x0025,
        0x0031,
    });
}

// failoverBreakerBlacklisted <- gui/mod.rs:-1
// ASCII 骨架: ""
QString failoverBreakerBlacklisted()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x8282, 0x70B9, 0x5DF2, 0x88AB, 0x65AD, 0x8DEF,
        0x5668, 0x62C9, 0x9ED1,
    });
}

// failoverSeekingBackup <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: "%1 %2"
QString failoverSeekingBackup()
{
    return fromCodePoints({
        0x0025, 0x0031, 0x0020, 0x0025, 0x0032, 0xFF0C, 0x6B63, 0x5728,
        0x5BFB, 0x627E, 0x66FF, 0x8865, 0x8282, 0x70B9,
    });
}

// switchReasonAuto <- gui/mod.rs:-1
// ASCII 骨架: ""
QString switchReasonAuto()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x5207, 0x6362,
    });
}

// elevatedRequested <- gui/mod.rs:-1
// ASCII 骨架: "..."
QString elevatedRequested()
{
    return fromCodePoints({
        0x5DF2, 0x8BF7, 0x6C42, 0x7BA1, 0x7406, 0x5458, 0x6743, 0x9650,
        0xFF0C, 0x6B63, 0x5728, 0x5207, 0x6362, 0x5230, 0x63D0, 0x6743,
        0x7A97, 0x53E3, 0x002E, 0x002E, 0x002E,
    });
}

// elevatedRequestFailed <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: ": %1"
QString elevatedRequestFailed()
{
    return fromCodePoints({
        0x8BF7, 0x6C42, 0x7BA1, 0x7406, 0x5458, 0x6743, 0x9650, 0x5931,
        0x8D25, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// elevatedConfigSerializeFailed <- gui/mod.rs:-1 ({} -> %n)
// ASCII 骨架: " TUN : %1"
QString elevatedConfigSerializeFailed()
{
    return fromCodePoints({
        0x5E8F, 0x5217, 0x5316, 0x0020, 0x0054, 0x0055, 0x004E, 0x0020,
        0x914D, 0x7F6E, 0x5931, 0x8D25, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// sparklineWaiting <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString sparklineWaiting()
{
    return fromCodePoints({
        0x7B49, 0x5F85, 0x6D41, 0x91CF, 0x7EDF, 0x8BA1, 0x2026,
    });
}

// sparklineIdle <- gui/widgets.rs:-1
// ASCII 骨架: "  "
QString sparklineIdle()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x672A, 0x8FD0, 0x884C, 0x0020, 0x00B7, 0x0020,
        0x542F, 0x52A8, 0x540E, 0x8FD9, 0x91CC, 0x753B, 0x5B9E, 0x65F6,
        0x4E0A, 0x4E0B, 0x884C,
    });
}

// sparklineHover <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "%1    %2   %3"
QString sparklineHover()
{
    return fromCodePoints({
        0x0025, 0x0031, 0x0020, 0x79D2, 0x524D, 0x0020, 0x00B7, 0x0020,
        0x2193, 0x0020, 0x0025, 0x0032, 0x0020, 0x00B7, 0x0020, 0x2191,
        0x0020, 0x0025, 0x0033,
    });
}

// panelRules <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString panelRules()
{
    return fromCodePoints({
        0x5206, 0x6D41, 0x89C4, 0x5219,
    });
}

// panelNetwork <- gui/views/proxy.rs:-1
// ASCII 骨架: "DNS / TUN"
QString panelNetwork()
{
    return QStringLiteral("DNS / TUN");
}

// panelKernels <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString panelKernels()
{
    return fromCodePoints({
        0x5185, 0x6838, 0x7BA1, 0x7406,
    });
}

// panelLogs <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString panelLogs()
{
    return fromCodePoints({
        0x8FD0, 0x884C, 0x65E5, 0x5FD7,
    });
}

// stateStopped <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString stateStopped()
{
    return fromCodePoints({
        0x5DF2, 0x505C, 0x6B62,
    });
}

// stateStarting <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString stateStarting()
{
    return fromCodePoints({
        0x542F, 0x52A8, 0x4E2D,
    });
}

// stateRunning <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString stateRunning()
{
    return fromCodePoints({
        0x8FD0, 0x884C, 0x4E2D,
    });
}

// stateDegraded <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString stateDegraded()
{
    return fromCodePoints({
        0x964D, 0x7EA7,
    });
}

// stateStopping <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString stateStopping()
{
    return fromCodePoints({
        0x505C, 0x6B62, 0x4E2D,
    });
}

// stateFailed <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString stateFailed()
{
    return fromCodePoints({
        0x5931, 0x8D25,
    });
}

// preparedFailed <- gui/views/proxy.rs:-1 ({} -> %n)
// ASCII 骨架: "   %1/%2"
QString preparedFailed()
{
    return fromCodePoints({
        0x9884, 0x5907, 0x0020, 0x00B7, 0x0020, 0x4E0B, 0x8F7D, 0x5931,
        0x8D25, 0x0020, 0x0025, 0x0031, 0x002F, 0x0025, 0x0032,
    });
}

// preparedDownloading <- gui/views/proxy.rs:-1 ({} -> %n)
// ASCII 骨架: "   (%1/%2)"
QString preparedDownloading()
{
    return fromCodePoints({
        0x9884, 0x5907, 0x0020, 0x00B7, 0x0020, 0x6B63, 0x5728, 0x4E0B,
        0x8F7D, 0x5185, 0x6838, 0x0020, 0x0028, 0x0025, 0x0031, 0x002F,
        0x0025, 0x0032, 0x0029,
    });
}

// prepared <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString prepared()
{
    return fromCodePoints({
        0x9884, 0x5907,
    });
}

// regionLabel <- gui/views/proxy.rs:-1
// ASCII 骨架: ":"
QString regionLabel()
{
    return fromCodePoints({
        0x5730, 0x533A, 0x003A,
    });
}

// regionAll <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionAll()
{
    return fromCodePoints({
        0x5168, 0x90E8,
    });
}

// regionHK <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionHK()
{
    return fromCodePoints({
        0x9999, 0x6E2F,
    });
}

// regionJP <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionJP()
{
    return fromCodePoints({
        0x65E5, 0x672C,
    });
}

// regionUS <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionUS()
{
    return fromCodePoints({
        0x7F8E, 0x56FD,
    });
}

// regionSG <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionSG()
{
    return fromCodePoints({
        0x65B0, 0x52A0, 0x5761,
    });
}

// regionTW <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionTW()
{
    return fromCodePoints({
        0x53F0, 0x6E7E,
    });
}

// regionKR <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionKR()
{
    return fromCodePoints({
        0x97E9, 0x56FD,
    });
}

// regionOther <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString regionOther()
{
    return fromCodePoints({
        0x5176, 0x5B83,
    });
}

// sortLabel <- gui/views/proxy.rs:-1
// ASCII 骨架: ":"
QString sortLabel()
{
    return fromCodePoints({
        0x6392, 0x5E8F, 0x003A,
    });
}

// sortByLatency <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString sortByLatency()
{
    return fromCodePoints({
        0x5EF6, 0x8FDF,
    });
}

// sortByName <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString sortByName()
{
    return fromCodePoints({
        0x540D, 0x79F0,
    });
}

// nodeCountTotal <- gui/views/proxy.rs:-1
// ASCII 骨架: " {} "
QString nodeCountTotal()
{
    return fromCodePoints({
        0x5171, 0x0020, 0x007B, 0x007D, 0x0020, 0x4E2A, 0x8282, 0x70B9,
    });
}

// searchHint <- gui/views/proxy.rs:-1
// ASCII 骨架: "..."
QString searchHint()
{
    return fromCodePoints({
        0x641C, 0x7D22, 0x8282, 0x70B9, 0x002E, 0x002E, 0x002E,
    });
}

// menuDetails <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString menuDetails()
{
    return fromCodePoints({
        0x67E5, 0x770B, 0x8BE6, 0x60C5,
    });
}

// menuSpeedtest <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString menuSpeedtest()
{
    return fromCodePoints({
        0x6D4B, 0x901F, 0x8282, 0x70B9,
    });
}

// menuIpRisk <- gui/views/proxy.rs:-1
// ASCII 骨架: "IP "
QString menuIpRisk()
{
    return fromCodePoints({
        0x0049, 0x0050, 0x0020, 0x68C0, 0x6D4B,
    });
}

// menuLockEgress <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString menuLockEgress()
{
    return fromCodePoints({
        0x9501, 0x5B9A, 0x4E3A, 0x51FA, 0x53E3, 0xFF08, 0x5173, 0x95ED,
        0x81EA, 0x52A8, 0x9009, 0x8DEF, 0xFF09,
    });
}

// switchReasonManual <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString switchReasonManual()
{
    return fromCodePoints({
        0x624B, 0x52A8, 0x5207, 0x6362,
    });
}

// latencyProxyFailed <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString latencyProxyFailed()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5931, 0x8D25,
    });
}

// latencyTcpFailed <- gui/widgets.rs:-1
// ASCII 骨架: "TCP  "
QString latencyTcpFailed()
{
    return fromCodePoints({
        0x0054, 0x0043, 0x0050, 0x5931, 0x8D25, 0x0020, 0x00B7, 0x0020,
        0x672A, 0x9A8C, 0x8BC1,
    });
}

// latencyUntested <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString latencyUntested()
{
    return fromCodePoints({
        0x672A, 0x6D4B, 0x901F,
    });
}

// kernelNameFallback <- gui/widgets.rs:-1
// ASCII 骨架: ""
QString kernelNameFallback()
{
    return fromCodePoints({
        0x4EE3, 0x7406,
    });
}

// switchingLabel <- gui/widgets.rs:-1
// ASCII 骨架: "..."
QString switchingLabel()
{
    return fromCodePoints({
        0x5207, 0x6362, 0x4E2D, 0x002E, 0x002E, 0x002E,
    });
}

// emptyNodeGrid <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString emptyNodeGrid()
{
    return fromCodePoints({
        0x6CA1, 0x6709, 0x5339, 0x914D, 0x7684, 0x53EF, 0x7528, 0x8282,
        0x70B9, 0xFF0C, 0x8BF7, 0x5728, 0x300C, 0x8282, 0x70B9, 0x5217,
        0x8868, 0x300D, 0x4E2D, 0x8FDB, 0x884C, 0x6D4B, 0x901F,
    });
}

// egressLockedStatus <- gui/views/proxy.rs:-1
// ASCII 骨架: " {name}"
QString egressLockedStatus()
{
    return fromCodePoints({
        0x5DF2, 0x5173, 0x95ED, 0x81EA, 0x52A8, 0x9009, 0x8DEF, 0xFF0C,
        0x51FA, 0x53E3, 0x9501, 0x5B9A, 0x4E3A, 0x0020, 0x007B, 0x006E,
        0x0061, 0x006D, 0x0065, 0x007D,
    });
}

// kernelSectionTitle <- gui/views/proxy.rs:1707
// ASCII 骨架: ""
QString kernelSectionTitle()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5185, 0x6838,
    });
}

// kernelSectionHint <- gui/views/proxy.rs:-1
// ASCII 骨架: " v2rayN"
QString kernelSectionHint()
{
    return fromCodePoints({
        0x5E94, 0x7528, 0x72EC, 0x7ACB, 0x4E0B, 0x8F7D, 0x3001, 0x6821,
        0x9A8C, 0x3001, 0x5907, 0x4EFD, 0x548C, 0x66F4, 0x65B0, 0xFF0C,
        0x4E0D, 0x4F9D, 0x8D56, 0x0020, 0x0076, 0x0032, 0x0072, 0x0061,
        0x0079, 0x004E,
    });
}

// kernelStorageHint <- gui/views/proxy.rs:-1
// ASCII 骨架: " data/kernels   GitHub Release"
QString kernelStorageHint()
{
    return fromCodePoints({
        0x5B58, 0x50A8, 0x76EE, 0x5F55, 0x0020, 0x0064, 0x0061, 0x0074,
        0x0061, 0x002F, 0x006B, 0x0065, 0x0072, 0x006E, 0x0065, 0x006C,
        0x0073, 0x0020, 0x00B7, 0x0020, 0x6765, 0x6E90, 0x4E3A, 0x5B98,
        0x65B9, 0x0020, 0x0047, 0x0069, 0x0074, 0x0048, 0x0075, 0x0062,
        0x0020, 0x0052, 0x0065, 0x006C, 0x0065, 0x0061, 0x0073, 0x0065,
    });
}

// kernelCheckUpdates <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString kernelCheckUpdates()
{
    return fromCodePoints({
        0x68C0, 0x67E5, 0x66F4, 0x65B0,
    });
}

// kernelCheckingStatus <- gui/views/proxy.rs:1721
// ASCII 骨架: "..."
QString kernelCheckingStatus()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x68C0, 0x67E5, 0x5B98, 0x65B9, 0x5185, 0x6838,
        0x7248, 0x672C, 0x002E, 0x002E, 0x002E,
    });
}

// kernelCancelDownload <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString kernelCancelDownload()
{
    return fromCodePoints({
        0x53D6, 0x6D88, 0x4E0B, 0x8F7D,
    });
}

// kernelCancellingStatus <- gui/views/proxy.rs:1725
// ASCII 骨架: "..."
QString kernelCancellingStatus()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x53D6, 0x6D88, 0x5185, 0x6838, 0x4E0B, 0x8F7D,
        0x002E, 0x002E, 0x002E,
    });
}

// kernelColumnKind <- gui/views/proxy.rs:1773
// ASCII 骨架: ""
QString kernelColumnKind()
{
    return fromCodePoints({
        0x5185, 0x6838,
    });
}

// kernelColumnStatus <- gui/views/proxy.rs:1777
// ASCII 骨架: ""
QString kernelColumnStatus()
{
    return fromCodePoints({
        0x72B6, 0x6001,
    });
}

// kernelColumnVersion <- gui/views/proxy.rs:1781
// ASCII 骨架: ""
QString kernelColumnVersion()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x7248, 0x672C,
    });
}

// kernelColumnLatest <- gui/views/proxy.rs:1785
// ASCII 骨架: ""
QString kernelColumnLatest()
{
    return fromCodePoints({
        0x6700, 0x65B0, 0x7248, 0x672C,
    });
}

// kernelColumnPath <- gui/views/proxy.rs:1789
// ASCII 骨架: ""
QString kernelColumnPath()
{
    return fromCodePoints({
        0x8DEF, 0x5F84,
    });
}

// kernelColumnAction <- gui/views/proxy.rs:1793
// ASCII 骨架: ""
QString kernelColumnAction()
{
    return fromCodePoints({
        0x64CD, 0x4F5C,
    });
}

// kernelStateAvailable <- gui/views/proxy.rs:1818
// ASCII 骨架: ""
QString kernelStateAvailable()
{
    return fromCodePoints({
        0x53EF, 0x7528,
    });
}

// kernelStateMissing <- gui/views/proxy.rs:1820
// ASCII 骨架: ""
QString kernelStateMissing()
{
    return fromCodePoints({
        0x672A, 0x627E, 0x5230,
    });
}

// kernelLatestChecking <- gui/views/proxy.rs:1802
// ASCII 骨架: ""
QString kernelLatestChecking()
{
    return fromCodePoints({
        0x68C0, 0x67E5, 0x4E2D,
    });
}

// kernelActionUpdate <- gui/views/proxy.rs:1847
// ASCII 骨架: ""
QString kernelActionUpdate()
{
    return fromCodePoints({
        0x66F4, 0x65B0,
    });
}

// kernelActionDownload <- gui/views/proxy.rs:1847
// ASCII 骨架: ""
QString kernelActionDownload()
{
    return fromCodePoints({
        0x4E0B, 0x8F7D,
    });
}

// kernelHoverInUse <- gui/views/proxy.rs:1860
// ASCII 骨架: ""
QString kernelHoverInUse()
{
    return fromCodePoints({
        0x505C, 0x6B62, 0x5F53, 0x524D, 0x4EE3, 0x7406, 0x540E, 0x624D,
        0x80FD, 0x66F4, 0x65B0, 0x8FD9, 0x4E2A, 0x5185, 0x6838,
    });
}

// kernelHoverAsset <- gui/views/proxy.rs:-1
// ASCII 骨架: " SHA-256 "
QString kernelHoverAsset()
{
    return fromCodePoints({
        0x4E0B, 0x8F7D, 0x5B98, 0x65B9, 0x8D44, 0x4EA7, 0xFF0C, 0x6821,
        0x9A8C, 0x0020, 0x0053, 0x0048, 0x0041, 0x002D, 0x0032, 0x0035,
        0x0036, 0x0020, 0x540E, 0x5907, 0x4EFD, 0x66FF, 0x6362,
    });
}

// kernelProgressFormat <- gui/views/proxy.rs:1744
// ASCII 骨架: "{}  {}  {}"
QString kernelProgressFormat()
{
    return fromCodePoints({
        0x007B, 0x007D, 0x0020, 0x00B7, 0x0020, 0x007B, 0x007D, 0x0020,
        0x00B7, 0x0020, 0x007B, 0x007D,
    });
}

// kernelDownloadFormat <- gui/views/proxy.rs:1731
// ASCII 骨架: "{} / {}  {}/s"
QString kernelDownloadFormat()
{
    return fromCodePoints({
        0x007B, 0x007D, 0x0020, 0x002F, 0x0020, 0x007B, 0x007D, 0x0020,
        0x00B7, 0x0020, 0x007B, 0x007D, 0x002F, 0x0073,
    });
}

// routingModeDescRule <- gui/widgets.rs:592
// ASCII 骨架: ""
QString routingModeDescRule()
{
    return fromCodePoints({
        0x6309, 0x672C, 0x5730, 0x81EA, 0x5B9A, 0x4E49, 0x89C4, 0x5219,
        0x4F9D, 0x6B21, 0x5339, 0x914D,
    });
}

// routingModeDescChinaDirect <- gui/widgets.rs:593
// ASCII 骨架: " Geo     "
QString routingModeDescChinaDirect()
{
    return fromCodePoints({
        0x5728, 0x7EBF, 0x0020, 0x0047, 0x0065, 0x006F, 0x0020, 0x89C4,
        0x5219, 0x0020, 0x00B7, 0x0020, 0x56FD, 0x5185, 0x76F4, 0x8FDE,
        0x0020, 0x00B7, 0x0020, 0x5176, 0x4F59, 0x4EE3, 0x7406,
    });
}

// routingModeDescGfwList <- gui/widgets.rs:594
// ASCII 骨架: " GFW     "
QString routingModeDescGfwList()
{
    return fromCodePoints({
        0x5728, 0x7EBF, 0x0020, 0x0047, 0x0046, 0x0057, 0x0020, 0x89C4,
        0x5219, 0x0020, 0x00B7, 0x0020, 0x547D, 0x4E2D, 0x4EE3, 0x7406,
        0x0020, 0x00B7, 0x0020, 0x5176, 0x4F59, 0x76F4, 0x8FDE,
    });
}

// routingModeDescGlobal <- gui/widgets.rs:595
// ASCII 骨架: ""
QString routingModeDescGlobal()
{
    return fromCodePoints({
        0x6240, 0x6709, 0x6D41, 0x91CF, 0x4F7F, 0x7528, 0x5F53, 0x524D,
        0x4EE3, 0x7406, 0x8282, 0x70B9,
    });
}

// routingModeDescDirect <- gui/widgets.rs:596
// ASCII 骨架: ""
QString routingModeDescDirect()
{
    return fromCodePoints({
        0x6240, 0x6709, 0x6D41, 0x91CF, 0x7ED5, 0x8FC7, 0x4EE3, 0x7406,
        0x76F4, 0x63A5, 0x8FDE, 0x63A5,
    });
}

// routingModeDetailRule <- gui/widgets.rs:602
// ASCII 骨架: ""
QString routingModeDetailRule()
{
    return fromCodePoints({
        0x4FDD, 0x7559, 0x5E76, 0x4F7F, 0x7528, 0x4E0B, 0x65B9, 0x6709,
        0x5E8F, 0x89C4, 0x5219, 0xFF1B, 0x6CA1, 0x6709, 0x547D, 0x4E2D,
        0x7684, 0x6D41, 0x91CF, 0x4F7F, 0x7528, 0x4EE3, 0x7406, 0x3002,
    });
}

// routingModeDetailChinaDirect <- gui/widgets.rs:604
// ASCII 骨架: "GeoSite CN  GeoIP CN "
QString routingModeDetailChinaDirect()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x4F7F, 0x7528, 0x5E7F, 0x544A, 0x3001, 0x0047,
        0x0065, 0x006F, 0x0053, 0x0069, 0x0074, 0x0065, 0x0020, 0x0043,
        0x004E, 0x0020, 0x4E0E, 0x0020, 0x0047, 0x0065, 0x006F, 0x0049,
        0x0050, 0x0020, 0x0043, 0x004E, 0x0020, 0x89C4, 0x5219, 0xFF1B,
        0x5728, 0x7EBF, 0x6570, 0x636E, 0x7531, 0x5BF9, 0x5E94, 0x5185,
        0x6838, 0x66F4, 0x65B0, 0x3002,
    });
}

// routingModeDetailGfwList <- gui/widgets.rs:606
// ASCII 骨架: " GFW "
QString routingModeDetailGfwList()
{
    return fromCodePoints({
        0x4EC5, 0x0020, 0x0047, 0x0046, 0x0057, 0x0020, 0x89C4, 0x5219,
        0x96C6, 0x547D, 0x4E2D, 0x7684, 0x7AD9, 0x70B9, 0x4F7F, 0x7528,
        0x4EE3, 0x7406, 0xFF0C, 0x5176, 0x4ED6, 0x6D41, 0x91CF, 0x76F4,
        0x63A5, 0x8FDE, 0x63A5, 0x3002,
    });
}

// routingModeDetailGlobal <- gui/widgets.rs:607
// ASCII 骨架: ""
QString routingModeDetailGlobal()
{
    return fromCodePoints({
        0x5FFD, 0x7565, 0x81EA, 0x5B9A, 0x4E49, 0x89C4, 0x5219, 0xFF0C,
        0x6240, 0x6709, 0x53EF, 0x4EE3, 0x7406, 0x6D41, 0x91CF, 0x90FD,
        0x4F7F, 0x7528, 0x5F53, 0x524D, 0x8282, 0x70B9, 0x3002,
    });
}

// routingModeDetailDirect <- gui/widgets.rs:608
// ASCII 骨架: ""
QString routingModeDetailDirect()
{
    return fromCodePoints({
        0x5FFD, 0x7565, 0x81EA, 0x5B9A, 0x4E49, 0x89C4, 0x5219, 0xFF0C,
        0x4EE3, 0x7406, 0x5185, 0x6838, 0x4FDD, 0x6301, 0x8FD0, 0x884C,
        0x4F46, 0x6D41, 0x91CF, 0x5168, 0x90E8, 0x76F4, 0x8FDE, 0x3002,
    });
}

// ruleActionDirect <- gui/widgets.rs:696
// ASCII 骨架: ""
QString ruleActionDirect()
{
    return fromCodePoints({
        0x76F4, 0x8FDE,
    });
}

// ruleActionProxy <- gui/widgets.rs:697
// ASCII 骨架: ""
QString ruleActionProxy()
{
    return fromCodePoints({
        0x4EE3, 0x7406,
    });
}

// ruleActionReject <- gui/widgets.rs:698
// ASCII 骨架: ""
QString ruleActionReject()
{
    return fromCodePoints({
        0x62D2, 0x7EDD,
    });
}

// matcherLabelDomain <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "DOMAIN  %1"
QString matcherLabelDomain()
{
    return fromCodePoints({
        0x0044, 0x004F, 0x004D, 0x0041, 0x0049, 0x004E, 0x0020, 0x00B7,
        0x0020, 0x0025, 0x0031,
    });
}

// matcherLabelDomainSuffix <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "DOMAIN-SUFFIX  %1"
QString matcherLabelDomainSuffix()
{
    return fromCodePoints({
        0x0044, 0x004F, 0x004D, 0x0041, 0x0049, 0x004E, 0x002D, 0x0053,
        0x0055, 0x0046, 0x0046, 0x0049, 0x0058, 0x0020, 0x00B7, 0x0020,
        0x0025, 0x0031,
    });
}

// matcherLabelGeoSite <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "GEOSITE  %1"
QString matcherLabelGeoSite()
{
    return fromCodePoints({
        0x0047, 0x0045, 0x004F, 0x0053, 0x0049, 0x0054, 0x0045, 0x0020,
        0x00B7, 0x0020, 0x0025, 0x0031,
    });
}

// matcherLabelGeoIp <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "GEOIP  %1"
QString matcherLabelGeoIp()
{
    return fromCodePoints({
        0x0047, 0x0045, 0x004F, 0x0049, 0x0050, 0x0020, 0x00B7, 0x0020,
        0x0025, 0x0031,
    });
}

// matcherLabelIpCidr <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "IP-CIDR  %1"
QString matcherLabelIpCidr()
{
    return fromCodePoints({
        0x0049, 0x0050, 0x002D, 0x0043, 0x0049, 0x0044, 0x0052, 0x0020,
        0x00B7, 0x0020, 0x0025, 0x0031,
    });
}

// matcherLabelDestinationPort <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "DST-PORT  %1"
QString matcherLabelDestinationPort()
{
    return fromCodePoints({
        0x0044, 0x0053, 0x0054, 0x002D, 0x0050, 0x004F, 0x0052, 0x0054,
        0x0020, 0x00B7, 0x0020, 0x0025, 0x0031,
    });
}

// matcherLabelProcessName <- gui/widgets.rs:-1 ({} -> %n)
// ASCII 骨架: "PROCESS-NAME  %1"
QString matcherLabelProcessName()
{
    return fromCodePoints({
        0x0050, 0x0052, 0x004F, 0x0043, 0x0045, 0x0053, 0x0053, 0x002D,
        0x004E, 0x0041, 0x004D, 0x0045, 0x0020, 0x00B7, 0x0020, 0x0025,
        0x0031,
    });
}

// matcherLabelNetworkTcp <- gui/widgets.rs:-1
// ASCII 骨架: "NETWORK  tcp"
QString matcherLabelNetworkTcp()
{
    return fromCodePoints({
        0x004E, 0x0045, 0x0054, 0x0057, 0x004F, 0x0052, 0x004B, 0x0020,
        0x00B7, 0x0020, 0x0074, 0x0063, 0x0070,
    });
}

// matcherLabelNetworkUdp <- gui/widgets.rs:-1
// ASCII 骨架: "NETWORK  udp"
QString matcherLabelNetworkUdp()
{
    return fromCodePoints({
        0x004E, 0x0045, 0x0054, 0x0057, 0x004F, 0x0052, 0x004B, 0x0020,
        0x00B7, 0x0020, 0x0075, 0x0064, 0x0070,
    });
}

// matcherLabelQuic <- gui/widgets.rs:-1
// ASCII 骨架: "QUIC  udp/443"
QString matcherLabelQuic()
{
    return fromCodePoints({
        0x0051, 0x0055, 0x0049, 0x0043, 0x0020, 0x00B7, 0x0020, 0x0075,
        0x0064, 0x0070, 0x002F, 0x0034, 0x0034, 0x0033,
    });
}

// ruleDraftDomain <- gui/types.rs:90
// ASCII 骨架: ""
QString ruleDraftDomain()
{
    return fromCodePoints({
        0x57DF, 0x540D,
    });
}

// ruleDraftDomainSuffix <- gui/types.rs:91
// ASCII 骨架: ""
QString ruleDraftDomainSuffix()
{
    return fromCodePoints({
        0x57DF, 0x540D, 0x540E, 0x7F00,
    });
}

// ruleDraftDestinationPort <- gui/types.rs:95
// ASCII 骨架: ""
QString ruleDraftDestinationPort()
{
    return fromCodePoints({
        0x76EE, 0x6807, 0x7AEF, 0x53E3,
    });
}

// ruleDraftProcessName <- gui/types.rs:96
// ASCII 骨架: ""
QString ruleDraftProcessName()
{
    return fromCodePoints({
        0x8FDB, 0x7A0B, 0x540D,
    });
}

// ruleMoveUp <- gui/views/proxy.rs:1367
// ASCII 骨架: ""
QString ruleMoveUp()
{
    return fromCodePoints({
        0x4E0A, 0x79FB,
    });
}

// ruleMoveDown <- gui/views/proxy.rs:1377
// ASCII 骨架: ""
QString ruleMoveDown()
{
    return fromCodePoints({
        0x4E0B, 0x79FB,
    });
}

// ruleRemove <- gui/views/proxy.rs:1382
// ASCII 骨架: ""
QString ruleRemove()
{
    return fromCodePoints({
        0x5220, 0x9664,
    });
}

// busyProcessingConfig <- gui/views/proxy.rs:1166
// ASCII 骨架: "..."
QString busyProcessingConfig()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x5904, 0x7406, 0x914D, 0x7F6E, 0x4E2D, 0x002E,
        0x002E, 0x002E,
    });
}

// busyProcessingRules <- gui/views/proxy.rs:1457
// ASCII 骨架: "..."
QString busyProcessingRules()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x5904, 0x7406, 0x89C4, 0x5219, 0x4E2D, 0x002E,
        0x002E, 0x002E,
    });
}

// processNameNeedsTun <- gui/views/proxy.rs:-1
// ASCII 骨架: "PROCESS-NAME  TUN "
QString processNameNeedsTun()
{
    return fromCodePoints({
        0x0050, 0x0052, 0x004F, 0x0043, 0x0045, 0x0053, 0x0053, 0x002D,
        0x004E, 0x0041, 0x004D, 0x0045, 0x0020, 0x53EA, 0x6709, 0x0020,
        0x0054, 0x0055, 0x004E, 0x0020, 0x6A21, 0x5F0F, 0x62FF, 0x5F97,
        0x5230, 0x8FDB, 0x7A0B, 0x540D, 0xFF0C, 0x7CFB, 0x7EDF, 0x4EE3,
        0x7406, 0x6A21, 0x5F0F, 0x4E0B, 0x4E0D, 0x4F1A, 0x751F, 0x6548,
    });
}

// ruleInvalidPrefix <- gui/views/proxy.rs:1497 ({} -> %n)
// ASCII 骨架: ": %1"
QString ruleInvalidPrefix()
{
    return fromCodePoints({
        0x89C4, 0x5219, 0x65E0, 0x6548, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// optimizeAlreadyBest <- gui/views/proxy.rs:1508
// ASCII 骨架: ""
QString optimizeAlreadyBest()
{
    return fromCodePoints({
        0x89C4, 0x5219, 0x5DF2, 0x7ECF, 0x662F, 0x6700, 0x4F18, 0x987A,
        0x5E8F, 0xFF0C, 0x6CA1, 0x6709, 0x53D1, 0x73B0, 0x91CD, 0x590D,
        0x9879,
    });
}

// optimizeRemovedN <- gui/views/proxy.rs:1510 ({} -> %n)
// ASCII 骨架: " %1 "
QString optimizeRemovedN()
{
    return fromCodePoints({
        0x5DF2, 0x81EA, 0x52A8, 0x79FB, 0x9664, 0x0020, 0x0025, 0x0031,
        0x0020, 0x6761, 0x91CD, 0x590D, 0x89C4, 0x5219, 0xFF0C, 0x8BF7,
        0x4FDD, 0x5B58, 0x89C4, 0x5219,
    });
}

// overrideRulesTitle <- gui/views/proxy.rs:1526
// ASCII 骨架: ""
QString overrideRulesTitle()
{
    return fromCodePoints({
        0x8986, 0x76D6, 0x5206, 0x6D41, 0x89C4, 0x5219,
    });
}

// overrideRulesBody <- gui/views/proxy.rs:1527
// ASCII 骨架: ""
QString overrideRulesBody()
{
    return fromCodePoints({
        0x73B0, 0x6709, 0x5206, 0x6D41, 0x89C4, 0x5219, 0x5C06, 0x88AB,
        0x63A8, 0x8350, 0x89C4, 0x5219, 0x8986, 0x76D6, 0xFF0C, 0x786E,
        0x8BA4, 0x540E, 0x7ACB, 0x5373, 0x4FDD, 0x5B58, 0x3002,
    });
}

// confirmOverride <- gui/views/proxy.rs:-1
// ASCII 骨架: ""
QString confirmOverride()
{
    return fromCodePoints({
        0x786E, 0x8BA4, 0x8986, 0x76D6,
    });
}

// confirmCancel <- gui/views/proxy.rs:1530
// ASCII 骨架: ""
QString confirmCancel()
{
    return fromCodePoints({
        0x53D6, 0x6D88,
    });
}

// ruleMoveUpGlyph <- gui/views/proxy.rs:1366
// ASCII 骨架: ""
QString ruleMoveUpGlyph()
{
    return fromCodePoints({
        0x2191,
    });
}

// ruleMoveDownGlyph <- gui/views/proxy.rs:1375
// ASCII 骨架: ""
QString ruleMoveDownGlyph()
{
    return fromCodePoints({
        0x2193,
    });
}

// ruleRemoveGlyph <- gui/views/proxy.rs:1382
// ASCII 骨架: ""
QString ruleRemoveGlyph()
{
    return fromCodePoints({
        0x00D7,
    });
}

// presetAiHover <- gui/views/proxy.rs:1230
// ASCII 骨架: " OpenAIClaudeGeminiCopilot "
QString presetAiHover()
{
    return fromCodePoints({
        0x6DFB, 0x52A0, 0x0020, 0x004F, 0x0070, 0x0065, 0x006E, 0x0041,
        0x0049, 0x3001, 0x0043, 0x006C, 0x0061, 0x0075, 0x0064, 0x0065,
        0x3001, 0x0047, 0x0065, 0x006D, 0x0069, 0x006E, 0x0069, 0x3001,
        0x0043, 0x006F, 0x0070, 0x0069, 0x006C, 0x006F, 0x0074, 0x0020,
        0x57DF, 0x540D, 0x8D70, 0x4EE3, 0x7406,
    });
}

// presetMediaHover <- gui/views/proxy.rs:1248
// ASCII 骨架: " YouTubeNetflixDisney+Spotify "
QString presetMediaHover()
{
    return fromCodePoints({
        0x6DFB, 0x52A0, 0x0020, 0x0059, 0x006F, 0x0075, 0x0054, 0x0075,
        0x0062, 0x0065, 0x3001, 0x004E, 0x0065, 0x0074, 0x0066, 0x006C,
        0x0069, 0x0078, 0x3001, 0x0044, 0x0069, 0x0073, 0x006E, 0x0065,
        0x0079, 0x002B, 0x3001, 0x0053, 0x0070, 0x006F, 0x0074, 0x0069,
        0x0066, 0x0079, 0x0020, 0x8D70, 0x4EE3, 0x7406,
    });
}

// presetSocialHover <- gui/views/proxy.rs:1266
// ASCII 骨架: " Telegram  IP DiscordX "
QString presetSocialHover()
{
    return fromCodePoints({
        0x6DFB, 0x52A0, 0x0020, 0x0054, 0x0065, 0x006C, 0x0065, 0x0067,
        0x0072, 0x0061, 0x006D, 0x0020, 0x57DF, 0x540D, 0x4E0E, 0x0020,
        0x0049, 0x0050, 0x0020, 0x6BB5, 0x3001, 0x0044, 0x0069, 0x0073,
        0x0063, 0x006F, 0x0072, 0x0064, 0x3001, 0x0058, 0x0020, 0x8D70,
        0x4EE3, 0x7406,
    });
}

// presetDevHover <- gui/views/proxy.rs:1284
// ASCII 骨架: ""
QString presetDevHover()
{
    return fromCodePoints({
        0x56FD, 0x5185, 0x5E38, 0x89C1, 0x5F00, 0x6E90, 0x5305, 0x4E0E,
        0x955C, 0x50CF, 0x7AD9, 0x76F4, 0x8FDE, 0x52A0, 0x901F,
    });
}

// presetGameHover <- gui/views/proxy.rs:1302
// ASCII 骨架: "Steam  CDN "
QString presetGameHover()
{
    return fromCodePoints({
        0x0053, 0x0074, 0x0065, 0x0061, 0x006D, 0x0020, 0x4E0B, 0x8F7D,
        0x0020, 0x0043, 0x0044, 0x004E, 0x0020, 0x4E0E, 0x6E38, 0x620F,
        0x5546, 0x5E97, 0x76F4, 0x8FDE,
    });
}

// presetAdHover <- gui/views/proxy.rs:1320
// ASCII 骨架: " QUIC "
QString presetAdHover()
{
    return fromCodePoints({
        0x62E6, 0x622A, 0x5E38, 0x89C1, 0x5E7F, 0x544A, 0x5E76, 0x963B,
        0x6B62, 0x0020, 0x0051, 0x0055, 0x0049, 0x0043, 0x0020, 0x964D,
        0x901F,
    });
}

// injectionLine <- gui/views/proxy.rs:1469 ({} -> %n)
// ASCII 骨架: " Mihomo  GEOIP,CN %1"
QString injectionLine()
{
    return fromCodePoints({
        0x5185, 0x7F6E, 0x6CE8, 0x5165, 0xFF08, 0x4E0D, 0x53EF, 0x7F16,
        0x8F91, 0xFF0C, 0x4EC5, 0x0020, 0x004D, 0x0069, 0x0068, 0x006F,
        0x006D, 0x006F, 0x0020, 0x751F, 0x6548, 0xFF0C, 0x63D2, 0x5728,
        0x0020, 0x0047, 0x0045, 0x004F, 0x0049, 0x0050, 0x002C, 0x0043,
        0x004E, 0x0020, 0x4E4B, 0x524D, 0xFF09, 0xFF1A, 0x0025, 0x0031,
    });
}

// injectionJoinSep <- gui/views/proxy.rs:1470
// ASCII 骨架: "  "
QString injectionJoinSep()
{
    return fromCodePoints({
        0x0020, 0x00B7, 0x0020,
    });
}

// hitsSummary <- gui/views/proxy.rs:1096 ({} -> %n)
// ASCII 骨架: " %1  %2  MATCH%3%"
QString hitsSummary()
{
    return fromCodePoints({
        0x672C, 0x8FDB, 0x7A0B, 0x542F, 0x52A8, 0x4EE5, 0x6765, 0x0020,
        0x0025, 0x0031, 0x0020, 0x6761, 0x8FDE, 0x63A5, 0xFF0C, 0x5176,
        0x4E2D, 0x0020, 0x0025, 0x0032, 0x0020, 0x6761, 0x8D70, 0x515C,
        0x5E95, 0x0020, 0x004D, 0x0041, 0x0054, 0x0043, 0x0048, 0xFF08,
        0x0025, 0x0033, 0x0025, 0xFF09,
    });
}

// hitsArrow <- gui/views/proxy.rs:1105 ({} -> %n)
// ASCII 骨架: " %1"
QString hitsArrow()
{
    return fromCodePoints({
        0x2192, 0x0020, 0x0025, 0x0031,
    });
}

// hitsCount <- gui/views/proxy.rs:1110 ({} -> %n)
// ASCII 骨架: "%1 "
QString hitsCount()
{
    return fromCodePoints({
        0x0025, 0x0031, 0x0020, 0x6761,
    });
}

// netSectionHint <- gui/views/proxy.rs:1554
// ASCII 骨架: " DNS"
QString netSectionHint()
{
    return fromCodePoints({
        0x914D, 0x7F6E, 0x63A5, 0x7BA1, 0x6A21, 0x5F0F, 0x3001, 0x76D1,
        0x542C, 0x7AEF, 0x53E3, 0x4E0E, 0x0020, 0x0044, 0x004E, 0x0053,
    });
}

// netModeStrong <- gui/views/proxy.rs:1559
// ASCII 骨架: ""
QString netModeStrong()
{
    return fromCodePoints({
        0x63A5, 0x7BA1, 0x6A21, 0x5F0F,
    });
}

// netModeSystem <- gui/views/proxy.rs:1561
// ASCII 骨架: ""
QString netModeSystem()
{
    return fromCodePoints({
        0x7CFB, 0x7EDF, 0x4EE3, 0x7406,
    });
}

// netKernelLabel <- gui/views/proxy.rs:1566
// ASCII 骨架: ""
QString netKernelLabel()
{
    return fromCodePoints({
        0x8FD0, 0x884C, 0x5185, 0x6838,
    });
}

// netKernelAuto <- gui/views/proxy.rs:1569
// ASCII 骨架: ""
QString netKernelAuto()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x9009, 0x62E9,
    });
}

// netPortMixed <- gui/views/proxy.rs:1599
// ASCII 骨架: "Mixed "
QString netPortMixed()
{
    return fromCodePoints({
        0x004D, 0x0069, 0x0078, 0x0065, 0x0064, 0x0020, 0x7AEF, 0x53E3,
    });
}

// netPortController <- gui/views/proxy.rs:1603
// ASCII 骨架: ""
QString netPortController()
{
    return fromCodePoints({
        0x63A7, 0x5236, 0x7AEF, 0x53E3,
    });
}

// netAllowLan <- gui/views/proxy.rs:1608
// ASCII 骨架: ""
QString netAllowLan()
{
    return fromCodePoints({
        0x5141, 0x8BB8, 0x5C40, 0x57DF, 0x7F51,
    });
}

// netTerminalProxy <- gui/views/proxy.rs:1611
// ASCII 骨架: " HTTP_PROXY / HTTPS_PROXYGitcurlnpmpip "
QString netTerminalProxy()
{
    return fromCodePoints({
        0x7EC8, 0x7AEF, 0x4EE3, 0x7406, 0xFF08, 0x5199, 0x5165, 0x0020,
        0x0048, 0x0054, 0x0054, 0x0050, 0x005F, 0x0050, 0x0052, 0x004F,
        0x0058, 0x0059, 0x0020, 0x002F, 0x0020, 0x0048, 0x0054, 0x0054,
        0x0050, 0x0053, 0x005F, 0x0050, 0x0052, 0x004F, 0x0058, 0x0059,
        0xFF0C, 0x0047, 0x0069, 0x0074, 0x00B7, 0x0063, 0x0075, 0x0072,
        0x006C, 0x00B7, 0x006E, 0x0070, 0x006D, 0x00B7, 0x0070, 0x0069,
        0x0070, 0x0020, 0x624D, 0x8BA4, 0xFF09,
    });
}

// netTerminalProxyHover <- gui/views/proxy.rs:1614
// ASCII 骨架: " HKCU\\Environment\n"
QString netTerminalProxyHover()
{
    return fromCodePoints({
        0x542F, 0x52A8, 0x4EE3, 0x7406, 0x65F6, 0x628A, 0x53D8, 0x91CF,
        0x5199, 0x8FDB, 0x0020, 0x0048, 0x004B, 0x0043, 0x0055, 0x005C,
        0x0045, 0x006E, 0x0076, 0x0069, 0x0072, 0x006F, 0x006E, 0x006D,
        0x0065, 0x006E, 0x0074, 0xFF0C, 0x505C, 0x6B62, 0x65F6, 0x6E05,
        0x9664, 0xFF1B, 0x53EA, 0x6E05, 0x6211, 0x4EEC, 0x81EA, 0x5DF1,
        0x5199, 0x7684, 0x503C, 0xFF0C, 0x000A, 0x4F60, 0x539F, 0x6709,
        0x7684, 0x4EE3, 0x7406, 0x53D8, 0x91CF, 0x4E0D, 0x4F1A, 0x88AB,
        0x78B0, 0x3002, 0x65B0, 0x5F00, 0x7684, 0x7EC8, 0x7AEF, 0x7ACB,
        0x5373, 0x751F, 0x6548, 0xFF0C, 0x5DF2, 0x5F00, 0x7740, 0x7684,
        0x7EC8, 0x7AEF, 0x8981, 0x91CD, 0x5F00, 0x3002,
    });
}

// netTunStrong <- gui/views/proxy.rs:1621
// ASCII 骨架: "TUN "
QString netTunStrong()
{
    return fromCodePoints({
        0x0054, 0x0055, 0x004E, 0x0020, 0x53C2, 0x6570,
    });
}

// netStackLabel <- gui/views/proxy.rs:1623
// ASCII 骨架: ""
QString netStackLabel()
{
    return fromCodePoints({
        0x534F, 0x8BAE, 0x6808,
    });
}

// netAutoRoute <- gui/views/proxy.rs:1643
// ASCII 骨架: ""
QString netAutoRoute()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x8DEF, 0x7531,
    });
}

// netAutoDetect <- gui/views/proxy.rs:1646
// ASCII 骨架: ""
QString netAutoDetect()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x68C0, 0x6D4B, 0x7F51, 0x5361,
    });
}

// netDnsEnable <- gui/views/proxy.rs:1654
// ASCII 骨架: ""
QString netDnsEnable()
{
    return fromCodePoints({
        0x542F, 0x7528,
    });
}

// netBusyNotice <- gui/views/proxy.rs:1696
// ASCII 骨架: ""
QString netBusyNotice()
{
    return fromCodePoints({
        0x505C, 0x6B62, 0x4EE3, 0x7406, 0x540E, 0x53EF, 0x4FEE, 0x6539,
        0x8FD0, 0x884C, 0x914D, 0x7F6E,
    });
}

// netSaveButton <- gui/views/proxy.rs:1693
// ASCII 骨架: ""
QString netSaveButton()
{
    return fromCodePoints({
        0x4FDD, 0x5B58, 0x7F51, 0x7EDC, 0x914D, 0x7F6E,
    });
}

// connFilterLabel <- gui/views/proxy.rs:949
// ASCII 骨架: ""
QString connFilterLabel()
{
    return fromCodePoints({
        0x7B5B, 0x9009,
    });
}

// connEmptyIdle <- gui/views/proxy.rs:1002
// ASCII 骨架: ""
QString connEmptyIdle()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x6682, 0x65E0, 0x6D3B, 0x52A8, 0x7684, 0x7F51,
        0x7EDC, 0x8FDE, 0x63A5,
    });
}

// connEmptyNoMatch <- gui/views/proxy.rs:1004
// ASCII 骨架: ""
QString connEmptyNoMatch()
{
    return fromCodePoints({
        0x6CA1, 0x6709, 0x5339, 0x914D, 0x7B5B, 0x9009, 0x6761, 0x4EF6,
        0x7684, 0x7F51, 0x7EDC, 0x8FDE, 0x63A5,
    });
}

// connEmptyStopped <- gui/views/proxy.rs:977
// ASCII 骨架: ""
QString connEmptyStopped()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x672A, 0x5728, 0x8FD0, 0x884C, 0x4E2D, 0xFF0C,
        0x6682, 0x65E0, 0x6D3B, 0x8DC3, 0x8FDE, 0x63A5, 0x6570, 0x636E,
    });
}

// connDisconnectHover <- gui/views/proxy.rs:1048
// ASCII 骨架: ""
QString connDisconnectHover()
{
    return fromCodePoints({
        0x65AD, 0x5F00, 0x6B64, 0x8FDE, 0x63A5,
    });
}

// logsTitle <- gui/views/proxy.rs:1882
// ASCII 骨架: ""
QString logsTitle()
{
    return fromCodePoints({
        0x8FD0, 0x884C, 0x65E5, 0x5FD7,
    });
}

// logsSubtitle <- gui/views/proxy.rs:1882 ({} -> %n)
// ASCII 骨架: "%1 "
QString logsSubtitle()
{
    return fromCodePoints({
        0x0025, 0x0031, 0x0020, 0x6700, 0x8FD1, 0x8F93, 0x51FA,
    });
}

// logsEmpty <- gui/views/proxy.rs:1889
// ASCII 骨架: ""
QString logsEmpty()
{
    return fromCodePoints({
        0x6682, 0x65E0, 0x65E5, 0x5FD7,
    });
}

// logsKernelFallback <- gui/views/proxy.rs:1881
// ASCII 骨架: ""
QString logsKernelFallback()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5185, 0x6838,
    });
}

// kernelUpdateFailed <- gui/mod.rs:1050 ({} -> %n)
// ASCII 骨架: ": %1"
QString kernelUpdateFailed()
{
    return fromCodePoints({
        0x5185, 0x6838, 0x66F4, 0x65B0, 0x5931, 0x8D25, 0x003A, 0x0020,
        0x0025, 0x0031,
    });
}

// configReadFailed <- gui/mod.rs:1058 ({} -> %n)
// ASCII 骨架: ": %1"
QString configReadFailed()
{
    return fromCodePoints({
        0x8BFB, 0x53D6, 0x4EE3, 0x7406, 0x914D, 0x7F6E, 0x5931, 0x8D25,
        0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// geoUpdatedOk <- gui/mod.rs:2860
// ASCII 骨架: "Geo "
QString geoUpdatedOk()
{
    return fromCodePoints({
        0x0047, 0x0065, 0x006F, 0x0020, 0x89C4, 0x5219, 0x6570, 0x636E,
        0x5E93, 0x66F4, 0x65B0, 0x6307, 0x4EE4, 0x5DF2, 0x6210, 0x529F,
        0x6267, 0x884C, 0x5E76, 0x91CD, 0x65B0, 0x8F7D, 0x5165,
    });
}

// geoUpdateFailed <- gui/mod.rs:1256 ({} -> %n)
// ASCII 骨架: "Geo : %1"
QString geoUpdateFailed()
{
    return fromCodePoints({
        0x0047, 0x0065, 0x006F, 0x0020, 0x89C4, 0x5219, 0x66F4, 0x65B0,
        0x5931, 0x8D25, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// geoOfflineBusy <- gui/mod.rs:2828
// ASCII 骨架: " GeoIP / GeoSite ..."
QString geoOfflineBusy()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x672A, 0x8FD0, 0x884C, 0xFF0C, 0x6B63, 0x5728,
        0x76F4, 0x63A5, 0x4E0B, 0x8F7D, 0x0020, 0x0047, 0x0065, 0x006F,
        0x0049, 0x0050, 0x0020, 0x002F, 0x0020, 0x0047, 0x0065, 0x006F,
        0x0053, 0x0069, 0x0074, 0x0065, 0x0020, 0x89C4, 0x5219, 0x6587,
        0x4EF6, 0x002E, 0x002E, 0x002E,
    });
}

// geoOfflineDone <- gui/mod.rs:2833 ({} -> %n)
// ASCII 骨架: " %1%2"
QString geoOfflineDone()
{
    return fromCodePoints({
        0x5185, 0x6838, 0x672A, 0x8FD0, 0x884C, 0xFF0C, 0x5DF2, 0x76F4,
        0x63A5, 0x66F4, 0x65B0, 0x5230, 0x0020, 0x0025, 0x0031, 0xFF1A,
        0x0025, 0x0032,
    });
}

// geoRequesting <- gui/mod.rs:2838
// ASCII 骨架: " GeoIP / GeoSite ..."
QString geoRequesting()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x5411, 0x5185, 0x6838, 0x8BF7, 0x6C42, 0x66F4,
        0x65B0, 0x0020, 0x0047, 0x0065, 0x006F, 0x0049, 0x0050, 0x0020,
        0x002F, 0x0020, 0x0047, 0x0065, 0x006F, 0x0053, 0x0069, 0x0074,
        0x0065, 0x0020, 0x89C4, 0x5219, 0x6587, 0x4EF6, 0x002E, 0x002E,
        0x002E,
    });
}

// geoBadStatus <- gui/mod.rs:2868 ({} -> %n)
// ASCII 骨架: ": %1"
QString geoBadStatus()
{
    return fromCodePoints({
        0x66F4, 0x65B0, 0x63A5, 0x53E3, 0x8FD4, 0x56DE, 0x72B6, 0x6001,
        0x7801, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// geoKernelDownloadFailed <- gui/mod.rs:2870 ({} -> %n)
// ASCII 骨架: "%1%2"
QString geoKernelDownloadFailed()
{
    return fromCodePoints({
        0x5185, 0x6838, 0x81EA, 0x5DF1, 0x4E0B, 0x8F7D, 0x5931, 0x8D25,
        0xFF08, 0x0025, 0x0031, 0xFF09, 0xFF1A, 0x0025, 0x0032,
    });
}

// geoRequestFailed <- gui/mod.rs:2874 ({} -> %n)
// ASCII 骨架: " Geo : %1"
QString geoRequestFailed()
{
    return fromCodePoints({
        0x8BF7, 0x6C42, 0x0020, 0x0047, 0x0065, 0x006F, 0x0020, 0x66F4,
        0x65B0, 0x5931, 0x8D25, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// savingConfig <- gui/mod.rs:2266
// ASCII 骨架: "..."
QString savingConfig()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x4FDD, 0x5B58, 0x5206, 0x6D41, 0x914D, 0x7F6E,
        0x5E76, 0x5E94, 0x7528, 0x002E, 0x002E, 0x002E,
    });
}

// configSaved <- gui/mod.rs:1065
// ASCII 骨架: ""
QString configSaved()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x914D, 0x7F6E, 0x5DF2, 0x4FDD, 0x5B58,
    });
}

// configSaveFailed <- gui/mod.rs:1068 ({} -> %n)
// ASCII 骨架: ": %1"
QString configSaveFailed()
{
    return fromCodePoints({
        0x4FDD, 0x5B58, 0x4EE3, 0x7406, 0x914D, 0x7F6E, 0x5931, 0x8D25,
        0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// kernelCheckDone <- gui/mod.rs:1014 ({} -> %n)
// ASCII 骨架: "%1"
QString kernelCheckDone()
{
    return fromCodePoints({
        0x5185, 0x6838, 0x7248, 0x672C, 0x68C0, 0x67E5, 0x5B8C, 0x6210,
        0xFF1A, 0x0025, 0x0031,
    });
}

// middleDotJoin <- gui/mod.rs:1013
// ASCII 骨架: "  "
QString middleDotJoin()
{
    return fromCodePoints({
        0x0020, 0x00B7, 0x0020,
    });
}

// sessionTraffic <- gui/views/proxy.rs:318 ({} -> %n)
// ASCII 骨架: "  %1  %2"
QString sessionTraffic()
{
    return fromCodePoints({
        0x4F1A, 0x8BDD, 0x0020, 0x2191, 0x0020, 0x0025, 0x0031, 0x0020,
        0x2193, 0x0020, 0x0025, 0x0032,
    });
}

// pagerInfo <- gui/views/nodes.rs:356 ({} -> %n)
// ASCII 骨架: " %1 / %2    %3 "
QString pagerInfo()
{
    return fromCodePoints({
        0x7B2C, 0x0020, 0x0025, 0x0031, 0x0020, 0x002F, 0x0020, 0x0025,
        0x0032, 0x0020, 0x9875, 0x0020, 0x00B7, 0x0020, 0x5171, 0x0020,
        0x0025, 0x0033, 0x0020, 0x6761,
    });
}

// pagerPrev <- gui/views/nodes.rs:360 ({} -> %n)
// ASCII 骨架: ""
QString pagerPrev()
{
    return fromCodePoints({
        0x4E0A, 0x4E00, 0x9875,
    });
}

// pagerNext <- gui/views/nodes.rs:367 ({} -> %n)
// ASCII 骨架: ""
QString pagerNext()
{
    return fromCodePoints({
        0x4E0B, 0x4E00, 0x9875,
    });
}

// logReadFailed <- gui/mod.rs:1075 ({} -> %n)
// ASCII 骨架: ": %1"
QString logReadFailed()
{
    return fromCodePoints({
        0x8BFB, 0x53D6, 0x4EE3, 0x7406, 0x65E5, 0x5FD7, 0x5931, 0x8D25,
        0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// kernelNoLatest <- gui/mod.rs:1009
// ASCII 骨架: ""
QString kernelNoLatest()
{
    return fromCodePoints({
        0x672A, 0x53D6, 0x5230, 0x6700, 0x65B0, 0x7248, 0x672C,
    });
}

// nodeDegradedMarked <- gui/mod.rs:2523 ({} -> %n)
// ASCII 骨架: "%1"
QString nodeDegradedMarked()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x8282, 0x70B9, 0x8FDE, 0x901A, 0x6027, 0x5F02,
        0x5E38, 0xFF08, 0x5DF2, 0x81EA, 0x52A8, 0x6807, 0x8BB0, 0x4E3A,
        0x4E0D, 0x53EF, 0x7528, 0xFF09, 0xFF1A, 0x0025, 0x0031,
    });
}

// proxyStartedWithNode <- gui/mod.rs:2110 ({} -> %n)
// ASCII 骨架: ": %1"
QString proxyStartedWithNode()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5DF2, 0x542F, 0x52A8, 0xFF0C, 0x5DF2, 0x8FDE,
        0x63A5, 0x8282, 0x70B9, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// switchedSmoothly <- gui/mod.rs:2112 ({} -> %n)
// ASCII 骨架: ": %1"
QString switchedSmoothly()
{
    return fromCodePoints({
        0x5DF2, 0x5E73, 0x6ED1, 0x5207, 0x6362, 0x81F3, 0x8282, 0x70B9,
        0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// switchNodeFailed <- gui/mod.rs:1104 ({} -> %n)
// ASCII 骨架: ": %1"
QString switchNodeFailed()
{
    return fromCodePoints({
        0x5207, 0x6362, 0x8282, 0x70B9, 0x5931, 0x8D25, 0x003A, 0x0020,
        0x0025, 0x0031,
    });
}

// startingProxyWithNode <- gui/mod.rs:2087 ({} -> %n)
// ASCII 骨架: " %1..."
QString startingProxyWithNode()
{
    return fromCodePoints({
        0x26A1, 0x0020, 0x6B63, 0x5728, 0x6781, 0x901F, 0x5F02, 0x6B65,
        0x542F, 0x52A8, 0x4EE3, 0x7406, 0x5E76, 0x8FDE, 0x63A5, 0x300C,
        0x0025, 0x0031, 0x300D, 0x002E, 0x002E, 0x002E,
    });
}

// switchingToNode <- gui/mod.rs:2089 ({} -> %n)
// ASCII 骨架: " %1..."
QString switchingToNode()
{
    return fromCodePoints({
        0x26A1, 0x0020, 0x6B63, 0x5728, 0x6781, 0x901F, 0x5F02, 0x6B65,
        0x5207, 0x6362, 0x81F3, 0x300C, 0x0025, 0x0031, 0x300D, 0x002E,
        0x002E, 0x002E,
    });
}

// proxyStartedTun <- gui/mod.rs:2527
// ASCII 骨架: "TUN "
QString proxyStartedTun()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5DF2, 0x542F, 0x52A8, 0xFF0C, 0x0054, 0x0055,
        0x004E, 0x0020, 0x6A21, 0x5F0F, 0x5DF2, 0x542F, 0x7528,
    });
}

// proxyStartedSystem <- gui/mod.rs:2529
// ASCII 骨架: ""
QString proxyStartedSystem()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5DF2, 0x542F, 0x52A8, 0xFF0C, 0x7CFB, 0x7EDF,
        0x4EE3, 0x7406, 0x5DF2, 0x542F, 0x7528,
    });
}

// speedGroupStatus <- gui/mod.rs:1914 ({} -> %n)
// ASCII 骨架: "%1%2 ..."
QString speedGroupStatus()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x6D4B, 0x901F, 0x8BA2, 0x9605, 0x201C, 0x0025,
        0x0031, 0x201D, 0xFF08, 0x0025, 0x0032, 0x0020, 0x4E2A, 0x8282,
        0x70B9, 0xFF09, 0x002E, 0x002E, 0x002E,
    });
}

// speedGroupEmpty <- gui/mod.rs:1928
// ASCII 骨架: ""
QString speedGroupEmpty()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x5206, 0x7EC4, 0x6CA1, 0x6709, 0x7B26, 0x5408,
        0x7B5B, 0x9009, 0x6761, 0x4EF6, 0x7684, 0x8282, 0x70B9,
    });
}

// speedAllInitial <- gui/mod.rs:1884
// ASCII 骨架: "..."
QString speedAllInitial()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x5E76, 0x53D1, 0x6D4B, 0x8BD5, 0x6240, 0x6709,
        0x8BA2, 0x9605, 0x8282, 0x70B9, 0x002E, 0x002E, 0x002E,
    });
}

// speedAllProgress <- gui/mod.rs:947 ({} -> %n)
// ASCII 骨架: " %1/%2 "
QString speedAllProgress()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x6D4B, 0x8BD5, 0x6240, 0x6709, 0x8282, 0x70B9,
        0xFF1A, 0x5DF2, 0x5B8C, 0x6210, 0x0020, 0x0025, 0x0031, 0x002F,
        0x0025, 0x0032, 0x0020, 0x4E2A, 0x8282, 0x70B9,
    });
}

// speedDoneSummary <- gui/mod.rs:961 ({} -> %n)
// ASCII 骨架: " %1/%2 %3 %4"
QString speedDoneSummary()
{
    return fromCodePoints({
        0x6D4B, 0x901F, 0x5B8C, 0x6210, 0xFF1A, 0x53EF, 0x7528, 0x0020,
        0x0025, 0x0031, 0x002F, 0x0025, 0x0032, 0xFF0C, 0x5E73, 0x5747,
        0x5EF6, 0x8FDF, 0x0020, 0x0025, 0x0033, 0xFF0C, 0x5E73, 0x5747,
        0x4E0B, 0x8F7D, 0x0020, 0x0025, 0x0034,
    });
}

// speedDoneFailed <- gui/mod.rs:969 ({} -> %n)
// ASCII 骨架: ": %1"
QString speedDoneFailed()
{
    return fromCodePoints({
        0x6D4B, 0x901F, 0x5931, 0x8D25, 0x003A, 0x0020, 0x0025, 0x0031,
    });
}

// speedPageLabel <- gui/mod.rs:1989 ({} -> %n)
// ASCII 骨架: " %1 ..."
QString speedPageLabel()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x771F, 0x5B9E, 0x4EE3, 0x7406, 0x9A8C, 0x8BC1,
        0x672C, 0x9875, 0x0020, 0x0025, 0x0031, 0x0020, 0x4E2A, 0x8282,
        0x70B9, 0x002E, 0x002E, 0x002E,
    });
}

// speedPageEmpty <- gui/mod.rs:1985
// ASCII 骨架: ""
QString speedPageEmpty()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x9875, 0x6CA1, 0x6709, 0x8282, 0x70B9,
    });
}

// speedOneStatus <- gui/mod.rs:1996
// ASCII 骨架: "..."
QString speedOneStatus()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x6D4B, 0x901F, 0x8282, 0x70B9, 0x002E, 0x002E,
        0x002E,
    });
}

// speedAddedSourceLabel <- gui/mod.rs:861 ({} -> %n)
// ASCII 骨架: "%1 %2 ..."
QString speedAddedSourceLabel()
{
    return fromCodePoints({
        0x6B63, 0x5728, 0x6D4B, 0x901F, 0x65B0, 0x589E, 0x8BA2, 0x9605,
        0x300C, 0x0025, 0x0031, 0x300D, 0x7684, 0x0020, 0x0025, 0x0032,
        0x0020, 0x4E2A, 0x8282, 0x70B9, 0x002E, 0x002E, 0x002E,
    });
}

// phaseTcpPrefilter <- gui/widgets.rs:728
// ASCII 骨架: "TCP"
QString phaseTcpPrefilter()
{
    return fromCodePoints({
        0x0054, 0x0043, 0x0050, 0x9884, 0x7B5B,
    });
}

// phaseRealProxy <- gui/widgets.rs:729
// ASCII 骨架: ""
QString phaseRealProxy()
{
    return fromCodePoints({
        0x771F, 0x5B9E, 0x4EE3, 0x7406, 0x9A8C, 0x8BC1,
    });
}

// speedProgressFormat <- gui/widgets.rs:744 ({} -> %n)
// ASCII 骨架: "%1%2 %3/%4%5%  %6 /  %7 %8s"
QString speedProgressFormat()
{
    return fromCodePoints({
        0x0025, 0x0031, 0x0025, 0x0032, 0x0020, 0x0025, 0x0033, 0x002F,
        0x0025, 0x0034, 0xFF08, 0x0025, 0x0035, 0x0025, 0xFF09, 0x0020,
        0x6210, 0x529F, 0x0020, 0x0025, 0x0036, 0x0020, 0x002F, 0x0020,
        0x5931, 0x8D25, 0x0020, 0x0025, 0x0037, 0xFF0C, 0x8017, 0x65F6,
        0x0020, 0x0025, 0x0038, 0x0073,
    });
}

// speedBatchDetail <- gui/widgets.rs:738 ({} -> %n)
// ASCII 骨架: "  %1  %2/%3"
QString speedBatchDetail()
{
    return fromCodePoints({
        0x0020, 0x00B7, 0x0020, 0x0025, 0x0031, 0x0020, 0x6279, 0x6B21,
        0x0020, 0x0025, 0x0032, 0x002F, 0x0025, 0x0033,
    });
}

// speedKernelDetail <- gui/widgets.rs:740 ({} -> %n)
// ASCII 骨架: "  %1"
QString speedKernelDetail()
{
    return fromCodePoints({
        0x0020, 0x00B7, 0x0020, 0x0025, 0x0031,
    });
}

// cardHoverAutoRouting <- gui/views/proxy.rs:690
// ASCII 骨架: "/  /  / IP "
QString cardHoverAutoRouting()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x9009, 0x8DEF, 0x4E2D, 0xFF1A, 0x51FA, 0x53E3,
        0x7531, 0x5185, 0x6838, 0x51B3, 0x5B9A, 0x3002, 0x53F3, 0x952E,
        0x53EF, 0x300C, 0x9501, 0x5B9A, 0x4E3A, 0x51FA, 0x53E3, 0x300D,
        0xFF08, 0x4F1A, 0x5173, 0x6389, 0x81EA, 0x52A8, 0x9009, 0x8DEF,
        0xFF09, 0x002F, 0x0020, 0x67E5, 0x770B, 0x8BE6, 0x60C5, 0x0020,
        0x002F, 0x0020, 0x6D4B, 0x901F, 0x0020, 0x002F, 0x0020, 0x0049,
        0x0050, 0x0020, 0x68C0, 0x6D4B,
    });
}

// cardHoverPlain <- gui/views/proxy.rs:692
// ASCII 骨架: " /  / IP "
QString cardHoverPlain()
{
    return fromCodePoints({
        0x70B9, 0x51FB, 0x8FDE, 0x63A5, 0xFF0C, 0x53F3, 0x952E, 0x67E5,
        0x770B, 0x8BE6, 0x60C5, 0x0020, 0x002F, 0x0020, 0x6D4B, 0x901F,
        0x0020, 0x002F, 0x0020, 0x0049, 0x0050, 0x0020, 0x68C0, 0x6D4B,
    });
}

// autoRoutingClickIgnored <- gui/views/proxy.rs:696
// ASCII 骨架: ""
QString autoRoutingClickIgnored()
{
    return fromCodePoints({
        0x81EA, 0x52A8, 0x9009, 0x8DEF, 0x4E2D, 0xFF1A, 0x51FA, 0x53E3,
        0x7531, 0x5185, 0x6838, 0x51B3, 0x5B9A, 0xFF0C, 0x8981, 0x56FA,
        0x5B9A, 0x8FD9, 0x4E2A, 0x8282, 0x70B9, 0x8BF7, 0x53F3, 0x952E,
        0x300C, 0x9501, 0x5B9A, 0x4E3A, 0x51FA, 0x53E3, 0x300D,
    });
}

// menuDeleteNode <- gui/views/nodes.rs:429
// ASCII 骨架: ""
QString menuDeleteNode()
{
    return fromCodePoints({
        0x5220, 0x9664, 0x8282, 0x70B9,
    });
}

// egressLockedStatus2 <- gui/views/proxy.rs:741 ({} -> %n)
// ASCII 骨架: " %1"
QString egressLockedStatus2()
{
    return fromCodePoints({
        0x5DF2, 0x5173, 0x95ED, 0x81EA, 0x52A8, 0x9009, 0x8DEF, 0xFF0C,
        0x51FA, 0x53E3, 0x9501, 0x5B9A, 0x4E3A, 0x0020, 0x0025, 0x0031,
    });
}

// speedGroupHover <- gui/views/nodes.rs:197
// ASCII 骨架: ""
QString speedGroupHover()
{
    return fromCodePoints({
        0x6D4B, 0x901F, 0x5F53, 0x524D, 0x8BA2, 0x9605, 0x4E2D, 0x7B26,
        0x5408, 0x7B5B, 0x9009, 0x6761, 0x4EF6, 0x7684, 0x5168, 0x90E8,
        0x8282, 0x70B9, 0xFF0C, 0x4E0D, 0x6B62, 0x672C, 0x9875,
    });
}

// speedAllHover <- gui/views/nodes.rs:206
// ASCII 骨架: " Google "
QString speedAllHover()
{
    return fromCodePoints({
        0x5BF9, 0x5168, 0x90E8, 0x8BA2, 0x9605, 0x4E0E, 0x672A, 0x5206,
        0x7EC4, 0x8282, 0x70B9, 0x6267, 0x884C, 0x4E25, 0x683C, 0x0020,
        0x0047, 0x006F, 0x006F, 0x0067, 0x006C, 0x0065, 0x0020, 0x4EE3,
        0x7406, 0x9A8C, 0x8BC1,
    });
}

}
