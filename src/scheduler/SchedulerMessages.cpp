// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。
// 对应 Rust src/scheduler/mod.rs 的 tracing 日志文本。
#include "scheduler/SchedulerMessages.h"

#include <initializer_list>

namespace Scheduler {
namespace Messages {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

QString format(const QString &text, const QVector<QString> &positional,
               std::initializer_list<Scheduler::Messages::Arg> named)
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

// refillSparseLog <- scheduler/mod.rs:101 （29 个码点）
// ASCII 骨架: "scheduler: "
QString refillSparseLog()
{
    return fromCodePoints({
        0x0073, 0x0063, 0x0068, 0x0065, 0x0064, 0x0075, 0x006C, 0x0065,
        0x0072, 0x003A, 0x0020, 0x6D4B, 0x901A, 0x8282, 0x70B9, 0x592A,
        0x5C11, 0xFF0C, 0x540E, 0x53F0, 0x81EA, 0x52A8, 0x8865, 0x4E00,
        0x8F6E, 0x5168, 0x91CF, 0x6D4B, 0x901F,
    });
}

// refillDoneLog <- scheduler/mod.rs:109 （17 个码点）
// ASCII 骨架: "scheduler: "
QString refillDoneLog()
{
    return fromCodePoints({
        0x0073, 0x0063, 0x0068, 0x0065, 0x0064, 0x0075, 0x006C, 0x0065,
        0x0072, 0x003A, 0x0020, 0x81EA, 0x52A8, 0x8865, 0x6D4B, 0x5B8C,
        0x6210,
    });
}

// refillFailedLog <- scheduler/mod.rs:112 （17 个码点）
// ASCII 骨架: "scheduler: "
QString refillFailedLog()
{
    return fromCodePoints({
        0x0073, 0x0063, 0x0068, 0x0065, 0x0064, 0x0075, 0x006C, 0x0065,
        0x0072, 0x003A, 0x0020, 0x81EA, 0x52A8, 0x8865, 0x6D4B, 0x5931,
        0x8D25,
    });
}

// refillCountFailedLog <- scheduler/mod.rs:123 （21 个码点）
// ASCII 骨架: "scheduler: "
QString refillCountFailedLog()
{
    return fromCodePoints({
        0x0073, 0x0063, 0x0068, 0x0065, 0x0064, 0x0075, 0x006C, 0x0065,
        0x0072, 0x003A, 0x0020, 0x6570, 0x4E0D, 0x51FA, 0x4E00, 0x5171,
        0x6709, 0x591A, 0x5C11, 0x8282, 0x70B9,
    });
}

}
}
