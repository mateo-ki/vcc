#include "proxy/XrayConfig.h"

#include <QString>
#include <QStringList>

#include <optional>

#include "proxy/KernelConfigMessages.h"

namespace Proxy {

namespace {

// toAsciiLower / findChild / rustLines 与 SingBoxConfig.cpp 里的同名私有工具是同一份逻辑。
// 两个内核文件在 Rust 里也各自维护自己的小助手（string_value vs string 的语义就不一样），
// 所以这里照搬：不抽公共头，免得一处改动同时改掉两个内核的输出。

/// Rust 的 `to_ascii_lowercase` 只动 A-Z。
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

/// nlohmann 的 `find()` 对非对象返回 end()，所以不必先判 is_object()。
Json findChild(const Json &value, const QString &key)
{
    const auto found = value.find(key.toUtf8().constData());
    return found == value.end() ? Json() : *found;
}

/// Rust 的 `str::lines()`：按 \n 切，行尾紧跟的 \r 去掉，结尾的换行不产生额外空行。
/// Xray 取的是"最后一行"（可能是空串），和 sing-box 的"最后一行非空"不同。
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

using OptionalString = std::optional<QString>;

/// Rust: string —— 与 sing-box 那版不同：**不 trim、空串也算 Some("")**。
/// 所以 "flow": "" 会保留成 Some("")，而缺键才是 None。
OptionalString xrayString(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (value.is_string()) {
            return QString::fromStdString(value.get<std::string>());
        }
    }
    return std::nullopt;
}

/// Rust: required
Result<QString> xrayRequired(const Json &config, const QStringList &keys)
{
    const OptionalString value = xrayString(config, keys);
    if (!value.has_value()) {
        return Result<QString>::withError(
            Messages::fill(Messages::xrayMissingField(), {{"{}", keys.join(QLatin1Char('/'))}}));
    }
    return Result<QString>::withValue(*value);
}

/// Rust: number —— 只认 as_u64()，不做字符串兜底（与 sing-box 的 number_value 不同）。
std::optional<qint64> xrayNumber(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (value.is_number_unsigned()) {
            return value.get<qint64>();
        }
    }
    return std::nullopt;
}

/// Rust: bool_value —— 只认 as_bool()。两个 Rust 文件里它都没被调用到，一并移植保持 1:1。
bool xrayBool(const Json &config, const QStringList &keys)
{
    for (const QString &key : keys) {
        const Json value = findChild(config, key);
        if (value.is_boolean() && value.get<bool>()) {
            return true;
        }
    }
    return false;
}

/// Rust: server_with_users —— http/https/socks 服务器；只有给了用户名或密码时才写 users。
Json serverWithUsers(const Models::Node &node, const Json &config)
{
    Json server;
    server["address"] = node.server.toStdString();
    server["port"] = node.port;
    const OptionalString user = xrayString(config, {QStringLiteral("username"), QStringLiteral("user")});
    const OptionalString pass =
        xrayString(config, {QStringLiteral("password"), QStringLiteral("pass")});
    if (user.has_value() || pass.has_value()) {
        Json entry;
        entry["user"] = user.value_or(QString()).toStdString();
        entry["pass"] = pass.value_or(QString()).toStdString();
        Json users = Json::array();
        users.push_back(entry);
        server["users"] = users;
    }
    return server;
}

Json addressAndPort(const Models::Node &node)
{
    Json entry;
    entry["address"] = node.server.toStdString();
    entry["port"] = node.port;
    return entry;
}

Json buildStreamSettings(const Json &config);

/// Rust: build_outbound
Result<Json> buildOutbound(const Models::Node &node)
{
    const Json config = node.config.is_object() ? node.config : Json::object();
    const QString protocol = toAsciiLower(node.protocol);
    Json settings;
    QString xrayProtocol;

    if (protocol == QLatin1String("ss") || protocol == QLatin1String("shadowsocks")) {
        const Result<QString> method =
            xrayRequired(config, {QStringLiteral("cipher"), QStringLiteral("method")});
        if (method.isErr()) {
            return Result<Json>::withError(method.error());
        }
        const Result<QString> password = xrayRequired(config, {QStringLiteral("password")});
        if (password.isErr()) {
            return Result<Json>::withError(password.error());
        }
        Json server = addressAndPort(node);
        server["method"] = method.value().toStdString();
        server["password"] = password.value().toStdString();
        settings = Json::object();
        settings["servers"] = Json::array({server});
        xrayProtocol = QStringLiteral("shadowsocks");
    } else if (protocol == QLatin1String("vmess")) {
        const Result<QString> id = xrayRequired(config, {QStringLiteral("uuid"), QStringLiteral("id")});
        if (id.isErr()) {
            return Result<Json>::withError(id.error());
        }
        Json user;
        user["id"] = id.value().toStdString();
        user["alterId"] = xrayNumber(config, {QStringLiteral("alterId"), QStringLiteral("alter-id")})
                              .value_or(qint64(0));
        user["security"] =
            xrayString(config, {QStringLiteral("cipher"), QStringLiteral("security")})
                .value_or(QStringLiteral("auto"))
                .toStdString();
        Json vnext = addressAndPort(node);
        Json users = Json::array();
        users.push_back(user);
        vnext["users"] = users;
        settings = Json::object();
        settings["vnext"] = Json::array({vnext});
        xrayProtocol = protocol;
    } else if (protocol == QLatin1String("vless")) {
        const Result<QString> id = xrayRequired(config, {QStringLiteral("uuid"), QStringLiteral("id")});
        if (id.isErr()) {
            return Result<Json>::withError(id.error());
        }
        Json user;
        user["id"] = id.value().toStdString();
        user["encryption"] = "none";
        user["flow"] = xrayString(config, {QStringLiteral("flow")})
                           .value_or(QStringLiteral(""))
                           .toStdString();
        Json vnext = addressAndPort(node);
        Json users = Json::array();
        users.push_back(user);
        vnext["users"] = users;
        settings = Json::object();
        settings["vnext"] = Json::array({vnext});
        xrayProtocol = protocol;
    } else if (protocol == QLatin1String("trojan")) {
        const Result<QString> password = xrayRequired(config, {QStringLiteral("password")});
        if (password.isErr()) {
            return Result<Json>::withError(password.error());
        }
        Json server = addressAndPort(node);
        server["password"] = password.value().toStdString();
        settings = Json::object();
        settings["servers"] = Json::array({server});
        xrayProtocol = protocol;
    } else if (protocol == QLatin1String("http") || protocol == QLatin1String("https")
               || protocol == QLatin1String("socks") || protocol == QLatin1String("socks5")) {
        settings = Json::object();
        settings["servers"] = Json::array({serverWithUsers(node, config)});
        xrayProtocol = protocol == QLatin1String("socks5")
            ? QStringLiteral("socks")
            : (protocol == QLatin1String("https") ? QStringLiteral("http") : protocol);
    } else {
        return Result<Json>::withError(
            Messages::fill(Messages::xrayProtocolUnsupported(), {{"{}", node.protocol}}));
    }

    Json outbound;
    outbound["tag"] = "proxy";
    outbound["protocol"] = xrayProtocol.toStdString();
    outbound["settings"] = settings;
    if (protocol == QLatin1String("vmess") || protocol == QLatin1String("vless")
        || protocol == QLatin1String("trojan")) {
        outbound["streamSettings"] = buildStreamSettings(config);
    }
    return Result<Json>::withValue(outbound);
}

/// Rust: build_stream_settings —— 只认 tls/reality，其它 security 一律 "none"。
Json buildStreamSettings(const Json &config)
{
    const QString network =
        xrayString(config, {QStringLiteral("network"), QStringLiteral("type")})
            .value_or(QStringLiteral("tcp"));
    const OptionalString declaredSecurity = xrayString(config, {QStringLiteral("security")});
    QString security = QStringLiteral("none");
    if (declaredSecurity.has_value()
        && (*declaredSecurity == QLatin1String("tls") || *declaredSecurity == QLatin1String("reality"))) {
        security = *declaredSecurity;
    }

    Json stream;
    stream["network"] = network.toStdString();
    stream["security"] = security.toStdString();
    if (network == QLatin1String("ws")) {
        const OptionalString host = xrayString(config, {QStringLiteral("host")});
        Json headers = Json::object();
        if (host.has_value()) {
            headers["Host"] = host->toStdString();
        }
        Json ws;
        ws["path"] = xrayString(config, {QStringLiteral("path")})
                         .value_or(QStringLiteral("/"))
                         .toStdString();
        ws["headers"] = headers;
        stream["wsSettings"] = ws;
    } else if (network == QLatin1String("grpc")) {
        Json grpc;
        grpc["serviceName"] =
            xrayString(config, {QStringLiteral("serviceName"), QStringLiteral("service-name")})
                .value_or(QStringLiteral(""))
                .toStdString();
        stream["grpcSettings"] = grpc;
    }

    if (security == QLatin1String("tls")) {
        Json tls;
        tls["serverName"] =
            xrayString(config, {QStringLiteral("sni"), QStringLiteral("servername"),
                                QStringLiteral("server-name")})
                .value_or(QStringLiteral(""))
                .toStdString();
        // 同 sing-box：不采信远端订阅里的关闭证书校验标记。
        tls["allowInsecure"] = false;
        stream["tlsSettings"] = tls;
    } else if (security == QLatin1String("reality")) {
        Json reality;
        reality["serverName"] =
            xrayString(config, {QStringLiteral("sni"), QStringLiteral("servername"),
                                QStringLiteral("server-name")})
                .value_or(QStringLiteral(""))
                .toStdString();
        reality["publicKey"] =
            xrayString(config, {QStringLiteral("public-key"), QStringLiteral("publicKey"),
                                QStringLiteral("pbk")})
                .value_or(QStringLiteral(""))
                .toStdString();
        reality["shortId"] =
            xrayString(config, {QStringLiteral("short-id"), QStringLiteral("shortId"),
                                QStringLiteral("sid")})
                .value_or(QStringLiteral(""))
                .toStdString();
        reality["fingerprint"] =
            xrayString(config, {QStringLiteral("client-fingerprint"), QStringLiteral("fingerprint"),
                                QStringLiteral("fp")})
                .value_or(QStringLiteral("chrome"))
                .toStdString();
        stream["realitySettings"] = reality;
    }
    return stream;
}

/// Rust: build_routing_rules。Xray 的 field 规则没有进程名字段，只能报错：
/// 丢掉一条 PROCESS-NAME 规则等于那个程序重新走代理/直连，用户看不出来。
Result<Json> buildRoutingRules(const MihomoConfigOptions &options)
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

    Json rules = Json::array();
    for (const TrafficRule &rule : selectedRules) {
        QString outbound;
        switch (rule.action) {
        case RuleAction::Direct:
            outbound = QStringLiteral("direct");
            break;
        case RuleAction::Proxy:
            outbound = QStringLiteral("proxy");
            break;
        case RuleAction::Reject:
            outbound = QStringLiteral("blocked");
            break;
        }
        Json value;
        value["type"] = "field";
        value["outboundTag"] = outbound.toStdString();
        switch (rule.matcher.kind) {
        case RuleMatcher::Kind::Domain:
            value["domain"] =
                Json::array({QStringLiteral("full:%1").arg(rule.matcher.value).toStdString()});
            break;
        case RuleMatcher::Kind::DomainSuffix:
            value["domain"] =
                Json::array({QStringLiteral("domain:%1").arg(rule.matcher.value).toStdString()});
            break;
        case RuleMatcher::Kind::GeoSite:
            // 不折叠大小写：Rust 直接 format!("geosite:{category}")，dat 文件自己按原名查。
            value["domain"] =
                Json::array({QStringLiteral("geosite:%1").arg(rule.matcher.value).toStdString()});
            break;
        case RuleMatcher::Kind::GeoIp:
            value["ip"] = Json::array(
                {QStringLiteral("geoip:%1")
                     .arg(toAsciiLower(rule.matcher.value))
                     .toStdString()});
            break;
        case RuleMatcher::Kind::IpCidr:
            value["ip"] = Json::array({rule.matcher.value.toStdString()});
            break;
        case RuleMatcher::Kind::DestinationPort: {
            bool ok = false;
            const unsigned long long parsed = rule.matcher.value.toULongLong(&ok);
            if (!ok || parsed > 65535ULL) {
                return Result<Json>::withError(
                    Messages::fill(Messages::xrayPortRuleInvalid(), {{"{port}", rule.matcher.value}}));
            }
            // Xray 这里写的是字符串（Rust 传的是 &str，只借 parse::<u16> 做校验）。
            value["port"] = rule.matcher.value.toStdString();
            break;
        }
        case RuleMatcher::Kind::ProcessName:
            return Result<Json>::withError(
                Messages::fill(Messages::xrayProcessNameUnsupported(),
                               {{"{name}", rule.matcher.value}}));
        case RuleMatcher::Kind::Network:
            value["network"] = rule.matcher.network == NetworkProtocol::Tcp ? "tcp" : "udp";
            break;
        case RuleMatcher::Kind::Quic:
            value["network"] = "udp";
            value["port"] = "443";
            break;
        }
        rules.push_back(value);
    }

    // 兜底：Xray 的 routing 没有 final 概念，用一条只看入站 tag 的规则当 MATCH。
    Json fallback;
    fallback["type"] = "field";
    fallback["inboundTag"] = Json::array({std::string("local-http")});
    fallback["outboundTag"] =
        (options.routingMode == RoutingMode::GfwList || options.routingMode == RoutingMode::Direct)
        ? "direct"
        : "proxy";
    rules.push_back(fallback);

    return Result<Json>::withValue(rules);
}

MihomoConfigOptions batchOptions(quint16 mixedPort)
{
    MihomoConfigOptions options;
    options.mixedPort = mixedPort;
    return options;
}

Json emptyObject()
{
    return Json::object();
}

}

qsizetype xrayBatchCount(qsizetype nodeCount)
{
    return nodeCount <= 0 ? 0 : (nodeCount + kXrayBatchSize - 1) / kXrayBatchSize;
}

QString lastXrayLogLine(const QString &text)
{
    const QStringList lines = rustLines(text);
    if (lines.isEmpty()) {
        return Messages::xrayUnknownError();
    }
    return lines.last();
}

Result<Json> buildXrayConfig(const Models::Node &node, MihomoConfigOptions options)
{
    const Status valid = options.validate();
    if (valid.isErr()) {
        return Result<Json>::withError(valid.error());
    }
    if (options.mode == ProxyMode::Tun) {
        return Result<Json>::withError(Messages::xrayTunUnsupported());
    }

    const Result<Json> outbound = buildOutbound(node);
    if (outbound.isErr()) {
        return Result<Json>::withError(outbound.error());
    }

    QStringList dnsServers;
    if (options.dns.enabled) {
        dnsServers = options.dns.nameservers;
    } else {
        dnsServers = {QStringLiteral("localhost")};
    }

    const Result<Json> routingRules = buildRoutingRules(options);
    if (routingRules.isErr()) {
        return Result<Json>::withError(routingRules.error());
    }

    Json log;
    log["loglevel"] = "warning";

    Json dns;
    Json servers = Json::array();
    for (const QString &server : dnsServers) {
        servers.push_back(server.toStdString());
    }
    dns["servers"] = servers;
    dns["queryStrategy"] = options.dns.ipv6 ? "UseIP" : "UseIPv4";

    Json inbound;
    inbound["tag"] = "local-http";
    inbound["listen"] = "127.0.0.1";
    inbound["port"] = options.mixedPort;
    inbound["protocol"] = "http";
    Json inboundSettings;
    inboundSettings["allowTransparent"] = false;
    inbound["settings"] = inboundSettings;
    Json inbounds = Json::array();
    inbounds.push_back(inbound);

    Json direct;
    direct["tag"] = "direct";
    direct["protocol"] = "freedom";
    direct["settings"] = emptyObject();
    Json blocked;
    blocked["tag"] = "blocked";
    blocked["protocol"] = "blackhole";
    blocked["settings"] = emptyObject();
    Json outbounds = Json::array();
    outbounds.push_back(outbound.value());
    outbounds.push_back(direct);
    outbounds.push_back(blocked);

    Json routing;
    routing["domainStrategy"] = "AsIs";
    routing["rules"] = routingRules.value();

    Json config;
    config["log"] = log;
    config["dns"] = dns;
    config["inbounds"] = inbounds;
    config["outbounds"] = outbounds;
    config["routing"] = routing;
    return Result<Json>::withValue(config);
}

Result<XrayBatchProbePlan> buildXrayBatchProbePlan(const QVector<Models::Node> &nodes,
                                                   const QVector<quint16> &ports)
{
    if (nodes.size() != ports.size()) {
        // Rust 侧靠索引越界 panic 暴露这个调用方错误；C++ 里越界是 UB，必须显式失败。
        return Result<XrayBatchProbePlan>::withError(
            QStringLiteral("buildXrayBatchProbePlan: ports.size() != nodes.size()"));
    }

    XrayBatchProbePlan plan;
    plan.failures.resize(nodes.size());
    Json inbounds = Json::array();
    Json outbounds = Json::array();
    Json rules = Json::array();

    for (qsizetype index = 0; index < nodes.size(); ++index) {
        const Models::Node &node = nodes.at(index);
        const Result<Json> config = buildXrayConfig(node, batchOptions(ports.at(index)));
        if (config.isErr()) {
            plan.failures[index] = Messages::fill(Messages::xrayConfigUnsupported(),
                                                  {{"{error}", config.error()}});
            continue;
        }

        XrayBatchProbeEntry entry;
        entry.nodeIndex = index;
        entry.port = ports.at(index);
        entry.inboundTag = QStringLiteral("probe-in-%1").arg(index);
        entry.outboundTag = QStringLiteral("probe-out-%1").arg(index);

        Json inbound = config.value().at("inbounds").at(0);
        inbound["tag"] = entry.inboundTag.toStdString();
        Json proxy = config.value().at("outbounds").at(0);
        proxy["tag"] = entry.outboundTag.toStdString();

        inbounds.push_back(inbound);
        outbounds.push_back(proxy);

        Json rule;
        rule["type"] = "field";
        rule["inboundTag"] = Json::array({entry.inboundTag.toStdString()});
        rule["outboundTag"] = entry.outboundTag.toStdString();
        rules.push_back(rule);

        plan.entries.append(entry);
    }

    // Rust: `if inbounds.is_empty() { return Ok(immediate) }` —— 一个都没建成就不起进程。
    if (inbounds.empty()) {
        return Result<XrayBatchProbePlan>::withValue(plan);
    }
    plan.hasInbounds = true;

    const Json &readyInbound = inbounds.at(0);
    const Json listenPort = findChild(readyInbound, QStringLiteral("port"));
    plan.readyPort = listenPort.is_number_integer() ? quint16(listenPort.get<qint64>())
                                                    : ports.value(0);

    // 批量配置里没有 dns / blocked 出口，只有 direct：与 Rust 的字面量一致。
    Json direct;
    direct["tag"] = "direct";
    direct["protocol"] = "freedom";
    direct["settings"] = emptyObject();
    outbounds.push_back(direct);

    Json log;
    log["loglevel"] = "warning";
    Json routing;
    routing["domainStrategy"] = "AsIs";
    routing["rules"] = rules;

    Json config;
    config["log"] = log;
    config["inbounds"] = inbounds;
    config["outbounds"] = outbounds;
    config["routing"] = routing;
    plan.config = config;
    return Result<XrayBatchProbePlan>::withValue(plan);
}

}
