#include "kernel/KernelTypes.h"

namespace Kernel {

namespace {

QString asciiLower(const QString &text)
{
    QString out = text;
    for (QChar &character : out) {
        if (character.unicode() >= 'A' && character.unicode() <= 'Z') {
            character = QChar(character.unicode() + 32);
        }
    }
    return out;
}

}

QString kindToText(Kind kind)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("mihomo");
    case Kind::Xray:
        return QStringLiteral("xray");
    case Kind::SingBox:
        return QStringLiteral("sing_box");
    }
    return QStringLiteral("mihomo");
}

Kind kindFromText(const QString &text)
{
    if (text == QLatin1String("xray")) {
        return Kind::Xray;
    }
    if (text == QLatin1String("sing_box")) {
        return Kind::SingBox;
    }
    return Kind::Mihomo;
}

QString preferenceToText(Preference preference)
{
    switch (preference) {
    case Preference::Auto:
        return QStringLiteral("auto");
    case Preference::Mihomo:
        return QStringLiteral("mihomo");
    case Preference::SingBox:
        return QStringLiteral("sing_box");
    case Preference::Xray:
        return QStringLiteral("xray");
    }
    return QStringLiteral("auto");
}

Preference preferenceFromText(const QString &text)
{
    if (text == QLatin1String("mihomo")) {
        return Preference::Mihomo;
    }
    if (text == QLatin1String("sing_box")) {
        return Preference::SingBox;
    }
    if (text == QLatin1String("xray")) {
        return Preference::Xray;
    }
    return Preference::Auto;
}

std::optional<Kind> preferenceKernel(Preference preference)
{
    switch (preference) {
    case Preference::Auto:
        return std::nullopt;
    case Preference::Mihomo:
        return Kind::Mihomo;
    case Preference::SingBox:
        return Kind::SingBox;
    case Preference::Xray:
        return Kind::Xray;
    }
    return std::nullopt;
}

QString kindDisplayName(Kind kind)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("Mihomo");
    case Kind::Xray:
        return QStringLiteral("Xray");
    case Kind::SingBox:
        return QStringLiteral("Sing-box");
    }
    return QStringLiteral("Mihomo");
}

QString kindEnvironmentVariable(Kind kind)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("MIHOMO_PATH");
    case Kind::Xray:
        return QStringLiteral("XRAY_PATH");
    case Kind::SingBox:
        return QStringLiteral("SING_BOX_PATH");
    }
    return QStringLiteral("MIHOMO_PATH");
}

QString kindExecutableName(Kind kind)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("mihomo.exe");
    case Kind::Xray:
        return QStringLiteral("xray.exe");
    case Kind::SingBox:
        return QStringLiteral("sing-box.exe");
    }
    return QStringLiteral("mihomo.exe");
}

QStringList kindVersionArgs(Kind kind)
{
    return kind == Kind::Mihomo ? QStringList{QStringLiteral("-v")}
                                : QStringList{QStringLiteral("version")};
}

QString kindDirectoryName(Kind kind)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("mihomo");
    case Kind::Xray:
        return QStringLiteral("xray");
    case Kind::SingBox:
        return QStringLiteral("sing-box");
    }
    return QStringLiteral("mihomo");
}

QString kindGithubRepository(Kind kind)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("MetaCubeX/mihomo");
    case Kind::Xray:
        return QStringLiteral("XTLS/Xray-core");
    case Kind::SingBox:
        return QStringLiteral("SagerNet/sing-box");
    }
    return QStringLiteral("MetaCubeX/mihomo");
}

QString kindAssetName(Kind kind, const QString &tag)
{
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("mihomo-windows-amd64-%1.zip").arg(tag);
    case Kind::Xray:
        return QStringLiteral("Xray-windows-64.zip");
    case Kind::SingBox:
        QString stripped = tag;
        if (stripped.startsWith(QLatin1Char('v'))) {
            stripped.remove(0, 1);
        }
        return QStringLiteral("sing-box-%1-windows-amd64.zip").arg(stripped);
    }
    return QStringLiteral("mihomo-windows-amd64-%1.zip").arg(tag);
}

bool kindMatchesArchiveExecutable(Kind kind, const QString &fileName)
{
    const QString lower = asciiLower(fileName);
    switch (kind) {
    case Kind::Mihomo:
        return lower.startsWith(QLatin1String("mihomo")) && lower.endsWith(QLatin1String(".exe"));
    case Kind::Xray:
        return lower == QLatin1String("xray.exe");
    case Kind::SingBox:
        return lower == QLatin1String("sing-box.exe");
    }
    return false;
}

bool kindSupportsProxyProtocol(Kind kind, const QString &protocol)
{
    const QString lower = asciiLower(protocol);
    static const QStringList mihomoProtocols = {
        QStringLiteral("ss"),          QStringLiteral("shadowsocks"), QStringLiteral("ssr"),
        QStringLiteral("shadowsocksr"), QStringLiteral("vmess"),      QStringLiteral("vless"),
        QStringLiteral("trojan"),      QStringLiteral("hysteria"),     QStringLiteral("hysteria2"),
        QStringLiteral("hy2"),         QStringLiteral("tuic"),        QStringLiteral("anytls"),
        QStringLiteral("wireguard"),   QStringLiteral("wg"),          QStringLiteral("http"),
        QStringLiteral("https"),       QStringLiteral("socks"),       QStringLiteral("socks5"),
    };
    static const QStringList singBoxProtocols = {
        QStringLiteral("ss"),         QStringLiteral("shadowsocks"), QStringLiteral("vmess"),
        QStringLiteral("vless"),      QStringLiteral("trojan"),      QStringLiteral("hysteria2"),
        QStringLiteral("hy2"),        QStringLiteral("tuic"),        QStringLiteral("anytls"),
        QStringLiteral("wireguard"),  QStringLiteral("wg"),          QStringLiteral("http"),
        QStringLiteral("https"),      QStringLiteral("socks"),       QStringLiteral("socks5"),
    };
    static const QStringList xrayProtocols = {
        QStringLiteral("ss"),     QStringLiteral("shadowsocks"), QStringLiteral("vmess"),
        QStringLiteral("vless"),  QStringLiteral("trojan"),      QStringLiteral("http"),
        QStringLiteral("https"),  QStringLiteral("socks"),       QStringLiteral("socks5"),
    };
    switch (kind) {
    case Kind::Mihomo:
        return mihomoProtocols.contains(lower);
    case Kind::SingBox:
        return singBoxProtocols.contains(lower);
    case Kind::Xray:
        return xrayProtocols.contains(lower);
    }
    return false;
}

std::optional<Kind> preferredProxyKernel(const QString &protocol)
{
    const QString lower = asciiLower(protocol);
    if (lower == QLatin1String("hysteria2") || lower == QLatin1String("hy2")
        || lower == QLatin1String("tuic") || lower == QLatin1String("anytls")
        || lower == QLatin1String("wireguard") || lower == QLatin1String("wg")) {
        return Kind::SingBox;
    }
    if (kindSupportsProxyProtocol(Kind::Mihomo, lower)) {
        return Kind::Mihomo;
    }
    return std::nullopt;
}

}
