#include "core/Version.h"

#include <algorithm>
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
    return Result<Release>::withValue(std::move(release));
}

} // namespace Core
