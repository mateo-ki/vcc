#include <gtest/gtest.h>

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/Ids.h"
#include "core/JsonUtil.h"
#include "models/Node.h"
#include "proxy/KernelConfigMessages.h"
#include "proxy/SingBoxConfig.h"
#include "proxy/XrayConfig.h"

using namespace Proxy;

namespace {

Json parseJson(const char *text)
{
    const Result<Json> parsed = JsonUtil::parse(QString::fromUtf8(text));
    EXPECT_TRUE(parsed.isOk()) << text;
    return parsed.isOk() ? parsed.value() : Json::object();
}

std::string dump(const Json &value)
{
    return JsonUtil::dump(value).toStdString();
}

/// 对应 sing_box.rs 测试里的 node()：server/port 从 config 取，id 是新 UUID，name=protocol。
Models::Node singBoxNode(const QString &protocol, const Json &config)
{
    Models::Node node;
    node.id = Ids::newUuidV4();
    node.protocol = protocol;
    node.name = protocol;
    node.server = JsonUtil::textOrEmpty(config, "server");
    node.port = int(JsonUtil::intOr(config, "port", 0));
    node.config = config;
    return node;
}

/// 对应 xray.rs 测试里的 node()：server/port 固定，name 与 id 都是 "node"。
Models::Node xrayNode(const QString &protocol, const Json &config)
{
    Models::Node node;
    node.id = QStringLiteral("node");
    node.protocol = protocol;
    node.name = QStringLiteral("node");
    node.server = QStringLiteral("example.com");
    node.port = 443;
    node.config = config;
    return node;
}

Json hysteria2NodeConfig()
{
    return parseJson(R"({"server":"1.2.3.4","port":443,"password":"secret","obfs":"salamander",
                         "obfs-password":"obfs","sni":"example.com","insecure":"1"})");
}

Json tuicNodeConfig()
{
    return parseJson(R"({"server":"tuic.example.com","port":443,
                         "uuid":"00000000-0000-0000-0000-000000000000",
                         "password":"secret","congestion-controller":"cubic",
                         "sni":"tuic.example.com"})");
}

Json anytlsNodeConfig()
{
    return parseJson(R"({"server":"anytls.example.com","port":443,"password":"secret",
                         "skip-cert-verify":true})");
}

Json wireGuardNodeConfig()
{
    return parseJson(R"({"server":"1.2.3.4","port":51820,"ip":"172.16.0.2/32",
                         "private-key":"EFlemF/JAgdqTmKVb/1W/yv9WnwuzhdqLfDHUUNC11w=",
                         "public-key":"TepNujm+9RJYXvQTB3CGDCnSccm05k6p/NmaHhj1C2c=",
                         "reserved":[0,0,0]})");
}

Json simpleHysteria2()
{
    return parseJson(R"({"server":"1.2.3.4","port":443,"password":"p","sni":"example.com"})");
}

}

// ---------------------------------------------------------------------------
// sing_box.rs: builds_hysteria2_tuic_and_anytls_outbounds
// ---------------------------------------------------------------------------

TEST(SingBoxKernelConfig, BuildsHysteria2TuicAndAnytlsOutbounds)
{
    struct Case {
        QString protocol;
        Json config;
        bool isEndpoint;
    };
    const QVector<Case> cases = {
        {QStringLiteral("hysteria2"), hysteria2NodeConfig(), false},
        {QStringLiteral("tuic"), tuicNodeConfig(), false},
        {QStringLiteral("anytls"), anytlsNodeConfig(), false},
        {QStringLiteral("wireguard"), wireGuardNodeConfig(), true},
    };
    for (const Case &item : cases) {
        const Models::Node node = singBoxNode(item.protocol, item.config);
        const Result<Json> built = buildSingBoxConfig(node, MihomoConfigOptions());
        ASSERT_TRUE(built.isOk()) << item.protocol.toStdString() << " "
                                  << built.error().toStdString();
        const Json &config = built.value();
        if (item.isEndpoint) {
            EXPECT_EQ(JsonUtil::textOrEmpty(config["endpoints"][0], "tag").toStdString(),
                      std::string("proxy"))
                << item.protocol.toStdString();
        } else {
            EXPECT_EQ(JsonUtil::textOrEmpty(config["outbounds"][0], "tag").toStdString(),
                      std::string("proxy"))
                << item.protocol.toStdString();
        }
        EXPECT_EQ(JsonUtil::textOrEmpty(config["inbounds"][0], "type").toStdString(),
                  std::string("mixed"));
        EXPECT_EQ(JsonUtil::textOrEmpty(config["route"], "final").toStdString(),
                  std::string("proxy"));
        EXPECT_EQ(JsonUtil::textOrEmpty(config["route"], "default_domain_resolver").toStdString(),
                  std::string("local"));
    }
}

TEST(SingBoxKernelConfig, Hysteria2OutboundMatchesRustByteForByte)
{
    // config.json 会和 Rust 产物对 diff：键按字节序、端口是整数、远端给的 insecure 不采信。
    const Models::Node node = singBoxNode(QStringLiteral("hysteria2"), hysteria2NodeConfig());
    const Result<Json> built = buildSingBoxConfig(node, MihomoConfigOptions());
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    EXPECT_EQ(dump(built.value()["outbounds"][0]),
              std::string(R"({"obfs":{"password":"obfs","type":"salamander"},"password":"secret",)"
                          R"("server":"1.2.3.4","server_port":443,"tag":"proxy",)"
                          R"("tls":{"enabled":true,"insecure":false,"server_name":"example.com"},)"
                          R"("type":"hysteria2"})"));
}

TEST(SingBoxKernelConfig, TuicDefaultsAndAnytlsIgnoreSkipVerify)
{
    const Models::Node tuic = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    const Result<Json> tuicConfig = buildSingBoxConfig(tuic, MihomoConfigOptions());
    ASSERT_TRUE(tuicConfig.isOk()) << tuicConfig.error().toStdString();
    EXPECT_EQ(dump(tuicConfig.value()["outbounds"][0]),
              std::string(R"({"congestion_control":"cubic","password":"secret",)"
                          R"("server":"tuic.example.com","server_port":443,"tag":"proxy",)"
                          R"("tls":{"enabled":true,"insecure":false,"server_name":"tuic.example.com"},)"
                          R"("type":"tuic","udp_relay_mode":"native",)"
                          R"("uuid":"00000000-0000-0000-0000-000000000000"})"));

    const Models::Node anytls = singBoxNode(QStringLiteral("anytls"), anytlsNodeConfig());
    const Result<Json> anytlsConfig = buildSingBoxConfig(anytls, MihomoConfigOptions());
    ASSERT_TRUE(anytlsConfig.isOk()) << anytlsConfig.error().toStdString();
    EXPECT_EQ(dump(anytlsConfig.value()["outbounds"][0]),
              std::string(R"({"password":"secret","server":"anytls.example.com","server_port":443,)"
                          R"("tag":"proxy","tls":{"enabled":true,"insecure":false,)"
                          R"("server_name":"anytls.example.com"},"type":"anytls"})"));

    // congestion-controller 缺省时才是 cubic。
    Models::Node bbr = singBoxNode(
        QStringLiteral("tuic"),
        parseJson(R"({"server":"bbr.example.com","port":443,"uuid":"u","password":"p",
                     "congestion-controller":"bbr"})"));
    const Result<Json> bbrConfig = buildSingBoxConfig(bbr, MihomoConfigOptions());
    ASSERT_TRUE(bbrConfig.isOk()) << bbrConfig.error().toStdString();
    EXPECT_EQ(JsonUtil::textOrEmpty(bbrConfig.value()["outbounds"][0], "congestion_control")
                  .toStdString(),
              std::string("bbr"));
}

TEST(SingBoxKernelConfig, WireGuardIsAnEndpointAndOutboundsAreOnlyDirect)
{
    const Models::Node node = singBoxNode(QStringLiteral("wireguard"), wireGuardNodeConfig());
    const Result<Json> built = buildSingBoxConfig(node, MihomoConfigOptions());
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    EXPECT_EQ(dump(built.value()["endpoints"][0]),
              std::string(R"({"address":["172.16.0.2/32"],"peers":[{"address":"1.2.3.4",)"
                          R"("allowed_ips":["0.0.0.0/0","::/0"],"port":51820,)"
                          R"("public_key":"TepNujm+9RJYXvQTB3CGDCnSccm05k6p/NmaHhj1C2c=",)"
                          R"("reserved":[0,0,0]}],)"
                          R"("private_key":"EFlemF/JAgdqTmKVb/1W/yv9WnwuzhdqLfDHUUNC11w=",)"
                          R"("tag":"proxy","type":"wireguard"})"));
    EXPECT_EQ(dump(built.value()["outbounds"]), std::string(R"([{"tag":"direct","type":"direct"}])"));
    EXPECT_FALSE(built.value()["route"].contains("rule_set"));
}

TEST(SingBoxKernelConfig, MissingFieldsAndUnsupportedProtocolsAreErrors)
{
    // 空串在 sing-box 侧等于没给（string_value 会 trim 后过滤掉）。
    const Models::Node noPassword =
        singBoxNode(QStringLiteral("hysteria2"), parseJson(R"({"server":"1.2.3.4","port":443})"));
    const Result<Json> missing = buildSingBoxConfig(noPassword, MihomoConfigOptions());
    ASSERT_TRUE(missing.isErr());
    EXPECT_EQ(missing.error(),
              Messages::fill(Messages::singBoxMissingField(), {{"{}", "password/auth"}}));

    const Models::Node blankPassword =
        singBoxNode(QStringLiteral("anytls"),
                    parseJson(R"({"server":"1.2.3.4","port":443,"password":"  "})"));
    EXPECT_TRUE(buildSingBoxConfig(blankPassword, MihomoConfigOptions()).isErr());

    const Models::Node obfsWithoutPassword = singBoxNode(
        QStringLiteral("hysteria2"),
        parseJson(R"({"server":"1.2.3.4","port":443,"password":"p","obfs":"salamander"})"));
    const Result<Json> obfs = buildSingBoxConfig(obfsWithoutPassword, MihomoConfigOptions());
    ASSERT_TRUE(obfs.isErr());
    EXPECT_EQ(obfs.error(),
              Messages::fill(Messages::singBoxMissingField(),
                             {{"{}", "obfs-password/obfs_password"}}));

    const Models::Node wgWithoutAddress = singBoxNode(
        QStringLiteral("wireguard"),
        parseJson(R"({"server":"1.2.3.4","port":51820,"private-key":"a","public-key":"b"})"));
    const Result<Json> wg = buildSingBoxConfig(wgWithoutAddress, MihomoConfigOptions());
    ASSERT_TRUE(wg.isErr());
    EXPECT_EQ(wg.error(), Messages::singBoxWgMissingAddress());

    const Models::Node ss =
        singBoxNode(QStringLiteral("ss"), parseJson(R"({"server":"1.2.3.4","port":443})"));
    const Result<Json> unsupported = buildSingBoxConfig(ss, MihomoConfigOptions());
    ASSERT_TRUE(unsupported.isErr());
    EXPECT_EQ(unsupported.error(),
              Messages::fill(Messages::singBoxProtocolUnsupported(), {{"{}", "ss"}}));
}

// ---------------------------------------------------------------------------
// sing_box.rs: sing_box_routing_presets_use_remote_rules_and_expected_fallback
// ---------------------------------------------------------------------------

TEST(SingBoxKernelConfig, RoutingPresetsUseRemoteRulesAndExpectedFallback)
{
    const Models::Node selected = singBoxNode(QStringLiteral("hysteria2"), simpleHysteria2());

    MihomoConfigOptions china;
    china.routingMode = RoutingMode::ChinaDirect;
    const Result<Json> chinaConfig = buildSingBoxConfig(selected, china);
    ASSERT_TRUE(chinaConfig.isOk()) << chinaConfig.error().toStdString();
    EXPECT_EQ(JsonUtil::textOrEmpty(chinaConfig.value()["route"], "final").toStdString(),
              std::string("proxy"));
    ASSERT_TRUE(chinaConfig.value()["route"]["rule_set"].is_array());
    EXPECT_GE(chinaConfig.value()["route"]["rule_set"].size(), size_t(2));

    MihomoConfigOptions gfw;
    gfw.routingMode = RoutingMode::GfwList;
    const Result<Json> gfwConfig = buildSingBoxConfig(selected, gfw);
    ASSERT_TRUE(gfwConfig.isOk()) << gfwConfig.error().toStdString();
    EXPECT_EQ(JsonUtil::textOrEmpty(gfwConfig.value()["route"], "final").toStdString(),
              std::string("direct"));
    bool hasGfwTag = false;
    for (const Json &ruleSet : gfwConfig.value()["route"]["rule_set"]) {
        if (JsonUtil::textOrEmpty(ruleSet, "tag") == QLatin1String("geosite-gfw")) {
            hasGfwTag = true;
        }
    }
    EXPECT_TRUE(hasGfwTag);
}

TEST(SingBoxKernelConfig, RemoteRuleSetShapeAndDedup)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    MihomoConfigOptions options;
    options.routingMode = RoutingMode::Rule;
    // 两个 geosite 分类 + 同一个 geoip-cn 写两次：rule_set 只登记三条。
    options.rules = {
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("category-ads-all"), RuleAction::Reject),
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("cn"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::GeoIp, QStringLiteral("CN"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::GeoIp, QStringLiteral("cn"), RuleAction::Proxy),
    };
    const Result<Json> built = buildSingBoxConfig(selected, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    const Json &route = built.value()["route"];
    ASSERT_EQ(route["rule_set"].size(), size_t(3));
    EXPECT_EQ(dump(route["rule_set"][0]),
              std::string(R"({"download_detour":"direct","format":"binary",)"
                          R"("tag":"geosite-category-ads-all","type":"remote",)"
                          R"("update_interval":"24h",)"
                          R"("url":"https://raw.githubusercontent.com/SagerNet/sing-geosite/rule-set/geosite-category-ads-all.srs"})"));
    EXPECT_EQ(dump(route["rule_set"][2]),
              std::string(R"({"download_detour":"direct","format":"binary","tag":"geoip-cn",)"
                          R"("type":"remote","update_interval":"24h",)"
                          R"("url":"https://raw.githubusercontent.com/SagerNet/sing-geoip/rule-set/geoip-cn.srs"})"));
    // REJECT 的规则没有 outbound；route 的规则带 outbound。
    EXPECT_EQ(dump(route["rules"][0]),
              std::string(R"({"action":"reject","rule_set":["geosite-category-ads-all"]})"));
    EXPECT_EQ(dump(route["rules"][3]),
              std::string(R"({"action":"route","outbound":"proxy","rule_set":["geoip-cn"]})"));
}

TEST(SingBoxKernelConfig, MatcherMappingForQuicNetworkPortAndProcessName)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    MihomoConfigOptions options;
    options.routingMode = RoutingMode::Rule;
    options.rules = {
        makeRule(RuleMatcher::Kind::Quic, QString(), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::Network, QString(), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::Domain, QStringLiteral("example.com"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("example.com"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("10.0.0.0/8"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DestinationPort, QStringLiteral("443"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::ProcessName, QStringLiteral("curl.exe"), RuleAction::Direct),
    };
    options.rules[1].matcher = RuleMatcher::makeNetwork(NetworkProtocol::Udp);
    const Result<Json> built = buildSingBoxConfig(selected, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    const Json &rules = built.value()["route"]["rules"];
    ASSERT_EQ(rules.size(), size_t(7));
    // QUIC 无论规则自带什么动作，都只能表达成 udp + 443 拒绝。
    EXPECT_EQ(dump(rules[0]), std::string(R"({"action":"reject","network":"udp","port":[443]})"));
    EXPECT_EQ(dump(rules[1]),
              std::string(R"({"action":"route","network":"udp","outbound":"proxy"})"));
    EXPECT_EQ(dump(rules[2]),
              std::string(R"({"action":"route","domain":["example.com"],"outbound":"direct"})"));
    EXPECT_EQ(dump(rules[3]),
              std::string(
                  R"({"action":"route","domain_suffix":["example.com"],"outbound":"proxy"})"));
    EXPECT_EQ(dump(rules[4]),
              std::string(R"({"action":"route","ip_cidr":["10.0.0.0/8"],"outbound":"direct"})"));
    // 端口是整数数组，不是字符串。
    EXPECT_EQ(dump(rules[5]), std::string(R"({"action":"route","outbound":"proxy","port":[443]})"));
    EXPECT_EQ(dump(rules[6]),
              std::string(R"({"action":"route","outbound":"direct","process_name":["curl.exe"]})"));
}

TEST(SingBoxKernelConfig, RejectsMatchersSingBoxCannotExpress)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());

    MihomoConfigOptions geoSite;
    geoSite.routingMode = RoutingMode::Rule;
    geoSite.rules = {makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("google"),
                              RuleAction::Proxy)};
    const Result<Json> site = buildSingBoxConfig(selected, geoSite);
    ASSERT_TRUE(site.isErr());
    EXPECT_EQ(site.error(),
              Messages::fill(Messages::singBoxGeoSiteUnsupported(), {{"{value}", "google"}}));

    MihomoConfigOptions geoIp;
    geoIp.routingMode = RoutingMode::Rule;
    geoIp.rules = {makeRule(RuleMatcher::Kind::GeoIp, QStringLiteral("US"), RuleAction::Proxy)};
    const Result<Json> ip = buildSingBoxConfig(selected, geoIp);
    ASSERT_TRUE(ip.isErr());
    EXPECT_EQ(ip.error(), Messages::fill(Messages::singBoxGeoIpUnsupported(), {{"{value}", "US"}}));

    MihomoConfigOptions port;
    port.routingMode = RoutingMode::Rule;
    port.rules = {
        makeRule(RuleMatcher::Kind::DestinationPort, QStringLiteral("70000"), RuleAction::Proxy),
    };
    const Result<Json> bad = buildSingBoxConfig(selected, port);
    ASSERT_TRUE(bad.isErr());
    EXPECT_EQ(bad.error(), Messages::fill(Messages::singBoxPortRuleInvalid(), {{"{value}", "70000"}}));
}

TEST(SingBoxKernelConfig, GlobalAndDirectModesEmitNoRulesAndOnlyChangeFinal)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());

    MihomoConfigOptions global;
    global.routingMode = RoutingMode::Global;
    const Result<Json> globalConfig = buildSingBoxConfig(selected, global);
    ASSERT_TRUE(globalConfig.isOk()) << globalConfig.error().toStdString();
    EXPECT_TRUE(globalConfig.value()["route"]["rules"].empty());
    EXPECT_EQ(JsonUtil::textOrEmpty(globalConfig.value()["route"], "final").toStdString(),
              std::string("proxy"));
    EXPECT_FALSE(globalConfig.value()["route"].contains("rule_set"));

    MihomoConfigOptions direct;
    direct.routingMode = RoutingMode::Direct;
    const Result<Json> directConfig = buildSingBoxConfig(selected, direct);
    ASSERT_TRUE(directConfig.isOk()) << directConfig.error().toStdString();
    EXPECT_TRUE(directConfig.value()["route"]["rules"].empty());
    EXPECT_EQ(JsonUtil::textOrEmpty(directConfig.value()["route"], "final").toStdString(),
              std::string("direct"));

    // Rule 模式且用户没有规则时同样没有 rules，但 final 仍是 proxy。
    MihomoConfigOptions rule;
    rule.routingMode = RoutingMode::Rule;
    const Result<Json> ruleConfig = buildSingBoxConfig(selected, rule);
    ASSERT_TRUE(ruleConfig.isOk()) << ruleConfig.error().toStdString();
    EXPECT_TRUE(ruleConfig.value()["route"]["rules"].empty());
    EXPECT_EQ(JsonUtil::textOrEmpty(ruleConfig.value()["route"], "final").toStdString(),
              std::string("proxy"));
}

TEST(SingBoxKernelConfig, TunModeAddsTunInboundAndHijacksDnsFirst)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    MihomoConfigOptions options;
    options.mode = ProxyMode::Tun;
    options.routingMode = RoutingMode::ChinaDirect;
    const Result<Json> built = buildSingBoxConfig(selected, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    ASSERT_EQ(built.value()["inbounds"].size(), size_t(2));
    EXPECT_EQ(dump(built.value()["inbounds"][1]),
              std::string(R"({"address":["172.19.0.1/30"],"auto_route":true,)"
                          R"("stack":"mixed","strict_route":true,"tag":"tun-in","type":"tun"})"));
    EXPECT_EQ(dump(built.value()["route"]["rules"][0]),
              std::string(R"({"action":"hijack-dns","protocol":"dns"})"));
}

TEST(SingBoxKernelConfig, AllowLanMovesTheInboundToAllInterfaces)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    MihomoConfigOptions options;
    options.allowLan = true;
    options.mixedPort = 12780;
    const Result<Json> built = buildSingBoxConfig(selected, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    EXPECT_EQ(dump(built.value()["inbounds"][0]),
              std::string(
                  R"({"listen":"0.0.0.0","listen_port":12780,"tag":"mixed-in","type":"mixed"})"));
}

TEST(SingBoxKernelConfig, DnsBlockMapsIpAndDohServers)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    MihomoConfigOptions options;
    options.dns.nameservers = {QStringLiteral("8.8.8.8"), QStringLiteral("https://doh.pub/dns-query"),
                               QStringLiteral("https://dns.example.com:8443/dns-query")};
    const Result<Json> built = buildSingBoxConfig(selected, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    const Json &dns = built.value()["dns"];
    EXPECT_EQ(dump(dns["servers"][0]), std::string(R"({"tag":"local","type":"local"})"));
    EXPECT_EQ(dump(dns["servers"][1]),
              std::string(R"({"server":"8.8.8.8","server_port":53,"tag":"dns-0","type":"udp"})"));
    EXPECT_EQ(dump(dns["servers"][2]),
              std::string(R"({"domain_resolver":"local","path":"/dns-query","server":"doh.pub",)"
                          R"("server_port":443,"tag":"dns-1",)"
                          R"("tls":{"enabled":true,"server_name":"doh.pub"},"type":"https"})"));
    EXPECT_EQ(dump(dns["servers"][3]),
              std::string(R"({"domain_resolver":"local","path":"/dns-query",)"
                          R"("server":"dns.example.com","server_port":8443,"tag":"dns-2",)"
                          R"("tls":{"enabled":true,"server_name":"dns.example.com"},)"
                          R"("type":"https"})"));
    EXPECT_EQ(JsonUtil::textOrEmpty(dns, "final").toStdString(), std::string("dns-0"));
    EXPECT_EQ(JsonUtil::textOrEmpty(dns, "strategy").toStdString(), std::string("ipv4_only"));
    EXPECT_EQ(dump(dns),
              std::string(R"({"final":"dns-0","servers":[)"
                          R"({"tag":"local","type":"local"},)"
                          R"({"server":"8.8.8.8","server_port":53,"tag":"dns-0","type":"udp"},)"
                          R"({"domain_resolver":"local","path":"/dns-query","server":"doh.pub",)"
                          R"("server_port":443,"tag":"dns-1",)"
                          R"("tls":{"enabled":true,"server_name":"doh.pub"},"type":"https"},)"
                          R"({"domain_resolver":"local","path":"/dns-query","server":"dns.example.com",)"
                          R"("server_port":8443,"tag":"dns-2",)"
                          R"("tls":{"enabled":true,"server_name":"dns.example.com"},"type":"https"}],)"
                          R"("strategy":"ipv4_only"})"));

    MihomoConfigOptions withV6 = options;
    withV6.dns.ipv6 = true;
    const Result<Json> v6 = buildSingBoxConfig(selected, withV6);
    ASSERT_TRUE(v6.isOk()) << v6.error().toStdString();
    EXPECT_EQ(JsonUtil::textOrEmpty(v6.value()["dns"], "strategy").toStdString(),
              std::string("prefer_ipv4"));

    // IP 形态的 DoH 主机不需要 local 兜底解析。
    MihomoConfigOptions ipHost = options;
    ipHost.dns.nameservers = {QStringLiteral("https://1.1.1.1/dns-query")};
    const Result<Json> ipDns = buildSingBoxConfig(selected, ipHost);
    ASSERT_TRUE(ipDns.isOk()) << ipDns.error().toStdString();
    EXPECT_EQ(dump(ipDns.value()["dns"]["servers"][1]),
              std::string(R"({"path":"/dns-query","server":"1.1.1.1","server_port":443,)"
                          R"("tag":"dns-0","tls":{"enabled":true,"server_name":"1.1.1.1"},)"
                          R"("type":"https"})"));

    // 非法 URL / 非 IP 非 https：都报错，不静默退回 local。
    MihomoConfigOptions unparseable = options;
    unparseable.dns.nameservers = {QStringLiteral("https://exa mple.com/dns-query")};
    EXPECT_TRUE(buildSingBoxConfig(selected, unparseable).isErr());

    MihomoConfigOptions schemeless = options;
    schemeless.dns.nameservers = {QStringLiteral("doh.pub/dns-query")};
    EXPECT_TRUE(buildSingBoxConfig(selected, schemeless).isErr());
}

TEST(SingBoxKernelConfig, ValidateRejectsSamePortForMixedAndController)
{
    const Models::Node selected = singBoxNode(QStringLiteral("tuic"), tuicNodeConfig());
    MihomoConfigOptions options;
    options.controllerPort = options.mixedPort;
    const Result<Json> built = buildSingBoxConfig(selected, options);
    ASSERT_TRUE(built.isErr());
    // 端口冲突的文案由 vccProxy 的 MihomoConfigOptions::validate 提供，这里不重复转录。
    EXPECT_EQ(built.error(), options.validate().error());
}

// ---------------------------------------------------------------------------
// sing_box.rs: sing_box_uses_one_process_per_sixty_four_nodes
// ---------------------------------------------------------------------------

TEST(SingBoxKernelConfig, UsesOneProcessPerSixtyFourNodes)
{
    EXPECT_EQ(singBoxBatchCount(0), qsizetype(0));
    EXPECT_EQ(singBoxBatchCount(64), qsizetype(1));
    EXPECT_EQ(singBoxBatchCount(65), qsizetype(2));
    EXPECT_EQ(singBoxBatchCount(128), qsizetype(2));
}

// ---------------------------------------------------------------------------
// sing_box.rs: probe_batch_with_sing_box（spawn 之前的纯逻辑）
// ---------------------------------------------------------------------------

TEST(SingBoxKernelConfig, EmptyBatchHasNoInbounds)
{
    const Result<SingBoxBatchProbePlan> plan =
        buildSingBoxBatchProbePlan(QVector<Models::Node>{}, QVector<quint16>{});
    ASSERT_TRUE(plan.isOk()) << plan.error().toStdString();
    // 对应 Rust: verify_nodes_with_sing_box(vec![]) 返回空 + `if inbounds.is_empty()` 分支。
    EXPECT_FALSE(plan.value().hasInbounds);
    EXPECT_TRUE(plan.value().entries.isEmpty());
    EXPECT_TRUE(plan.value().failures.isEmpty());
    EXPECT_TRUE(plan.value().config.is_null());
}

TEST(SingBoxKernelConfig, BatchBindsTagsPerNodeAndKeepsWireGuardInEndpoints)
{
    const QVector<Models::Node> nodes = {
        singBoxNode(QStringLiteral("tuic"), tuicNodeConfig()),
        singBoxNode(QStringLiteral("wireguard"), wireGuardNodeConfig()),
        singBoxNode(QStringLiteral("ss"), parseJson(R"({"server":"1.2.3.4","port":443})")),
    };
    const QVector<quint16> ports = {20001, 20002, 20003};
    const Result<SingBoxBatchProbePlan> plan = buildSingBoxBatchProbePlan(nodes, ports);
    ASSERT_TRUE(plan.isOk()) << plan.error().toStdString();
    ASSERT_TRUE(plan.value().hasInbounds);
    ASSERT_EQ(plan.value().entries.size(), 2);
    EXPECT_EQ(plan.value().entries[0].nodeIndex, qsizetype(0));
    EXPECT_EQ(plan.value().entries[0].port, quint16(20001));
    EXPECT_EQ(plan.value().entries[0].inboundTag.toStdString(), std::string("probe-in-0"));
    EXPECT_EQ(plan.value().entries[0].outboundTag.toStdString(), std::string("probe-out-0"));
    EXPECT_EQ(plan.value().entries[1].nodeIndex, qsizetype(1));
    EXPECT_EQ(plan.value().entries[1].inboundTag.toStdString(), std::string("probe-in-1"));
    EXPECT_EQ(plan.value().readyPort, quint16(20001));

    // 第三个节点建配失败：failures[2] 带 Rust 的完整文案，其余保持 null。
    ASSERT_EQ(plan.value().failures.size(), 3);
    EXPECT_TRUE(plan.value().failures[0].isNull());
    EXPECT_TRUE(plan.value().failures[1].isNull());
    ASSERT_FALSE(plan.value().failures[2].isNull());
    EXPECT_EQ(plan.value().failures[2],
              Messages::fill(Messages::singBoxConfigUnsupported(),
                             {{"{error}", Messages::fill(Messages::singBoxProtocolUnsupported(),
                                                         {{"{}", "ss"}})}}));

    const Json &config = plan.value().config;
    ASSERT_EQ(config["inbounds"].size(), size_t(2));
    EXPECT_EQ(dump(config["inbounds"][0]),
              std::string(
                  R"({"listen":"127.0.0.1","listen_port":20001,"tag":"probe-in-0","type":"mixed"})"));
    EXPECT_EQ(dump(config["inbounds"][1]),
              std::string(
                  R"({"listen":"127.0.0.1","listen_port":20002,"tag":"probe-in-1","type":"mixed"})"));
    EXPECT_EQ(dump(config["outbounds"][0]), std::string(R"({"tag":"direct","type":"direct"})"));
    EXPECT_EQ(JsonUtil::textOrEmpty(config["outbounds"][1], "tag").toStdString(),
              std::string("probe-out-0"));
    EXPECT_EQ(JsonUtil::textOrEmpty(config["outbounds"][1], "type").toStdString(),
              std::string("tuic"));
    ASSERT_EQ(config["endpoints"].size(), size_t(1));
    EXPECT_EQ(JsonUtil::textOrEmpty(config["endpoints"][0], "tag").toStdString(),
              std::string("probe-out-1"));
    EXPECT_EQ(dump(config["route"]),
              std::string(R"({"auto_detect_interface":true,"default_domain_resolver":"local",)"
                          R"("final":"direct","rules":[)"
                          R"({"action":"route","inbound":["probe-in-0"],"outbound":"probe-out-0"},)"
                          R"({"action":"route","inbound":["probe-in-1"],"outbound":"probe-out-1"}]})"));
    EXPECT_EQ(dump(config["log"]), std::string(R"({"level":"warning","timestamp":true})"));

    // dns 段用的是默认选项那一份（与 Rust 的 build_dns(&MihomoConfigOptions::default()) 一致）。
    const Result<Json> reference = buildSingBoxConfig(nodes[0], MihomoConfigOptions());
    ASSERT_TRUE(reference.isOk()) << reference.error().toStdString();
    EXPECT_EQ(config["dns"], reference.value()["dns"]);
}

TEST(SingBoxKernelConfig, BatchRejectsPortCountMismatch)
{
    const QVector<Models::Node> nodes = {singBoxNode(QStringLiteral("tuic"), tuicNodeConfig())};
    const QVector<quint16> ports = {20001, 20002};
    EXPECT_TRUE(buildSingBoxBatchProbePlan(nodes, ports).isErr());
}

TEST(SingBoxKernelConfig, BatchWithoutAnyUsableNodeReportsOnlyFailures)
{
    const QVector<Models::Node> nodes = {
        singBoxNode(QStringLiteral("ss"), parseJson(R"({"server":"1.2.3.4","port":443})")),
        singBoxNode(QStringLiteral("vmess"), parseJson(R"({"server":"1.2.3.4","port":443})")),
    };
    const QVector<quint16> ports = {20001, 20002};
    const Result<SingBoxBatchProbePlan> plan = buildSingBoxBatchProbePlan(nodes, ports);
    ASSERT_TRUE(plan.isOk()) << plan.error().toStdString();
    EXPECT_FALSE(plan.value().hasInbounds);
    EXPECT_TRUE(plan.value().entries.isEmpty());
    EXPECT_TRUE(plan.value().config.is_null());
    ASSERT_EQ(plan.value().failures.size(), 2);
    EXPECT_FALSE(plan.value().failures[0].isNull());
    EXPECT_FALSE(plan.value().failures[1].isNull());
}

// ---------------------------------------------------------------------------
// sing_box.rs: last_sing_box_log_line
// ---------------------------------------------------------------------------

TEST(SingBoxKernelConfig, LastLogLineSkipsBlankLinesAndFallsBack)
{
    EXPECT_EQ(lastSingBoxLogLine(QStringLiteral("first\n  second  \n\n")),
              QStringLiteral("second"));
    EXPECT_EQ(lastSingBoxLogLine(QStringLiteral("one\r\ntwo\r\n")), QStringLiteral("two"));
    EXPECT_EQ(lastSingBoxLogLine(QString()), Messages::singBoxNoLogLine());
    EXPECT_EQ(lastSingBoxLogLine(QStringLiteral("   \n\t\n")), Messages::singBoxNoLogLine());
}

// ---------------------------------------------------------------------------
// sing_box.rs: installed_sing_box_accepts_generated_configs
// ---------------------------------------------------------------------------

TEST(SingBoxKernelConfig, GeneratedConfigsSerializeTheSameWayRustWritesThem)
{
    // Rust 这条测试把 config.json 落盘后交给 `sing-box check`。本任务禁止执行内核，
    // 所以这里只覆盖它 spawn 之前的半步：序列化能往返、top-level 形状与 Rust 一致。
    const QVector<Models::Node> nodes = {
        singBoxNode(QStringLiteral("hysteria2"), hysteria2NodeConfig()),
        singBoxNode(QStringLiteral("tuic"), tuicNodeConfig()),
        singBoxNode(QStringLiteral("anytls"), anytlsNodeConfig()),
        singBoxNode(QStringLiteral("wireguard"), wireGuardNodeConfig()),
    };
    for (const Models::Node &node : nodes) {
        const Result<Json> built = buildSingBoxConfig(node, MihomoConfigOptions());
        ASSERT_TRUE(built.isOk()) << node.protocol.toStdString() << " "
                                  << built.error().toStdString();
        const QString text = JsonUtil::dump(built.value());
        const Result<Json> roundTrip = JsonUtil::parse(text);
        ASSERT_TRUE(roundTrip.isOk()) << node.protocol.toStdString();
        EXPECT_EQ(roundTrip.value(), built.value()) << node.protocol.toStdString();
        EXPECT_TRUE(built.value().contains("log"));
        EXPECT_TRUE(built.value().contains("dns"));
        EXPECT_TRUE(built.value().contains("inbounds"));
        EXPECT_TRUE(built.value().contains("outbounds"));
        EXPECT_TRUE(built.value().contains("route"));
        // 端口必须是整数，不能出现 443.0。
        EXPECT_FALSE(text.contains(QStringLiteral("443.0"))) << node.protocol.toStdString();
        EXPECT_EQ(JsonUtil::textOrEmpty(built.value()["log"], "level").toStdString(),
                  std::string("info"));
    }
}

// ---------------------------------------------------------------------------
// xray.rs: builds_supported_xray_outbounds
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, BuildsSupportedXrayOutbounds)
{
    struct Case {
        QString protocol;
        Json config;
        const char *xrayProtocol;
    };
    const QVector<Case> cases = {
        {QStringLiteral("ss"), parseJson(R"({"cipher":"aes-128-gcm","password":"p"})"),
         "shadowsocks"},
        {QStringLiteral("vmess"), parseJson(R"({"uuid":"00000000-0000-0000-0000-000000000000"})"),
         "vmess"},
        {QStringLiteral("vless"), parseJson(R"({"uuid":"00000000-0000-0000-0000-000000000000"})"),
         "vless"},
        {QStringLiteral("trojan"), parseJson(R"({"password":"p"})"), "trojan"},
        {QStringLiteral("http"), parseJson(R"({"username":"u","password":"p"})"), "http"},
        {QStringLiteral("socks5"), parseJson(R"({"username":"u","password":"p"})"), "socks"},
    };
    for (const Case &item : cases) {
        const Result<Json> built =
            buildXrayConfig(xrayNode(item.protocol, item.config), MihomoConfigOptions());
        ASSERT_TRUE(built.isOk()) << item.protocol.toStdString() << " "
                                  << built.error().toStdString();
        EXPECT_EQ(JsonUtil::textOrEmpty(built.value()["outbounds"][0], "protocol").toStdString(),
                  std::string(item.xrayProtocol))
            << item.protocol.toStdString();
        EXPECT_EQ(JsonUtil::textOrEmpty(built.value()["outbounds"][0], "tag").toStdString(),
                  std::string("proxy"));
    }
    // https → http 的名字映射；streamSettings 只出现在 vmess/vless/trojan 上。
    const Result<Json> https =
        buildXrayConfig(xrayNode(QStringLiteral("https"), parseJson(R"({"password":"p"})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(https.isOk()) << https.error().toStdString();
    EXPECT_EQ(JsonUtil::textOrEmpty(https.value()["outbounds"][0], "protocol").toStdString(),
              std::string("http"));
    EXPECT_FALSE(https.value()["outbounds"][0].contains("streamSettings"));

    const Result<Json> trojanTls = buildXrayConfig(
        xrayNode(QStringLiteral("trojan"),
                 parseJson(R"({"password":"p","security":"tls","sni":"example.com"})")),
        MihomoConfigOptions());
    ASSERT_TRUE(trojanTls.isOk()) << trojanTls.error().toStdString();
    EXPECT_EQ(dump(trojanTls.value()["outbounds"][0]["streamSettings"]),
              std::string(R"({"network":"tcp","security":"tls",)"
                          R"("tlsSettings":{"allowInsecure":false,"serverName":"example.com"}})"));
}

// ---------------------------------------------------------------------------
// xray.rs: rejects_xray_tun_mode
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, RejectsTunMode)
{
    MihomoConfigOptions options;
    options.mode = ProxyMode::Tun;
    const Result<Json> built =
        buildXrayConfig(xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")), options);
    ASSERT_TRUE(built.isErr());
    EXPECT_EQ(built.error(), Messages::xrayTunUnsupported());
}

// ---------------------------------------------------------------------------
// xray.rs: xray_config_uses_configured_dns_servers
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, ConfigUsesConfiguredDnsServers)
{
    const Result<Json> built =
        buildXrayConfig(xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    EXPECT_EQ(built.value()["dns"]["servers"][0], Json("https://doh.pub/dns-query"));
    EXPECT_EQ(built.value()["dns"]["servers"][1], Json("https://dns.alidns.com/dns-query"));
    EXPECT_EQ(JsonUtil::textOrEmpty(built.value()["dns"], "queryStrategy").toStdString(),
              std::string("UseIPv4"));

    // dns.enabled=false → 只剩 localhost；queryStrategy 仍只看 ipv6。
    MihomoConfigOptions off;
    off.dns.enabled = false;
    off.dns.ipv6 = true;
    const Result<Json> disabled =
        buildXrayConfig(xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")), off);
    ASSERT_TRUE(disabled.isOk()) << disabled.error().toStdString();
    EXPECT_EQ(dump(disabled.value()["dns"]),
              std::string(R"({"queryStrategy":"UseIP","servers":["localhost"]})"));

    // 顶层形状：log / dns / inbounds / outbounds(proxy, direct, blocked) / routing。
    EXPECT_EQ(dump(built.value()["inbounds"][0]),
              std::string(R"({"listen":"127.0.0.1","port":9980,"protocol":"http",)"
                          R"("settings":{"allowTransparent":false},"tag":"local-http"})"));
    EXPECT_EQ(dump(built.value()["outbounds"][1]),
              std::string(R"({"protocol":"freedom","settings":{},"tag":"direct"})"));
    EXPECT_EQ(dump(built.value()["outbounds"][2]),
              std::string(R"({"protocol":"blackhole","settings":{},"tag":"blocked"})"));
    EXPECT_EQ(dump(built.value()["log"]), std::string(R"({"loglevel":"warning"})"));
    EXPECT_EQ(JsonUtil::textOrEmpty(built.value()["routing"], "domainStrategy").toStdString(),
              std::string("AsIs"));
}

// ---------------------------------------------------------------------------
// xray.rs: xray_rejects_unsupported_process_routing_rule
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, RejectsUnsupportedProcessNameRule)
{
    MihomoConfigOptions options;
    options.routingMode = RoutingMode::Rule;
    options.rules = {
        makeRule(RuleMatcher::Kind::ProcessName, QStringLiteral("app.exe"), RuleAction::Proxy),
    };
    const Result<Json> built =
        buildXrayConfig(xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")), options);
    ASSERT_TRUE(built.isErr());
    // Rust 只断言错误文本含"进程名分流"那一段；这里连整条文案一起钉住。
    EXPECT_TRUE(built.error().contains(Messages::xrayProcessNameMarker()));
    EXPECT_EQ(built.error(), Messages::fill(Messages::xrayProcessNameUnsupported(),
                                            {{"{name}", "app.exe"}}));
}

// ---------------------------------------------------------------------------
// xray.rs: xray_routing_presets_map_rules_and_fallbacks
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, RoutingPresetsMapRulesAndFallbacks)
{
    const Models::Node selected = xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})"));

    MihomoConfigOptions china;
    china.routingMode = RoutingMode::ChinaDirect;
    const Result<Json> chinaConfig = buildXrayConfig(selected, china);
    ASSERT_TRUE(chinaConfig.isOk()) << chinaConfig.error().toStdString();
    bool hasGeoSiteCn = false;
    for (const Json &rule : chinaConfig.value()["routing"]["rules"]) {
        if (rule.contains("domain") && rule["domain"].is_array() && !rule["domain"].empty()
            && rule["domain"][0] == Json("geosite:cn")) {
            hasGeoSiteCn = true;
        }
    }
    EXPECT_TRUE(hasGeoSiteCn);
    const Json chinaLast = chinaConfig.value()["routing"]["rules"].back();
    EXPECT_EQ(JsonUtil::textOrEmpty(chinaLast, "outboundTag").toStdString(), std::string("proxy"));
    EXPECT_EQ(dump(chinaLast),
              std::string(R"({"inboundTag":["local-http"],"outboundTag":"proxy","type":"field"})"));

    MihomoConfigOptions gfw;
    gfw.routingMode = RoutingMode::GfwList;
    const Result<Json> gfwConfig = buildXrayConfig(selected, gfw);
    ASSERT_TRUE(gfwConfig.isOk()) << gfwConfig.error().toStdString();
    bool hasGeoSiteGfw = false;
    for (const Json &rule : gfwConfig.value()["routing"]["rules"]) {
        if (rule.contains("domain") && rule["domain"].is_array() && !rule["domain"].empty()
            && rule["domain"][0] == Json("geosite:gfw")) {
            hasGeoSiteGfw = true;
        }
    }
    EXPECT_TRUE(hasGeoSiteGfw);
    EXPECT_EQ(JsonUtil::textOrEmpty(gfwConfig.value()["routing"]["rules"].back(), "outboundTag")
                  .toStdString(),
              std::string("direct"));
}

TEST(XrayKernelConfig, RoutingRuleMappingForEveryMatcher)
{
    const Models::Node selected =
        xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})"));
    MihomoConfigOptions options;
    options.routingMode = RoutingMode::Rule;
    options.rules = {
        makeRule(RuleMatcher::Kind::Domain, QStringLiteral("example.com"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::DomainSuffix, QStringLiteral("example.com"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::GeoSite, QStringLiteral("YouTube"), RuleAction::Proxy),
        makeRule(RuleMatcher::Kind::GeoIp, QStringLiteral("US"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::IpCidr, QStringLiteral("10.0.0.0/8"), RuleAction::Reject),
        makeRule(RuleMatcher::Kind::DestinationPort, QStringLiteral("8080"), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::Network, QString(), RuleAction::Direct),
        makeRule(RuleMatcher::Kind::Quic, QString(), RuleAction::Reject),
    };
    options.rules[6].matcher = RuleMatcher::makeNetwork(NetworkProtocol::Udp);
    options.rules[6].action = RuleAction::Proxy;
    const Result<Json> built = buildXrayConfig(selected, options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    const Json &rules = built.value()["routing"]["rules"];
    ASSERT_EQ(rules.size(), size_t(9));
    EXPECT_EQ(dump(rules[0]),
              std::string(R"({"domain":["full:example.com"],"outboundTag":"direct","type":"field"})"));
    EXPECT_EQ(dump(rules[1]),
              std::string(
                  R"({"domain":["domain:example.com"],"outboundTag":"proxy","type":"field"})"));
    // GEOSITE 分类名不折叠大小写，GEOIP 才折叠；REJECT 走 blocked 出口。
    EXPECT_EQ(dump(rules[2]),
              std::string(R"({"domain":["geosite:YouTube"],"outboundTag":"proxy","type":"field"})"));
    EXPECT_EQ(dump(rules[3]),
              std::string(R"({"ip":["geoip:us"],"outboundTag":"direct","type":"field"})"));
    EXPECT_EQ(dump(rules[4]),
              std::string(R"({"ip":["10.0.0.0/8"],"outboundTag":"blocked","type":"field"})"));
    // Xray 的 port 是字符串（Rust 传的是 &str，只借 parse::<u16> 校验）。
    EXPECT_EQ(dump(rules[5]),
              std::string(R"({"outboundTag":"direct","port":"8080","type":"field"})"));
    EXPECT_EQ(dump(rules[6]),
              std::string(R"({"network":"udp","outboundTag":"proxy","type":"field"})"));
    EXPECT_EQ(dump(rules[7]),
              std::string(R"({"network":"udp","outboundTag":"blocked","port":"443","type":"field"})"));
    EXPECT_EQ(dump(rules[8]),
              std::string(R"({"inboundTag":["local-http"],"outboundTag":"proxy","type":"field"})"));
}

TEST(XrayKernelConfig, GlobalAndDirectModesOnlyEmitTheFallbackRule)
{
    const Models::Node selected =
        xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})"));

    MihomoConfigOptions global;
    global.routingMode = RoutingMode::Global;
    const Result<Json> globalConfig = buildXrayConfig(selected, global);
    ASSERT_TRUE(globalConfig.isOk()) << globalConfig.error().toStdString();
    ASSERT_EQ(globalConfig.value()["routing"]["rules"].size(), size_t(1));
    EXPECT_EQ(dump(globalConfig.value()["routing"]["rules"][0]),
              std::string(R"({"inboundTag":["local-http"],"outboundTag":"proxy","type":"field"})"));

    MihomoConfigOptions direct;
    direct.routingMode = RoutingMode::Direct;
    const Result<Json> directConfig = buildXrayConfig(selected, direct);
    ASSERT_TRUE(directConfig.isOk()) << directConfig.error().toStdString();
    ASSERT_EQ(directConfig.value()["routing"]["rules"].size(), size_t(1));
    EXPECT_EQ(JsonUtil::textOrEmpty(directConfig.value()["routing"]["rules"][0], "outboundTag")
                  .toStdString(),
              std::string("direct"));
}

TEST(XrayKernelConfig, RejectsBadPortUnsupportedProtocolAndMissingFields)
{
    const Models::Node selected =
        xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})"));
    MihomoConfigOptions port;
    port.routingMode = RoutingMode::Rule;
    port.rules = {
        makeRule(RuleMatcher::Kind::DestinationPort, QStringLiteral("nope"), RuleAction::Proxy),
    };
    const Result<Json> badPort = buildXrayConfig(selected, port);
    ASSERT_TRUE(badPort.isErr());
    EXPECT_EQ(badPort.error(), Messages::fill(Messages::xrayPortRuleInvalid(), {{"{port}", "nope"}}));

    const Result<Json> badProtocol =
        buildXrayConfig(xrayNode(QStringLiteral("hysteria2"), parseJson(R"({"password":"p"})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(badProtocol.isErr());
    EXPECT_EQ(badProtocol.error(),
              Messages::fill(Messages::xrayProtocolUnsupported(), {{"{}", "hysteria2"}}));

    const Result<Json> missing =
        buildXrayConfig(xrayNode(QStringLiteral("vmess"), parseJson(R"({"alterId":1})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(missing.isErr());
    EXPECT_EQ(missing.error(), Messages::fill(Messages::xrayMissingField(), {{"{}", "uuid/id"}}));

    MihomoConfigOptions samePort;
    samePort.controllerPort = samePort.mixedPort;
    const Result<Json> invalid =
        buildXrayConfig(xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")),
                        samePort);
    ASSERT_TRUE(invalid.isErr());
    EXPECT_EQ(invalid.error(), samePort.validate().error());
}

TEST(XrayKernelConfig, NullableStringsKeepSomeEmptyAndDropMissing)
{
    // xray 侧的 string() 不 trim：空串算 Some("")，缺键才是 None。
    const Result<Json> vless = buildXrayConfig(
        xrayNode(QStringLiteral("vless"),
                 parseJson(R"({"uuid":"00000000-0000-0000-0000-000000000000","flow":""})")),
        MihomoConfigOptions());
    ASSERT_TRUE(vless.isOk()) << vless.error().toStdString();
    EXPECT_EQ(dump(vless.value()["outbounds"][0]["settings"]),
              std::string(R"({"vnext":[{"address":"example.com","port":443,)"
                          R"("users":[{"encryption":"none","flow":"","id":)"
                          R"("00000000-0000-0000-0000-000000000000"}]}]})"));

    // vmess 才写 alterId/security；alterId 只认数字（不像 sing-box 那样解析字符串）。
    const Result<Json> vmess = buildXrayConfig(
        xrayNode(QStringLiteral("vmess"),
                 parseJson(R"({"uuid":"00000000-0000-0000-0000-000000000000","alterId":"1"})")),
        MihomoConfigOptions());
    ASSERT_TRUE(vmess.isOk()) << vmess.error().toStdString();
    EXPECT_EQ(dump(vmess.value()["outbounds"][0]["settings"]),
              std::string(R"({"vnext":[{"address":"example.com","port":443,)"
                          R"("users":[{"alterId":0,"id":)"
                          R"("00000000-0000-0000-0000-000000000000","security":"auto"}]}]})"));
    EXPECT_EQ(dump(vmess.value()["outbounds"][0]["streamSettings"]),
              std::string(R"({"network":"tcp","security":"none"})"));
    EXPECT_EQ(dump(vmess.value()["outbounds"][0]["streamSettings"]),
              dump(vless.value()["outbounds"][0]["streamSettings"]));

    const Result<Json> withCipher = buildXrayConfig(
        xrayNode(QStringLiteral("vmess"),
                 parseJson(R"({"uuid":"u","alterId":8,"cipher":"aes-128-gcm","network":"tcp"})")),
        MihomoConfigOptions());
    ASSERT_TRUE(withCipher.isOk()) << withCipher.error().toStdString();
    EXPECT_EQ(dump(withCipher.value()["outbounds"][0]["settings"]),
              std::string(R"({"vnext":[{"address":"example.com","port":443,)"
                          R"("users":[{"alterId":8,"id":"u","security":"aes-128-gcm"}]}]})"));
}

TEST(XrayKernelConfig, HttpAndSocksServersOnlyAddUsersWhenGiven)
{
    const Result<Json> plain =
        buildXrayConfig(xrayNode(QStringLiteral("socks"), parseJson(R"({"port":1080})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(plain.isOk()) << plain.error().toStdString();
    EXPECT_EQ(dump(plain.value()["outbounds"][0]),
              std::string(R"({"protocol":"socks","settings":{"servers":[)"
                          R"({"address":"example.com","port":443}]},)"
                          R"("tag":"proxy"})"));

    const Result<Json> onlyPass =
        buildXrayConfig(xrayNode(QStringLiteral("http"), parseJson(R"({"password":"p"})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(onlyPass.isOk()) << onlyPass.error().toStdString();
    EXPECT_EQ(dump(onlyPass.value()["outbounds"][0]["settings"]),
              std::string(R"({"servers":[{"address":"example.com","port":443,)"
                          R"("users":[{"pass":"p","user":""}]}]})"));
}

TEST(XrayKernelConfig, RealityGrpcAndWebsocketStreamSettings)
{
    const Result<Json> reality = buildXrayConfig(
        xrayNode(QStringLiteral("vless"),
                 parseJson(R"({"uuid":"u","network":"grpc","security":"reality","serviceName":"x",
                               "public-key":"pk","short-id":"si","fp":"firefox"})")),
        MihomoConfigOptions());
    ASSERT_TRUE(reality.isOk()) << reality.error().toStdString();
    EXPECT_EQ(dump(reality.value()["outbounds"][0]["streamSettings"]),
              std::string(R"({"grpcSettings":{"serviceName":"x"},"network":"grpc",)"
                          R"("realitySettings":{"fingerprint":"firefox","publicKey":"pk",)"
                          R"("serverName":"","shortId":"si"},)"
                          R"("security":"reality"})"));

    const Result<Json> ws = buildXrayConfig(
        xrayNode(QStringLiteral("vless"),
                 parseJson(R"({"uuid":"u","network":"ws","host":"a.example.com","path":"/ws"})")),
        MihomoConfigOptions());
    ASSERT_TRUE(ws.isOk()) << ws.error().toStdString();
    EXPECT_EQ(dump(ws.value()["outbounds"][0]["streamSettings"]),
              std::string(R"({"network":"ws","security":"none",)"
                          R"("wsSettings":{"headers":{"Host":"a.example.com"},"path":"/ws"}})"));

    // 没有 host 时 headers 是空对象，path 缺省是 "/"。
    const Result<Json> bareWs =
        buildXrayConfig(xrayNode(QStringLiteral("vless"), parseJson(R"({"uuid":"u","network":"ws"})")),
                        MihomoConfigOptions());
    ASSERT_TRUE(bareWs.isOk()) << bareWs.error().toStdString();
    EXPECT_EQ(dump(bareWs.value()["outbounds"][0]["streamSettings"]),
              std::string(R"({"network":"ws","security":"none",)"
                          R"("wsSettings":{"headers":{},"path":"/"}})"));

    // security 只认 tls/reality，其它取值一律退回 none。
    const Result<Json> other = buildXrayConfig(
        xrayNode(QStringLiteral("vless"), parseJson(R"({"uuid":"u","security":"auto"})")),
        MihomoConfigOptions());
    ASSERT_TRUE(other.isOk()) << other.error().toStdString();
    EXPECT_EQ(JsonUtil::textOrEmpty(other.value()["outbounds"][0]["streamSettings"], "security")
                  .toStdString(),
              std::string("none"));
}

TEST(XrayKernelConfig, NonObjectNodeConfigIsTreatedAsEmpty)
{
    Models::Node node = xrayNode(QStringLiteral("trojan"), Json());
    const Result<Json> built = buildXrayConfig(node, MihomoConfigOptions());
    ASSERT_TRUE(built.isErr());
    EXPECT_EQ(built.error(), Messages::fill(Messages::xrayMissingField(), {{"{}", "password"}}));
}

// ---------------------------------------------------------------------------
// xray.rs: xray_uses_one_process_per_sixty_four_nodes
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, UsesOneProcessPerSixtyFourNodes)
{
    EXPECT_EQ(xrayBatchCount(0), qsizetype(0));
    EXPECT_EQ(xrayBatchCount(1), qsizetype(1));
    EXPECT_EQ(xrayBatchCount(64), qsizetype(1));
    EXPECT_EQ(xrayBatchCount(65), qsizetype(2));
    EXPECT_EQ(xrayBatchCount(129), qsizetype(3));
}

// ---------------------------------------------------------------------------
// xray.rs: probe_batch（spawn 之前的纯逻辑）
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, BatchBindsTagsPerNode)
{
    const QVector<Models::Node> nodes = {
        xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")),
        xrayNode(QStringLiteral("hysteria2"), parseJson(R"({"password":"p"})")),
        xrayNode(QStringLiteral("vmess"),
                 parseJson(R"({"uuid":"00000000-0000-0000-0000-000000000000"})")),
    };
    const QVector<quint16> ports = {21001, 21002, 21003};
    const Result<XrayBatchProbePlan> plan = buildXrayBatchProbePlan(nodes, ports);
    ASSERT_TRUE(plan.isOk()) << plan.error().toStdString();
    ASSERT_TRUE(plan.value().hasInbounds);
    ASSERT_EQ(plan.value().entries.size(), 2);
    EXPECT_EQ(plan.value().entries[0].nodeIndex, qsizetype(0));
    EXPECT_EQ(plan.value().entries[0].port, quint16(21001));
    EXPECT_EQ(plan.value().entries[0].inboundTag.toStdString(), std::string("probe-in-0"));
    EXPECT_EQ(plan.value().entries[0].outboundTag.toStdString(), std::string("probe-out-0"));
    EXPECT_EQ(plan.value().entries[1].nodeIndex, qsizetype(2));
    EXPECT_EQ(plan.value().readyPort, quint16(21001));

    ASSERT_EQ(plan.value().failures.size(), 3);
    EXPECT_TRUE(plan.value().failures[0].isNull());
    ASSERT_FALSE(plan.value().failures[1].isNull());
    EXPECT_EQ(plan.value().failures[1],
              Messages::fill(Messages::xrayConfigUnsupported(),
                             {{"{error}", Messages::fill(Messages::xrayProtocolUnsupported(),
                                                         {{"{}", "hysteria2"}})}}));
    EXPECT_TRUE(plan.value().failures[2].isNull());

    const Json &config = plan.value().config;
    EXPECT_EQ(dump(config["log"]), std::string(R"({"loglevel":"warning"})"));
    ASSERT_EQ(config["inbounds"].size(), size_t(2));
    EXPECT_EQ(dump(config["inbounds"][0]),
              std::string(R"({"listen":"127.0.0.1","port":21001,"protocol":"http",)"
                          R"("settings":{"allowTransparent":false},"tag":"probe-in-0"})"));
    ASSERT_EQ(config["outbounds"].size(), size_t(3));
    EXPECT_EQ(JsonUtil::textOrEmpty(config["outbounds"][0], "tag").toStdString(),
              std::string("probe-out-0"));
    EXPECT_EQ(JsonUtil::textOrEmpty(config["outbounds"][1], "tag").toStdString(),
              std::string("probe-out-2"));
    EXPECT_EQ(dump(config["outbounds"][2]),
              std::string(R"({"protocol":"freedom","settings":{},"tag":"direct"})"));
    EXPECT_EQ(dump(config["routing"]),
              std::string(R"({"domainStrategy":"AsIs","rules":[)"
                          R"({"inboundTag":["probe-in-0"],"outboundTag":"probe-out-0","type":"field"},)"
                          R"({"inboundTag":["probe-in-2"],"outboundTag":"probe-out-2","type":"field"}]})"));
    // 批量配置里没有 dns，也没有 blocked 出口。
    EXPECT_FALSE(config.contains("dns"));
}

TEST(XrayKernelConfig, BatchWithoutAnyUsableNodeReportsOnlyFailures)
{
    const QVector<Models::Node> nodes = {
        xrayNode(QStringLiteral("hysteria2"), parseJson(R"({"password":"p"})")),
    };
    const QVector<quint16> ports = {21001};
    const Result<XrayBatchProbePlan> plan = buildXrayBatchProbePlan(nodes, ports);
    ASSERT_TRUE(plan.isOk()) << plan.error().toStdString();
    EXPECT_FALSE(plan.value().hasInbounds);
    EXPECT_TRUE(plan.value().entries.isEmpty());
    EXPECT_TRUE(plan.value().config.is_null());
    ASSERT_EQ(plan.value().failures.size(), 1);
    EXPECT_FALSE(plan.value().failures[0].isNull());
}

TEST(XrayKernelConfig, BatchRejectsPortCountMismatch)
{
    const QVector<Models::Node> nodes = {
        xrayNode(QStringLiteral("trojan"), parseJson(R"({"password":"p"})")),
    };
    EXPECT_TRUE(buildXrayBatchProbePlan(nodes, QVector<quint16>{}).isErr());
}

TEST(XrayKernelConfig, LastLogLineTakesTheVeryLastLine)
{
    // Rust: detail.lines().last()，不跳过空行、不 trim。
    EXPECT_EQ(lastXrayLogLine(QStringLiteral("a\nb\n")), QStringLiteral("b"));
    EXPECT_EQ(lastXrayLogLine(QStringLiteral("a\n\n")), QStringLiteral(""));
    EXPECT_EQ(lastXrayLogLine(QString()), Messages::xrayUnknownError());
}

// ---------------------------------------------------------------------------
// xray.rs: installed_xray_accepts_generated_config
// ---------------------------------------------------------------------------

TEST(XrayKernelConfig, GeneratedConfigSerializesTheSameWayRustWritesIt)
{
    // Rust 这条测试把 config.json 落盘后跑 `xray run -test`。本任务禁止执行内核，
    // 所以只覆盖它 spawn 之前的半步。
    MihomoConfigOptions options;
    options.mixedPort = 17891;
    options.controllerPort = 19091;
    const Result<Json> built = buildXrayConfig(
        xrayNode(QStringLiteral("trojan"),
                 parseJson(R"({"password":"p","security":"tls","sni":"example.com"})")),
        options);
    ASSERT_TRUE(built.isOk()) << built.error().toStdString();
    const QString text = JsonUtil::dump(built.value());
    const Result<Json> roundTrip = JsonUtil::parse(text);
    ASSERT_TRUE(roundTrip.isOk());
    EXPECT_EQ(roundTrip.value(), built.value());
    EXPECT_FALSE(text.contains(QStringLiteral("443.0")));
    EXPECT_FALSE(text.contains(QStringLiteral("17891.0")));
}
