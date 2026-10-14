#include "core/Version.h"

#include <algorithm>
#include <QRegularExpression>
#include <QStringList>

#include "core/JsonUtil.h"

namespace Core {

QString appVersion()
{
#ifdef VCC_VERSION_STRING
    return QStringLiteral(VCC_VERSION_STRING);
#else
    // 没定义就等于构建配置出错，但别让它变成"关于框里显示空版本"。
    return QStringLiteral("0.0.0");
#endif
}

QString releaseVersion(const QString &tag)
{
    const QString trimmed = tag.trimmed();
    return trimmed.startsWith(QLatin1Char('v')) || trimmed.startsWith(QLatin1Char('V'))
        ? trimmed.mid(1)
        : trimmed;
}

int compareVersions(const QString &left, const QString &right)
{
    const QStringList leftParts = releaseVersion(left).split(QLatin1Char('.'));
    const QStringList rightParts = releaseVersion(right).split(QLatin1Char('.'));
    const int count = std::max(leftParts.size(), rightParts.size());
    for (int index = 0; index < count; ++index) {
        const auto part = [](const QStringList &parts, int at) {
            if (at >= parts.size()) {
                return 0;
            }
            bool ok = false;
            const int value = parts.at(at).trimmed().toInt(&ok);
            return ok ? value : 0;
        };
        const int a = part(leftParts, index);
        const int b = part(rightParts, index);
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    return 0;
}

bool isUpdateAvailable(const QString &latestTag, const QString &currentVersion)
{
    const QString latest = releaseVersion(latestTag);
    if (latest.isEmpty()) {
        return false;
    }
    return compareVersions(latest, currentVersion) > 0;
}

Result<Release> releaseFromJsonText(const QString &jsonText)
{
    const Result<Json> parsed = JsonUtil::parse(jsonText);
    if (parsed.isErr()) {
        return Result<Release>::withError(parsed.error());
    }
    Release release;
    release.version = releaseVersion(JsonUtil::textOrEmpty(parsed.value(), "tag_name"));
    release.url = JsonUtil::textOrEmpty(parsed.value(), "html_url");
    if (release.version.isEmpty()) {
        // GitHub 对"仓库还没有 release"回的是 404 + 一条 message，这里统一成一句人话，
        // 免得上层把 JSON 原文糊到界面上。
        const QString message = JsonUtil::textOrEmpty(parsed.value(), "message");
        return Result<Release>::withError(message.isEmpty()
            ? QStringLiteral("release info has no tag_name")
            : message);
    }

    // 安装包资产：优先名字以 VCC 开头的 .exe（我们自己的产物就是这个形状），其次挑
    // 最大的 .exe。一个都没有就留空 —— 界面上"下载"那颗因此保持灰着，比让用户点下去
    // 然后报"没东西可下"诚实。
    const auto assets = parsed.value().find("assets");
    if (assets != parsed.value().end() && assets->is_array()) {
        struct Candidate {
            QString name;
            QString url;
            QString digest;
            qint64 size = -1;
        };
        Candidate ours;
        Candidate any;
        for (const Json &item : *assets) {
            const QString name = JsonUtil::textOrEmpty(item, "name");
            const QString url = JsonUtil::textOrEmpty(item, "browser_download_url");
            if (url.isEmpty() || !name.toLower().endsWith(QLatin1String(".exe"))) {
                continue;
            }
            Candidate candidate;
            candidate.name = name;
            candidate.url = url;
            candidate.digest = JsonUtil::textOrEmpty(item, "digest");
            const auto sizeField = item.find("size");
            candidate.size = (sizeField != item.end() && sizeField->is_number())
                ? sizeField->get<qint64>()
                : 0;
            if (name.toLower().startsWith(QLatin1String("vcc-"))
                && candidate.size >= ours.size) {
                ours = candidate;
            }
            if (candidate.size > any.size) {
                any = candidate;
            }
        }
        const Candidate &chosen = ours.url.isEmpty() ? any : ours;
        if (!chosen.url.isEmpty()) {
            release.assetName = chosen.name;
            release.assetUrl = chosen.url;
            release.assetDigest = chosen.digest;
            release.assetSize = chosen.size > 0 ? quint64(chosen.size) : 0;
        }
    }
    return Result<Release>::withValue(std::move(release));
}

QString safeInstallerFileName(const QString &assetName, const QString &version)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,126}\\.exe$"));
    if (safe.match(assetName).hasMatch() && !assetName.contains(QLatin1String(".."))) {
        return assetName;
    }
    const QString digitsOnly = QString(version).replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z.-]")),
                                                         QStringLiteral("-"));
    return QStringLiteral("VCC-%1-win64.exe").arg(digitsOnly.isEmpty() ? QStringLiteral("update") : digitsOnly);
}

} // namespace Core
