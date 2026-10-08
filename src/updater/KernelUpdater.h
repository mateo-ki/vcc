#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "core/JsonUtil.h"
#include "core/Result.h"
#include "kernel/KernelManager.h"
#include "kernel/KernelRelease.h"
#include "kernel/KernelSource.h"
#include "kernel/KernelTypes.h"

// 对应 Rust src/kernel/updater.rs：内核 Release 检查 → 下载 → 校验 → 解压 → 原子安装 整条链。
// 目录布局挂在 Kernel::managedKernelRoot() 下面（<root>/<kind 目录>/<exe>、
// <exe>.backup、<名>.staging.exe、<root>/.release-cache/<目录名>.json）。
namespace Updater {

/// 对应 Rust `MAX_ARCHIVE_SIZE`：150 MiB，压缩包超过这个数一律当异常处理。
constexpr quint64 kMaxArchiveSize = 150ULL * 1024ULL * 1024ULL;

/// 对应 Rust reqwest 客户端的 `user_agent("vpn-parser/0.1 kernel-updater")`。
extern const QString kUserAgent;
/// 对应 Rust `fetch_json` 里的 `.timeout(Duration::from_secs(8))`（整个请求）。
constexpr int kReleaseTimeoutMs = 8000;
/// 对应 Rust `stream_asset` 里的 `.timeout(Duration::from_secs(300))`。
constexpr int kAssetTimeoutMs = 300000;
/// 对应 Rust 客户端上的 `.connect_timeout(Duration::from_secs(4))`。
/// Rust 另有 `.read_timeout(Duration::from_secs(15))`（单次读停顿上限）与 `.http1_only()`，
/// Qt 的 QNetworkAccessManager 没有这两个开关，只能靠整体时限兜住。
constexpr int kConnectTimeoutMs = 4000;

/// 对应 Rust 私有 `GithubAsset`。`digest` 用 null QString 表示 `Option<String>` 的 `None`。
struct Asset {
    QString name;
    QString browserDownloadUrl;
    quint64 size = 0;
    QString digest;

    Json toJson() const;
    /// serde 的字段是必填的（缺 name/browser_download_url/size 就整条解析失败），照做。
    static std::optional<Asset> fromJson(const Json &value);
};

/// 对应 Rust 私有 `GithubRelease`（`fallback` 是内置兜底 Release 的标记，serde default=false）。
struct Release {
    QString tagName;
    QVector<Asset> assets;
    bool fallback = false;

    Json toJson() const;
    static std::optional<Release> fromJson(const Json &value);
};

/// 对应 Rust `Transport`：一条通道 = 一个代理（空 = 直连）加它的中文标签。
/// Rust 在这里存一个配好代理的 reqwest Client，本层用 Net::Request 的 proxyUrl 表达同一件事。
struct Transport {
    QString label;
    QString proxyUrl;

    bool operator==(const Transport &other) const
    {
        return label == other.label && proxyUrl == other.proxyUrl;
    }
};

/// 对应 Rust 的 `Arc<AtomicBool>` / `&AtomicBool`：拷贝共享同一份状态。
class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    bool cancelled() const { return flag_->load(std::memory_order_relaxed); }
    void cancel() { flag_->store(true, std::memory_order_relaxed); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

/// 对应 Rust 进度闭包 `&F where F: Fn(KernelUpdateProgress)`。
using ProgressCallback = std::function<void(const Kernel::UpdateProgress &)>;

/// 对应 Rust `proxy_candidates`：**应用内代理 → 系统代理 → 直连**，去重且末尾必是直连。
/// 两个入参都用 null QString 表示 Rust 的 `None`；`systemProxy` 由调用方给（生产走
/// `SysProxy::currentSystemProxyUrl()`，单测直接传值，不碰注册表）。
QVector<std::optional<QString>> proxyCandidates(const QString &appProxy, const QString &systemProxy);
/// 读一次 Windows 系统代理再转上面的纯函数（阻塞式注册表查询）。
QVector<std::optional<QString>> proxyCandidatesWithSystemProxy(const QString &appProxy);

/// 对应 Rust 私有 `require_sha256`：摘要是 `sha256:<64 位十六进制>` 吗？是就返回那 64 位。
Result<QString> requireSha256(const Asset &asset);

/// 对应 Rust 私有 `verify_sha256`：算一遍 SHA-256 和期望值比（忽略大小写）。
Status verifySha256(const QByteArray &bytes, const QString &expectedHex);

/// 对应 Rust 私有 `staging_name`：**必须以 .exe 结尾、不带前导点**（杀软才不会把
/// 它当数据文件；这是 2026-10-08 VirtualBox 那次排障换来的结论）。
QString stagingName(Kernel::Kind kind);

/// 对应 Rust 私有 `ensure_not_cancelled`。
Status ensureNotCancelled(const CancelToken &cancelled);

/// 对应 Rust 私有 `fallback_release`：只有 mihomo 有内置兜底版本。
std::optional<Release> fallbackRelease(Kernel::Kind kind);

/// 内核下载/更新器，对应 Rust `KernelUpdater`。
/// 这里是同步实现（`Net::send` 阻塞），调用方要放到 worker 线程里跑，
/// 和 Rust 的 async 版一一对应，只是不再有并发。
class KernelUpdater {
public:
    KernelUpdater() = default;
    KernelUpdater(QString root, QVector<Transport> transports)
        : root_(std::move(root)), transports_(std::move(transports))
    {
    }

    /// 对应 Rust `KernelUpdater::new(root, app_proxy)`：按代理候选链建通道。
    /// 会读一次系统代理（注册表），所以单测请用 `createWithCandidates`。
    static KernelUpdater create(const QString &root, const QString &appProxy);
    /// 候选链由调用方给定的版本（对应 Rust `proxy_candidates` 的输出）。
    /// `appProxy` 只用于区分标签是"应用内代理"还是"系统代理"，与 Rust 判定一致。
    static KernelUpdater createWithCandidates(const QString &root,
                                              const QString &appProxy,
                                              const QVector<std::optional<QString>> &candidates);

    const QString &root() const { return root_; }
    const QVector<Transport> &transports() const { return transports_; }

    /// 对应 Rust `check`：查官方 Release，比对本地已装版本。
    Result<Kernel::ReleaseInfo> check(const Kernel::Info &info) const;

    /// 对应 Rust `update`：`source` 决定走哪条路；`localFile` 只在 `Source::Local` 下用
    ///（null QString = Rust 的 `None`）。
    Result<Kernel::UpdateResult> update(Kernel::Kind kind,
                                        Kernel::Source source,
                                        const QString &localFile,
                                        const CancelToken &cancelled,
                                        const ProgressCallback &progress) const;

    /// 对应 Rust 私有 `install_bytes`：校验过字节之后的统一落地——解压到暂存 → 跑一次拿版本
    /// → 备份旧内核 → 替换 → 失败回滚。Rust 的单测也是直接打这一层，所以这里公开。
    /// `expectedTag` 传 null QString 表示 Rust 的 `None`（本地导入且对不上官方资产时）。
    Result<Kernel::UpdateResult> installBytes(Kernel::Kind kind,
                                              const QByteArray &bytes,
                                              const QString &expectedTag,
                                              const CancelToken &cancelled,
                                              const ProgressCallback &progress) const;

    /// 对应 Rust 私有 `release_cache_path`。
    QString releaseCachePath(Kernel::Kind kind) const;

    /// 对应 Rust 私有 `transport_order`：这个下载方式允许哪些通道下标；一条都没有就报错。
    Result<QVector<int>> transportOrder(Kernel::Source source) const;

    /// 对应 Rust `release.assets.iter().find(|a| a.name == asset_name)` 加那句 context。
    /// 纯函数，`check` 和 `update_from_network` 都走它。
    static Result<Asset> selectAsset(const Release &release, const QString &assetName);

    /// 对应 Rust `check` 里"拿到 Release 之后"的纯逻辑（选资产、要摘要、比版本、拼文案）。
    /// 公开是为了能完全不联网地测版本比对规则。
    static Result<Kernel::ReleaseInfo> releaseInfoFromRelease(const Release &release,
                                                              const Kernel::Info &info);

    /// 对应 Rust 私有 `fetch_release`。
    Result<Release> fetchRelease(Kernel::Kind kind) const;
    /// 对应 Rust 私有 `fetch_release_with_transport`：按通道依次试，成功写本地缓存；
    /// 全失败退本地缓存，再退内置兜底。返回 (Release, 赢的那条通道下标)。
    Result<std::pair<Release, int>> fetchReleaseWithTransport(Kernel::Kind kind) const;
    /// 对应 Rust 私有 `download_asset`：一条通道走到底，失败才换下一条。
    Result<QByteArray> downloadAsset(Kernel::Kind kind,
                                     const Asset &asset,
                                     Kernel::Source source,
                                     int preferred,
                                     const CancelToken &cancelled,
                                     const ProgressCallback &progress) const;

private:
    Result<Kernel::UpdateResult> installLocal(Kernel::Kind kind,
                                              const QString &localFile,
                                              const CancelToken &cancelled,
                                              const ProgressCallback &progress) const;
    Result<Kernel::UpdateResult> updateFromNetwork(Kernel::Kind kind,
                                                   Kernel::Source source,
                                                   const CancelToken &cancelled,
                                                   const ProgressCallback &progress) const;
    /// 对应 Rust 私有 `stream_asset`（单条连接的下载）。
    /// Rust 是逐块读并每块报一次进度；`Net::send` 是一次性回体，所以这里只报一次
    /// （downloaded/total/速度都由这一条连接的真实耗时算出来）。
    Result<QByteArray> streamAsset(Kernel::Kind kind,
                                   const Transport &transport,
                                   const QString &url,
                                   const QString &label,
                                   const QString &name,
                                   quint64 expected,
                                   const CancelToken &cancelled,
                                   const ProgressCallback &progress) const;
    /// 对应 Rust `fetch_json::<GithubRelease>`。
    Result<Release> fetchReleaseJson(const Transport &transport, const QString &url) const;

    QString root_;
    QVector<Transport> transports_;
};

}
