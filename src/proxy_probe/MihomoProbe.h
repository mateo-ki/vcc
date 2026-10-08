#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>
#include <utility>

#include "core/JsonUtil.h"
#include "core/Result.h"
#include "models/Node.h"
#include "proxy_probe/JoinWindow.h"
#include "proxy_probe/ProbeTypes.h"

class QSemaphore;

namespace ProxyProbe {

/// 对应 Rust proxy_probe.rs 的 mihomo 真实测速实现：一个常驻 mihomo 进程挂整批代理，
/// 经控制器 `/proxies/<name>/delay` 逐个打真实流量。
/// 六个入口里 `verifyNodesWithMihomo(nodes, timeoutMs, progress, limit, batchSize,
/// conditionalIsolation)` 是 ProbeTypes.h 冻结的合约函数（= Rust 的
/// `verify_nodes_with_mihomo_progress_limited`），下面这两个是 Rust 另外两个公开入口。

/// Rust 的 `verify_nodes_with_mihomo`：无进度，256 一批，先整批 `mihomo -t` 二分隔离。
Result<QVector<ProbeResult>> verifyNodesWithMihomo(const QVector<Models::Node> &nodes,
                                                   int timeoutMs);

/// Rust 的 `verify_nodes_with_mihomo_progress`：自带 96 份网络额度。
Result<QVector<ProbeResult>> verifyNodesWithMihomoProgress(const QVector<Models::Node> &nodes,
                                                           int timeoutMs,
                                                           const ProgressFn &progress);

/// Rust 的 `PreparedProxy`：一个节点已经转成 clash proxy 对象、并且拿到了这次测速用的
/// 唯一名字（`probe-<index>-<id>`：带 index 是因为不同节点的 name 可能重名，控制器只按
/// 名字认代理）。
struct PreparedProxy {
    qsizetype index = -1;
    QString id;
    QString name;
    Json config = Json::object();

    bool operator<(const PreparedProxy &other) const { return index < other.index; }
};

/// Rust 的 `prepare_proxies`：返回 (可测的代理, 与入参等长的结果槽位)。
/// 槽位里非空的只有"协议 mihomo 不认识"的那些，其余留给探针填。
struct PreparedProxies {
    QVector<PreparedProxy> prepared;
    QVector<std::optional<ProbeResult>> results;
};
PreparedProxies prepareProxies(const QVector<Models::Node> &nodes);

/// Rust 测速专用的 `build_mihomo_config`（不是 Proxy::buildMihomoConfig：那份是给常驻
/// 代理用的，这份只带代理表 + 一个 select 组 + MATCH 规则，控制器端口由调用方占好）。
Json buildBatchProbeConfig(const QVector<PreparedProxy> &prepared, quint16 controllerPort);

/// Rust 的 `nodes.chunks(batch_size.max(1))` 在 ProbeKernel.h（三个内核共用）。

/// 一个批次的回传，对应 Rust 的 `(ids, outcome)`：ids 是批次失败时要逐个补的节点 id。
struct MihomoBatchOutcome {
    QVector<QString> ids;
    Result<QVector<ProbeResult>> results;
};

/// Rust 里 `match outcome { Ok(v) => results.extend(v), Err(e) => ids.map(补一条失败) }`。
/// 抽出来是为了能单测"批次失败绝不吞掉节点"。
void appendBatchOutcome(QVector<ProbeResult> *results, const MihomoBatchOutcome &outcome,
                        const QString &batchError);

/// `/proxies/<name>/delay?url=..&timeout=..`：路径分量按 NON_ALPHANUMERIC 全编码，
/// query 按 form-urlencoded。纯函数，测试直接断言字符串。
QString mihomoDelayUrl(const QString &proxyName, quint16 controllerPort, const QString &targetUrl,
                       const QString &timeoutMs);

/// `{delay} ms (真实代理 · {target})` / `真实代理测试失败: {error}`。
QString delaySuccessMessage(qint64 delayMs, const QString &targetName);
QString delayFailureMessage(const QString &error, const QString &targetName);
/// `{target_name}: 未返回有效延迟`
QString delayMissingValueMessage(const QString &targetName);

/// `mihomo -t` 校验器：返回 nullopt = 这批配置合法，非 nullopt = 报错文本（已压缩）。
/// 注入进来是为了单测二分隔离的编排而不起真的内核进程。
using BatchValidator =
    std::function<Result<std::optional<QString>>(const QVector<PreparedProxy> &batch)>;

struct IsolationOutcome {
    QVector<PreparedProxy> valid;
    QVector<std::pair<PreparedProxy, QString>> rejected;
};

/// Rust 的 `isolate_invalid_proxies`：整层并发校验，只有失败的那批才二分下沉，
/// 层数是 O(log n)。valid/rejected 都按 index 排好。
Result<IsolationOutcome> isolateInvalidProxies(const QVector<PreparedProxy> &prepared,
                                               const BatchValidator &validator);

/// 真的起 `mihomo -t -d <dir> -f <file>` 去校验一批配置（Rust 的 validate_proxy_batch）。
Result<std::optional<QString>> validateProxyBatchWithMihomo(const QVector<PreparedProxy> &batch,
                                                             const QString &mihomoPath,
                                                             const QString &workDir);

/// 单批（一个 mihomo 进程）的完整流程，对应 Rust 的 `verify_nodes_with_mihomo_path`。
/// mihomoPath 由调用方给（Rust 在 limited 里 find_mihomo_executable 一次，然后每批 clone）。
Result<QVector<ProbeResult>>
verifyNodesWithMihomoPath(const QVector<Models::Node> &nodes, int timeoutMs,
                          const QString &mihomoPath, const ProgressFn &progress,
                          QSemaphore &networkLimit, bool conditionalIsolation);

}
