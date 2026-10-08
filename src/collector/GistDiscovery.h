#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/JsonUtil.h"

// collector/mod.rs 的 Gist 发现部分：搜索页 HTML → gist 列表，以及搜索 URL 的拼法。
// 翻页/注册/试拉的编排在 Collector 里（discoverGists / discoverGistsFrom /
// registerGistBatch），这里只放不依赖网络的纯函数与常量。
namespace Collector {

/// collector/mod.rs:50-55。
inline constexpr const char *kGistSearchHost = "gist.github.com";
inline constexpr const char *kGistRawBase = "https://gist.githubusercontent.com";
/// 单轮扫描页数硬上限（搜索页每页 10 条，全量 100+ 页）。
inline constexpr int kGistMaxPagesHard = 300;
/// 单轮最多注册的新 gist 源数。
inline constexpr int kGistMaxNewPerRun = 2000;
/// 发现任务在任务面板里使用的伪源 id（:59）。
inline constexpr const char *kGistDiscoverySourceId = "__gist_discovery__";
/// 默认搜索词（q 参数会被 percent-encode）（:61）。
inline constexpr const char *kGistDiscoveryDefaultQuery = "vless//";

/// GUI 多选预设查询词（:66-74），对应 Rust 的 GIST_DISCOVERY_QUERIES。
const QVector<QString> &gistDiscoveryQueries();

/// 一条搜索结果里的 gist（:1048）。
struct DiscoveredGist {
    QString user;
    QString id;
    QString name;

    bool operator==(const DiscoveredGist &other) const
    {
        return user == other.user && id == other.id && name == other.name;
    }
};

/// register_gist_batch 里每条 gist 的归宿（:1055）。Failed 不进台账，下轮重来。
enum class GistOutcome {
    Registered,
    Unusable,
    Pending,
    Failed,
};

/// 一次 Gist 发现的汇总（:1063）。
struct GistDiscoverySummary {
    qint64 pagesScanned = 0;
    qint64 registered = 0;
    qint64 nodes = 0;
    qint64 skipped = 0;
    qint64 rejected = 0;
    qint64 pending = 0;
    QString stoppedReason;

    Json toJson() const;
};

/// `percent_encoding::utf8_percent_encode(query, NON_ALPHANUMERIC)`：
/// 除 [A-Za-z0-9] 之外全部按 UTF-8 字节 percent-encode，十六进制大写。
QString percentEncodeNonAlphanumeric(const QString &text);

/// 对应 Rust 的 search_url = format!("https://{host}/search?o=desc&q={}&s=updated")（:529）。
QString buildGistSearchUrl(const QString &query);

/// 对应 `Url::parse(...).query_pairs_mut().append_pair(key, value)`（:635-639）。
/// URL 解析不出来时返回 null QString（调用方报 "gist 搜索 URL 无法解析"）。
QString appendQueryParam(const QString &url, const QString &key, const QString &value);

/// `reqwest::Url::parse` 的等价物（整个 collector 共用）：只接受带 scheme 的绝对 URL。
/// 解析成功返回 true，hostOut 给出主机名（可能为空串，比如 file:///x，Rust 那边
/// host_str() 也是 None，所以调用方要把空串再映射成 null）。
bool parseAbsoluteUrl(const QString &url, QString &hostOut);

/// 从 gist 搜索结果页提取 gist（:1077）。同一块里指向 /stargazers 之类子路径的
/// href 不会当成另一个 gist（正则要求 id 后面紧跟引号）。
QVector<DiscoveredGist> extractGists(const QString &searchHtml);

} // namespace Collector
