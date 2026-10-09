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

// brandSubtitle <- gui/views/sidebar.rs:51
// ASCII 骨架: ""
QString brandSubtitle()
{
    return fromCodePoints({
        0x7F51, 0x7EDC, 0x4EE3, 0x7406, 0x63A7, 0x5236, 0x53F0,
    });
}

// navConsole <- gui/views/sidebar.rs:63
// ASCII 骨架: ""
QString navConsole()
{
    return fromCodePoints({
        0x63A7, 0x5236, 0x53F0,
    });
}

// navNodePool <- gui/views/sidebar.rs:64
// ASCII 骨架: ""
QString navNodePool()
{
    return fromCodePoints({
        0x8282, 0x70B9, 0x6C60,
    });
}

// navSubscriptions <- gui/views/sidebar.rs:65
// ASCII 骨架: ""
QString navSubscriptions()
{
    return fromCodePoints({
        0x8BA2, 0x9605, 0x6E90,
    });
}

// navSystemSettings <- gui/views/sidebar.rs:66
// ASCII 骨架: ""
QString navSystemSettings()
{
    return fromCodePoints({
        0x7CFB, 0x7EDF, 0x8BBE, 0x7F6E,
    });
}

// navExternalApi <- gui/views/sidebar.rs:67
// ASCII 骨架: ""
QString navExternalApi()
{
    return fromCodePoints({
        0x5916, 0x90E8, 0x63A5, 0x53E3,
    });
}

// panelNodes <- gui/views/proxy.rs:333
// ASCII 骨架: ""
QString panelNodes()
{
    return fromCodePoints({
        0x53EF, 0x7528, 0x8282, 0x70B9,
    });
}

// panelConnections <- gui/views/proxy.rs:337
// ASCII 骨架: ""
QString panelConnections()
{
    return fromCodePoints({
        0x6D3B, 0x8DC3, 0x8FDE, 0x63A5,
    });
}

// panelConnectionsCounted <- gui/views/proxy.rs:340
// ASCII 骨架: " ({})"
QString panelConnectionsCounted()
{
    return fromCodePoints({
        0x6D3B, 0x8DC3, 0x8FDE, 0x63A5, 0x0020, 0x0028, 0x007B, 0x007D,
        0x0029,
    });
}

// panelRules <- gui/views/proxy.rs:352
// ASCII 骨架: ""
QString panelRules()
{
    return fromCodePoints({
        0x5206, 0x6D41, 0x89C4, 0x5219,
    });
}

// panelNetwork <- gui/views/proxy.rs:359
// ASCII 骨架: "DNS / TUN"
QString panelNetwork()
{
    return QStringLiteral("DNS / TUN");
}

// panelKernels <- gui/views/proxy.rs:366
// ASCII 骨架: ""
QString panelKernels()
{
    return fromCodePoints({
        0x5185, 0x6838, 0x7BA1, 0x7406,
    });
}

// panelLogs <- gui/views/proxy.rs:373
// ASCII 骨架: ""
QString panelLogs()
{
    return fromCodePoints({
        0x8FD0, 0x884C, 0x65E5, 0x5FD7,
    });
}

// stateStopped <- gui/widgets.rs:670
// ASCII 骨架: ""
QString stateStopped()
{
    return fromCodePoints({
        0x5DF2, 0x505C, 0x6B62,
    });
}

// stateStarting <- gui/widgets.rs:671
// ASCII 骨架: ""
QString stateStarting()
{
    return fromCodePoints({
        0x542F, 0x52A8, 0x4E2D,
    });
}

// stateRunning <- gui/widgets.rs:672
// ASCII 骨架: ""
QString stateRunning()
{
    return fromCodePoints({
        0x8FD0, 0x884C, 0x4E2D,
    });
}

// stateDegraded <- gui/widgets.rs:673
// ASCII 骨架: ""
QString stateDegraded()
{
    return fromCodePoints({
        0x964D, 0x7EA7,
    });
}

// stateStopping <- gui/widgets.rs:674
// ASCII 骨架: ""
QString stateStopping()
{
    return fromCodePoints({
        0x505C, 0x6B62, 0x4E2D,
    });
}

// stateFailed <- gui/widgets.rs:675
// ASCII 骨架: ""
QString stateFailed()
{
    return fromCodePoints({
        0x5931, 0x8D25,
    });
}

// preparedFailed <- gui/views/proxy.rs:56
// ASCII 骨架: "   {}/{}"
QString preparedFailed()
{
    return fromCodePoints({
        0x9884, 0x5907, 0x0020, 0x00B7, 0x0020, 0x4E0B, 0x8F7D, 0x5931,
        0x8D25, 0x0020, 0x007B, 0x007D, 0x002F, 0x007B, 0x007D,
    });
}

// preparedDownloading <- gui/views/proxy.rs:54
// ASCII 骨架: "   ({}/{})"
QString preparedDownloading()
{
    return fromCodePoints({
        0x9884, 0x5907, 0x0020, 0x00B7, 0x0020, 0x6B63, 0x5728, 0x4E0B,
        0x8F7D, 0x5185, 0x6838, 0x0020, 0x0028, 0x007B, 0x007D, 0x002F,
        0x007B, 0x007D, 0x0029,
    });
}

// prepared <- gui/views/proxy.rs:65
// ASCII 骨架: ""
QString prepared()
{
    return fromCodePoints({
        0x9884, 0x5907,
    });
}

// regionLabel <- gui/views/proxy.rs:506
// ASCII 骨架: ":"
QString regionLabel()
{
    return fromCodePoints({
        0x5730, 0x533A, 0x003A,
    });
}

// regionAll <- gui/views/proxy.rs:507
// ASCII 骨架: ""
QString regionAll()
{
    return fromCodePoints({
        0x5168, 0x90E8,
    });
}

// regionHK <- gui/views/proxy.rs:508
// ASCII 骨架: ""
QString regionHK()
{
    return fromCodePoints({
        0x9999, 0x6E2F,
    });
}

// regionJP <- gui/views/proxy.rs:509
// ASCII 骨架: ""
QString regionJP()
{
    return fromCodePoints({
        0x65E5, 0x672C,
    });
}

// regionUS <- gui/views/proxy.rs:510
// ASCII 骨架: ""
QString regionUS()
{
    return fromCodePoints({
        0x7F8E, 0x56FD,
    });
}

// regionSG <- gui/views/proxy.rs:511
// ASCII 骨架: ""
QString regionSG()
{
    return fromCodePoints({
        0x65B0, 0x52A0, 0x5761,
    });
}

// regionTW <- gui/views/proxy.rs:512
// ASCII 骨架: ""
QString regionTW()
{
    return fromCodePoints({
        0x53F0, 0x6E7E,
    });
}

// regionKR <- gui/views/proxy.rs:513
// ASCII 骨架: ""
QString regionKR()
{
    return fromCodePoints({
        0x97E9, 0x56FD,
    });
}

// regionOther <- gui/views/proxy.rs:514
// ASCII 骨架: ""
QString regionOther()
{
    return fromCodePoints({
        0x5176, 0x5B83,
    });
}

// sortLabel <- gui/views/proxy.rs:535
// ASCII 骨架: ":"
QString sortLabel()
{
    return fromCodePoints({
        0x6392, 0x5E8F, 0x003A,
    });
}

// sortByLatency <- gui/views/proxy.rs:538
// ASCII 骨架: ""
QString sortByLatency()
{
    return fromCodePoints({
        0x5EF6, 0x8FDF,
    });
}

// sortByName <- gui/views/proxy.rs:544
// ASCII 骨架: ""
QString sortByName()
{
    return fromCodePoints({
        0x540D, 0x79F0,
    });
}

// nodeCountTotal <- gui/views/proxy.rs:552
// ASCII 骨架: " {} "
QString nodeCountTotal()
{
    return fromCodePoints({
        0x5171, 0x0020, 0x007B, 0x007D, 0x0020, 0x4E2A, 0x8282, 0x70B9,
    });
}

// searchHint <- gui/views/proxy.rs:575
// ASCII 骨架: "..."
QString searchHint()
{
    return fromCodePoints({
        0x641C, 0x7D22, 0x8282, 0x70B9, 0x002E, 0x002E, 0x002E,
    });
}

// menuDetails <- gui/views/proxy.rs:702
// ASCII 骨架: ""
QString menuDetails()
{
    return fromCodePoints({
        0x67E5, 0x770B, 0x8BE6, 0x60C5,
    });
}

// menuSpeedtest <- gui/views/proxy.rs:709
// ASCII 骨架: ""
QString menuSpeedtest()
{
    return fromCodePoints({
        0x6D4B, 0x901F, 0x8282, 0x70B9,
    });
}

// menuIpRisk <- gui/views/proxy.rs:713
// ASCII 骨架: "IP "
QString menuIpRisk()
{
    return fromCodePoints({
        0x0049, 0x0050, 0x0020, 0x68C0, 0x6D4B,
    });
}

// menuLockEgress <- gui/views/proxy.rs:719
// ASCII 骨架: ""
QString menuLockEgress()
{
    return fromCodePoints({
        0x9501, 0x5B9A, 0x4E3A, 0x51FA, 0x53E3, 0xFF08, 0x5173, 0x95ED,
        0x81EA, 0x52A8, 0x9009, 0x8DEF, 0xFF09,
    });
}

// switchReasonManual <- gui/views/proxy.rs:733
// ASCII 骨架: ""
QString switchReasonManual()
{
    return fromCodePoints({
        0x624B, 0x52A8, 0x5207, 0x6362,
    });
}

// latencyProxyFailed <- gui/widgets.rs:580
// ASCII 骨架: ""
QString latencyProxyFailed()
{
    return fromCodePoints({
        0x4EE3, 0x7406, 0x5931, 0x8D25,
    });
}

// latencyTcpFailed <- gui/widgets.rs:585
// ASCII 骨架: "TCP  "
QString latencyTcpFailed()
{
    return fromCodePoints({
        0x0054, 0x0043, 0x0050, 0x5931, 0x8D25, 0x0020, 0x00B7, 0x0020,
        0x672A, 0x9A8C, 0x8BC1,
    });
}

// latencyUntested <- gui/widgets.rs:586
// ASCII 骨架: ""
QString latencyUntested()
{
    return fromCodePoints({
        0x672A, 0x6D4B, 0x901F,
    });
}

// kernelNameFallback <- gui/widgets.rs:576
// ASCII 骨架: ""
QString kernelNameFallback()
{
    return fromCodePoints({
        0x4EE3, 0x7406,
    });
}

// switchingLabel <- gui/widgets.rs:241
// ASCII 骨架: "..."
QString switchingLabel()
{
    return fromCodePoints({
        0x5207, 0x6362, 0x4E2D, 0x002E, 0x002E, 0x002E,
    });
}

// emptyNodeGrid <- gui/views/proxy.rs:659
// ASCII 骨架: ""
QString emptyNodeGrid()
{
    return fromCodePoints({
        0x6CA1, 0x6709, 0x5339, 0x914D, 0x7684, 0x53EF, 0x7528, 0x8282,
        0x70B9, 0xFF0C, 0x8BF7, 0x5728, 0x300C, 0x8282, 0x70B9, 0x5217,
        0x8868, 0x300D, 0x4E2D, 0x8FDB, 0x884C, 0x6D4B, 0x901F,
    });
}

// egressLockedStatus <- gui/views/proxy.rs:741
// ASCII 骨架: " {name}"
QString egressLockedStatus()
{
    return fromCodePoints({
        0x5DF2, 0x5173, 0x95ED, 0x81EA, 0x52A8, 0x9009, 0x8DEF, 0xFF0C,
        0x51FA, 0x53E3, 0x9501, 0x5B9A, 0x4E3A, 0x0020, 0x007B, 0x006E,
        0x0061, 0x006D, 0x0065, 0x007D,
    });
}

}
