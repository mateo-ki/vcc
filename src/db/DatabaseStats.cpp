#include <QFileInfo>
#include <QFuture>
#include <QPair>
#include <QtConcurrent>

#include <algorithm>

#include "core/Log.h"
#include "db/Database.h"
#include "db/TextOrder.h"

namespace {

/// "未分组" —— 用码点写，避免转录时丢字。
QString ungroupedLabel()
{
    static const char32_t kCodePoints[] = {0x672A, 0x5206, 0x7EC4};
    return QString::fromUcs4(kCodePoints, 3);
}

}

Result<std::optional<Database::ScopeNodeStats>> Database::scopeNodeStats(const QString &scope)
{
    if (!QFileInfo::exists(nodeDbPath(scope))) {
        return Result<std::optional<ScopeNodeStats>>::withValue(std::nullopt);
    }
    Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
    if (pool.isErr()) {
        return Result<std::optional<ScopeNodeStats>>::withError(pool.error());
    }
    static const char *kAggregate = R"(SELECT protocol, COUNT(*) AS c,
                      SUM(CASE WHEN latency_ok IS NOT NULL AND latency_test_kind = 'google_proxy'
                          THEN 1 ELSE 0 END) AS tested,
                      SUM(CASE WHEN latency_ok = 1 AND latency_test_kind = 'google_proxy'
                          THEN 1 ELSE 0 END) AS alive
               FROM nodes GROUP BY protocol)";
    ScopeNodeStats stats;
    {
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(QString::fromLatin1(kAggregate));
        if (prepared.isErr()) {
            return Result<std::optional<ScopeNodeStats>>::withError(prepared.error());
        }
        Statement row = prepared.takeValue();
        for (;;) {
            const Result<bool> stepped = row.step();
            if (stepped.isErr()) {
                return Result<std::optional<ScopeNodeStats>>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            const qint64 count = row.int64("c");
            stats.nodeCount += count;
            stats.testedCount += row.int64("tested");
            stats.aliveCount += row.int64("alive");
            stats.protocolCounts[row.text("protocol")] += count;
        }
    }
    return Result<std::optional<ScopeNodeStats>>::withValue(stats);
}

/// (节点总数, 测通数) 跨所有分库。单个分库出错只 warn 并跳过：那会让总数偏低，
/// 判定于是偏向"先不折腾"，是安全的一侧；但不能静默。
Result<QPair<qint64, qint64>> Database::nodeVerificationCounts()
{
    const QVector<QString> scopes = nodeScopes(QString());
    auto counter = [this](const QString &scope) -> QPair<qint64, qint64> {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            return {0, 0};
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            Log::warn(QStringLiteral("读取测通数量失败：打不开分库 %1").arg(nodeDbPath(scope)));
            return {0, 0};
        }
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(QStringLiteral(
            "SELECT COUNT(*), COALESCE(SUM(latency_ok = 1), 0) FROM nodes"));
        if (prepared.isErr()) {
            Log::warn(QStringLiteral("读取测通数量失败（该分库按 0 计）: %1")
                          .arg(prepared.error()));
            return {0, 0};
        }
        Statement row = prepared.takeValue();
        const Result<bool> stepped = row.step();
        if (stepped.isErr() || !stepped.value()) {
            Log::warn(QStringLiteral("读取测通数量失败（该分库按 0 计）"));
            return {0, 0};
        }
        return {row.int64(0), row.int64(1)};
    };

    QVector<QFuture<QPair<qint64, qint64>>> futures;
    futures.reserve(scopes.size());
    for (const QString &scope : scopes) {
        futures.append(QtConcurrent::run(counter, scope));
    }
    qint64 total = 0;
    qint64 verified = 0;
    for (QFuture<QPair<qint64, qint64>> &future : futures) {
        future.waitForFinished();
        const QPair<qint64, qint64> pair = future.result();
        total += pair.first;
        verified += pair.second;
    }
    return Result<QPair<qint64, qint64>>::withValue({total, verified});
}

Result<QVector<Models::NodeGroupSummary>> Database::listNodeGroups(
    const std::optional<QString> &protocol, const std::optional<QString> &keyword,
    const std::optional<QString> &latency)
{
    Result<QVector<Models::Source>> sources = listSources();
    if (sources.isErr()) {
        return Result<QVector<Models::NodeGroupSummary>>::withError(sources.error());
    }
    QHash<QString, QString> names;
    for (const Models::Source &source : sources.value()) {
        names.insert(source.id, source.name);
    }

    // 过滤条件全部下推 SQL，每组只回一条聚合行，不再整表加载。
    QString whereSql = QStringLiteral("WHERE 1=1");
    QVector<QString> binds;
    if (protocol.has_value()) {
        whereSql += QStringLiteral(" AND protocol = ?");
        binds.append(*protocol);
    }
    if (keyword.has_value()) {
        const QString pattern =
            QStringLiteral("%") + TextOrder::asciiLower(*keyword) + QStringLiteral("%");
        whereSql += QStringLiteral(" AND (lower(name) LIKE ? OR lower(server) LIKE ?)");
        binds.append(pattern);
        binds.append(pattern);
    }
    if (latency.has_value()) {
        const QString value = TextOrder::asciiLower(*latency);
        if (value == QLatin1String("alive")) {
            whereSql +=
                QStringLiteral(" AND latency_ok = 1 AND latency_test_kind = 'google_proxy'");
        } else if (value == QLatin1String("dead")) {
            whereSql += QStringLiteral(" AND latency_ok = 0");
        } else if (value == QLatin1String("untested")) {
            whereSql += QStringLiteral(" AND latency_ok IS NULL");
        }
    }

    const QVector<QString> scopes = nodeScopes(QString());
    struct GroupRow {
        QString scope;
        qint64 total = 0;
        qint64 alive = 0;
    };
    auto groupQuery = [this, whereSql, binds](const QString &scope) -> Result<GroupRow> {
        GroupRow row{scope, 0, 0};
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            return Result<GroupRow>::withValue(row);
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<GroupRow>::withError(pool.error());
        }
        const QString sql = QStringLiteral(
            "SELECT COUNT(*) AS total,\n"
            "                      COALESCE(SUM(CASE WHEN latency_ok = 1 AND "
            "latency_test_kind = 'google_proxy'\n"
            "                          THEN 1 ELSE 0 END), 0) AS alive\n"
            "               FROM nodes %1")
                                .arg(whereSql);
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(sql);
        if (prepared.isErr()) {
            return Result<GroupRow>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (int index = 0; index < binds.size(); ++index) {
            statement.bindText(index + 1, binds.at(index));
        }
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<GroupRow>::withError(stepped.error());
        }
        if (stepped.value()) {
            row.total = statement.int64("total");
            row.alive = statement.int64("alive");
        }
        return Result<GroupRow>::withValue(row);
    };

    QVector<QFuture<Result<GroupRow>>> futures;
    futures.reserve(scopes.size());
    for (const QString &scope : scopes) {
        futures.append(QtConcurrent::run(groupQuery, scope));
    }
    QVector<Models::NodeGroupSummary> groups;
    for (QFuture<Result<GroupRow>> &future : futures) {
        future.waitForFinished();
        const Result<GroupRow> result = future.result();
        if (result.isErr()) {
            return Result<QVector<Models::NodeGroupSummary>>::withError(result.error());
        }
        const GroupRow row = result.value();
        if (row.total == 0) {
            continue;
        }
        Models::NodeGroupSummary summary;
        summary.sourceId = row.scope;
        const auto name = names.constFind(row.scope);
        summary.sourceName = row.scope.isNull() || name == names.constEnd()
            ? ungroupedLabel()
            : name.value();
        summary.total = row.total;
        summary.alive = row.alive;
        groups.append(summary);
    }
    std::stable_sort(groups.begin(), groups.end(),
                    [](const Models::NodeGroupSummary &left,
                       const Models::NodeGroupSummary &right) {
                        return TextOrder::less(left.sourceName, right.sourceName);
                    });
    return Result<QVector<Models::NodeGroupSummary>>::withValue(std::move(groups));
}

Result<qint64> Database::sourceCount()
{
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("SELECT COUNT(*) as c FROM sources"));
    if (prepared.isErr()) {
        return Result<qint64>::withError(prepared.error());
    }
    Statement row = prepared.takeValue();
    const Result<bool> stepped = row.step();
    if (stepped.isErr()) {
        return Result<qint64>::withError(stepped.error());
    }
    return Result<qint64>::withValue(stepped.value() ? row.int64("c") : 0);
}

Result<Models::Stats> Database::stats()
{
    Result<qint64> sources = sourceCount();
    if (sources.isErr()) {
        return Result<Models::Stats>::withError(sources.error());
    }
    Models::Stats stats;
    stats.sourceCount = sources.value();
    {
        auto lease = main_->acquire();
        Result<Statement> prepared =
            lease->prepare(QStringLiteral("SELECT COUNT(*) FROM sources WHERE enabled = 1"));
        if (prepared.isErr()) {
            return Result<Models::Stats>::withError(prepared.error());
        }
        Statement row = prepared.takeValue();
        const Result<bool> stepped = row.step();
        if (stepped.isErr()) {
            return Result<Models::Stats>::withError(stepped.error());
        }
        stats.enabledSourceCount = stepped.value() ? row.int64(0) : 0;
    }

    // 聚合下推到 SQL 端完成：整表加载 2.6 万节点到内存只为数数太浪费。
    const QVector<QString> scopes = nodeScopes(QString());
    auto aggregator = [this](const QString &scope) { return scopeNodeStats(scope); };
    QVector<QFuture<Result<std::optional<ScopeNodeStats>>>> futures;
    futures.reserve(scopes.size());
    for (const QString &scope : scopes) {
        futures.append(QtConcurrent::run(aggregator, scope));
    }
    ScopeNodeStats merged;
    for (QFuture<Result<std::optional<ScopeNodeStats>>> &future : futures) {
        future.waitForFinished();
        const Result<std::optional<ScopeNodeStats>> result = future.result();
        if (result.isErr()) {
            return Result<Models::Stats>::withError(result.error());
        }
        if (!result.value().has_value()) {
            continue;
        }
        const ScopeNodeStats &scopeStats = *result.value();
        merged.nodeCount += scopeStats.nodeCount;
        merged.testedCount += scopeStats.testedCount;
        merged.aliveCount += scopeStats.aliveCount;
        for (auto it = scopeStats.protocolCounts.constBegin();
             it != scopeStats.protocolCounts.constEnd(); ++it) {
            merged.protocolCounts[it.key()] += it.value();
        }
    }

    stats.nodeCount = merged.nodeCount;
    stats.testedCount = merged.testedCount;
    stats.aliveCount = merged.aliveCount;
    for (auto it = merged.protocolCounts.constBegin(); it != merged.protocolCounts.constEnd();
         ++it) {
        Models::ProtocolStat stat;
        stat.protocol = it.key();
        stat.count = it.value();
        stats.protocolStats.append(stat);
    }
    std::stable_sort(stats.protocolStats.begin(), stats.protocolStats.end(),
                     [](const Models::ProtocolStat &left, const Models::ProtocolStat &right) {
                         return left.count > right.count;
                     });
    return Result<Models::Stats>::withValue(std::move(stats));
}
