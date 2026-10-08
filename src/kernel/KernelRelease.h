#pragma once

#include <QString>

#include <cstdint>

#include "core/JsonUtil.h"
#include "kernel/KernelTypes.h"

namespace Kernel {

/// 对应 Rust `kernel::updater::KernelReleaseInfo`（mod.rs 里 `pub use` 出去的那个）。
/// 字段名与 serde 的 snake_case 输出逐字一致。
struct ReleaseInfo {
    Kind kind = Kind::Mihomo;
    /// null QString = Rust 的 `None`。
    QString currentVersion;
    QString latestVersion;
    bool updateAvailable = false;
    QString assetName;
    QString message;

    Json toJson() const;
    static ReleaseInfo fromJson(const Json &value);
};

/// 对应 Rust `KernelUpdateStage`。
enum class UpdateStage {
    Checking,
    Downloading,
    Verifying,
    Extracting,
    Installing,
};

/// serde 的 snake_case 取值（"checking" …）。
QString updateStageToText(UpdateStage stage);
UpdateStage updateStageFromText(const QString &text);
/// 中文阶段名，对应 Rust `KernelUpdateStage::label`。
QString updateStageLabel(UpdateStage stage);

/// 对应 Rust `KernelUpdateProgress`。
struct UpdateProgress {
    Kind kind = Kind::Mihomo;
    UpdateStage stage = UpdateStage::Checking;
    /// Rust 的 u64：下载字节数，0 是合法值（"刚开始"）。
    quint64 downloaded = 0;
    quint64 total = 0;
    double bytesPerSecond = 0.0;
    QString message;

    /// 对应 Rust `fraction()`：total 为 0 时给 0，结果夹到 [0, 1]。
    float fraction() const;

    Json toJson() const;
    static UpdateProgress fromJson(const Json &value);
};

/// 对应 Rust `KernelUpdateResult`。
struct UpdateResult {
    Kind kind = Kind::Mihomo;
    QString version;
    /// 已安装内核的路径（Rust 是 PathBuf，JSON 里就是字符串）。
    QString path;
    /// null QString = `Option<PathBuf>` 的 `None`。
    QString backupPath;
    QString message;

    Json toJson() const;
    static UpdateResult fromJson(const Json &value);
};

/// 对应 Rust updater 的私有 `version_matches`：把版本行里任意一段和 Release tag 对上。
/// 构造 `ReleaseInfo::updateAvailable` 和安装校验都靠它，所以放进本模块而不是下载模块。
bool versionMatches(const QString &output, const QString &tag);

}
