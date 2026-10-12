#include <gtest/gtest.h>

#include "proxy/ConfigTypes.h"

using namespace Proxy;

TEST(TunConfig, DefaultsEnableRouteAndDnsHijack)
{
    const TunConfig tun;
    EXPECT_EQ(tun.stack, TunStack::Mixed);
    EXPECT_TRUE(tun.autoRoute);
    EXPECT_TRUE(tun.autoDetectInterface);
    ASSERT_EQ(tun.dnsHijack.size(), 1);
    EXPECT_EQ(tun.dnsHijack.first().toStdString(), std::string("any:53"));
}

TEST(DnsConfig, DefaultsUseDohWithFakeIp)
{
    const DnsConfig dns;
    EXPECT_TRUE(dns.enabled);
    EXPECT_EQ(dns.enhancedMode, DnsEnhancedMode::FakeIp);
    ASSERT_EQ(dns.nameservers.size(), 2);
    EXPECT_EQ(dns.nameservers[0].toStdString(), std::string("https://doh.pub/dns-query"));
    EXPECT_EQ(dns.nameservers[1].toStdString(), std::string("https://dns.alidns.com/dns-query"));
    EXPECT_FALSE(dns.proxyServerNameserver.isEmpty());
    EXPECT_TRUE(dns.validate().isOk());
}

TEST(DnsConfig, MigratesLegacyCloudflareDefault)
{
    DnsConfig dns;
    dns.nameservers = {QStringLiteral("https://1.1.1.1/dns-query")};
    EXPECT_TRUE(dns.migrateLegacyDefaults());
    ASSERT_EQ(dns.nameservers.size(), 2);
    EXPECT_EQ(dns.nameservers[0].toStdString(), std::string("https://doh.pub/dns-query"));
}

TEST(DnsConfig, MigratesLegacyPlaintextDefault)
{
    // 旧默认 8.8.8.8/114 明文 UDP 属于 DNS 泄露，保存过的配置要自动迁移。
    DnsConfig dns;
    dns.nameservers = {QStringLiteral("8.8.8.8"), QStringLiteral("114.114.114.114")};
    dns.proxyServerNameserver = {};
    EXPECT_TRUE(dns.migrateLegacyDefaults());
    ASSERT_EQ(dns.nameservers.size(), 2);
    EXPECT_EQ(dns.nameservers[0].toStdString(), std::string("https://doh.pub/dns-query"));
    EXPECT_FALSE(dns.proxyServerNameserver.isEmpty());
}

TEST(DnsConfig, AcceptsIpNameservers)
{
    DnsConfig dns;
    dns.nameservers = {QStringLiteral("8.8.8.8"), QStringLiteral("114.114.114.114")};
    EXPECT_TRUE(dns.validate().isOk());
}

TEST(DnsConfig, RejectsInvalidNameserver)
{
    DnsConfig dns;
    dns.nameservers = {QStringLiteral("not-a-dns-server")};
    const Status status = dns.validate();
    ASSERT_TRUE(status.isErr());
    EXPECT_NE(status.error().indexOf(QStringLiteral("IP")), -1);
    EXPECT_NE(status.error().indexOf(QStringLiteral("HTTPS")), -1);
}

TEST(TrafficRule, RendersAllSupportedMatchers)
{
    struct Case {
        RuleMatcher matcher;
        const char *expected;
    };
    const Case cases[] = {
        {RuleMatcher::simple(RuleMatcher::Kind::Domain, QStringLiteral("example.com")),
         "DOMAIN,example.com,PROXY"},
        {RuleMatcher::simple(RuleMatcher::Kind::DomainSuffix, QStringLiteral("example.com")),
         "DOMAIN-SUFFIX,example.com,PROXY"},
        {RuleMatcher::simple(RuleMatcher::Kind::GeoSite, QStringLiteral("cn")), "GEOSITE,cn,PROXY"},
        {RuleMatcher::simple(RuleMatcher::Kind::GeoIp, QStringLiteral("CN")),
         "GEOIP,CN,PROXY,no-resolve"},
        {RuleMatcher::simple(RuleMatcher::Kind::IpCidr, QStringLiteral("10.0.0.0/8")),
         "IP-CIDR,10.0.0.0/8,PROXY"},
        {RuleMatcher::simple(RuleMatcher::Kind::DestinationPort, QStringLiteral("443")),
         "DST-PORT,443,PROXY"},
        {RuleMatcher::simple(RuleMatcher::Kind::ProcessName, QStringLiteral("curl.exe")),
         "PROCESS-NAME,curl.exe,PROXY"},
        {RuleMatcher::makeNetwork(NetworkProtocol::Tcp), "NETWORK,tcp,PROXY"},
        {RuleMatcher::makeQuic(), "AND,((NETWORK,udp),(DST-PORT,443)),PROXY"},
    };
    for (const Case &item : cases) {
        TrafficRule rule;
        rule.matcher = item.matcher;
        rule.action = RuleAction::Proxy;
        const Result<QString> rendered = rule.toMihomoRule();
        ASSERT_TRUE(rendered.isOk()) << item.expected;
        EXPECT_EQ(rendered.value().toStdString(), std::string(item.expected)) << item.expected;
    }
}

TEST(RuleAction, MapsToMihomoTargets)
{
    EXPECT_EQ(ruleActionToTarget(RuleAction::Direct).toStdString(), std::string("DIRECT"));
    EXPECT_EQ(ruleActionToTarget(RuleAction::Proxy).toStdString(), std::string("PROXY"));
    EXPECT_EQ(ruleActionToTarget(RuleAction::Reject).toStdString(), std::string("REJECT"));
}

TEST(TrafficRulePresets, RecommendedCoverLanAdsCnAndQuicInOrder)
{
    QStringList rendered;
    for (const TrafficRule &rule : recommendedTrafficRules()) {
        const Result<QString> item = rule.toMihomoRule();
        ASSERT_TRUE(item.isOk()) << item.error().toStdString();
        rendered.append(item.value());
    }
    const QStringList expected = {
        QStringLiteral("DOMAIN,localhost,DIRECT"),
        QStringLiteral("DOMAIN-SUFFIX,lan,DIRECT"),
        QStringLiteral("DOMAIN-SUFFIX,home.arpa,DIRECT"),
        QStringLiteral("IP-CIDR,127.0.0.0/8,DIRECT"),
        QStringLiteral("IP-CIDR,10.0.0.0/8,DIRECT"),
        QStringLiteral("IP-CIDR,172.16.0.0/12,DIRECT"),
        QStringLiteral("IP-CIDR,192.168.0.0/16,DIRECT"),
        QStringLiteral("IP-CIDR,169.254.0.0/16,DIRECT"),
        QStringLiteral("IP-CIDR,100.64.0.0/10,DIRECT"),
        QStringLiteral("IP-CIDR,192.0.0.0/24,DIRECT"),
        QStringLiteral("IP-CIDR,198.18.0.0/15,DIRECT"),
        QStringLiteral("GEOSITE,category-ads-all,REJECT"),
        QStringLiteral("GEOSITE,cn,DIRECT"),
        QStringLiteral("GEOIP,CN,DIRECT,no-resolve"),
        QStringLiteral("AND,((NETWORK,udp),(DST-PORT,443)),REJECT"),
    };
    EXPECT_EQ(rendered, expected);
}

TEST(TrafficRulePresets, GfwlistProxiesMatchesAndRejectsAds)
{
    QStringList rendered;
    for (const TrafficRule &rule : gfwlistTrafficRules()) {
        const Result<QString> item = rule.toMihomoRule();
        ASSERT_TRUE(item.isOk()) << item.error().toStdString();
        rendered.append(item.value());
    }
    const QStringList expected = {QStringLiteral("GEOSITE,category-ads-all,REJECT"),
                                 QStringLiteral("GEOSITE,gfw,PROXY")};
    EXPECT_EQ(rendered, expected);
}

TEST(TrafficRuleOptimizer, RemovesDuplicatesAndPreservesOrder)
{
    const TrafficRule first = makeRule(RuleMatcher::Kind::DomainSuffix,
                                       QStringLiteral("example.com"), RuleAction::Proxy);
    const TrafficRule second =
        makeRule(RuleMatcher::Kind::Domain, QStringLiteral("localhost"), RuleAction::Direct);
    const QVector<TrafficRule> optimized = optimizeTrafficRules({first, second, first});
    ASSERT_EQ(optimized.size(), 2);
    EXPECT_EQ(optimized[0], first);
    EXPECT_EQ(optimized[1], second);
}

TEST(RoutingMode, DefaultsWhenOldConfigHasNoField)
{
    const Json legacy = JsonUtil::parseOrEmptyObject(QString::fromUtf8("{}"));
    EXPECT_EQ(routingModeFromText(JsonUtil::textOrEmpty(legacy, "routing_mode")), RoutingMode::Rule);
}

TEST(MihomoDnsPoisonOverrides, InsertsBeforeFirstGeoIpCn)
{
    QVector<TrafficRule> rules = {
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("cn"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::GeoIp, QStringLiteral("CN"), RuleAction::Direct),
    };
    applyMihomoDnsPoisonOverrides(rules);
    ASSERT_EQ(rules.size(), 4);
    EXPECT_EQ(rules[1].matcher.value.toStdString(), std::string("google"));
    EXPECT_EQ(rules[2].matcher.value.toStdString(), std::string("youtube"));
    EXPECT_EQ(rules[3].matcher.kind, RuleMatcher::Kind::GeoIp);

    // 已经有 youtube 分类时不重复注入。
    const int before = rules.size();
    applyMihomoDnsPoisonOverrides(rules);
    EXPECT_EQ(before, rules.size());

    // 没有 GEOIP,CN 的规则表不注入任何东西。
    QVector<TrafficRule> withoutGeoIp = {
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("cn"), RuleAction::Direct),
    };
    applyMihomoDnsPoisonOverrides(withoutGeoIp);
    EXPECT_EQ(withoutGeoIp.size(), 1);
}

TEST(RuleMatcherJson, UsesAdjacentTagAndContentShape)
{
    const RuleMatcher domain =
        RuleMatcher::simple(RuleMatcher::Kind::DomainSuffix, QStringLiteral("example.com"));
    EXPECT_EQ(JsonUtil::dump(domain.toJson()).toStdString(),
              std::string(R"({"type":"domain_suffix","value":"example.com"})"));
    EXPECT_EQ(JsonUtil::dump(RuleMatcher::makeQuic().toJson()).toStdString(),
              std::string(R"({"type":"quic"})"));
    EXPECT_EQ(JsonUtil::dump(RuleMatcher::makeNetwork(NetworkProtocol::Udp).toJson()).toStdString(),
              std::string(R"({"type":"network","value":"udp"})"));

    const Json parsed = JsonUtil::parseOrEmptyObject(
        QString::fromUtf8(R"({"type":"geo_ip","value":"CN"})"));
    const RuleMatcher restored = RuleMatcher::fromJson(parsed);
    EXPECT_EQ(restored.kind, RuleMatcher::Kind::GeoIp);
    EXPECT_EQ(restored.value.toStdString(), std::string("CN"));
}

namespace {

bool hasRule(const QVector<TrafficRule> &rules, RuleMatcher::Kind kind, const QString &value,
             RuleAction action)
{
    for (const TrafficRule &rule : rules) {
        if (rule.matcher.kind == kind && rule.matcher.value == value && rule.action == action) {
            return true;
        }
    }
    return false;
}

}

/// 规则自动最优（vcc 独有）：这三条钉住的是"B 站整站绕海外"那类事故的兜底。
TEST(AutoOptimalRules, emptyRuleListGetsTheRecommendedBlock)
{
    const QVector<TrafficRule> seeded = withMissingFallbackRules({});
    EXPECT_EQ(seeded.size(), recommendedTrafficRules().size());
    EXPECT_TRUE(hasRule(seeded, RuleMatcher::Kind::GeoSite, QStringLiteral("cn"), RuleAction::Direct));
    EXPECT_TRUE(hasRule(seeded, RuleMatcher::Kind::GeoIp, QStringLiteral("CN"), RuleAction::Direct));
    EXPECT_TRUE(hasRule(seeded, RuleMatcher::Kind::DomainSuffix, QStringLiteral("home.arpa"),
                        RuleAction::Direct));
}

TEST(AutoOptimalRules, onlyTheMissingFallbacksAreAppendedAndUserOrderSurvives)
{
    // 模拟库里已有的东西：两条自己的规则，没有任何中国大陆直连兜底。
    const QVector<TrafficRule> stored = {
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("gitee.com"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("x.com"), RuleAction::Proxy),
    };
    const QVector<TrafficRule> fixed = withMissingFallbackRules(stored);

    EXPECT_TRUE(hasRule(fixed, RuleMatcher::Kind::GeoSite, QStringLiteral("cn"), RuleAction::Direct));
    EXPECT_TRUE(hasRule(fixed, RuleMatcher::Kind::IpCidr, QStringLiteral("10.0.0.0/8"),
                        RuleAction::Direct));
    // 用户自己的两条还在，而且顺序没动（追加只在末尾）。
    EXPECT_EQ(fixed.first().matcher.value.toStdString(), std::string("gitee.com"));
    EXPECT_EQ(fixed.at(1).matcher.value.toStdString(), std::string("x.com"));
    EXPECT_LT(fixed.size(), 2 + recommendedTrafficRules().size() + 1)
        << "推荐的条目应该被 optimize 合并掉重复的，不是整段照抄";
}

TEST(AutoOptimalRules, itIsIdempotent)
{
    const QVector<TrafficRule> stored = {
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("gitee.com"), RuleAction::Direct),
    };
    const QVector<TrafficRule> once = withMissingFallbackRules(stored);
    const QVector<TrafficRule> twice = withMissingFallbackRules(once);
    EXPECT_EQ(once.size(), twice.size());
    for (int index = 0; index < once.size(); ++index) {
        EXPECT_EQ(once.at(index).matcher.value.toStdString(), twice.at(index).matcher.value.toStdString());
        EXPECT_EQ(once.at(index).action, twice.at(index).action);
    }
}
