#include <QDir>
#include <QFileInfo>

#include "core/Ids.h"
#include "core/Log.h"
#include "db/Database.h"

namespace {

/// 一次迁移里所有语句都用同一条连接，避免 ALTER 与后续读列在不同连接间交错。
class Migrator {
public:
    explicit Migrator(ConnectionPool &pool) : pool_(pool) {}

    Status run(const QString &sql)
    {
        auto lease = pool_.acquire();
        return lease->execute(sql);
    }

    void runIgnoringFailure(const QString &sql)
    {
        auto lease = pool_.acquire();
        lease->executeIgnoringFailure(sql);
    }

    Result<bool> tableHasColumn(const QString &table, const QString &column)
    {
        auto lease = pool_.acquire();
        Result<Statement> prepared =
            lease->prepare(QStringLiteral("SELECT 1 FROM pragma_table_info(?) WHERE name = ?"));
        if (prepared.isErr()) {
            return Result<bool>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        statement.bindText(1, table);
        statement.bindText(2, column);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<bool>::withError(stepped.error());
        }
        return Result<bool>::withValue(stepped.value());
    }

    Result<qint64> scalarInt(const QString &sql)
    {
        auto lease = pool_.acquire();
        Result<Statement> prepared = lease->prepare(sql);
        if (prepared.isErr()) {
            return Result<qint64>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<qint64>::withError(stepped.error());
        }
        return Result<qint64>::withValue(stepped.value() ? statement.int64(0) : 0);
    }

private:
    ConnectionPool &pool_;
};

void ensureColumn(Migrator &migrator, const QString &table, const QString &column,
                  const QString &declaration)
{
    // 对应 Rust 的 `let _ = ALTER TABLE ... ADD COLUMN`：列已存在就是预期路径。
    migrator.runIgnoringFailure(
        QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, declaration));
}

}

Status Database::migrateNodePool(ConnectionPool &pool)
{
    Migrator migrator(pool);

    static const char *kNodesDdl = R"(CREATE TABLE IF NOT EXISTS nodes (
                id TEXT PRIMARY KEY NOT NULL,
                source_id TEXT,
                protocol TEXT NOT NULL,
                name TEXT NOT NULL,
                server TEXT NOT NULL,
                port INTEGER NOT NULL,
                raw TEXT NOT NULL DEFAULT '',
                config TEXT NOT NULL DEFAULT '{}',
                country TEXT,
                latency_ms INTEGER,
                latency_ok INTEGER,
                download_kbps REAL,
                latency_kernel TEXT,
                latency_test_kind TEXT,
                latency_tested_at TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                UNIQUE(protocol, server, port, name)
            ))";
    const Status nodesCreated = migrator.run(QString::fromLatin1(kNodesDdl));
    if (nodesCreated.isErr()) {
        return nodesCreated;
    }

    static const char *kPayloadsDdl = R"(CREATE TABLE IF NOT EXISTS node_payloads (
                node_id TEXT PRIMARY KEY NOT NULL,
                raw TEXT NOT NULL,
                config TEXT NOT NULL
            ))";
    const Status payloadsCreated = migrator.run(QString::fromLatin1(kPayloadsDdl));
    if (payloadsCreated.isErr()) {
        return payloadsCreated;
    }

    // 迁移既有节点数据库中的载荷至 node_payloads
    migrator.runIgnoringFailure(R"(INSERT OR IGNORE INTO node_payloads (node_id, raw, config)
               SELECT id, raw, config FROM nodes
               WHERE (raw != '' AND raw IS NOT NULL) OR (config != '' AND config IS NOT NULL))");

    // 清理 nodes 表中的冗余大字段以缩小叶子页面尺寸
    migrator.runIgnoringFailure(R"(UPDATE nodes SET raw = '', config = '{}'
               WHERE (raw != '' OR config != '{}') AND id IN (SELECT node_id FROM node_payloads))");

    static const char *kIndexes[] = {
        "CREATE INDEX IF NOT EXISTS idx_nodes_protocol ON nodes(protocol)",
        "CREATE INDEX IF NOT EXISTS idx_nodes_latency ON nodes(latency_ms)",
        "CREATE INDEX IF NOT EXISTS idx_nodes_latency_ok ON nodes(latency_ok)",
        "CREATE INDEX IF NOT EXISTS idx_nodes_test_kind ON nodes(latency_test_kind)",
        "CREATE INDEX IF NOT EXISTS idx_nodes_updated_at ON nodes(updated_at)",
        "CREATE INDEX IF NOT EXISTS idx_nodes_country ON nodes(country)",
        "CREATE INDEX IF NOT EXISTS idx_nodes_server_port ON nodes(server, port)",
        "CREATE INDEX IF NOT EXISTS idx_node_payloads_id ON node_payloads(node_id)",
    };
    for (const char *index : kIndexes) {
        const Status created = migrator.run(QLatin1String(index));
        if (created.isErr()) {
            return created;
        }
    }
    return Status::succeeded();
}

Status Database::migrate()
{
    Migrator migrator(*main_);

    static const char *kSourcesDdl = R"(CREATE TABLE IF NOT EXISTS sources (
                id TEXT PRIMARY KEY NOT NULL,
                name TEXT NOT NULL,
                url_template TEXT NOT NULL,
                enabled INTEGER NOT NULL DEFAULT 1,
                remark TEXT,
                last_fetch_at TEXT,
                last_status TEXT,
                last_node_count INTEGER NOT NULL DEFAULT 0,
                last_resolved_url TEXT,
                last_etag TEXT,
                last_modified TEXT,
                content_sha256 TEXT,
                fail_streak INTEGER NOT NULL DEFAULT 0,
                last_fetch_host TEXT,
                push_disabled INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            ))";
    const Status sourcesCreated = migrator.run(QString::fromLatin1(kSourcesDdl));
    if (sourcesCreated.isErr()) {
        return sourcesCreated;
    }

    const Status nodePoolMigrated = migrateNodePool(*main_);
    if (nodePoolMigrated.isErr()) {
        return nodePoolMigrated;
    }

    static const char *kSettingsDdl = R"(CREATE TABLE IF NOT EXISTS settings (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                auto_fetch_enabled INTEGER NOT NULL DEFAULT 0,
                auto_fetch_interval_min INTEGER NOT NULL DEFAULT 60,
                auto_speedtest_enabled INTEGER NOT NULL DEFAULT 0,
                fetch_concurrency INTEGER NOT NULL DEFAULT 4,
                fetch_timeout_secs INTEGER NOT NULL DEFAULT 30,
                speedtest_concurrency INTEGER NOT NULL DEFAULT 128,
                speedtest_timeout_ms INTEGER NOT NULL DEFAULT 1500,
                speedtest_download INTEGER NOT NULL DEFAULT 0,
                download_test_ms INTEGER NOT NULL DEFAULT 800,
                http_server_enabled INTEGER NOT NULL DEFAULT 0,
                close_to_tray INTEGER NOT NULL DEFAULT 1,
                last_auto_fetch_at TEXT,
                last_auto_speedtest_at TEXT
            ))";
    const Status settingsCreated = migrator.run(QString::fromLatin1(kSettingsDdl));
    if (settingsCreated.isErr()) {
        return settingsCreated;
    }

    migrator.runIgnoringFailure(
        "ALTER TABLE settings ADD COLUMN http_server_enabled INTEGER NOT NULL DEFAULT 0");

    static const char *kProxySettingsDdl = R"(CREATE TABLE IF NOT EXISTS proxy_settings (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                config_json TEXT NOT NULL DEFAULT ''
            ))";
    const Status proxyCreated = migrator.run(QString::fromLatin1(kProxySettingsDdl));
    if (proxyCreated.isErr()) {
        return proxyCreated;
    }
    const Status proxySeeded =
        migrator.run("INSERT OR IGNORE INTO proxy_settings (id, config_json) VALUES (1, '')");
    if (proxySeeded.isErr()) {
        return proxySeeded;
    }

    // Gist 发现的去重台账：独立于 sources 存在，用户删掉的 gist 源不会被重新加回。
    const Status seenGistsMigrated = migrateSeenGists();
    if (seenGistsMigrated.isErr()) {
        return seenGistsMigrated;
    }

    // 健康时间线 / 切换历史 / 规则命中 / 拉取历史 —— 一律在主库：
    // data/subscriptions/*.db 那些分片每次拉订阅都整库重建并改名，放那儿等于每天丢数据。
    const Status telemetryMigrated = migrateTelemetry();
    if (telemetryMigrated.isErr()) {
        return telemetryMigrated;
    }

    static const char *kMainIndexes[] = {
        "CREATE INDEX IF NOT EXISTS idx_sources_enabled ON sources(enabled)",
        "CREATE INDEX IF NOT EXISTS idx_seen_gists_time ON seen_gists(first_seen_at)",
    };
    for (const char *index : kMainIndexes) {
        const Status created = migrator.run(QLatin1String(index));
        if (created.isErr()) {
            return created;
        }
    }

    const Status settingsSeeded = migrator.run("INSERT OR IGNORE INTO settings (id) VALUES (1)");
    if (settingsSeeded.isErr()) {
        return settingsSeeded;
    }

    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("last_fetch_host"),
                 QStringLiteral("TEXT DEFAULT NULL"));
    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("push_disabled"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("fail_streak"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("last_resolved_url"),
                 QStringLiteral("TEXT"));
    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("last_etag"),
                 QStringLiteral("TEXT"));
    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("last_modified"),
                 QStringLiteral("TEXT"));
    ensureColumn(migrator, QStringLiteral("sources"), QStringLiteral("content_sha256"),
                 QStringLiteral("TEXT"));
    ensureColumn(migrator, QStringLiteral("nodes"), QStringLiteral("latency_kernel"),
                 QStringLiteral("TEXT"));
    ensureColumn(migrator, QStringLiteral("nodes"), QStringLiteral("latency_test_kind"),
                 QStringLiteral("TEXT"));
    ensureColumn(migrator, QStringLiteral("nodes"), QStringLiteral("download_kbps"),
                 QStringLiteral("REAL"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("exclude_keywords"),
                 QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    // 发布服务器地址与推送 token：以前只写 eframe storage，而 eframe 的 persistence
    // feature 是关掉的，所以从来没落过盘。
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("publisher_api"),
                 QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("publisher_token"),
                 QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("ui_theme"),
                 QStringLiteral("TEXT NOT NULL DEFAULT 'obsidian'"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("kernel_source"),
                 QStringLiteral("TEXT NOT NULL DEFAULT 'gh-proxy'"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("firewall_auto_allow"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("failover_latency_ms"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 1500"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("failover_probe_urls"),
                 QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("auto_failover"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("speedtest_auto_refill"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    // 这两条是"软件自己动"的开关，默认开（新装机器要能自己把内核下下来、
    // 加完订阅源要能自己测），但都能在系统设置里关掉。
    for (const char *column : {"auto_prepare_kernel", "speedtest_added_source"}) {
        ensureColumn(migrator, QStringLiteral("settings"), QLatin1String(column),
                     QStringLiteral("INTEGER NOT NULL DEFAULT 1"));
    }
    // 五个测速优化开关默认全部关闭，行为与老版本一致。
    for (const char *column : {"speedtest_parallel_kernels", "speedtest_persistent_kernel",
                               "speedtest_reuse_minutes", "speedtest_conditional_isolation",
                               "speedtest_download_top_n", "bandwidth_guard",
                               "bandwidth_min_kbps"}) {
        ensureColumn(migrator, QStringLiteral("settings"), QLatin1String(column),
                     QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    }
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("fetch_concurrency"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 4"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("fetch_timeout_secs"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 30"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("speedtest_download"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("download_test_ms"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 800"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("http_server_enabled"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(migrator, QStringLiteral("settings"), QStringLiteral("close_to_tray"),
                 QStringLiteral("INTEGER NOT NULL DEFAULT 1"));

    return migrateLegacyNodes();
}

Status Database::migrateSeenGists()
{
    Migrator migrator(*main_);
    migrator.runIgnoringFailure("DROP TABLE IF EXISTS seen_gists_new");

    static const char *kSeenGistsDdl = R"(CREATE TABLE IF NOT EXISTS seen_gists (
                gist_id TEXT PRIMARY KEY NOT NULL,
                source_id TEXT,
                first_seen_at TEXT NOT NULL
            ))";
    const Status created = migrator.run(QString::fromLatin1(kSeenGistsDdl));
    if (created.isErr()) {
        return created;
    }

    // 老版本表结构没有这三列：补列并把老数据搬过来。
    Result<bool> hasFirstSeenAt = migrator.tableHasColumn(QStringLiteral("seen_gists"),
                                                           QStringLiteral("first_seen_at"));
    if (hasFirstSeenAt.isOk() && !hasFirstSeenAt.value()) {
        migrator.runIgnoringFailure("ALTER TABLE seen_gists ADD COLUMN first_seen_at TEXT NOT "
                                    "NULL DEFAULT ''");
        migrator.runIgnoringFailure(
            "UPDATE seen_gists SET first_seen_at = COALESCE(created_at, '') WHERE first_seen_at = ''");
    }
    Result<bool> hasGistId = migrator.tableHasColumn(QStringLiteral("seen_gists"),
                                                     QStringLiteral("gist_id"));
    if (hasGistId.isOk() && !hasGistId.value()) {
        migrator.runIgnoringFailure(
            "ALTER TABLE seen_gists ADD COLUMN gist_id TEXT NOT NULL DEFAULT ''");
        migrator.runIgnoringFailure(
            "UPDATE seen_gists SET gist_id = COALESCE(id, '') WHERE gist_id = ''");
    }
    Result<bool> hasSourceId = migrator.tableHasColumn(QStringLiteral("seen_gists"),
                                                       QStringLiteral("source_id"));
    if (hasSourceId.isOk() && !hasSourceId.value()) {
        migrator.runIgnoringFailure("ALTER TABLE seen_gists ADD COLUMN source_id TEXT");
    }
    return Status::succeeded();
}

Status Database::migrateTelemetry()
{
    Migrator migrator(*main_);
    static const char *kStatements[] = {
        // 每一次连通性实测都落一行。表头那句「Google: 已连通」以前只看 state，
        // 内核日志里全是 dial i/o timeout 它照样画绿勾（2026-10-06 实测）。
        R"(CREATE TABLE IF NOT EXISTS egress_health (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                at TEXT NOT NULL,
                ok INTEGER NOT NULL,
                latency_ms INTEGER,
                error TEXT,
                node_id TEXT,
                node_name TEXT,
                kind TEXT NOT NULL
            ))",
        "CREATE INDEX IF NOT EXISTS idx_egress_health_at ON egress_health(at)",
        // 谁在什么时候把出口从哪个节点换到哪个节点、为什么换、当时延迟多少。
        R"(CREATE TABLE IF NOT EXISTS egress_switches (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                at TEXT NOT NULL,
                from_node TEXT,
                from_name TEXT,
                to_node TEXT,
                to_name TEXT,
                reason TEXT NOT NULL,
                latency_ms INTEGER,
                pinned INTEGER NOT NULL DEFAULT 0
            ))",
        "CREATE INDEX IF NOT EXISTS idx_egress_switches_at ON egress_switches(at)",
        // 规则命中按天累计：连接是瞬时的，/connections 每 1s 采一次，边采边加。
        R"(CREATE TABLE IF NOT EXISTS rule_hits (
                day TEXT NOT NULL,
                rule TEXT NOT NULL,
                target TEXT NOT NULL,
                hits INTEGER NOT NULL DEFAULT 0,
                first_at TEXT,
                last_at TEXT,
                PRIMARY KEY (day, rule, target)
            ))",
        R"(CREATE TABLE IF NOT EXISTS source_fetch_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                source_id TEXT NOT NULL,
                at TEXT NOT NULL,
                ok INTEGER NOT NULL,
                node_count INTEGER NOT NULL DEFAULT 0,
                status TEXT NOT NULL
            ))",
        "CREATE INDEX IF NOT EXISTS idx_fetch_history_source ON source_fetch_history(source_id, at)",
    };
    for (const char *statement : kStatements) {
        const Status run = migrator.run(QString::fromLatin1(statement));
        if (run.isErr()) {
            return run;
        }
    }
    return Status::succeeded();
}

/// 兼容旧版本：若主库 nodes 仍有数据，将其按 source_id 迁入分库并清空主库 nodes。
Status Database::migrateLegacyNodes()
{
    Migrator migrator(*main_);
    const Result<qint64> legacyCount =
        migrator.scalarInt(QStringLiteral("SELECT COUNT(*) FROM nodes"));
    const qint64 count = legacyCount.isOk() ? legacyCount.value() : 0;
    if (count == 0) {
        return Status::succeeded();
    }
    Log::info(QStringLiteral("把 %1 个旧主库节点迁进分库").arg(count));

    static const char *kSelect = R"(SELECT n.id, n.source_id, n.protocol, n.name, n.server, n.port,
                      COALESCE(NULLIF(p.raw, ''), n.raw, '') AS raw,
                      COALESCE(NULLIF(p.config, ''), n.config, '{}') AS config,
                      n.country, n.latency_ms, n.latency_ok, n.download_kbps,
                      n.latency_kernel, n.latency_test_kind, n.latency_tested_at,
                      n.created_at, n.updated_at
               FROM nodes n
               LEFT JOIN node_payloads p ON n.id = p.node_id)";

    QHash<QString, QVector<Models::Node>> grouped;
    QVector<QString> groupOrder;
    {
        auto lease = main_->acquire();
        Result<Statement> prepared = lease->prepare(QString::fromLatin1(kSelect));
        if (prepared.isErr()) {
            return Status::failed(prepared.error());
        }
        Statement statement = prepared.takeValue();
        for (;;) {
            const Result<bool> stepped = statement.step();
            if (stepped.isErr() || !stepped.value()) {
                break;
            }
            const Models::Node node = rowToNode(statement);
            const QString key = node.sourceId.isNull() ? QString() : node.sourceId;
            if (!grouped.contains(key)) {
                groupOrder.append(key);
            }
            grouped[key].append(node);
        }
    }

    for (const QString &sourceId : groupOrder) {
        const QString scope = sourceId.isEmpty() ? QString() : sourceId;
        Result<std::shared_ptr<ConnectionPool>> pool = openNodePool(scope);
        if (pool.isErr()) {
            return Status::failed(pool.error());
        }
        Transaction transaction(pool.value()->acquire());
        for (const Models::Node &node : grouped.value(sourceId)) {
            const Status inserted = insertNode(transaction, node);
            if (inserted.isErr()) {
                return inserted;
            }
        }
        const Status committed = transaction.commit();
        if (committed.isErr()) {
            return committed;
        }
    }

    const Status cleared = migrator.run("DELETE FROM nodes");
    if (cleared.isErr()) {
        return cleared;
    }
    migrator.runIgnoringFailure("DELETE FROM node_payloads");
    return Status::succeeded();
}
