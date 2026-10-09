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

// panelConnectionsCounted <- gui/views/proxy.rs:-1
// ASCII 骨架: " ({})"
QString panelConnectionsCounted()
{
    return fromCodePoints({
        0x6D3B, 0x8DC3, 0x8FDE, 0x63A5, 0x0020, 0x0028, 0x007B, 0x007D,
        0x0029,
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

// preparedFailed <- gui/views/proxy.rs:-1
// ASCII 骨架: "   {}/{}"
QString preparedFailed()
{
    return fromCodePoints({
        0x9884, 0x5907, 0x0020, 0x00B7, 0x0020, 0x4E0B, 0x8F7D, 0x5931,
        0x8D25, 0x0020, 0x007B, 0x007D, 0x002F, 0x007B, 0x007D,
    });
}

// preparedDownloading <- gui/views/proxy.rs:-1
// ASCII 骨架: "   ({}/{})"
QString preparedDownloading()
{
    return fromCodePoints({
        0x9884, 0x5907, 0x0020, 0x00B7, 0x0020, 0x6B63, 0x5728, 0x4E0B,
        0x8F7D, 0x5185, 0x6838, 0x0020, 0x0028, 0x007B, 0x007D, 0x002F,
        0x007B, 0x007D, 0x0029,
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

}
