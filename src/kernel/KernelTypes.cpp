#include "kernel/KernelTypes.h"

#include <QSysInfo>

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

QString displayName(Kind kind)
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
    // Linux/macOS 上三个内核都没有扩展名。这个串同时是"内核在不在位"的判据和
    // QProcess 要拉起的文件名，写死 .exe 会让非 Windows 上永远找不到内核。
#ifdef Q_OS_WIN
    const QString suffix = QStringLiteral(".exe");
#else
    const QString suffix;
#endif
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("mihomo") + suffix;
    case Kind::Xray:
        return QStringLiteral("xray") + suffix;
    case Kind::SingBox:
        return QStringLiteral("sing-box") + suffix;
    }
    return QStringLiteral("mihomo") + suffix;
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

/// 上游 release 资产名里的"平台-架构"段。三家内核各用自己的叫法（xray 说 64、
/// 另两家说 amd64），所以不能统一成一个字符串。
/// 架构来自 QSysInfo::currentCpuArchitecture()：x86_64 / arm64 / aarch64。
QString platformAssetSuffix()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    const bool arm64 = arch.compare(QLatin1String("arm64"), Qt::CaseInsensitive) == 0
                    || arch.compare(QLatin1String("aarch64"), Qt::CaseInsensitive) == 0;
#ifdef Q_OS_WIN
    return arm64 ? QStringLiteral("windows-arm64") : QStringLiteral("windows-amd64");
#elif defined(Q_OS_MACOS)
    // macOS 段**未经真机核实**（本机没有 macOS 目标）：mihomo 用 darwin-amd64/arm64、
    // sing-box 发的是 darwin-all（通用二进制）。真上要装 macOS 时先核对资产列表。
    return arm64 ? QStringLiteral("darwin-arm64") : QStringLiteral("darwin-amd64");
#else
    return arm64 ? QStringLiteral("linux-arm64") : QStringLiteral("linux-amd64");
#endif
}

/// xray 的资产名不用 amd64 而用 64，且不带 tag（永远拉 latest release 的那一个包）。
QString xrayAssetName()
{
    const QString suffix = platformAssetSuffix();
    const int dash = suffix.indexOf(QLatin1Char('-'));
    const QString os = suffix.left(dash);
    const QString arch = suffix.mid(dash + 1) == QLatin1String("amd64")
                             ? QStringLiteral("64")
                             : suffix.mid(dash + 1);
    return QStringLiteral("Xray-%1-%2.zip").arg(os, arch);
}

QString kindAssetName(Kind kind, const QString &tag)
{
    const QString platform = platformAssetSuffix();
    switch (kind) {
    case Kind::Mihomo:
        return QStringLiteral("mihomo-%1-%2.zip").arg(platform, tag);
    case Kind::Xray:
        return xrayAssetName();
    case Kind::SingBox: {
        QString stripped = tag;
        if (stripped.startsWith(QLatin1Char('v'))) {
            stripped.remove(0, 1);
        }
#ifdef Q_OS_MACOS
        // sing-box 在 macOS 上只发一个通用二进制包，名字是 darwin-all，不分 amd64/arm64。
        return QStringLiteral("sing-box-%1-darwin-all.zip").arg(stripped);
#else
        return QStringLiteral("sing-box-%1-%2.zip").arg(stripped, platform);
#endif
    }
    }
    return QStringLiteral("mihomo-%1-%2.zip").arg(platform, tag);
}

bool kindMatchesArchiveExecutable(Kind kind, const QString &fileName)
{
    // 三家上游在 Windows 包里给的是 `mihomo.exe` / `xray.exe` / `sing-box.exe`，
    // 在 Linux/macOS 包里是同名**无扩展名**的可执行文件（mihomo 的包还会附带
    // wintun.dll / tun.dll 之类的 Windows 侧文件，靠这条后缀判据把它们排除掉）。
    const QString lower = asciiLower(fileName);
#ifdef Q_OS_WIN
    const QString suffix = QStringLiteral(".exe");
#else
    const QString suffix;
#endif
    switch (kind) {
    case Kind::Mihomo:
        // 保留上游原来的"前缀 mihomo + 平台可执行后缀"宽判据（收紧会把
        // mihomo-alpha.exe 这类包名判成不匹配），只把 .exe 换成按平台取。
        return lower.startsWith(QLatin1String("mihomo")) && lower.endsWith(suffix);
    case Kind::Xray:
        return lower == QLatin1String("xray") + suffix;
    case Kind::SingBox:
        return lower == QLatin1String("sing-box") + suffix;
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
