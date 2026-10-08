#include "core/HostUtil.h"

#include <QAbstractSocket>
#include <QHostAddress>

#include <initializer_list>

namespace {

QString normalizeHost(const QString &raw)
{
    QString host = raw.trimmed();
    while (host.startsWith(QLatin1Char('[')) || host.startsWith(QLatin1Char(']'))) {
        host.remove(0, 1);
    }
    while (host.endsWith(QLatin1Char('[')) || host.endsWith(QLatin1Char(']'))) {
        host.chop(1);
    }
    return host;
}

bool isPublicIpv4(const uchar *octets)
{
    // 0.0.0.0/8、10/8、127/8、169.254/16、172.16-31、192.168/16、100.64-127（CGNAT）、
    // >=224（组播与保留段）都不是公网。
    if (octets[0] == 0) {
        return false;
    }
    if (octets[0] == 10) {
        return false;
    }
    if (octets[0] == 127) {
        return false;
    }
    if (octets[0] == 169 && octets[1] == 254) {
        return false;
    }
    if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) {
        return false;
    }
    if (octets[0] == 192 && octets[1] == 168) {
        return false;
    }
    if (octets[0] == 100 && octets[1] >= 64 && octets[1] <= 127) {
        return false;
    }
    if (octets[0] >= 224) {
        return false;
    }
    return true;
}

bool isPublicIpv6(const uchar *bytes)
{
    bool allZero = true;
    for (int index = 0; index < 16; ++index) {
        if (bytes[index] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        return false; // ::
    }
    static const uchar kLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    bool loopback = true;
    for (int index = 0; index < 16; ++index) {
        if (bytes[index] != kLoopback[index]) {
            loopback = false;
            break;
        }
    }
    if (loopback) {
        return false;
    }
    if (bytes[0] == 0xff) {
        return false; // ff00::/8 组播
    }
    if ((bytes[0] & 0xfe) == 0xfc) {
        return false; // fc00::/7 唯一本地
    }
    if (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) {
        return false; // fe80::/10 链路本地
    }
    bool v4Mapped = true;
    for (int index = 0; index < 10; ++index) {
        if (bytes[index] != 0) {
            v4Mapped = false;
            break;
        }
    }
    if (v4Mapped && bytes[10] == 0xff && bytes[11] == 0xff) {
        return isPublicIpv4(bytes + 12);
    }
    return true;
}

bool isPublicIp(const QString &host)
{
    QHostAddress address;
    if (!address.setAddress(host)) {
        return false;
    }
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        bool ok = false;
        // Qt 这里返回主机字节序，第一段就是最高位。
        const quint32 raw = address.toIPv4Address(&ok);
        if (!ok) {
            return false;
        }
        const uchar octets[4] = {uchar((raw >> 24) & 0xff), uchar((raw >> 16) & 0xff),
                                 uchar((raw >> 8) & 0xff), uchar(raw & 0xff)};
        return isPublicIpv4(octets);
    }
    const Q_IPV6ADDR v6 = address.toIPv6Address();
    uchar bytes[16];
    for (int index = 0; index < 16; ++index) {
        bytes[index] = uchar(v6[index]);
    }
    return isPublicIpv6(bytes);
}

bool looksLikeIpAddress(const QString &host)
{
    QHostAddress address;
    return address.setAddress(host);
}

bool endsWithAny(const QString &lower, std::initializer_list<const char *> suffixes)
{
    for (const char *suffix : suffixes) {
        if (lower.endsWith(QLatin1String(suffix))) {
            return true;
        }
    }
    return false;
}

}

namespace HostUtil {

bool isPublicProxyHost(const QString &host)
{
    const QString normalized = normalizeHost(host);
    if (normalized.isEmpty()) {
        return false;
    }
    const QString lower = normalized.toLower();

    static const QStringList kExplicitBad = {
        QStringLiteral("localhost"), QStringLiteral("localhost.localdomain"),
        QStringLiteral("local"),     QStringLiteral("0.0.0.0"),
        QStringLiteral("::"),        QStringLiteral("::1"),
        QStringLiteral("example.com"), QStringLiteral("example.org"),
        QStringLiteral("example.net"), QStringLiteral("test"),
        QStringLiteral("invalid"),
    };
    if (kExplicitBad.contains(lower)) {
        return false;
    }
    if (endsWithAny(lower, {".localhost", ".local", ".invalid", ".test", ".example", ".lan",
                            ".home", ".internal"})) {
        return false;
    }

    if (looksLikeIpAddress(normalized)) {
        return isPublicIp(normalized);
    }

    // 主机名必须至少含一个点：单标签名基本是坏订阅里的垃圾。
    return lower.contains(QLatin1Char('.'));
}

bool isDecoyWebsiteHost(const QString &host)
{
    const QString normalized = normalizeHost(host);
    if (normalized.isEmpty()) {
        return false;
    }
    const QString lower = normalized.toLower();

    static const char *kDecoys[] = {
        "themeforest.net",   "shopify.com",          "www.shopify.com",
        "fbi.gov",           "www.fbi.gov",          "cloudflare.com",
        "www.cloudflare.com", "blog.cloudflare.com", "google.com",
        "www.google.com",    "youtube.com",          "www.youtube.com",
        "github.com",        "www.github.com",       "microsoft.com",
        "www.microsoft.com", "apple.com",            "www.apple.com",
        "amazon.com",        "www.amazon.com",       "facebook.com",
        "www.facebook.com",  "twitter.com",          "x.com",
        "wikipedia.org",     "www.wikipedia.org",    "baidu.com",
        "www.baidu.com",     "qq.com",               "www.qq.com",
        "skk.moe",           "ip.sb",                "1.1.1.1",
        "8.8.8.8",           "8.8.4.4",              "9.9.9.9",
    };
    for (const char *decoy : kDecoys) {
        const QString suffix = QStringLiteral(".") + QLatin1String(decoy);
        if (lower == QLatin1String(decoy) || lower.endsWith(suffix)) {
            return true;
        }
    }
    if (lower.contains(QStringLiteral("fastly"))
        || lower.contains(QStringLiteral("cloudfront.net"))
        || lower.endsWith(QStringLiteral(".cdn.cloudflare.net"))
        || lower.contains(QStringLiteral("hosting-ip.com"))) {
        return true;
    }
    return false;
}

bool isQualityExportHost(const QString &host)
{
    return isPublicProxyHost(host) && !isDecoyWebsiteHost(host);
}

}
