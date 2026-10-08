#include "export/ClashProxy.h"

#include <QString>
#include <QStringList>
#include <QSet>

#include <cstdint>
#include <initializer_list>
#include <optional>

namespace Export {
namespace {

using ProxyObject = Json::object_t;

/// Rust 的 String::to_ascii_lowercase：只折 ASCII 大写，非 ASCII 原样保留。
/// 协议名、tls 值、security 值都走这条路径，不能用 toLower()（会动非 ASCII 字符）。
QString toAsciiLower(const QString &text)
{
    QString out = text;
    for (QChar &ch : out) {
        if (ch.unicode() >= u'A' && ch.unicode() <= u'Z') {
            ch = QChar(static_cast<ushort>(ch.unicode() + 32));
        }
    }
    return out;
}

/// Rust 的 str::trim（按 Unicode White_Space），Qt 的 trimmed 覆盖同一批字符。
QString trimRs(const QString &text)
{
    return text.trimmed();
}

Json jsonOf(const QString &text)
{
    return Json(text.toStdString());
}

Json jsonOfList(const QStringList &items)
{
    Json array = Json::array();
    for (const QString &item : items) {
        array.push_back(item.toStdString());
    }
    return array;
}

/// Rust 的 `s.parse::<i64>()`：只接受 `[+-]?数字`，不跳空白，溢出算失败。
std::optional<qint64> parseI64Strict(const QString &text)
{
    qsizetype pos = 0;
    if (!text.isEmpty() && (text.at(0) == QLatin1Char('-') || text.at(0) == QLatin1Char('+'))) {
        pos = 1;
    }
    if (pos >= text.size()) {
        return std::nullopt;
    }
    for (; pos < text.size(); ++pos) {
        const char16_t code = text.at(pos).unicode();
        if (code < u'0' || code > u'9') {
            return std::nullopt;
        }
    }
    bool ok = false;
    const qint64 value = text.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

/// Rust 的 `s.parse::<u16>()`，规则同上，只接受负号以外的范围。
std::optional<quint16> parseU16Strict(const QString &text)
{
    const std::optional<qint64> value = parseI64Strict(text);
    if (!value.has_value() || *value < 0 || *value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<quint16>(*value);
}

/// Rust 的 as_u16：serde_json 的 as_u64 只认非负整数（负数和 443.0 这样的浮点都不算），
/// 字符串才走 parse::<u16>。
std::optional<quint16> asU16(const Json &value)
{
    if (value.is_number_unsigned()) {
        const std::uint64_t raw = value.get<std::uint64_t>();
        if (raw > 0xFFFFULL) {
            return std::nullopt;
        }
        return static_cast<quint16>(raw);
    }
    if (value.is_string()) {
        return parseU16Strict(QString::fromStdString(value.get<std::string>()));
    }
    return std::nullopt;
}

const Json *peek(const ProxyObject &obj, const char *key)
{
    const auto found = obj.find(std::string(key));
    return found == obj.end() ? nullptr : &found->second;
}

bool holds(const ProxyObject &obj, const char *key)
{
    return obj.find(std::string(key)) != obj.end();
}

void put(ProxyObject &obj, const char *key, const Json &value)
{
    obj.insert_or_assign(std::string(key), value);
}

void drop(ProxyObject &obj, const char *key)
{
    obj.erase(std::string(key));
}

/// 对应 export.rs 的 first_str。注意与 UriExport 里的 str_field 不同：
/// 空串也算命中（Some("")），只有 null/数组/对象才继续找下一个键。
/// 调用方一律用 isNull() 区分 Rust 的 Option，不能用 isEmpty()。
QString firstStr(const ProxyObject &obj, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const Json *value = peek(obj, key);
        if (value == nullptr) {
            continue;
        }
        if (value->is_string()) {
            return QString::fromStdString(value->get<std::string>());
        }
        if (value->is_number()) {
            return QString::fromStdString(value->dump());
        }
        if (value->is_boolean()) {
            return value->get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
        }
    }
    return QString();
}

/// Rust 的 coerce_numberish：字符串能整型化就转成 JSON 整数，否则原样返回。
Json coerceNumberish(const Json &value)
{
    if (value.is_string()) {
        const std::optional<qint64> parsed =
            parseI64Strict(QString::fromStdString(value.get<std::string>()));
        if (parsed.has_value()) {
            return Json(*parsed);
        }
    }
    return value;
}

/// Rust 的 strip_empty_strings：只有"值是字符串且为空"才删，null/数字/数组都留着。
void stripEmptyStrings(ProxyObject &obj, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const Json *value = peek(obj, key);
        if (value != nullptr && value->is_string() && value->get_ref<const std::string &>().empty()) {
            drop(obj, key);
        }
    }
}

void setType(Json &cfg, const char *type)
{
    if (ProxyObject *obj = cfg.get_ptr<ProxyObject *>()) {
        put(*obj, "type", jsonOf(QString::fromUtf8(type)));
    }
}

void ensureServerPortName(Json &cfg, const Models::Node &node)
{
    ProxyObject *obj = cfg.get_ptr<ProxyObject *>();
    if (obj == nullptr) {
        return;
    }
    const Json *server = peek(*obj, "server");
    if (server == nullptr || !server->is_string() || server->get_ref<const std::string &>().empty()) {
        put(*obj, "server", jsonOf(node.server));
    }
    const Json *port = peek(*obj, "port");
    if (port == nullptr || !asU16(*port).has_value()) {
        put(*obj, "port", Json(node.port));
    }
    const Json *name = peek(*obj, "name");
    if (name == nullptr || !name->is_string() || name->get_ref<const std::string &>().empty()) {
        const QString fallback =
            node.name.isEmpty() ? QStringLiteral("%1:%2").arg(node.server, QString::number(node.port))
                                : node.name;
        put(*obj, "name", jsonOf(fallback));
    }
}

/// Rust 的 split(',').map(trim).filter(!empty)：空串在 trim 之后才过滤。
QStringList splitCommaList(const QString &text)
{
    QStringList out;
    qsizetype start = 0;
    while (true) {
        const qsizetype comma = text.indexOf(u',', start);
        const QString part =
            trimRs(comma < 0 ? text.mid(start) : text.mid(start, comma - start));
        if (!part.isEmpty()) {
            out.append(part);
        }
        if (comma < 0) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

/// alpn 是字符串时拆成数组；拆空则删键。对应 vmess/vless/trojan 里重复出现的那段。
void normalizeAlpnString(ProxyObject &obj)
{
    const Json *value = peek(obj, "alpn");
    if (value == nullptr || !value->is_string()) {
        return;
    }
    const QStringList list = splitCommaList(QString::fromStdString(value->get<std::string>()));
    if (list.isEmpty()) {
        drop(obj, "alpn");
    } else {
        put(obj, "alpn", jsonOfList(list));
    }
}

/// websocket/grpc 的 opts 生成，三个协议共用同一段字面逻辑（Rust 里是三份拷贝）。
void fillWsOpts(ProxyObject &obj, const QString &path, const QString &host)
{
    if (holds(obj, "ws-opts")) {
        return;
    }
    ProxyObject headers;
    if (!host.isEmpty()) {
        headers.insert_or_assign(std::string("Host"), jsonOf(host));
    }
    ProxyObject ws;
    if (!path.isEmpty()) {
        ws.insert_or_assign(std::string("path"), jsonOf(path));
    }
    if (!headers.empty()) {
        ws.insert_or_assign(std::string("headers"), Json(std::move(headers)));
    }
    if (!ws.empty()) {
        put(obj, "ws-opts", Json(std::move(ws)));
    }
}

void fillGrpcOpts(ProxyObject &obj, const QString &path)
{
    if (holds(obj, "grpc-opts")) {
        return;
    }
    QString serviceName = firstStr(obj, {"serviceName", "servicename"});
    if (serviceName.isNull()) {
        serviceName = path;
    }
    if (!serviceName.isEmpty()) {
        Json grpc = Json::object();
        grpc["grpc-service-name"] = serviceName.toStdString();
        put(obj, "grpc-opts", grpc);
    }
}

void normalizeSs(Json &cfg, const Models::Node &node)
{
    setType(cfg, "ss");
    ensureServerPortName(cfg, node);
    ProxyObject *obj = cfg.get_ptr<ProxyObject *>();
    if (obj == nullptr) {
        return;
    }
    if (!holds(*obj, "cipher")) {
        if (const Json *method = peek(*obj, "method")) {
            put(*obj, "cipher", *method);
        }
    }
    drop(*obj, "method");
    stripEmptyStrings(*obj, {"plugin", "plugin-opts"});
}

void normalizeVmess(Json &cfg, const Models::Node &node)
{
    setType(cfg, "vmess");
    ensureServerPortName(cfg, node);
    ProxyObject *obj = cfg.get_ptr<ProxyObject *>();
    if (obj == nullptr) {
        return;
    }

    if (!holds(*obj, "uuid")) {
        if (const Json *id = peek(*obj, "id")) {
            put(*obj, "uuid", *id);
        }
    }
    if (!holds(*obj, "alterId")) {
        if (const Json *aid = peek(*obj, "aid")) {
            put(*obj, "alterId", coerceNumberish(*aid));
        } else {
            put(*obj, "alterId", Json(0));
        }
    } else {
        put(*obj, "alterId", coerceNumberish(*peek(*obj, "alterId")));
    }
    if (!holds(*obj, "cipher")) {
        if (const Json *scy = peek(*obj, "scy")) {
            put(*obj, "cipher", *scy);
        } else {
            put(*obj, "cipher", jsonOf(QStringLiteral("auto")));
        }
    }

    QString network = firstStr(*obj, {"network", "net"});
    if (network.isNull()) {
        network = QStringLiteral("tcp");
    }
    put(*obj, "network", jsonOf(network));

    bool tls = false;
    if (const Json *value = peek(*obj, "tls")) {
        if (value->is_boolean()) {
            tls = value->get<bool>();
        } else if (value->is_string()) {
            const QString text = toAsciiLower(QString::fromStdString(value->get<std::string>()));
            tls = text == QLatin1String("tls") || text == QLatin1String("true")
                  || text == QLatin1String("1");
        }
    }
    put(*obj, "tls", Json(tls));

    if (const QString sni = firstStr(*obj, {"servername", "sni"}); !sni.isNull() && !sni.isEmpty()) {
        put(*obj, "servername", jsonOf(sni));
    }

    const QString path = firstStr(*obj, {"path"});
    const QString host = firstStr(*obj, {"host"});
    const QString alpn = firstStr(*obj, {"alpn"});

    if (network == QLatin1String("ws") || network == QLatin1String("websocket")) {
        put(*obj, "network", jsonOf(QStringLiteral("ws")));
        fillWsOpts(*obj, path, host);
    } else if (network == QLatin1String("grpc")) {
        fillGrpcOpts(*obj, path);
    } else if (network == QLatin1String("h2") || network == QLatin1String("http")) {
        if (!holds(*obj, "h2-opts") && (!path.isEmpty() || !host.isEmpty())) {
            ProxyObject h2;
            if (!path.isEmpty()) {
                h2.insert_or_assign(std::string("path"), jsonOf(path));
            }
            if (!host.isEmpty()) {
                h2.insert_or_assign(std::string("host"), jsonOfList({host}));
            }
            put(*obj, "h2-opts", Json(std::move(h2)));
        }
    }

    const Json *alpnValue = peek(*obj, "alpn");
    if (alpnValue != nullptr && alpnValue->is_string()) {
        const QStringList list =
            splitCommaList(QString::fromStdString(alpnValue->get<std::string>()));
        if (list.isEmpty()) {
            drop(*obj, "alpn");
        } else {
            put(*obj, "alpn", jsonOfList(list));
        }
    } else if (!alpn.isEmpty()) {
        const QStringList list = splitCommaList(alpn);
        if (!list.isEmpty()) {
            put(*obj, "alpn", jsonOfList(list));
        }
    }

    for (const char *key : {"add", "id", "aid", "net", "scy", "ps", "v", "remark", "server_port",
                            "class"}) {
        drop(*obj, key);
    }
    stripEmptyStrings(*obj, {"path", "host", "sni", "servername", "fp"});
}

void normalizeVless(Json &cfg, const Models::Node &node)
{
    setType(cfg, "vless");
    ensureServerPortName(cfg, node);
    ProxyObject *obj = cfg.get_ptr<ProxyObject *>();
    if (obj == nullptr) {
        return;
    }

    if (!holds(*obj, "uuid")) {
        if (const Json *id = peek(*obj, "id")) {
            put(*obj, "uuid", *id);
        }
    }

    QString network = firstStr(*obj, {"network", "net"});
    if (network.isNull()) {
        network = QStringLiteral("tcp");
    }
    put(*obj, "network", jsonOf(network));

    // security 优先，其次 tls；注意 firstStr 会把空串也算命中，于是 tls 键不再被读。
    const QString security = firstStr(*obj, {"security", "tls"});
    const QString secLower = toAsciiLower(security.isNull() ? QString() : security);
    const bool isReality = secLower == QLatin1String("reality");
    const bool isTls = isReality || secLower == QLatin1String("tls")
                       || secLower == QLatin1String("true") || secLower == QLatin1String("1");
    put(*obj, "tls", Json(isTls));
    if (isReality) {
        ProxyObject ro;
        const QString pbk = firstStr(*obj, {"pbk", "public-key"});
        if (!pbk.isNull() && !pbk.isEmpty()) {
            put(ro, "public-key", jsonOf(pbk));
        }
        const QString sid = firstStr(*obj, {"sid", "short-id"});
        if (!sid.isNull() && !sid.isEmpty()) {
            put(ro, "short-id", jsonOf(sid));
        }
        put(*obj, "reality-opts", Json(std::move(ro)));
    }

    if (const QString sni = firstStr(*obj, {"servername", "sni"}); !sni.isNull() && !sni.isEmpty()) {
        put(*obj, "servername", jsonOf(sni));
    }
    if (const QString fp = firstStr(*obj, {"fp", "client-fingerprint"});
        !fp.isNull() && !fp.isEmpty()) {
        put(*obj, "client-fingerprint", jsonOf(fp));
    }
    if (const QString flow = firstStr(*obj, {"flow"}); !flow.isNull() && !flow.isEmpty()) {
        put(*obj, "flow", jsonOf(flow));
    } else {
        drop(*obj, "flow");
    }

    const QString path = firstStr(*obj, {"path"});
    const QString host = firstStr(*obj, {"host"});

    if (network == QLatin1String("ws") || network == QLatin1String("websocket")) {
        put(*obj, "network", jsonOf(QStringLiteral("ws")));
        fillWsOpts(*obj, path, host);
    } else if (network == QLatin1String("grpc")) {
        fillGrpcOpts(*obj, path);
    }

    normalizeAlpnString(*obj);

    // 节点数据来自远端订阅/gist：允许它关掉证书校验就等于让来源自己决定要不要
    // 被 MITM。实测 19158 个节点里没有一个用到这些标记，所以一律强制关闭。
    drop(*obj, "allowInsecure");
    drop(*obj, "insecure");
    put(*obj, "skip-cert-verify", Json(false));

    for (const char *key : {"security", "encryption", "pbk", "sid", "fp", "sni", "serviceName",
                            "allowInsecure", "insecure", "id", "net", "add"}) {
        drop(*obj, key);
    }
    if (holds(*obj, "ws-opts") || holds(*obj, "grpc-opts")) {
        drop(*obj, "path");
        drop(*obj, "host");
    } else {
        stripEmptyStrings(*obj, {"path", "host"});
    }
    stripEmptyStrings(*obj, {"client-fingerprint", "servername", "flow"});
}

void normalizeTrojan(Json &cfg, const Models::Node &node)
{
    setType(cfg, "trojan");
    ensureServerPortName(cfg, node);
    ProxyObject *obj = cfg.get_ptr<ProxyObject *>();
    if (obj == nullptr) {
        return;
    }

    QString network = firstStr(*obj, {"network", "net"});
    if (network.isNull()) {
        network = QStringLiteral("tcp");
    }
    if (network == QLatin1String("tcp")) {
        const Json *declared = peek(*obj, "network");
        const bool declaredTcp = declared != nullptr && declared->is_string()
                                && QString::fromStdString(declared->get<std::string>())
                                       == QLatin1String("tcp");
        if (declaredTcp && !holds(*obj, "ws-opts") && !holds(*obj, "grpc-opts")) {
            drop(*obj, "network");
        }
    } else {
        put(*obj, "network", jsonOf(network));
    }

    if (const QString sni = firstStr(*obj, {"sni", "servername"}); !sni.isNull() && !sni.isEmpty()) {
        put(*obj, "sni", jsonOf(sni));
    }

    const QString path = firstStr(*obj, {"path"});
    const QString host = firstStr(*obj, {"host"});
    if (network == QLatin1String("ws") || network == QLatin1String("websocket")) {
        put(*obj, "network", jsonOf(QStringLiteral("ws")));
        fillWsOpts(*obj, path, host);
    } else if (network == QLatin1String("grpc")) {
        put(*obj, "network", jsonOf(QStringLiteral("grpc")));
        fillGrpcOpts(*obj, path);
    }

    // 同上：证书校验一律强制打开，不让订阅自己选。
    drop(*obj, "allowInsecure");
    drop(*obj, "insecure");
    put(*obj, "skip-cert-verify", Json(false));

    normalizeAlpnString(*obj);

    for (const char *key : {
             "security", "fp", "allowInsecure", "insecure", "serviceName", "net" }) {
        drop(*obj, key);
    }
    if (holds(*obj, "ws-opts") || holds(*obj, "grpc-opts")) {
        drop(*obj, "path");
        drop(*obj, "host");
    } else {
        stripEmptyStrings(*obj, {"path", "host"});
    }
    stripEmptyStrings(*obj, {"sni"});
}

void normalizeHysteria2(Json &cfg, const Models::Node &node)
{
    setType(cfg, "hysteria2");
    ensureServerPortName(cfg, node);
    ProxyObject *obj = cfg.get_ptr<ProxyObject *>();
    if (obj == nullptr) {
        return;
    }
    if (!holds(*obj, "password")) {
        const Json *auth = peek(*obj, "auth");
        if (auth == nullptr) {
            auth = peek(*obj, "auth-str");
        }
        if (auth != nullptr) {
            put(*obj, "password", *auth);
        }
    }
    if (const QString sni = firstStr(*obj, {"sni", "servername"}); !sni.isNull() && !sni.isEmpty()) {
        put(*obj, "sni", jsonOf(sni));
    }
    // 同上：证书校验一律强制打开。
    drop(*obj, "allowInsecure");
    drop(*obj, "insecure");
    put(*obj, "skip-cert-verify", Json(false));
    const QString obfs = firstStr(*obj, {"obfs"});
    if (obfs.isNull() || obfs.isEmpty()) {
        drop(*obj, "obfs");
        drop(*obj, "obfs-password");
    }
    stripEmptyStrings(*obj, {"sni", "obfs", "obfs-password"});
    drop(*obj, "insecure");
    drop(*obj, "allowInsecure");
}

QString sanitizeName(const QString &text)
{
    QString out = text;
    for (QChar &ch : out) {
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t')) {
            ch = QLatin1Char(' ');
        }
    }
    return trimRs(out);
}

/// Rust 的 unique_name：先净化，净化后为空则 `node-{idx+1}`，冲突再追加 `-2`、`-3`……
QString uniqueName(const QString &base, QSet<QString> &used, qsizetype idx)
{
    QString name = sanitizeName(base);
    if (name.isEmpty()) {
        name = QStringLiteral("node-%1").arg(idx + 1);
    }
    if (!used.contains(name)) {
        used.insert(name);
        return name;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1-%2").arg(name, QString::number(n));
        if (!used.contains(candidate)) {
            used.insert(candidate);
            return candidate;
        }
    }
}

} // namespace

std::optional<Json> nodeToClashProxy(const Models::Node &node)
{
    Json cfg = node.config;
    if (!cfg.is_object()) {
        cfg = Json::object();
    }
    const QString protocol = toAsciiLower(node.protocol);
    if (protocol == QLatin1String("shadowsocks") || protocol == QLatin1String("ss")) {
        normalizeSs(cfg, node);
        return cfg;
    }
    if (protocol == QLatin1String("vmess")) {
        normalizeVmess(cfg, node);
        return cfg;
    }
    if (protocol == QLatin1String("vless")) {
        normalizeVless(cfg, node);
        return cfg;
    }
    if (protocol == QLatin1String("trojan")) {
        normalizeTrojan(cfg, node);
        return cfg;
    }
    if (protocol == QLatin1String("hysteria2") || protocol == QLatin1String("hy2")) {
        normalizeHysteria2(cfg, node);
        return cfg;
    }
    auto passThrough = [&cfg, &node](const char *type) {
        setType(cfg, type);
        ensureServerPortName(cfg, node);
        return cfg;
    };
    if (protocol == QLatin1String("hysteria")) {
        return passThrough("hysteria");
    }
    if (protocol == QLatin1String("socks5") || protocol == QLatin1String("socks")) {
        return passThrough("socks5");
    }
    if (protocol == QLatin1String("http") || protocol == QLatin1String("https")) {
        return passThrough("http");
    }
    if (protocol == QLatin1String("tuic")) {
        return passThrough("tuic");
    }
    if (protocol == QLatin1String("anytls")) {
        return passThrough("anytls");
    }
    if (protocol == QLatin1String("wireguard") || protocol == QLatin1String("wg")) {
        return passThrough("wireguard");
    }
    if (protocol == QLatin1String("shadowsocksr") || protocol == QLatin1String("ssr")) {
        return passThrough("ssr");
    }
    return std::nullopt;
}

QVector<Json> nodesToClashProxies(const QVector<Models::Node> &nodes)
{
    QVector<Json> proxies;
    QSet<QString> usedNames;
    for (qsizetype idx = 0; idx < nodes.size(); ++idx) {
        const Models::Node &node = nodes.at(idx);
        std::optional<Json> proxy = nodeToClashProxy(node);
        if (!proxy.has_value()) {
            continue;
        }
        // base 取归一化后的 name（可能来自 config.name / node.name），纯空白视为没名字。
        QString base;
        bool hasBase = false;
        const auto named = proxy->find(std::string("name"));
        if (named != proxy->end() && named->is_string()) {
            const QString candidate = QString::fromStdString(named->get<std::string>());
            if (!trimRs(candidate).isEmpty()) {
                base = candidate;
                hasBase = true;
            }
        }
        if (!hasBase) {
            base = QStringLiteral("%1-%2-%3")
                       .arg(node.protocol, node.server, QString::number(node.port));
        }
        const QString name = uniqueName(base, usedNames, idx);
        if (ProxyObject *obj = proxy->get_ptr<ProxyObject *>()) {
            put(*obj, "name", jsonOf(name));
        }
        proxies.append(*proxy);
    }
    return proxies;
}

} // namespace Export
