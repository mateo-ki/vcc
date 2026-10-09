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

}
