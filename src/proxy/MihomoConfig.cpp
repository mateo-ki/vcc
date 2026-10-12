#include "proxy/MihomoConfig.h"

#include <QSet>

#include <initializer_list>

#include "core/Log.h"
#include "export/ClashProxy.h"
#include "geo/GeoConstants.h"
#include "proxy/TestUrls.h"

namespace Proxy {

namespace {

QString fromCodePoints(std::initializer_list<char32_t> codePoints)
{
    const QVector<char32_t> points(codePoints.begin(), codePoints.end());
    return QString::fromUcs4(points.constData(), points.size());
}

/// 落库文本里"必填键缺失"的报错，对应 serde 在没有 #[serde(default)] 时的失败。
Result<bool> requireKeys(const Json &value, std::initializer_list<const char *> keys)
{
    if (!value.is_object()) {
        return Result<bool>::withError(QStringLiteral("解析已保存的代理配置失败"));
    }
    for (const char *key : keys) {
        if (!value.contains(key)) {
            return Result<bool>::withError(QStringLiteral("解析已保存的代理配置失败"));
        }
    }
    return Result<bool>::withValue(true);
}

}

const QString kUrlTestGroup = fromCodePoints({0x81EA, 0x52A8, 0x9009, 0x62E9});
const QString kFinalGroup = fromCodePoints({0x6F0F, 0x7F51, 0x4E4B, 0x9C7C});

QString proxyModeToText(ProxyMode mode)
{
    return mode == ProxyMode::Tun ? QStringLiteral("tun") : QStringLiteral("system");
}

ProxyMode proxyModeFromText(const QString &text)
{
    return text == QLatin1String("tun") ? ProxyMode::Tun : ProxyMode::System;
}

QString proxyStateToText(ProxyState state)
{
    switch (state) {
    case ProxyState::Stopped:
        return QStringLiteral("stopped");
    case ProxyState::Starting:
        return QStringLiteral("starting");
    case ProxyState::Running:
        return QStringLiteral("running");
    case ProxyState::Degraded:
        return QStringLiteral("degraded");
    case ProxyState::Stopping:
        return QStringLiteral("stopping");
    case ProxyState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("stopped");
}

bool stateHasLocalProxy(ProxyState state)
{
    return state == ProxyState::Running || state == ProxyState::Degraded;
}

Status MihomoConfigOptions::validate() const
{
    if (mixedPort == controllerPort) {
        return Status::failed(QStringLiteral("代理端口和控制端口不能相同"));
    }
    const Status dnsStatus = dns.validate();
    if (dnsStatus.isErr()) {
        return dnsStatus;
    }
    for (const TrafficRule &rule : rules) {
        const Result<QString> rendered = rule.toMihomoRule();
        if (rendered.isErr()) {
            return Status::failed(rendered.error());
        }
    }
    return Status::succeeded();
}

Json MihomoConfigOptions::toJson() const
{
    // controller_secret 与 url_test_nodes 是 #[serde(skip)]：不进持久化 JSON。
    Json value;
    value["mode"] = proxyModeToText(mode).toStdString();
    value["mixed_port"] = mixedPort;
    value["controller_port"] = controllerPort;
    value["allow_lan"] = allowLan;
    value["tun"] = tun.toJson();
    value["dns"] = dns.toJson();
    value["routing_mode"] = routingModeToText(routingMode).toStdString();
    Json rules = Json::array();
    for (const TrafficRule &rule : this->rules) {
        rules.push_back(rule.toJson());
    }
    value["rules"] = rules;
    value["kernel_preference"] = Kernel::preferenceToText(kernelPreference).toStdString();
    value["url_test"] = urlTest;
    value["url_test_size"] = urlTestSize;
    value["terminal_proxy"] = terminalProxy;
    return value;
}

MihomoConfigOptions MihomoConfigOptions::fromJson(const Json &value)
{
    MihomoConfigOptions options;
    options.mode = proxyModeFromText(JsonUtil::textOrEmpty(value, "mode"));
    options.mixedPort = quint16(JsonUtil::intOr(value, "mixed_port", 9980));
    options.controllerPort = quint16(JsonUtil::intOr(value, "controller_port", 9090));
    options.allowLan = JsonUtil::boolOr(value, "allow_lan", false);
    if (value.is_object() && value.contains("tun")) {
        options.tun = TunConfig::fromJson(value.at("tun"));
    }
    if (value.is_object() && value.contains("dns")) {
        options.dns = DnsConfig::fromJson(value.at("dns"));
    } else {
        options.dns = DnsConfig();
    }
    // #[serde(default)]：老配置没这个字段时按 Rule。
    options.routingMode = routingModeFromText(JsonUtil::textOrEmpty(value, "routing_mode"));
    options.rules.clear();
    if (value.is_object() && value.contains("rules") && value.at("rules").is_array()) {
        for (const Json &entry : value.at("rules")) {
            options.rules.append(TrafficRule::fromJson(entry));
        }
    }
    options.kernelPreference =
        Kernel::preferenceFromText(JsonUtil::textOrEmpty(value, "kernel_preference"));
    options.urlTest = JsonUtil::boolOr(value, "url_test", false);
    options.urlTestSize =
        quint16(JsonUtil::intOr(value, "url_test_size", kDefaultUrlTestSize));
    options.terminalProxy = JsonUtil::boolOr(value, "terminal_proxy", false);
    return options;
}

Result<MihomoConfigOptions> MihomoConfigOptions::fromSavedJsonText(const QString &jsonText)
{
    if (jsonText.isNull() || jsonText.trimmed().isEmpty()) {
        return Result<MihomoConfigOptions>::withValue(MihomoConfigOptions());
    }
    const Result<Json> parsed = JsonUtil::parse(jsonText);
    if (parsed.isErr()) {
        return Result<MihomoConfigOptions>::withError(
            QStringLiteral("解析已保存的代理配置失败"));
    }
    const Result<bool> required =
        requireKeys(parsed.value(), {"mode", "mixed_port", "controller_port", "allow_lan", "tun",
                                     "dns", "rules"});
    if (required.isErr()) {
        return Result<MihomoConfigOptions>::withError(required.error());
    }
    MihomoConfigOptions options = MihomoConfigOptions::fromJson(parsed.value());
    bool migrated = options.dns.migrateLegacyDefaults();
    if (options.mixedPort == 7890) {
        options.mixedPort = 9980;
        migrated = true;
    }
    if (migrated) {
        // 迁移只改内存值；写回 proxy_settings 由调用方（ProxyManager 持有 Database）完成，
        // 否则每次读配置都要重算一遍。
        Log::info(QStringLiteral("已把旧的默认 DNS / mixed 端口 7890 迁移到当前默认值"));
    }
    const Status valid = options.validate();
    if (valid.isErr()) {
        return Result<MihomoConfigOptions>::withError(valid.error());
    }
    return Result<MihomoConfigOptions>::withValue(options);
}

Result<QString> MihomoConfigOptions::toSavedJsonText() const
{
    const Status valid = validate();
    if (valid.isErr()) {
        return Result<QString>::withError(valid.error());
    }
    return Result<QString>::withValue(JsonUtil::dump(toJson()));
}

ProxyRuntimeStatus ProxyRuntimeStatus::stopped(const MihomoConfigOptions &options)
{
    ProxyRuntimeStatus status;
    status.state = ProxyState::Stopped;
    status.mode = options.mode;
    status.mixedPort = options.mixedPort;
    status.controllerPort = options.controllerPort;
    status.controllerSecret = options.controllerSecret;
    status.message = fromCodePoints({0x4EE3, 0x7406, 0x672A, 0x542F, 0x52A8});
    status.lastError = QString();
    return status;
}

bool ProxyRuntimeStatus::localProxyActive() const
{
    return localProxyReady && stateHasLocalProxy(state);
}

bool ProxyRuntimeStatus::configEditable() const
{
    return state == ProxyState::Stopped || state == ProxyState::Failed
        || (state == ProxyState::Degraded && !localProxyReady);
}

bool ProxyRuntimeStatus::kernelInUse(Kernel::Kind kind) const
{
    if (!kernel.has_value() || *kernel != kind) {
        return false;
    }
    if (state == ProxyState::Starting || state == ProxyState::Running
        || state == ProxyState::Stopping) {
        return true;
    }
    return state == ProxyState::Degraded && localProxyReady;
}

Json ProxyRuntimeStatus::toJson() const
{
    Json value;
    value["state"] = proxyStateToText(state).toStdString();
    value["kernel"] = kernel.has_value() ? Json(Kernel::kindToText(*kernel).toStdString())
                                         : Json();
    value["mode"] = proxyModeToText(mode).toStdString();
    value["node_id"] = JsonUtil::optText(nodeId);
    value["mixed_port"] = mixedPort;
    value["controller_port"] = controllerPort;
    value["message"] = message.toStdString();
    value["local_proxy_ready"] = localProxyReady;
    value["system_proxy_enabled"] = systemProxyEnabled;
    value["last_error"] = JsonUtil::optText(lastError);
    return value;
}

bool isNodeConnectivityFailure(const QString &message)
{
    static const QString marker =
        fromCodePoints({0x8282, 0x70B9, 0x8FDE, 0x901A, 0x6027, 0x9A8C, 0x8BC1, 0x5931, 0x8D25});
    return message.contains(marker);
}

/// 生成 mihomo 配置文档。
Result<Json> buildMihomoConfig(const Models::Node &node, MihomoConfigOptions options)
{
    const Status valid = options.validate();
    if (valid.isErr()) {
        return Result<Json>::withError(valid.error());
    }

    const QVector<Json> proxies = Export::nodesToClashProxies({node});
    if (proxies.isEmpty()) {
        return Result<Json>::withError(
            QStringLiteral("Mihomo 不支持协议: %1").arg(node.protocol));
    }
    const Json proxy = proxies.last();
    const QString proxyName = JsonUtil::textOrEmpty(proxy, "name");
    if (proxyName.isNull() || proxyName.isEmpty()) {
        return Result<Json>::withError(QStringLiteral("Mihomo 代理配置缺少名称"));
    }

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
    // 境内 DoH 污染出的国内 IP 会让 GEOIP,CN 误判直连，先按域名定性（仅 mihomo）。
    applyMihomoDnsPoisonOverrides(selectedRules);

    QVector<QString> rules;
    for (const TrafficRule &rule : selectedRules) {
        const Result<QString> rendered = rule.toMihomoRule();
        if (rendered.isErr()) {
            return Result<Json>::withError(rendered.error());
        }
        rules.append(rendered.value());
    }

    // 候选节点：当前节点排第一，其余来自界面按最新测速填进来的 urlTestNodes。
    // clash 要求代理名唯一，而不同订阅里同名节点很常见，所以撞名就加 #2/#3 后缀。
    QVector<Json> candidateProxies = {proxy};
    QSet<QString> usedNames;
    usedNames.insert(proxyName);
    if (options.urlTest) {
        const int limit = qMax<int>(options.urlTestSize, 2);
        for (const Json &candidate : options.urlTestNodes) {
            if (candidateProxies.size() >= limit) {
                break;
            }
            if (!candidate.is_object()) {
                continue;
            }
            Json object = candidate;
            const QString raw = JsonUtil::textOrEmpty(object, "name");
            if (raw.isEmpty()) {
                continue;
            }
            QString name = raw;
            int index = 2;
            while (usedNames.contains(name)) {
                name = QStringLiteral("%1 #%2").arg(raw).arg(index);
                index += 1;
            }
            usedNames.insert(name);
            object["name"] = name.toStdString();
            candidateProxies.append(object);
        }
    }

    QStringList candidateNames;
    for (const Json &item : candidateProxies) {
        const QString name = JsonUtil::textOrEmpty(item, "name");
        if (!name.isNull() && !name.isEmpty()) {
            candidateNames.append(name);
        }
    }
    const bool urlTestActive = candidateNames.size() > 1;
    if (options.urlTest && !urlTestActive) {
        Log::warn(QStringLiteral(
            "url-test 已开启但候选不足（只有当前节点），本次配置退回单节点 PROXY 组"));
    }

    const QString defaultTarget =
        options.routingMode == RoutingMode::GfwList || options.routingMode == RoutingMode::Direct
        ? QStringLiteral("DIRECT")
        : QStringLiteral("PROXY");
    const QString matchTarget = urlTestActive && defaultTarget == QLatin1String("PROXY")
        ? kFinalGroup
        : defaultTarget;

    // 内核下载 geo 库用的是它自己的规则引擎；不钉直连就会被下面的 MATCH 丢进代理节点，
    // 实测 10 秒 TLS 握手超时（同一 URL 本机直连 1.4 秒）。
    for (const QString &host : Geo::kMirrorDirectHosts) {
        rules.append(QStringLiteral("DOMAIN-SUFFIX,%1,DIRECT").arg(host));
    }
    rules.append(QStringLiteral("MATCH,%1").arg(matchTarget));

    Json ruleList = Json::array();
    for (const QString &rule : rules) {
        ruleList.push_back(rule.toStdString());
    }
    Json proxyList = Json::array();
    for (const Json &item : candidateProxies) {
        proxyList.push_back(item);
    }

    Json groups = Json::array();
    if (urlTestActive) {
        // PROXY 的第一个成员是「自动选择」：默认就让内核按延迟自己挑，界面上选中
        // 某个具体节点即等价于 PUT /proxies/PROXY，不需要重写配置。
        Json proxyMembers = Json::array();
        proxyMembers.push_back(kUrlTestGroup.toStdString());
        for (const QString &name : candidateNames) {
            proxyMembers.push_back(name.toStdString());
        }
        Json urlTestProxies = Json::array();
        for (const QString &name : candidateNames) {
            urlTestProxies.push_back(name.toStdString());
        }
        Json finalMembers = Json::array();
        finalMembers.push_back("PROXY");
        finalMembers.push_back(kUrlTestGroup.toStdString());
        finalMembers.push_back("DIRECT");
        finalMembers.push_back("REJECT");

        Json proxyGroup;
        proxyGroup["name"] = "PROXY";
        proxyGroup["type"] = "select";
        proxyGroup["proxies"] = proxyMembers;

        Json urlTestGroup;
        urlTestGroup["name"] = kUrlTestGroup.toStdString();
        urlTestGroup["type"] = "url-test";
        urlTestGroup["url"] = ProxyProbe::kProxyConnectivityTestUrl.toStdString();
        urlTestGroup["interval"] = kUrlTestIntervalSecs;
        urlTestGroup["tolerance"] = kUrlTestToleranceMs;
        urlTestGroup["timeout"] = kUrlTestTimeoutMs;
        urlTestGroup["max-failed"] = 3;
        urlTestGroup["lazy"] = true;
        urlTestGroup["proxies"] = urlTestProxies;

        Json finalGroup;
        finalGroup["name"] = kFinalGroup.toStdString();
        finalGroup["type"] = "select";
        finalGroup["proxies"] = finalMembers;

        groups.push_back(proxyGroup);
        groups.push_back(urlTestGroup);
        groups.push_back(finalGroup);
    } else {
        Json members = Json::array();
        for (const QString &name : candidateNames) {
            members.push_back(name.toStdString());
        }
        Json proxyGroup;
        proxyGroup["name"] = "PROXY";
        proxyGroup["type"] = "select";
        proxyGroup["proxies"] = members;
        groups.push_back(proxyGroup);
    }

    Json dns;
    dns["enable"] = options.dns.enabled;
    dns["ipv6"] = options.dns.ipv6;
    dns["enhanced-mode"] = dnsEnhancedModeToMihomoValue(options.dns.enhancedMode).toStdString();
    dns["fake-ip-range"] = "198.18.0.1/16";
    Json nameServers = Json::array();
    for (const QString &server : options.dns.nameservers) {
        nameServers.push_back(server.toStdString());
    }
    dns["nameserver"] = nameServers;
    // 节点自身域名用独立的 DNS 列表解析，避免经 ISP 明文查询暴露代理用途。
    if (!options.dns.proxyServerNameserver.isEmpty()) {
        Json proxyServers = Json::array();
        for (const QString &server : options.dns.proxyServerNameserver) {
            proxyServers.push_back(server.toStdString());
        }
        dns["proxy-server-nameserver"] = proxyServers;
    }

    Json config;
    config["mixed-port"] = options.mixedPort;
    config["external-controller"] =
        QStringLiteral("127.0.0.1:%1").arg(options.controllerPort).toStdString();
    config["secret"] = options.controllerSecret.toStdString();
    config["allow-lan"] = options.allowLan;
    config["mode"] = "rule";
    config["log-level"] = "info";
    config["proxies"] = proxyList;
    config["proxy-groups"] = groups;
    config["dns"] = dns;
    config["rules"] = ruleList;

    bool usesGeo = false;
    for (const TrafficRule &rule : selectedRules) {
        if (rule.usesGeo()) {
            usesGeo = true;
            break;
        }
    }
    if (usesGeo) {
        config["geodata-mode"] = true;
        // 关死内核侧自动更新：它只会直连 GitHub（实测 0.116 MB/s），在启动窗口内下不完就
        // 留下半截库 → `list cn not found` → 内核起不来。库改由 geo 模块负责：
        // 镜像优先 + 校验大小 + 先写 .part 再原子替换。
        config["geo-auto-update"] = false;
        config["geo-update-interval"] = 24;
        Json geoUrls;
        geoUrls["geoip"] =
            "https://gh-proxy.com/https://github.com/Loyalsoldier/v2ray-rules-dat/releases/"
            "latest/download/geoip.dat";
        geoUrls["geosite"] =
            "https://gh-proxy.com/https://github.com/Loyalsoldier/v2ray-rules-dat/releases/"
            "latest/download/geosite.dat";
        config["geox-url"] = geoUrls;
    }

    if (options.mode == ProxyMode::Tun) {
        Json tun;
        tun["enable"] = true;
        tun["stack"] = tunStackToMihomoValue(options.tun.stack).toStdString();
        Json hijack = Json::array();
        for (const QString &entry : options.tun.dnsHijack) {
            hijack.push_back(entry.toStdString());
        }
        tun["dns-hijack"] = hijack;
        tun["auto-route"] = options.tun.autoRoute;
        tun["auto-detect-interface"] = options.tun.autoDetectInterface;
        config["tun"] = tun;
    }

    return Result<Json>::withValue(config);
}

Result<QString> MihomoConfigOptions::encodeHandoffConfig(const MihomoConfigOptions &options)
{
    const Result<QString> json = options.toSavedJsonText();
    if (json.isErr()) {
        return Result<QString>::withError(json.error());
    }
    QByteArray packed = json.value().toUtf8().toBase64();
    packed.replace('+', "-");
    packed.replace('/', "_");
    while (packed.endsWith('=')) {
        packed.chop(1);
    }
    return Result<QString>::withValue(QString::fromLatin1(packed));
}

Result<MihomoConfigOptions> MihomoConfigOptions::decodeHandoffConfig(const QString &encoded)
{
    QByteArray packed = encoded.toLatin1();
    packed.replace('-', "+");
    packed.replace('_', "/");
    while (packed.size() % 4 != 0) {
        packed.append('=');
    }
    const QByteArray json = QByteArray::fromBase64(packed);
    if (json.isEmpty() && !encoded.isEmpty()) {
        return Result<MihomoConfigOptions>::withError(QStringLiteral("invalid base64url payload"));
    }
    return MihomoConfigOptions::fromSavedJsonText(QString::fromUtf8(json));
}

MihomoConfigOptions configForUi(const MihomoConfigOptions &options)
{
    MihomoConfigOptions normalized = options;
    normalized.urlTest = false;
    return normalized;
}

}
