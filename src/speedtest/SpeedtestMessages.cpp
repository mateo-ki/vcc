// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。
// 这些文本要与 vpn-parser/src/speedtest/mod.rs 逐字一致：档位文案进界面，测速消息进
// latency message 与日志，两个实现读同一份库，比较字符串时依赖这些文本。
// 模板里的 {} / {latency_ms} / {detail} 等占位符原样保留，由调用方按 Rust format! 语义替换。
#include "speedtest/SpeedtestMessages.h"

#include <initializer_list>

namespace Speedtest {
namespace Messages {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

QString format(const QString &text, const QVector<QString> &positional,
               std::initializer_list<Speedtest::Messages::Arg> named)
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

// fastLabel <- speedtest/mod.rs:34 （11 个码点）
// ASCII 骨架: "  ()"
QString fastLabel()
{
    return fromCodePoints({
        0x26A1, 0x0020, 0x6781, 0x901F, 0x6A21, 0x5F0F, 0x0020, 0x0028,
        0x8F7B, 0x5EA6, 0x0029,
    });
}

// standardLabel <- speedtest/mod.rs:35 （12 个码点）
// ASCII 骨架: "  ()"
QString standardLabel()
{
    return fromCodePoints({
        0x2696, 0xFE0F, 0x0020, 0x6807, 0x51C6, 0x6A21, 0x5F0F, 0x0020,
        0x0028, 0x5E73, 0x8861, 0x0029,
    });
}

// deepLabel <- speedtest/mod.rs:36 （11 个码点）
// ASCII 骨架: "  ()"
QString deepLabel()
{
    return fromCodePoints({
        0x1F52C, 0x0020, 0x6DF1, 0x5EA6, 0x6A21, 0x5F0F, 0x0020, 0x0028,
        0x4E25, 0x82DB, 0x0029,
    });
}

// fastDescription <- speedtest/mod.rs:42 （29 个码点）
// ASCII 骨架: " 256 500ms"
QString fastDescription()
{
    return fromCodePoints({
        0x5E76, 0x53D1, 0x0020, 0x0032, 0x0035, 0x0036, 0xFF0C, 0x77ED,
        0x8D85, 0x65F6, 0x0020, 0x0035, 0x0030, 0x0030, 0x006D, 0x0073,
        0xFF0C, 0x6781, 0x901F, 0x7B5B, 0x9664, 0x5927, 0x91CF, 0x79BB,
        0x7EBF, 0x50F5, 0x5C38, 0x8282, 0x70B9,
    });
}

// standardDescription <- speedtest/mod.rs:43 （36 个码点）
// ASCII 骨架: " 128 1500msTCP  + "
QString standardDescription()
{
    return fromCodePoints({
        0x5E76, 0x53D1, 0x0020, 0x0031, 0x0032, 0x0038, 0xFF0C, 0x6807,
        0x51C6, 0x8D85, 0x65F6, 0x0020, 0x0031, 0x0035, 0x0030, 0x0030,
        0x006D, 0x0073, 0xFF0C, 0x0054, 0x0043, 0x0050, 0x0020, 0x9884,
        0x7B5B, 0x0020, 0x002B, 0x0020, 0x771F, 0x5B9E, 0x5185, 0x6838,
        0x4EE3, 0x7406, 0x63E1, 0x624B,
    });
}

// deepDescription <- speedtest/mod.rs:44 （29 个码点）
// ASCII 骨架: " 64 3500ms"
QString deepDescription()
{
    return fromCodePoints({
        0x5E76, 0x53D1, 0x0020, 0x0036, 0x0034, 0xFF0C, 0x9AD8, 0x5BB9,
        0x5FCD, 0x957F, 0x8D85, 0x65F6, 0x0020, 0x0033, 0x0035, 0x0030,
        0x0030, 0x006D, 0x0073, 0xFF0C, 0x4E25, 0x82DB, 0x6821, 0x9A8C,
        0x771F, 0x5B9E, 0x53EF, 0x7528, 0x6027,
    });
}

// downloadUnmeasurable <- speedtest/mod.rs:170 （34 个码点）
// ASCII 骨架: "{latency_ms} ms /  ({detail})"
QString downloadUnmeasurable()
{
    return fromCodePoints({
        0x007B, 0x006C, 0x0061, 0x0074, 0x0065, 0x006E, 0x0063, 0x0079,
        0x005F, 0x006D, 0x0073, 0x007D, 0x0020, 0x006D, 0x0073, 0x0020,
        0x002F, 0x0020, 0x4E0B, 0x8F7D, 0x4E0D, 0x53EF, 0x6D4B, 0x0020,
        0x0028, 0x007B, 0x0064, 0x0065, 0x0074, 0x0061, 0x0069, 0x006C,
        0x007D, 0x0029,
    });
}

// networkGateClosed <- speedtest/mod.rs:513 （9 个码点）
// ASCII 骨架: ""
QString networkGateClosed()
{
    return fromCodePoints({
        0x6D4B, 0x901F, 0x5E76, 0x53D1, 0x63A7, 0x5236, 0x5DF2, 0x5173,
        0x95ED,
    });
}

// noNodesToTest <- speedtest/mod.rs:285 （7 个码点）
// ASCII 骨架: ""
QString noNodesToTest()
{
    return fromCodePoints({
        0x6CA1, 0x6709, 0x53EF, 0x6D4B, 0x901F, 0x8282, 0x70B9,
    });
}

// noNodesOrAllFailed <- speedtest/mod.rs:1031 （18 个码点）
// ASCII 骨架: ""
QString noNodesOrAllFailed()
{
    return fromCodePoints({
        0x6CA1, 0x6709, 0x53EF, 0x6D4B, 0x901F, 0x8282, 0x70B9, 0xFF0C,
        0x6216, 0x6240, 0x6709, 0x6D4B, 0x901F, 0x4EFB, 0x52A1, 0x5747,
        0x5931, 0x8D25,
    });
}

// googleVerifyDone <- speedtest/mod.rs:411 （32 个码点）
// ASCII 骨架: " Google  {ok} {fail}"
QString googleVerifyDone()
{
    return fromCodePoints({
        0x4E25, 0x683C, 0x0020, 0x0047, 0x006F, 0x006F, 0x0067, 0x006C,
        0x0065, 0x0020, 0x6821, 0x9A8C, 0x5B8C, 0x6210, 0xFF1A, 0x6210,
        0x529F, 0x0020, 0x007B, 0x006F, 0x006B, 0x007D, 0xFF0C, 0x5931,
        0x8D25, 0x0020, 0x007B, 0x0066, 0x0061, 0x0069, 0x006C, 0x007D,
    });
}

// onlyReceivedBytes <- speedtest/mod.rs:1206 （14 个码点）
// ASCII 骨架: " {total} "
QString onlyReceivedBytes()
{
    return fromCodePoints({
        0x4EC5, 0x6536, 0x5230, 0x0020, 0x007B, 0x0074, 0x006F, 0x0074,
        0x0061, 0x006C, 0x007D, 0x0020, 0x5B57, 0x8282,
    });
}

// publicWebsiteNote <- speedtest/mod.rs:1212 （36 个码点）
// ASCII 骨架: " HTTP  ({total} bytes)"
QString publicWebsiteNote()
{
    return fromCodePoints({
        0x7591, 0x4F3C, 0x666E, 0x901A, 0x7F51, 0x7AD9, 0x0020, 0x0048,
        0x0054, 0x0054, 0x0050, 0x0020, 0x54CD, 0x5E94, 0xFF0C, 0x4E0D,
        0x8BA1, 0x5165, 0x4E0B, 0x8F7D, 0x0020, 0x0028, 0x007B, 0x0074,
        0x006F, 0x0074, 0x0061, 0x006C, 0x007D, 0x0020, 0x0062, 0x0079,
        0x0074, 0x0065, 0x0073, 0x0029,
    });
}

// reusedVerifiedLog <- speedtest/mod.rs:466 （17 个码点）
// ASCII 骨架: ""
QString reusedVerifiedLog()
{
    return fromCodePoints({
        0x6CBF, 0x7528, 0x8FD1, 0x671F, 0x5DF2, 0x9A8C, 0x8BC1, 0x7684,
        0x8282, 0x70B9, 0x7ED3, 0x679C, 0xFF0C, 0x8DF3, 0x8FC7, 0x91CD,
        0x6D4B,
    });
}

// downloadTopMeasuredLog <- speedtest/mod.rs:692 （12 个码点）
// ASCII 骨架: ""
QString downloadTopMeasuredLog()
{
    return fromCodePoints({
        0x6309, 0x5EF6, 0x8FDF, 0x6392, 0x540D, 0x8865, 0x6D4B, 0x4E86,
        0x4E0B, 0x8F7D, 0x901F, 0x7387,
    });
}

}
}
