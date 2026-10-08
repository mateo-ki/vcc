#include "parser/Uri.h"

#include <QStringList>

#include <initializer_list>
#include <utility>

#include "core/JsonUtil.h"
#include "parser/Common.h"
#include "parser/Subscription.h"

namespace Parser {

namespace {

using Common::Authority;
using Common::HostPort;
using Common::UserinfoHostPort;

std::optional<Models::Node> parseSs(const QString& uri, const QString& sourceId);
std::optional<Models::Node> parseVmess(const QString& uri, const QString& sourceId);
std::optional<Models::Node> parseVless(const QString& uri, const QString& sourceId);
std::optional<Models::Node> parseTrojan(const QString& uri, const QString& sourceId);
std::optional<Models::Node> parseHysteria2(const QString& uri, const QString& sourceId);
std::optional<Models::Node> parseSocks(const QString& uri, const QString& sourceId);
std::optional<Models::Node> parseHttpProxy(const QString& uri, const QString& sourceId);

/// 对应 Rust 的 `trim_start_matches(a).trim_start_matches(b)`：两次独立的反复剥前缀。
QString trimPrefixRepeated(QString text, const QString& prefix)
{
    while (text.startsWith(prefix)) {
        text.remove(0, prefix.size());
    }
    return text;
}

/// 去掉 ss:// / SS:// 前缀（uri.rs 里三个函数都用这一对）。
QString stripSsPrefix(const QString& uri)
{
    return trimPrefixRepeated(trimPrefixRepeated(uri, QStringLiteral("ss://")),
                              QStringLiteral("SS://"));
}

/// 对应 Rust `split_fragment`：第二个元素为 null QString 表示 Option 的 None。
std::pair<QString, QString> splitFragment(const QString& text)
{
    const int hash = text.indexOf(QLatin1Char('#'));
    if (hash < 0) {
        return {text, QString()};
    }
    return {text.left(hash), text.mid(hash + 1)};
}

/// 对应 Rust `str::replacen(before, after, 1)`：只替换第一处出现。
QString replaceFirst(QString text, const QString& before, const QString& after)
{
    const int index = text.indexOf(before);
    if (index >= 0) {
        text.replace(index, before.size(), after);
    }
    return text;
}

/// serde_json 的 `v.get(k).and_then(as_str)`：只有字符串类型才算命中。
std::optional<QString> jsonAsString(const Json& value, const char* key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || !found->is_string()) {
        return std::nullopt;
    }
    return QString::fromStdString(found->get<std::string>());
}

/// 对应 Rust `parse_port_value`：`as_u64` 命中后截 u16，否则按 `str::parse::<u16>()` 解字符串。
std::optional<int> parsePortValue(const Json& value)
{
    if (value.is_number_unsigned()) {
        const quint64 raw = value.get<quint64>();
        if (raw <= quint64(65535)) {
            return int(raw);
        }
        return std::nullopt;
    }
    if (value.is_string()) {
        int port = 0;
        if (Common::parsePortNumber(QString::fromStdString(value.get<std::string>()), port)) {
            return port;
        }
    }
    return std::nullopt;
}

/// `v.get(key).and_then(parse_port_value)`：键不存在或解不出 u16 都是 None。
std::optional<int> portFromKey(const Json& value, const char* key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end()) {
        return std::nullopt;
    }
    return parsePortValue(*found);
}

/// `q.get(k).cloned().unwrap_or_else(|| fallback)`，键缺失即 fallback（默认空串）。
QString queryValue(const QMap<QString, QString>& query, const QString& key,
                   const QString& fallback = QString())
{
    const auto found = query.find(key);
    if (found == query.end()) {
        return fallback;
    }
    return found.value();
}

Json ssConfig(const QString& host, int port, const QString& method, const QString& password,
              const QString& name)
{
    Json config = Json::object();
    config["type"] = std::string("ss");
    config["server"] = host.toStdString();
    config["port"] = port;
    config["cipher"] = method.toStdString();
    config["password"] = password.toStdString();
    config["name"] = name.toStdString();
    return config;
}

std::optional<Models::Node> tryParseMislabeledSsAsVless(const QString& uri, const QString& sourceId)
{
    const QString rest = stripSsPrefix(uri);
    const auto [body, fragment] = splitFragment(rest);
    Q_UNUSED(fragment);
    if (!body.contains(QLatin1Char('@')) || !body.contains(QLatin1Char('?'))) {
        return std::nullopt;
    }
    const int at = body.indexOf(QLatin1Char('@')); // split_once：第一个 '@'
    const QString hostport = body.mid(at + 1);
    const QString userinfo = Common::stripQueryFragment(body.left(at));

    const int question = body.indexOf(QLatin1Char('?'));
    const QString q = question < 0 ? QString() : body.mid(question + 1);
    const QString ql = Common::toAsciiLower(q);
    const bool vlessLike = ql.contains(QStringLiteral("security="))
        || ql.contains(QStringLiteral("encryption=")) || ql.contains(QStringLiteral("type="))
        || ql.contains(QStringLiteral("sni=")) || ql.contains(QStringLiteral("path="));
    if (!vlessLike) {
        return std::nullopt;
    }
    // Rust 的这段分支是"用户名 + vless 风格查询参数"的宽松放行；
    // 只有"含 ':' 但不是 UUID"才放弃。
    if (!Common::looksLikeUuid(userinfo) && !userinfo.contains(QLatin1Char(':'))) {
        // 继续
    } else if (!Common::looksLikeUuid(userinfo)) {
        return std::nullopt;
    }
    if (!Common::splitHostPort(hostport).has_value()) {
        return std::nullopt;
    }
    auto node = parseVless(QStringLiteral("vless://") + rest, sourceId);
    if (node.has_value()) {
        node->raw = uri;
    }
    return node;
}

std::optional<Models::Node> tryParseMislabeledSsAsVmess(const QString& uri, const QString& sourceId)
{
    const QString rest = stripSsPrefix(uri);
    const auto [body, fragment] = splitFragment(rest);
    Q_UNUSED(fragment);
    // 只有看起来像 base64 JSON（以 eyJ 开头）的正文才改写。
    QString cleaned;
    cleaned.reserve(body.size());
    for (const QChar ch : body) {
        if (!Common::isUnicodeSpace(ch)) {
            cleaned.append(ch);
        }
    }
    const bool looksJsonB64 = cleaned.startsWith(QStringLiteral("eyJ"))
        || cleaned.startsWith(QStringLiteral("eyj"));
    if (!looksJsonB64) {
        return std::nullopt;
    }
    auto node = parseVmess(QStringLiteral("vmess://") + cleaned, sourceId);
    if (node.has_value()) {
        node->raw = uri;
    }
    return node;
}

std::optional<Models::Node> parseSs(const QString& uri, const QString& sourceId)
{
    const QString rest = stripSsPrefix(uri);
    const auto [body, fragment] = splitFragment(rest);
    const QString name = fragment.isNull() ? QString() : Common::percentDecode(fragment);

    // SIP002: ss://base64(method:password)@host:port#name
    //         ss://base64(method:password)@host:port/?plugin=...#name
    // Legacy: ss://base64(method:password@host:port)#name
    if (body.contains(QLatin1Char('@'))) {
        const std::optional<UserinfoHostPort> parts = Common::splitUserinfoHostPort(body);
        if (!parts.has_value()) {
            return std::nullopt;
        }
        const QString userinfoDecoded =
            Common::decodeBase64Str(parts->userinfo).value_or(parts->userinfo);
        const int colon = userinfoDecoded.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            // userinfo 只有密码、没有 method（罕见）——和 Rust 一样放弃
            return std::nullopt;
        }
        const QString method = userinfoDecoded.left(colon);
        const QString password = userinfoDecoded.mid(colon + 1);

        const int question = body.indexOf(QLatin1Char('?'));
        const QString queryText = question < 0 ? QString() : body.mid(question + 1);
        const QMap<QString, QString> query = Common::parseQuery(queryText);

        Json config = ssConfig(parts->host, parts->port, method, password, name);
        if (config.is_object()) {
            if (query.contains(QStringLiteral("plugin"))) {
                config["plugin"] = query.value(QStringLiteral("plugin")).toStdString();
            }
            QString pluginOptsKey;
            if (query.contains(QStringLiteral("plugin-opts"))) {
                pluginOptsKey = QStringLiteral("plugin-opts");
            } else if (query.contains(QStringLiteral("plugin_opts"))) {
                pluginOptsKey = QStringLiteral("plugin_opts");
            }
            if (!pluginOptsKey.isEmpty()) {
                config["plugin-opts"] = query.value(pluginOptsKey).toStdString();
            }
        }
        return makeNode(QStringLiteral("shadowsocks"), name, parts->host, parts->port, uri, config,
                        sourceId);
    }

    const std::optional<QString> decodedOpt = Common::decodeBase64Str(body);
    if (!decodedOpt.has_value()) {
        return std::nullopt;
    }
    const QString decoded = *decodedOpt;
    if (decoded.trimmed().startsWith(QLatin1Char('{'))) {
        const Result<Json> parsed = JsonUtil::parse(decoded);
        if (parsed.isOk()) {
            const Json& value = parsed.value();
            const bool looksVmess = value.is_object()
                && (value.find("add") != value.end() || value.find("id") != value.end());
            if (looksVmess) {
                auto node = parseVmess(QStringLiteral("vmess://") + body, sourceId);
                if (node.has_value()) {
                    node->raw = uri;
                }
                return node;
            }
        }
    }
    // method:password@host:port
    const int at = decoded.lastIndexOf(QLatin1Char('@')); // rsplit_once
    if (at < 0) {
        return std::nullopt;
    }
    const QString methodPass = decoded.left(at);
    const QString hostport = decoded.mid(at + 1);
    const int colon = methodPass.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return std::nullopt;
    }
    const QString method = methodPass.left(colon);
    const QString password = methodPass.mid(colon + 1);
    const std::optional<HostPort> hostPort = Common::splitHostPort(hostport);
    if (!hostPort.has_value()) {
        return std::nullopt;
    }
    int port = 0;
    if (!Common::parsePortNumber(hostPort->port, port)) {
        return std::nullopt;
    }
    const Json config = ssConfig(hostPort->host, port, method, password, name);
    return makeNode(QStringLiteral("shadowsocks"), name, hostPort->host, port, uri, config,
                    sourceId);
}

std::optional<Models::Node> parseVmess(const QString& uri, const QString& sourceId)
{
    QString rest = trimPrefixRepeated(trimPrefixRepeated(uri, QStringLiteral("vmess://")),
                                      QStringLiteral("VMESS://"));
    rest = Common::stripQueryFragment(rest);
    const std::optional<QString> decoded = Common::decodeBase64Str(rest);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    Result<Json> parsed = JsonUtil::parse(*decoded);
    if (parsed.isErr()) {
        return std::nullopt;
    }
    Json value = parsed.takeValue();

    std::optional<QString> server = jsonAsString(value, "add");
    if (!server.has_value()) {
        server = jsonAsString(value, "server");
    }
    if (!server.has_value()) {
        return std::nullopt;
    }
    std::optional<int> port = portFromKey(value, "port");
    if (!port.has_value()) {
        port = portFromKey(value, "server_port");
    }
    if (!port.has_value()) {
        return std::nullopt;
    }
    std::optional<QString> name = jsonAsString(value, "ps");
    if (!name.has_value()) {
        name = jsonAsString(value, "remark");
    }
    if (!name.has_value()) {
        name = QString();
    }

    Json config = value;
    if (config.is_object()) {
        config["type"] = std::string("vmess");
        config["server"] = server->toStdString();
        config["port"] = *port;
        config["name"] = name->toStdString();
    }
    return makeNode(QStringLiteral("vmess"), *name, *server, *port, uri, config, sourceId);
}

Models::Node vlessNode(const QString& uri, const QString& sourceId, const QString& uuid,
                       const QString& host, int port, const QString& name,
                       const QMap<QString, QString>& query)
{
    Json config = Json::object();
    config["type"] = std::string("vless");
    config["server"] = host.toStdString();
    config["port"] = port;
    config["uuid"] = uuid.toStdString();
    config["name"] = name.toStdString();
    config["flow"] = queryValue(query, QStringLiteral("flow")).toStdString();
    config["encryption"] =
        queryValue(query, QStringLiteral("encryption"), QStringLiteral("none")).toStdString();
    config["network"] =
        queryValue(query, QStringLiteral("type"), QStringLiteral("tcp")).toStdString();
    config["security"] = queryValue(query, QStringLiteral("security")).toStdString();
    config["sni"] = queryValue(query, QStringLiteral("sni")).toStdString();
    config["fp"] = queryValue(query, QStringLiteral("fp")).toStdString();
    config["pbk"] = queryValue(query, QStringLiteral("pbk")).toStdString();
    config["sid"] = queryValue(query, QStringLiteral("sid")).toStdString();
    config["path"] = queryValue(query, QStringLiteral("path")).toStdString();
    config["host"] = queryValue(query, QStringLiteral("host")).toStdString();
    config["serviceName"] = queryValue(query, QStringLiteral("serviceName")).toStdString();
    config["alpn"] = queryValue(query, QStringLiteral("alpn")).toStdString();
    // 查询参数来自订阅链接本身，不能让它关掉证书校验。
    config["allowInsecure"] = false;
    return makeNode(QStringLiteral("vless"), name, host, port, uri, config, sourceId);
}

std::optional<Models::Node> parseVless(const QString& uri, const QString& sourceId)
{
    if (const std::optional<Url> url = parseUrl(uri)) {
        const QString uuid = Common::percentDecode(url->username);
        if (uuid.isEmpty()) {
            return std::nullopt;
        }
        const int port = url->port < 0 ? 443 : url->port;
        const QString name = url->hasFragment() ? Common::percentDecode(url->fragment) : QString();
        const QMap<QString, QString> query =
            Common::parseQuery(url->hasQuery() ? url->query : QString());
        return vlessNode(uri, sourceId, uuid, url->host, port, name, query);
    }

    // Url::parse 解析不了（emoji / 奇怪 fragment）时的手工拆解
    const std::optional<Authority> authority = Common::parseAuthority(uri);
    if (!authority.has_value()) {
        return std::nullopt;
    }
    if (authority->userinfo.isEmpty()) {
        return std::nullopt;
    }
    const QMap<QString, QString> query = Common::parseQuery(authority->query);
    return vlessNode(uri, sourceId, authority->userinfo, authority->host, authority->port,
                     authority->fragment, query);
}

Models::Node trojanNode(const QString& uri, const QString& sourceId, const QString& password,
                        const QString& host, int port, const QString& name,
                        const QMap<QString, QString>& query)
{
    Json config = Json::object();
    config["type"] = std::string("trojan");
    config["server"] = host.toStdString();
    config["port"] = port;
    config["password"] = password.toStdString();
    config["name"] = name.toStdString();
    config["sni"] = queryValue(query, QStringLiteral("sni")).toStdString();
    config["network"] =
        queryValue(query, QStringLiteral("type"), QStringLiteral("tcp")).toStdString();
    config["security"] =
        queryValue(query, QStringLiteral("security"), QStringLiteral("tls")).toStdString();
    config["fp"] = queryValue(query, QStringLiteral("fp")).toStdString();
    config["alpn"] = queryValue(query, QStringLiteral("alpn")).toStdString();
    config["path"] = queryValue(query, QStringLiteral("path")).toStdString();
    config["host"] = queryValue(query, QStringLiteral("host")).toStdString();
    // 查询参数来自订阅链接本身，不能让它关掉证书校验。
    config["allowInsecure"] = false;
    return makeNode(QStringLiteral("trojan"), name, host, port, uri, config, sourceId);
}

std::optional<Models::Node> parseTrojan(const QString& uri, const QString& sourceId)
{
    if (const std::optional<Url> url = parseUrl(uri)) {
        const QString password = Common::percentDecode(url->username);
        if (password.isEmpty()) {
            return std::nullopt;
        }
        const int port = url->port < 0 ? 443 : url->port;
        const QString name = url->hasFragment() ? Common::percentDecode(url->fragment) : QString();
        const QMap<QString, QString> query =
            Common::parseQuery(url->hasQuery() ? url->query : QString());
        return trojanNode(uri, sourceId, password, url->host, port, name, query);
    }
    const std::optional<Authority> authority = Common::parseAuthority(uri);
    if (!authority.has_value()) {
        return std::nullopt;
    }
    if (authority->userinfo.isEmpty()) {
        return std::nullopt;
    }
    const QMap<QString, QString> query = Common::parseQuery(authority->query);
    return trojanNode(uri, sourceId, authority->userinfo, authority->host, authority->port,
                      authority->fragment, query);
}

Json hysteria2Config(const QString& host, int port, const QString& auth, const QString& name,
                     const QMap<QString, QString>& query)
{
    Json config = Json::object();
    config["type"] = std::string("hysteria2");
    config["server"] = host.toStdString();
    config["port"] = port;
    config["password"] = auth.toStdString();
    config["name"] = name.toStdString();
    config["sni"] = queryValue(query, QStringLiteral("sni")).toStdString();
    config["insecure"] = false;
    config["obfs"] = queryValue(query, QStringLiteral("obfs")).toStdString();
    config["obfs-password"] = queryValue(query, QStringLiteral("obfs-password")).toStdString();
    return config;
}

std::optional<Models::Node> parseHysteria2(const QString& uri, const QString& sourceId)
{
    QString normalized = replaceFirst(uri, QStringLiteral("hy2://"), QStringLiteral("hysteria2://"));
    normalized = replaceFirst(normalized, QStringLiteral("HY2://"), QStringLiteral("hysteria2://"));

    if (const std::optional<Url> url = parseUrl(normalized)) {
        const QString auth = Common::percentDecode(url->username);
        const int port = url->port < 0 ? 443 : url->port;
        const QString name = url->hasFragment() ? Common::percentDecode(url->fragment) : QString();
        const QMap<QString, QString> query =
            Common::parseQuery(url->hasQuery() ? url->query : QString());
        // raw 用的是传进来的 uri，而不是改写后的 normalized。
        return makeNode(QStringLiteral("hysteria2"), name, url->host, port, uri,
                        hysteria2Config(url->host, port, auth, name, query), sourceId);
    }

    const std::optional<Authority> authority = Common::parseAuthority(normalized);
    if (!authority.has_value()) {
        return std::nullopt;
    }
    const QMap<QString, QString> query = Common::parseQuery(authority->query);
    return makeNode(QStringLiteral("hysteria2"), authority->fragment, authority->host,
                    authority->port, uri,
                    hysteria2Config(authority->host, authority->port, authority->userinfo,
                                    authority->fragment, query),
                    sourceId);
}

std::optional<Models::Node> parseSocks(const QString& uri, const QString& sourceId)
{
    const std::optional<Url> url = parseUrl(uri);
    if (!url.has_value()) {
        return std::nullopt;
    }
    const int port = url->port < 0 ? 1080 : url->port;
    const QString name = url->hasFragment() ? Common::percentDecode(url->fragment) : QString();
    Json config = Json::object();
    config["type"] = std::string("socks5");
    config["server"] = url->host.toStdString();
    config["port"] = port;
    // Rust 这里直接取 username()（序列化原文），不做 percent_decode。
    config["username"] = url->username.toStdString();
    config["password"] =
        (url->password.isNull() ? QString() : Common::percentDecode(url->password)).toStdString();
    config["name"] = name.toStdString();
    return makeNode(QStringLiteral("socks5"), name, url->host, port, uri, config, sourceId);
}

std::optional<Models::Node> parseHttpProxy(const QString& uri, const QString& sourceId)
{
    const std::optional<Url> url = parseUrl(uri);
    if (!url.has_value()) {
        return std::nullopt;
    }
    const bool tls = url->scheme == QLatin1String("https");
    const int port = url->port < 0 ? (tls ? 443 : 80) : url->port;
    const QString name = url->hasFragment() ? Common::percentDecode(url->fragment) : QString();
    Json config = Json::object();
    config["type"] = std::string("http");
    config["server"] = url->host.toStdString();
    config["port"] = port;
    config["username"] = url->username.toStdString();
    config["password"] =
        (url->password.isNull() ? QString() : Common::percentDecode(url->password)).toStdString();
    config["tls"] = tls;
    config["name"] = name.toStdString();
    return makeNode(QStringLiteral("http"), name, url->host, port, uri, config, sourceId);
}

/// WHATWG forbidden host code point（opaque host 里出现即解析失败，与 Rust 一致）。
bool isForbiddenHostChar(QChar ch)
{
    const ushort u = ch.unicode();
    return u == 0x00 || u == u' ' || u == u'#' || u == u'/' || u == u':' || u == u'<' || u == u'>'
        || u == u'?' || u == u'@' || u == u'[' || u == u'\\' || u == u']' || u == u'^' || u == u'|';
}

/// url crate 的 parse_port：只允许空串或纯 ASCII 数字，且不超过 65535。
bool parseUrlPort(const QString& text, int& out)
{
    if (text.isEmpty()) {
        return false; // 空端口 = Rust 的 Ok(None)
    }
    quint64 value = 0;
    for (const QChar ch : text) {
        if (!(ch.unicode() >= u'0' && ch.unicode() <= u'9')) {
            return false;
        }
        value = value * 10u + quint64(ch.unicode() - u'0');
        if (value > 65535u) {
            return false;
        }
    }
    out = int(value);
    return true;
}

} // namespace

std::optional<Url> parseUrl(const QString& input)
{
    // 1) url crate 的 Input 会删掉所有 TAB/LF/CR，并去掉首尾的 C0 控制符与空格。
    QString text;
    text.reserve(input.size());
    for (const QChar ch : input) {
        if (ch != QLatin1Char('\t') && ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
            text.append(ch);
        }
    }
    while (!text.isEmpty() && text.at(0).unicode() <= 0x20) {
        text.remove(0, 1);
    }
    while (!text.isEmpty() && text.at(text.size() - 1).unicode() <= 0x20) {
        text.chop(1);
    }

    // 2) scheme = [A-Za-z][A-Za-z0-9+.-]* ':'
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return std::nullopt;
    }
    const ushort head = text.at(0).unicode();
    if (!((head >= u'A' && head <= u'Z') || (head >= u'a' && head <= u'z'))) {
        return std::nullopt;
    }
    for (qsizetype i = 1; i < colon; ++i) {
        const ushort u = text.at(i).unicode();
        const bool allowed = (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z')
            || (u >= u'0' && u <= u'9') || u == u'+' || u == u'-' || u == u'.';
        if (!allowed) {
            return std::nullopt;
        }
    }
    Url url;
    url.scheme = Common::toAsciiLower(text.left(colon));
    if (!text.mid(colon + 1).startsWith(QStringLiteral("//"))) {
        // Rust 会把它解析成 opaque path（host_str() = None），调用方紧接着 `?` 放弃；
        // 而 uri.rs 的兜底 parse_authority 同样要求 "://"，两种路径结果一致。
        return std::nullopt;
    }
    QString rest = text.mid(colon + 3);

    // 3) authority 止于第一个 '/'、'?'、'#'
    qsizetype authorityEnd = rest.size();
    for (const QChar stop : {QLatin1Char('/'), QLatin1Char('?'), QLatin1Char('#')}) {
        const int index = rest.indexOf(stop);
        if (index >= 0 && index < authorityEnd) {
            authorityEnd = index;
        }
    }
    const QString authority = rest.left(authorityEnd);
    QString tail = rest.mid(authorityEnd);

    // 4) fragment 取第一个 '#'，query 取剩下的第一个 '?'（url crate 保留原文，不解码）
    const int hash = tail.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        url.fragment = tail.mid(hash + 1);
        tail = tail.left(hash);
    }
    const int question = tail.indexOf(QLatin1Char('?'));
    if (question >= 0) {
        url.query = tail.mid(question + 1);
    }

    // 5) userinfo 以最后一个 '@' 为界；username/password 再按第一个 ':' 分割
    QString hostpart = authority;
    const int at = authority.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {
        const QString userinfo = authority.left(at);
        hostpart = authority.mid(at + 1);
        const int userColon = userinfo.indexOf(QLatin1Char(':'));
        if (userColon < 0) {
            url.username = Common::encodeUserinfo(userinfo);
        } else {
            url.username = Common::encodeUserinfo(userinfo.left(userColon));
            url.password = Common::encodeUserinfo(userinfo.mid(userColon + 1));
        }
    }

    // 6) host 取到第一个方括号外的 ':'，其余（去掉冒号）是端口
    QString hostText;
    QString portText;
    bool portGiven = false;
    if (hostpart.startsWith(QLatin1Char('['))) {
        if (!hostpart.endsWith(QLatin1Char(']'))) {
            return std::nullopt; // Rust 的 InvalidIpv6Address
        }
        const int close = hostpart.indexOf(QLatin1Char(']'));
        hostText = hostpart.mid(0, close + 1); // host_str() 对方括号主机保留中括号
        const QString remainder = hostpart.mid(close + 1);
        if (remainder.isEmpty()) {
            // 无端口
        } else if (!remainder.startsWith(QLatin1Char(':'))) {
            return std::nullopt;
        } else {
            portGiven = true;
            portText = remainder.mid(1);
        }
    } else {
        const int firstColon = hostpart.indexOf(QLatin1Char(':'));
        if (firstColon >= 0) {
            hostText = hostpart.left(firstColon);
            portText = hostpart.mid(firstColon + 1);
            portGiven = true;
        } else {
            hostText = hostpart;
        }
    }
    if (portGiven && !portText.isEmpty()) {
        int port = 0;
        if (!parseUrlPort(portText, port)) {
            return std::nullopt; // Rust 的 InvalidPortNumber
        }
        url.port = port;
    }

    const bool special = url.scheme == QLatin1String("http") || url.scheme == QLatin1String("https")
        || url.scheme == QLatin1String("ws") || url.scheme == QLatin1String("wss")
        || url.scheme == QLatin1String("ftp");
    if (special && hostText.isEmpty()) {
        return std::nullopt; // Rust 的 EmptyHost
    }
    for (const QChar ch : hostText) {
        if (isForbiddenHostChar(ch)) {
            return std::nullopt; // Rust 的 InvalidDomainCharacter
        }
    }
    // special scheme 走 IDNA（ASCII 主机等价于逐字符小写）；非特殊 scheme 的 opaque host 原样。
    url.host = special ? Common::toAsciiLower(hostText) : hostText;
    return url;
}

namespace Uri {

std::optional<Models::Node> parseUri(const QString& uri, const QString& sourceId)
{
    const QString normalized = Common::normalizeUri(uri);
    const QString lower = Common::toAsciiLower(normalized);

    // 有些订阅导出把 VLESS/VMess 链接标成了 ss://
    if (lower.startsWith(QStringLiteral("ss://"))) {
        if (const auto node = tryParseMislabeledSsAsVless(normalized, sourceId)) {
            return node;
        }
        if (const auto node = tryParseMislabeledSsAsVmess(normalized, sourceId)) {
            return node;
        }
        return parseSs(normalized, sourceId);
    }
    if (lower.startsWith(QStringLiteral("vmess://"))) {
        return parseVmess(normalized, sourceId);
    }
    if (lower.startsWith(QStringLiteral("vless://"))) {
        return parseVless(normalized, sourceId);
    }
    if (lower.startsWith(QStringLiteral("trojan://"))) {
        return parseTrojan(normalized, sourceId);
    }
    if (lower.startsWith(QStringLiteral("hysteria2://"))
        || lower.startsWith(QStringLiteral("hy2://"))) {
        return parseHysteria2(normalized, sourceId);
    }
    if (lower.startsWith(QStringLiteral("socks5://")) || lower.startsWith(QStringLiteral("socks://"))) {
        return parseSocks(normalized, sourceId);
    }
    if (lower.startsWith(QStringLiteral("http://")) || lower.startsWith(QStringLiteral("https://"))) {
        // 只有带 userinfo（代理鉴权形态）才按代理链接处理
        const std::optional<Url> url = parseUrl(normalized);
        if (url.has_value()) {
            if (url->username.isEmpty()) {
                return std::nullopt;
            }
            return parseHttpProxy(normalized, sourceId);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace Uri

} // namespace Parser
