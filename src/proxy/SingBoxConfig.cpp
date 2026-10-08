#include "proxy/SingBoxConfig.h"

#include <QChar>
#include <QHostAddress>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <optional>

#include "proxy/KernelConfigMessages.h"

namespace Proxy {

const QString kSingBoxGeoSiteCnUrl =
    QStringLiteral("https://raw.githubusercontent.com/SagerNet/sing-geosite/rule-set/geosite-cn.srs");
const QString kSingBoxGeoSiteAdsUrl = QStringLiteral(
    "https://raw.githubusercontent.com/SagerNet/sing-geosite/rule-set/geosite-category-ads-all.srs");
const QString kSingBoxGeoSiteGfwUrl =
    QStringLiteral("https://raw.githubusercontent.com/SagerNet/sing-geosite/rule-set/geosite-gfw.srs");
const QString kSingBoxGeoIpCnUrl =
    QStringLiteral("https://raw.githubusercontent.com/SagerNet/sing-geoip/rule-set/geoip-cn.srs");

const QString kSingBoxGeoSiteCnTag = QStringLiteral("geosite-cn");
const QString kSingBoxGeoSiteAdsTag = QStringLiteral("geosite-category-ads-all");
const QString kSingBoxGeoSiteGfwTag = QStringLiteral("geosite-gfw");
const QString kSingBoxGeoIpCnTag = QStringLiteral("geoip-cn");

namespace {

/// 用 nlohmann 时注意：`find()` 对非对象返回 end()，所以这里不需要额外的 is_object 守卫。
Json findChild(const Json &value, const QString &key)
{
    const auto found = value.find(key.toUtf8().constData());
    return found == value.end() ? Json() : *found;
}

/// Rust 的 `to_ascii_lowercase` 只动 A-Z；QString::toLower() 会做全 Unicode 折叠，
/// 遇到 İ 之类会多出一个字符，所以这里自己按 ASCII 来。
QString toAsciiLower(const QString &text)
{
    QString out = text;
    for (QChar &ch : out) {
        if (ch.unicode() >= 0x41 && ch.unicode() <= 0x5A) {
            ch = QChar(ch.unicode() + 32);
        }
    }
    return out;
}

/// Rust 的 `str::parse::<std::net::IpAddr>()`：只接受纯 IPv4/IPv6 字面量，不接受前导零、
/// 三段式 "1.2.3"、八进制/十进制裸数（"1" != 0.0.0.1），也不带 scope id。
/// Qt 6.10 的 QHostAddress::setAddress(QString) 只有宽松模式这一种，所以自己判。
bool isIpv4Literal(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() != 4) {
        return false;
    }
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3) {
            return false;
        }
        if (part.size() > 1 && part.at(0) == QLatin1Char('0')) {
            return false;
        }
        for (const QChar &ch : part) {
            if (ch.unicode() < 0x30 || ch.unicode() > 0x39) {
                return false;
            }
        }
        if (part.toInt() > 255) {
            return false;
        }
    }
    return true;
}

bool isIpv6Literal(const QString &text)
{
    if (!text.contains(QLatin1Char(':')) || text.contains(QLatin1Char('%'))) {
        return false;
    }
    QHostAddress address;
    return address.setAddress(text) && address.protocol() == QHostAddress::IPv6Protocol;
}

bool isIpAddress(const QString &text)
{
    return isIpv4Literal(text) || isIpv6Literal(text);
}

/// Rust 的 `str::lines()`：按 \n 切，行尾紧跟的 \r 去掉，结尾的换行不产生额外空行，
/// 空串返回 0 行。这个语义决定"启动失败"取到哪一行内核日志。
QStringList rustLines(const QString &text)
{
    QStringList lines;
    if (text.isEmpty()) {
        return lines;
    }
    qsizetype start = 0;
    while (start < text.size()) {
        const qsizetype newline = text.indexOf(QLatin1Char('\n'), start);
        if (newline < 0) {
            lines.append(text.mid(start));
            break;
        }
        QString line = text.mid(start, newline - start);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        lines.append(line);
        start = newline + 1;
        if (start == text.size()) {
            break;
        }
    }
    return lines;
}

/// Rust 的 Option<&str>：nullopt = None。sing-box 侧的 string_value 会把 trim 后为空的
/// 值也当成 None，所以这里的有值恒为非空字符串。
using OptionalString = std::optional<QString>;

/// Rust: string_value —— 按顺序找第一个"是字符串且 trim 后非空"的键，返回 trim 后的文本。
OptionalString stringValue(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (!value.is_string()) {
            continue;
        }
        const QString text = QString::fromStdString(value.get<std::string>()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return std::nullopt;
}

/// Rust: required_string —— 没有可用字符串就报错，错误文本列出所有候选键。
Result<QString> requiredString(const Json &config, const QStringList &keys)
{
    const OptionalString value = stringValue(config, keys);
    if (!value.has_value()) {
        return Result<QString>::withError(
            Messages::fill(Messages::singBoxMissingField(), {{"{}", keys.join(QLatin1Char('/'))}}));
    }
    return Result<QString>::withValue(*value);
}

/// Rust: string_list_value —— 字符串当单元素数组，数组取其非空字符串项；全空 = None，
/// 不会退化成空数组（WireGuard 的 address 为空数组是配不起来的）。
std::optional<QStringList> stringListValue(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (value.is_string()) {
            const QString text = QString::fromStdString(value.get<std::string>()).trimmed();
            if (!text.isEmpty()) {
                return QStringList{text};
            }
            continue;
        }
        if (value.is_array()) {
            QStringList texts;
            for (const Json &item : value) {
                if (!item.is_string()) {
                    continue;
                }
                const QString text = QString::fromStdString(item.get<std::string>()).trimmed();
                if (!text.isEmpty()) {
                    texts.append(text);
                }
            }
            if (!texts.isEmpty()) {
                return texts;
            }
        }
    }
    return std::nullopt;
}

/// Rust: number_value —— as_u64() 或字符串再 parse::<u64>()；负数和小数都不是合法 u64。
std::optional<qint64> numberValue(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (value.is_number_unsigned()) {
            return value.get<qint64>();
        }
        if (value.is_string()) {
            bool ok = false;
            const unsigned long long parsed =
                QString::fromStdString(value.get<std::string>()).toULongLong(&ok);
            if (ok) {
                return qint64(parsed);
            }
        }
    }
    return std::nullopt;
}

/// Rust: bool_value —— 任一候选键为 true / "1|true|yes" / 非零整数即为真。
/// sing-box 与 xray 两个 Rust 文件里它都是没被调用到的私有工具，一并移植以保持 1:1。
bool boolValue(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (value.is_boolean()) {
            if (value.get<bool>()) {
                return true;
            }
            continue;
        }
        if (value.is_string()) {
            const QString text = toAsciiLower(QString::fromStdString(value.get<std::string>()));
            if (text == QLatin1String("1") || text == QLatin1String("true")
                || text == QLatin1String("yes")) {
                return true;
            }
            continue;
        }
        if (value.is_number_integer() && value.get<qint64>() != 0) {
            return true;
        }
    }
    return false;
}

Json arrayFromStrings(const QStringList &values)
{
    Json array = Json::array();
    for (const QString &value : values) {
        array.push_back(value.toStdString());
    }
    return array;
}

bool isWireGuard(const QString &protocol)
{
    const QString lowered = toAsciiLower(protocol);
    return lowered == QLatin1String("wireguard") || lowered == QLatin1String("wg");
}

Json directOutbound()
{
    Json direct;
    direct["type"] = "direct";
    direct["tag"] = "direct";
    return direct;
}

Json commonOutbound(const char *type, const Models::Node &node)
{
    Json outbound;
    outbound["type"] = type;
    outbound["tag"] = "proxy";
    outbound["server"] = node.server.toStdString();
    outbound["server_port"] = node.port;
    return outbound;
}

/// 远端订阅里的 skip-cert-verify/insecure 一律不采信：那等于让来源自己决定要不要被中间人。
/// 实测现有节点没有一个依赖它，所以 insecure 恒为 false。
Json tlsConfig(const Models::Node &node)
{
    const OptionalString serverName =
        stringValue(node.config, {QStringLiteral("sni"), QStringLiteral("server_name")});
    Json tls;
    tls["enabled"] = true;
    tls["server_name"] = (serverName.has_value() ? *serverName : node.server).toStdString();
    tls["insecure"] = false;
    const Json alpn = findChild(node.config, QStringLiteral("alpn"));
    if (alpn.is_array()) {
        tls["alpn"] = alpn;
    }
    return tls;
}

Result<Json> buildHysteria2(const Models::Node &node)
{
    const Result<QString> password =
        requiredString(node.config, {QStringLiteral("password"), QStringLiteral("auth")});
    if (password.isErr()) {
        return Result<Json>::withError(password.error());
    }
    Json outbound = commonOutbound("hysteria2", node);
    outbound["password"] = password.value().toStdString();
    const OptionalString obfsType = stringValue(node.config, {QStringLiteral("obfs")});
    if (obfsType.has_value()) {
        const Result<QString> obfsPassword = requiredString(
            node.config, {QStringLiteral("obfs-password"), QStringLiteral("obfs_password")});
        if (obfsPassword.isErr()) {
            return Result<Json>::withError(obfsPassword.error());
        }
        Json obfs;
        obfs["type"] = obfsType->toStdString();
        obfs["password"] = obfsPassword.value().toStdString();
        outbound["obfs"] = obfs;
    }
    outbound["tls"] = tlsConfig(node);
    return Result<Json>::withValue(outbound);
}

Result<Json> buildTuic(const Models::Node &node)
{
    const Result<QString> uuid = requiredString(node.config, {QStringLiteral("uuid")});
    if (uuid.isErr()) {
        return Result<Json>::withError(uuid.error());
    }
    const Result<QString> password = requiredString(node.config, {QStringLiteral("password")});
    if (password.isErr()) {
        return Result<Json>::withError(password.error());
    }
    Json outbound = commonOutbound("tuic", node);
    outbound["uuid"] = uuid.value().toStdString();
    outbound["password"] = password.value().toStdString();
    const OptionalString congestion =
        stringValue(node.config, {QStringLiteral("congestion-controller"),
                                  QStringLiteral("congestion_control")});
    outbound["congestion_control"] = congestion.value_or(QStringLiteral("cubic")).toStdString();
    outbound["udp_relay_mode"] = "native";
    outbound["tls"] = tlsConfig(node);
    return Result<Json>::withValue(outbound);
}

Result<Json> buildAnytls(const Models::Node &node)
{
    const Result<QString> password = requiredString(node.config, {QStringLiteral("password")});
    if (password.isErr()) {
        return Result<Json>::withError(password.error());
    }
    Json outbound = commonOutbound("anytls", node);
    outbound["password"] = password.value().toStdString();
    outbound["tls"] = tlsConfig(node);
    return Result<Json>::withValue(outbound);
}

/// Rust: build_proxy_outbound —— 只认这三个 QUIC 系协议，其它报错而不是降级成 ss/vmess。
Result<Json> buildProxyOutbound(const Models::Node &node)
{
    const QString protocol = toAsciiLower(node.protocol);
    if (protocol == QLatin1String("hysteria2") || protocol == QLatin1String("hy2")) {
        return buildHysteria2(node);
    }
    if (protocol == QLatin1String("tuic")) {
        return buildTuic(node);
    }
    if (protocol == QLatin1String("anytls")) {
        return buildAnytls(node);
    }
    return Result<Json>::withError(
        Messages::fill(Messages::singBoxProtocolUnsupported(), {{"{}", node.protocol}}));
}

/// WireGuard 在 sing-box 里是 endpoint（不是 outbound），tag 仍是 "proxy"。
Result<Json> buildWireGuardEndpoint(const Models::Node &node)
{
    const Result<QString> privateKey =
        requiredString(node.config, {QStringLiteral("private-key"), QStringLiteral("private_key")});
    if (privateKey.isErr()) {
        return Result<Json>::withError(privateKey.error());
    }
    const Result<QString> publicKey =
        requiredString(node.config, {QStringLiteral("public-key"), QStringLiteral("public_key")});
    if (publicKey.isErr()) {
        return Result<Json>::withError(publicKey.error());
    }
    const std::optional<QStringList> address =
        stringListValue(node.config, {QStringLiteral("ip"), QStringLiteral("address"),
                                      QStringLiteral("local-address"),
                                      QStringLiteral("local_address")});
    if (!address.has_value()) {
        return Result<Json>::withError(Messages::singBoxWgMissingAddress());
    }
    const std::optional<QStringList> allowedIps =
        stringListValue(node.config, {QStringLiteral("allowed-ips"), QStringLiteral("allowed_ips")});

    Json peer;
    peer["address"] = node.server.toStdString();
    peer["port"] = node.port;
    peer["public_key"] = publicKey.value().toStdString();
    peer["allowed_ips"] = arrayFromStrings(allowedIps.has_value()
        ? *allowedIps
        : QStringList{QStringLiteral("0.0.0.0/0"), QStringLiteral("::/0")});
    const OptionalString preSharedKey =
        stringValue(node.config, {QStringLiteral("pre-shared-key"), QStringLiteral("pre_shared_key"),
                                  QStringLiteral("preshared-key")});
    if (preSharedKey.has_value()) {
        peer["pre_shared_key"] = preSharedKey->toStdString();
    }
    const Json reserved = findChild(node.config, QStringLiteral("reserved"));
    if (reserved.is_array()) {
        peer["reserved"] = reserved;
    }
    const std::optional<qint64> keepalive =
        numberValue(node.config, {QStringLiteral("persistent-keepalive"),
                                  QStringLiteral("persistent_keepalive_interval")});
    if (keepalive.has_value()) {
        peer["persistent_keepalive_interval"] = *keepalive;
    }

    Json endpoint;
    endpoint["type"] = "wireguard";
    endpoint["tag"] = "proxy";
    endpoint["address"] = arrayFromStrings(*address);
    endpoint["private_key"] = privateKey.value().toStdString();
    Json peers = Json::array();
    peers.push_back(peer);
    endpoint["peers"] = peers;
    const std::optional<qint64> mtu = numberValue(node.config, {QStringLiteral("mtu")});
    if (mtu.has_value()) {
        endpoint["mtu"] = *mtu;
    }
    return Result<Json>::withValue(endpoint);
}

/// Rust: rule_action —— Reject 在这里是空串，随后由 with_reject_action 换成 "reject"。
QString singBoxRuleAction(RuleAction action)
{
    switch (action) {
    case RuleAction::Direct:
        return QStringLiteral("direct");
    case RuleAction::Proxy:
        return QStringLiteral("proxy");
    case RuleAction::Reject:
        return QString();
    }
    return QString();
}

Json withRejectAction(Json value)
{
    if (value.is_object()) {
        value["action"] = "reject";
        value.erase("outbound");
    }
    return value;
}

void pushRuleSet(Json &ruleSets, QSet<QString> &seen, const QString &tag, const QString &url)
{
    if (seen.contains(tag)) {
        return;
    }
    seen.insert(tag);
    Json ruleSet;
    ruleSet["type"] = "remote";
    ruleSet["tag"] = tag.toStdString();
    ruleSet["format"] = "binary";
    ruleSet["url"] = url.toStdString();
    ruleSet["download_detour"] = "direct";
    ruleSet["update_interval"] = "24h";
    ruleSets.push_back(ruleSet);
}

struct RouteRules {
    Json rules = Json::array();
    Json ruleSets = Json::array();
};

/// Rust: build_route_rules —— 分类不在 sing-box 官方 rule-set 里就必须报错：
/// 静默丢一条规则等于让本该走代理（或被拦掉）的流量按 final 走。
Result<RouteRules> buildRouteRules(const MihomoConfigOptions &options)
{
    QVector<TrafficRule> selectedRules;
    switch (options.routingMode) {
    case RoutingMode::Rule:
        selectedRules = optimizeTrafficRules(options.rules);
        break;
    case RoutingMode::ChinaDirect:
        selectedRules = recommendedTrafficRules();
        break;
    case RoutingMode::GfwList:
        selectedRules = gfwlistTrafficRules();
        break;
    case RoutingMode::Global:
    case RoutingMode::Direct:
        selectedRules.clear();
        break;
    }

    RouteRules built;
    QSet<QString> seenRuleSets;
    for (const TrafficRule &rule : selectedRules) {
        const QString action = singBoxRuleAction(rule.action);
        Json value;
        switch (rule.matcher.kind) {
        case RuleMatcher::Kind::Domain:
            value["domain"] = arrayFromStrings({rule.matcher.value});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        case RuleMatcher::Kind::DomainSuffix:
            value["domain_suffix"] = arrayFromStrings({rule.matcher.value});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        case RuleMatcher::Kind::IpCidr:
            value["ip_cidr"] = arrayFromStrings({rule.matcher.value});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        case RuleMatcher::Kind::DestinationPort: {
            bool ok = false;
            const unsigned long long parsed = rule.matcher.value.toULongLong(&ok);
            if (!ok || parsed > 65535ULL) {
                return Result<RouteRules>::withError(
                    Messages::fill(Messages::singBoxPortRuleInvalid(),
                                   {{"{value}", rule.matcher.value}}));
            }
            value["port"] = Json::array({parsed});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        }
        case RuleMatcher::Kind::ProcessName:
            // sing-box 有 process_name 字段，照原样输出（Rust 也是直接给）；进程名匹配
            // 只在 TUN 入站下有意义，这是内核自己的限制，不在这里替它判断。
            value["process_name"] = arrayFromStrings({rule.matcher.value});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        case RuleMatcher::Kind::Network:
            value["network"] = rule.matcher.network == NetworkProtocol::Tcp ? "tcp" : "udp";
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        case RuleMatcher::Kind::Quic: {
            // QUIC 只能表达成"udp + 443 直接拒"，与规则自带的动作无关。
            value["network"] = "udp";
            value["port"] = Json::array({443});
            value["action"] = "reject";
            break;
        }
        case RuleMatcher::Kind::GeoSite: {
            const QString category = toAsciiLower(rule.matcher.value);
            QString tag;
            QString url;
            if (category == QLatin1String("cn")) {
                tag = kSingBoxGeoSiteCnTag;
                url = kSingBoxGeoSiteCnUrl;
            } else if (category == QLatin1String("category-ads-all")) {
                tag = kSingBoxGeoSiteAdsTag;
                url = kSingBoxGeoSiteAdsUrl;
            } else if (category == QLatin1String("gfw")) {
                tag = kSingBoxGeoSiteGfwTag;
                url = kSingBoxGeoSiteGfwUrl;
            } else {
                return Result<RouteRules>::withError(
                    Messages::fill(Messages::singBoxGeoSiteUnsupported(),
                                   {{"{value}", rule.matcher.value}}));
            }
            pushRuleSet(built.ruleSets, seenRuleSets, tag, url);
            value["rule_set"] = arrayFromStrings({tag});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        }
        case RuleMatcher::Kind::GeoIp: {
            if (toAsciiLower(rule.matcher.value) != QLatin1String("cn")) {
                return Result<RouteRules>::withError(
                    Messages::fill(Messages::singBoxGeoIpUnsupported(),
                                   {{"{value}", rule.matcher.value}}));
            }
            pushRuleSet(built.ruleSets, seenRuleSets, kSingBoxGeoIpCnTag, kSingBoxGeoIpCnUrl);
            value["rule_set"] = arrayFromStrings({kSingBoxGeoIpCnTag});
            value["action"] = "route";
            value["outbound"] = action.toStdString();
            break;
        }
        }
        built.rules.push_back(rule.action == RuleAction::Reject ? withRejectAction(value) : value);
    }
    return Result<RouteRules>::withValue(built);
}

/// Rust: build_dns。明文 IP → udp:53；URL → 拆成 https 的 server/server_port/path + tls。
Result<Json> buildDns(const MihomoConfigOptions &options)
{
    Json servers = Json::array();
    Json local;
    local["type"] = "local";
    local["tag"] = "local";
    servers.push_back(local);

    for (qsizetype index = 0; index < options.dns.nameservers.size(); ++index) {
        const QString &server = options.dns.nameservers.at(index);
        const QString tag = QStringLiteral("dns-%1").arg(index);
        if (isIpAddress(server)) {
            Json entry;
            entry["type"] = "udp";
            entry["tag"] = tag.toStdString();
            entry["server"] = server.toStdString();
            entry["server_port"] = 53;
            servers.push_back(entry);
            continue;
        }
        const QUrl url(server);
        if (!url.isValid()) {
            return Result<Json>::withError(
                Messages::fill(Messages::dohUrlInvalid(), {{"{error}", url.errorString()}}));
        }
        const QString host = url.host();
        if (host.isEmpty()) {
            return Result<Json>::withError(
                Messages::fill(Messages::dohUrlMissingHost(), {{"{server}", server}}));
        }
        const int port = url.port(-1);
        Json entry;
        entry["type"] = "https";
        entry["tag"] = tag.toStdString();
        entry["server"] = host.toStdString();
        entry["server_port"] = port > 0 ? port : 443;
        // Rust 的 url.path() 是未解码的原始路径，这里同样按 FullyEncoded 取。
        entry["path"] = url.path(QUrl::FullyEncoded).toStdString();
        Json tls;
        tls["enabled"] = true;
        tls["server_name"] = host.toStdString();
        entry["tls"] = tls;
        if (!isIpAddress(host)) {
            entry["domain_resolver"] = "local";
        }
        servers.push_back(entry);
    }

    Json dns;
    dns["servers"] = servers;
    dns["final"] = "dns-0";
    dns["strategy"] = options.dns.ipv6 ? "prefer_ipv4" : "ipv4_only";
    return Result<Json>::withValue(dns);
}

QString singBoxTunStack(TunStack stack)
{
    switch (stack) {
    case TunStack::System:
        return QStringLiteral("system");
    case TunStack::Gvisor:
        return QStringLiteral("gvisor");
    case TunStack::Mixed:
        return QStringLiteral("mixed");
    }
    return QStringLiteral("mixed");
}

/// route.final：sing-box 用出口 tag 表达默认动作，"proxy"/"direct" 两个出口恒在配置里。
QString singBoxFinalOutbound(RoutingMode mode)
{
    switch (mode) {
    case RoutingMode::Rule:
    case RoutingMode::ChinaDirect:
    case RoutingMode::Global:
        return QStringLiteral("proxy");
    case RoutingMode::GfwList:
    case RoutingMode::Direct:
        return QStringLiteral("direct");
    }
    return QStringLiteral("proxy");
}

MihomoConfigOptions batchOptions(quint16 mixedPort)
{
    MihomoConfigOptions options;
    options.mixedPort = mixedPort;
    return options;
}

}

qsizetype singBoxBatchCount(qsizetype nodeCount)
{
    return nodeCount <= 0 ? 0 : (nodeCount + kSingBoxBatchSize - 1) / kSingBoxBatchSize;
}

QString lastSingBoxLogLine(const QString &text)
{
    const QStringList lines = rustLines(text);
    for (qsizetype index = lines.size() - 1; index >= 0; --index) {
        const QString line = lines.at(index).trimmed();
        if (!line.isEmpty()) {
            return line;
        }
    }
    return Messages::singBoxNoLogLine();
}

Result<Json> buildSingBoxConfig(const Models::Node &node, MihomoConfigOptions options)
{
    const Status valid = options.validate();
    if (valid.isErr()) {
        return Result<Json>::withError(valid.error());
    }

    const bool wireguard = isWireGuard(node.protocol);
    const Result<Json> proxyResult =
        wireguard ? buildWireGuardEndpoint(node) : buildProxyOutbound(node);
    if (proxyResult.isErr()) {
        return Result<Json>::withError(proxyResult.error());
    }
    const Json proxy = proxyResult.value();

    Json inbounds = Json::array();
    Json mixed;
    mixed["type"] = "mixed";
    mixed["tag"] = "mixed-in";
    mixed["listen"] = options.allowLan ? "0.0.0.0" : "127.0.0.1";
    mixed["listen_port"] = options.mixedPort;
    inbounds.push_back(mixed);
    if (options.mode == ProxyMode::Tun) {
        Json tun;
        tun["type"] = "tun";
        tun["tag"] = "tun-in";
        tun["address"] = arrayFromStrings({QStringLiteral("172.19.0.1/30")});
        tun["auto_route"] = options.tun.autoRoute;
        tun["strict_route"] = true;
        tun["stack"] = singBoxTunStack(options.tun.stack).toStdString();
        inbounds.push_back(tun);
    }

    // Rust 先把规则表按模式写进一份 options 副本，再让 build_route_rules 按同一模式重新映射；
    // 结果一样，保留这个形状是为了和 Rust 逐行对照。
    MihomoConfigOptions routingOptions = options;
    switch (options.routingMode) {
    case RoutingMode::Rule:
        routingOptions.rules = options.rules;
        break;
    case RoutingMode::ChinaDirect:
        routingOptions.rules = recommendedTrafficRules();
        break;
    case RoutingMode::GfwList:
        routingOptions.rules = gfwlistTrafficRules();
        break;
    case RoutingMode::Global:
    case RoutingMode::Direct:
        routingOptions.rules.clear();
        break;
    }

    const Result<RouteRules> routeRules = buildRouteRules(routingOptions);
    if (routeRules.isErr()) {
        return Result<Json>::withError(routeRules.error());
    }
    Json rules = routeRules.value().rules;
    const Json ruleSets = routeRules.value().ruleSets;
    if (options.mode == ProxyMode::Tun) {
        // TUN 下先劫持 DNS 查询，否则 fake-ip 段的域名永远进不了后面的规则。
        Json hijack;
        hijack["protocol"] = "dns";
        hijack["action"] = "hijack-dns";
        rules.insert(rules.begin(), hijack);
    }

    Json route;
    route["auto_detect_interface"] = true;
    route["default_domain_resolver"] = "local";
    route["rules"] = rules;
    route["final"] = singBoxFinalOutbound(options.routingMode).toStdString();
    if (!ruleSets.empty()) {
        route["rule_set"] = ruleSets;
    }

    const Result<Json> dns = buildDns(options);
    if (dns.isErr()) {
        return Result<Json>::withError(dns.error());
    }

    Json outbounds = Json::array();
    outbounds.push_back(proxy);
    outbounds.push_back(directOutbound());

    Json log;
    log["level"] = "info";
    log["timestamp"] = true;

    Json config;
    config["log"] = log;
    config["dns"] = dns.value();
    config["inbounds"] = inbounds;
    config["outbounds"] = outbounds;
    config["route"] = route;
    if (wireguard) {
        Json endpoints = Json::array();
        endpoints.push_back(proxy);
        config["endpoints"] = endpoints;
        Json onlyDirect = Json::array();
        onlyDirect.push_back(directOutbound());
        config["outbounds"] = onlyDirect;
    }
    return Result<Json>::withValue(config);
}

Result<SingBoxBatchProbePlan> buildSingBoxBatchProbePlan(const QVector<Models::Node> &nodes,
                                                         const QVector<quint16> &ports)
{
    if (nodes.size() != ports.size()) {
        // Rust 侧靠索引越界 panic 暴露这个调用方错误；C++ 里越界是 UB，必须显式失败。
        return Result<SingBoxBatchProbePlan>::withError(
            QStringLiteral("buildSingBoxBatchProbePlan: ports.size() != nodes.size()"));
    }

    SingBoxBatchProbePlan plan;
    plan.failures.resize(nodes.size());
    Json inbounds = Json::array();
    Json outbounds = Json::array();
    outbounds.push_back(directOutbound());
    Json endpoints = Json::array();
    Json rules = Json::array();

    for (qsizetype index = 0; index < nodes.size(); ++index) {
        const Models::Node &node = nodes.at(index);
        const Result<Json> config = buildSingBoxConfig(node, batchOptions(ports.at(index)));
        if (config.isErr()) {
            plan.failures[index] = Messages::fill(Messages::singBoxConfigUnsupported(),
                                                  {{"{error}", config.error()}});
            continue;
        }

        SingBoxBatchProbeEntry entry;
        entry.nodeIndex = index;
        entry.port = ports.at(index);
        entry.inboundTag = QStringLiteral("probe-in-%1").arg(index);
        entry.outboundTag = QStringLiteral("probe-out-%1").arg(index);

        Json inbound = config.value().at("inbounds").at(0);
        inbound["tag"] = entry.inboundTag.toStdString();
        inbounds.push_back(inbound);

        // WireGuard 节点在 endpoints 里，普通协议在 outbounds 里：两处都打同一个 tag，
        // 规则的 outbound 引用不区分它们（sing-box 按 tag 全局找出口）。
        const Json &document = config.value();
        const Json endpoint = findChild(document, QStringLiteral("endpoints"));
        const Json firstEndpoint =
            endpoint.is_array() && !endpoint.empty() ? endpoint.at(0) : Json();
        const Json outboundList = findChild(document, QStringLiteral("outbounds"));
        const Json firstOutbound =
            outboundList.is_array() && !outboundList.empty() ? outboundList.at(0) : Json();

        if (firstEndpoint.is_object()) {
            Json tagged = firstEndpoint;
            tagged["tag"] = entry.outboundTag.toStdString();
            endpoints.push_back(tagged);
        } else if (firstOutbound.is_object()) {
            Json tagged = firstOutbound;
            tagged["tag"] = entry.outboundTag.toStdString();
            outbounds.push_back(tagged);
        }

        Json rule;
        rule["inbound"] = arrayFromStrings({entry.inboundTag});
        rule["action"] = "route";
        rule["outbound"] = entry.outboundTag.toStdString();
        rules.push_back(rule);

        plan.entries.append(entry);
    }

    // Rust: `if inbounds.is_empty() { return Ok(immediate) }` —— 一个都没建成就不起进程。
    if (inbounds.empty()) {
        return Result<SingBoxBatchProbePlan>::withValue(plan);
    }
    plan.hasInbounds = true;

    const Json &readyInbound = inbounds.at(0);
    const Json listenPort = findChild(readyInbound, QStringLiteral("listen_port"));
    plan.readyPort =
        listenPort.is_number_integer() ? quint16(listenPort.get<qint64>()) : ports.value(0);

    // Rust 用的是 MihomoConfigOptions::default()（mixed_port 不影响 dns 段）。
    const Result<Json> dns = buildDns(MihomoConfigOptions());
    if (dns.isErr()) {
        return Result<SingBoxBatchProbePlan>::withError(dns.error());
    }

    Json log;
    log["level"] = "warning";
    log["timestamp"] = true;
    Json route;
    route["auto_detect_interface"] = true;
    route["default_domain_resolver"] = "local";
    route["rules"] = rules;
    route["final"] = "direct";

    Json config;
    config["log"] = log;
    config["dns"] = dns.value();
    config["inbounds"] = inbounds;
    config["outbounds"] = outbounds;
    config["route"] = route;
    if (!endpoints.empty()) {
        config["endpoints"] = endpoints;
    }
    plan.config = config;
    return Result<SingBoxBatchProbePlan>::withValue(plan);
}

}
