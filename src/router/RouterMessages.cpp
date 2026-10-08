// 由 gen-router-cjk.mjs 从 router.rs 生成，请勿手改。
// 这些决策原因文本要与 vpn-parser 的 Rust 实现逐字一致（会进界面提示和日志）。
#include "router/RouterMessages.h"

#include <initializer_list>

namespace Router::Messages {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

// breakerReason <- router.rs:245
// Rust 原文: "连续 {} 次验证失败，拉黑 {} 分钟",
// ASCII 骨架: " {}  {} "
QString breakerReason()
{
    return fromCodePoints({
        0x8FDE, 0x7EED, 0x0020, 0x007B, 0x007D, 0x0020, 0x6B21, 0x9A8C,
        0x8BC1, 0x5931, 0x8D25, 0xFF0C, 0x62C9, 0x9ED1, 0x0020, 0x007B,
        0x007D, 0x0020, 0x5206, 0x949F,
    });
}

// latencyDetail <- router.rs:286
// Rust 原文: "延迟 EWMA {:.0}ms 超过阈值 {}ms",
// ASCII 骨架: " EWMA {:.0}ms  {}ms"
QString latencyDetail()
{
    return fromCodePoints({
        0x5EF6, 0x8FDF, 0x0020, 0x0045, 0x0057, 0x004D, 0x0041, 0x0020,
        0x007B, 0x003A, 0x002E, 0x0030, 0x007D, 0x006D, 0x0073, 0x0020,
        0x8D85, 0x8FC7, 0x9608, 0x503C, 0x0020, 0x007B, 0x007D, 0x006D,
        0x0073,
    });
}

// bandwidthDetail <- router.rs:290
// Rust 原文: format!("下行持续低于 {:.0} KB/s", self.cfg.bandwidth_threshold_kbps)
// ASCII 骨架: " {:.0} KB/s"
QString bandwidthDetail()
{
    return fromCodePoints({
        0x4E0B, 0x884C, 0x6301, 0x7EED, 0x4F4E, 0x4E8E, 0x0020, 0x007B,
        0x003A, 0x002E, 0x0030, 0x007D, 0x0020, 0x004B, 0x0042, 0x002F,
        0x0073,
    });
}

// switchReason <- router.rs:294
// Rust 原文: reason: format!("{detail}，评分 {current_score:.0} → {target_score:.0}"),
// ASCII 骨架: "{detail} {current_score:.0}  {target_score:.0}"
QString switchReason()
{
    return fromCodePoints({
        0x007B, 0x0064, 0x0065, 0x0074, 0x0061, 0x0069, 0x006C, 0x007D,
        0xFF0C, 0x8BC4, 0x5206, 0x0020, 0x007B, 0x0063, 0x0075, 0x0072,
        0x0072, 0x0065, 0x006E, 0x0074, 0x005F, 0x0073, 0x0063, 0x006F,
        0x0072, 0x0065, 0x003A, 0x002E, 0x0030, 0x007D, 0x0020, 0x2192,
        0x0020, 0x007B, 0x0074, 0x0061, 0x0072, 0x0067, 0x0065, 0x0074,
        0x005F, 0x0073, 0x0063, 0x006F, 0x0072, 0x0065, 0x003A, 0x002E,
        0x0030, 0x007D,
    });
}

// latencyHead <- router.rs:512
// Rust 原文: assert!(reason.contains("延迟 EWMA"));
// ASCII 骨架: " EWMA"
QString latencyHead()
{
    return fromCodePoints({
        0x5EF6, 0x8FDF, 0x0020, 0x0045, 0x0057, 0x004D, 0x0041,
    });
}

// bandwidthHead <- router.rs:571
// Rust 原文: assert!(reason.contains("下行"), "{reason}");
// ASCII 骨架: ""
QString bandwidthHead()
{
    return fromCodePoints({
        0x4E0B, 0x884C,
    });
}

}
