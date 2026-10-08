#include "parser/Common.h"

#include <QByteArray>
#include <QChar>
#include <QDate>
#include <QSet>

#include <initializer_list>

#include "core/Encoding.h"

namespace Parser {
namespace Common {

namespace {

/// base64 0.22 的四种引擎。core/Encoding 只提供 STANDARD 与 URL_SAFE_NO_PAD，
/// 另外两种用等价的字母表/填充变换补齐（每个分支的等价性见注释）。
enum class Engine4 {
    Standard,
    StandardNoPad,
    UrlSafe,
    UrlSafeNoPad,
};

const Engine4 kEngineOrder[4] = {Engine4::Standard,
                                 Engine4::StandardNoPad,
                                 Engine4::UrlSafe,
                                 Engine4::UrlSafeNoPad};

std::optional<QByteArray> decodeWithEngine(const QByteArray& text, Engine4 engine)
{
    switch (engine) {
    case Engine4::Standard: {
        const Result<QByteArray> result = Encoding::decode(text, Encoding::Engine::Standard);
        return result.isOk() ? std::optional<QByteArray>(result.value()) : std::nullopt;
    }
    case Engine4::StandardNoPad: {
        // STANDARD_NO_PAD：字母表同 STANDARD，但出现任何 '=' 填充即报错。
        // 补齐成 4 的倍数再交给严格引擎，残余比特与 len%4==1 的判定完全一致。
        if (text.contains('=')) {
            return std::nullopt;
        }
        if (text.size() % 4 == 1) {
            return std::nullopt;
        }
        QByteArray padded = text;
        while (padded.size() % 4 != 0) {
            padded.append('=');
        }
        return decodeWithEngine(padded, Engine4::Standard);
    }
    case Engine4::UrlSafe: {
        // URL_SAFE 的字母表是 A-Za-z0-9-_，'+' 与 '/' 是非法字符；换表后即 STANDARD。
        if (text.contains('+') || text.contains('/')) {
            return std::nullopt;
        }
        QByteArray translated = text;
        translated.replace('-', '+');
        translated.replace('_', '/');
        return decodeWithEngine(translated, Engine4::Standard);
    }
    case Engine4::UrlSafeNoPad: {
        const Result<QByteArray> result = Encoding::decode(text, Encoding::Engine::UrlSafeNoPad);
        return result.isOk() ? std::optional<QByteArray>(result.value()) : std::nullopt;
    }
    }
    return std::nullopt;
}

bool isAsciiDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool isAsciiHex(char ch)
{
    return isAsciiDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool isAsciiAlnum(char ch)
{
    return isAsciiDigit(ch) || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

/// 取 index 处的码点，width 返回它占用的 UTF-16 单元数。
uint codePointAt(const QString& text, qsizetype index, qsizetype& width)
{
    if (index + 1 < text.size() && QChar::isHighSurrogate(text.at(index))
        && QChar::isLowSurrogate(text.at(index + 1))) {
        width = 2;
        return QChar::surrogateToUcs4(text.at(index), text.at(index + 1));
    }
    width = 1;
    return text.at(index).unicode();
}

/// Rust `str::parse::<u16>()`：允许一个前导 '+'，之后必须全是 ASCII 数字且不溢出。
bool parseU16(const QString& text, int& out)
{
    QStringView view = QStringView(text).trimmed();
    view = QStringView(text);
    qsizetype start = 0;
    if (!view.isEmpty() && view.at(0) == QLatin1Char('+')) {
        start = 1;
    }
    if (start >= view.size()) {
        return false;
    }
    quint64 value = 0;
    for (qsizetype i = start; i < view.size(); ++i) {
        const QChar ch = view.at(i);
        if (!(ch.unicode() >= u'0' && ch.unicode() <= u'9')) {
            return false;
        }
        value = value * 10u + quint64(ch.unicode() - u'0');
        if (value > 65535u) {
            return false;
        }
    }
    out = int(value);
    return true;
}

/// UTF-8 解码。Rust 的 `from_utf8` / `from_utf8_lossy` 按"最大合法子集"规则处理坏序列
/// （一个坏序列只产出一个 U+FFFD，并从断链的那个字节重新起算），而 Qt 的 fromUtf8
/// 对 overlong 等坏序列的处理不同，所以这里自己实现，保证与 Rust 逐字符一致。
QString decodeUtf8(const QByteArray& bytes, bool lossy, bool& ok)
{
    ok = true;
    QString out;
    out.reserve(bytes.size());
    static const QChar kReplacement(u'\xFFFD');

    qsizetype i = 0;
    const qsizetype size = bytes.size();
    while (i < size) {
        const uchar b0 = uchar(bytes.at(i));
        if (b0 < 0x80) {
            out.append(QChar(b0));
            ++i;
            continue;
        }

        int need = 0;
        uchar low = 0x80;
        uchar high = 0xBF;
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            need = 2;
        } else if (b0 >= 0xE0 && b0 <= 0xEF) {
            need = 3;
            if (b0 == 0xE0) {
                low = 0xA0;
            } else if (b0 == 0xED) {
                high = 0x9F;
            }
        } else if (b0 >= 0xF0 && b0 <= 0xF4) {
            need = 4;
            if (b0 == 0xF0) {
                low = 0x90;
            } else if (b0 == 0xF4) {
                high = 0x8F;
            }
        } else {
            // 0x80..0xC1、0xF5..0xFF：坏起始字节，自身就是一个最大子集。
            if (!lossy) {
                ok = false;
                return QString();
            }
            out.append(kReplacement);
            ++i;
            continue;
        }

        uint codePoint = 0;
        if (need == 2) {
            codePoint = uint(b0 & 0x1F);
        } else if (need == 3) {
            codePoint = uint(b0 & 0x0F);
        } else {
            codePoint = uint(b0 & 0x07);
        }

        qsizetype consumed = 1;
        bool broken = false;
        for (; consumed < need; ++consumed) {
            if (i + consumed >= size) {
                break; // 截断
            }
            const uchar next = uchar(bytes.at(i + consumed));
            const uchar lo = consumed == 1 ? low : uchar(0x80);
            const uchar hi = consumed == 1 ? high : uchar(0xBF);
            if (next < lo || next > hi) {
                broken = true;
                break;
            }
            codePoint = (codePoint << 6) | uint(next & 0x3F);
        }

        if (!broken && consumed == need) {
            if (codePoint > 0xFFFF) {
                out.append(QChar::highSurrogate(qchar32_t(codePoint)));
                out.append(QChar::lowSurrogate(qchar32_t(codePoint)));
            } else {
                out.append(QChar(ushort(codePoint)));
            }
            i += need;
            continue;
        }
        if (!lossy) {
            ok = false;
            return QString();
        }
        out.append(kReplacement);
        // 坏序列只在第 consumed 字节处断链时，从那个字节继续；整段截断则直接结束。
        i += broken ? consumed : size;
    }
    return out;
}

/// String::from_utf8(bytes) 的成功分支。
std::optional<QString> fromUtf8Strict(const QByteArray& bytes)
{
    bool ok = false;
    const QString text = decodeUtf8(bytes, false, ok);
    if (!ok) {
        return std::nullopt;
    }
    return text;
}

QByteArray stripWhitespace(const QString& text)
{
    QByteArray out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (!Common::isUnicodeSpace(ch)) {
            const QByteArray unit = QString(ch).toUtf8();
            // 代理对拆开会写出半个码点，这里按码点遍历更稳妥。
            out.append(unit);
        }
    }
    return out;
}

QStringList htmlEntityPairs()
{
    // 顺序不能改：Rust 是先 &amp; 再 &amp，所以 "&amp;amp;" 会变成 "&;"。
    return {QStringLiteral("&amp;"), QStringLiteral("&"), QStringLiteral("&amp"),
            QStringLiteral("&"),      QStringLiteral("&lt;"), QStringLiteral("<"),
            QStringLiteral("&gt;"),   QStringLiteral(">"),    QStringLiteral("&quot;"),
            QStringLiteral("\""),     QStringLiteral("&#39;"), QStringLiteral("'"),
            QStringLiteral("&apos;"), QStringLiteral("'")};
}

void decodeHtmlEntities(QString& text)
{
    const QStringList table = htmlEntityPairs();
    for (qsizetype i = 0; i + 1 < table.size(); i += 2) {
        text.replace(table.at(i), table.at(i + 1));
    }
}

/// url crate 的 USERINFO 集（= CONTROLS + 这些 ASCII 字符）；码位 >=128 一律不编码。
bool isUserinfoEncoded(uchar ch)
{
    if (ch <= 0x1F || ch == 0x7F) {
        return true;
    }
    static const char kChars[] = " \"<>`#?{}/:;=@[\\]^|";
    for (const char* p = kChars; *p != '\0'; ++p) {
        if (ch == uchar(*p)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace {

// 前向声明用不上的东西都放这里，避免污染头文件。
QString trimTrailingPunct(QString uri)
{
    static const QString kPunct = QStringLiteral(")]>,;.\"'");
    while (!uri.isEmpty() && kPunct.contains(uri.at(uri.size() - 1))) {
        uri.chop(1);
    }
    return uri;
}

} // namespace

qsizetype utf8Length(const QString& text)
{
    return text.toUtf8().size();
}

bool isUnicodeSpace(QChar ch)
{
    // Rust 的 char::is_whitespace == Unicode White_Space 属性。
    switch (ch.category()) {
    case QChar::Separator_Space:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
        return true;
    default:
        break;
    }
    const ushort u = ch.unicode();
    return (u >= 0x0009 && u <= 0x000D) || u == 0x0020 || u == 0x0085 || u == 0x00A0
        || u == 0x1680 || (u >= 0x2000 && u <= 0x200A) || u == 0x2028 || u == 0x2029
        || u == 0x202F || u == 0x205F || u == 0x3000;
}

QString toAsciiLower(const QString& text)
{
    // Rust 的 to_ascii_lowercase 只处理 A-Z，非 ASCII 原样保留（不能用 toLower()）。
    QString out = text;
    for (QChar& ch : out) {
        if (ch.unicode() >= u'A' && ch.unicode() <= u'Z') {
            ch = QChar(ch.unicode() + 32);
        }
    }
    return out;
}

const QRegularExpression& uriRegex()
{
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern(QString()),
                                            QRegularExpression::UseUnicodePropertiesOption);
    Q_UNUSED(pattern);
    static const QRegularExpression re(
        QStringLiteral(R"re((?i)\b(?:(?:ss|vmess|vless|trojan|hysteria2|hy2|wireguard|wg|socks5|socks|http|https)://)[^\s<>'\]\)]+)re)"),
        QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QRegularExpression& lineUriRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"re((?i)^(?:ss|vmess|vless|trojan|hysteria2|hy2|wireguard|wg|socks5|socks)://\S+)re)"),
        QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

QString expandUrlTemplate(const QString& text)
{
    // Rust 用 chrono::Local::now()，即本地日期而不是 UTC。
    const QDate today = QDate::currentDate();
    const QString y = QStringLiteral("%1").arg(today.year(), 4, 10, QLatin1Char('0'));
    const QString m = QStringLiteral("%1").arg(today.month(), 2, 10, QLatin1Char('0'));
    const QString d = QStringLiteral("%1").arg(today.day(), 2, 10, QLatin1Char('0'));
    const QString ymd = y + m + d;
    const QString ymdShort = y.right(2) + m + d;
    QString mNoZero = m;
    mNoZero.remove(QRegularExpression(QStringLiteral("^0+")));
    QString dNoZero = d;
    dNoZero.remove(QRegularExpression(QStringLiteral("^0+")));
    const QString mShort = mNoZero.isEmpty() ? QStringLiteral("0") : mNoZero;
    const QString dShort = dNoZero.isEmpty() ? QStringLiteral("0") : dNoZero;

    QString out = text;
    out.replace(QStringLiteral("{Ymd}"), ymd);
    out.replace(QStringLiteral("{ymd}"), ymdShort);
    out.replace(QStringLiteral("{Y}"), y);
    out.replace(QStringLiteral("{mm}"), m);
    out.replace(QStringLiteral("{m}"), mShort);
    out.replace(QStringLiteral("{dd}"), d);
    out.replace(QStringLiteral("{d}"), dShort);
    return out;
}

QString normalizeSubscriptionText(const QString& input)
{
    QString s = input;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    decodeHtmlEntities(s);
    return s;
}

QString normalizeUri(const QString& uri)
{
    QString s = uri.trimmed();
    decodeHtmlEntities(s);
    // Rust 判的是 UTF-8 字节长度，但 '<' '>' 都是单字节，两种长度在这里等价。
    while (s.startsWith(QLatin1Char('<')) && s.endsWith(QLatin1Char('>')) && s.size() > 2) {
        s = s.mid(1, s.size() - 2);
    }
    return trimTrailingPunct(s);
}

std::optional<QString> decodeMaybeBase64(const QString& input)
{
    const QByteArray cleaned = stripWhitespace(input);
    if (cleaned.size() < 16) {
        return std::nullopt;
    }
    qsizetype okCount = 0;
    for (const char ch : cleaned) {
        if (isAsciiAlnum(ch) || ch == '+' || ch == '/' || ch == '-' || ch == '_' || ch == '=') {
            ++okCount;
        }
    }
    // Rust 用 char 计数除以 len()（UTF-8 字节数）；这里 cleaned 已是按字节过滤后的结果，
    // 非 ASCII 会让占比下降，与 Rust 的行为一致。
    const float ratio = float(okCount) / float(cleaned.size());
    if (ratio < 0.95f) {
        return std::nullopt;
    }

    for (const Engine4 engine : kEngineOrder) {
        const std::optional<QByteArray> bytes = decodeWithEngine(cleaned, engine);
        if (!bytes.has_value()) {
            continue;
        }
        const std::optional<QString> text = fromUtf8Strict(*bytes);
        if (!text.has_value()) {
            continue;
        }
        bool printable = true;
        for (qsizetype i = 0; i < text->size();) {
            qsizetype width = 1;
            const uint cp = codePointAt(*text, i, width);
            // Rust 的 char::is_control()（Cc）：C0、DEL、C1。
            const bool control = (cp <= 0x1F) || (cp >= 0x7F && cp <= 0x9F);
            if (control && cp != u'\n' && cp != u'\r' && cp != u'\t') {
                printable = false;
                break;
            }
            i += width;
        }
        if (printable) {
            return text;
        }
    }
    return std::nullopt;
}

std::optional<QString> decodeBase64Str(const QString& input)
{
    const QByteArray cleaned = stripWhitespace(input);
    QByteArray padded = cleaned;
    while (padded.size() % 4 != 0) {
        padded.append('=');
    }
    for (const QByteArray& candidate : {cleaned, padded}) {
        for (const Engine4 engine : kEngineOrder) {
            const std::optional<QByteArray> bytes = decodeWithEngine(candidate, engine);
            if (!bytes.has_value()) {
                continue;
            }
            const std::optional<QString> text = fromUtf8Strict(*bytes);
            if (text.has_value()) {
                return text;
            }
        }
    }
    return std::nullopt;
}

QStringList extractUris(const QString& content)
{
    const QString text = normalizeSubscriptionText(content);
    QStringList out;
    QSet<QString> seen;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QRegularExpressionMatch whole = lineUriRegex().match(line);
        if (whole.hasMatch()) {
            const QString uri = normalizeUri(whole.captured(0));
            if (!seen.contains(uri)) {
                seen.insert(uri);
                out.append(uri);
            }
            continue;
        }
        QRegularExpression::MatchedLength length = 0;
        QRegularExpression::MatchOptions options;
        Q_UNUSED(length);
        Q_UNUSED(options);
        auto matches = uriRegex().globalMatch(line);
        while (matches.hasNext()) {
            const QRegularExpressionMatch cap = matches.next();
            const QString target = cap.captured(1);
            if (target.isEmpty()) {
                continue;
            }
            const QString uri = normalizeUri(target);
            if (!seen.contains(uri)) {
                seen.insert(uri);
                out.append(uri);
            }
        }
    }

    if (out.isEmpty()) {
        auto matches = uriRegex().globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch cap = matches.next();
            const QString target = cap.captured(1);
            if (target.isEmpty()) {
                continue;
            }
            const QString uri = normalizeUri(target);
            if (!seen.contains(uri)) {
                seen.insert(uri);
                out.append(uri);
            }
        }
    }
    return out;
}

QString percentDecode(const QString& text)
{
    const QByteArray input = text.toUtf8();
    QByteArray bytes;
    bytes.reserve(input.size());
    for (qsizetype i = 0; i < input.size(); ++i) {
        if (input.at(i) == '%' && i + 2 < input.size() && isAsciiHex(input.at(i + 1))
            && isAsciiHex(input.at(i + 2))) {
            bool ok = false;
            const int value = input.mid(i + 1, 2).toInt(&ok, 16);
            bytes.append(char(value));
            i += 2;
            continue;
        }
        bytes.append(input.at(i));
    }
    bool ok = false;
    return decodeUtf8(bytes, true, ok);
}

QString encodeUserinfo(const QString& text)
{
    static const char* kHex = "0123456789ABCDEF";
    const QByteArray input = text.toUtf8();
    QString out;
    out.reserve(input.size());
    for (const char ch : input) {
        const uchar value = uchar(ch);
        if (isUserinfoEncoded(value)) {
            out.append(QLatin1Char('%'));
            out.append(QLatin1Char(kHex[value >> 4]));
            out.append(QLatin1Char(kHex[value & 0x0F]));
        } else {
            out.append(QChar::fromLatin1(char(value)));
        }
    }
    return out;
}

QString stripQueryFragment(const QString& text)
{
    const int question = text.indexOf(QLatin1Char('?'));
    const int hash = text.indexOf(QLatin1Char('#'));
    int cut = -1;
    if (question >= 0) {
        cut = question;
    }
    if (hash >= 0 && (cut < 0 || hash < cut)) {
        cut = hash;
    }
    return cut < 0 ? text : text.left(cut);
}

std::optional<HostPort> splitHostPort(const QString& text)
{
    const QString stripped = stripQueryFragment(text);
    if (stripped.startsWith(QLatin1Char('['))) {
        const QString rest = stripped.mid(1);
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return std::nullopt;
        }
        const QString after = rest.mid(close + 1);
        if (!after.startsWith(QLatin1Char(':'))) {
            return std::nullopt;
        }
        return HostPort{rest.left(close), after.mid(1)};
    }
    const int colon = stripped.lastIndexOf(QLatin1Char(':'));
    if (colon < 0) {
        return std::nullopt;
    }
    return HostPort{stripped.left(colon), stripped.mid(colon + 1)};
}

std::optional<UserinfoHostPort> splitUserinfoHostPort(const QString& text)
{
    const QString stripped = stripQueryFragment(text);
    const int at = stripped.lastIndexOf(QLatin1Char('@'));
    if (at < 0) {
        return std::nullopt;
    }
    const std::optional<HostPort> hostPort = splitHostPort(stripped.mid(at + 1));
    if (!hostPort.has_value()) {
        return std::nullopt;
    }
    int port = 0;
    if (!parseU16(hostPort->port, port)) {
        return std::nullopt;
    }
    return UserinfoHostPort{stripped.left(at), hostPort->host, port};
}

QMap<QString, QString> parseQuery(const QString& query)
{
    QMap<QString, QString> map;
    const QStringList pairs = query.split(QLatin1Char('&'));
    for (const QString& pair : pairs) {
        if (pair.isEmpty()) {
            continue;
        }
        const int equals = pair.indexOf(QLatin1Char('='));
        const QString key = equals < 0 ? pair : pair.left(equals);
        const QString value = equals < 0 ? QString() : pair.mid(equals + 1);
        if (!key.isEmpty()) {
            map.insert(key, percentDecode(value));
        }
    }
    return map;
}

std::optional<Authority> parseAuthority(const QString& uri)
{
    const QString normalized = normalizeUri(uri);
    const int schemeEnd = normalized.indexOf(QStringLiteral("://"));
    if (schemeEnd < 0) {
        return std::nullopt;
    }
    QString rest = normalized.mid(schemeEnd + 3);

    QString fragment;
    const int hash = rest.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        fragment = percentDecode(rest.mid(hash + 1));
        rest = rest.left(hash);
    }
    QString query;
    const int question = rest.indexOf(QLatin1Char('?'));
    if (question >= 0) {
        query = rest.mid(question + 1);
        rest = rest.left(question);
    }
    const int at = rest.lastIndexOf(QLatin1Char('@'));
    if (at < 0) {
        return std::nullopt;
    }
    const std::optional<HostPort> hostPort = splitHostPort(rest.mid(at + 1));
    if (!hostPort.has_value()) {
        return std::nullopt;
    }
    int port = 0;
    if (!parseU16(hostPort->port, port)) {
        return std::nullopt;
    }
    return Authority{toAsciiLower(normalized.left(schemeEnd)), percentDecode(rest.left(at)), port,
                     hostPort->host, query, fragment};
}

bool looksLikeUuid(const QString& text)
{
    const QByteArray bytes = text.trimmed().toUtf8();
    if (bytes.size() != 36) {
        return false;
    }
    if (bytes.at(8) != '-' || bytes.at(13) != '-' || bytes.at(18) != '-' || bytes.at(23) != '-') {
        return false;
    }
    for (const char ch : bytes) {
        if (!isAsciiHex(ch) && ch != '-') {
            return false;
        }
    }
    return true;
}

} // namespace Common
} // namespace Parser
