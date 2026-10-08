#include "geo/Geo.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include "core/Log.h"
#include "geo/GeoMessages.h"
#include "kernel/KernelManager.h"
#include "net/HttpClient.h"
#include "sysproxy/Format.h"

namespace {

/// 下载候选，按实测速度排序：镜像在前，官方直连兜底。
const QVector<QString> kUpstreams = {
    QStringLiteral("https://gh-proxy.com/https://github.com/Loyalsoldier/v2ray-rules-dat/"
                   "releases/latest/download/"),
    QStringLiteral("https://ghfast.top/https://github.com/Loyalsoldier/v2ray-rules-dat/"
                   "releases/latest/download/"),
    QStringLiteral("https://github.com/Loyalsoldier/v2ray-rules-dat/"
                   "releases/latest/download/"),
};

/// Rust 的 format!("{:.1}MB")：小数点固定是 '.'，不受 locale 影响。
QString humanMb(quint64 bytes)
{
    return QString::asprintf("%.1fMB", double(bytes) / 1048576.0);
}

/// 对应 Rust 的 url.split('/').nth(2) + trim_start_matches("gh-proxy.com@")。
QString hostOf(const QString &url)
{
    const QStringList parts = url.split(QLatin1Char('/'));
    QString host = parts.size() > 2 ? parts.at(2) : QStringLiteral("?");
    const QString prefix = QStringLiteral("gh-proxy.com@");
    while (host.startsWith(prefix)) {
        host.remove(0, prefix.length());
    }
    return host;
}

/// 全角标点不能由我手打（会静默丢字），一律用码点。
QString joinReports(const QStringList &parts)
{
    return parts.join(QChar(0xFF0C));
}

QString joinFailures(const QStringList &parts)
{
    return parts.join(QChar(0xFF1B));
}

Result<QByteArray> downloadInto(const QString &proxyUrl, const QString &url, const QString &target)
{
    Net::Request request;
    request.url = url;
    request.method = QByteArrayLiteral("GET");
    request.userAgent = QStringLiteral("vpn-parser geo-updater");
    request.connectTimeoutMs = 6000;
    request.timeoutMs = 180000;
    request.proxyUrl = proxyUrl;

    const Result<Net::Response> sent = Net::send(request);
    if (sent.isErr()) {
        return Result<QByteArray>::withError(sent.error());
    }
    const Net::Response response = sent.value();
    if (!response.isSuccess()) {
        return Result<QByteArray>::withError(QStringLiteral("HTTP %1").arg(response.status));
    }
    const Result<quint64> validated = Geo::validateGeoBody(response.body);
    if (validated.isErr()) {
        return Result<QByteArray>::withError(validated.error());
    }
    // 先落到 .part 再替换：内核永远不会看到写了一半的库。
    const QString part = target + QStringLiteral(".part");
    QFile file(part);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<QByteArray>::withError(
            QStringLiteral("%1: %2").arg(part, file.errorString()));
    }
    file.write(response.body);
    file.close();
    QFile::remove(target);
    if (!file.rename(target)) {
        return Result<QByteArray>::withError(
            QStringLiteral("%1 -> %2: %3").arg(part, target, file.errorString()));
    }
    return Result<QByteArray>::withValue(response.body);
}

}

namespace Geo {

const QVector<QString> kGeoFiles = {QStringLiteral("geosite.dat"), QStringLiteral("geoip.dat")};

QString mihomoDir()
{
    return QDir(Kernel::managedKernelRoot()).filePath(QStringLiteral("mihomo"));
}

QVector<FileState> geoState(const QString &dir)
{
    QVector<FileState> states;
    for (const QString &name : kGeoFiles) {
        FileState state;
        state.name = name;
        const QFileInfo info(QDir(dir).filePath(name));
        if (info.exists() && info.size() >= qint64(kMinGeoBytes)) {
            state.usableBytes = quint64(info.size());
        }
        states.push_back(state);
    }
    return states;
}

bool geoReady(const QString &dir)
{
    for (const FileState &state : geoState(dir)) {
        if (!state.usableBytes.has_value()) {
            return false;
        }
    }
    return true;
}

Result<quint64> validateGeoBody(const QByteArray &bytes)
{
    if (quint64(bytes.size()) < kMinGeoBytes) {
        return Result<quint64>::withError(
            SysProxy::fillTemplate(Messages::geoTruncated(), {}, {QString::number(bytes.size())}));
    }
    if (bytes.startsWith("<")) {
        return Result<quint64>::withError(Messages::geoHtmlBody());
    }
    // geo 数据是 protobuf：第一个字节是 field1+LD 的 tag (0x0a)。
    const int first = uchar(bytes.at(0));
    if (first != 0x0a) {
        return Result<quint64>::withError(
            SysProxy::fillTemplate(Messages::geoBadMagic(),
                                   {{QStringLiteral("{:#04x}"),
                                     QStringLiteral("0x%1").arg(first, 2, 16, QLatin1Char('0'))}}));
    }
    return Result<quint64>::withValue(quint64(bytes.size()));
}

Result<QString> provision(const QString &dir, const QString &proxyUrl, bool force)
{
    QDir().mkpath(dir);
    QStringList report;
    for (const QString &name : kGeoFiles) {
        const QString target = QDir(dir).filePath(name);
        const QFileInfo info(target);
        const std::optional<quint64> existing =
            info.exists() ? std::optional<quint64>(quint64(info.size())) : std::nullopt;
        if (!force && existing.has_value() && *existing >= kMinGeoBytes) {
            report.push_back(SysProxy::fillTemplate(Messages::geoAlreadyReady(),
                                                    {{QStringLiteral("{name}"), name}},
                                                    {humanMb(*existing)}));
            continue;
        }
        QStringList failures;
        bool done = false;
        for (const QString &upstream : kUpstreams) {
            const QString url = upstream + name;
            const Result<QByteArray> downloaded = downloadInto(proxyUrl, url, target);
            if (downloaded.isOk()) {
                report.push_back(SysProxy::fillTemplate(
                    Messages::geoUpdated(), {{QStringLiteral("{name}"), name}},
                    {humanMb(quint64(downloaded.value().size())), hostOf(url)}));
                done = true;
                break;
            }
            failures.push_back(QStringLiteral("%1: %2").arg(hostOf(url), downloaded.error()));
        }
        if (!done) {
            return Result<QString>::withError(
                SysProxy::fillTemplate(Messages::geoAllSourcesFailed(),
                                       {{QStringLiteral("{name}"), name}},
                                       {joinFailures(failures)}));
        }
    }
    return Result<QString>::withValue(joinReports(report));
}

}
