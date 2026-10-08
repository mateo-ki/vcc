#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/JsonUtil.h"
#include "core/Result.h"

namespace Proxy {

/// 默认 DNS 走 DoH：明文 UDP（8.8.8.8/114）在境内会被透明拦截，属于 DNS 泄露。
inline constexpr const char *kDefaultDnsPrimary = "https://doh.pub/dns-query";
inline constexpr const char *kDefaultDnsSecondary = "https://dns.alidns.com/dns-query";

enum class RoutingMode {
    Rule,
    ChinaDirect,
    GfwList,
    Global,
    Direct,
};

QString routingModeToText(RoutingMode mode);
/// 未知取值退回 Rule（对应 serde default + 老配置没这个字段的情况）。
RoutingMode routingModeFromText(const QString &text);

enum class TunStack {
    System,
    Gvisor,
    Mixed,
};

QString tunStackToText(TunStack stack);
TunStack tunStackFromText(const QString &text);
/// 写进 mihomo 配置的取值。
QString tunStackToMihomoValue(TunStack stack);

struct TunConfig {
    TunStack stack = TunStack::Mixed;
    QStringList dnsHijack = {QString::fromLatin1("any:53")};
    bool autoRoute = true;
    bool autoDetectInterface = true;

    Json toJson() const;
    static TunConfig fromJson(const Json &value);
};

enum class DnsEnhancedMode {
    FakeIp,
    RedirHost,
};

QString dnsEnhancedModeToText(DnsEnhancedMode mode);
DnsEnhancedMode dnsEnhancedModeFromText(const QString &text);
QString dnsEnhancedModeToMihomoValue(DnsEnhancedMode mode);

struct DnsConfig {
    bool enabled = true;
    bool ipv6 = false;
    DnsEnhancedMode enhancedMode = DnsEnhancedMode::FakeIp;
    QStringList nameservers = {QLatin1String(kDefaultDnsPrimary), QLatin1String(kDefaultDnsSecondary)};
    /// 解析代理节点自身域名所用 DNS。节点域名如果走 ISP 明文解析会暴露"在使用
    /// 代理"，mihomo 用这个独立列表解析它们（默认与主 DNS 同为 DoH）。
    QStringList proxyServerNameserver = {QLatin1String(kDefaultDnsPrimary),
                                         QLatin1String(kDefaultDnsSecondary)};

    /// 老配置命中明文/旧 DoH 默认值时迁移到当前默认，返回是否改动过。
    bool migrateLegacyDefaults();
    Status validate() const;

    Json toJson() const;
    static DnsConfig fromJson(const Json &value);
};

enum class RuleAction {
    Direct,
    Proxy,
    Reject,
};

QString ruleActionToText(RuleAction action);
RuleAction ruleActionFromText(const QString &text);
/// 规则行尾的出口名。
QString ruleActionToTarget(RuleAction action);

enum class NetworkProtocol {
    Tcp,
    Udp,
};

QString networkProtocolToText(NetworkProtocol protocol);
NetworkProtocol networkProtocolFromText(const QString &text);
QString networkProtocolToMihomoValue(NetworkProtocol protocol);

/// 规则的匹配条件。serde 形状是 `{"type":"domain","value":"example.com"}`，
/// quic 这种无值变体只有 `{"type":"quic"}`。
struct RuleMatcher {
    enum class Kind {
        Domain,
        DomainSuffix,
        GeoSite,
        GeoIp,
        IpCidr,
        DestinationPort,
        ProcessName,
        Network,
        Quic,
    };

    Kind kind = Kind::Domain;
    QString value;
    NetworkProtocol network = NetworkProtocol::Tcp;

    static RuleMatcher simple(Kind kind, const QString &text);
    static RuleMatcher makeNetwork(NetworkProtocol protocol);
    static RuleMatcher makeQuic();

    bool operator==(const RuleMatcher &other) const;

    Json toJson() const;
    static RuleMatcher fromJson(const Json &value);
};

struct TrafficRule {
    RuleMatcher matcher;
    RuleAction action = RuleAction::Direct;

    bool operator==(const TrafficRule &other) const
    {
        return matcher == other.matcher && action == other.action;
    }

    Result<QString> toMihomoRule() const;
    bool usesGeo() const;

    Json toJson() const;
    static TrafficRule fromJson(const Json &value);
};

/// 保留用户规则顺序，只移除完全重复项与被前面同动作规则覆盖的项，
/// 避免多个内核重复匹配同一条规则。
QVector<TrafficRule> optimizeTrafficRules(const QVector<TrafficRule> &rules);

/// `earlier` 是否已经覆盖 `later`（mihomo 从上到下匹配，先命中的规则生效）。
bool shadowsRule(const TrafficRule &earlier, const TrafficRule &later);

/// 仅 mihomo 使用的域名优先规则：fake-ip 模式下 `GEOIP,CN` 会拿真实解析结果参与判定，
/// 而境内 DoH 间歇性把 google/youtube 的 CDN 域名污染成国内 IP（实测 googlevideo 解析
/// 到上海/北京），于是被判成"国内直连"再 i/o timeout。插在第一条 GEOIP,CN 之前按域名
/// 定性，就不需要依赖任何境外 DNS。sing-box 侧没有对应分类的 rule-set，故只在生成
/// mihomo 配置时注入，不写进用户可见的规则列表。
QVector<TrafficRule> mihomoDnsPoisonOverrides();

/// 把污染免疫规则插到第一条 `GEOIP,CN` 之前；规则表里没有 GEOIP,CN 时无需注入。
void applyMihomoDnsPoisonOverrides(QVector<TrafficRule> &rules);

/// 便捷构造：Quic 变体忽略 value。
TrafficRule makeRule(RuleMatcher::Kind kind, const QString &value, RuleAction action);

QVector<TrafficRule> recommendedTrafficRules();QVector<TrafficRule> gfwlistTrafficRules();
QVector<TrafficRule> aiServiceRules();
QVector<TrafficRule> streamingMediaRules();
QVector<TrafficRule> socialTelegramRules();
QVector<TrafficRule> devMirrorsRules();
QVector<TrafficRule> gamePlatformsRules();
QVector<TrafficRule> adblockQuicRules();

}
