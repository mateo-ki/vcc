#include "kernel/KernelRelease.h"

#include "kernel/KernelMessages.h"

namespace Kernel {

namespace {

/// Rust 的 `str::trim_start_matches('v')`：删掉**全部**前导 'v'，不是只删一个。
QString trimLeadingV(const QString &text)
{
    int start = 0;
    while (start < text.size() && text.at(start) == QLatin1Char('v')) {
        ++start;
    }
    return text.mid(start);
}

/// Rust 的 `char::is_ascii_alphanumeric()`：非 ASCII 字符也算分隔符。
bool isAsciiAlphanumeric(QChar character)
{
    const ushort code = character.unicode();
    return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
}

}

Json ReleaseInfo::toJson() const
{
    Json value;
    value["kind"] = kindToText(kind).toStdString();
    value["current_version"] = JsonUtil::optText(currentVersion);
    value["latest_version"] = JsonUtil::optText(latestVersion);
    value["update_available"] = updateAvailable;
    value["asset_name"] = JsonUtil::optText(assetName);
    value["message"] = message.toStdString();
    return value;
}

ReleaseInfo ReleaseInfo::fromJson(const Json &value)
{
    ReleaseInfo info;
    info.kind = kindFromText(JsonUtil::textOrEmpty(value, "kind"));
    info.currentVersion = JsonUtil::nullableText(value, "current_version");
    info.latestVersion = JsonUtil::nullableText(value, "latest_version");
    info.updateAvailable = JsonUtil::boolOr(value, "update_available", false);
    info.assetName = JsonUtil::nullableText(value, "asset_name");
    info.message = JsonUtil::textOrEmpty(value, "message");
    return info;
}

QString updateStageToText(UpdateStage stage)
{
    switch (stage) {
    case UpdateStage::Checking:
        return QStringLiteral("checking");
    case UpdateStage::Downloading:
        return QStringLiteral("downloading");
    case UpdateStage::Verifying:
        return QStringLiteral("verifying");
    case UpdateStage::Extracting:
        return QStringLiteral("extracting");
    case UpdateStage::Installing:
        return QStringLiteral("installing");
    }
    return QStringLiteral("checking");
}

UpdateStage updateStageFromText(const QString &text)
{
    if (text == QLatin1String("downloading")) {
        return UpdateStage::Downloading;
    }
    if (text == QLatin1String("verifying")) {
        return UpdateStage::Verifying;
    }
    if (text == QLatin1String("extracting")) {
        return UpdateStage::Extracting;
    }
    if (text == QLatin1String("installing")) {
        return UpdateStage::Installing;
    }
    return UpdateStage::Checking;
}

QString updateStageLabel(UpdateStage stage)
{
    switch (stage) {
    case UpdateStage::Checking:
        return Messages::stageChecking();
    case UpdateStage::Downloading:
        return Messages::stageDownloading();
    case UpdateStage::Verifying:
        return Messages::stageVerifying();
    case UpdateStage::Extracting:
        return Messages::stageExtracting();
    case UpdateStage::Installing:
        return Messages::stageInstalling();
    }
    return Messages::stageChecking();
}

float UpdateProgress::fraction() const
{
    if (total == 0) {
        return 0.0F;
    }
    const double raw = double(downloaded) / double(total);
    return float(raw < 0.0 ? 0.0 : (raw > 1.0 ? 1.0 : raw));
}

Json UpdateProgress::toJson() const
{
    Json value;
    value["kind"] = kindToText(kind).toStdString();
    value["stage"] = updateStageToText(stage).toStdString();
    value["downloaded"] = downloaded;
    value["total"] = total;
    value["bytes_per_second"] = bytesPerSecond;
    value["message"] = message.toStdString();
    return value;
}

UpdateProgress UpdateProgress::fromJson(const Json &value)
{
    UpdateProgress progress;
    progress.kind = kindFromText(JsonUtil::textOrEmpty(value, "kind"));
    progress.stage = updateStageFromText(JsonUtil::textOrEmpty(value, "stage"));
    progress.downloaded = quint64(JsonUtil::intOr(value, "downloaded", 0));
    progress.total = quint64(JsonUtil::intOr(value, "total", 0));
    progress.bytesPerSecond = JsonUtil::doubleOr(value, "bytes_per_second", 0.0);
    progress.message = JsonUtil::textOrEmpty(value, "message");
    return progress;
}

Json UpdateResult::toJson() const
{
    Json value;
    value["kind"] = kindToText(kind).toStdString();
    value["version"] = version.toStdString();
    value["path"] = path.toStdString();
    value["backup_path"] = JsonUtil::optText(backupPath);
    value["message"] = message.toStdString();
    return value;
}

UpdateResult UpdateResult::fromJson(const Json &value)
{
    UpdateResult result;
    result.kind = kindFromText(JsonUtil::textOrEmpty(value, "kind"));
    result.version = JsonUtil::textOrEmpty(value, "version");
    result.path = JsonUtil::textOrEmpty(value, "path");
    result.backupPath = JsonUtil::nullableText(value, "backup_path");
    result.message = JsonUtil::textOrEmpty(value, "message");
    return result;
}

bool versionMatches(const QString &output, const QString &tag)
{
    const QString version = trimLeadingV(tag);
    // Rust 的 `split(predicate)` 保留空段，段数 = 分隔符数 + 1，这里照做。
    QString segment;
    QStringList segments;
    for (const QChar &character : output) {
        if (isAsciiAlphanumeric(character) || character == QLatin1Char('.')) {
            segment.append(character);
        } else {
            segments.push_back(segment);
            segment.clear();
        }
    }
    segments.push_back(segment);
    for (const QString &part : segments) {
        if (trimLeadingV(part) == version) {
            return true;
        }
    }
    return false;
}

}
