#include "ip/IpRisk.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QStringList>
#include <QUrl>

#include <initializer_list>

#include "core/JsonUtil.h"
#include "ip/IpRiskMessages.h"
#include "net/HttpClient.h"

namespace IpRisk {

namespace {

struct Arg {
    QString marker;
    QString value;
};

/// Rust 的 `format!` 只解释模板一次，填进去的值不会被二次求值。这里同样单遍扫描，
/// 免得主机名里真出现 "{}" 之类的字面量时被连环替换。具名占位符先匹配，剩下的
/// `{}` / `{:?}` 按位置依次填掉。
QString fill(const QString &text, const std::initializer_list<Arg> &named,
             const std::initializer_list<QString> &positional = {})
{
    QStringList left = QStringList(positional);
    QString out;
    out.reserve(text.size() + 16);
    for (qsizetype index = 0; index < text.size();) {
        bool matched = false;
        if (text.at(index) == QLatin1Char('{')) {
            for (const Arg &arg : named) {
                if (!arg.marker.isEmpty() && text.mid(index, arg.marker.size()) == arg.marker) {
                    out += arg.value;
                    index += arg.marker.size();
                    matched = true;
                    break;
                }
            }
            if (!matched && !left.isEmpty() && text.mid(index, 4) == QLatin1String("{:?}")) {
                out += left.takeFirst();
                index += 4;
                matched = true;
            }
            if (!matched && !left.isEmpty() && text.mid(index, 2) == QLatin1String("{}")) {
                out += left.takeFirst();
                index += 2;
                matched = true;
            }
        }
        if (!matched) {
            out += text.at(index);
            ++index;
        }
    }
    return out;
}

/// 对应 Rust 的 `str::to_ascii_lowercase`：只对 ASCII 字母做折叠，非 ASCII 原样留着
/// （`QString::toLower()` 会连带把 İ 折成 i+组合符，判定结果就不一样了）。
QString toAsciiLower(const QString &text)
{
    QString out = text;
    for (QChar &ch : out) {
        const ushort code = ch.unicode();
        if (code >= 'A' && code <= 'Z') {
            ch = QChar(code + 32);
        }
    }
    return out;
}

/// 对应 Rust 的 `Option<String>::eq_ignore_ascii_case("hosting")`：缺失的 None 不算命中。
bool isHostingWord(const QString &value)
{
    return !value.isNull() && toAsciiLower(value) == QLatin1String("hosting");
}

/// Rust 的 `{candidate:?}`（str 的 Debug）：加引号、转义控制字符。
QString debugQuoted(const QString &text)
{
    QString out;
    out += QLatin1Char('"');
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        switch (code) {
        case 0x0022:  // 双引号
            out += QLatin1String("\\\"");
            break;
        case 0x005C:  // 反斜杠
            out += QLatin1String("\\\\");
            break;
        case 0x000A:
            out += QLatin1String("\\n");
            break;
        case 0x000D:
            out += QLatin1String("\\r");
            break;
        case 0x0009:
            out += QLatin1String("\\t");
            break;
        default:
            if (code < 0x20 || code == 0x7f) {
                out += QStringLiteral("\\u{%1}").arg(int(code), 0, 16);
            } else {
                out += ch;
            }
            break;
        }
    }
    out += QLatin1Char('"');
    return out;
}

/// ipinfo 响应里的 `company` 对象；缺失、为 null 或形状不对都当没有（Rust 的 serde
/// 严格到会整条解析失败，本仓库统一按 JsonUtil 的宽松约定处理）。
Json companyOf(const Json &info)
{
    if (!info.is_object()) {
        return Json();
    }
    const auto found = info.find("company");
    if (found == info.end() || !found->is_object()) {
        return Json();
    }
    return *found;
}

/// 对应 Rust `classify`：ipinfo 的 priv/company.kind 直接判 hosting，其次 ip-api 的
/// hosting 标志，最后才是 org / company.name 的关键字表。
bool classify(const Json &info, const std::optional<Json> &api)
{
    const bool kindIsHosting = isHostingWord(JsonUtil::nullableText(info, "priv"))
        || isHostingWord(JsonUtil::nullableText(companyOf(info), "kind"));
    if (kindIsHosting) {
        return true;
    }
    if (api.has_value() && JsonUtil::boolOr(*api, "hosting", false)) {
        return true;
    }
    const QString org = toAsciiLower(JsonUtil::nullableText(info, "org"));
    const QString company = toAsciiLower(JsonUtil::nullableText(companyOf(info), "name"));
    for (const QString &hint : Messages::kDatacenterHints) {
        if (org.contains(hint, Qt::CaseSensitive) || company.contains(hint, Qt::CaseSensitive)) {
            return true;
        }
    }
    return false;
}

/// Rust 的 IPv4 词法：恰好四段十进制、每段 1–3 位、不许前导零、每段 ≤255。
/// Qt 的 `QHostAddress::setAddress` 比这宽松，而"是不是 IP"决定了要不要走 DNS，所以先自己判。
bool isStrictIpv4(const QString &text)
{
    const QStringList groups = text.split(QLatin1Char('.'));
    if (groups.size() != 4) {
        return false;
    }
    for (const QString &group : groups) {
        if (group.isEmpty() || group.size() > 3) {
            return false;
        }
        for (const QChar ch : group) {
            if (ch.unicode() < '0' || ch.unicode() > '9') {
                return false;
            }
        }
        if (group.size() > 1 && group.at(0) == QLatin1Char('0')) {
            return false;
        }
        if (group.toInt() > 255) {
            return false;
        }
    }
    return true;
}

/// Rust 的 `Ipv6Addr` Display（RFC 5952：小写、压缩最长零段、v4 映射尾段写成点分十进制）。
/// Qt 的 `toString()` 不压缩，所以这里自己拼，保证 URL 和 `ip` 字段与 Rust 一致。
QString ipv6Text(const Q_IPV6ADDR &bytes)
{
    quint16 groups[8];
    for (int index = 0; index < 8; ++index) {
        groups[index] = quint16((quint16(quint8(bytes[index * 2])) << 8) | quint8(bytes[index * 2 + 1]));
    }
    const auto dottedTail = [&groups]() {
        return QStringLiteral("%1.%2.%3.%4")
            .arg(groups[6] >> 8)
            .arg(groups[6] & 0xff)
            .arg(groups[7] >> 8)
            .arg(groups[7] & 0xff);
    };
    bool leadingZeros = true;
    for (int index = 0; index < 5; ++index) {
        leadingZeros = leadingZeros && groups[index] == 0;
    }
    if (leadingZeros && groups[5] == 0xffff) {
        return QStringLiteral("::ffff:") + dottedTail();  // Rust: ::ffff:1.2.3.4
    }
    if (leadingZeros && groups[5] == 0 && groups[6] != 0) {
        return QStringLiteral("::") + dottedTail();  // Rust: ::1.2.3.4（v4-compatible）
    }
    // 最长的一整段零（长度 >=2），并列时取最靠前的，和 Rust 一致。
    int bestStart = -1;
    int bestLength = 0;
    int runStart = -1;
    for (int index = 0; index <= 8; ++index) {
        if (index < 8 && groups[index] == 0) {
            if (runStart < 0) {
                runStart = index;
            }
            continue;
        }
        const int length = runStart < 0 ? 0 : index - runStart;
        if (length > bestLength) {
            bestLength = length;
            bestStart = runStart;
        }
        runStart = -1;
    }
    if (bestLength < 2) {
        bestStart = -1;
    }
    QStringList head;
    QStringList tail;
    const int stop = bestStart < 0 ? 8 : bestStart;
    for (int index = 0; index < stop; ++index) {
        head << QString::number(groups[index], 16);
    }
    for (int index = bestStart < 0 ? 8 : bestStart + bestLength; index < 8; ++index) {
        tail << QString::number(groups[index], 16);
    }
    if (bestStart < 0) {
        return head.join(QLatin1Char(':'));
    }
    QString out = head.isEmpty() ? QStringLiteral("::") : head.join(QLatin1Char(':')) + QStringLiteral("::");
    out += tail.join(QLatin1Char(':'));
    return out;
}

/// QHostAddress 的文本，去掉 IPv6 的 scope id：Rust 的 `IpAddr` 根本没有作用域。
QString addressTextOf(const QHostAddress &address)
{
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        return ipv6Text(address.toIPv6Address());
    }
    QString text = address.toString();
    const int percent = text.indexOf(QLatin1Char('%'));
    if (percent >= 0) {
        text.chop(text.size() - percent);
    }
    return text;
}

/// Rust 侧 fetch_text 的 `what` 标签（会出现在错误文案 "{what} 查询失败" 里）。
const QString kWhatIpinfo = QStringLiteral("ipinfo");
const QString kWhatIpApi = QStringLiteral("ip-api");

}

QString Risk::headline() const
{
    const QString kind = datacenter ? Messages::kindDatacenter() : Messages::kindResidential();
    const QStringList parts {
        hostname == ip ? ip : fill(Messages::hostnameArrow(), {}, {hostname, ip}),
        country.isEmpty() ? Messages::unknownRegion() : country,
        org.isEmpty() ? Messages::noAsnInfo() : org,
        kind,
    };
    return parts.join(Messages::headlineJoiner());
}

Result<Risk> parse(const QString &hostname, const QString &ip, const QString &body,
                   const QString &apiBody)
{
    const Result<Json> parsed = JsonUtil::parse(body);
    if (parsed.isErr()) {
        return Result<Risk>::withError(
            fill(Messages::parseFailed(), {{QStringLiteral("{error}"), parsed.error()}}));
    }
    std::optional<Json> api;
    if (!apiBody.isNull()) {
        const Result<Json> apiParsed = JsonUtil::parse(apiBody);
        if (apiParsed.isOk()) {
            api = apiParsed.value();
        }
    }
    const Json &info = parsed.value();

    Risk risk;
    risk.ip = ip;
    risk.hostname = hostname;
    risk.datacenter = classify(info, api);
    risk.publicProxy = api.has_value() && JsonUtil::boolOr(*api, "proxy", false);
    risk.country = JsonUtil::nullableText(info, "country");
    risk.org = JsonUtil::nullableText(info, "org");
    if (risk.datacenter) {
        risk.verdict = Messages::verdictDatacenter();
    } else if (risk.publicProxy) {
        risk.verdict = Messages::verdictPublicProxy();
    } else {
        risk.verdict = Messages::verdictResidential();
    }
    return Result<Risk>::withValue(risk);
}

QString ipinfoUrl(const QString &address)
{
    return QStringLiteral("https://ipinfo.io/%1/json").arg(address);
}

QString ipApiUrl(const QString &address)
{
    // ip-api 免费档只有 HTTP，且不能经代理查询。
    return QStringLiteral("http://ip-api.com/json/%1?fields=status,hosting,proxy,mobile,as,isp")
        .arg(address);
}

const QStringList &egressEchoUrls()
{
    return Messages::kEgressEchoUrls;
}

QString whatIpinfo()
{
    return kWhatIpinfo;
}

QString whatIpApi()
{
    return kWhatIpApi;
}

QString addressText(const QString &text)
{
    if (text.isNull()) {
        return QString();
    }
    const bool maybeV6 = text.contains(QLatin1Char(':'));
    if (!maybeV6 && !isStrictIpv4(text)) {
        return QString();
    }
    QHostAddress address;
    if (!address.setAddress(text)) {
        return QString();
    }
    if (!maybeV6 && address.protocol() != QAbstractSocket::IPv4Protocol) {
        return QString();
    }
    return addressTextOf(address);
}

Result<QString> fetchText(const QString &url, const QString &what, const QString &proxyUrl)
{
    // Rust 用 reqwest::Client::new()（不设 UA）+ 每条请求 .timeout(8s)；Net::Request 的
    // userAgent 为空就是不发这个头，connectTimeout 保持缺省（reqwest 默认客户端没有连接超时）。
    Net::Request request;
    request.url = url;
    request.method = QByteArrayLiteral("GET");
    request.timeoutMs = 8000;
    request.proxyUrl = proxyUrl;

    const Result<Net::Response> sent = Net::send(request);
    if (sent.isErr()) {
        // `:#` 打的是 source 链最里层的原因，Qt 侧已经翻译成了同一层级的文本。
        return Result<QString>::withError(fill(
            Messages::queryFailed(), {{QStringLiteral("{what}"), what},
                                      {QStringLiteral("{error:#}"), sent.error()}}));
    }
    const Net::Response response = sent.value();
    if (!response.isSuccess()) {
        // Rust 这里是 .error_for_status()，文本形如 "HTTP status client error (404 Not Found) for url (...)"；
        // Net::Response 没带 reason phrase，所以只能给出状态码。
        return Result<QString>::withError(fill(
            Messages::queryFailed(), {{QStringLiteral("{what}"), what},
                                      {QStringLiteral("{error:#}"),
                                       QStringLiteral("HTTP status client error (%1) for url (%2)")
                                           .arg(response.status)
                                           .arg(url)}}));
    }
    return Result<QString>::withValue(QString::fromUtf8(response.body));
}

Result<QString> resolveFirst(const QString &host)
{
    // 阻塞式解析（对应 tokio 的 lookup_host）：和这个模块的其它入口一样，只在 worker 线程调用。
    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError) {
        return Result<QString>::withError(fill(
            Messages::resolveFailed(), {{QStringLiteral("{host}"), host},
                                        {QStringLiteral("{error}"), info.errorString()}}));
    }
    const QList<QHostAddress> addresses = info.addresses();
    // 优先 IPv4：ipinfo 对 IPv6 经常缺 org / country 字段。
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            return Result<QString>::withValue(addressTextOf(address));
        }
    }
    if (!addresses.isEmpty()) {
        return Result<QString>::withValue(addressTextOf(addresses.first()));
    }
    return Result<QString>::withError(
        fill(Messages::noRecords(), {{QStringLiteral("{host}"), host}}));
}

Status checkProxyUrl(const QString &proxyUrl)
{
    // 对应 reqwest::Proxy::all()：Url::parse 失败或 scheme 不是 http/https/socks* 都会在
    // 发第一个请求之前就报错，所以这里也提前失败。
    const QUrl url(proxyUrl);
    const QString scheme = url.scheme();
    static const QStringList kSchemes {QStringLiteral("http"), QStringLiteral("https"),
                                      QStringLiteral("socks5"), QStringLiteral("socks5h")};
    if (!url.isValid() || url.host().isEmpty() || !kSchemes.contains(scheme, Qt::CaseInsensitive)) {
        const QString detail = url.errorString().isEmpty()
            ? QStringLiteral("%1 (%2)").arg(proxyUrl, scheme.isEmpty() ? QStringLiteral("-") : scheme)
            : url.errorString();
        return Status::failed(fill(Messages::invalidProxy(), {{QStringLiteral("{error}"), detail}}));
    }
    return Status::succeeded();
}

EgressCandidate egressCandidate(const QString &body)
{
    // Rust: body.trim().split_whitespace().next().unwrap_or_default()
    const QString trimmed = body.trimmed();
    QString token = trimmed;
    for (qsizetype index = 0; index < trimmed.size(); ++index) {
        if (trimmed.at(index).isSpace()) {
            token = trimmed.left(index);
            break;
        }
    }
    EgressCandidate candidate;
    candidate.text = token;
    candidate.addressText = addressText(token);
    return candidate;
}

Result<Risk> classifyIp(const QString &label, const QString &address)
{
    return classifyIpWith(fetchText, label, address);
}

Result<Risk> classifyIpWith(const Fetcher &fetch, const QString &label, const QString &address)
{
    // ipinfo.io 对纯 IP 查询不需要 token。
    const Result<QString> body = fetch(ipinfoUrl(address), whatIpinfo(), QString());
    if (body.isErr()) {
        return Result<Risk>::withError(body.error());
    }
    // ip-api 挂了不影响结论，只是少一路证据（免费档只有 HTTP，且不能走代理）。
    const Result<QString> api = fetch(ipApiUrl(address), whatIpApi(), QString());
    const QString apiBody = api.isOk() ? api.value() : QString();
    return parse(label, address, body.value(), apiBody);
}

Result<Risk> lookup(const QString &server)
{
    return lookupWith(fetchText, resolveFirst, server);
}

Result<Risk> lookupWith(const Fetcher &fetch, const Resolver &resolve, const QString &server)
{
    QString address = addressText(server);
    if (address.isNull()) {
        const Result<QString> resolved = resolve(server);
        if (resolved.isErr()) {
            return Result<Risk>::withError(resolved.error());
        }
        address = resolved.value();
    }
    return classifyIpWith(fetch, server, address);
}

Result<Risk> lookupEgress(const QString &proxyUrl)
{
    const Status proxy = checkProxyUrl(proxyUrl);
    if (proxy.isErr()) {
        return Result<Risk>::withError(proxy.error());
    }
    return lookupEgressWith(fetchText, proxyUrl);
}

Result<Risk> lookupEgressWith(const Fetcher &fetch, const QString &proxyUrl)
{
    // 两个回显地址轮流试：内核每 300 秒换成员，某个出口打不通其中一家站点的概率不低。
    QString lastError = Messages::noEgressAddress();
    for (const QString &url : Messages::kEgressEchoUrls) {
        const Result<QString> response = fetch(url, Messages::egressLabel(), proxyUrl);
        if (response.isErr()) {
            lastError = response.error();
            continue;
        }
        const EgressCandidate candidate = egressCandidate(response.value());
        if (!candidate.addressText.isNull()) {
            // 回显拿到的是出口 IP，归属仍用直连查（label 用响应里的原词，ip 用规范化文本）。
            return classifyIpWith(fetch, candidate.text, candidate.addressText);
        }
        lastError = fill(Messages::notAnIp(),
                         {{QStringLiteral("{url}"), url},
                          {QStringLiteral("{candidate:?}"), debugQuoted(candidate.text)}});
    }
    return Result<Risk>::withError(
        fill(Messages::egressFailed(), {{QStringLiteral("{last_error}"), lastError}}));
}

}
