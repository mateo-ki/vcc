#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <optional>

namespace Kernel {

/// 内核下载走哪条路（存在 `settings.kernel_source`，「下载内核」弹框里选）。
/// 五种方式都是"一条路走到底"：不同时开几条连接抢跑。
/// 对应 Rust `kernel::updater::KernelSource`。
enum class Source {
    GhProxy,
    GhFast,
    Official,
    Proxy,
    Local,
};

/// 一条下载方式的展示信息，对应 Rust `SourceOption`。
struct SourceOption {
    QString id;
    QString label;
    QString hint;
    Source source = Source::GhProxy;
};

/// 对应 Rust `pub const SOURCE_OPTIONS`：弹框里按这个顺序列五种方式。
const QList<SourceOption> &sourceOptions();

/// 对应 Rust `source_from_id`：认不出的 id（包括库里残留的旧值 "race"）落到 gh-proxy。
Source sourceFromId(const QString &id);

QString sourceId(Source source);
QString sourceLabel(Source source);

/// 对应 Rust `pub const ASSET_MIRROR_PREFIXES`：镜像前缀只代理 github.com 的文件直链。
const QList<QPair<QString, QString>> &assetMirrorPrefixes();

/// 对应 Rust 私有 `KernelSource::prefix`：这个下载方式用的镜像前缀，无则 nullopt。
std::optional<QString> sourceMirrorPrefix(Source source);

/// 对应 Rust `asset_url_for`：选定的下载方式对应的那一个 URL，一次只有一条连接。
QString assetUrlFor(Source source, const QString &directUrl);

/// 对应 Rust 私有 `KernelSource::allows`：这个方式允许哪些通道下标
/// （`transportLabel` 是 `KernelUpdater::Transport::label`，"直连" 即不经代理那一条）。
bool sourceAllowsTransport(Source source, const QString &transportLabel);

}
