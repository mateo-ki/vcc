#include "proxy/ConfigTypes.h"

#include <QHash>
#include <QHostAddress>

#include <algorithm>

namespace Proxy {

namespace {

QStringList defaultProxyServerNameserver()
{
    return {QLatin1String(kDefaultDnsPrimary), QLatin1String(kDefaultDnsSecondary)};
}

/// 旧版本的明文默认值；保存过的配置命中它们时迁移到 DoH。
const QStringList kLegacyDefaultPlaintext = {QStringLiteral("8.8.8.8"),
                                             QStringLiteral("114.114.114.114")};
const QStringList kLegacyDefaultDoh = {QStringLiteral("https://1.1.1.1/dns-query")};
const QStringList kPreviousDefaultDoh = {QStringLiteral("https://223.5.5.5/dns-query")};

bool isPlausibleIp(const QString &text)
{
    QHostAddress address;
    return address.setAddress(text);
}

RuleMatcher::Kind matcherKindFromText(const QString &text)
{
    static const QHash<QString, RuleMatcher::Kind> kinds = {
        {QStringLiteral("domain"), RuleMatcher::Kind::Domain},
        {QStringLiteral("domain_suffix"), RuleMatcher::Kind::DomainSuffix},
        {QStringLiteral("geo_site"), RuleMatcher::Kind::GeoSite},
        {QStringLiteral("geo_ip"), RuleMatcher::Kind::GeoIp},
        {QStringLiteral("ip_cidr"), RuleMatcher::Kind::IpCidr},
        {QStringLiteral("destination_port"), RuleMatcher::Kind::DestinationPort},
        {QStringLiteral("process_name"), RuleMatcher::Kind::ProcessName},
        {QStringLiteral("network"), RuleMatcher::Kind::Network},
        {QStringLiteral("quic"), RuleMatcher::Kind::Quic},
    };
    return kinds.value(text, RuleMatcher::Kind::Domain);
}

QString matcherKindToText(RuleMatcher::Kind kind)
{
    switch (kind) {
    case RuleMatcher::Kind::Domain:
        return QStringLiteral("domain");
    case RuleMatcher::Kind::DomainSuffix:
        return QStringLiteral("domain_suffix");
    case RuleMatcher::Kind::GeoSite:
        return QStringLiteral("geo_site");
    case RuleMatcher::Kind::GeoIp:
        return QStringLiteral("geo_ip");
    case RuleMatcher::Kind::IpCidr:
        return QStringLiteral("ip_cidr");
    case RuleMatcher::Kind::DestinationPort:
        return QStringLiteral("destination_port");
    case RuleMatcher::Kind::ProcessName:
        return QStringLiteral("process_name");
    case RuleMatcher::Kind::Network:
        return QStringLiteral("network");
    case RuleMatcher::Kind::Quic:
        return QStringLiteral("quic");
    }
    return QStringLiteral("domain");
}

/// 对应 Rust 的 render_value_rule：值不能为空，也不能带逗号或换行——它们会拆坏规则行。
Result<QString> renderValueRule(const QString &kind, const QString &rawValue,
                                const QString &target)
{
    const QString value = rawValue.trimmed();
    if (value.isEmpty()) {
        return Result<QString>::withError(
            QStringLiteral("%1 规则值不能为空").arg(kind));
    }
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r'))) {
        return Result<QString>::withError(QStringLiteral("%1 规则值包含非法分隔符").arg(kind));
    }
    return Result<QString>::withValue(
        QStringLiteral("%1,%2,%3").arg(kind, value, target));
}

}

QString routingModeToText(RoutingMode mode)
{
    switch (mode) {
    case RoutingMode::Rule:
        return QStringLiteral("rule");
    case RoutingMode::ChinaDirect:
        return QStringLiteral("china_direct");
    case RoutingMode::GfwList:
        return QStringLiteral("gfw_list");
    case RoutingMode::Global:
        return QStringLiteral("global");
    case RoutingMode::Direct:
        return QStringLiteral("direct");
    }
    return QStringLiteral("rule");
}

RoutingMode routingModeFromText(const QString &text)
{
    if (text == QLatin1String("china_direct")) {
        return RoutingMode::ChinaDirect;
    }
    if (text == QLatin1String("gfw_list")) {
        return RoutingMode::GfwList;
    }
    if (text == QLatin1String("global")) {
        return RoutingMode::Global;
    }
    if (text == QLatin1String("direct")) {
        return RoutingMode::Direct;
    }
    return RoutingMode::Rule;
}

QString tunStackToText(TunStack stack)
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

TunStack tunStackFromText(const QString &text)
{
    if (text == QLatin1String("system")) {
        return TunStack::System;
    }
    if (text == QLatin1String("gvisor")) {
        return TunStack::Gvisor;
    }
    return TunStack::Mixed;
}

QString tunStackToMihomoValue(TunStack stack)
{
    return tunStackToText(stack);
}

Json TunConfig::toJson() const
{
    Json value;
    value["stack"] = tunStackToText(stack).toStdString();
    Json hijack = Json::array();
    for (const QString &entry : dnsHijack) {
        hijack.push_back(entry.toStdString());
    }
    value["dns_hijack"] = hijack;
    value["auto_route"] = autoRoute;
    value["auto_detect_interface"] = autoDetectInterface;
    return value;
}

TunConfig TunConfig::fromJson(const Json &value)
{
    TunConfig config;
    config.stack = tunStackFromText(JsonUtil::textOrEmpty(value, "stack"));
    if (value.is_object() && value.contains("dns_hijack") && value.at("dns_hijack").is_array()) {
        QStringList entries;
        for (const Json &entry : value.at("dns_hijack")) {
            if (entry.is_string()) {
                entries.append(QString::fromStdString(entry.get<std::string>()));
            }
        }
        config.dnsHijack = entries;
    }
    config.autoRoute = JsonUtil::boolOr(value, "auto_route", true);
    config.autoDetectInterface = JsonUtil::boolOr(value, "auto_detect_interface", true);
    return config;
}

QString dnsEnhancedModeToText(DnsEnhancedMode mode)
{
    return mode == DnsEnhancedMode::FakeIp ? QStringLiteral("fake_ip") : QStringLiteral("redir_host");
}

DnsEnhancedMode dnsEnhancedModeFromText(const QString &text)
{
    return text == QLatin1String("redir_host") ? DnsEnhancedMode::RedirHost
                                               : DnsEnhancedMode::FakeIp;
}

QString dnsEnhancedModeToMihomoValue(DnsEnhancedMode mode)
{
    return mode == DnsEnhancedMode::FakeIp ? QStringLiteral("fake-ip")
                                           : QStringLiteral("redir-host");
}

bool DnsConfig::migrateLegacyDefaults()
{
    bool migrated = false;
    if (nameservers == kLegacyDefaultPlaintext || nameservers == kLegacyDefaultDoh
        || nameservers == kPreviousDefaultDoh) {
        nameservers = {QLatin1String(kDefaultDnsPrimary), QLatin1String(kDefaultDnsSecondary)};
        migrated = true;
    }
    if (proxyServerNameserver.isEmpty()) {
        proxyServerNameserver = defaultProxyServerNameserver();
        migrated = true;
    }
    return migrated;
}

Status DnsConfig::validate() const
{
    if (enabled && nameservers.isEmpty()) {
        return Status::failed(QStringLiteral("启用 DNS 时至少需要一个 DNS 地址"));
    }
    for (const QString &server : nameservers) {
        if (!isPlausibleIp(server) && !server.startsWith(QLatin1String("https://"))) {
            return Status::failed(
                QStringLiteral("DNS 地址必须是 IP 或 HTTPS URL: %1").arg(server));
        }
    }
    for (const QString &server : proxyServerNameserver) {
        if (!isPlausibleIp(server) && !server.startsWith(QLatin1String("https://"))) {
            return Status::failed(
                QStringLiteral("proxy-server-nameserver 地址必须是 IP 或 HTTPS URL: %1")
                    .arg(server));
        }
    }
    return Status::succeeded();
}

Json DnsConfig::toJson() const
{
    Json value;
    value["enabled"] = enabled;
    value["ipv6"] = ipv6;
    value["enhanced_mode"] = dnsEnhancedModeToText(enhancedMode).toStdString();
    Json servers = Json::array();
    for (const QString &server : nameservers) {
        servers.push_back(server.toStdString());
    }
    value["nameservers"] = servers;
    Json proxyServers = Json::array();
    for (const QString &server : proxyServerNameserver) {
        proxyServers.push_back(server.toStdString());
    }
    value["proxy_server_nameserver"] = proxyServers;
    return value;
}

DnsConfig DnsConfig::fromJson(const Json &value)
{
    DnsConfig config;
    config.enabled = JsonUtil::boolOr(value, "enabled", true);
    config.ipv6 = JsonUtil::boolOr(value, "ipv6", false);
    config.enhancedMode = dnsEnhancedModeFromText(JsonUtil::textOrEmpty(value, "enhanced_mode"));
    if (value.is_object() && value.contains("nameservers") && value.at("nameservers").is_array()) {
        QStringList entries;
        for (const Json &entry : value.at("nameservers")) {
            if (entry.is_string()) {
                entries.append(QString::fromStdString(entry.get<std::string>()));
            }
        }
        config.nameservers = entries;
    }
    if (value.is_object() && value.contains("proxy_server_nameserver")
        && value.at("proxy_server_nameserver").is_array()) {
        QStringList entries;
        for (const Json &entry : value.at("proxy_server_nameserver")) {
            if (entry.is_string()) {
                entries.append(QString::fromStdString(entry.get<std::string>()));
            }
        }
        config.proxyServerNameserver = entries;
    } else {
        config.proxyServerNameserver = defaultProxyServerNameserver();
    }
    return config;
}

QString ruleActionToText(RuleAction action)
{
    switch (action) {
    case RuleAction::Direct:
        return QStringLiteral("direct");
    case RuleAction::Proxy:
        return QStringLiteral("proxy");
    case RuleAction::Reject:
        return QStringLiteral("reject");
    }
    return QStringLiteral("direct");
}

RuleAction ruleActionFromText(const QString &text)
{
    if (text == QLatin1String("proxy")) {
        return RuleAction::Proxy;
    }
    if (text == QLatin1String("reject")) {
        return RuleAction::Reject;
    }
    return RuleAction::Direct;
}

QString ruleActionToTarget(RuleAction action)
{
    switch (action) {
    case RuleAction::Direct:
        return QStringLiteral("DIRECT");
    case RuleAction::Proxy:
        return QStringLiteral("PROXY");
    case RuleAction::Reject:
        return QStringLiteral("REJECT");
    }
    return QStringLiteral("DIRECT");
}

QString networkProtocolToText(NetworkProtocol protocol)
{
    return protocol == NetworkProtocol::Tcp ? QStringLiteral("tcp") : QStringLiteral("udp");
}

NetworkProtocol networkProtocolFromText(const QString &text)
{
    return text == QLatin1String("udp") ? NetworkProtocol::Udp : NetworkProtocol::Tcp;
}

QString networkProtocolToMihomoValue(NetworkProtocol protocol)
{
    return networkProtocolToText(protocol);
}

RuleMatcher RuleMatcher::simple(Kind kind, const QString &text)
{
    RuleMatcher matcher;
    matcher.kind = kind;
    matcher.value = text;
    return matcher;
}

RuleMatcher RuleMatcher::makeNetwork(NetworkProtocol protocol)
{
    RuleMatcher matcher;
    matcher.kind = Kind::Network;
    matcher.network = protocol;
    return matcher;
}

RuleMatcher RuleMatcher::makeQuic()
{
    RuleMatcher matcher;
    matcher.kind = Kind::Quic;
    return matcher;
}

bool RuleMatcher::operator==(const RuleMatcher &other) const
{
    if (kind != other.kind) {
        return false;
    }
    if (kind == Kind::Network) {
        return network == other.network;
    }
    return value == other.value;
}

Json RuleMatcher::toJson() const
{
    Json value;
    value["type"] = matcherKindToText(kind).toStdString();
    switch (kind) {
    case Kind::Network:
        value["value"] = networkProtocolToText(network).toStdString();
        break;
    case Kind::Quic:
        break;
    default:
        value["value"] = this->value.toStdString();
        break;
    }
    return value;
}

RuleMatcher RuleMatcher::fromJson(const Json &value)
{
    RuleMatcher matcher;
    const QString type = JsonUtil::textOrEmpty(value, "type");
    matcher.kind = matcherKindFromText(type);
    if (matcher.kind == Kind::Network) {
        matcher.network = networkProtocolFromText(JsonUtil::textOrEmpty(value, "value"));
    } else if (matcher.kind != Kind::Quic) {
        matcher.value = JsonUtil::textOrEmpty(value, "value");
    }
    return matcher;
}

Result<QString> TrafficRule::toMihomoRule() const
{
    const QString target = ruleActionToTarget(action);
    switch (matcher.kind) {
    case RuleMatcher::Kind::Domain:
        return renderValueRule(QStringLiteral("DOMAIN"), matcher.value, target);
    case RuleMatcher::Kind::DomainSuffix:
        return renderValueRule(QStringLiteral("DOMAIN-SUFFIX"), matcher.value, target);
    case RuleMatcher::Kind::GeoSite:
        return renderValueRule(QStringLiteral("GEOSITE"), matcher.value, target);
    case RuleMatcher::Kind::GeoIp: {
        Result<QString> rendered = renderValueRule(QStringLiteral("GEOIP"), matcher.value, target);
        if (rendered.isErr()) {
            return rendered;
        }
        // no-resolve：域名连接不参与 GEOIP 判定。否则 fake-ip 下每条没被前面规则命中的
        // 域名都要先真解析一次，而境内 DoH 常把境外域名污染成国内 IP（实测 googlevideo
        // 解析到上海/北京），于是被误判成"国内直连"。
        return Result<QString>::withValue(rendered.value() + QStringLiteral(",no-resolve"));
    }
    case RuleMatcher::Kind::IpCidr:
        return renderValueRule(QStringLiteral("IP-CIDR"), matcher.value, target);
    case RuleMatcher::Kind::DestinationPort:
        return renderValueRule(QStringLiteral("DST-PORT"), matcher.value, target);
    case RuleMatcher::Kind::ProcessName:
        return renderValueRule(QStringLiteral("PROCESS-NAME"), matcher.value, target);
    case RuleMatcher::Kind::Network:
        return Result<QString>::withValue(
            QStringLiteral("NETWORK,%1,%2").arg(networkProtocolToMihomoValue(matcher.network),
                                                 target));
    case RuleMatcher::Kind::Quic:
        return Result<QString>::withValue(
            QStringLiteral("AND,((NETWORK,udp),(DST-PORT,443)),%1").arg(target));
    }
    return Result<QString>::withError(QStringLiteral("未知规则类型"));
}

bool TrafficRule::usesGeo() const
{
    return matcher.kind == RuleMatcher::Kind::GeoSite
        || matcher.kind == RuleMatcher::Kind::GeoIp;
}

Json TrafficRule::toJson() const
{
    Json value;
    value["matcher"] = matcher.toJson();
    value["action"] = ruleActionToText(action).toStdString();
    return value;
}

TrafficRule TrafficRule::fromJson(const Json &value)
{
    TrafficRule rule;
    if (value.is_object() && value.contains("matcher")) {
        rule.matcher = RuleMatcher::fromJson(value.at("matcher"));
    }
    rule.action = ruleActionFromText(JsonUtil::textOrEmpty(value, "action"));
    return rule;
}

bool shadowsRule(const TrafficRule &earlier, const TrafficRule &later)
{
    const RuleMatcher &a = earlier.matcher;
    const RuleMatcher &b = later.matcher;
    if (a.kind == b.kind) {
        switch (a.kind) {
        case RuleMatcher::Kind::Domain:
        case RuleMatcher::Kind::GeoSite:
        case RuleMatcher::Kind::IpCidr:
        case RuleMatcher::Kind::DestinationPort:
        case RuleMatcher::Kind::ProcessName:
            return a.value == b.value;
        case RuleMatcher::Kind::DomainSuffix:
            return b.value == a.value
                || b.value.endsWith(QStringLiteral(".") + a.value);
        case RuleMatcher::Kind::GeoIp:
            return a.value.compare(b.value, Qt::CaseInsensitive) == 0;
        case RuleMatcher::Kind::Quic:
            return true;
        case RuleMatcher::Kind::Network:
            return a.network == b.network;
        }
    }
    if (a.kind == RuleMatcher::Kind::DomainSuffix && b.kind == RuleMatcher::Kind::Domain) {
        return b.value == a.value || b.value.endsWith(QStringLiteral(".") + a.value);
    }
    return false;
}

QVector<TrafficRule> optimizeTrafficRules(const QVector<TrafficRule> &rules)
{
    QVector<TrafficRule> optimized;
    optimized.reserve(rules.size());
    for (const TrafficRule &rule : rules) {
        // 完全重复，或被前面同动作的规则覆盖（域名后缀包含、或已被某个 GEOSITE 分类
        // 的父域覆盖）的规则，匹配不到任何东西，只会让规则表越来越长。
        const bool covered = std::any_of(
            optimized.constBegin(), optimized.constEnd(), [&](const TrafficRule &existing) {
                return shadowsRule(existing, rule) && existing.action == rule.action;
            });
        if (covered) {
            continue;
        }
        optimized.append(rule);
    }
    return optimized;
}

QVector<TrafficRule> mihomoDnsPoisonOverrides()
{
    QVector<TrafficRule> rules;
    for (const char *category : {"google", "youtube"}) {
        TrafficRule rule;
        rule.matcher = RuleMatcher::simple(RuleMatcher::Kind::GeoSite,
                                          QLatin1String(category));
        rule.action = RuleAction::Proxy;
        rules.append(rule);
    }
    return rules;
}

/// 把污染免疫规则插到第一条 `GEOIP,CN` 之前；规则表里没有 GEOIP,CN 时无需注入。
void applyMihomoDnsPoisonOverrides(QVector<TrafficRule> &rules)
{
    int position = -1;
    for (int index = 0; index < rules.size(); ++index) {
        const TrafficRule &rule = rules.at(index);
        if (rule.matcher.kind == RuleMatcher::Kind::GeoIp
            && rule.matcher.value.compare(QLatin1String("CN"), Qt::CaseInsensitive) == 0) {
            position = index;
            break;
        }
    }
    if (position < 0) {
        return;
    }
    for (int index = 0; index < position; ++index) {
        const TrafficRule &rule = rules.at(index);
        if (rule.matcher.kind == RuleMatcher::Kind::GeoSite
            && rule.matcher.value == QLatin1String("youtube")) {
            return;
        }
    }
    const QVector<TrafficRule> overrides = mihomoDnsPoisonOverrides();
    for (int offset = 0; offset < overrides.size(); ++offset) {
        rules.insert(position + offset, overrides.at(offset));
    }
}

TrafficRule makeRule(RuleMatcher::Kind kind, const QString &value, RuleAction action)
{
    TrafficRule rule;
    rule.matcher = kind == RuleMatcher::Kind::Quic
        ? RuleMatcher::makeQuic()
        : RuleMatcher::simple(kind, value);
    rule.action = action;
    return rule;
}

QVector<TrafficRule> recommendedTrafficRules()
{
    return {
        makeRule(RuleMatcher::Kind::Domain, QStringLiteral("localhost"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("lan"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("home.arpa"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("127.0.0.0/8"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("10.0.0.0/8"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("172.16.0.0/12"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("192.168.0.0/16"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("169.254.0.0/16"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("100.64.0.0/10"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("192.0.0.0/24"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("198.18.0.0/15"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("category-ads-all"),
                 RuleAction::Reject),
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("cn"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::GeoIp, QStringLiteral("CN"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::Quic, QString(), RuleAction::Reject),
    };
}

QVector<TrafficRule> gfwlistTrafficRules()
{
    return {
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("category-ads-all"),
                 RuleAction::Reject),
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("gfw"), RuleAction::Proxy),
    };
}

namespace {

/// 只看"匹配的是什么"，不看动作：用来判断某条兜底规则是不是已经在表里了。
bool sameMatcherKindAndValue(const TrafficRule &left, const TrafficRule &right)
{
    if (left.matcher.kind != right.matcher.kind) {
        return false;
    }
    if (left.matcher.kind == RuleMatcher::Kind::Quic) {
        return true;
    }
    if (left.matcher.kind == RuleMatcher::Kind::Network) {
        return left.matcher.network == right.matcher.network;
    }
    return left.matcher.value == right.matcher.value;
}

}

QVector<TrafficRule> withMissingFallbackRules(const QVector<TrafficRule> &rules)
{
    if (rules.isEmpty()) {
        return optimizeTrafficRules(recommendedTrafficRules());
    }
    QVector<TrafficRule> merged = rules;
    const QVector<TrafficRule> fallbacks = recommendedTrafficRules();
    for (const TrafficRule &fallback : fallbacks) {
        const bool already = std::any_of(
            merged.cbegin(), merged.cend(), [&](const TrafficRule &existing) {
                return existing.action == fallback.action && sameMatcherKindAndValue(existing, fallback);
            });
        if (!already) {
            merged.append(fallback);
        }
    }
    return optimizeTrafficRules(merged);
}

QVector<TrafficRule> aiServiceRules()
{
    // 实测这份 GeoSite.dat 里有 openai / anthropic 分类（没有 chatgpt/claude/gemini/
    // copilot），用分类比手写域名覆盖更全、也更少。
    return {
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("openai"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("anthropic"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("copilot.microsoft.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("ai.google.dev"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("gemini.google.com"),
                 RuleAction::Proxy),
    };
}

QVector<TrafficRule> streamingMediaRules()
{
    return {
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("youtube.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("googlevideo.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("ytimg.com"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("netflix.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("nflxvideo.net"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("disneyplus.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("spotify.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("twitch.tv"), RuleAction::Proxy),
    };
}

QVector<TrafficRule> socialTelegramRules()
{
    return {
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("telegram.org"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("t.me"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("telegra.ph"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("91.108.4.0/22"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("91.108.56.0/22"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("149.154.160.0/20"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("discord.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("discordapp.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("twitter.com"),
                 RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("x.com"), RuleAction::Proxy),
    };
}

QVector<TrafficRule> devMirrorsRules()
{
    return {
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("npmmirror.com"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("cnpmjs.org"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("tsinghua.edu.cn"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("ustc.edu.cn"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("aliyun.com"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("gitee.com"), RuleAction::Direct),
    };
}

QVector<TrafficRule> gamePlatformsRules()
{
    return {
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("steamcontent.com"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix,
                 QStringLiteral("steambroadcast.akamaized.net"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("epicgames.com"),
                 RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("battlenet.com.cn"),
                 RuleAction::Direct),
    };
}

QVector<TrafficRule> adblockQuicRules()
{
    return {
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("category-ads-all"),
                 RuleAction::Reject),
        makeRule(RuleMatcher::Kind::Quic, QString(), RuleAction::Reject),
    };
}

}
