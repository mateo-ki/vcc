#pragma once

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

#include "core/Result.h"
#include "db/ConnectionPool.h"
#include "models/Node.h"
#include "models/Source.h"
#include "models/Settings.h"
#include "models/Types.h"

/// 未分组节点库的哨兵 id。
inline const QString kUngroupedSourceId = QStringLiteral("__ungrouped__");

class Database;

/// 主库 + 按订阅源分片的节点库。对应 Rust 的 `Database`（Clone 共享同一份分库缓存）。
class Database : public std::enable_shared_from_this<Database> {
public:
    /// 打开（必要时创建）主库并完成迁移。
    static Result<std::shared_ptr<Database>> connect(const QString &dbPath);

    QString mainPath() const { return mainPath_; }
    QString nodeRoot() const { return nodeRoot_; }
    std::shared_ptr<ConnectionPool> mainPool() const { return main_; }

    /// 分库文件名（不含扩展名）。source_id 可能来自 HTTP 查询参数，因此只接受
    /// UUID/哨兵形状的字符集；带 `/`、`\`、`..` 的一律归到未分组库，杜绝
    /// nodeRoot_.join 跑到目录外面去建库。
    static QString nodeDbStem(const QString &sourceId);
    static QString nodePoolCacheKey(const QString &sourceId);
    QString nodeDbPath(const QString &sourceId) const;

    /// 取（并缓存）某源的节点库连接池。
    Result<std::shared_ptr<ConnectionPool>> openNodePool(const QString &sourceId);
    /// 打开（不缓存）一个节点库连接池，openNodePool 的底层实现。
    static Result<std::shared_ptr<ConnectionPool>> connectNodePool(const QString &path);
    /// 取出缓存的节点库连接池（不关闭）。库文件即将被换名/删除前必须调用：
    /// Windows 上打开着的 SQLite 句柄会让 rename/remove_file 失败。
    Result<std::shared_ptr<ConnectionPool>> takeNodePool(const QString &sourceId);
    /// 关闭并移除缓存的节点库连接池。删除库文件前必须调用。
    void closeNodePool(const QString &sourceId);
    /// 清空所有缓存的节点库连接池，释放底层 SQLite 句柄。
    void trimCachedPools();
    /// 只有真的不知道节点属于哪个源时才走这里：所有分库并发查。
    Result<std::shared_ptr<ConnectionPool>> findNodePool(const QString &nodeId);
    /// 需要扫描的节点库作用域集合（None = 未分组）。
    QVector<QString> nodeScopes(const QString &sourceId);

    /// 建表与列补齐。对应 schema.rs。
    Status migrate();
    static Status migrateNodePool(ConnectionPool &pool);

    // ---- 订阅源（db/sources.rs）----
    Result<QVector<Models::Source>> listSources();
    Result<Models::Source> getSource(const QString &id);
    Result<Models::Source> createSource(const Models::CreateSource &input);
    Result<Models::Source> updateSource(const QString &id, const Models::UpdateSource &input);
    Result<bool> deleteSource(const QString &id);
    Status markSourceFetched(const QString &id, const QString &status, qint64 nodeCount);
    Result<qint64> recordFetchOutcome(const QString &id, bool ok, bool networkFailure);
    Status setSourceFetchHost(const QString &id, const QString &host);
    Result<QSet<QString>> seenGistIds();
    Status markGistSeen(const QString &id, const QString &sourceId);
    Status updateSourceCache(const QString &id, const QString &resolvedUrl, const QString &etag,
                             const QString &lastModified, const QString &contentSha256);
    Result<QVector<Models::SourceHomogeneityReport>> analyzeSourceHomogeneity();
    Result<Models::PruneHomogeneityResult> pruneHomogenizedSources(
        const QVector<QString> &sourceIds, bool deleteRecords);

    // ---- 节点（db/nodes.rs）----
    static Status insertNode(Transaction &tx, const Models::Node &node);
    Result<QVector<Models::Node>> loadNodes(const QString &sourceId);
    Result<QVector<Models::NodeSummary>> loadNodeSummaries(const QString &sourceId);
    Result<QVector<Models::NodeSummary>> loadAvailableNodeSummaries(const QString &sourceId);
    Result<Models::Page<Models::NodeSummary>> listNodes(const std::optional<QString> &protocol,
                                                        const QString &sourceId,
                                                        const std::optional<QString> &keyword,
                                                        const std::optional<QString> &latency,
                                                        const std::optional<QString> &country,
                                                        quint32 page, quint32 pageSize,
                                                        const std::optional<QString> &sort);
    Result<Models::Node> getNode(const QString &id);
    Result<QVector<Models::Node>> getNodes(const QVector<QString> &ids);
    Result<QVector<Models::Node>> topVerifiedNodes(int limit, const QString &excludeId);
    Result<bool> deleteNode(const QString &id);
    Result<qint64> clearNodes();
    Result<qint64> dedupeNodes();
    Result<qint64> deleteDeadNodes();
    Result<qint64> deduplicatePhysical();
    Result<int> cleanAndBeautifyNodeNames();
    Result<qint64> scrubInvalidHosts();
    Result<qint64> deleteInvalidHosts();
    /// 用新一批节点整体替换该源的分片：这一次没出现的就删掉（普通订阅链接的语义）。
    Result<int> replaceSourceNodes(const QString &sourceId, const QVector<Models::Node> &nodes);
    /// Telegram 频道这类"零散发布"的源：新出现的并进来了，这一页看不到的旧节点**不删**。
    Result<int> accumulateSourceNodes(const QString &sourceId, const QVector<Models::Node> &nodes);
    Result<int> importNodes(const QVector<Models::Node> &nodes);

    static QVector<Models::Node> filterNodes(QVector<Models::Node> nodes,
                                             const std::optional<QString> &protocol,
                                             const std::optional<QString> &keyword,
                                             const std::optional<QString> &latency,
                                             const std::optional<QString> &country);
    static QVector<Models::NodeSummary> filterNodeSummaries(
        QVector<Models::NodeSummary> nodes, const std::optional<QString> &protocol,
        const std::optional<QString> &keyword, const std::optional<QString> &latency,
        const std::optional<QString> &country);

    // ---- 设置与代理配置（db/settings.rs）----
    Result<Models::AppSettings> getSettings();
    Result<Models::AppSettings> updateSettings(const Models::UpdateSettings &patch);
    /// 只写发布服务器两项，避免和"整套设置"的读写互相覆盖。
    Result<Models::AppSettings> updatePublisher(const QString &api, const QString &token);
    Status touchAutoFetch();
    Status touchAutoSpeedtest();
    /// proxy_settings 单行表里的原始 JSON。类型化读写（校验、端口迁移）在 proxy 层。
    Result<QString> getProxyConfigJsonText();
    Status setProxyConfigJsonText(const QString &json);

    // ---- 测速结果写库（db/speedtest.rs）----
    struct SpeedUpdate {
        QString id;
        bool ok = false;
        std::optional<qint64> latencyMs;
        std::optional<double> downloadKbps;
        QString latencyKernel;
        QString latencyTestKind;
    };
    /// 按 source_id 分组，每个分库一个事务批量写入。
    Status updateNodeSpeedsBatch(const QVector<SpeedUpdate> &updates);
    Status updateNodeSpeed(const QString &id, bool ok, const std::optional<qint64> &latencyMs,
                           const std::optional<double> &downloadKbps, const QString &kernel,
                           const QString &testKind);
    Status updateNodeLatency(const QString &id, bool ok, const std::optional<qint64> &latencyMs);
    /// 全量节点列表：批量 TCP 预筛 + 真实代理验证用。
    Result<QVector<Models::Node>> listNodesForSpeedtest();
    /// 同上，但只覆盖当前列表筛选条件，"测速全部"测的是用户筛后的结果。
    Result<QVector<Models::Node>> listNodesForSpeedtestFiltered(
        const std::optional<QString> &protocol, const std::optional<QString> &keyword,
        const std::optional<QString> &latency);
    /// 某个订阅 Tab 下符合筛选条件的节点 id（测速当前订阅用）。
    Result<QVector<QString>> listNodeIdsForGroup(const QString &sourceId,
                                                 const std::optional<QString> &protocol,
                                                 const std::optional<QString> &keyword,
                                                 const std::optional<QString> &latency);

    // ---- 统计聚合（db/stats.rs）----
    /// 单个节点库的统计聚合，SQL 端完成，不把行数据拉进内存。
    struct ScopeNodeStats {
        qint64 nodeCount = 0;
        qint64 testedCount = 0;
        qint64 aliveCount = 0;
        QHash<QString, qint64> protocolCounts;
    };
    Result<std::optional<ScopeNodeStats>> scopeNodeStats(const QString &scope);
    /// (节点总数, 测通数) 跨所有分库一趟查完。测通数就是"自动切换还有多少替补可挑"。
    Result<QPair<qint64, qint64>> nodeVerificationCounts();
    Result<QVector<Models::NodeGroupSummary>> listNodeGroups(const std::optional<QString> &protocol,
                                                             const std::optional<QString> &keyword,
                                                             const std::optional<QString> &latency);
    Result<Models::Stats> stats();
    Result<qint64> sourceCount();

    // ---- 行映射与纯函数工具（对应 db/mod.rs 的自由函数）----
    static Models::Source rowToSource(const Statement &row);
    static Models::Node rowToNode(const Statement &row);
    static Models::NodeSummary rowToNodeSummary(const Statement &row);
    /// 解析失败退回当前时刻，和 Rust parse_dt 的 unwrap_or_else 一致。
    static TimeUtil::OptionalUtc parseDt(const QString &text);
    /// 节点名质量分：推广/URL 名字扣分，短名字加分。
    static int nameQualityScore(const QString &name);
    /// 去重时保留哪个节点的打分（可用 > 未测 > 失败，再看延迟/带宽/名称/时间）。
    static qint64 dedupeKeepScore(const Models::Node &node);

private:
    Database() = default;

    /// 单个分库的加载（库文件不存在时返回空，不建库）。
    Result<QVector<Models::Node>> loadScopeNodes(const QString &scope);
    Result<QVector<Models::NodeSummary>> loadScopeNodeSummaries(const QString &scope);
    Result<QVector<Models::NodeSummary>> loadAvailableScopeNodeSummaries(const QString &scope);
    /// 单节点库的 SQL 分页查询：过滤/排序/分页全部在库内完成，且只加载元数据。
    Result<Models::Page<Models::NodeSummary>> listNodesPage(
        const QString &scope, const std::optional<QString> &protocol,
        const std::optional<QString> &keyword, const std::optional<QString> &latency,
        const std::optional<QString> &country, quint32 page, quint32 pageSize,
        const std::optional<QString> &sort);
    Result<int> writeSourceNodes(const QString &sourceId, const QVector<Models::Node> &nodes,
                                 bool keepMissing);
    /// Gist 发现台账表的老结构补齐。
    Status migrateSeenGists();
    /// 四张"说实话"用的表：出口健康、切换历史、规则命中、拉取历史。
    Status migrateTelemetry();
    /// 兼容旧版本：主库 nodes 有数据就按 source_id 迁入分库并清空。
    Status migrateLegacyNodes();

    QString mainPath_;
    QString nodeRoot_;
    std::shared_ptr<ConnectionPool> main_;

    mutable QMutex poolsMutex_;
    QHash<QString, std::shared_ptr<ConnectionPool>> nodePools_;
};
