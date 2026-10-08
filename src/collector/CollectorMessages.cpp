// 由 tools/gen-collector-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。
// 这些文本要与 vpn-parser/src/collector/mod.rs 逐字一致（错误消息会写进 last_status、
// 任务面板和日志，两个实现读同一份库，字符串比较依赖这个格式）。
// 模板里的 {} / {host} / {error} / {attempts} 等占位符原样保留，
// 由下面的 format() 按 Rust format! 的语义替换。
#include "collector/CollectorMessages.h"

#include <initializer_list>

namespace Collector {
namespace Messages {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

QString format(const QString &text, const QVector<QString> &positional,
               std::initializer_list<Arg> named)
{
    QString out;
    out.reserve(text.size() + 32);
    qsizetype positionalIndex = 0;
    for (qsizetype index = 0; index < text.size();) {
        if (text.at(index) == QLatin1Char('{')) {
            if (text.mid(index, 2) == QLatin1String("{}")) {
                out += positionalIndex < positional.size() ? positional.at(positionalIndex)
                                                          : QString();
                ++positionalIndex;
                index += 2;
                continue;
            }
            bool matched = false;
            for (const Arg &arg : named) {
                const QString marker = QLatin1String(arg.marker);
                if (!marker.isEmpty() && text.mid(index, marker.size()) == marker) {
                    out += arg.value;
                    index += marker.size();
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }
        out += text.at(index);
        ++index;
    }
    return out;
}

// viaMirrorNote <- collector/mod.rs:242 （20 个码点）
// ASCII 骨架: " {host} "
QString viaMirrorNote()
{
    return fromCodePoints({
        0xFF08, 0x7ECF, 0x0020, 0x007B, 0x0068, 0x006F, 0x0073, 0x0074,
        0x007D, 0x0020, 0x52A0, 0x901F, 0xFF0C, 0x672A, 0x4E0E, 0x76F4,
        0x8FDE, 0x6BD4, 0x5BF9, 0xFF09,
    });
}

// directCandidatePrefix <- collector/mod.rs:412 （13 个码点）
// ASCII 骨架: ": {message}"
QString directCandidatePrefix()
{
    return fromCodePoints({
        0x76F4, 0x8FDE, 0x003A, 0x0020, 0x007B, 0x006D, 0x0065, 0x0073,
        0x0073, 0x0061, 0x0067, 0x0065, 0x007D,
    });
}

// candidateTaskFailed <- collector/mod.rs:414 （15 个码点）
// ASCII 骨架: ": {error}"
QString candidateTaskFailed()
{
    return fromCodePoints({
        0x5019, 0x9009, 0x4EFB, 0x52A1, 0x5931, 0x8D25, 0x003A, 0x0020,
        0x007B, 0x0065, 0x0072, 0x0072, 0x006F, 0x0072, 0x007D,
    });
}

// mirrorGateClosed <- collector/mod.rs:434 （8 个码点）
// ASCII 骨架: ""
QString mirrorGateClosed()
{
    return fromCodePoints({
        0x955C, 0x50CF, 0x5E76, 0x53D1, 0x95F8, 0x5DF2, 0x5173, 0x95ED,
    });
}

// hostGateClosed <- collector/mod.rs:439 （8 个码点）
// ASCII 骨架: ""
QString hostGateClosed()
{
    return fromCodePoints({
        0x4E3B, 0x673A, 0x5E76, 0x53D1, 0x95F8, 0x5DF2, 0x5173, 0x95ED,
    });
}

// responseHeaderTimeout <- collector/mod.rs:456 （31 个码点）
// ASCII 骨架: ": {}s "
QString responseHeaderTimeout()
{
    return fromCodePoints({
        0x54CD, 0x5E94, 0x5934, 0x8D85, 0x65F6, 0x003A, 0x0020, 0x007B,
        0x007D, 0x0073, 0x0020, 0x5185, 0x672A, 0x6536, 0x5230, 0x54CD,
        0x5E94, 0xFF08, 0x8FDE, 0x63A5, 0x5DF2, 0x5EFA, 0x7ACB, 0x4F46,
        0x670D, 0x52A1, 0x7AEF, 0x672A, 0x8FD4, 0x56DE, 0xFF09,
    });
}

// connectTimeoutFailed <- collector/mod.rs:462 （16 个码点）
// ASCII 骨架: "{}s "
QString connectTimeoutFailed()
{
    return fromCodePoints({
        0x8FDE, 0x63A5, 0x8D85, 0x65F6, 0xFF08, 0x007B, 0x007D, 0x0073,
        0x0020, 0x5185, 0x672A, 0x5B8C, 0x6210, 0x8FDE, 0x63A5, 0xFF09,
    });
}

// gistDiscoveryName <- collector/mod.rs:545 （7 个码点）
// ASCII 骨架: "Gist "
QString gistDiscoveryName()
{
    return fromCodePoints({
        0x0047, 0x0069, 0x0073, 0x0074, 0x0020, 0x53D1, 0x73B0,
    });
}

// gistPageNotModified <- collector/mod.rs:583 （15 个码点）
// ASCII 骨架: " {}  304"
QString gistPageNotModified()
{
    return fromCodePoints({
        0x7B2C, 0x0020, 0x007B, 0x007D, 0x0020, 0x9875, 0x8FD4, 0x56DE,
        0x0020, 0x0033, 0x0030, 0x0034, 0xFF0C, 0x505C, 0x6B62,
    });
}

// gistPageFetchFailed <- collector/mod.rs:588 （25 个码点）
// ASCII 骨架: " {} {error}"
QString gistPageFetchFailed()
{
    return fromCodePoints({
        0x7B2C, 0x0020, 0x007B, 0x007D, 0x0020, 0x9875, 0x62C9, 0x53D6,
        0x5931, 0x8D25, 0xFF08, 0x007B, 0x0065, 0x0072, 0x0072, 0x006F,
        0x0072, 0x007D, 0xFF09, 0xFF0C, 0x5DF2, 0x505C, 0x6B62, 0x626B,
        0x63CF,
    });
}

// gistPageNoGists <- collector/mod.rs:596 （16 个码点）
// ASCII 骨架: " {}  gist"
QString gistPageNoGists()
{
    return fromCodePoints({
        0x7B2C, 0x0020, 0x007B, 0x007D, 0x0020, 0x9875, 0x6CA1, 0x6709,
        0x0020, 0x0067, 0x0069, 0x0073, 0x0074, 0xFF0C, 0x505C, 0x6B62,
    });
}

// gistPageAllSeen <- collector/mod.rs:613 （15 个码点）
// ASCII 骨架: " {} "
QString gistPageAllSeen()
{
    return fromCodePoints({
        0x7B2C, 0x0020, 0x007B, 0x007D, 0x0020, 0x9875, 0x5168, 0x90E8,
        0x5DF2, 0x626B, 0x63CF, 0x8FC7, 0xFF0C, 0x505C, 0x6B62,
    });
}

// gistPageCap <- collector/mod.rs:628 （20 个码点）
// ASCII 骨架: " {max_pages}"
QString gistPageCap()
{
    return fromCodePoints({
        0x8FBE, 0x5230, 0x5355, 0x8F6E, 0x9875, 0x6570, 0x4E0A, 0x9650,
        0x0020, 0x007B, 0x006D, 0x0061, 0x0078, 0x005F, 0x0070, 0x0061,
        0x0067, 0x0065, 0x0073, 0x007D,
    });
}

// gistRegisterCap <- collector/mod.rs:632 （31 个码点）
// ASCII 骨架: " {GIST_MAX_NEW_PER_RUN}"
QString gistRegisterCap()
{
    return fromCodePoints({
        0x8FBE, 0x5230, 0x5355, 0x8F6E, 0x6CE8, 0x518C, 0x4E0A, 0x9650,
        0x0020, 0x007B, 0x0047, 0x0049, 0x0053, 0x0054, 0x005F, 0x004D,
        0x0041, 0x0058, 0x005F, 0x004E, 0x0045, 0x0057, 0x005F, 0x0050,
        0x0045, 0x0052, 0x005F, 0x0052, 0x0055, 0x004E, 0x007D,
    });
}

// gistSearchUrlUnparsable <- collector/mod.rs:636 （25 个码点）
// ASCII 骨架: "gist  URL : {error}"
QString gistSearchUrlUnparsable()
{
    return fromCodePoints({
        0x0067, 0x0069, 0x0073, 0x0074, 0x0020, 0x641C, 0x7D22, 0x0020,
        0x0055, 0x0052, 0x004C, 0x0020, 0x65E0, 0x6CD5, 0x89E3, 0x6790,
        0x003A, 0x0020, 0x007B, 0x0065, 0x0072, 0x0072, 0x006F, 0x0072,
        0x007D,
    });
}

// gistScanComplete <- collector/mod.rs:642 （4 个码点）
// ASCII 骨架: ""
QString gistScanComplete()
{
    return fromCodePoints({
        0x626B, 0x63CF, 0x5B8C, 0x6210,
    });
}

// gistDiscoveryFinished <- collector/mod.rs:645 （60 个码点）
// ASCII 骨架: "Gist  {}  {} {}  {} {} {}{}"
QString gistDiscoveryFinished()
{
    return fromCodePoints({
        0x0047, 0x0069, 0x0073, 0x0074, 0x0020, 0x53D1, 0x73B0, 0x5B8C,
        0x6210, 0xFF1A, 0x626B, 0x63CF, 0x0020, 0x007B, 0x007D, 0x0020,
        0x9875, 0xFF0C, 0x65B0, 0x589E, 0x0020, 0x007B, 0x007D, 0x0020,
        0x4E2A, 0x6E90, 0xFF08, 0x007B, 0x007D, 0x0020, 0x8282, 0x70B9,
        0xFF09, 0xFF0C, 0x8DF3, 0x8FC7, 0x5DF2, 0x6709, 0x0020, 0x007B,
        0x007D, 0xFF0C, 0x65E0, 0x8282, 0x70B9, 0x4E22, 0x5F03, 0x0020,
        0x007B, 0x007D, 0xFF0C, 0x5F85, 0x91CD, 0x8BD5, 0x0020, 0x007B,
        0x007D, 0xFF1B, 0x007B, 0x007D,
    });
}

// gistPageHandled <- collector/mod.rs:625 （12 个码点）
// ASCII 骨架: "gist "
QString gistPageHandled()
{
    return fromCodePoints({
        0x0067, 0x0069, 0x0073, 0x0074, 0x0020, 0x53D1, 0x73B0, 0x9875,
        0x5904, 0x7406, 0x5B8C, 0x6210,
    });
}

// gistRemarkAuto <- collector/mod.rs:684 （9 个码点）
// ASCII 骨架: "gist "
QString gistRemarkAuto()
{
    return fromCodePoints({
        0x0067, 0x0069, 0x0073, 0x0074, 0x0020, 0x81EA, 0x52A8, 0x53D1,
        0x73B0,
    });
}

// gistRegisterFailed <- collector/mod.rs:690 （10 个码点）
// ASCII 骨架: "gist "
QString gistRegisterFailed()
{
    return fromCodePoints({
        0x0067, 0x0069, 0x0073, 0x0074, 0x0020, 0x6E90, 0x6CE8, 0x518C,
        0x5931, 0x8D25,
    });
}

// attemptsSuffix <- collector/mod.rs:830 （22 个码点）
// ASCII 骨架: "{} {attempts} "
QString attemptsSuffix()
{
    return fromCodePoints({
        0x007B, 0x007D, 0xFF08, 0x5DF2, 0x81EA, 0x52A8, 0x5C1D, 0x8BD5,
        0x0020, 0x007B, 0x0061, 0x0074, 0x0074, 0x0065, 0x006D, 0x0070,
        0x0074, 0x0073, 0x007D, 0x0020, 0x6B21, 0xFF09,
    });
}

// degradedStreakWarn <- collector/mod.rs:841 （19 个码点）
// ASCII 骨架: ""
QString degradedStreakWarn()
{
    return fromCodePoints({
        0x8BA2, 0x9605, 0x6E90, 0x8FDE, 0x7EED, 0x7F51, 0x7EDC, 0x8D85,
        0x65F6, 0xFF0C, 0x540E, 0x7EED, 0x62C9, 0x53D6, 0x53EA, 0x5355,
        0x6B21, 0x5C1D, 0x8BD5,
    });
}

// recordOutcomeFailed <- collector/mod.rs:844 （8 个码点）
// ASCII 骨架: ""
QString recordOutcomeFailed()
{
    return fromCodePoints({
        0x8BB0, 0x5F55, 0x62C9, 0x53D6, 0x7ED3, 0x679C, 0x5931, 0x8D25,
    });
}

// subtaskExited <- collector/mod.rs:959 （22 个码点）
// ASCII 骨架: ""
QString subtaskExited()
{
    return fromCodePoints({
        0x62C9, 0x53D6, 0x5B50, 0x4EFB, 0x52A1, 0x5F02, 0x5E38, 0x9000,
        0x51FA, 0xFF0C, 0x5DF2, 0x7ED3, 0x675F, 0x8BE5, 0x4EFB, 0x52A1,
        0xFF0C, 0x53EF, 0x70B9, 0x51FB, 0x91CD, 0x8BD5,
    });
}

} // namespace Messages
} // namespace Collector
