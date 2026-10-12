#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include "db/Database.h"

namespace {

const char *kProductionDbEnv = "VCC_PRODUCTION_DB";

QString productionDbPath()
{
    // 只认环境变量：仓库里不留任何机器上的默认路径 —— 这条用例读的是**活库**，
    // 拿默认值去猜一个真实安装目录，等于把别人的隐私路径写进公共代码，
    // 而且换台机器还可能误读到一个不相干的库。没设就直接跳过。
    return QString::fromLocal8Bit(qgetenv(kProductionDbEnv));
}

/// 连库带 -wal/-shm 一起复制：WAL 里可能还有未 checkpoint 的写入。
bool copyDatabasePair(const QString &source, const QString &destination)
{
    if (!QFile::copy(source, destination)) {
        return false;
    }
    for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QString extra = source + suffix;
        if (QFileInfo::exists(extra)) {
            QFile::remove(destination + suffix);
            QFile::copy(extra, destination + suffix);
        }
    }
    return true;
}

QSet<QString> tableNames(ConnectionPool &pool)
{
    QSet<QString> names;
    auto lease = pool.acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"));
    if (prepared.isErr()) {
        return names;
    }
    Statement statement = prepared.takeValue();
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr() || !stepped.value()) {
            break;
        }
        names.insert(statement.text(0));
    }
    return names;
}

QSet<QString> columnNames(ConnectionPool &pool, const QString &table)
{
    QSet<QString> names;
    auto lease = pool.acquire();
    Result<Statement> prepared =
        lease->prepare(QStringLiteral("SELECT name FROM pragma_table_info(?)"));
    if (prepared.isErr()) {
        return names;
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, table);
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr() || !stepped.value()) {
            break;
        }
        names.insert(statement.text(0));
    }
    return names;
}

}

TEST(DatabaseSchema, FreshDatabaseHasEveryTableFromSchemaRs)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));

    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    auto database = opened.takeValue();

    const QSet<QString> tables = tableNames(*database->mainPool());
    for (const char *table : {"sources", "nodes", "node_payloads", "settings", "proxy_settings",
                              "seen_gists", "egress_health", "egress_switches", "rule_hits",
                              "source_fetch_history"}) {
        EXPECT_TRUE(tables.contains(QLatin1String(table))) << table;
    }

    const QSet<QString> settings = columnNames(*database->mainPool(), QStringLiteral("settings"));
    // ensure_column 列表里的每一项都要在库里。
    for (const char *column : {"exclude_keywords", "publisher_api", "publisher_token", "ui_theme",
                               "kernel_source", "firewall_auto_allow", "failover_latency_ms",
                               "auto_failover", "speedtest_auto_refill", "auto_prepare_kernel",
                               "speedtest_added_source", "speedtest_parallel_kernels",
                               "speedtest_persistent_kernel", "speedtest_reuse_minutes",
                               "speedtest_conditional_isolation", "speedtest_download_top_n",
                               "bandwidth_guard", "bandwidth_min_kbps", "fetch_concurrency",
                               "fetch_timeout_secs", "speedtest_download", "download_test_ms",
                               "http_server_enabled", "close_to_tray"}) {
        EXPECT_TRUE(settings.contains(QLatin1String(column))) << column;
    }

    const QSet<QString> sources = columnNames(*database->mainPool(), QStringLiteral("sources"));
    for (const char *column : {"last_resolved_url", "last_etag", "last_modified", "content_sha256",
                               "fail_streak", "last_fetch_host", "push_disabled"}) {
        EXPECT_TRUE(sources.contains(QLatin1String(column))) << column;
    }

    // 单行表必须已播种，否则设置读写会打到空行上。
    const Result<Models::AppSettings> settingsLoaded = database->getSettings();
    ASSERT_TRUE(settingsLoaded.isOk()) << settingsLoaded.error().toStdString();
    EXPECT_TRUE(settingsLoaded.value().closeToTray);
    EXPECT_TRUE(settingsLoaded.value().autoPrepareKernel);
    EXPECT_TRUE(settingsLoaded.value().speedtestAddedSource);
}

/// 老库形状：raw/config 内联在 nodes 行里。迁移把载荷搬进 node_payloads，叶子页只剩
/// 元数据，但读出来必须还是原样（COALESCE + LEFT JOIN node_payloads）。
TEST(DatabaseSchema, NodePoolSplitsPayloadOutOfTheLeafPages)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    auto database = opened.takeValue();

    const QString sourceId = QStringLiteral("11112222-3333-4444-5555-666677778888");
    const QString shardPath = database->nodeDbPath(sourceId);
    {
        Result<std::shared_ptr<ConnectionPool>> raw = ConnectionPool::create(shardPath, 1);
        ASSERT_TRUE(raw.isOk()) << raw.error().toStdString();
        auto lease = raw.value()->acquire();
        ASSERT_TRUE(lease->execute(QStringLiteral(
            "CREATE TABLE nodes (id TEXT PRIMARY KEY NOT NULL, source_id TEXT,"
            " protocol TEXT NOT NULL, name TEXT NOT NULL, server TEXT NOT NULL,"
            " port INTEGER NOT NULL, raw TEXT NOT NULL DEFAULT '',"
            " config TEXT NOT NULL DEFAULT '{}', country TEXT,"
            " latency_ms INTEGER, latency_ok INTEGER, download_kbps REAL,"
            " latency_kernel TEXT, latency_test_kind TEXT, latency_tested_at TEXT,"
            " created_at TEXT NOT NULL, updated_at TEXT NOT NULL)")).isOk());
        ASSERT_TRUE(lease
                        ->execute(QStringLiteral("INSERT INTO nodes (id, source_id, protocol,"
                                                  " name, server, port, raw, config, created_at,"
                                                  " updated_at) VALUES ('n1','%1','ss','s',"
                                                  " '1.2.3.4',443,'ss://a@b',"
                                                  " '{\"cipher\":\"aes-256-gcm\"}',"
                                                  " '2026-01-01T00:00:00+00:00',"
                                                  " '2026-01-01T00:00:00+00:00')")
                                      .arg(sourceId))
                        .isOk());
    }

    const Result<QVector<Models::Node>> loaded = database->loadNodes(sourceId);
    ASSERT_TRUE(loaded.isOk()) << loaded.error().toStdString();
    ASSERT_EQ(loaded.value().size(), 1);
    EXPECT_EQ(loaded.value().first().raw.toStdString(), std::string("ss://a@b"));
    EXPECT_EQ(JsonUtil::dump(loaded.value().first().config).toStdString(),
              std::string("{\"cipher\":\"aes-256-gcm\"}"));

    Result<std::shared_ptr<ConnectionPool>> shard = database->openNodePool(sourceId);
    ASSERT_TRUE(shard.isOk()) << shard.error().toStdString();
    auto shardLease = shard.value()->acquire();
    Result<Statement> check = shardLease->prepare(QStringLiteral(
        "SELECT (SELECT raw FROM node_payloads WHERE node_id = 'n1') AS payload,"
        "       (SELECT raw FROM nodes WHERE id = 'n1') AS leaf"));
    ASSERT_TRUE(check.isOk()) << check.error().toStdString();
    Statement row = check.takeValue();
    ASSERT_TRUE(row.step().value());
    EXPECT_EQ(row.text("payload").toStdString(), std::string("ss://a@b"));
    EXPECT_EQ(row.text("leaf").toStdString(), std::string(""));
}

/// 老主库里的 nodes 行按 source_id 迁进分库，然后主库清空（Rust migrate_legacy_nodes）。
TEST(DatabaseSchema, LegacyMainDbNodesMoveIntoShardsAndMainIsCleared)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    auto database = opened.takeValue();

    {
        auto lease = database->mainPool()->acquire();
        ASSERT_TRUE(lease
                        ->execute(QStringLiteral(
                            "INSERT INTO nodes (id, source_id, protocol, name, server, port,"
                            " raw, config, created_at, updated_at) VALUES ('legacy-1', NULL,"
                            " 'ss', 'n', '1.2.3.4', 443, 'ss://a@b',"
                            " '{\"cipher\":\"aes-256-gcm\"}', '2026-01-01T00:00:00+00:00',"
                            " '2026-01-01T00:00:00+00:00')"))
                        .isOk());
    }
    ASSERT_TRUE(database->migrate().isOk());

    auto lease = database->mainPool()->acquire();
    Result<Statement> count = lease->prepare(QStringLiteral("SELECT COUNT(*) AS c FROM nodes"));
    ASSERT_TRUE(count.isOk()) << count.error().toStdString();
    Statement row = count.takeValue();
    ASSERT_TRUE(row.step().value());
    EXPECT_EQ(row.int64("c"), 0);

    const Result<QVector<Models::Node>> inShard = database->loadNodes(kUngroupedSourceId);
    ASSERT_TRUE(inShard.isOk()) << inShard.error().toStdString();
    ASSERT_EQ(inShard.value().size(), 1);
    EXPECT_EQ(inShard.value().first().id.toStdString(), std::string("legacy-1"));
    EXPECT_EQ(inShard.value().first().raw.toStdString(), std::string("ss://a@b"));
}

TEST(DatabaseSchema, UngroupedShardPathIsUsedForIllegalSourceIds)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    auto database = opened.takeValue();

    // source_id 可能来自 HTTP 查询参数：带路径分隔符的一律落到未分组库，不能建到目录外。
    EXPECT_EQ(Database::nodeDbStem(QStringLiteral("../../evil")).toStdString(),
              kUngroupedSourceId.toStdString());
    EXPECT_EQ(Database::nodeDbStem(QStringLiteral(".hidden")).toStdString(),
              kUngroupedSourceId.toStdString());
    EXPECT_EQ(Database::nodeDbStem(QString()).toStdString(), kUngroupedSourceId.toStdString());
    EXPECT_EQ(Database::nodeDbStem(QStringLiteral("00ffe96a-0ff2-4085-b407-d44a5531cd97"))
                  .toStdString(),
              std::string("00ffe96a-0ff2-4085-b407-d44a5531cd97"));
}

TEST(DatabaseProduction, ReadsRealRustDatabaseAndKeepsTimestampsByteIdentical)
{
    const QString production = productionDbPath();
    if (!QFileInfo::exists(production)) {
        GTEST_SKIP() << "没有找到生产库：" << production.toStdString();
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString copied = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    ASSERT_TRUE(copyDatabasePair(production, copied));
    // 分库也复制过来，node_root 会指向同一个 subscriptions 目录。
    QDir(directory.path()).mkpath(QStringLiteral("subscriptions"));
    const QDir sourceShards = QFileInfo(production).absoluteDir().filePath(QStringLiteral("subscriptions"));
    const QDir targetShards = QDir(directory.path()).filePath(QStringLiteral("subscriptions"));
    int copiedShards = 0;
    for (const QString &entry : QDir(sourceShards).entryList(QDir::Files)) {
        if (!entry.endsWith(QStringLiteral(".db"))) {
            continue;
        }
        if (copyDatabasePair(QDir(sourceShards).filePath(entry),
                             QDir(targetShards).filePath(entry))) {
            copiedShards += 1;
        }
    }
    ASSERT_GT(copiedShards, 0);

    Result<std::shared_ptr<Database>> opened = Database::connect(copied);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    auto database = opened.takeValue();

    const Result<QVector<Models::Source>> sources = database->listSources();
    ASSERT_TRUE(sources.isOk()) << sources.error().toStdString();
    ASSERT_FALSE(sources.value().isEmpty());

    for (const Models::Source &source : sources.value()) {
        ASSERT_TRUE(source.createdAt.hasValue) << source.id.toStdString();
        // 读出来的 created_at 再格式化一次必须一模一样（chrono 的 9/6/3/0 位小数收缩）。
        const QString roundTripped =
            TimeUtil::OptionalUtc::fromText(source.createdAt.toDbText()).toDbText();
        EXPECT_EQ(roundTripped, source.createdAt.toDbText()) << source.id.toStdString();
        if (!source.lastFetchAt.isNull()) {
            EXPECT_EQ(TimeUtil::OptionalUtc::fromText(source.lastFetchAt.toDbText()).toDbText(),
                      source.lastFetchAt.toDbText())
                << source.id.toStdString();
        }
    }

    // 分片里老库的 raw/config 是内联的，migrate 之后要搬进 node_payloads 且读回不变。
    const QString firstSourceId = sources.value().first().id;
    const Result<QVector<Models::Node>> nodes = database->loadNodes(firstSourceId);
    ASSERT_TRUE(nodes.isOk()) << nodes.error().toStdString();
    if (!nodes.value().isEmpty()) {
        const Models::Node &node = nodes.value().first();
        EXPECT_FALSE(node.raw.isNull());
        EXPECT_TRUE(node.config.is_object());
        EXPECT_EQ(node.sourceId, firstSourceId);
    }

    const Result<Models::AppSettings> settings = database->getSettings();
    ASSERT_TRUE(settings.isOk()) << settings.error().toStdString();
    // Rust 库里 fetch_concurrency 是 24，读出来必须被夹到 1..8 的合法区间。
    EXPECT_GE(settings.value().fetchConcurrency, 1);
    EXPECT_LE(settings.value().fetchConcurrency, 8);

    const Result<QString> proxyConfig = database->getProxyConfigJsonText();
    ASSERT_TRUE(proxyConfig.isOk()) << proxyConfig.error().toStdString();
    if (!proxyConfig.value().trimmed().isEmpty()) {
        const Result<Json> parsed = JsonUtil::parse(proxyConfig.value());
        ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
        EXPECT_TRUE(parsed.value().contains("mixed_port"));
        EXPECT_TRUE(parsed.value().contains("routing_mode"));
    }
}

/// 连接池空闲回收（Rust 那边由 sqlx 的 idle_timeout 做：主库 60s、订阅分库 30s）。
/// 不回收 = 34 源 × 2 条 SQLite 句柄（每条自带 ~2MB 页缓存）常驻，实测就是那 36MB。
namespace {

QString poolProbePath(const QTemporaryDir &directory, const QString &name)
{
    return QDir(directory.path()).filePath(name + QStringLiteral(".db"));
}

std::shared_ptr<ConnectionPool> makePool(const QString &path, int max, qint64 idleMs)
{
    Result<std::shared_ptr<ConnectionPool>> created = ConnectionPool::create(path, max, idleMs);
    EXPECT_TRUE(created.isOk()) << created.error().toStdString();
    return created.value();
}

void seedTable(ConnectionPool &pool)
{
    auto lease = pool.acquire();
    ASSERT_TRUE(lease->execute(QStringLiteral("CREATE TABLE IF NOT EXISTS probe (x TEXT)")).isOk());
    ASSERT_TRUE(lease->execute(QStringLiteral("DELETE FROM probe")).isOk());
    ASSERT_TRUE(
        lease->execute(QStringLiteral("INSERT INTO probe VALUES ('alive')")).isOk());
}

QString readProbe(ConnectionPool &pool)
{
    auto lease = pool.acquire();
    Result<Statement> prepared = lease->prepare(QStringLiteral("SELECT x FROM probe"));
    if (prepared.isErr()) {
        return prepared.error();
    }
    Statement statement = prepared.takeValue();
    const Result<bool> stepped = statement.step();
    if (stepped.isErr() || !stepped.value()) {
        return QStringLiteral("<no row>");
    }
    return statement.text(0);
}

}

TEST(ConnectionPoolReap, idleConnectionsShrinkTheirCacheAndStayUsable)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = poolProbePath(directory, QStringLiteral("reap"));

    auto pool = makePool(path, 3, /*idleTimeoutMs=*/40);
    seedTable(*pool);
    EXPECT_EQ(pool->maxConnections(), 3);
    EXPECT_EQ(pool->shrunkConnections(), 0);

    // 还没到空闲线：一条都不该动。
    pool->reapIdle();
    EXPECT_EQ(pool->shrunkConnections(), 0);

    QThread::msleep(120);
    pool->reapIdle();
    EXPECT_EQ(pool->shrunkConnections(), 3);
    // 关键：句柄都还在，读写照常（回收只缩页缓存，不改并发语义）。
    EXPECT_EQ(pool->maxConnections(), 3);
    EXPECT_EQ(readProbe(*pool), QStringLiteral("alive"));

    // 用过的那条要重新计入，不能永远被当成已回收。
    { auto lease = pool->acquire(); }
    EXPECT_EQ(pool->shrunkConnections(), 2);

    QThread::msleep(120);
    pool->reapIdle();
    EXPECT_EQ(pool->shrunkConnections(), 3);
    EXPECT_EQ(readProbe(*pool), QStringLiteral("alive"));
}

TEST(ConnectionPoolReap, busyConnectionIsNeverShrunkUnderneathItsOwner)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto pool = makePool(poolProbePath(directory, QStringLiteral("busy")), 2, /*idleTimeoutMs=*/30);
    seedTable(*pool);

    {
        auto lease = pool->acquire();
        ASSERT_TRUE(lease->execute(QStringLiteral("PRAGMA user_version = 7")).isOk());
        QThread::msleep(120);
        pool->reapIdle();
        // 借走的那条不动，另一条空闲超时可以。
        EXPECT_EQ(pool->shrunkConnections(), 1);
        // 借用者手里的连接依然可用（没被从下面抽走）。
        Result<Statement> prepared = lease->prepare(QStringLiteral("PRAGMA user_version"));
        ASSERT_TRUE(prepared.isOk()) << prepared.error().toStdString();
        Statement statement = prepared.takeValue();
        ASSERT_TRUE(statement.step().isOk());
        EXPECT_EQ(statement.text(0), QStringLiteral("7"));
    }
    // 同池嵌套借两条是本仓库真实存在的调用形态：回收之后两条都必须还能借到（关槽会死锁）。
    auto first = pool->acquire();
    auto second = pool->acquire();
    EXPECT_TRUE(first.isValid());
    EXPECT_TRUE(second.isValid());
}

TEST(ConnectionPoolReap, zeroTimeoutKeepsEveryConnectionWarm)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto pool = makePool(poolProbePath(directory, QStringLiteral("noexpire")), 2, 0);
    seedTable(*pool);
    QThread::msleep(120);
    pool->reapIdle();
    EXPECT_EQ(pool->shrunkConnections(), 0);
    EXPECT_EQ(readProbe(*pool), QStringLiteral("alive"));
}
