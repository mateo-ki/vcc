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

}
