#include <QFileInfo>

#include "core/TimeUtil.h"
#include "db/Database.h"
#include "db/FanOut.h"

namespace {

const char *kUpdateSpeed = R"(UPDATE nodes SET latency_ok=?, latency_ms=?, download_kbps=?, latency_kernel=?,
                      latency_test_kind=?, latency_tested_at=?, updated_at=?
               WHERE id=?)";

Status applyUpdate(Connection &connection, const QString &now, const Database::SpeedUpdate &item)
{
    Result<Statement> prepared = connection.prepare(QString::fromLatin1(kUpdateSpeed));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindBool(1, item.ok);
    if (item.latencyMs.has_value()) {
        statement.bindInt64(2, *item.latencyMs);
    } else {
        statement.bindNull(2);
    }
    if (item.downloadKbps.has_value()) {
        statement.bindDouble(3, *item.downloadKbps);
    } else {
        statement.bindNull(3);
    }
    statement.bindText(4, item.latencyKernel);
    statement.bindText(5, item.latencyTestKind);
    statement.bindText(6, now);
    statement.bindText(7, now);
    statement.bindText(8, item.id);
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}

QStringList placeholderList(int count)
{
    QStringList marks;
    marks.reserve(count);
    for (int index = 0; index < count; ++index) {
        marks.append(QStringLiteral("?"));
    }
    return marks;
}

}

Status Database::updateNodeLatency(const QString &id, bool ok,
                                   const std::optional<qint64> &latencyMs)
{
    return updateNodeSpeed(id, ok, latencyMs, std::nullopt, QString(), QString());
}

Status Database::updateNodeSpeed(const QString &id, bool ok, const std::optional<qint64> &latencyMs,
                                 const std::optional<double> &downloadKbps,
                                 const QString &kernel, const QString &testKind)
{
    Result<std::shared_ptr<ConnectionPool>> pool = findNodePool(id);
    if (pool.isErr()) {
        return Status::failed(pool.error());
    }
    if (!pool.value()) {
        return Status::succeeded();
    }
    SpeedUpdate item;
    item.id = id;
    item.ok = ok;
    item.latencyMs = latencyMs;
    item.downloadKbps = downloadKbps;
    item.latencyKernel = kernel;
    item.latencyTestKind = testKind;

    const QString now = TimeUtil::nowRfc3339();
    auto lease = pool.value()->acquire();
    return applyUpdate(*lease, now, item);
}

/// 批量写测速结果：按分库分组，每个分库一个事务，比逐节点 UPDATE 快一个数量级。
Status Database::updateNodeSpeedsBatch(const QVector<SpeedUpdate> &items)
{
    if (items.isEmpty()) {
        return Status::succeeded();
    }
    QHash<QString, const SpeedUpdate *> lookup;
    lookup.reserve(items.size() * 2);
    for (const SpeedUpdate &item : items) {
        lookup.insert(item.id, &item);
    }

    QHash<QString, QVector<const SpeedUpdate *>> grouped;
    QVector<QString> groupOrder;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Status::failed(pool.error());
        }
        // 只按候选 id 走主键点查：以前每个分库都 `SELECT id FROM nodes` 全量导出，
        // 19000 条结果里绝大多数会被丢掉。
        auto lease = pool.value()->acquire();
        QVector<const SpeedUpdate *> group;
        for (int offset = 0; offset < items.size(); offset += 500) {
            const QVector<SpeedUpdate> chunk = items.mid(offset, 500);
            const QString sql = QStringLiteral("SELECT id FROM nodes WHERE id IN (%1)")
                                    .arg(placeholderList(chunk.size()).join(QLatin1Char(',')));
            Result<Statement> prepared = lease->prepare(sql);
            if (prepared.isErr()) {
                return Status::failed(prepared.error());
            }
            Statement statement = prepared.takeValue();
            for (int index = 0; index < chunk.size(); ++index) {
                statement.bindText(index + 1, chunk.at(index).id);
            }
            for (;;) {
                const Result<bool> stepped = statement.step();
                if (stepped.isErr()) {
                    return Status::failed(stepped.error());
                }
                if (!stepped.value()) {
                    break;
                }
                const auto found = lookup.constFind(statement.text(0));
                if (found != lookup.constEnd()) {
                    group.append(found.value());
                }
            }
        }
        if (!group.isEmpty()) {
            const QString key = scope.isNull() ? QString() : scope;
            if (!grouped.contains(key)) {
                groupOrder.append(key);
            }
            grouped.insert(key, group);
        }
    }

    const QString now = TimeUtil::nowRfc3339();
    for (const QString &key : groupOrder) {
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(key);
        if (pool.isErr()) {
            return Status::failed(pool.error());
        }
        Transaction transaction(pool.value()->acquire());
        Connection *connection = transaction.connection();
        for (const SpeedUpdate *item : grouped.value(key)) {
            const Status updated = applyUpdate(*connection, now, *item);
            if (updated.isErr()) {
                return updated;
            }
        }
        const Status committed = transaction.commit();
        if (committed.isErr()) {
            return committed;
        }
    }
    return Status::succeeded();
}

Result<QVector<Models::Node>> Database::listNodesForSpeedtest()
{
    return listNodesForSpeedtestFiltered(std::nullopt, std::nullopt, std::nullopt);
}

/// 与 listNodesForSpeedtest 相同，但只覆盖当前列表筛选出来的节点，
/// 所以"测速全部"测的是用户筛后的结果。
Result<QVector<Models::Node>> Database::listNodesForSpeedtestFiltered(
    const std::optional<QString> &protocol, const std::optional<QString> &keyword,
    const std::optional<QString> &latency)
{
    Result<QVector<Models::Node>> loaded = loadNodes(QString());
    if (loaded.isErr()) {
        return loaded;
    }
    return Result<QVector<Models::Node>>::withValue(
        filterNodes(loaded.takeValue(), protocol, keyword, latency, std::nullopt));
}

Result<QVector<QString>> Database::listNodeIdsForGroup(const QString &sourceId,
                                                      const std::optional<QString> &protocol,
                                                      const std::optional<QString> &keyword,
                                                      const std::optional<QString> &latency)
{
    const QString scope = sourceId.isNull() || sourceId.isEmpty() ? kUngroupedSourceId : sourceId;
    Result<QVector<Models::Node>> loaded = loadNodes(scope);
    if (loaded.isErr()) {
        return Result<QVector<QString>>::withError(loaded.error());
    }
    QVector<QString> ids;
    const QVector<Models::Node> nodes =
        filterNodes(loaded.takeValue(), protocol, keyword, latency, std::nullopt);
    ids.reserve(nodes.size());
    for (const Models::Node &node : nodes) {
        ids.append(node.id);
    }
    return Result<QVector<QString>>::withValue(std::move(ids));
}
