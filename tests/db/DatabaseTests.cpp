#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "db/Database.h"

namespace {

const char *kProductionDbEnv = "VCC_PRODUCTION_DB";

QString productionDbPath()
{
    QString path = QString::fromLocal8Bit(qgetenv(kProductionDbEnv));
    if (path.isEmpty()) {
        path = QStringLiteral("D:/software/VPN Parser/data/vpn-parser.db");
    }
    return path;
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

TEST(DatabaseSchema, NodePoolSplitsPayloadOutOfTheLeafPages)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    auto database = opened.takeValue();

    // 老库形状：raw/config 直接写在 nodes 行里。
    {
        auto lease = database->mainPool()->acquire();
        ASSERT_TRUE(
            lease->execute(QStringLiteral(
                "INSERT INTO nodes (id, source_id, protocol, name, server, port, raw, config,"
                " created_at, updated_at) VALUES ('legacy-1', NULL, 'ss', 'n', '1.2.3.4', 443,"
                " 'ss://a@b', '{\"cipher\":\"aes-256-gcm\"}', '2026-01-01T00:00:00+00:00',"
                " '2026-01-01T00:00:00+00:00')"))
                .isOk());
    }
    ASSERT_TRUE(database->migrate().isOk());

    {
        auto lease = database->mainPool()->acquire();
        Result<Statement> prepared =
            lease->prepare(QStringLiteral("SELECT raw, config FROM nodes WHERE id = 'legacy-1'"));
        ASSERT_TRUE(prepared.isOk()) << prepared.error().toStdString();
        Statement statement = prepared.takeValue();
        ASSERT_TRUE(statement.step().value());
        // 大字段被搬走，叶子页只剩元数据。
        EXPECT_EQ(statement.text("raw").toStdString(), std::string(""));
        EXPECT_EQ(statement.text("config").toStdString(), std::string("{}"));
    }

    // 但读出来必须还是原样（COALESCE + node_payloads）。
    const Result<Models::Node> node = database->getNode(QStringLiteral("legacy-1"));
    ASSERT_TRUE(node.isOk()) << node.error().toStdString();
    EXPECT_EQ(node.value().raw.toStdString(), std::string("ss://a@b"));
    EXPECT_EQ(JsonUtil::dump(node.value().config).toStdString(),
              std::string("{\"cipher\":\"aes-256-gcm\"}"));
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
