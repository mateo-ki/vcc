// 由 gen-router-cjk.mjs 从 router.rs 生成，请勿手改。
#pragma once

#include <QString>

#include <initializer_list>

namespace RouterTestCjk {
namespace detail {
inline QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

} // namespace RouterTestCjk

namespace RouterTestCjk {

// currentNodeName <- router.rs:709
// Rust 原文: node("c", "当前", Some(90)),
// ASCII 骨架: ""
inline QString currentNodeName()
{
    return detail::fromCodePoints({
        0x5F53, 0x524D,
    });
}

// midNodeName <- router.rs:710
// Rust 原文: node("b", "中等", Some(300)),
// ASCII 骨架: ""
inline QString midNodeName()
{
    return detail::fromCodePoints({
        0x4E2D, 0x7B49,
    });
}

// fastestNodeName <- router.rs:711
// Rust 原文: node("a", "最快", Some(120)),
// ASCII 骨架: ""
inline QString fastestNodeName()
{
    return detail::fromCodePoints({
        0x6700, 0x5FEB,
    });
}

// brokenNodeName <- router.rs:724
// Rust 原文: node("x", "坏的", Some(100)),
// ASCII 骨架: ""
inline QString brokenNodeName()
{
    return detail::fromCodePoints({
        0x574F, 0x7684,
    });
}

// untestedNodeName <- router.rs:725
// Rust 原文: node("y", "没测过", None),
// ASCII 骨架: ""
inline QString untestedNodeName()
{
    return detail::fromCodePoints({
        0x6CA1, 0x6D4B, 0x8FC7,
    });
}

// expiredThrottledNodeName <- router.rs:726
// Rust 原文: node("z", "到期 限速", Some(110)),
// ASCII 骨架: " "
inline QString expiredThrottledNodeName()
{
    return detail::fromCodePoints({
        0x5230, 0x671F, 0x0020, 0x9650, 0x901F,
    });
}

// usableNodeName <- router.rs:727
// Rust 原文: node("w", "能用", Some(500)),
// ASCII 骨架: ""
inline QString usableNodeName()
{
    return detail::fromCodePoints({
        0x80FD, 0x7528,
    });
}

// onlyChildNodeName <- router.rs:745
// Rust 原文: assert!(pick_backups(&[node("c", "只此一家", Some(1))], "c", &none(), "", 8).is_empty());
// ASCII 骨架: ""
inline QString onlyChildNodeName()
{
    return detail::fromCodePoints({
        0x53EA, 0x6B64, 0x4E00, 0x5BB6,
    });
}

// excludeKeywords <- router.rs:731
// Rust 原文: let picked = pick_backups(&all, "cur", &black, "到期,限速", 8);
// ASCII 骨架: ","
inline QString excludeKeywords()
{
    return detail::fromCodePoints({
        0x5230, 0x671F, 0x002C, 0x9650, 0x901F,
    });
}

}
