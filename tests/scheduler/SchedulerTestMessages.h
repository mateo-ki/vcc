// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。
// 对应 Rust src/scheduler/mod.rs 的 #[test] 断言消息：规则读不通时，测试要先能说明为什么。
#pragma once

#include <QString>

#include <initializer_list>

namespace SchedulerTests {
namespace {
inline QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

// concurrencyNotGreedy <- scheduler/mod.rs:255 （12 个码点）
// ASCII 骨架: ""
inline QString concurrencyNotGreedy()
{
    return fromCodePoints({
        0x540E, 0x53F0, 0x8865, 0x6D4B, 0x4E0D, 0x8BB8, 0x5403, 0x6EE1,
        0x6D4B, 0x901F, 0x5E76, 0x53D1,
    });
}

// noDownloadThroughput <- scheduler/mod.rs:259 （18 个码点）
// ASCII 骨架: ""
inline QString noDownloadThroughput()
{
    return fromCodePoints({
        0x4E0B, 0x8F7D, 0x541E, 0x5410, 0x90A3, 0x4E00, 0x6BB5, 0x6700,
        0x5360, 0x5E26, 0x5BBD, 0xFF0C, 0x81EA, 0x52A8, 0x8865, 0x6D4B,
        0x4E0D, 0x505A,
    });
}

// timeoutFollowsSettings <- scheduler/mod.rs:261 （9 个码点）
// ASCII 骨架: ""
inline QString timeoutFollowsSettings()
{
    return fromCodePoints({
        0x8D85, 0x65F6, 0x4ECD, 0x8DDF, 0x968F, 0x4ED6, 0x7684, 0x8BBE,
        0x7F6E,
    });
}

// reuseRecentlyVerified <- scheduler/mod.rs:262 （10 个码点）
// ASCII 骨架: ""
inline QString reuseRecentlyVerified()
{
    return fromCodePoints({
        0x521A, 0x6D4B, 0x901A, 0x7684, 0x4E0D, 0x8BE5, 0x518D, 0x6D4B,
        0x4E00, 0x904D,
    });
}

}
