#include "export/UriExport.h"

#include <QByteArray>
#include <QString>
#include <QSet>
#include <QVector>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>

#include "core/Encoding.h"

namespace Export {
namespace {

using ProxyObject = Json::object_t;

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

QString trimRs(const QString &text)
{
    return text.trimmed();
}

/// Rust 的 `s.parse::<u16>()`：`[+-]?数字`，不跳空白，溢出/负数算失败。
std::optional<quint16> parseU16Strict(const QString &text)
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
    if (!ok || value < 0 || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<quint16>(value);
}

/// Rust 的 as_u16：serde_json 的 as_u64 只认非负整数（负数与 443.0 都算没有端口）。
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

const Json *member(const Json &value, const char *key)
{
    const auto *obj = value.get_ptr<const ProxyObject *>();
    if (obj == nullptr) {
        return nullptr;
    }
    const auto found = obj->find(std::string(key));
    return found == obj->end() ? nullptr : &found->second;
}

const Json *memberAt2(const Json &value, const char *first, const char *second)
{
    const Json *level = member(value, first);
    return level == nullptr ? nullptr : member(*level, second);
}

const Json *memberAt3(const Json &value, const char *first, const char *second, const char *third)
{
    const Json *level = member(value, first);
    return level == nullptr ? nullptr : memberAt2(*level, second, third);
}

/// 对应 Rust 的 str_field：空串不算命中（与 ClashProxy 里的 first_str 不同），
/// 数字/布尔转文本；null/数组/对象继续找下一个键。null QString = None。
QString strField(const Json &cfg, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const Json *value = member(cfg, key);
        if (value == nullptr) {
            continue;
        }
        if (value->is_string()) {
            const QString text = QString::fromStdString(value->get<std::string>());
            if (!text.isEmpty()) {
                return text;
            }
        } else if (value->is_number()) {
            return QString::fromStdString(value->dump());
        } else if (value->is_boolean()) {
            return value->get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
        }
    }
    return QString();
}

/// pointer(...).and_then(as_str) 的 Some("") 语义：空串也算命中，
/// 只有类型不对才返回 null QString。
QString strOf(const Json *value)
{
    if (value == nullptr || !value->is_string()) {
        return QString();
    }
    return QString::fromStdString(value->get<std::string>());
}

/// Rust 的 `port_field(cfg).unwrap_or(node.port)`。
QString portOf(const Json &cfg, const Models::Node &node)
{
    const Json *value = member(cfg, "port");
    if (value != nullptr) {
        const std::optional<quint16> parsed = asU16(*value);
        if (parsed.has_value()) {
            return QString::number(*parsed);
        }
    }
    return QString::number(node.port);
}

/// urlencoding_lite：保留 RFC 3986 非保留字符 A-Za-z0-9-_.~，其余按 UTF-8 字节
/// 写 %XX（大写）。这是 Rust 侧手写的函数，不是 percent-encoding crate 的实现。
QString urlencodingLite(const QString &text)
{
    static const char kHex[] = "0123456789ABCDEF";
    const QByteArray bytes = text.toUtf8();
    QByteArray out;
    out.reserve(bytes.size());
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        const bool keep = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                          || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_'
                          || byte == '.' || byte == '~';
        if (keep) {
            out.append(static_cast<char>(byte));
        } else {
            out.append('%').append(kHex[(byte >> 4) & 0x0F]).append(kHex[byte & 0x0F]);
        }
    }
    return QString::fromLatin1(out);
}

/// 把 (键,值) 拼成 query：键不编码（Rust 里键都是写死的字母串），值走 urlencoding_lite。
QString buildQuery(const QVector<QPair<QString, QString>> &pairs)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(pairs.size()));
    for (const auto &pair : pairs) {
        parts.append(QStringLiteral("%1=%2").arg(pair.first, urlencodingLite(pair.second)));
    }
    return parts.join(QLatin1Char('&'));
}

QString wsPath(const Json &cfg)
{
    return strOf(memberAt2(cfg, "ws-opts", "path"));
}

QString wsHost(const Json &cfg)
{
    const QString upper = strOf(memberAt3(cfg, "ws-opts", "headers", "Host"));
    if (!upper.isNull()) {
        return upper;
    }
    return strOf(memberAt3(cfg, "ws-opts", "headers", "host"));
}

QString alpnField(const Json &cfg)
{
    const Json *value = member(cfg, "alpn");
    if (value == nullptr) {
        return QString();
    }
    if (value->is_array()) {
        QStringList parts;
        for (const Json &item : *value) {
            if (item.is_string()) {
                parts.append(QString::fromStdString(item.get<std::string>()));
            }
        }
        return parts.join(QLatin1Char(','));
    }
    if (value->is_string()) {
        return QString::fromStdString(value->get<std::string>());
    }
    return QString();
}

bool rawSchemeMatchesProtocol(const QString &raw, const QString &protocol)
{
    const QString lower = toAsciiLower(raw);
    if (protocol == QLatin1String("shadowsocks") || protocol == QLatin1String("ss")) {
        return lower.startsWith(QLatin1String("ss://"));
    }
    if (protocol == QLatin1String("shadowsocksr") || protocol == QLatin1String("ssr")) {
        return lower.startsWith(QLatin1String("ssr://"));
    }
    if (protocol == QLatin1String("vmess")) {
        return lower.startsWith(QLatin1String("vmess://"));
    }
    if (protocol == QLatin1String("vless")) {
        return lower.startsWith(QLatin1String("vless://"));
    }
    if (protocol == QLatin1String("trojan")) {
        return lower.startsWith(QLatin1String("trojan://"));
    }
    if (protocol == QLatin1String("hysteria2") || protocol == QLatin1String("hy2")) {
        return lower.startsWith(QLatin1String("hysteria2://"))
               || lower.startsWith(QLatin1String("hy2://"));
    }
    if (protocol == QLatin1String("socks5") || protocol == QLatin1String("socks")) {
        return lower.startsWith(QLatin1String("socks5://"))
               || lower.startsWith(QLatin1String("socks://"));
    }
    if (protocol == QLatin1String("http") || protocol == QLatin1String("https")) {
        return lower.startsWith(QLatin1String("http://"))
               || lower.startsWith(QLatin1String("https://"));
    }
    return false;
}

bool looksLikeShareUri(const QString &raw)
{
    const QString lower = toAsciiLower(raw);
    static const QLatin1String kSchemes[] = {
        QLatin1String("ss://"),     QLatin1String("ssr://"),      QLatin1String("vmess://"),
        QLatin1String("vless://"),  QLatin1String("trojan://"),   QLatin1String("hysteria2://"),
        QLatin1String("hy2://"),    QLatin1String("socks5://"),   QLatin1String("socks://"),
    };
    for (const QLatin1String &scheme : kSchemes) {
        if (lower.startsWith(scheme)) {
            return true;
        }
    }
    // http(s) 只在带 @ 且单行时才算分享链接，否则订阅说明里的普通网址会被当成节点。
    if (lower.startsWith(QLatin1String("http://"))
        || lower.startsWith(QLatin1String("https://"))) {
        return raw.contains(QLatin1Char('@')) && !raw.contains(QLatin1Char('\n'));
    }
    return false;
}

QString rebuildSsUri(const Json &cfg, const Models::Node &node)
{
    const QString method = strField(cfg, {"cipher", "method"});
    if (method.isNull()) {
        return QString();
    }
    const QString password = strField(cfg, {"password"});
    if (password.isNull()) {
        return QString();
    }
    QString server = strField(cfg, {"server"});
    if (server.isNull()) {
        server = node.server;
    }
    QString name = strField(cfg, {"name"});
    if (name.isNull()) {
        name = node.name;
    }
    const QString userinfo = QString::fromLatin1(
        Encoding::encode(QStringLiteral("%1:%2").arg(method, password).toUtf8(),
                         Encoding::Engine::Standard));
    QString uri = QStringLiteral("ss://%1@%2:%3").arg(userinfo, server, portOf(cfg, node));
    const QString plugin = strField(cfg, {"plugin"});
    if (!plugin.isNull() && !plugin.isEmpty()) {
        uri += QStringLiteral("?plugin=") + urlencodingLite(plugin);
    }
    if (!name.isEmpty()) {
        uri += QLatin1Char('#') + urlencodingLite(name);
    }
    return uri;
}

QString rebuildVmessUri(const Json &cfg, const Models::Node &node)
{
    QString server = strField(cfg, {"server", "add"});
    if (server.isNull()) {
        server = node.server;
    }
    const QString uuid = strField(cfg, {"uuid", "id"});
    if (uuid.isNull()) {
        return QString();
    }
    QString name = strField(cfg, {"name", "ps"});
    if (name.isNull()) {
        name = node.name;
    }
    QString network = strField(cfg, {"network", "net"});
    if (network.isNull()) {
        network = QStringLiteral("tcp");
    }

    QString tls;
    const Json *tlsValue = member(cfg, "tls");
    if (tlsValue != nullptr) {
        if (tlsValue->is_boolean()) {
            if (tlsValue->get<bool>()) {
                tls = QStringLiteral("tls");
            }
        } else if (tlsValue->is_string()) {
            const QString text = QString::fromStdString(tlsValue->get<std::string>());
            if (!text.isEmpty() && text != QLatin1String("false") && text != QLatin1String("0")) {
                tls = text;
            }
        }
    }

    QString aid = QStringLiteral("0");
    const Json *aidValue = member(cfg, "alterId");
    if (aidValue == nullptr) {
        aidValue = member(cfg, "aid");
    }
    if (aidValue != nullptr) {
        if (aidValue->is_number()) {
            aid = QString::fromStdString(aidValue->dump());
        } else if (aidValue->is_string()) {
            aid = QString::fromStdString(aidValue->get<std::string>());
        }
    }

    QString cipher = strField(cfg, {"cipher", "scy"});
    if (cipher.isNull()) {
        cipher = QStringLiteral("auto");
    }

    QString path = wsPath(cfg);
    if (path.isNull()) {
        path = strField(cfg, {"path"});
    }
    QString host = wsHost(cfg);
    if (host.isNull()) {
        host = strField(cfg, {"host"});
    }
    const QString sni = strField(cfg, {"servername", "sni"});
    const QString alpn = alpnField(cfg);

    // 键集合与顺序由 serde_json 的 BTreeMap 决定（dump 按键字节序），
    // 别的客户端直接比对这段 base64，所以 port/aid 必须是字符串而不是数字。
    Json payload = Json::object();
    payload["v"] = "2";
    payload["ps"] = name.toStdString();
    payload["add"] = server.toStdString();
    payload["port"] = portOf(cfg, node).toStdString();
    payload["id"] = uuid.toStdString();
    payload["aid"] = aid.toStdString();
    payload["scy"] = cipher.toStdString();
    payload["net"] = network.toStdString();
    payload["type"] = "none";
    payload["host"] = host.toStdString();
    payload["path"] = path.toStdString();
    payload["tls"] = tls.toStdString();
    payload["sni"] = sni.toStdString();
    payload["alpn"] = alpn.toStdString();

    const QByteArray encoded =
        Encoding::encode(JsonUtil::dump(payload).toUtf8(), Encoding::Engine::Standard);
    return QStringLiteral("vmess://%1").arg(QString::fromLatin1(encoded));
}

QString rebuildVlessUri(const Json &cfg, const Models::Node &node)
{
    const QString uuid = strField(cfg, {"uuid", "id"});
    if (uuid.isNull()) {
        return QString();
    }
    QString server = strField(cfg, {"server"});
    if (server.isNull()) {
        server = node.server;
    }
    QString name = strField(cfg, {"name"});
    if (name.isNull()) {
        name = node.name;
    }
    QString network = strField(cfg, {"network", "net"});
    if (network.isNull()) {
        network = QStringLiteral("tcp");
    }

    QVector<QPair<QString, QString>> query;
    QString encryption = strField(cfg, {"encryption"});
    if (encryption.isNull()) {
        encryption = QStringLiteral("none");
    }
    query.append(qMakePair(QStringLiteral("encryption"), encryption));
    query.append(qMakePair(QStringLiteral("type"), network));

    const bool isReality = member(cfg, "reality-opts") != nullptr
                           || strField(cfg, {"security"}) == QLatin1String("reality");
    bool tls = isReality;
    const Json *tlsValue = member(cfg, "tls");
    if (tlsValue != nullptr) {
        if (tlsValue->is_boolean()) {
            tls = tlsValue->get<bool>();
        } else if (tlsValue->is_string()) {
            const QString text = toAsciiLower(QString::fromStdString(tlsValue->get<std::string>()));
            tls = text == QLatin1String("tls") || text == QLatin1String("true")
                  || text == QLatin1String("1") || text == QLatin1String("reality");
        }
    }
    if (isReality) {
        query.append(qMakePair(QStringLiteral("security"), QStringLiteral("reality")));
        QString pbk = strOf(memberAt2(cfg, "reality-opts", "public-key"));
        if (pbk.isNull()) {
            pbk = strField(cfg, {"pbk"});
        }
        if (!pbk.isNull() && !pbk.isEmpty()) {
            query.append(qMakePair(QStringLiteral("pbk"), pbk));
        }
        QString sid = strOf(memberAt2(cfg, "reality-opts", "short-id"));
        if (sid.isNull()) {
            sid = strField(cfg, {"sid"});
        }
        if (!sid.isNull() && !sid.isEmpty()) {
            query.append(qMakePair(QStringLiteral("sid"), sid));
        }
    } else if (tls) {
        query.append(qMakePair(QStringLiteral("security"), QStringLiteral("tls")));
    }

    if (const QString sni = strField(cfg, {"servername", "sni"}); !sni.isNull() && !sni.isEmpty()) {
        query.append(qMakePair(QStringLiteral("sni"), sni));
    }
    if (const QString fp = strField(cfg, {"client-fingerprint", "fp"});
        !fp.isNull() && !fp.isEmpty()) {
        query.append(qMakePair(QStringLiteral("fp"), fp));
    }
    if (const QString flow = strField(cfg, {"flow"}); !flow.isNull() && !flow.isEmpty()) {
        query.append(qMakePair(QStringLiteral("flow"), flow));
    }
    QString path = wsPath(cfg);
    if (path.isNull()) {
        path = strField(cfg, {"path"});
    }
    if (!path.isNull() && !path.isEmpty()) {
        query.append(qMakePair(QStringLiteral("path"), path));
    }
    QString host = wsHost(cfg);
    if (host.isNull()) {
        host = strField(cfg, {"host"});
    }
    if (!host.isNull() && !host.isEmpty()) {
        query.append(qMakePair(QStringLiteral("host"), host));
    }
    if (network == QLatin1String("grpc")) {
        QString serviceName = strOf(memberAt2(cfg, "grpc-opts", "grpc-service-name"));
        if (serviceName.isNull()) {
            serviceName = strField(cfg, {"serviceName"});
        }
        if (!serviceName.isNull() && !serviceName.isEmpty()) {
            query.append(qMakePair(QStringLiteral("serviceName"), serviceName));
        }
    }
    if (const QString alpn = alpnField(cfg); !alpn.isNull() && !alpn.isEmpty()) {
        query.append(qMakePair(QStringLiteral("alpn"), alpn));
    }

    // vless 的 query 永远带 '?'，哪怕一个参数都没有（Rust 的 format! 就是这么写的）。
    QString uri = QStringLiteral("vless://%1@%2:%3?%4")
                      .arg(uuid, server, portOf(cfg, node), buildQuery(query));
    if (!name.isEmpty()) {
        uri += QLatin1Char('#') + urlencodingLite(name);
    }
    return uri;
}

QString rebuildTrojanUri(const Json &cfg, const Models::Node &node)
{
    const QString password = strField(cfg, {"password"});
    if (password.isNull()) {
        return QString();
    }
    QString server = strField(cfg, {"server"});
    if (server.isNull()) {
        server = node.server;
    }
    QString name = strField(cfg, {"name"});
    if (name.isNull()) {
        name = node.name;
    }

    QVector<QPair<QString, QString>> query;
    if (const QString sni = strField(cfg, {"sni", "servername"}); !sni.isNull() && !sni.isEmpty()) {
        query.append(qMakePair(QStringLiteral("sni"), sni));
    }
    // trojan 只读 network，不读 net（和 vless/vmess 不一样，照抄）。
    QString network = strField(cfg, {"network"});
    if (network.isNull()) {
        network = QStringLiteral("tcp");
    }
    if (network != QLatin1String("tcp")) {
        query.append(qMakePair(QStringLiteral("type"), network));
    }
    QString path = wsPath(cfg);
    if (path.isNull()) {
        path = strField(cfg, {"path"});
    }
    if (!path.isNull() && !path.isEmpty()) {
        query.append(qMakePair(QStringLiteral("path"), path));
    }
    QString host = wsHost(cfg);
    if (host.isNull()) {
        host = strField(cfg, {"host"});
    }
    if (!host.isNull() && !host.isEmpty()) {
        query.append(qMakePair(QStringLiteral("host"), host));
    }
    const Json *skip = member(cfg, "skip-cert-verify");
    if (skip != nullptr && skip->is_boolean() && skip->get<bool>()) {
        query.append(qMakePair(QStringLiteral("allowInsecure"), QStringLiteral("1")));
    }

    QString uri = QStringLiteral("trojan://%1@%2:%3").arg(password, server, portOf(cfg, node));
    const QString text = buildQuery(query);
    if (!text.isEmpty()) {
        uri += QLatin1Char('?') + text;
    }
    if (!name.isEmpty()) {
        uri += QLatin1Char('#') + urlencodingLite(name);
    }
    return uri;
}

QString rebuildHysteria2Uri(const Json &cfg, const Models::Node &node)
{
    const QString password = strField(cfg, {"password", "auth", "auth-str"});
    if (password.isNull()) {
        return QString();
    }
    QString server = strField(cfg, {"server"});
    if (server.isNull()) {
        server = node.server;
    }
    QString name = strField(cfg, {"name"});
    if (name.isNull()) {
        name = node.name;
    }

    QVector<QPair<QString, QString>> query;
    if (const QString sni = strField(cfg, {"sni", "servername"}); !sni.isNull() && !sni.isEmpty()) {
        query.append(qMakePair(QStringLiteral("sni"), sni));
    }
    if (const QString obfs = strField(cfg, {"obfs"}); !obfs.isNull() && !obfs.isEmpty()) {
        query.append(qMakePair(QStringLiteral("obfs"), obfs));
    }
    if (const QString obfsPassword = strField(cfg, {"obfs-password"});
        !obfsPassword.isNull() && !obfsPassword.isEmpty()) {
        query.append(qMakePair(QStringLiteral("obfs-password"), obfsPassword));
    }
    bool insecure = false;
    const Json *skip = member(cfg, "skip-cert-verify");
    if (skip != nullptr && skip->is_boolean() && skip->get<bool>()) {
        insecure = true;
    }
    const QString insecureText = strField(cfg, {"insecure"});
    if (insecureText == QLatin1String("1") || insecureText == QLatin1String("true")) {
        insecure = true;
    }
    if (insecure) {
        query.append(qMakePair(QStringLiteral("insecure"), QStringLiteral("1")));
    }

    QString uri = QStringLiteral("hysteria2://%1@%2:%3").arg(password, server, portOf(cfg, node));
    const QString text = buildQuery(query);
    if (!text.isEmpty()) {
        uri += QLatin1Char('?') + text;
    }
    if (!name.isEmpty()) {
        uri += QLatin1Char('#') + urlencodingLite(name);
    }
    return uri;
}

QString rebuildSocksUri(const Json &cfg, const Models::Node &node)
{
    QString server = strField(cfg, {"server"});
    if (server.isNull()) {
        server = node.server;
    }
    const QString port = portOf(cfg, node);
    const QString user = strField(cfg, {"username"});
    const QString pass = strField(cfg, {"password"});
    QString name = strField(cfg, {"name"});
    if (name.isNull()) {
        name = node.name;
    }
    // 账号密码是原样拼进去的，不做百分号编码（与 Rust 一致）。
    QString uri = (user.isNull() || user.isEmpty())
                      ? QStringLiteral("socks5://%1:%2").arg(server, port)
                      : QStringLiteral("socks5://%1:%2@%3:%4").arg(user, pass, server, port);
    if (!name.isEmpty()) {
        uri += QLatin1Char('#') + urlencodingLite(name);
    }
    return uri;
}

QString rebuildHttpUri(const Json &cfg, const Models::Node &node)
{
    QString server = strField(cfg, {"server"});
    if (server.isNull()) {
        server = node.server;
    }
    const QString port = portOf(cfg, node);
    const QString user = strField(cfg, {"username"});
    const QString pass = strField(cfg, {"password"});
    QString name = strField(cfg, {"name"});
    if (name.isNull()) {
        name = node.name;
    }
    const Json *tlsValue = member(cfg, "tls");
    const bool tls = tlsValue != nullptr && tlsValue->is_boolean() && tlsValue->get<bool>();
    const QString scheme = tls ? QStringLiteral("https") : QStringLiteral("http");
    QString uri = (user.isNull() || user.isEmpty())
                      ? QStringLiteral("%1://%2:%3").arg(scheme, server, port)
                      : QStringLiteral("%1://%2:%3@%4:%5").arg(scheme, user, pass, server, port);
    if (!name.isEmpty()) {
        uri += QLatin1Char('#') + urlencodingLite(name);
    }
    return uri;
}

} // namespace

QString nodeToShareUri(const Models::Node &node)
{
    // 一律从解析出来的 config 重建：很多免费订阅把 vless 标成 ss://，
    // 直接回吐 raw 会破坏 v2rayN 的导入与连接。
    const Json &cfg = node.config;
    const QString protocol = toAsciiLower(node.protocol);
    QString uri;
    if (protocol == QLatin1String("shadowsocks") || protocol == QLatin1String("ss")) {
        uri = rebuildSsUri(cfg, node);
    } else if (protocol == QLatin1String("vmess")) {
        uri = rebuildVmessUri(cfg, node);
    } else if (protocol == QLatin1String("vless")) {
        uri = rebuildVlessUri(cfg, node);
    } else if (protocol == QLatin1String("trojan")) {
        uri = rebuildTrojanUri(cfg, node);
    } else if (protocol == QLatin1String("hysteria2") || protocol == QLatin1String("hy2")) {
        uri = rebuildHysteria2Uri(cfg, node);
    } else if (protocol == QLatin1String("socks5") || protocol == QLatin1String("socks")) {
        uri = rebuildSocksUri(cfg, node);
    } else if (protocol == QLatin1String("http") || protocol == QLatin1String("https")) {
        uri = rebuildHttpUri(cfg, node);
    }
    if (!uri.isNull()) {
        return uri;
    }

    // 兜底：只有 raw 的 scheme 和节点协议一致时才保留 raw。
    const QString raw = trimRs(node.raw);
    if (looksLikeShareUri(raw) && rawSchemeMatchesProtocol(raw, protocol)) {
        return raw;
    }
    return QString();
}

QVector<QString> nodesToShareUris(const QVector<Models::Node> &nodes)
{
    QVector<QString> out;
    QSet<QString> seen;
    for (const Models::Node &node : nodes) {
        const QString uri = nodeToShareUri(node);
        if (uri.isNull()) {
            continue;
        }
        if (!seen.contains(uri)) {
            seen.insert(uri);
            out.append(uri);
        }
    }
    return out;
}

} // namespace Export
