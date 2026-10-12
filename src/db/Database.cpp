#include "db/Database.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

#include <algorithm>

#include "core/Ids.h"
#include "core/JsonUtil.h"
#include "core/Log.h"

namespace {

/// sqlx 侧的 idle_timeout（`db/mod.rs:49-53` 主库 60s、`db/mod.rs:131-135` 订阅分库 30s）。
/// 每条 SQLite 连接自带 ~2MB 页缓存，不回收就是 34 源 × 2 条常驻。
constexpr qint64 kMainPoolIdleTimeoutMs = 60 * 1000;
constexpr qint64 kNodePoolIdleTimeoutMs = 30 * 1000;

/// 只有真的不知道节点属于哪个源时才并发扫所有分库。
struct PoolProbe {
    std::shared_ptr<ConnectionPool> pool;
    bool found = false;
};

}

Result<std::shared_ptr<Database>> Database::connect(const QString &dbPath)
{
    const QFileInfo info(dbPath);
    const QDir parent = info.absoluteDir();
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        return Result<std::shared_ptr<Database>>::withError(
            QStringLiteral("无法创建数据库目录 %1").arg(parent.absolutePath()));
    }

    Result<std::shared_ptr<ConnectionPool>> mainPool =
        ConnectionPool::create(dbPath, 4, kMainPoolIdleTimeoutMs);
    if (mainPool.isErr()) {
        return Result<std::shared_ptr<Database>>::withError(
            QStringLiteral("连接 sqlite %1 失败: %2").arg(dbPath, mainPool.error()));
    }

    auto database = std::shared_ptr<Database>(new Database());
    database->mainPath_ = info.absoluteFilePath();
    database->main_ = mainPool.takeValue();
    database->nodeRoot_ = parent.absoluteFilePath(QStringLiteral("subscriptions"));
    if (!QDir().mkpath(database->nodeRoot_)) {
        return Result<std::shared_ptr<Database>>::withError(
            QStringLiteral("无法创建订阅库目录 %1").arg(database->nodeRoot_));
    }

    const Status migrated = database->migrate();
    if (migrated.isErr()) {
        return Result<std::shared_ptr<Database>>::withError(migrated.error());
    }
    return Result<std::shared_ptr<Database>>::withValue(std::move(database));
}

QString Database::nodeDbStem(const QString &sourceId)
{
    // null QString 是 Rust 的 None（未分组库）；空串是 Some("")，形状非法，
    // 与 Rust 一样告警后落到未分组库，两者最终都用同一个库文件。
    if (sourceId.isNull()) {
        return kUngroupedSourceId;
    }
    const bool valid = !sourceId.isEmpty() && !sourceId.startsWith(QLatin1Char('.'))
        && std::all_of(sourceId.begin(), sourceId.end(), [](QChar character) {
               return (character.isLetterOrNumber() && character.unicode() < 128)
                   || character == QLatin1Char('-') || character == QLatin1Char('_')
                   || character == QLatin1Char('.');
           });
    if (!valid) {
        Log::warn(QStringLiteral("非法 source_id，已按未分组节点库处理: %1").arg(sourceId));
        return kUngroupedSourceId;
    }
    return sourceId;
}

QString Database::nodePoolCacheKey(const QString &sourceId)
{
    return nodeDbStem(sourceId);
}

QString Database::nodeDbPath(const QString &sourceId) const
{
    return QDir(nodeRoot_).filePath(nodeDbStem(sourceId) + QStringLiteral(".db"));
}

Result<std::shared_ptr<ConnectionPool>> Database::connectNodePool(const QString &path)
{
    Result<std::shared_ptr<ConnectionPool>> pool =
        ConnectionPool::create(path, 2, kNodePoolIdleTimeoutMs);
    if (pool.isErr()) {
        return Result<std::shared_ptr<ConnectionPool>>::withError(
            QStringLiteral("连接订阅库 %1 失败: %2").arg(path, pool.error()));
    }
    const Status migrated = migrateNodePool(*pool.value());
    if (migrated.isErr()) {
        return Result<std::shared_ptr<ConnectionPool>>::withError(migrated.error());
    }
    return pool;
}

void Database::reapIdlePools()
{
    QVector<std::shared_ptr<ConnectionPool>> pools;
    {
        QMutexLocker locker(&poolsMutex_);
        if (main_) {
            pools.append(main_);
        }
        for (auto it = nodePools_.constBegin(); it != nodePools_.constEnd(); ++it) {
            pools.append(it.value());
        }
    }
    // 锁外回收：reapIdle 自己会拿池内锁，别让 poolsMutex_ 套住一次磁盘句柄关闭。
    for (const std::shared_ptr<ConnectionPool> &pool : pools) {
        pool->reapIdle();
    }
}

Result<std::shared_ptr<ConnectionPool>> Database::openNodePool(const QString &sourceId)
{
    const QString key = nodePoolCacheKey(sourceId);
    {
        QMutexLocker locker(&poolsMutex_);
        const auto found = nodePools_.constFind(key);
        if (found != nodePools_.constEnd()) {
            return Result<std::shared_ptr<ConnectionPool>>::withValue(found.value());
        }
    }
    Result<std::shared_ptr<ConnectionPool>> pool = connectNodePool(nodeDbPath(sourceId));
    if (pool.isErr()) {
        return pool;
    }
    QMutexLocker locker(&poolsMutex_);
    const auto found = nodePools_.constFind(key);
    if (found != nodePools_.constEnd()) {
        return Result<std::shared_ptr<ConnectionPool>>::withValue(found.value());
    }
    nodePools_.insert(key, pool.value());
    return pool;
}

Result<std::shared_ptr<ConnectionPool>> Database::takeNodePool(const QString &sourceId)
{
    const QString key = nodePoolCacheKey(sourceId);
    {
        QMutexLocker locker(&poolsMutex_);
        const auto found = nodePools_.constFind(key);
        if (found != nodePools_.constEnd()) {
            const std::shared_ptr<ConnectionPool> pool = found.value();
            nodePools_.erase(found);
            return Result<std::shared_ptr<ConnectionPool>>::withValue(pool);
        }
    }
    const QString path = nodeDbPath(sourceId);
    if (!QFileInfo::exists(path)) {
        return Result<std::shared_ptr<ConnectionPool>>::withValue(nullptr);
    }
    return connectNodePool(path);
}

void Database::closeNodePool(const QString &sourceId)
{
    const QString key = nodePoolCacheKey(sourceId);
    QMutexLocker locker(&poolsMutex_);
    nodePools_.remove(key);
}

void Database::trimCachedPools()
{
    QMutexLocker locker(&poolsMutex_);
    nodePools_.clear();
}

Result<std::shared_ptr<ConnectionPool>> Database::findNodePool(const QString &nodeId)
{
    const QVector<QString> scopes = nodeScopes(QString());
    QVector<QString> paths;
    paths.reserve(scopes.size());
    for (const QString &scope : scopes) {
        const QString path = nodeDbPath(scope);
        if (QFileInfo::exists(path)) {
            paths.append(path);
        }
    }
    if (paths.isEmpty()) {
        return Result<std::shared_ptr<ConnectionPool>>::withValue(nullptr);
    }

    auto probe = [this, nodeId](const QString &path) -> PoolProbe {
        const QString scope = QFileInfo(path).completeBaseName();
        Result<std::shared_ptr<ConnectionPool>> opened = openNodePool(scope);
        if (opened.isErr() || !opened.value()) {
            return PoolProbe{};
        }
        auto lease = opened.value()->acquire();
        Result<Statement> prepared =
            lease->prepare(QStringLiteral("SELECT 1 FROM nodes WHERE id = ?"));
        if (prepared.isErr()) {
            return PoolProbe{};
        }
        Statement statement = prepared.takeValue();
        statement.bindText(1, nodeId);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr() || !stepped.value()) {
            return PoolProbe{};
        }
        return PoolProbe{opened.value(), true};
    };

    QThreadPool *pool = QThreadPool::globalInstance();
    QVector<QFuture<PoolProbe>> futures;
    futures.reserve(paths.size());
    for (const QString &path : paths) {
        futures.append(QtConcurrent::run(pool, probe, path));
    }
    for (QFuture<PoolProbe> &future : futures) {
        future.waitForFinished();
        const PoolProbe probeResult = future.result();
        if (probeResult.found) {
            return Result<std::shared_ptr<ConnectionPool>>::withValue(probeResult.pool);
        }
    }
    return Result<std::shared_ptr<ConnectionPool>>::withValue(nullptr);
}

QVector<QString> Database::nodeScopes(const QString &sourceId)
{
    if (sourceId == kUngroupedSourceId) {
        return {QString()};
    }
    if (!sourceId.isNull()) {
        return {sourceId};
    }
    QVector<QString> scopes;
    const Result<QVector<Models::Source>> sources = listSources();
    if (sources.isOk()) {
        for (const Models::Source &source : sources.value()) {
            scopes.append(source.id);
        }
    }
    scopes.append(QString());
    return scopes;
}

Models::Node Database::rowToNode(const Statement &row)
{
    Models::Node node;
    node.id = row.text("id");
    node.sourceId = row.text("source_id");
    node.protocol = row.text("protocol");
    node.name = row.text("name");
    node.server = row.text("server");
    node.port = int(row.int64("port"));
    node.raw = row.text("raw");
    node.config = JsonUtil::parseOrEmptyObject(row.text("config"));
    node.country = row.text("country");
    node.latencyMs = row.optionalInt64("latency_ms");
    node.latencyOk = row.optionalBool("latency_ok");
    node.downloadKbps = row.optionalDouble("download_kbps");
    node.latencyKernel = row.text("latency_kernel");
    node.latencyTestKind = row.text("latency_test_kind");
    node.latencyTestedAt = TimeUtil::OptionalUtc::fromText(row.text("latency_tested_at"));
    node.createdAt = parseDt(row.text("created_at"));
    node.updatedAt = parseDt(row.text("updated_at"));
    return node;
}

Models::NodeSummary Database::rowToNodeSummary(const Statement &row)
{
    Models::NodeSummary summary;
    summary.id = row.text("id");
    summary.sourceId = row.text("source_id");
    summary.protocol = row.text("protocol");
    summary.name = row.text("name");
    summary.server = row.text("server");
    summary.port = int(row.int64("port"));
    summary.country = row.text("country");
    summary.latencyMs = row.optionalInt64("latency_ms");
    summary.latencyOk = row.optionalBool("latency_ok");
    summary.downloadKbps = row.optionalDouble("download_kbps");
    summary.latencyKernel = row.text("latency_kernel");
    summary.latencyTestKind = row.text("latency_test_kind");
    summary.latencyTestedAt = TimeUtil::OptionalUtc::fromText(row.text("latency_tested_at"));
    summary.updatedAt = parseDt(row.text("updated_at"));
    return summary;
}

TimeUtil::OptionalUtc Database::parseDt(const QString &text)
{
    const TimeUtil::OptionalUtc parsed = TimeUtil::OptionalUtc::fromText(text);
    return parsed.hasValue ? parsed : TimeUtil::OptionalUtc::now();
}

int Database::nameQualityScore(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }
    int score = 10;
    const QString lower = trimmed.toLower();
    if (lower.contains(QStringLiteral("telegram")) || lower.contains(QStringLiteral("join"))
        || lower.contains(QStringLiteral("http://")) || lower.contains(QStringLiteral("https://"))
        || lower.contains(QStringLiteral("t.me"))) {
        score -= 6;
    }
    if (trimmed.length() <= 48) {
        score += 2;
    }
    if (trimmed.length() > 80) {
        score -= 3;
    }
    return score;
}

qint64 Database::dedupeKeepScore(const Models::Node &node)
{
    qint64 score = 0;
    if (node.latencyOk.has_value()) {
        if (*node.latencyOk) {
            score += 1000000;
            if (node.latencyMs.has_value()) {
                const qint64 clamped = std::clamp<qint64>(*node.latencyMs, 0, 99999);
                score += 100000 - clamped;
            }
        } else {
            score += 10000;
        }
    } else {
        score += 50000;
    }
    if (node.downloadKbps.has_value()) {
        score += qint64(std::min(*node.downloadKbps, 10000.0));
    }
    score += nameQualityScore(node.name);
    // chrono 的 timestamp() 是秒，Rust 侧再整除 1e6，所以这一项实际恒为个位数：
    // 保留它只为与 Rust 的打分口径一致，不要"顺手"改成毫秒。
    score += (node.updatedAt.nanos / 1000000000LL) / 1000000LL;
    return score;
}
