#include "parser/Fingerprint.h"

#include <QStringList>

#include <initializer_list>

namespace Parser {

namespace {

QString numberToText(const Json &value)
{
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return QString::number(value.get<qint64>());
    }
    return QString::fromStdString(value.dump());
}

/// 依序取第一个命中的键：非空字符串、或任意数字、或布尔。
QString firstScalar(const Json &config, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        if (!config.is_object()) {
            continue;
        }
        const auto found = config.find(key);
        if (found == config.end()) {
            continue;
        }
        if (found->is_string()) {
            const QString text = QString::fromStdString(found->get<std::string>());
            if (!text.isEmpty()) {
                return text;
            }
            continue;
        }
        if (found->is_number()) {
            return numberToText(*found);
        }
        if (found->is_boolean()) {
            return found->get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
        }
    }
    return QString();
}

}

QString normalizeProtocol(const QString &protocol)
{
    const QString lower = protocol.toLower();
    if (lower == QLatin1String("ss") || lower == QLatin1String("shadowsocks")) {
        return QStringLiteral("shadowsocks");
    }
    if (lower == QLatin1String("ssr") || lower == QLatin1String("shadowsocksr")) {
        return QStringLiteral("shadowsocksr");
    }
    if (lower == QLatin1String("hy2") || lower == QLatin1String("hysteria2")) {
        return QStringLiteral("hysteria2");
    }
    if (lower == QLatin1String("socks") || lower == QLatin1String("socks5")) {
        return QStringLiteral("socks5");
    }
    if (lower == QLatin1String("https")) {
        return QStringLiteral("http");
    }
    return lower;
}

QString credentialToken(const QString &protocol, const Json &config)
{
    if (protocol == QLatin1String("shadowsocks")) {
        return firstScalar(config, {"cipher", "method"}) + QLatin1Char(':')
            + firstScalar(config, {"password"});
    }
    if (protocol == QLatin1String("shadowsocksr")) {
        return firstScalar(config, {"cipher", "method"}) + QLatin1Char(':')
            + firstScalar(config, {"password"}) + QLatin1Char(':')
            + firstScalar(config, {"protocol", "protocolparam"}) + QLatin1Char(':')
            + firstScalar(config, {"obfs", "obfsparam"});
    }
    if (protocol == QLatin1String("vmess") || protocol == QLatin1String("vless")) {
        return firstScalar(config, {"uuid", "id"});
    }
    if (protocol == QLatin1String("trojan") || protocol == QLatin1String("hysteria2")
        || protocol == QLatin1String("hysteria") || protocol == QLatin1String("tuic")) {
        return firstScalar(config, {"password", "auth", "auth-str", "uuid"});
    }
    if (protocol == QLatin1String("socks5") || protocol == QLatin1String("http")) {
        return firstScalar(config, {"username", "user"}) + QLatin1Char(':')
            + firstScalar(config, {"password", "pass"});
    }
    if (protocol == QLatin1String("wireguard")) {
        return firstScalar(config, {"private-key", "private_key", "public-key", "public_key"});
    }

    // 兜底：把几个稳定字段拼起来。
    const QStringList parts = {firstScalar(config, {"uuid", "id"}),
                               firstScalar(config, {"password", "auth"}),
                               firstScalar(config, {"cipher", "method"}),
                               firstScalar(config, {"username"})};
    QStringList nonEmpty;
    for (const QString &part : parts) {
        if (!part.isEmpty()) {
            nonEmpty.append(part);
        }
    }
    return nonEmpty.join(QLatin1Char(':'));
}

QString nodeFingerprint(const Models::Node &node)
{
    const QString protocol = normalizeProtocol(node.protocol);
    const QString server = node.server.trimmed().toLower();
    const QString credential = credentialToken(protocol, node.config);
    return QStringLiteral("%1|%2|%3|%4").arg(protocol, server, QString::number(node.port),
                                             credential);
}

}
