#include "parser/Subscription.h"

#include <QByteArray>
#include <QChar>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <initializer_list>
#include <optional>
#include <utility>

#include "core/Ids.h"
#include "core/TimeUtil.h"
#include "parser/Clash.h"
#include "parser/Fingerprint.h"
#include "parser/Uri.h"

namespace Parser {

namespace {

using Common::ucs4;

/// Rust 的 regex crate 默认按 Unicode 处理 \s \w \b，这里统一打开 UCP；
/// 本模块的正则没有使用 \d，所以 UCP 不会引入别的差异。
QRegularExpression makeRe(const QString& pattern)
{
    return QRegularExpression(pattern, QRegularExpression::UseUnicodePropertiesOption);
}

// ---- mod.rs 的 8 个 lazy_static 正则。CJK 一律写成 \x{…}，避免字面量在转录时丢码位。----

const QRegularExpression& reUrl()
{
    static const QRegularExpression re =
        makeRe(QStringLiteral(R"re(https?://\S+|www\.\S+)re"));
    return re;
}

const QRegularExpression& reDomain()
{
    static const QRegularExpression re = makeRe(QStringLiteral(
        R"re((?i)\b[a-z0-9-]+\.(?:com|net|org|xyz|top|cc|me|vip|club|site|app|cn|io|icu|link)\b)re"));
    return re;
}

const QRegularExpression& reScheme()
{
    static const QRegularExpression re = makeRe(QStringLiteral(
        R"re((?i)\b(?:vless|vmess|trojan|ss|ssr|hysteria2?|tuic|socks5?)://\S*)re"));
    return re;
}

/// 关键词：tg|群|官网|关注|推广|发布|频道|订阅|购买|永久|最新|防封|免翻|剩余|到期|流量
/// （[]、【】、() 三组里各重复一次，与 Rust 的字面量一致。）
const QRegularExpression& reBrackets()
{
    static const QRegularExpression re = makeRe(QStringLiteral(
        R"re((?i)\[[^\]]*(?:tg|\x{7FA4}|\x{5B98}\x{7F51}|\x{5173}\x{6CE8}|\x{63A8}\x{5E7F}|\x{53D1}\x{5E03}|\x{9891}\x{9053}|\x{8BA2}\x{9605}|\x{8D2D}\x{4E70}|\x{6C38}\x{4E45}|\x{6700}\x{65B0}|\x{9632}\x{5C01}|\x{514D}\x{7FFB}|\x{5269}\x{4F59}|\x{5230}\x{671F}|\x{6D41}\x{91CF})[^\]]*\]|\x{3010}[^\x{3011}]*(?:tg|\x{7FA4}|\x{5B98}\x{7F51}|\x{5173}\x{6CE8}|\x{63A8}\x{5E7F}|\x{53D1}\x{5E03}|\x{9891}\x{9053}|\x{8BA2}\x{9605}|\x{8D2D}\x{4E70}|\x{6C38}\x{4E45}|\x{6700}\x{65B0}|\x{9632}\x{5C01}|\x{514D}\x{7FFB}|\x{5269}\x{4F59}|\x{5230}\x{671F}|\x{6D41}\x{91CF})[^\x{3011}]*\x{3011}|\([^\)]*(?:tg|\x{7FA4}|\x{5B98}\x{7F51}|\x{5173}\x{6CE8}|\x{63A8}\x{5E7F}|\x{53D1}\x{5E03}|\x{9891}\x{9053}|\x{8BA2}\x{9605}|\x{8D2D}\x{4E70}|\x{6C38}\x{4E45}|\x{6700}\x{65B0}|\x{9632}\x{5C01}|\x{514D}\x{7FFB}|\x{5269}\x{4F59}|\x{5230}\x{671F}|\x{6D41}\x{91CF})[^\)]*\))re"));
    return re;
}

const QRegularExpression& rePromo()
{
    static const QRegularExpression re = makeRe(QStringLiteral(
        R"re((?i)(?:TG|\x{4EA4}\x{6D41}\x{7FA4}|TG\x{7FA4}|\x{9891}\x{9053}|\x{516C}\x{4F17}\x{53F7}|\x{5173}\x{6CE8}|\x{63A8}\x{5E7F}|\x{9632}\x{5931}\x{8054}|\x{6700}\x{65B0}\x{5730}\x{5740}|\x{6C38}\x{4E45}\x{514D}\x{8D39}|\x{514D}\x{8D39}\x{8282}\x{70B9}|\x{5907}\x{7528}\x{5730}\x{5740}|\x{9080}\x{8BF7}\x{7801}|\x{8054}\x{7CFB}\x{65B9}\x{5F0F})[:\x{FF1A}]?\s*@?[\w_.-]+)re"));
    return re;
}

const QRegularExpression& reTraffic()
{
    static const QRegularExpression re = makeRe(QStringLiteral(
        R"re((?i)(?:\x{5269}\x{4F59}\x{6D41}\x{91CF}|\x{5269}\x{4F59}|\x{5230}\x{671F}\x{65F6}\x{95F4}|\x{5230}\x{671F}|\x{91CD}\x{7F6E}|\x{6D41}\x{91CF}|\x{500D}\x{7387}|\x{500D}\x{6570}|\x{4E0D}\x{9650}\x{901F}|\x{4E0D}\x{9650}\x{6D41}|\x{9AD8}\x{901F}|\x{63A8}\x{8350}|\x{5B98}\x{7F51}|\x{8D2D}\x{4E70})[:\x{FF1A}\s-]*[^\s\x{FF0C},\x{3001}|]*)re"));
    return re;
}

const QRegularExpression& reTgHandle()
{
    static const QRegularExpression re = makeRe(QStringLiteral(R"re(@[\w_]{4,})re"));
    return re;
}

const QRegularExpression& reSpaces()
{
    static const QRegularExpression re = makeRe(QStringLiteral(R"re(\s+)re"));
    return re;
}

/// 对应 Rust 的 `trim_matches(closure)`：两端反复剥掉被条件命中的字符。
template <typename Predicate>
QString trimMatchedChars(QString text, Predicate matches)
{
    while (!text.isEmpty() && matches(text.at(0))) {
        text.remove(0, 1);
    }
    while (!text.isEmpty() && matches(text.at(text.size() - 1))) {
        text.chop(1);
    }
    return text;
}

bool isTrimNoiseChar(QChar ch)
{
    return Common::isUnicodeSpace(ch) || ch == QLatin1Char('-') || ch == QLatin1Char('_')
        || ch == QLatin1Char(':') || ch == QChar(0xFF1A);
}

bool isTrimTailChar(QChar ch)
{
    return Common::isUnicodeSpace(ch) || ch == QLatin1Char('-') || ch == QLatin1Char('_');
}

struct ZhCountry {
    QVector<uint> key;
    const char* code;
};

/// guess_country 的中文表，顺序与 Rust 的 zh_map 一致（contains 是先到先得）。
const QVector<ZhCountry>& zhCountryTable()
{
    static const QVector<ZhCountry> table = {
        {{0x9999, 0x6E2F}, "HK"},                                 // 香港
        {{0x53F0, 0x6E7E}, "TW"},                                 // 台湾
        {{0x53F0, 0x7063}, "TW"},                                 // 台灣
        {{0x65E5, 0x672C}, "JP"},                                 // 日本
        {{0x65B0, 0x52A0, 0x5761}, "SG"},                         // 新加坡
        {{0x7F8E, 0x56FD}, "US"},                                 // 美国
        {{0x7F8E, 0x570B}, "US"},                                 // 美國
        {{0x97E9, 0x56FD}, "KR"},                                 // 韩国
        {{0x97D3, 0x570B}, "KR"},                                 // 韓國
        {{0x82F1, 0x56FD}, "GB"},                                 // 英国
        {{0x82F1, 0x570B}, "GB"},                                 // 英國
        {{0x5FB7, 0x56FD}, "DE"},                                 // 德国
        {{0x6CD5, 0x56FD}, "FR"},                                 // 法国
        {{0x52A0, 0x62FF, 0x5927}, "CA"},                         // 加拿大
        {{0x6FB3, 0x6D32}, "AU"},                                 // 澳洲
        {{0x6FB3, 0x5927, 0x5229, 0x4E9A}, "AU"},                 // 澳大利亚
        {{0x4FC4, 0x7F57, 0x65AF}, "RU"},                         // 俄罗斯
        {{0x5370, 0x5EA6}, "IN"},                                 // 印度
        {{0x571F, 0x8033, 0x5176}, "TR"},                         // 土耳其
        {{0x8377, 0x5170}, "NL"},                                 // 荷兰
    };
    return table;
}

QString toAsciiUpper(const QString& text)
{
    QString out = text;
    for (QChar& ch : out) {
        if (ch.unicode() >= u'a' && ch.unicode() <= u'z') {
            ch = QChar(ushort(ch.unicode() - 32));
        }
    }
    return out;
}

/// clean_node_name 的国家码 → 中文名映射（"GB"|"UK" → 英国，未命中 → 其它）。
QString regionLabel(const QString& asciiUpperCode)
{
    static const std::pair<QString, QVector<uint>> kLabels[] = {
        {QStringLiteral("HK"), {0x9999, 0x6E2F}},                    // 香港
        {QStringLiteral("JP"), {0x65E5, 0x672C}},                    // 日本
        {QStringLiteral("US"), {0x7F8E, 0x56FD}},                    // 美国
        {QStringLiteral("SG"), {0x65B0, 0x52A0, 0x5761}},            // 新加坡
        {QStringLiteral("TW"), {0x53F0, 0x6E7E}},                    // 台湾
        {QStringLiteral("KR"), {0x97E9, 0x56FD}},                    // 韩国
        {QStringLiteral("GB"), {0x82F1, 0x56FD}},                    // 英国
        {QStringLiteral("UK"), {0x82F1, 0x56FD}},                    // 英国
        {QStringLiteral("DE"), {0x5FB7, 0x56FD}},                    // 德国
        {QStringLiteral("FR"), {0x6CD5, 0x56FD}},                    // 法国
        {QStringLiteral("CA"), {0x52A0, 0x62FF, 0x5927}},            // 加拿大
        {QStringLiteral("AU"), {0x6FB3, 0x5927, 0x5229, 0x4E9A}},    // 澳大利亚
        {QStringLiteral("RU"), {0x4FC4, 0x7F57, 0x65AF}},            // 俄罗斯
        {QStringLiteral("IN"), {0x5370, 0x5EA6}},                    // 印度
        {QStringLiteral("NL"), {0x8377, 0x5170}},                    // 荷兰
        {QStringLiteral("MY"), {0x9A6C, 0x6765, 0x897F, 0x4E9A}},    // 马来西亚
        {QStringLiteral("TH"), {0x6CF0, 0x56FD}},                    // 泰国
        {QStringLiteral("PH"), {0x83F2, 0x5F8B, 0x6EE8}},            // 菲律宾
        {QStringLiteral("VN"), {0x8D8A, 0x5357}},                    // 越南
        {QStringLiteral("TR"), {0x571F, 0x8033, 0x5176}},            // 土耳其
    };
    for (const auto& entry : kLabels) {
        if (entry.first == asciiUpperCode) {
            return ucs4(entry.second);
        }
    }
    return ucs4({0x5176, 0x5B83}); // 其它
}

/// clean_node_name 里被替换成空格的字符集（'|' '*' ★ ☆ ⭐ 🌟 ⚡ ✨ 🔥 🚀 👑 💎
/// ▲ ▼ ◆ ◇ 【 】 [ ] ( ) （ ）），全部按码点写。
bool isDecorationCodePoint(uint codePoint)
{
    static const uint kCodePoints[] = {
        0x007C, 0x002A, 0x2605, 0x2606, 0x2B50, 0x1F31F, 0x26A1, 0x2728, 0x1F525,
        0x1F680, 0x1F451, 0x1F48E, 0x25B2, 0x25BC, 0x25C6, 0x25C7, 0x3010, 0x3011,
        0x005B, 0x005D, 0x0028, 0x0029, 0xFF08, 0xFF09,
    };
    for (const uint candidate : kCodePoints) {
        if (candidate == codePoint) {
            return true;
        }
    }
    return false;
}

} // namespace

bool looksLikeClash(const QString& content)
{
    const QString lower = Common::toAsciiLower(content);
    return lower.contains(QStringLiteral("proxies:"))
        || lower.contains(QStringLiteral("proxy-groups:"))
        || (lower.contains(QStringLiteral("port:")) && lower.contains(QStringLiteral("type:"))
            && lower.contains(QStringLiteral("server:")));
}

int nameQuality(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        return 0;
    }
    int score = 10;
    const QString lower = Common::toAsciiLower(n);
    if (lower.contains(QStringLiteral("telegram")) || lower.contains(QStringLiteral("join"))
        || lower.contains(QStringLiteral("http"))) {
        score -= 5;
    }
    // Rust 判的是 chars().count()：astral 字符在 UTF-16 里占两个单元，必须按码点算。
    if (Common::codePointCount(n) <= 40) {
        score += 2;
    }
    return score;
}

bool preferNode(const Models::Node& a, const Models::Node& b)
{
    // 对应 Rust 的 match (a.latency_ok, b.latency_ok)，分支顺序即优先级。
    const bool aOk = a.latencyOk.has_value() && *a.latencyOk;
    const bool aBad = a.latencyOk.has_value() && !*a.latencyOk;
    const bool bOk = b.latencyOk.has_value() && *b.latencyOk;
    const bool bBad = b.latencyOk.has_value() && !*b.latencyOk;
    const bool bothUnknown = !a.latencyOk.has_value() && !b.latencyOk.has_value();

    if (aOk && bOk) {
        if (a.latencyMs.has_value() && b.latencyMs.has_value() && *a.latencyMs != *b.latencyMs) {
            return *a.latencyMs < *b.latencyMs;
        }
        return a.updatedAt.nanos >= b.updatedAt.nanos;
    }
    if (aOk) {
        return true;
    }
    if (bOk) {
        return false;
    }
    if (aBad && bBad) {
        return a.updatedAt.nanos >= b.updatedAt.nanos;
    }
    if (bothUnknown) {
        const int aScore = nameQuality(a.name);
        const int bScore = nameQuality(b.name);
        if (aScore != bScore) {
            return aScore > bScore;
        }
        return a.updatedAt.nanos >= b.updatedAt.nanos;
    }
    if (!a.latencyOk.has_value() && bBad) {
        return true;
    }
    if (aBad && !b.latencyOk.has_value()) {
        return false;
    }
    return a.updatedAt.nanos >= b.updatedAt.nanos;
}

QVector<Models::Node> dedupNodes(const QVector<Models::Node>& nodes)
{
    // Rust 用 HashMap（输出顺序随机）；这里按键升序遍历，让同一批输入产出稳定顺序。
    QMap<QString, Models::Node> best;
    for (const Models::Node& node : nodes) {
        const QString key = nodeFingerprint(node);
        const auto found = best.find(key);
        if (found != best.end() && preferNode(found.value(), node)) {
            continue;
        }
        best.insert(key, node);
    }
    QVector<Models::Node> out;
    out.reserve(best.size());
    for (const Models::Node& node : best) {
        out.append(node);
    }
    return out;
}

Models::Node makeNode(const QString& protocol, const QString& name, const QString& server, int port,
                      const QString& raw, const Json& config, const QString& sourceId)
{
    const TimeUtil::OptionalUtc now = TimeUtil::OptionalUtc::now();
    Models::Node node;
    node.id = Ids::newUuidV4();
    node.sourceId = sourceId;
    node.protocol = protocol;
    node.name = name.isEmpty() ? QStringLiteral("%1:%2").arg(server, QString::number(port)) : name;
    node.server = server;
    node.port = port;
    node.raw = raw;
    node.config = config;
    node.country = guessCountry(name);
    node.createdAt = now;
    node.updatedAt = now;
    return node;
}

QString guessCountry(const QString& name)
{
    const QString n = Common::toAsciiLower(name);
    static const std::pair<const char*, const char*> kAscii[] = {
        {"hongkong", "HK"},        {"hong kong", "HK"},      {"taiwan", "TW"},
        {"tokyo", "JP"},           {"osaka", "JP"},          {"japan", "JP"},
        {"singapore", "SG"},       {"losangeles", "US"},     {"los angeles", "US"},
        {"america", "US"},         {"united states", "US"},  {"seoul", "KR"},
        {"korea", "KR"},           {"london", "GB"},         {"england", "GB"},
        {"germany", "DE"},         {"france", "FR"},         {"canada", "CA"},
        {"australia", "AU"},       {"russia", "RU"},         {"india", "IN"},
        {"turkey", "TR"},          {"netherlands", "NL"},    {"hk", "HK"},
        {"tw", "TW"},              {"jp", "JP"},             {"sg", "SG"},
        {"us", "US"},              {"usa", "US"},            {"kr", "KR"},
        {"uk", "GB"},              {"gb", "GB"},             {"de", "DE"},
        {"fr", "FR"},              {"ca", "CA"},             {"au", "AU"},
        {"ru", "RU"},              {"in", "IN"},             {"tr", "TR"},
        {"nl", "NL"},
    };
    for (const auto& entry : kAscii) {
        if (n.contains(QLatin1String(entry.first))) {
            return QString::fromUtf8(entry.second);
        }
    }
    // 中文关键词在原始名字上 contains（Rust 就是这么写的，不判大小写）。
    for (const ZhCountry& entry : zhCountryTable()) {
        if (name.contains(ucs4(entry.key))) {
            return QString::fromUtf8(entry.code);
        }
    }
    return QString();
}

QString cleanNodeName(const QString& name, const QString& countryHint, const QString& server,
                      int port)
{
    QString s = name.trimmed();
    s.replace(reUrl(), QString());
    s.replace(reDomain(), QString());
    s.replace(reScheme(), QString());
    s.replace(reBrackets(), QString());
    s.replace(rePromo(), QString());
    s.replace(reTraffic(), QString());
    s.replace(reTgHandle(), QString());

    // 去掉区域指示符 emoji（U+1F1E6..U+1F1FF）。
    QString filtered;
    filtered.reserve(s.size());
    for (qsizetype i = 0; i < s.size();) {
        qsizetype width = 1;
        const uint codePoint = Common::codePointAt(s, i, width);
        if (!(codePoint >= 0x1F1E6 && codePoint <= 0x1F1FF)) {
            filtered.append(s.mid(i, width));
        }
        i += width;
    }

    // 装饰字符换成空格，collapse 之后词还能切开。
    QString decorated;
    decorated.reserve(filtered.size());
    for (qsizetype i = 0; i < filtered.size();) {
        qsizetype width = 1;
        const uint codePoint = Common::codePointAt(filtered, i, width);
        decorated.append(isDecorationCodePoint(codePoint) ? QString(QLatin1Char(' '))
                                                          : filtered.mid(i, width));
        i += width;
    }

    s = decorated;
    s.replace(reSpaces(), QStringLiteral(" "));
    s = trimMatchedChars(s, isTrimNoiseChar);
    s = s.trimmed();

    // Rust: country_hint.map(to_string).or_else(|| guess_country(&s)).or_else(|| guess_country(name))
    QString countryCode = countryHint;
    if (countryCode.isNull()) {
        countryCode = guessCountry(s);
        if (countryCode.isNull()) {
            countryCode = guessCountry(name);
        }
    }
    if (!countryCode.isNull()) {
        const QString upper = toAsciiUpper(countryCode);
        const QString zhName = regionLabel(upper);
        QString cleanTail = s;
        cleanTail.replace(zhName, QString());
        cleanTail.replace(upper, QString());
        cleanTail.replace(Common::toAsciiLower(upper), QString());
        cleanTail = trimMatchedChars(cleanTail, isTrimTailChar);
        cleanTail = cleanTail.trimmed();
        if (cleanTail.isEmpty()) {
            return QStringLiteral("%1 %2:%3").arg(zhName, server, QString::number(port));
        }
        return QStringLiteral("%1 %2").arg(zhName, cleanTail);
    }
    if (s.isEmpty()) {
        return QStringLiteral("%1:%2").arg(server, QString::number(port));
    }
    return s;
}

Json emptyConfig()
{
    return Json::object();
}

QVector<Models::Node> parseSubscription(const QString& content, const QString& sourceId)
{
    const QString text = normalizeSubscriptionText(content).trimmed();
    if (text.isEmpty()) {
        return {};
    }

    // 1) 先看像不像 Clash YAML。
    if (looksLikeClash(text)) {
        const Result<QVector<Models::Node>> result = parseClashYaml(text, sourceId);
        if (result.isOk() && !result.value().isEmpty()) {
            return dedupNodes(result.value());
        }
    }

    // 2) 明文 / base64 / 混排文本里的分享链接。
    QStringList uris = extractUris(text);
    if (uris.isEmpty()) {
        if (const std::optional<QString> decoded = decodeMaybeBase64(text)) {
            const QString normalized = normalizeSubscriptionText(*decoded);
            uris = extractUris(normalized);
            if (uris.isEmpty() && looksLikeClash(normalized)) {
                const Result<QVector<Models::Node>> result = parseClashYaml(normalized, sourceId);
                if (result.isOk()) {
                    return dedupNodes(result.value());
                }
            }
        }
    }

    QVector<Models::Node> nodes;
    for (const QString& uri : uris) {
        if (const auto node = Uri::parseUri(uri, sourceId)) {
            nodes.append(*node);
        }
    }
    return dedupNodes(nodes);
}

} // namespace Parser
