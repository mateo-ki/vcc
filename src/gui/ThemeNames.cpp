// 由 tools/gen-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。
// 这些提示文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）。
#include "gui/ThemeNames.h"

#include <initializer_list>

namespace Gui::ThemeNames {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

// softCobalt <- gui/theme.rs:-1
// ASCII 骨架: ""
QString softCobalt()
{
    return fromCodePoints({
        0x94B4, 0x84DD, 0xFF08, 0x9ED8, 0x8BA4, 0xFF09,
    });
}

// softTeal <- gui/theme.rs:-1
// ASCII 骨架: ""
QString softTeal()
{
    return fromCodePoints({
        0x9752, 0x7EFF,
    });
}

// softTerracotta <- gui/theme.rs:-1
// ASCII 骨架: ""
QString softTerracotta()
{
    return fromCodePoints({
        0x9676, 0x571F, 0x6A59,
    });
}

// softIndigo <- gui/theme.rs:-1
// ASCII 骨架: ""
QString softIndigo()
{
    return fromCodePoints({
        0x975B, 0x84DD, 0x7D2B,
    });
}

// softViolet <- gui/theme.rs:-1
// ASCII 骨架: ""
QString softViolet()
{
    return fromCodePoints({
        0x7D2B, 0x7F57, 0x5170,
    });
}

}
