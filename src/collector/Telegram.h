#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/Result.h"
#include "models/Node.h"
#include "models/Source.h"
#include "models/Types.h"

class Database;

// 对应 Rust src/collector/telegram.rs：Telegram 频道 → 节点。
//
// 走的是 t.me 的**公开网页预览**（`https://t.me/s/<频道名>`）：不需要账号、不需要
// api_id/api_hash，代价是只能读到公开频道，而且一页只有最近约 20 条消息。
//
// 实测数据（这台机器，2026-10-07）：
// - `t.me:443` 直连 TCP 握手超时，走本地代理才通 —— 所以这条通道必须挂代理跑；
//   预览页给的证书是真货（`CN=*.t.me`，GoDaddy G2 签发），TLS 校验不用放宽；
// - `https://t.me/s/v2node` → 200、115 KB、19 条正文，其中一条 3.7 KB 的正文里有
//   15 个 `ss://` + 8 个 `vless://`，另有 1 个 `raw.githubusercontent.com/.../base64.txt` 订阅链接；
// - 不存在的频道一律返回 9.6 KB 的空壳页、0 条正文，能一眼分辨；
// - `?before=<消息号>` 确实往前翻，第 1 页 12610..12635、第 2 页 12582..12609，零重叠。
//
// Rust 侧这些是 `impl super::Collector` 的方法；Collector 的移植还没落地，所以这里把它们
// 做成接收 TelegramContext 的自由函数：db / 任务面板 / 出口（代理）都由 Collector 注入，
// 这样页面解析、实体解码、去重、累积入库这些语义可以完全不碰网络地测。
namespace Collector::Telegram {

/// 第一次拉某个频道：往前翻 3 页（约 60 条消息），把历史里发过的节点一次性补上。
inline constexpr int kSeedPages = 3;
/// 之后每次只取最新一页。频道节点是零散发布的，旧节点上一轮已经落库了，
/// 反复翻页只会让一次拉取慢好几秒。
inline constexpr int kFollowPages = 1;
/// 正文里的订阅链接最多跟进几个。闲聊频道能甩几十个外链，全跟会把预算花光。
inline constexpr int kMaxSubUrls = 8;
/// 每个订阅链接单独给的时间上限；整轮仍然受该源的总预算约束。
inline constexpr std::chrono::milliseconds kPerSubUrl{8000};

/// 对应 Rust tasks::TaskPhase 里这一路用到的三档；Collector 侧换成自己的枚举即可。
enum class TaskPhase {
    Downloading,
    Parsing,
    Writing,
};

/// 一次成功下载的结果（还没解析/入库），对应 collector/mod.rs 的 `Downloaded`。
/// etag / lastModified 用 null QString 表示 Rust 的 `None`。
struct Downloaded {
    QByteArray body;
    QString finalUrl;
    QString etag;
    QString lastModified;
};

/// 一页/一个订阅链接的下载，对应 Rust `fetch_page` 里那次 `download_candidate` 调用：
/// `FetchCandidate { url, host: url_host(url), mirror: false }`，头部预算
/// `RESPONSE_HEADER_TIMEOUT`（6s）也由 Collector 侧带进去。
/// `Ok(None)` 表示服务端回了 304；Err 的字符串就是 Rust 的失败消息原文。
using PageFetcher = std::function<Result<std::optional<Downloaded>>(const Models::Source &source,
                                                                    const QString &url,
                                                                    const QString &taskId,
                                                                    std::chrono::milliseconds budget)>;

/// Rust 的 `self.tasks.phase(task_id, &source.id, phase)`；未设置就是不汇报。
using PhaseReporter =
    std::function<void(const QString &taskId, const QString &sourceId, TaskPhase phase)>;

/// Rust 的 `self.tasks.nodes(task_id, &source.id, round)`。
using NodesReporter =
    std::function<void(const QString &taskId, const QString &sourceId, int nodeCount)>;

/// Rust 的 `crate::system_proxy::current_system_proxy_url()`：null QString = `None`。
using SystemProxyUrlResolver = std::function<QString()>;

/// Rust 的 `self.with_proxy(&url)`：给这一轮换一个挂代理的出口，失败返回 Err
/// （Rust 里是 anyhow 错，走到「系统代理地址无法用作出口」那条消息）。
using ApplyProxy = std::function<Result<PageFetcher>(const QString &proxyUrl)>;

/// Rust 的 `parse_subscription(&text, Some(&source_id))`（外面套 PARSE_LIMIT 与
/// spawn_blocking）。未设置时直接用 Parser::parseSubscription。
using SubscriptionParser =
    std::function<QVector<Models::Node>(const QString &text, const QString &sourceId)>;

/// 一轮 Telegram 拉取需要的上下文，对应 Rust 方法里的 `&self`（Collector）。
///
/// Collector 侧的接线（照 Rust collector/mod.rs::fetch_source_tracked）：
///   1. `channelName(Parser::expandUrlTemplate(source.urlTemplate))` 非 null → 这一路；
///   2. `fetchPage` = `downloadCandidate(source, FetchCandidate{url, urlHost(url), /*mirror=*/false},
///      taskId, budget, RESPONSE_HEADER_TIMEOUT)`，`Downloaded` 的四项从 mod.rs 那份结构搬过来；
///   3. `applyProxy` = `with_proxy(proxyUrl)` 的等价物（换一份挂代理的 client → 新 transport）；
///   4. `systemProxyUrl` = `SysProxy::currentSystemProxyUrl()`；
///   5. `phase` / `nodes` = `tasks.phase(...)` / `tasks.nodes(...)`；
///   6. `parse` 不设就是 `Parser::parseSubscription`，要复刻 PARSE_LIMIT(4) + 工作线程就在
///      这里包一层；重试与 fail_streak 记账仍在 Collector 的 fetch_source_in_task 里。
struct TelegramContext {
    std::shared_ptr<Database> db;
    /// Rust 的 `self.via_proxy`：这一份 collector 的出站是不是显式走代理。
    bool viaProxy = false;
    PageFetcher fetchPage;
    ApplyProxy applyProxy;
    SystemProxyUrlResolver systemProxyUrl;
    PhaseReporter phase;
    NodesReporter nodes;
    SubscriptionParser parse;
};

/// `https://t.me/s/foo` / `t.me/foo` / `@foo` → `"foo"`；不是频道源就返回 null QString。
/// 永久链接（`t.me/foo/12345`）也归到频道名 `foo`；私有频道（`t.me/+xxxx`、
/// `t.me/joinchat/xxxx`）网页预览根本读不到，所以不当频道源认。
QString channelName(const QString &url);

/// Telegram 用户名规则：首字符是字母，总长 5–32，只允许字母数字下划线。
/// 返回 null QString = Rust 的 `None`（Rust 私有 fn，开放出来才好测）。
QString validChannel(const QString &name);

/// 预览页里每条消息的正文（还是 HTML 片段，未清洗）。
QStringList messageBlocks(const QString &html);

/// 这一页出现过的全部消息号，升序。翻页要拿最老的那条当游标。
QVector<qulonglong> messageIds(const QString &html);

/// 把消息正文拍成纯文本：`<br/>` 转换行，其余标签丢掉，实体最后才解 ——
/// 先解实体会把正文里的 `&lt;b&gt;` 当成标签吃掉。
QString plainText(const QString &block);

QString decodeEntities(const QString &text);

/// payloads 的两类载荷：裸节点 URI 与疑似订阅端点的 http(s) 链接。
struct Payloads {
    QStringList uris;
    QStringList urls;
};

/// 从消息正文里分出两类载荷：裸节点 URI，和疑似订阅端点的 http(s) 链接。
/// 链接同时从可见文字和 `href` 里取 —— 频道常把真链接藏在"点击下方按钮打开"后面。
Payloads payloads(const QStringList &blocks);

/// 去重追加：两端裁空白、去掉尾部收尾标点，空串丢弃，已存在的不重复加。
void pushUnique(QStringList &list, const QString &value);

/// 订阅候选链接：要真的像端点。Telegram 自己的域名（永久链接、@提及、转发）先排掉。
/// 返回 null QString = Rust 的 `None`。
QString subscriptionUrl(const QString &raw);

/// 订阅端点的“像不像”打分（Rust 返回 u8，取值范围 0–14）。
int scoreSubscriptionUrl(const QString &url);

/// 裸节点 URI 拼成解析器认识的正文（一条一行）。
QString rawUriText(const QStringList &uris);

/// Telegram 频道源的一轮拉取。返回的 FetchResult 和普通源同构，所以任务面板、
/// 健康度时间线、连续失败降级那些逻辑都不用改；重试和 fail_streak 记账也仍然由
/// Collector 的 `fetch_source_in_task` 统一负责，这里不自己调。
Models::FetchResult fetchTelegramSource(const TelegramContext &context, const Models::Source &source,
                                        const QString &channel, const QString &taskId,
                                        std::chrono::milliseconds budget);

/// 一轮拉取的主体，`egress` 是写进消息里的出口名（“应用内代理”/“系统代理”）。
Models::FetchResult fetchChannel(const TelegramContext &context, const Models::Source &source,
                                 const QString &channel, const QString &taskId,
                                 std::chrono::milliseconds budget, const QString &egress);

/// 下载一页/一个订阅链接。`Ok(None)` 表示服务端回了 304。
/// 并发闸和条件请求都在 Collector 的 download_candidate 里，这里不重复申请许可。
Result<std::optional<Downloaded>> fetchPage(const TelegramContext &context,
                                            const Models::Source &source, const QString &url,
                                            const QString &taskId,
                                            std::chrono::milliseconds budget);

/// 解析一批订阅正文。Rust 在 PARSE_LIMIT 许可下 spawn_blocking，join 失败返回 `None`；
/// 这里同步执行，等价于永远 `Some`。
QVector<Models::Node> parseBlocking(const TelegramContext &context, const QString &text,
                                    const QString &sourceId);

/// 失败收口：把原因写进 last_status（node_count 记 0），返回 success=false 的 FetchResult。
Models::FetchResult telegramFailure(const TelegramContext &context, const Models::Source &source,
                                    const QString &resolvedUrl, const QString &message);

} // namespace Collector::Telegram
