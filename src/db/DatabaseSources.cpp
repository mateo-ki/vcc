#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include "core/Ids.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "db/TextOrder.h"

namespace {

const char *kSourceSelect = R"(SELECT id, name, url_template, enabled, remark, last_fetch_at,
                      last_status, last_node_count, last_resolved_url, last_etag,
                      last_modified, content_sha256, fail_streak, last_fetch_host,
                      push_disabled, created_at, updated_at
               FROM sources)";

using TextOrder::asciiLower;

}

Models::Source Database::rowToSource(const Statement &row)
{
    Models::Source source;
    source.id = row.text("id");
    source.name = row.text("name");
    source.urlTemplate = row.text("url_template");
    source.enabled = row.int64("enabled") != 0;
    source.remark = row.text("remark");
    source.lastFetchAt = TimeUtil::OptionalUtc::fromText(row.text("last_fetch_at"));
    source.lastStatus = row.text("last_status");
    source.lastNodeCount = row.int64("last_node_count");
    source.lastResolvedUrl = row.text("last_resolved_url");
    source.lastEtag = row.text("last_etag");
    source.lastModified = row.text("last_modified");
    source.contentSha256 = row.text("content_sha256");
    source.failStreak = row.isNull("fail_streak") ? 0 : row.int64("fail_streak");
    source.lastFetchHost = row.text("last_fetch_host");
    source.pushDisabled = !row.isNull("push_disabled") && row.int64("push_disabled") != 0;
    source.createdAt = parseDt(row.text("created_at"));
    source.updatedAt = parseDt(row.text("updated_at"));
    return source;
}

Result<QVector<Models::Source>> Database::listSources()
{
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QString::fromLatin1(kSourceSelect)
                       + QStringLiteral(" ORDER BY created_at DESC"));
    if (prepared.isErr()) {
        return Result<QVector<Models::Source>>::withError(prepared.error());
    }
    Statement row = prepared.takeValue();
    QVector<Models::Source> sources;
    for (;;) {
        const Result<bool> stepped = row.step();
        if (stepped.isErr()) {
            return Result<QVector<Models::Source>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        sources.append(rowToSource(row));
    }
    return Result<QVector<Models::Source>>::withValue(std::move(sources));
}

Result<Models::Source> Database::getSource(const QString &id)
{
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(
        QStringLiteral("%1 WHERE id = ?").arg(QString::fromLatin1(kSourceSelect)));
    if (prepared.isErr()) {
        return Result<Models::Source>::withError(prepared.error());
    }
    Statement row = prepared.takeValue();
    row.bindText(1, id);
    const Result<bool> stepped = row.step();
    if (stepped.isErr()) {
        return Result<Models::Source>::withError(stepped.error());
    }
    if (!stepped.value()) {
        return Result<Models::Source>::withValue(Models::Source());
    }
    return Result<Models::Source>::withValue(rowToSource(row));
}

Result<Models::Source> Database::createSource(const Models::CreateSource &input)
{
    Models::Source source;
    source.id = Ids::newUuidV4();
    source.name = input.name;
    source.urlTemplate = input.urlTemplate;
    source.enabled = input.enabled;
    source.remark = input.remark;
    source.createdAt = TimeUtil::OptionalUtc::now();
    source.updatedAt = TimeUtil::OptionalUtc::now();

    static const char *kInsert = R"(INSERT INTO sources
               (id, name, url_template, enabled, remark, last_fetch_at, last_status,
                last_node_count, created_at, updated_at)
               VALUES (?, ?, ?, ?, ?, NULL, NULL, 0, ?, ?))";
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(QString::fromLatin1(kInsert));
    if (prepared.isErr()) {
        return Result<Models::Source>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, source.id);
    statement.bindText(2, source.name);
    statement.bindText(3, source.urlTemplate);
    statement.bindInt64(4, source.enabled ? 1 : 0);
    statement.bindText(5, source.remark);
    statement.bindText(6, source.createdAt.toDbText());
    statement.bindText(7, source.updatedAt.toDbText());
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<Models::Source>::withError(stepped.error());
    }
    return Result<Models::Source>::withValue(source);
}

Result<Models::Source> Database::updateSource(const QString &id, const Models::UpdateSource &patch)
{
    Result<Models::Source> loaded = getSource(id);
    if (loaded.isErr()) {
        return loaded;
    }
    Models::Source source = loaded.takeValue();
    if (source.id.isEmpty()) {
        return Result<Models::Source>::withError(QStringLiteral("source not found"));
    }

    if (patch.name) {
        source.name = *patch.name;
    }
    if (patch.urlTemplate) {
        source.urlTemplate = *patch.urlTemplate;
    }
    if (patch.enabled) {
        source.enabled = *patch.enabled;
    }
    if (patch.remark) {
        source.remark = *patch.remark;
    }
    if (patch.pushDisabled) {
        source.pushDisabled = *patch.pushDisabled;
    }
    source.updatedAt = TimeUtil::OptionalUtc::now();

    static const char *kUpdate = R"(UPDATE sources SET name=?, url_template=?, enabled=?, remark=?, push_disabled=?,
                      updated_at=?
               WHERE id=?)";
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(QString::fromLatin1(kUpdate));
    if (prepared.isErr()) {
        return Result<Models::Source>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, source.name);
    statement.bindText(2, source.urlTemplate);
    statement.bindInt64(3, source.enabled ? 1 : 0);
    statement.bindText(4, source.remark);
    statement.bindInt64(5, source.pushDisabled ? 1 : 0);
    statement.bindText(6, source.updatedAt.toDbText());
    statement.bindText(7, id);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<Models::Source>::withError(stepped.error());
    }
    return Result<Models::Source>::withValue(source);
}

Result<bool> Database::deleteSource(const QString &id)
{
    {
        auto lease = main_->acquire();
        Result<Statement> prepared =
            lease->prepare(QStringLiteral("DELETE FROM sources WHERE id = ?"));
        if (prepared.isErr()) {
            return Result<bool>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        statement.bindText(1, id);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<bool>::withError(stepped.error());
        }
        if (lease->changes() == 0) {
            return Result<bool>::withValue(false);
        }
    }

    const QString path = nodeDbPath(id);
    closeNodePool(id);
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QString candidate = path + suffix;
        if (QFileInfo::exists(candidate) && !QFile::remove(candidate)) {
            return Result<bool>::withError(
                QStringLiteral("删除订阅库失败 %1").arg(candidate));
        }
    }
    return Result<bool>::withValue(true);
}

Status Database::markSourceFetched(const QString &id, const QString &status, qint64 nodeCount)
{
    const QString now = TimeUtil::nowRfc3339();
    auto lease = main_->acquire();
    {
        static const char *kUpdate = R"(UPDATE sources SET last_fetch_at=?, last_status=?, last_node_count=?, updated_at=?
               WHERE id=?)";
        Result<Statement> prepared = lease->prepare(QString::fromLatin1(kUpdate));
        if (prepared.isErr()) {
            return Status::failed(prepared.error());
        }
        Statement statement = prepared.takeValue();
        statement.bindText(1, now);
        statement.bindText(2, status);
        statement.bindInt64(3, nodeCount);
        statement.bindText(4, now);
        statement.bindText(5, id);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Status::failed(stepped.error());
        }
    }

    // 每次写状态都同时留一行历史：界面上"最近 7 次"色条靠它。只看 last_status 的话，
    // 一个源上周失败五次、今天成功一次就会显示成"正常"。
    const bool ok = status == QLatin1String("ok") || status == QLatin1String("not modified")
        || status == QLatin1String("content unchanged");
    Result<Statement> history = lease->prepare(
        QStringLiteral("INSERT INTO source_fetch_history (source_id, at, ok, node_count, status) "
                       "VALUES (?, ?, ?, ?, ?)"));
    if (history.isErr()) {
        return Status::failed(history.error());
    }
    Statement statement = history.takeValue();
    statement.bindText(1, id);
    statement.bindText(2, now);
    statement.bindInt64(3, ok ? 1 : 0);
    statement.bindInt64(4, nodeCount);
    statement.bindText(5, status);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }
    return Status::succeeded();
}

/// 连续网络失败计数，让不可达的源不再吃掉整轮重试预算。非网络失败（HTTP 404、
/// 解析错误）不动计数：它们不能证明主机不可达。
Result<qint64> Database::recordFetchOutcome(const QString &id, bool ok, bool networkFailure)
{
    auto lease = main_->acquire();
    if (ok) {
        Result<Statement> reset =
            lease->prepare(QStringLiteral("UPDATE sources SET fail_streak = 0 WHERE id = ?"));
        if (reset.isErr()) {
            return Result<qint64>::withError(reset.error());
        }
        Statement statement = reset.takeValue();
        statement.bindText(1, id);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<qint64>::withError(stepped.error());
        }
        return Result<qint64>::withValue(0);
    }
    if (networkFailure) {
        Result<Statement> bump = lease->prepare(
            QStringLiteral("UPDATE sources SET fail_streak = fail_streak + 1 WHERE id = ?"));
        if (bump.isErr()) {
            return Result<qint64>::withError(bump.error());
        }
        Statement statement = bump.takeValue();
        statement.bindText(1, id);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<qint64>::withError(stepped.error());
        }
    }
    Result<Statement> read =
        lease->prepare(QStringLiteral("SELECT fail_streak FROM sources WHERE id = ?"));
    if (read.isErr()) {
        return Result<qint64>::withError(read.error());
    }
    Statement statement = read.takeValue();
    statement.bindText(1, id);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<qint64>::withError(stepped.error());
    }
    return Result<qint64>::withValue(stepped.value() ? statement.int64(0) : 0);
}

/// 记住上一次成功拉取是哪家主机（直连或镜像） serving 的，下一轮从那儿开始。
Status Database::setSourceFetchHost(const QString &id, const QString &host)
{
    auto lease = main_->acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("UPDATE sources SET last_fetch_host = ? WHERE id = ?"));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, host);
    statement.bindText(2, id);
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}

Result<QSet<QString>> Database::seenGistIds()
{
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(QStringLiteral("SELECT gist_id FROM seen_gists"));
    if (prepared.isErr()) {
        return Result<QSet<QString>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    QSet<QString> ids;
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QSet<QString>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        ids.insert(statement.text(0));
    }
    return Result<QSet<QString>>::withValue(ids);
}

Status Database::markGistSeen(const QString &id, const QString &sourceId)
{
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(
        QStringLiteral("INSERT OR REPLACE INTO seen_gists (gist_id, source_id, first_seen_at) "
                       "VALUES (?, ?, ?)"));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, id);
    statement.bindText(2, sourceId);
    statement.bindText(3, TimeUtil::nowRfc3339());
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}

Status Database::updateSourceCache(const QString &id, const QString &resolvedUrl,
                                   const QString &etag, const QString &lastModified,
                                   const QString &contentSha256)
{
    static const char *kUpdate = R"(UPDATE sources SET last_resolved_url=?, last_etag=?, last_modified=?,
                      content_sha256=?, updated_at=? WHERE id=?)";
    auto lease = main_->acquire();
    Result<Statement> prepared = lease->prepare(QString::fromLatin1(kUpdate));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, resolvedUrl);
    statement.bindText(2, etag);
    statement.bindText(3, lastModified);
    statement.bindText(4, contentSha256);
    statement.bindText(5, TimeUtil::nowRfc3339());
    statement.bindText(6, id);
    const Result<bool> stepped = statement.step();
    return stepped.isErr() ? Status::failed(stepped.error()) : Status::succeeded();
}

/// 分析所有订阅源之间的重合度与同质化水平。
Result<QVector<Models::SourceHomogeneityReport>> Database::analyzeSourceHomogeneity()
{
    Result<QVector<Models::Source>> sources = listSources();
    if (sources.isErr()) {
        return Result<QVector<Models::SourceHomogeneityReport>>::withError(sources.error());
    }
    if (sources.value().isEmpty()) {
        return Result<QVector<Models::SourceHomogeneityReport>>::withValue(
            QVector<Models::SourceHomogeneityReport>());
    }

    Result<QVector<Models::NodeSummary>> summaries = loadNodeSummaries(QString());
    if (summaries.isErr()) {
        return Result<QVector<Models::SourceHomogeneityReport>>::withError(summaries.error());
    }
    const QVector<Models::NodeSummary> allNodes = summaries.takeValue();

    struct Endpoint {
        QString protocol;
        QString server;
        int port = 0;
    };
    auto endpointKey = [](const Models::NodeSummary &node) {
        return asciiLower(node.protocol) + QLatin1Char('|') + asciiLower(node.server.trimmed())
            + QLatin1Char('|') + QString::number(node.port);
    };

    // 建立 endpoint 索引: (protocol, server, port) -> Vec<(source_id, is_alive)>
    QHash<QString, QVector<QPair<QString, bool>>> endpointMap;
    for (const Models::NodeSummary &node : allNodes) {
        if (node.sourceId.isNull() || node.sourceId.trimmed().isEmpty()) {
            continue;
        }
        endpointMap[endpointKey(node)].append({node.sourceId, node.latencyOk.value_or(false)});
    }

    QVector<Models::SourceHomogeneityReport> reports;
    for (const Models::Source &source : sources.value()) {
        QHash<QString, bool> sourceEndpoints;
        for (const Models::NodeSummary &node : allNodes) {
            if (node.sourceId == source.id) {
                const QString key = endpointKey(node);
                bool &entry = sourceEndpoints[key];
                if (node.latencyOk.value_or(false)) {
                    entry = true;
                }
            }
        }

        const int totalNodes = sourceEndpoints.size();
        int aliveNodes = 0;
        int uniqueNodes = 0;
        int uniqueAliveNodes = 0;

        for (auto it = sourceEndpoints.constBegin(); it != sourceEndpoints.constEnd(); ++it) {
            const bool alive = it.value();
            if (alive) {
                aliveNodes += 1;
            }
            int otherCount = 0;
            const auto providers = endpointMap.constFind(it.key());
            if (providers != endpointMap.constEnd()) {
                for (const auto &provider : providers.value()) {
                    if (provider.first != source.id) {
                        otherCount += 1;
                    }
                }
            }
            if (otherCount == 0) {
                uniqueNodes += 1;
                if (alive) {
                    uniqueAliveNodes += 1;
                }
            }
        }

        const int duplicateNodes = qMax(0, totalNodes - uniqueNodes);
        const float overlapRatePct =
            totalNodes > 0 ? (float(duplicateNodes) / float(totalNodes)) * 100.0f : 0.0f;

        // 冗余源判定：
        // 1. 该源有节点，但独有可用节点为 0，且重合率 >= 85%（或自身存活为 0）
        // 2. 或重合率 >= 98%
        // 3. 或总节点为 0 且连续失败 >= 2 次
        const bool isRedundant = totalNodes > 0
            ? ((uniqueAliveNodes == 0 && (overlapRatePct >= 85.0f || aliveNodes == 0))
               || overlapRatePct >= 98.0f)
            : (source.failStreak >= 2);

        Models::SourceHomogeneityReport report;
        report.sourceId = source.id;
        report.sourceName = source.name;
        report.totalNodes = totalNodes;
        report.aliveNodes = aliveNodes;
        report.duplicateNodes = duplicateNodes;
        report.uniqueNodes = uniqueNodes;
        report.uniqueAliveNodes = uniqueAliveNodes;
        report.overlapRatePct = overlapRatePct;
        report.isRedundant = isRedundant;
        reports.append(report);
    }

    std::stable_sort(reports.begin(), reports.end(),
                     [](const Models::SourceHomogeneityReport &left,
                        const Models::SourceHomogeneityReport &right) {
                         if (left.isRedundant != right.isRedundant) {
                             return left.isRedundant;
                         }
                         if (left.overlapRatePct != right.overlapRatePct) {
                             return left.overlapRatePct > right.overlapRatePct;
                         }
                         return left.uniqueAliveNodes < right.uniqueAliveNodes;
                     });
    return Result<QVector<Models::SourceHomogeneityReport>>::withValue(std::move(reports));
}

/// 反向剔除同质化源：可选择批量禁用（保留配置关闭拉取）或彻底删除。
Result<Models::PruneHomogeneityResult> Database::pruneHomogenizedSources(
    const QVector<QString> &sourceIds, bool deleteRecords)
{
    Models::PruneHomogeneityResult result;
    result.deletedRecords = deleteRecords;
    for (const QString &id : sourceIds) {
        Result<Models::Source> loaded = getSource(id);
        if (loaded.isErr()) {
            return Result<Models::PruneHomogeneityResult>::withError(loaded.error());
        }
        const Models::Source source = loaded.value();
        if (source.id.isEmpty()) {
            continue;
        }
        result.affectedNames.append(source.name);
        if (deleteRecords) {
            const Result<bool> deleted = deleteSource(id);
            if (deleted.isErr()) {
                return Result<Models::PruneHomogeneityResult>::withError(deleted.error());
            }
        } else {
            Models::UpdateSource patch;
            patch.enabled = false;
            const Result<Models::Source> disabled = updateSource(id, patch);
            if (disabled.isErr()) {
                return Result<Models::PruneHomogeneityResult>::withError(disabled.error());
            }
        }
    }
    result.prunedCount = result.affectedNames.size();
    return Result<Models::PruneHomogeneityResult>::withValue(std::move(result));
}
