#pragma once

#include <QString>
#include <QStringList>

#include <optional>

#include "core/JsonUtil.h"

namespace Kernel {

enum class Kind {
    Mihomo,
    Xray,
    SingBox,
};

enum class Preference {
    Auto,
    Mihomo,
    SingBox,
    Xray,
};

/// serde 的 snake_case 取值：kernel_kind / preference 在 JSON 里长这样。
QString kindToText(Kind kind);
/// 给用户看的名字（对应 Rust `KernelKind::display_name`，kernel/mod.rs:46）。
/// 与 kindToText 不同：那个是 serde 取值（sing_box），这个是界面写法（Sing-box）。
QString displayName(Kind kind);
Kind kindFromText(const QString &text);
QString preferenceToText(Preference preference);
Preference preferenceFromText(const QString &text);
/// Auto 表示"由协议和安装状态自己决定"。
std::optional<Kind> preferenceKernel(Preference preference);

QString kindDisplayName(Kind kind);
QString kindEnvironmentVariable(Kind kind);
QString kindExecutableName(Kind kind);
QStringList kindVersionArgs(Kind kind);
QString kindDirectoryName(Kind kind);
QString kindGithubRepository(Kind kind);
/// GitHub Release 里那个 Windows x64 ZIP 的文件名。
QString kindAssetName(Kind kind, const QString &tag);
bool kindMatchesArchiveExecutable(Kind kind, const QString &fileName);
bool kindSupportsProxyProtocol(Kind kind, const QString &protocol);

/// 该协议首选哪个内核；None = 三个内核都不认这个协议。
std::optional<Kind> preferredProxyKernel(const QString &protocol);

/// 首选不可用时按 mihomo ↔ sing-box → xray 顺序退让，且必须仍支持该协议。
/// `available` 是"这个内核装好了吗"。全都不满足时返回错误。
template <typename AvailableFn>
Result<Kind> selectProxyKernelWith(const QString &protocol, AvailableFn available)
{
    const std::optional<Kind> preferred = preferredProxyKernel(protocol);
    if (!preferred.has_value()) {
        return Result<Kind>::withError(QStringLiteral("暂不支持协议: %1").arg(protocol));
    }
    const Kind fallback = *preferred == Kind::Mihomo
        ? Kind::SingBox
        : (*preferred == Kind::SingBox ? Kind::Mihomo : Kind::Xray);
    const Kind candidates[3] = {*preferred, fallback, Kind::Xray};
    for (const Kind candidate : candidates) {
        if (kindSupportsProxyProtocol(candidate, protocol) && available(candidate)) {
            return Result<Kind>::withValue(candidate);
        }
    }
    return Result<Kind>::withError(
        QStringLiteral("没有可用内核支持协议: %1").arg(protocol));
}

}
