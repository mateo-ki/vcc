#include "collector/Telegram.h"

#include <QByteArray>
#include <QChar>
#include <QLatin1String>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "core/Crypto.h"
#include "db/Database.h"
#include "parser/Common.h"
#include "parser/Subscription.h"

// ==== BEGIN GENERATED: tools/gen-telegram-cjk.mjs（勿手改，重跑脚本即可再生成）====
// telegram.rs 里的用户可见文本与正则模式。模板中的 {} / {pages_read} 等占位符原样保留，
// 由 formatText() 按 Rust format! 的语义替换。
namespace Collector::Telegram::Messages {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

// patternBody <- telegram.rs:34（69 个码点）
// ASCII 骨架: "(?s)class=\"tgme_widget_message_text js-message_text\"[^>]*>(.*?)</div>"
QString patternBody()
{
    return fromCodePoints({
        0x0028, 0x003F, 0x0073, 0x0029, 0x0063, 0x006C, 0x0061, 0x0073,
        0x0073, 0x003D, 0x0022, 0x0074, 0x0067, 0x006D, 0x0065, 0x005F,
        0x0077, 0x0069, 0x0064, 0x0067, 0x0065, 0x0074, 0x005F, 0x006D,
        0x0065, 0x0073, 0x0073, 0x0061, 0x0067, 0x0065, 0x005F, 0x0074,
        0x0065, 0x0078, 0x0074, 0x0020, 0x006A, 0x0073, 0x002D, 0x006D,
        0x0065, 0x0073, 0x0073, 0x0061, 0x0067, 0x0065, 0x005F, 0x0074,
        0x0065, 0x0078, 0x0074, 0x0022, 0x005B, 0x005E, 0x003E, 0x005D,
        0x002A, 0x003E, 0x0028, 0x002E, 0x002A, 0x003F, 0x0029, 0x003C,
        0x002F, 0x0064, 0x0069, 0x0076, 0x003E,
    });
}

// patternTags <- telegram.rs:37（11 个码点）
// ASCII 骨架: "(?s)<[^>]+>"
QString patternTags()
{
    return fromCodePoints({
        0x0028, 0x003F, 0x0073, 0x0029, 0x003C, 0x005B, 0x005E, 0x003E,
        0x005D, 0x002B, 0x003E,
    });
}

// patternPostId <- telegram.rs:39（33 个码点）
// ASCII 骨架: "data-post=\"([A-Za-z0-9_]+)/(\\d+)\""
QString patternPostId()
{
    return fromCodePoints({
        0x0064, 0x0061, 0x0074, 0x0061, 0x002D, 0x0070, 0x006F, 0x0073,
        0x0074, 0x003D, 0x0022, 0x0028, 0x005B, 0x0041, 0x002D, 0x005A,
        0x0061, 0x002D, 0x007A, 0x0030, 0x002D, 0x0039, 0x005F, 0x005D,
        0x002B, 0x0029, 0x002F, 0x0028, 0x005C, 0x0064, 0x002B, 0x0029,
        0x0022,
    });
}

// patternHref <- telegram.rs:41（18 个码点）
// ASCII 骨架: "(?i)href=\"([^\"]+)\""
QString patternHref()
{
    return fromCodePoints({
        0x0028, 0x003F, 0x0069, 0x0029, 0x0068, 0x0072, 0x0065, 0x0066,
        0x003D, 0x0022, 0x0028, 0x005B, 0x005E, 0x0022, 0x005D, 0x002B,
        0x0029, 0x0022,
    });
}

// patternNodeUri <- telegram.rs:44（125 个码点）
// ASCII 骨架: "(?ix)\\b(\n              (?:ss|ssr|vmess|vless|trojan|tuic|hysteria2|hy2|socks5)\n              ://[^\\s\"'<>,;)]+\n          )"
QString patternNodeUri()
{
    return fromCodePoints({
        0x0028, 0x003F, 0x0069, 0x0078, 0x0029, 0x005C, 0x0062, 0x0028,
        0x000A, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
        0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0028,
        0x003F, 0x003A, 0x0073, 0x0073, 0x007C, 0x0073, 0x0073, 0x0072,
        0x007C, 0x0076, 0x006D, 0x0065, 0x0073, 0x0073, 0x007C, 0x0076,
        0x006C, 0x0065, 0x0073, 0x0073, 0x007C, 0x0074, 0x0072, 0x006F,
        0x006A, 0x0061, 0x006E, 0x007C, 0x0074, 0x0075, 0x0069, 0x0063,
        0x007C, 0x0068, 0x0079, 0x0073, 0x0074, 0x0065, 0x0072, 0x0069,
        0x0061, 0x0032, 0x007C, 0x0068, 0x0079, 0x0032, 0x007C, 0x0073,
        0x006F, 0x0063, 0x006B, 0x0073, 0x0035, 0x0029, 0x000A, 0x0020,
        0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
        0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x003A, 0x002F, 0x002F,
        0x005B, 0x005E, 0x005C, 0x0073, 0x0022, 0x0027, 0x003C, 0x003E,
        0xFF0C, 0x002C, 0xFF1B, 0x003B, 0xFF09, 0x0029, 0xFF08, 0x005D,
        0x002B, 0x000A, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
        0x0020, 0x0020, 0x0020, 0x0020, 0x0029,
    });
}

// patternHttpUrl <- telegram.rs:52（26 个码点）
// ASCII 骨架: "https?://[^\\s\"'<>,;)]+"
QString patternHttpUrl()
{
    return fromCodePoints({
        0x0068, 0x0074, 0x0074, 0x0070, 0x0073, 0x003F, 0x003A, 0x002F,
        0x002F, 0x005B, 0x005E, 0x005C, 0x0073, 0x0022, 0x0027, 0x003C,
        0x003E, 0xFF0C, 0x002C, 0xFF1B, 0x003B, 0xFF09, 0x0029, 0xFF08,
        0x005D, 0x002B,
    });
}

// patternEntity <- telegram.rs:54（28 个码点）
// ASCII 骨架: "&#(?:x([0-9a-fA-F]+)|(\\d+));"
QString patternEntity()
{
    return fromCodePoints({
        0x0026, 0x0023, 0x0028, 0x003F, 0x003A, 0x0078, 0x0028, 0x005B,
        0x0030, 0x002D, 0x0039, 0x0061, 0x002D, 0x0066, 0x0041, 0x002D,
        0x0046, 0x005D, 0x002B, 0x0029, 0x007C, 0x0028, 0x005C, 0x0064,
        0x002B, 0x0029, 0x0029, 0x003B,
    });
}

// egressInApp <- telegram.rs:272（5 个码点）
// ASCII 骨架: ""
QString egressInApp()
{
    return fromCodePoints({
        0x5E94, 0x7528, 0x5185, 0x4EE3, 0x7406,
    });
}

// egressSystem <- telegram.rs:287（4 个码点）
// ASCII 骨架: ""
QString egressSystem()
{
    return fromCodePoints({
        0x7CFB, 0x7EDF, 0x4EE3, 0x7406,
    });
}

// systemProxyUnusable <- telegram.rs:294（12 个码点）
// ASCII 骨架: ""
QString systemProxyUnusable()
{
    return fromCodePoints({
        0x7CFB, 0x7EDF, 0x4EE3, 0x7406, 0x5730, 0x5740, 0x65E0, 0x6CD5,
        0x7528, 0x4F5C, 0x51FA, 0x53E3,
    });
}

// needEgress <- telegram.rs:303（73 个码点）
// ASCII 骨架: "Telegram "
QString needEgress()
{
    return fromCodePoints({
        0x0054, 0x0065, 0x006C, 0x0065, 0x0067, 0x0072, 0x0061, 0x006D,
        0x0020, 0x9891, 0x9053, 0x9700, 0x8981, 0x51FA, 0x53E3, 0xFF1A,
        0x8FD9, 0x4E00, 0x8F6E, 0x65E2, 0x6CA1, 0x8D70, 0x5E94, 0x7528,
        0x5185, 0x4EE3, 0x7406, 0xFF0C, 0x7CFB, 0x7EDF, 0x4EE3, 0x7406,
        0x4E5F, 0x6CA1, 0x5F00, 0x3002, 0x8BF7, 0x5728, 0x63A7, 0x5236,
        0x53F0, 0x542F, 0x52A8, 0x4EE3, 0x7406, 0x540E, 0x7528, 0x300C,
        0x4F7F, 0x7528, 0x4EE3, 0x7406, 0x62C9, 0x53D6, 0x300D, 0xFF0C,
        0x6216, 0x628A, 0x7CFB, 0x7EDF, 0x4EE3, 0x7406, 0x6307, 0x5411,
        0x4E00, 0x4E2A, 0x80FD, 0x51FA, 0x5883, 0x5916, 0x7684, 0x5730,
        0x5740,
    });
}

// notModified <- telegram.rs:346（29 个码点）
// ASCII 骨架: "not modified  "
QString notModified()
{
    return fromCodePoints({
        0x006E, 0x006F, 0x0074, 0x0020, 0x006D, 0x006F, 0x0064, 0x0069,
        0x0066, 0x0069, 0x0065, 0x0064, 0x0020, 0x00B7, 0x0020, 0x9875,
        0x9762, 0x6CA1, 0x53D8, 0x5316, 0xFF0C, 0x8FD9, 0x4E00, 0x8F6E,
        0x4E0D, 0x91CD, 0x590D, 0x89E3, 0x6790,
    });
}

// contentUnchanged <- telegram.rs:372（39 个码点）
// ASCII 骨架: "content unchanged  {} "
QString contentUnchanged()
{
    return fromCodePoints({
        0x0063, 0x006F, 0x006E, 0x0074, 0x0065, 0x006E, 0x0074, 0x0020,
        0x0075, 0x006E, 0x0063, 0x0068, 0x0061, 0x006E, 0x0067, 0x0065,
        0x0064, 0x0020, 0x00B7, 0x0020, 0x007B, 0x007D, 0x0020, 0x6761,
        0x6B63, 0x6587, 0x548C, 0x4E0A, 0x6B21, 0x4E00, 0x6A21, 0x4E00,
        0x6837, 0xFF0C, 0x6CA1, 0x6709, 0x65B0, 0x4E1C, 0x897F,
    });
}

// failureNote <- telegram.rs:461（4 个码点）
// ASCII 骨架: "{}"
QString failureNote()
{
    return fromCodePoints({
        0xFF08, 0x007B, 0x007D, 0xFF09,
    });
}

// zeroNodes <- telegram.rs:466（91 个码点）
// ASCII 骨架: " {pages_read}  {}  {hit}/{tried}{note}"
QString zeroNodes()
{
    return fromCodePoints({
        0x8BFB, 0x4E86, 0x0020, 0x007B, 0x0070, 0x0061, 0x0067, 0x0065,
        0x0073, 0x005F, 0x0072, 0x0065, 0x0061, 0x0064, 0x007D, 0x0020,
        0x9875, 0x0020, 0x007B, 0x007D, 0x0020, 0x6761, 0x6B63, 0x6587,
        0xFF0C, 0x4E00, 0x4E2A, 0x8282, 0x70B9, 0x90FD, 0x6CA1, 0x89E3,
        0x51FA, 0x6765, 0xFF1A, 0x8FD9, 0x4E2A, 0x9891, 0x9053, 0x53EF,
        0x80FD, 0x628A, 0x8282, 0x70B9, 0x653E, 0x5728, 0x56FE, 0x7247,
        0x91CC, 0x3001, 0x85CF, 0x5728, 0x6309, 0x94AE, 0x540E, 0x9762,
        0xFF0C, 0x6216, 0x8005, 0x53EA, 0x53D1, 0x53E3, 0x4EE4, 0xFF1B,
        0x8DDF, 0x8FDB, 0x7684, 0x8BA2, 0x9605, 0x94FE, 0x63A5, 0x0020,
        0x007B, 0x0068, 0x0069, 0x0074, 0x007D, 0x002F, 0x007B, 0x0074,
        0x0072, 0x0069, 0x0065, 0x0064, 0x007D, 0x007B, 0x006E, 0x006F,
        0x0074, 0x0065, 0x007D,
    });
}

// okSummary <- telegram.rs:492（113 个码点）
// ASCII 骨架: "ok  {egress}  {pages_read}  {}    {}    {hit}/{tried}{note}   {round}   {stored}"
QString okSummary()
{
    return fromCodePoints({
        0x006F, 0x006B, 0x0020, 0x00B7, 0x0020, 0x7ECF, 0x007B, 0x0065,
        0x0067, 0x0072, 0x0065, 0x0073, 0x0073, 0x007D, 0x0020, 0x00B7,
        0x0020, 0x007B, 0x0070, 0x0061, 0x0067, 0x0065, 0x0073, 0x005F,
        0x0072, 0x0065, 0x0061, 0x0064, 0x007D, 0x0020, 0x9875, 0x0020,
        0x007B, 0x007D, 0x0020, 0x6761, 0x6B63, 0x6587, 0x0020, 0x00B7,
        0x0020, 0x88F8, 0x94FE, 0x63A5, 0x0020, 0x007B, 0x007D, 0x0020,
        0x4E2A, 0x0020, 0x00B7, 0x0020, 0x8BA2, 0x9605, 0x94FE, 0x63A5,
        0x0020, 0x007B, 0x0068, 0x0069, 0x0074, 0x007D, 0x002F, 0x007B,
        0x0074, 0x0072, 0x0069, 0x0065, 0x0064, 0x007D, 0x007B, 0x006E,
        0x006F, 0x0074, 0x0065, 0x007D, 0x0020, 0x00B7, 0x0020, 0x672C,
        0x8F6E, 0x89E3, 0x51FA, 0x0020, 0x007B, 0x0072, 0x006F, 0x0075,
        0x006E, 0x0064, 0x007D, 0x0020, 0x00B7, 0x0020, 0x5206, 0x7247,
        0x603B, 0x8BA1, 0x0020, 0x007B, 0x0073, 0x0074, 0x006F, 0x0072,
        0x0065, 0x0064, 0x007D, 0xFF08, 0x53EA, 0x589E, 0x4E0D, 0x5220,
        0xFF09,
    });
}

} // namespace Collector::Telegram::Messages
// ==== END GENERATED: tools/gen-telegram-cjk.mjs ====

namespace Collector::Telegram {
namespace {

using Parser::Common::isUnicodeSpace;
using Parser::Common::toAsciiLower;
using Parser::Common::utf8Length;

/// Rust 的 `String::from_utf8_lossy`：非法字节替换成 U+FFFD。Qt 的 fromUtf8 同义
/// （和 export/QualityExport.cpp、platform/Firewall.cpp 里的处理保持一致）。
QString lossyText(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes);
}

/// 对应 collector/mod.rs 的 `fn url_host`：本文件只在失败消息里用它取 host。
std::optional<QString> urlHost(const QString &url)
{
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid()) {
        return std::nullopt;
    }
    const QString host = parsed.host();
    if (host.isEmpty()) {
        return std::nullopt;
    }
    return toAsciiLower(host);
}

/// Rust 的 `budget.saturating_sub(started.elapsed())`。
std::chrono::milliseconds saturatingSub(std::chrono::milliseconds budget,
                                        std::chrono::milliseconds elapsed)
{
    return budget > elapsed ? budget - elapsed : std::chrono::milliseconds(0);
}

/// Rust 的 `Lazy<Regex>`：第一次用到才编译，这里同样用函数内 static。
/// 模式文本由 tools/gen-telegram-cjk.mjs 从 telegram.rs 逐字抽出来（含正则里的中文标点），
/// UseUnicodePropertiesOption 让 \b \s \d 的语义与 Rust 的 Unicode 版一致。
const QRegularExpression &bodyRegex()
{
    static const QRegularExpression re(Messages::patternBody(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression &tagsRegex()
{
    static const QRegularExpression re(Messages::patternTags(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression &postIdRegex()
{
    static const QRegularExpression re(Messages::patternPostId(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression &hrefRegex()
{
    static const QRegularExpression re(Messages::patternHref(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression &nodeUriRegex()
{
    static const QRegularExpression re(Messages::patternNodeUri(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression &httpUrlRegex()
{
    static const QRegularExpression re(Messages::patternHttpUrl(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression &entityRegex()
{
    static const QRegularExpression re(Messages::patternEntity(),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

/// Rust 的 `str::trim()`：两端按 Unicode White_Space 裁。
QString trimUnicode(const QString &text)
{
    qsizetype start = 0;
    qsizetype end = text.size();
    while (start < end && isUnicodeSpace(text.at(start))) {
        start += 1;
    }
    while (end > start && isUnicodeSpace(text.at(end - 1))) {
        end -= 1;
    }
    return text.mid(start, end - start);
}

/// Rust 的 `trim_end_matches([')', '，', ',', '。', ';', '；'])`：尾部这几个字符反复剥掉。
/// 中文标点一律按码点写，免得转录时被静默换成别的字符。
QString trimTailPunctuation(const QString &text)
{
    static const QChar kTail[] = {
        QChar(0x0029), // )
        QChar(0xFF0C), // 全角逗号
        QChar(0x002C), // ,
        QChar(0x3002), // 句号
        QChar(0x003B), // ;
        QChar(0xFF1B), // 全角分号
    };
    QString out = text;
    while (!out.isEmpty()) {
        bool matched = false;
        for (const QChar ch : kTail) {
            if (out.at(out.size() - 1) == ch) {
                out.chop(1);
                matched = true;
                break;
            }
        }
        if (!matched) {
            break;
        }
    }
    return out;
}

/// Rust 的 `u32::from_str_radix(text, 16)`：只接受 ASCII 十六进制，溢出算失败。
bool parseHexU32(const QString &text, quint32 &out)
{
    if (text.isEmpty()) {
        return false;
    }
    quint64 value = 0;
    for (const QChar ch : text) {
        const ushort unit = ch.unicode();
        int digit = -1;
        if (unit >= u'0' && unit <= u'9') {
            digit = unit - u'0';
        } else if (unit >= u'a' && unit <= u'f') {
            digit = unit - u'a' + 10;
        } else if (unit >= u'A' && unit <= u'F') {
            digit = unit - u'A' + 10;
        }
        if (digit < 0) {
            return false;
        }
        value = value * 16u + quint64(digit);
        if (value > 0xFFFFFFFFu) {
            return false;
        }
    }
    out = quint32(value);
    return true;
}

/// Rust 的 `text.parse::<u32>()`：必须全是 ASCII 十进制数字，溢出算失败。
bool parseDecimalU32(const QString &text, quint32 &out)
{
    if (text.isEmpty()) {
        return false;
    }
    quint64 value = 0;
    for (const QChar ch : text) {
        const ushort unit = ch.unicode();
        if (unit < u'0' || unit > u'9') {
            return false;
        }
        value = value * 10u + quint64(unit - u'0');
        if (value > 0xFFFFFFFFu) {
            return false;
        }
    }
    out = quint32(value);
    return true;
}

/// Rust 的 `captures[2].parse::<u64>()`：同样只吃 ASCII 十进制，溢出算失败。
bool parseDecimalU64(const QString &text, quint64 &out)
{
    if (text.isEmpty()) {
        return false;
    }
    quint64 value = 0;
    for (const QChar ch : text) {
        const ushort unit = ch.unicode();
        if (unit < u'0' || unit > u'9') {
            return false;
        }
        const quint64 digit = quint64(unit - u'0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    out = value;
    return true;
}

/// Rust 的 `char::from_u32(code)`：代理区间与超出 U+10FFFF 都不是合法码点。
bool isValidCodePoint(quint32 code)
{
    if (code > 0x10FFFFu) {
        return false;
    }
    return !(code >= 0xD800u && code <= 0xDFFFu);
}

/// format! 的占位符：`{}` 按给出的顺序依次顶替，`{name}` 按名字匹配，
/// 匹配不上的花括号原样留着。
struct Placeholder {
    const char *marker;
    QString value;
};

QString formatText(const QString &text, std::initializer_list<Placeholder> args)
{
    const QVector<Placeholder> named(args.begin(), args.end());
    QStringList positional;
    for (const Placeholder &arg : named) {
        if (std::strcmp(arg.marker, "{}") == 0) {
            positional.append(arg.value);
        }
    }
    QString out;
    out.reserve(text.size() + 16);
    qsizetype next = 0;
    for (qsizetype index = 0; index < text.size();) {
        const QChar ch = text.at(index);
        bool consumed = false;
        if (ch == QLatin1Char('{')) {
            const qsizetype close = text.indexOf(QLatin1Char('}'), index + 1);
            if (close > index) {
                const QString name = text.mid(index + 1, close - index - 1);
                if (name.isEmpty()) {
                    if (next < positional.size()) {
                        out += positional.at(next);
                        next += 1;
                        index = close + 1;
                        continue;
                    }
                } else {
                    // args 里的具名占位符带着花括号（"{egress}"），模板里读到的名字不带，
                    // 所以按带花括号的形式比。
                    const QString braced = QLatin1Char('{') + name + QLatin1Char('}');
                    for (const Placeholder &arg : named) {
                        if (braced == QString::fromLatin1(arg.marker)) {
                            out += arg.value;
                            index = close + 1;
                            consumed = true;
                            break;
                        }
                    }
                    if (consumed) {
                        continue;
                    }
                }
            }
        }
        out += ch;
        index += 1;
    }
    return out;
}

void reportPhase(const TelegramContext &context, const QString &taskId, const QString &sourceId,
                 TaskPhase phase)
{
    if (context.phase) {
        context.phase(taskId, sourceId, phase);
    }
}

void reportNodes(const TelegramContext &context, const QString &taskId, const QString &sourceId,
                 int nodeCount)
{
    if (context.nodes) {
        context.nodes(taskId, sourceId, nodeCount);
    }
}

} // namespace

// ---- 与 Collector 无关的纯函数（Rust 里是模块级 fn）----

QString validChannel(const QString &name)
{
    // Telegram 用户名规则：首字符是字母，总长 5–32（按 UTF-8 字节数算，和 Rust 的 str::len 一致），
    // 只允许字母数字下划线。
    const qsizetype bytes = utf8Length(name);
    if (bytes < 5 || bytes > 32) {
        return QString();
    }
    const QByteArray raw = name.toUtf8();
    const char first = raw.at(0);
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
        return QString();
    }
    for (const char byte : raw) {
        const bool ok = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '_';
        if (!ok) {
            return QString();
        }
    }
    return name;
}

QString channelName(const QString &url)
{
    const QString trimmed = trimUnicode(url);
    if (trimmed.isEmpty()) {
        return QString();
    }
    if (trimmed.at(0) == QLatin1Char('@')) {
        return validChannel(trimmed.mid(1));
    }
    QString withScheme = trimmed;
    if (!trimmed.contains(QLatin1String("://"))) {
        QString stripped = trimmed;
        qsizetype lead = 0;
        while (lead < stripped.size() && stripped.at(lead) == QLatin1Char('/')) {
            lead += 1;
        }
        stripped.remove(0, lead);
        withScheme = QStringLiteral("https://") + stripped;
    }
    const QUrl parsed(withScheme, QUrl::StrictMode);
    if (!parsed.isValid()) {
        return QString();
    }
    const QString host = toAsciiLower(parsed.host());
    if (host.isEmpty()) {
        return QString();
    }
    if (host != QLatin1String("t.me") && host != QLatin1String("telegram.me")
        && host != QLatin1String("telegram.org") && host != QLatin1String("telegram.dog")
        && !host.endsWith(QLatin1String(".t.me"))) {
        return QString();
    }
    QStringList segments;
    // Rust 的 `parsed.path_segments()?.filter(|s| !s.is_empty())`：空段一律丢掉。
    const QStringList rawSegments = parsed.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : rawSegments) {
        if (!segment.isEmpty()) {
            segments.append(segment);
        }
    }
    QString name;
    if (segments.isEmpty()) {
        return QString();
    }
    if (segments.first() == QLatin1String("s")) {
        if (segments.size() < 2) {
            return QString();
        }
        name = segments.at(1);
    } else {
        name = segments.first();
    }
    if (name == QLatin1String("joinchat") || name == QLatin1String("proxy")
        || name == QLatin1String("addstickers") || name == QLatin1String("addchain")
        || name == QLatin1String("addlist") || name == QLatin1String("share")
        || name == QLatin1String("iv") || name == QLatin1String("login")
        || name == QLatin1String("setlanguage") || name == QLatin1String("telegram")
        || name == QLatin1String("blog") || name == QLatin1String("faq")
        || name == QLatin1String("apps") || name == QLatin1String("premium")
        || name == QLatin1String("socks")
        || name.startsWith(QLatin1Char('+'))) {
        return QString();
    }
    return validChannel(name);
}

QStringList messageBlocks(const QString &html)
{
    QStringList blocks;
    auto matches = bodyRegex().globalMatch(html);
    while (matches.hasNext()) {
        blocks.append(matches.next().captured(1));
    }
    return blocks;
}

QVector<qulonglong> messageIds(const QString &html)
{
    QVector<qulonglong> ids;
    auto matches = postIdRegex().globalMatch(html);
    while (matches.hasNext()) {
        quint64 value = 0;
        if (parseDecimalU64(matches.next().captured(2), value)) {
            ids.append(qulonglong(value));
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

QString decodeEntities(const QString &text)
{
    // Rust 的顺序：五个具名实体 → 数字实体 → 最后才 &amp;。反过来会把 &amp;lt; 直接解成 <。
    QString out = text;
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));

    {
        QString replaced;
        replaced.reserve(out.size());
        qsizetype cursor = 0;
        auto matches = entityRegex().globalMatch(out);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            replaced += out.mid(cursor, match.capturedStart() - cursor);
            cursor = match.capturedEnd();
            // char::from_u32(code.unwrap_or(0xfffd)).unwrap_or('\u{fffd}')
            quint32 code = 0xFFFDu;
            bool haveCode = false;
            if (!match.captured(1).isEmpty()) {
                haveCode = parseHexU32(match.captured(1), code);
            }
            if (!haveCode && !match.captured(2).isEmpty()) {
                haveCode = parseDecimalU32(match.captured(2), code);
            }
            if (!haveCode || !isValidCodePoint(code)) {
                code = 0xFFFDu;
            }
            const char32_t unit = char32_t(code);
            replaced += QString::fromUcs4(&unit, 1);
        }
        replaced += out.mid(cursor);
        out = replaced;
    }
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return out;
}

QString plainText(const QString &block)
{
    // 把消息正文拍成纯文本：<br/> 转换行，其余标签丢掉，实体最后才解 ——
    // 先解实体会把正文里的 &lt;b&gt; 当成标签吃掉。
    QString withoutBreaks = block;
    withoutBreaks.replace(QStringLiteral("<br/>"), QStringLiteral("\n"));
    withoutBreaks.replace(QStringLiteral("<br />"), QStringLiteral("\n"));
    withoutBreaks.replace(QStringLiteral("<br>"), QStringLiteral("\n"));

    QString stripped;
    stripped.reserve(withoutBreaks.size());
    {
        qsizetype cursor = 0;
        auto matches = tagsRegex().globalMatch(withoutBreaks);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            stripped += withoutBreaks.mid(cursor, match.capturedStart() - cursor);
            cursor = match.capturedEnd();
        }
        stripped += withoutBreaks.mid(cursor);
    }
    return decodeEntities(stripped);
}

void pushUnique(QStringList &list, const QString &value)
{
    const QString trimmed = trimTailPunctuation(trimUnicode(value));
    if (trimmed.isEmpty()) {
        return;
    }
    if (!list.contains(trimmed)) {
        list.append(trimmed);
    }
}

QString subscriptionUrl(const QString &raw)
{
    const QString url = trimUnicode(raw);
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid()) {
        return QString();
    }
    const QString scheme = parsed.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return QString();
    }
    const QString host = toAsciiLower(parsed.host());
    if (host.isEmpty()) {
        return QString();
    }
    if (host.endsWith(QLatin1String("t.me")) || host.endsWith(QLatin1String("telegram.me"))
        || host.endsWith(QLatin1String("telegram.org"))
        || host.endsWith(QLatin1String("telegram.dog")) || host == QLatin1String("localhost")) {
        return QString();
    }
    const QString path = toAsciiLower(parsed.path());
    static const QLatin1String kExtensions[] = {
        QLatin1String(".png"),  QLatin1String(".jpg"),   QLatin1String(".jpeg"),
        QLatin1String(".gif"),  QLatin1String(".webp"),  QLatin1String(".svg"),
        QLatin1String(".ico"),  QLatin1String(".css"),   QLatin1String(".js"),
        QLatin1String(".woff"), QLatin1String(".woff2"), QLatin1String(".mp4"),
    };
    for (const QLatin1String ext : kExtensions) {
        if (path.endsWith(ext)) {
            return QString();
        }
    }
    return url;
}

int scoreSubscriptionUrl(const QString &url)
{
    const QString lower = toAsciiLower(url);
    static const QLatin1String kNeedles[] = {
        QLatin1String("subscribe"), QLatin1String("subscription"), QLatin1String("clash"),
        QLatin1String("mihomo"),    QLatin1String("singbox"),      QLatin1String("sing-box"),
        QLatin1String("base64"),    QLatin1String("download"),     QLatin1String("node"),
        QLatin1String("proxy"),     QLatin1String(".yaml"),        QLatin1String(".yml"),
        QLatin1String(".txt"),      QLatin1String(".json"),
    };
    int score = 0;
    for (const QLatin1String needle : kNeedles) {
        if (lower.contains(needle)) {
            score += 1;
        }
    }
    return score;
}

Payloads payloads(const QStringList &blocks)
{
    Payloads result;
    for (const QString &block : blocks) {
        const QString text = plainText(block);
        auto uris = nodeUriRegex().globalMatch(text);
        while (uris.hasNext()) {
            pushUnique(result.uris, uris.next().captured(1));
        }
        auto hrefs = hrefRegex().globalMatch(block);
        while (hrefs.hasNext()) {
            const QString url = subscriptionUrl(decodeEntities(hrefs.next().captured(1)));
            if (!url.isNull()) {
                pushUnique(result.urls, url);
            }
        }
        auto urls = httpUrlRegex().globalMatch(text);
        while (urls.hasNext()) {
            const QString url = subscriptionUrl(decodeEntities(urls.next().captured(0)));
            if (!url.isNull()) {
                pushUnique(result.urls, url);
            }
        }
    }
    // 最像订阅端点的排前面，然后才受 MAX_SUB_URLS 截断。
    // Rust 的 sort_by 是稳定排序，String 比较走 UTF-8 字节序，这里用 toUtf8() 照搬。
    std::stable_sort(result.urls.begin(), result.urls.end(), [](const QString &a, const QString &b) {
        const int scoreB = scoreSubscriptionUrl(b);
        const int scoreA = scoreSubscriptionUrl(a);
        if (scoreB != scoreA) {
            return scoreB < scoreA;
        }
        return a.toUtf8() < b.toUtf8();
    });
    while (result.urls.size() > kMaxSubUrls) {
        result.urls.removeLast();
    }
    return result;
}

QString rawUriText(const QStringList &uris)
{
    return uris.join(QStringLiteral("\n"));
}

// ---- 对应 Rust `impl super::Collector` 里的 Telegram 方法 ----

Result<std::optional<Downloaded>> fetchPage(const TelegramContext &context,
                                            const Models::Source &source, const QString &url,
                                            const QString &taskId,
                                            std::chrono::milliseconds budget)
{
    // Rust 在这里拼 FetchCandidate{ url, host: url_host(url).unwrap_or_default(), mirror: false }
    // 再交给 download_candidate（按主机并发闸、条件请求、RESPONSE_HEADER_TIMEOUT 都在那儿）。
    // Collector 的移植把这一整段包成 context.fetchPage，本文件因此能完全不碰网络。
    if (!context.fetchPage) {
        return Result<std::optional<Downloaded>>::withError(
            QStringLiteral("telegram transport is not wired"));
    }
    return context.fetchPage(source, url, taskId, budget);
}

QVector<Models::Node> parseBlocking(const TelegramContext &context, const QString &text,
                                    const QString &sourceId)
{
    if (context.parse) {
        return context.parse(text, sourceId);
    }
    return Parser::parseSubscription(text, sourceId);
}

Models::FetchResult telegramFailure(const TelegramContext &context, const Models::Source &source,
                                    const QString &resolvedUrl, const QString &message)
{
    if (context.db) {
        context.db->markSourceFetched(source.id, message, 0);
    }
    Models::FetchResult result;
    result.sourceId = source.id;
    result.sourceName = source.name;
    result.resolvedUrl = resolvedUrl;
    result.success = false;
    result.message = message;
    result.nodeCount = 0;
    return result;
}

Models::FetchResult fetchChannel(const TelegramContext &context, const Models::Source &source,
                                 const QString &channel, const QString &taskId,
                                 std::chrono::milliseconds budget, const QString &egress)
{
    const auto started = std::chrono::steady_clock::now();
    const QString failureUrl = QStringLiteral("https://t.me/s/") + channel;
    reportPhase(context, taskId, source.id, TaskPhase::Downloading);

    const auto elapsedNow = [&started]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
    };

    const Result<std::optional<Downloaded>> first =
        fetchPage(context, source, failureUrl, taskId,
                  std::max(budget, std::chrono::milliseconds(500)));
    if (first.isErr()) {
        return telegramFailure(context, source, failureUrl, first.error());
    }
    if (!first.value().has_value()) {
        // Ok(None) = 服务端回了 304。
        if (context.db) {
            context.db->markSourceFetched(source.id, QStringLiteral("not modified"),
                                          source.lastNodeCount);
        }
        Models::FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = failureUrl;
        result.success = true;
        result.message = Messages::notModified();
        result.nodeCount = int(std::max<qint64>(source.lastNodeCount, 0));
        return result;
    }
    const Downloaded downloaded = *first.value();

    const QString contentSha256 = Crypto::sha256Hex(downloaded.body);
    const bool unchanged = !source.contentSha256.isNull() && source.contentSha256 == contentSha256
        && !source.lastResolvedUrl.isNull() && source.lastResolvedUrl == failureUrl;
    const QString html = lossyText(downloaded.body);
    QStringList blocks = messageBlocks(html);
    QVector<qulonglong> ids = messageIds(html);
    int pagesRead = 1;

    if (unchanged) {
        if (context.db) {
            context.db->markSourceFetched(source.id, QStringLiteral("content unchanged"),
                                          source.lastNodeCount);
        }
        Models::FetchResult result;
        result.sourceId = source.id;
        result.sourceName = source.name;
        result.resolvedUrl = downloaded.finalUrl;
        result.success = true;
        result.message =
            formatText(Messages::contentUnchanged(), {{"{}", QString::number(blocks.size())}});
        result.nodeCount = int(std::max<qint64>(source.lastNodeCount, 0));
        return result;
    }

    if (context.db) {
        context.db->updateSourceCache(source.id, failureUrl, downloaded.etag,
                                      downloaded.lastModified, contentSha256);
    }

    // 第一次见到这个源就多翻几页补历史，之后只看最新一页。
    const int wanted = source.lastFetchAt.isNull() ? kSeedPages : kFollowPages;
    while (pagesRead < wanted) {
        if (ids.isEmpty()) {
            break;
        }
        const qulonglong oldest = *std::min_element(ids.begin(), ids.end());
        if (saturatingSub(budget, elapsedNow()) <= kPerSubUrl) {
            break;
        }
        const QString next = QStringLiteral("https://t.me/s/%1?before=%2")
                                 .arg(channel, QString::number(oldest));
        const std::chrono::milliseconds remaining = saturatingSub(budget, elapsedNow());
        const Result<std::optional<Downloaded>> page =
            fetchPage(context, source, next, taskId, std::max(remaining, kPerSubUrl));
        if (page.isOk() && page.value().has_value()) {
            const QString older = lossyText(page.value()->body);
            const QVector<qulonglong> olderIds = messageIds(older);
            if (olderIds.isEmpty()) {
                break;
            }
            blocks.append(messageBlocks(older));
            ids = olderIds;
            pagesRead += 1;
        } else {
            // 翻页是锦上添花：拿不到就用手上这一页继续，别把整轮判死。
            break;
        }
    }

    const Payloads found = payloads(blocks);
    const QStringList uris = found.uris;
    const QStringList urls = found.urls;
    reportPhase(context, taskId, source.id, TaskPhase::Parsing);
    QVector<Models::Node> nodes = parseBlocking(context, rawUriText(uris), source.id);

    int hit = 0;
    int tried = 0;
    QStringList failures;
    for (const QString &url : urls) {
        const std::chrono::milliseconds remaining = saturatingSub(budget, elapsedNow());
        if (remaining <= std::chrono::milliseconds(600)) {
            break;
        }
        tried += 1;
        const std::chrono::milliseconds perUrl = std::min(remaining, kPerSubUrl);
        const Result<std::optional<Downloaded>> page =
            fetchPage(context, source, url, taskId, perUrl);
        if (page.isOk()) {
            if (page.value().has_value()) {
                const QString body = lossyText(page.value()->body);
                const QVector<Models::Node> parsed = parseBlocking(context, body, source.id);
                if (!parsed.isEmpty()) {
                    hit += 1;
                    nodes += parsed;
                }
            }
        } else {
            // 跟进订阅链接只是加分项，挂了不判死整轮 —— 但原因要留在消息里，
            // 否则“这源只解出 16 个”根本分不清是频道没发还是网关不通。
            if (failures.size() < 2) {
                const QString host = urlHost(url).value_or(QStringLiteral("?"));
                failures.append(host + QStringLiteral(": ") + page.error());
            }
        }
    }

    const QString note = failures.isEmpty()
        ? QString()
        : formatText(Messages::failureNote(), {{"{}", failures.join(QStringLiteral(" / "))}});
    const int round = nodes.size();
    if (round == 0) {
        const QString message = formatText(Messages::zeroNodes(),
                                           {{"{}", QString::number(blocks.size())},
                                            {"{pages_read}", QString::number(pagesRead)},
                                            {"{hit}", QString::number(hit)},
                                            {"{tried}", QString::number(tried)},
                                            {"{note}", note}});
        return telegramFailure(context, source, failureUrl, message);
    }

    reportNodes(context, taskId, source.id, round);
    reportPhase(context, taskId, source.id, TaskPhase::Writing);
    // Rust 的 self.db 一定存在；db 为空只出现在不建库的纯逻辑测试里，那时 stored = 本轮解出数。
    int stored = round;
    if (context.db) {
        // 只增不删：Telegram 频道的节点是零散发布的，这一页看不到的旧节点绝不能删，
        // 所以走 accumulate_source_nodes，而不是 replace_source_nodes。
        const Result<int> accumulated = context.db->accumulateSourceNodes(source.id, nodes);
        if (accumulated.isErr()) {
            return telegramFailure(context, source, failureUrl,
                                   QStringLiteral("store failed: ") + accumulated.error());
        }
        stored = accumulated.value();
    }
    if (context.db) {
        context.db->markSourceFetched(source.id, QStringLiteral("ok"), qint64(stored));
    }
    const QString message = formatText(Messages::okSummary(),
                                       {{"{}", QString::number(blocks.size())},
                                        {"{}", QString::number(uris.size())},
                                        {"{egress}", egress},
                                        {"{pages_read}", QString::number(pagesRead)},
                                        {"{hit}", QString::number(hit)},
                                        {"{tried}", QString::number(tried)},
                                        {"{note}", note},
                                        {"{round}", QString::number(round)},
                                        {"{stored}", QString::number(stored)}});
    Models::FetchResult result;
    result.sourceId = source.id;
    result.sourceName = source.name;
    result.resolvedUrl = downloaded.finalUrl;
    result.success = true;
    result.message = message;
    result.nodeCount = stored;
    return result;
}

Models::FetchResult fetchTelegramSource(const TelegramContext &context,
                                        const Models::Source &source, const QString &channel,
                                        const QString &taskId, std::chrono::milliseconds budget)
{
    if (context.viaProxy) {
        return fetchChannel(context, source, channel, taskId, budget, Messages::egressInApp());
    }
    // 定时自动拉取（scheduler）拿的是不带代理的那份 collector，而 t.me 直连必挂。
    // 退一步认 Windows 系统代理：app 自己在系统代理模式下写的就是 9980，
    // v2rayN 之类外部工具指着也一样能用。只试这一条，不并发试多条。
    // Rust 用 spawn_blocking 查注册表，出口查询同样交给注入进来的实现。
    const QString systemProxy = context.systemProxyUrl ? context.systemProxyUrl() : QString();
    if (!systemProxy.isNull()) {
        PageFetcher via;
        if (context.applyProxy) {
            const Result<PageFetcher> built = context.applyProxy(systemProxy);
            if (built.isOk()) {
                via = built.value();
            }
        }
        if (via) {
            TelegramContext proxied = context;
            proxied.fetchPage = via;
            return fetchChannel(proxied, source, channel, taskId, budget,
                                Messages::egressSystem());
        }
        return telegramFailure(context, source, QStringLiteral("https://t.me/s/") + channel,
                               Messages::systemProxyUnusable());
    }
    return telegramFailure(context, source, QStringLiteral("https://t.me/s/") + channel,
                           Messages::needEgress());
}

} // namespace Collector::Telegram
