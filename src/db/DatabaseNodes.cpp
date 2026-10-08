#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Encoding.h"
#include "core/HostUtil.h"
#include "core/Ids.h"
#include "core/JsonUtil.h"
#include "core/Log.h"
#include "db/Database.h"
#include "db/FanOut.h"
#include "db/TextOrder.h"
#include "parser/Fingerprint.h"

// clean_node_name 由 parser 模块提供（P3），这里只声明需要的签名。
namespace Parser {
QString cleanNodeName(const QString &name, const QString &countryHint, const QString &server,
                      int port);
}

namespace {

bool lessText(const QString &left, const QString &right)
{
    return TextOrder::less(left, right);
}

/// 把 id 列表拼成 `?,?,?` 形式的 IN 占位符。
QString placeholders(int count)
{
    QStringList marks;
    marks.reserve(count);
    for (int index = 0; index < count; ++index) {
        marks.append(QStringLiteral("?"));
    }
    return marks.join(QLatin1Char(','));
}

constexpr int kIdChunkSize = 500;
constexpr int kDeleteChunkSize = 200;

const char *kFullNodeSelect = R"(SELECT n.id, n.source_id, n.protocol, n.name, n.server, n.port,
                      COALESCE(NULLIF(p.raw, ''), n.raw, '') AS raw,
                      COALESCE(NULLIF(p.config, ''), n.config, '{}') AS config,
                      n.country, n.latency_ms, n.latency_ok, n.download_kbps,
                      n.latency_kernel, n.latency_test_kind, n.latency_tested_at,
                      n.created_at, n.updated_at
               FROM nodes n
               LEFT JOIN node_payloads p ON n.id = p.node_id)";

const char *kSummarySelect = R"(SELECT id, source_id, protocol, name, server, port, country,
                      latency_ms, latency_ok, download_kbps, latency_kernel,
                      latency_test_kind, latency_tested_at, updated_at
               FROM nodes)";

bool matchesLatencyFilter(const std::optional<QString> &latency, const std::optional<bool> &ok,
                          const QString &testKind)
{
    if (!latency.has_value()) {
        return true;
    }
    const QString value = latency->toLower();
    if (value == QLatin1String("alive")) {
        return ok.has_value() && *ok && testKind == QLatin1String("google_proxy");
    }
    if (value == QLatin1String("dead")) {
        return ok.has_value() && !*ok;
    }
    if (value == QLatin1String("untested")) {
        return !ok.has_value();
    }
    return true;
}

bool matchesCountryFilter(const std::optional<QString> &country, const QString &nodeCountry)
{
    if (!country.has_value()) {
        return true;
    }
    static const QStringList known = {QStringLiteral("HK"), QStringLiteral("JP"),
                                      QStringLiteral("US"), QStringLiteral("SG"),
                                      QStringLiteral("TW"), QStringLiteral("KR"),
                                      QStringLiteral("GB"), QStringLiteral("DE")};
    if (country->compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (country->compare(QStringLiteral("OTHER"), Qt::CaseInsensitive) == 0) {
        return nodeCountry.isNull() || !known.contains(nodeCountry, Qt::CaseInsensitive);
    }
    return !nodeCountry.isNull()
        && nodeCountry.compare(*country, Qt::CaseInsensitive) == 0;
}

template <typename NodeLike>
bool matchesFilters(const NodeLike &node, const std::optional<QString> &protocol,
                    const QString &needle, const std::optional<QString> &latency,
                    const std::optional<QString> &country)
{
    if (protocol.has_value() && node.protocol != *protocol) {
        return false;
    }
    if (!needle.isEmpty()) {
        const QString name = node.name.toLower();
        const QString server = node.server.toLower();
        if (!name.contains(needle) && !server.contains(needle)) {
            return false;
        }
    }
    if (!matchesLatencyFilter(latency, node.latencyOk, node.latencyTestKind)) {
        return false;
    }
    return matchesCountryFilter(country, node.country);
}

/// 与 Rust `to_ascii_lowercase` 一致。
QString asciiLower(const QString &text)
{
    return TextOrder::asciiLower(text);
}

}

Status Database::insertNode(Transaction &tx, const Models::Node &node)
{
    Connection *connection = tx.connection();
    if (!connection) {
        return Status::failed(QStringLiteral("事务不可用"));
    }

    static const char *kInsertNode = R"(INSERT INTO nodes
               (id, source_id, protocol, name, server, port, raw, config, country,
                latency_ms, latency_ok, download_kbps, latency_kernel,
                latency_test_kind, latency_tested_at, created_at, updated_at)
               VALUES (?, ?, ?, ?, ?, ?, '', '{}', ?, ?, ?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT DO UPDATE SET
                 source_id=excluded.source_id, protocol=excluded.protocol, name=excluded.name,
                 server=excluded.server, port=excluded.port, raw='', config='{}',
                 country=excluded.country, latency_ms=excluded.latency_ms, latency_ok=excluded.latency_ok,
                 download_kbps=excluded.download_kbps, latency_kernel=excluded.latency_kernel,
                 latency_test_kind=excluded.latency_test_kind, latency_tested_at=excluded.latency_tested_at,
                 updated_at=excluded.updated_at)";

    Result<Statement> prepared = connection->prepare(QString::fromLatin1(kInsertNode));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, node.id);
    statement.bindText(2, node.sourceId);
    statement.bindText(3, node.protocol);
    statement.bindText(4, node.name);
    statement.bindText(5, node.server);
    statement.bindInt64(6, node.port);
    statement.bindText(7, node.country);
    if (node.latencyMs.has_value()) {
        statement.bindInt64(8, *node.latencyMs);
    } else {
        statement.bindNull(8);
    }
    if (node.latencyOk.has_value()) {
        statement.bindInt64(9, *node.latencyOk ? 1 : 0);
    } else {
        statement.bindNull(9);
    }
    if (node.downloadKbps.has_value()) {
        statement.bindDouble(10, *node.downloadKbps);
    } else {
        statement.bindNull(10);
    }
    statement.bindText(11, node.latencyKernel);
    statement.bindText(12, node.latencyTestKind);
    statement.bindText(13, node.latencyTestedAt.toDbText());
    statement.bindText(14, node.createdAt.toDbText());
    statement.bindText(15, node.updatedAt.toDbText());
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }

    static const char *kInsertPayload = R"(INSERT INTO node_payloads (node_id, raw, config)
               VALUES (?, ?, ?)
               ON CONFLICT(node_id) DO UPDATE SET
                 raw=excluded.raw, config=excluded.config)";
    Result<Statement> payload = connection->prepare(QString::fromLatin1(kInsertPayload));
    if (payload.isErr()) {
        return Status::failed(payload.error());
    }
    Statement payloadStatement = payload.takeValue();
    payloadStatement.bindText(1, node.id);
    payloadStatement.bindText(2, node.raw);
    payloadStatement.bindText(3, JsonUtil::dump(node.config));
    const Result<bool> payloadStepped = payloadStatement.step();
    if (payloadStepped.isErr()) {
        return Status::failed(payloadStepped.error());
    }
    return Status::succeeded();
}

Result<QVector<Models::Node>> Database::loadScopeNodes(const QString &scope)
{
    if (!QFileInfo::exists(nodeDbPath(scope))) {
        return Result<QVector<Models::Node>>::withValue(QVector<Models::Node>());
    }
    Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
    if (pool.isErr()) {
        return Result<QVector<Models::Node>>::withError(pool.error());
    }
    QVector<Models::Node> nodes;
    {
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(QString::fromLatin1(kFullNodeSelect));
        if (prepared.isErr()) {
            return Result<QVector<Models::Node>>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<QVector<Models::Node>>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            nodes.append(rowToNode(statement));
        }
    }
    return Result<QVector<Models::Node>>::withValue(std::move(nodes));
}

Result<QVector<Models::Node>> Database::loadNodes(const QString &sourceId)
{
    const QVector<QString> scopes = nodeScopes(sourceId);
    if (scopes.size() <= 1) {
        const QString scope = scopes.isEmpty() ? QString() : scopes.first();
        return loadScopeNodes(scope);
    }
    auto loader = [this](const QString &scope) { return loadScopeNodes(scope); };
    return runOverScopes<Models::Node>(scopes, loader);
}

Result<QVector<Models::NodeSummary>> Database::loadScopeNodeSummaries(const QString &scope)
{
    if (!QFileInfo::exists(nodeDbPath(scope))) {
        return Result<QVector<Models::NodeSummary>>::withValue(QVector<Models::NodeSummary>());
    }
    Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
    if (pool.isErr()) {
        return Result<QVector<Models::NodeSummary>>::withError(pool.error());
    }
    QVector<Models::NodeSummary> summaries;
    {
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(QString::fromLatin1(kSummarySelect));
        if (prepared.isErr()) {
            return Result<QVector<Models::NodeSummary>>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<QVector<Models::NodeSummary>>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            summaries.append(rowToNodeSummary(statement));
        }
    }
    return Result<QVector<Models::NodeSummary>>::withValue(std::move(summaries));
}

Result<QVector<Models::NodeSummary>> Database::loadNodeSummaries(const QString &sourceId)
{
    const QVector<QString> scopes = nodeScopes(sourceId);
    if (scopes.size() <= 1) {
        const QString scope = scopes.isEmpty() ? QString() : scopes.first();
        return loadScopeNodeSummaries(scope);
    }
    auto loader = [this](const QString &scope) { return loadScopeNodeSummaries(scope); };
    return runOverScopes<Models::NodeSummary>(scopes, loader);
}

Result<QVector<Models::NodeSummary>> Database::loadAvailableScopeNodeSummaries(
    const QString &scope)
{
    if (!QFileInfo::exists(nodeDbPath(scope))) {
        return Result<QVector<Models::NodeSummary>>::withValue(QVector<Models::NodeSummary>());
    }
    Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
    if (pool.isErr()) {
        return Result<QVector<Models::NodeSummary>>::withError(pool.error());
    }
    QVector<Models::NodeSummary> summaries;
    {
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(
            QStringLiteral("%1 WHERE latency_ok = 1 AND latency_test_kind = 'google_proxy' "
                           "ORDER BY latency_ms ASC")
                .arg(QString::fromLatin1(kSummarySelect)));
        if (prepared.isErr()) {
            return Result<QVector<Models::NodeSummary>>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<QVector<Models::NodeSummary>>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            summaries.append(rowToNodeSummary(statement));
        }
    }
    return Result<QVector<Models::NodeSummary>>::withValue(std::move(summaries));
}

Result<QVector<Models::NodeSummary>> Database::loadAvailableNodeSummaries(const QString &sourceId)
{
    const QVector<QString> scopes = nodeScopes(sourceId);
    QVector<Models::NodeSummary> summaries;
    if (scopes.size() <= 1) {
        const QString scope = scopes.isEmpty() ? QString() : scopes.first();
        Result<QVector<Models::NodeSummary>> loaded = loadAvailableScopeNodeSummaries(scope);
        if (loaded.isErr()) {
            return loaded;
        }
        summaries = loaded.takeValue();
    } else {
        auto loader = [this](const QString &scope) {
            return loadAvailableScopeNodeSummaries(scope);
        };
        Result<QVector<Models::NodeSummary>> loaded =
            runOverScopes<Models::NodeSummary>(scopes, loader);
        if (loaded.isErr()) {
            return loaded;
        }
        summaries = loaded.takeValue();
    }
    std::stable_sort(summaries.begin(), summaries.end(),
                     [](const Models::NodeSummary &left, const Models::NodeSummary &right) {
                         const qint64 leftMs = left.latencyMs.value_or(std::numeric_limits<qint64>::max());
                         const qint64 rightMs =
                             right.latencyMs.value_or(std::numeric_limits<qint64>::max());
                         if (leftMs != rightMs) {
                             return leftMs < rightMs;
                         }
                         return lessText(left.name, right.name);
                     });
    return Result<QVector<Models::NodeSummary>>::withValue(std::move(summaries));
}

Result<Models::Page<Models::NodeSummary>> Database::listNodesPage(
    const QString &scopeInput, const std::optional<QString> &protocol,
    const std::optional<QString> &keyword, const std::optional<QString> &latency,
    const std::optional<QString> &country, quint32 page, quint32 pageSize,
    const std::optional<QString> &sort)
{
    const QString scope = scopeInput == kUngroupedSourceId ? QString() : scopeInput;

    QString whereSql = QStringLiteral("WHERE 1=1");
    QVector<QString> binds;
    if (protocol.has_value()) {
        whereSql += QStringLiteral(" AND protocol = ?");
        binds.append(*protocol);
    }
    if (keyword.has_value()) {
        const QString pattern = QStringLiteral("%") + asciiLower(*keyword) + QStringLiteral("%");
        whereSql += QStringLiteral(" AND (lower(name) LIKE ? OR lower(server) LIKE ?)");
        binds.append(pattern);
        binds.append(pattern);
    }
    if (latency.has_value()) {
        const QString value = asciiLower(*latency);
        if (value == QLatin1String("alive")) {
            whereSql +=
                QStringLiteral(" AND latency_ok = 1 AND latency_test_kind = 'google_proxy'");
        } else if (value == QLatin1String("dead")) {
            whereSql += QStringLiteral(" AND latency_ok = 0");
        } else if (value == QLatin1String("untested")) {
            whereSql += QStringLiteral(" AND latency_ok IS NULL");
        }
    }
    if (country.has_value() && country->compare(QStringLiteral("all"), Qt::CaseInsensitive) != 0) {
        if (country->compare(QStringLiteral("OTHER"), Qt::CaseInsensitive) == 0) {
            whereSql += QStringLiteral(
                " AND (country IS NULL OR country NOT IN "
                "('HK','JP','US','SG','TW','KR','GB','DE'))");
        } else {
            whereSql += QStringLiteral(" AND lower(country) = lower(?)");
            binds.append(*country);
        }
    }

    const QString sortKey = sort.has_value() ? asciiLower(*sort) : QStringLiteral("updated");
    QString orderBy;
    if (sortKey == QLatin1String("latency")) {
        orderBy = QStringLiteral(
            "ORDER BY (latency_ok = 1) DESC, (latency_ms IS NULL) ASC, latency_ms ASC, name ASC");
    } else if (sortKey == QLatin1String("download")) {
        orderBy = QStringLiteral("ORDER BY (download_kbps IS NULL) ASC, download_kbps DESC");
    } else if (sortKey == QLatin1String("name")) {
        orderBy = QStringLiteral("ORDER BY name ASC");
    } else {
        orderBy = QStringLiteral("ORDER BY updated_at DESC");
    }

    Models::Page<Models::NodeSummary> result;
    result.total = 0;
    result.pageNumber = page;
    result.pageSize = pageSize;

    if (!QFileInfo::exists(nodeDbPath(scope))) {
        // 不存在的分库直接返回空页：走 openNodePool 会因 mode=rwc 建库。
        return Result<Models::Page<Models::NodeSummary>>::withValue(result);
    }
    Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
    if (pool.isErr()) {
        return Result<Models::Page<Models::NodeSummary>>::withError(pool.error());
    }

    auto lease = pool.value()->acquire();
    {
        const QString countSql =
            QStringLiteral("SELECT COUNT(*) AS total FROM nodes %1").arg(whereSql);
        Result<Statement> prepared = lease->prepare(countSql);
        if (prepared.isErr()) {
            return Result<Models::Page<Models::NodeSummary>>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (int index = 0; index < binds.size(); ++index) {
            statement.bindText(index + 1, binds.at(index));
        }
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<Models::Page<Models::NodeSummary>>::withError(stepped.error());
        }
        result.total = stepped.value() ? statement.int64(0) : 0;
    }

    const QString rowsSql = QStringLiteral(
                                "SELECT id, source_id, protocol, name, server, port, country,\n"
                                "                      latency_ms, latency_ok, download_kbps, "
                                "latency_kernel,\n"
                                "                      latency_test_kind, latency_tested_at, "
                                "updated_at\n"
                                "               FROM nodes %1 %2 LIMIT ? OFFSET ?")
                                .arg(whereSql, orderBy);
    Result<Statement> prepared = lease->prepare(rowsSql);
    if (prepared.isErr()) {
        return Result<Models::Page<Models::NodeSummary>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    int bindIndex = 1;
    for (const QString &bind : binds) {
        statement.bindText(bindIndex++, bind);
    }
    statement.bindInt64(bindIndex++, qint64(pageSize));
    statement.bindInt64(bindIndex, qint64((page - 1) * pageSize));
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<Models::Page<Models::NodeSummary>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        result.items.append(rowToNodeSummary(statement));
    }
    return Result<Models::Page<Models::NodeSummary>>::withValue(std::move(result));
}

Result<Models::Page<Models::NodeSummary>> Database::listNodes(
    const std::optional<QString> &protocol, const QString &sourceId,
    const std::optional<QString> &keyword, const std::optional<QString> &latency,
    const std::optional<QString> &country, quint32 page, quint32 pageSize,
    const std::optional<QString> &sort)
{
    page = qMax<quint32>(1, page);
    pageSize = std::clamp<quint32>(pageSize, 1, 500);
    if (!sourceId.isNull()) {
        return listNodesPage(sourceId, protocol, keyword, latency, country, page, pageSize, sort);
    }

    Result<QVector<Models::NodeSummary>> loaded = loadNodeSummaries(QString());
    if (loaded.isErr()) {
        return Result<Models::Page<Models::NodeSummary>>::withError(loaded.error());
    }
    QVector<Models::NodeSummary> nodes = filterNodeSummaries(loaded.takeValue(), protocol, keyword,
                                                            latency, country);
    const QString sortKey = sort.has_value() ? asciiLower(*sort) : QStringLiteral("updated");
    if (sortKey == QLatin1String("latency")) {
        std::stable_sort(nodes.begin(), nodes.end(),
                         [](const Models::NodeSummary &left, const Models::NodeSummary &right) {
                             const bool leftAlive = left.latencyOk.value_or(false);
                             const bool rightAlive = right.latencyOk.value_or(false);
                             if (leftAlive != rightAlive) {
                                 return !leftAlive && rightAlive;
                             }
                             const qint64 leftMs =
                                 left.latencyMs.value_or(std::numeric_limits<qint64>::max());
                             const qint64 rightMs =
                                 right.latencyMs.value_or(std::numeric_limits<qint64>::max());
                             if (leftMs != rightMs) {
                                 return leftMs < rightMs;
                             }
                             return lessText(left.name, right.name);
                         });
    } else if (sortKey == QLatin1String("download")) {
        std::stable_sort(nodes.begin(), nodes.end(),
                         [](const Models::NodeSummary &left, const Models::NodeSummary &right) {
                             if (left.downloadKbps.has_value() != right.downloadKbps.has_value()) {
                                 return left.downloadKbps.has_value();
                             }
                             if (!left.downloadKbps.has_value()) {
                                 return false;
                             }
                             return *left.downloadKbps > *right.downloadKbps;
                         });
    } else if (sortKey == QLatin1String("name")) {
        std::stable_sort(nodes.begin(), nodes.end(),
                         [](const Models::NodeSummary &left, const Models::NodeSummary &right) {
                             return lessText(left.name, right.name);
                         });
    } else {
        std::stable_sort(nodes.begin(), nodes.end(),
                         [](const Models::NodeSummary &left, const Models::NodeSummary &right) {
                             return left.updatedAt.nanos > right.updatedAt.nanos;
                         });
    }

    Models::Page<Models::NodeSummary> result;
    result.total = nodes.size();
    result.pageNumber = page;
    result.pageSize = pageSize;
    const int skip = int((page - 1) * pageSize);
    if (skip < nodes.size()) {
        result.items = nodes.mid(skip, int(pageSize));
    }
    return Result<Models::Page<Models::NodeSummary>>::withValue(std::move(result));
}

QVector<Models::Node> Database::filterNodes(QVector<Models::Node> nodes,
                                            const std::optional<QString> &protocol,
                                            const std::optional<QString> &keyword,
                                            const std::optional<QString> &latency,
                                            const std::optional<QString> &country)
{
    // 关键字和状态各只小写一次：写在闭包里等于每个节点重新分配一遍（全库两万行）。
    const QString needle = keyword.has_value() ? asciiLower(*keyword) : QString();
    const std::optional<QString> loweredLatency =
        latency.has_value() ? std::optional<QString>(asciiLower(*latency)) : std::nullopt;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [&](const Models::Node &node) {
                                   return !matchesFilters(node, protocol, needle, loweredLatency,
                                                          country);
                               }),
                 nodes.end());
    return nodes;
}

QVector<Models::NodeSummary> Database::filterNodeSummaries(
    QVector<Models::NodeSummary> nodes, const std::optional<QString> &protocol,
    const std::optional<QString> &keyword, const std::optional<QString> &latency,
    const std::optional<QString> &country)
{
    const QString needle = keyword.has_value() ? asciiLower(*keyword) : QString();
    const std::optional<QString> loweredLatency =
        latency.has_value() ? std::optional<QString>(asciiLower(*latency)) : std::nullopt;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [&](const Models::NodeSummary &node) {
                                   return !matchesFilters(node, protocol, needle, loweredLatency,
                                                          country);
                               }),
                 nodes.end());
    return nodes;
}

Result<Models::Node> Database::getNode(const QString &id)
{
    Result<std::shared_ptr<ConnectionPool>> pool = findNodePool(id);
    if (pool.isErr()) {
        return Result<Models::Node>::withError(pool.error());
    }
    if (!pool.value()) {
        return Result<Models::Node>::withValue(Models::Node());
    }
    auto lease = pool.value()->acquire();
    Result<Statement> prepared =
        lease->prepare(QString::fromLatin1(kFullNodeSelect) + QStringLiteral(" WHERE n.id = ?"));
    if (prepared.isErr()) {
        return Result<Models::Node>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, id);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<Models::Node>::withError(stepped.error());
    }
    if (!stepped.value()) {
        return Result<Models::Node>::withValue(Models::Node());
    }
    return Result<Models::Node>::withValue(rowToNode(statement));
}

Result<QVector<Models::Node>> Database::getNodes(const QVector<QString> &ids)
{
    // 批量按 id 取节点：每个分库一次 `id IN (...)`，分库之间并发。以前逐个
    // get_node 要把 35 个分库扇出找归属，5000 个 id 就是 17.5 万条查询。
    if (ids.isEmpty()) {
        return Result<QVector<Models::Node>>::withValue(QVector<Models::Node>());
    }
    const QVector<QString> scopes = nodeScopes(QString());
    auto loader = [this, ids](const QString &scope) -> Result<QVector<Models::Node>> {
        QVector<Models::Node> found;
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            return Result<QVector<Models::Node>>::withValue(found);
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            Log::warn(QStringLiteral("批量读取节点失败: %1").arg(pool.error()));
            return Result<QVector<Models::Node>>::withValue(found);
        }
        auto lease = pool.value()->acquire();
        for (int offset = 0; offset < ids.size(); offset += kIdChunkSize) {
            const QVector<QString> chunk = ids.mid(offset, kIdChunkSize);
            const QString sql = QString::fromLatin1(kFullNodeSelect)
                + QStringLiteral(" WHERE n.id IN (%1)").arg(placeholders(chunk.size()));
            Result<Statement> prepared = lease->prepare(sql);
            if (prepared.isErr()) {
                Log::warn(QStringLiteral("批量读取节点失败: %1").arg(prepared.error()));
                continue;
            }
            Statement statement = prepared.takeValue();
            for (int index = 0; index < chunk.size(); ++index) {
                statement.bindText(index + 1, chunk.at(index));
            }
            for (;;) {
                const Result<bool> stepped = statement.step();
                if (stepped.isErr()) {
                    Log::warn(QStringLiteral("批量读取节点失败: %1").arg(stepped.error()));
                    break;
                }
                if (!stepped.value()) {
                    break;
                }
                found.append(rowToNode(statement));
            }
        }
        return Result<QVector<Models::Node>>::withValue(found);
    };
    return runOverScopes<Models::Node>(scopes, loader);
}

Result<QVector<Models::Node>> Database::topVerifiedNodes(int limit, const QString &excludeId)
{
    // 各分库按真实测速延迟取前 N 名（验证通过的），合并成全局前 N，供内核内
    // url-test 组当候选。刻意不走 loadNodes：那会把上万个节点的 raw/config
    // 全读出来并逐条解析 JSON。
    limit = qMax(limit, 2);
    const QVector<QString> scopes = nodeScopes(QString());
    auto loader = [this, limit, excludeId](const QString &scope) -> Result<QVector<Models::Node>> {
        QVector<Models::Node> nodes;
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            return Result<QVector<Models::Node>>::withValue(nodes);
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<QVector<Models::Node>>::withValue(nodes);
        }
        const bool hasExclude = !excludeId.isNull() && !excludeId.isEmpty();
        QString sql = QString::fromLatin1(kFullNodeSelect)
            + QStringLiteral(" WHERE n.latency_ok = 1");
        if (hasExclude) {
            sql += QStringLiteral(" AND n.id != ?");
        }
        sql += QStringLiteral(" ORDER BY n.latency_ms ASC LIMIT ?");
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(sql);
        if (prepared.isErr()) {
            Log::warn(QStringLiteral("读取该分库的 url-test 候选失败: %1").arg(prepared.error()));
            return Result<QVector<Models::Node>>::withValue(nodes);
        }
        Statement statement = prepared.takeValue();
        int bindIndex = 1;
        if (hasExclude) {
            statement.bindText(bindIndex++, excludeId);
        }
        statement.bindInt64(bindIndex, limit);
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                Log::warn(
                    QStringLiteral("读取该分库的 url-test 候选失败: %1").arg(stepped.error()));
                break;
            }
            if (!stepped.value()) {
                break;
            }
            nodes.append(rowToNode(statement));
        }
        return Result<QVector<Models::Node>>::withValue(nodes);
    };

    Result<QVector<Models::Node>> loaded = runOverScopes<Models::Node>(scopes, loader);
    if (loaded.isErr()) {
        return loaded;
    }
    QVector<Models::Node> nodes = loaded.takeValue();
    std::stable_sort(nodes.begin(), nodes.end(), [](const Models::Node &left,
                                                    const Models::Node &right) {
        const qint64 leftMs = left.latencyMs.value_or(std::numeric_limits<qint64>::max());
        const qint64 rightMs = right.latencyMs.value_or(std::numeric_limits<qint64>::max());
        return leftMs < rightMs;
    });
    if (nodes.size() > limit) {
        nodes.resize(limit);
    }
    return Result<QVector<Models::Node>>::withValue(std::move(nodes));
}

Result<bool> Database::deleteNode(const QString &id)
{
    Result<std::shared_ptr<ConnectionPool>> pool = findNodePool(id);
    if (pool.isErr()) {
        return Result<bool>::withError(pool.error());
    }
    if (!pool.value()) {
        return Result<bool>::withValue(false);
    }
    Transaction transaction(pool.value()->acquire());
    Connection *connection = transaction.connection();
    Result<Statement> prepared =
        connection->prepare(QStringLiteral("DELETE FROM nodes WHERE id = ?"));
    if (prepared.isErr()) {
        return Result<bool>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, id);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<bool>::withError(stepped.error());
    }
    const qint64 affected = connection->changes();
    Result<Statement> payloads =
        connection->prepare(QStringLiteral("DELETE FROM node_payloads WHERE node_id = ?"));
    if (payloads.isOk()) {
        Statement payloadStatement = payloads.takeValue();
        payloadStatement.bindText(1, id);
        payloadStatement.step();
    }
    const Status committed = transaction.commit();
    if (committed.isErr()) {
        return Result<bool>::withError(committed.error());
    }
    return Result<bool>::withValue(affected > 0);
}

Result<qint64> Database::clearNodes()
{
    qint64 deleted = 0;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }
        Transaction transaction(pool.value()->acquire());
        Connection *connection = transaction.connection();
        const Status cleared = connection->execute(QStringLiteral("DELETE FROM nodes"));
        if (cleared.isErr()) {
            return Result<qint64>::withError(cleared.error());
        }
        deleted += connection->changes();
        connection->executeIgnoringFailure(QStringLiteral("DELETE FROM node_payloads"));
        const Status committed = transaction.commit();
        if (committed.isErr()) {
            return Result<qint64>::withError(committed.error());
        }
    }
    return Result<qint64>::withValue(deleted);
}

Result<qint64> Database::dedupeNodes()
{
    qint64 deleted = 0;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }

        QVector<Models::Node> nodes;
        {
            auto lease = pool.value()->acquire();
            Result<Statement> prepared = lease->prepare(QString::fromLatin1(kFullNodeSelect));
            if (prepared.isErr()) {
                return Result<qint64>::withError(prepared.error());
            }
            Statement statement = prepared.takeValue();
            for (;;) {
                const Result<bool> stepped = statement.step();
                if (stepped.isErr()) {
                    return Result<qint64>::withError(stepped.error());
                }
                if (!stepped.value()) {
                    break;
                }
                nodes.append(rowToNode(statement));
            }
        }

        QHash<QString, QPair<QString, qint64>> best;
        QVector<QString> deleteIds;
        for (const Models::Node &node : nodes) {
            const QString key = Parser::nodeFingerprint(node);
            const qint64 score = dedupeKeepScore(node);
            const auto found = best.constFind(key);
            if (found == best.constEnd()) {
                best.insert(key, {node.id, score});
            } else if (score > found->second) {
                deleteIds.append(found->first);
                best.insert(key, {node.id, score});
            } else {
                deleteIds.append(node.id);
            }
        }

        if (deleteIds.isEmpty()) {
            continue;
        }
        Transaction transaction(pool.value()->acquire());
        Connection *connection = transaction.connection();
        for (int offset = 0; offset < deleteIds.size(); offset += kDeleteChunkSize) {
            const QVector<QString> chunk = deleteIds.mid(offset, kDeleteChunkSize);
            const QString marks = placeholders(chunk.size());
            Result<Statement> nodesDelete = connection->prepare(
                QStringLiteral("DELETE FROM nodes WHERE id IN (%1)").arg(marks));
            if (nodesDelete.isErr()) {
                return Result<qint64>::withError(nodesDelete.error());
            }
            Statement statement = nodesDelete.takeValue();
            Result<Statement> payloadsDelete = connection->prepare(
                QStringLiteral("DELETE FROM node_payloads WHERE node_id IN (%1)").arg(marks));
            for (int index = 0; index < chunk.size(); ++index) {
                statement.bindText(index + 1, chunk.at(index));
            }
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<qint64>::withError(stepped.error());
            }
            deleted += connection->changes();
            if (payloadsDelete.isOk()) {
                Statement payloadStatement = payloadsDelete.takeValue();
                for (int index = 0; index < chunk.size(); ++index) {
                    payloadStatement.bindText(index + 1, chunk.at(index));
                }
                payloadStatement.step();
            }
        }
        const Status committed = transaction.commit();
        if (committed.isErr()) {
            return Result<qint64>::withError(committed.error());
        }
    }
    return Result<qint64>::withValue(deleted);
}

Result<qint64> Database::deleteDeadNodes()
{
    qint64 deleted = 0;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }
        Transaction transaction(pool.value()->acquire());
        Connection *connection = transaction.connection();

        QVector<QString> deadIds;
        {
            Result<Statement> prepared =
                connection->prepare(QStringLiteral("SELECT id FROM nodes WHERE latency_ok = 0"));
            if (prepared.isErr()) {
                return Result<qint64>::withError(prepared.error());
            }
            Statement statement = prepared.takeValue();
            for (;;) {
                const Result<bool> stepped = statement.step();
                if (stepped.isErr()) {
                    return Result<qint64>::withError(stepped.error());
                }
                if (!stepped.value()) {
                    break;
                }
                deadIds.append(statement.text(0));
            }
        }

        if (!deadIds.isEmpty()) {
            Result<Statement> prepared =
                connection->prepare(QStringLiteral("DELETE FROM nodes WHERE latency_ok = 0"));
            if (prepared.isErr()) {
                return Result<qint64>::withError(prepared.error());
            }
            Statement statement = prepared.takeValue();
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<qint64>::withError(stepped.error());
            }
            deleted += connection->changes();
            for (int offset = 0; offset < deadIds.size(); offset += kDeleteChunkSize) {
                const QVector<QString> chunk = deadIds.mid(offset, kDeleteChunkSize);
                Result<Statement> payloads = connection->prepare(
                    QStringLiteral("DELETE FROM node_payloads WHERE node_id IN (%1)")
                        .arg(placeholders(chunk.size())));
                if (payloads.isErr()) {
                    continue;
                }
                Statement payloadStatement = payloads.takeValue();
                for (int index = 0; index < chunk.size(); ++index) {
                    payloadStatement.bindText(index + 1, chunk.at(index));
                }
                payloadStatement.step();
            }
        }
        const Status committed = transaction.commit();
        if (committed.isErr()) {
            return Result<qint64>::withError(committed.error());
        }
    }
    return Result<qint64>::withValue(deleted);
}

Result<qint64> Database::deduplicatePhysical()
{
    Result<QVector<Models::Node>> loaded = loadNodes(QString());
    if (loaded.isErr()) {
        return Result<qint64>::withError(loaded.error());
    }
    const QVector<Models::Node> allNodes = loaded.value();
    if (allNodes.isEmpty()) {
        return Result<qint64>::withValue(0);
    }

    QHash<QString, QPair<QString, qint64>> best;
    QVector<QString> deleteIds;
    for (const Models::Node &node : allNodes) {
        // 物理端点去重：同一 server:port 只留分数最高的那个。
        const QString key = node.server.trimmed().toLower() + QStringLiteral(":")
            + QString::number(node.port);
        const qint64 score = dedupeKeepScore(node);
        const auto found = best.constFind(key);
        if (found == best.constEnd()) {
            best.insert(key, {node.id, score});
        } else if (score > found->second) {
            deleteIds.append(found->first);
            best.insert(key, {node.id, score});
        } else {
            deleteIds.append(node.id);
        }
    }
    if (deleteIds.isEmpty()) {
        return Result<qint64>::withValue(0);
    }

    qint64 deleted = 0;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }
        auto lease = pool.value()->acquire();
        for (int offset = 0; offset < deleteIds.size(); offset += kDeleteChunkSize) {
            const QVector<QString> chunk = deleteIds.mid(offset, kDeleteChunkSize);
            const QString marks = placeholders(chunk.size());
            Result<Statement> nodesDelete =
                lease->prepare(QStringLiteral("DELETE FROM nodes WHERE id IN (%1)").arg(marks));
            if (nodesDelete.isErr()) {
                return Result<qint64>::withError(nodesDelete.error());
            }
            Statement statement = nodesDelete.takeValue();
            for (int index = 0; index < chunk.size(); ++index) {
                statement.bindText(index + 1, chunk.at(index));
            }
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<qint64>::withError(stepped.error());
            }
            deleted += lease->changes();
            Result<Statement> payloads =
                lease->prepare(QStringLiteral("DELETE FROM node_payloads WHERE node_id IN (%1)")
                                   .arg(marks));
            if (payloads.isOk()) {
                Statement payloadStatement = payloads.takeValue();
                for (int index = 0; index < chunk.size(); ++index) {
                    payloadStatement.bindText(index + 1, chunk.at(index));
                }
                payloadStatement.step();
            }
        }
    }
    return Result<qint64>::withValue(deleted);
}

Result<int> Database::cleanAndBeautifyNodeNames()
{
    int updated = 0;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<int>::withError(pool.error());
        }

        QVector<QPair<QString, QString>> renames;
        {
            auto lease = pool.value()->acquire();
            Result<Statement> prepared =
                lease->prepare(QStringLiteral("SELECT id, name, server, port, country FROM nodes"));
            if (prepared.isErr()) {
                return Result<int>::withError(prepared.error());
            }
            Statement statement = prepared.takeValue();
            for (;;) {
                const Result<bool> stepped = statement.step();
                if (stepped.isErr()) {
                    return Result<int>::withError(stepped.error());
                }
                if (!stepped.value()) {
                    break;
                }
                const QString id = statement.text("id");
                const QString oldName = statement.text("name");
                const QString server = statement.text("server");
                const int port = int(statement.int64("port"));
                const QString country = statement.text("country");
                const QString cleaned =
                    Parser::cleanNodeName(oldName, country.isNull() ? QString() : country, server,
                                          port);
                if (cleaned != oldName) {
                    renames.append({cleaned, id});
                }
            }
        }

        // 一个分库一个事务：逐条 UPDATE 在 19000 个节点上就是 19000 次提交。
        Transaction transaction(pool.value()->acquire());
        Connection *connection = transaction.connection();
        for (const auto &rename : renames) {
            Result<Statement> prepared =
                connection->prepare(QStringLiteral("UPDATE nodes SET name = ? WHERE id = ?"));
            if (prepared.isErr()) {
                return Result<int>::withError(prepared.error());
            }
            Statement statement = prepared.takeValue();
            statement.bindText(1, rename.first);
            statement.bindText(2, rename.second);
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<int>::withError(stepped.error());
            }
            if (connection->changes() > 0) {
                updated += 1;
            }
        }
        const Status committed = transaction.commit();
        if (committed.isErr()) {
            return Result<int>::withError(committed.error());
        }
    }
    return Result<int>::withValue(updated);
}

Result<qint64> Database::scrubInvalidHosts()
{
    // 只需要 id/server/上次测速的内核与方式：绝不能走 load_nodes，那会把全部
    // 节点的 raw+config 读出来并逐条解析 JSON，和首屏渲染抢时间。
    QVector<SpeedUpdate> items;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }
        auto lease = pool.value()->acquire();
        Result<Statement> prepared = lease->prepare(
            QStringLiteral("SELECT id, server, latency_kernel, latency_test_kind FROM nodes"));
        if (prepared.isErr()) {
            return Result<qint64>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<qint64>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            const QString server = statement.text("server");
            if (HostUtil::isPublicProxyHost(server) && !HostUtil::isDecoyWebsiteHost(server)) {
                continue;
            }
            SpeedUpdate update;
            update.id = statement.text("id");
            update.ok = false;
            update.latencyKernel = statement.text("latency_kernel");
            update.latencyTestKind = statement.text("latency_test_kind");
            items.append(update);
        }
    }
    const qint64 changed = items.size();
    const Status written = updateNodeSpeedsBatch(items);
    if (written.isErr()) {
        return Result<qint64>::withError(written.error());
    }
    return Result<qint64>::withValue(changed);
}

Result<qint64> Database::deleteInvalidHosts()
{
    // 按分库分组批量删：以前是每个节点都 find_node_pool 扫一遍全部分库，
    // 19000 个节点 × 34 个库 ≈ 65 万次查询。
    QHash<QString, QVector<QString>> byScope;
    QVector<QString> scopeOrder;
    const QVector<QString> scopes = nodeScopes(QString());
    for (const QString &scope : scopes) {
        if (!QFileInfo::exists(nodeDbPath(scope))) {
            continue;
        }
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }
        auto lease = pool.value()->acquire();
        Result<Statement> prepared =
            lease->prepare(QStringLiteral("SELECT id, server FROM nodes"));
        if (prepared.isErr()) {
            return Result<qint64>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        const QString key = scope.isNull() ? QString() : scope;
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<qint64>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            const QString server = statement.text("server");
            if (!HostUtil::isPublicProxyHost(server)) {
                if (!byScope.contains(key)) {
                    scopeOrder.append(key);
                }
                byScope[key].append(statement.text("id"));
            }
        }
    }

    qint64 deleted = 0;
    for (const QString &key : scopeOrder) {
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(key);
        if (pool.isErr()) {
            return Result<qint64>::withError(pool.error());
        }
        const QVector<QString> ids = byScope.value(key);
        for (int offset = 0; offset < ids.size(); offset += kIdChunkSize) {
            const QVector<QString> chunk = ids.mid(offset, kIdChunkSize);
            Transaction transaction(pool.value()->acquire());
            Connection *connection = transaction.connection();
            const QString marks = placeholders(chunk.size());
            Result<Statement> nodesDelete =
                connection->prepare(QStringLiteral("DELETE FROM nodes WHERE id IN (%1)").arg(marks));
            if (nodesDelete.isErr()) {
                return Result<qint64>::withError(nodesDelete.error());
            }
            Statement statement = nodesDelete.takeValue();
            for (int index = 0; index < chunk.size(); ++index) {
                statement.bindText(index + 1, chunk.at(index));
            }
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                return Result<qint64>::withError(stepped.error());
            }
            deleted += connection->changes();
            Result<Statement> payloads =
                connection->prepare(QStringLiteral("DELETE FROM node_payloads WHERE node_id IN (%1)")
                                        .arg(marks));
            if (payloads.isOk()) {
                Statement payloadStatement = payloads.takeValue();
                for (int index = 0; index < chunk.size(); ++index) {
                    payloadStatement.bindText(index + 1, chunk.at(index));
                }
                payloadStatement.step();
            }
            const Status committed = transaction.commit();
            if (committed.isErr()) {
                return Result<qint64>::withError(committed.error());
            }
        }
    }
    return Result<qint64>::withValue(deleted);
}

namespace {

/// 同一批里同指纹的节点先按名称质量、再按更新时间择优保留（对应 Rust 的 batch 折叠）。
void foldBatchByFingerprint(const QVector<Models::Node> &nodes, QVector<QString> &keyOrder,
                            QHash<QString, const Models::Node *> &batch)
{
    for (const Models::Node &node : nodes) {
        const QString key = Parser::nodeFingerprint(node);
        const auto found = batch.constFind(key);
        if (found == batch.constEnd()) {
            keyOrder.append(key);
            batch.insert(key, &node);
            continue;
        }
        const int oldQuality = Database::nameQualityScore(found.value()->name);
        const int newQuality = Database::nameQualityScore(node.name);
        if (newQuality > oldQuality
            || (newQuality == oldQuality
                && node.updatedAt.nanos >= found.value()->updatedAt.nanos)) {
            batch.insert(key, &node);
        }
    }
}

}

/// 用新一批节点整体替换该源的分片：这一次没出现的就删掉（普通订阅链接的语义）。
Result<int> Database::replaceSourceNodes(const QString &sourceId,
                                         const QVector<Models::Node> &nodes)
{
    return writeSourceNodes(sourceId, nodes, false);
}

/// Telegram 频道这类"零散发布"的源：新出现的并进来了，这一页看不到的旧节点**不删**。
/// 淘汰仍旧交给连续测不通（fail_streak）那条线，不靠拉取覆盖。
Result<int> Database::accumulateSourceNodes(const QString &sourceId,
                                            const QVector<Models::Node> &nodes)
{
    return writeSourceNodes(sourceId, nodes, true);
}

Result<int> Database::writeSourceNodes(const QString &sourceId,
                                       const QVector<Models::Node> &nodes, bool keepMissing)
{
    QVector<QString> keyOrder;
    QHash<QString, const Models::Node *> batch;
    foldBatchByFingerprint(nodes, keyOrder, batch);

    const QString destination = nodeDbPath(sourceId);
    const QString temporary = QDir(nodeRoot_)
                                  .filePath(QStringLiteral(".%1-%2.tmp")
                                                .arg(sourceId, Ids::newUuidV4()));
    Result<std::shared_ptr<ConnectionPool>> tempPool = ConnectionPool::create(temporary, 1);
    if (tempPool.isErr()) {
        return Result<int>::withError(tempPool.error());
    }
    const Status tempMigrated = migrateNodePool(*tempPool.value());
    if (tempMigrated.isErr()) {
        QFile::remove(temporary);
        return Result<int>::withError(tempMigrated.error());
    }

    Result<std::shared_ptr<ConnectionPool>> taken = takeNodePool(sourceId);
    if (taken.isErr()) {
        QFile::remove(temporary);
        return Result<int>::withError(taken.error());
    }
    std::shared_ptr<ConnectionPool> oldPool = taken.takeValue();
    if (!oldPool) {
        Result<std::shared_ptr<ConnectionPool>> connected =
            connectNodePool(destination);
        if (connected.isErr()) {
            QFile::remove(temporary);
            return Result<int>::withError(connected.error());
        }
        oldPool = connected.takeValue();
    }

    static const char *kExistingSelect =
        R"(SELECT n.protocol, n.server, n.port, n.name,
                      COALESCE(NULLIF(p.config, ''), n.config, '{}') AS config,
                      COALESCE(NULLIF(p.raw, ''), n.raw, '') AS raw,
                      n.country, n.created_at, n.updated_at,
                      n.latency_ms, n.latency_ok, n.download_kbps,
                      n.latency_kernel, n.latency_test_kind, n.latency_tested_at
               FROM nodes n
               LEFT JOIN node_payloads p ON n.id = p.node_id
               WHERE n.source_id = ?)";

    struct PreservedLatency {
        std::optional<qint64> latencyMs;
        std::optional<bool> latencyOk;
        std::optional<double> downloadKbps;
        QString latencyKernel;
        QString latencyTestKind;
        QString latencyTestedAt;
    };
    QHash<QString, PreservedLatency> preserved;
    QHash<QString, Models::Node> leftovers;

    {
        auto lease = oldPool->acquire();
        Result<Statement> prepared = lease->prepare(QString::fromLatin1(kExistingSelect));
        if (prepared.isErr()) {
            QFile::remove(temporary);
            return Result<int>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        statement.bindText(1, sourceId);
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr()) {
                QFile::remove(temporary);
                return Result<int>::withError(stepped.error());
            }
            if (!stepped.value()) {
                break;
            }
            Models::Node carried;
            carried.sourceId = sourceId;
            carried.protocol = statement.text("protocol");
            carried.name = statement.text("name");
            carried.server = statement.text("server");
            carried.port = int(statement.int64("port"));
            carried.config = JsonUtil::parseOrEmptyObject(statement.text("config"));
            carried.raw = statement.text("raw");
            carried.country = statement.text("country");
            carried.latencyMs = statement.optionalInt64("latency_ms");
            carried.latencyOk = statement.optionalBool("latency_ok");
            carried.downloadKbps = statement.optionalDouble("download_kbps");
            carried.latencyKernel = statement.text("latency_kernel");
            carried.latencyTestKind = statement.text("latency_test_kind");
            carried.latencyTestedAt =
                TimeUtil::OptionalUtc::fromText(statement.text("latency_tested_at"));
            const QString createdAt = statement.text("created_at");
            const QString updatedAt = statement.text("updated_at");
            carried.createdAt = createdAt.isNull() || createdAt.trimmed().isEmpty()
                ? TimeUtil::OptionalUtc::now()
                : parseDt(createdAt);
            carried.updatedAt = updatedAt.isNull() || updatedAt.trimmed().isEmpty()
                ? TimeUtil::OptionalUtc::now()
                : parseDt(updatedAt);

            const QString key = Parser::nodeFingerprint(carried);
            if (keepMissing && !batch.contains(key)) {
                leftovers.insert(key, carried);
            }
            PreservedLatency record;
            record.latencyMs = carried.latencyMs;
            record.latencyOk = carried.latencyOk;
            record.downloadKbps = carried.downloadKbps;
            record.latencyKernel = carried.latencyKernel;
            record.latencyTestKind = carried.latencyTestKind;
            record.latencyTestedAt = carried.latencyTestedAt.toDbText();
            preserved.insert(key, record);
        }
    }

    int inserted = 0;
    int carriedOver = 0;
    Transaction transaction(tempPool.value()->acquire());
    for (const QString &key : keyOrder) {
        const Models::Node *source = batch.value(key);
        Models::Node fresh = *source;
        fresh.sourceId = sourceId;
        const auto found = preserved.constFind(key);
        if (found != preserved.constEnd()) {
            fresh.latencyMs = found->latencyMs;
            fresh.latencyOk = found->latencyOk;
            fresh.downloadKbps = found->downloadKbps;
            fresh.latencyKernel = found->latencyKernel;
            fresh.latencyTestKind = found->latencyTestKind;
            fresh.latencyTestedAt = TimeUtil::OptionalUtc::fromText(found->latencyTestedAt);
        }
        const Status stored = insertNode(transaction, fresh);
        if (stored.isErr()) {
            QFile::remove(temporary);
            return Result<int>::withError(stored.error());
        }
        inserted += 1;
    }

    // 累积模式：这一次页面上看不见的旧节点原样搬回去（延迟记录也一起搬）。
    for (auto it = leftovers.constBegin(); it != leftovers.constEnd(); ++it) {
        Models::Node carry = it.value();
        carry.id = Ids::newUuidV4();
        const Status stored = insertNode(transaction, carry);
        if (stored.isErr()) {
            QFile::remove(temporary);
            return Result<int>::withError(stored.error());
        }
        carriedOver += 1;
    }

    const Status committed = transaction.commit();
    if (committed.isErr()) {
        QFile::remove(temporary);
        return Result<int>::withError(committed.error());
    }
    // 把事务持有的连接立刻还给池子：tempPool 只有 1 个连接，而 Transaction 的 Lease
    // 原本要等函数结束才析构，下面的 wal_checkpoint 再 acquire 就会永久阻塞在
    // available_.wait() 上（Rust 侧 commit() 消费事务，连接当场归还，不会死锁）。
    transaction = Transaction();
    {
        auto lease = tempPool.value()->acquire();
        const Status checkpoint = lease->execute(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        if (checkpoint.isErr()) {
            QFile::remove(temporary);
            return Result<int>::withError(checkpoint.error());
        }
    }
    // 释放两侧连接后才能改名：Windows 上打开着的句柄会让 rename 失败。
    tempPool.value().reset();
    oldPool.reset();

    if (QFileInfo::exists(destination)) {
        const QString backup =
            QDir(nodeRoot_).filePath(QStringLiteral(".%1-%2.bak").arg(sourceId, Ids::newUuidV4()));
        if (!QFile::rename(destination, backup)) {
            QFile::remove(temporary);
            return Result<int>::withError(
                QStringLiteral("备份旧订阅库失败: %1").arg(destination));
        }
        if (!QFile::rename(temporary, destination)) {
            QFile::rename(backup, destination);
            return Result<int>::withError(
                QStringLiteral("替换订阅库失败: %1").arg(destination));
        }
        QFile::remove(backup);
    } else if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return Result<int>::withError(
            QStringLiteral("写入订阅库失败: %1").arg(destination));
    }
    return Result<int>::withValue(inserted + carriedOver);
}

Result<int> Database::importNodes(const QVector<Models::Node> &nodes)
{
    if (nodes.isEmpty()) {
        return Result<int>::withValue(0);
    }
    QVector<QString> keyOrder;
    QHash<QString, const Models::Node *> batch;
    foldBatchByFingerprint(nodes, keyOrder, batch);

    Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(QString());
    if (pool.isErr()) {
        return Result<int>::withError(pool.error());
    }
    Transaction transaction(pool.value()->acquire());
    int stored = 0;
    for (const QString &key : keyOrder) {
        Models::Node fresh = *batch.value(key);
        fresh.sourceId = QString();
        const Status inserted = insertNode(transaction, fresh);
        if (inserted.isErr()) {
            return Result<int>::withError(inserted.error());
        }
        stored += 1;
    }
    const Status committed = transaction.commit();
    if (committed.isErr()) {
        return Result<int>::withError(committed.error());
    }
    return Result<int>::withValue(stored);
}
