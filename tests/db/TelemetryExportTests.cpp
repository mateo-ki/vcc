#include <gtest/gtest.h>

#include <QChar>
#include <QDateTime>
#include <QDir>
#include <QPair>
#include <QString>
#include <QTemporaryDir>
#include <QTimeZone>

#include <initializer_list>
#include <memory>
#include <optional>

#include "core/TimeUtil.h"
#include "db/ConnectionPool.h"
#include "db/Database.h"
#include "db/DatabaseExport.h"
#include "db/DatabaseTelemetry.h"
#include "db/Sqlite.h"
#include "models/Source.h"

namespace {

constexpr qint64 kNanosPerSec = 1000000000LL;
constexpr qint64 kSecondsPerDay = 86400LL;
constexpr qint64 kRetentionSwitchesDays = 90;

/// 这个环境手敲 CJK 会静默丢字符，所以中文一律按码点构造，注释里给出原文。
QString ucs4(std::initializer_list<char32_t> codePoints)
{
    return QString::fromUcs4(codePoints.begin(), qsizetype(codePoints.size()));
}

// 唯一节点
QString onlyNode()
{
    return ucs4({0x552F, 0x4E00, 0x8282, 0x70B9});
}

// 没有可用替补：TCP 复检过 8 个候选
QString missReasonProbe()
{
    return ucs4({0x6CA1, 0x6709, 0x53EF, 0x7528, 0x66FF, 0x8865, 0xFF1A, 0x0054, 0x0043, 0x0050,
                 0x0020, 0x590D, 0x68C0, 0x8FC7, 0x0020, 0x0038, 0x0020, 0x4E2A, 0x5019, 0x9009});
}

// 没有可用替补：库里也找不出第二个测通过的节点
QString missReasonNoSecondNode()
{
    return ucs4({0x6CA1, 0x6709, 0x53EF, 0x7528, 0x66FF, 0x8865, 0xFF1A, 0x5E93, 0x91CC, 0x4E5F,
                 0x627E, 0x4E0D, 0x51FA, 0x7B2C, 0x4E8C, 0x4E2A, 0x6D4B, 0x901A, 0x8FC7, 0x7684,
                 0x8282, 0x70B9});
}

// 节点一
QString nodeOne()
{
    return ucs4({0x8282, 0x70B9, 0x4E00});
}

// 节点二
QString nodeTwo()
{
    return ucs4({0x8282, 0x70B9, 0x4E8C});
}

// 连接超时
QString connectionTimeout()
{
    return ucs4({0x8FDE, 0x63A5, 0x8D85, 0x65F6});
}

// TLS 握手失败
QString tlsHandshakeFailed()
{
    return ucs4({0x0054, 0x004C, 0x0053, 0x0020, 0x63E1, 0x624B, 0x5931, 0x8D25});
}

// 探测超时
QString probeTimeout()
{
    return ucs4({0x63A2, 0x6D4B, 0x8D85, 0x65F6});
}

// 用户手动切换
QString manualSwitch()
{
    return ucs4({0x7528, 0x6237, 0x624B, 0x52A8, 0x5207, 0x6362});
}

// " 接管"（record_at 里拼在 to 之后的原因后缀）
QString takeoverSuffix()
{
    return ucs4({0x0020, 0x63A5, 0x7BA1});
}

// 甲 / 乙 / 丙 / 丁 / 戊
QString stemA()
{
    return ucs4({0x7532});
}
QString stemB()
{
    return ucs4({0x4E59});
}
QString stemC()
{
    return ucs4({0x4E19});
}
QString stemD()
{
    return ucs4({0x4E01});
}
QString stemE()
{
    return ucs4({0x620A});
}

/// Rust 的 `chars().count()`：代理对只算一个 char。
int charCount(const QString &text)
{
    int count = 0;
    for (int index = 0; index < text.size(); ++index) {
        if (!QChar::isLowSurrogate(text.at(index).unicode())) {
            count += 1;
        }
    }
    return count;
}

std::shared_ptr<Database> openTempDatabase(const QTemporaryDir &directory)
{
    const QString path = QDir(directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    if (opened.isErr()) {
        ADD_FAILURE() << opened.error().toStdString();
        return nullptr;
    }
    return opened.takeValue();
}

/// 本地墙上时钟今天过了多少秒 = chrono 的 `Local::now().num_seconds_from_midnight()`。
qint64 localSecondsFromMidnight(qint64 epochNanos)
{
    const QDateTime local
        = QDateTime::fromMSecsSinceEpoch(epochNanos / 1000000LL, QTimeZone::systemTimeZone());
    const QTime time = local.time();
    return qint64(time.hour()) * 3600 + qint64(time.minute()) * 60 + qint64(time.second());
}

/// 直接写一条指定时刻的记录：跨本地日和保留期边界这两种用例必须控制 `at`，
/// 不能全指望 `now_rfc3339()`。照抄 Rust 测试里的 `record_at`。
void recordAt(Database &database, const QString &at, const QString &from, const QString &to)
{
    static const char *kInsert =
        "INSERT INTO egress_switches "
        "(at, from_node, from_name, to_node, to_name, reason) "
        "VALUES (?1, ?2, ?2, ?3, ?3, ?4)";
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kInsert));
    ASSERT_TRUE(prepared.isOk()) << prepared.error().toStdString();
    Statement statement = prepared.takeValue();
    statement.bindText(1, at);
    statement.bindText(2, from);
    statement.bindText(3, to);
    statement.bindText(4, to + takeoverSuffix());
    const Result<bool> stepped = statement.step();
    ASSERT_TRUE(stepped.isOk()) << stepped.error().toStdString();
}

/// 往某源的分片里直插一行节点：导出走的是分库全量加载，这里绕开 parser 的
/// 指纹折叠路径，只造导出判定要看的列。
void insertShardNode(Database &database, const QString &sourceId, const QString &id,
                     const QString &name, const QString &server,
                     const std::optional<qint64> &latencyMs, const std::optional<bool> &latencyOk,
                     const QString &testKind, const std::optional<double> &downloadKbps)
{
    static const char *kInsert =
        "INSERT INTO nodes (id, source_id, protocol, name, server, port, country, latency_ms, "
        "latency_ok, download_kbps, latency_test_kind, created_at, updated_at) "
        "VALUES (?, ?, 'ss', ?, ?, 443, NULL, ?, ?, ?, ?, ?, ?)";
    Result<std::shared_ptr<ConnectionPool>> pool = database.openNodePool(sourceId);
    ASSERT_TRUE(pool.isOk()) << pool.error().toStdString();
    const QString stamp = TimeUtil::nowRfc3339();
    auto lease = pool.value()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kInsert));
    ASSERT_TRUE(prepared.isOk()) << prepared.error().toStdString();
    Statement statement = prepared.takeValue();
    statement.bindText(1, id);
    statement.bindText(2, sourceId);
    statement.bindText(3, name);
    statement.bindText(4, server);
    if (latencyMs.has_value()) {
        statement.bindInt64(5, *latencyMs);
    } else {
        statement.bindNull(5);
    }
    if (latencyOk.has_value()) {
        statement.bindInt64(6, *latencyOk ? 1 : 0);
    } else {
        statement.bindNull(6);
    }
    if (downloadKbps.has_value()) {
        statement.bindDouble(7, *downloadKbps);
    } else {
        statement.bindNull(7);
    }
    statement.bindText(8, testKind);
    statement.bindText(9, stamp);
    statement.bindText(10, stamp);
    const Result<bool> stepped = statement.step();
    ASSERT_TRUE(stepped.isOk()) << stepped.error().toStdString();
}

QString createSourceId(Database &database, const QString &name)
{
    Models::CreateSource input;
    input.name = name;
    input.urlTemplate = QStringLiteral("https://example.invalid/sub");
    input.enabled = true;
    input.remark = QString();
    Result<Models::Source> created = database.createSource(input);
    EXPECT_TRUE(created.isOk()) << created.error().toStdString();
    const Result<QVector<Models::Source>> sources = database.listSources();
    EXPECT_TRUE(sources.isOk()) << sources.error().toStdString();
    if (sources.isErr() || sources.value().isEmpty()) {
        return QString();
    }
    return sources.value().first().id;
}

}

/// 自动切换判定要切、但一个替补都没找到 —— 这种"没切成"以前只写在状态栏，
/// 滑过去就查无实据。现在进切换历史，并且连续同样的失败不许刷成一片。
TEST(DatabaseTelemetry, a_failover_without_a_target_is_recorded_once_not_spammed)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    auto miss = [](const QString &reason) {
        DbTelemetry::SwitchRecord record;
        record.fromNode = QStringLiteral("n-1");
        record.fromName = onlyNode();
        // 空串而不是 NULL：空 to_node 表示这压根没切。
        record.toNode = QStringLiteral("");
        record.toName = QString();
        record.reason = reason;
        record.latencyMs = std::nullopt;
        return record;
    };

    ASSERT_TRUE(DbTelemetry::recordEgressSwitch(*database, miss(missReasonProbe())).isOk());
    ASSERT_TRUE(DbTelemetry::recordEgressSwitch(*database, miss(missReasonProbe())).isOk());

    Result<QVector<DbTelemetry::SwitchRow>> rows
        = DbTelemetry::recentEgressSwitches(*database, 10);
    ASSERT_TRUE(rows.isOk()) << rows.error().toStdString();
    // 同样的复检失败不该每两分钟刷一条
    ASSERT_EQ(rows.value().size(), 1);
    const DbTelemetry::SwitchRow &row = rows.value().first();
    EXPECT_TRUE(!row.toNode.isNull() && row.toNode == QStringLiteral(""));

    ASSERT_TRUE(
        DbTelemetry::recordEgressSwitch(*database, miss(missReasonNoSecondNode())).isOk());
    Result<QVector<DbTelemetry::SwitchRow>> again = DbTelemetry::recentEgressSwitches(*database, 10);
    ASSERT_TRUE(again.isOk()) << again.error().toStdString();
    // 原因变了就该各留一条，否则看不出前后差别
    EXPECT_EQ(again.value().size(), 2);
}

TEST(DatabaseTelemetry, health_records_read_back_newest_first_and_count_the_streak)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    ASSERT_TRUE(DbTelemetry::recordEgressHealth(*database, true, qint64(900), QString(),
                                                QStringLiteral("n1"), nodeOne(),
                                                QStringLiteral("startup"))
                    .isOk());
    ASSERT_TRUE(DbTelemetry::recordEgressHealth(*database, false, std::nullopt, connectionTimeout(),
                                                QStringLiteral("n2"), nodeTwo(),
                                                QStringLiteral("periodic"))
                    .isOk());
    ASSERT_TRUE(DbTelemetry::recordEgressHealth(*database, false, qint64(8000),
                                                tlsHandshakeFailed(), QStringLiteral("n2"),
                                                QString(), QStringLiteral("periodic"))
                    .isOk());

    Result<QVector<DbTelemetry::HealthRow>> recent
        = DbTelemetry::recentEgressHealth(*database, 10);
    ASSERT_TRUE(recent.isOk()) << recent.error().toStdString();
    ASSERT_EQ(recent.value().size(), 3);
    EXPECT_EQ(recent.value().at(0).kind.toStdString(), std::string("periodic"));
    ASSERT_FALSE(recent.value().at(0).error.isNull());
    EXPECT_EQ(recent.value().at(0).error, tlsHandshakeFailed());
    ASSERT_FALSE(recent.value().at(2).nodeName.isNull());
    EXPECT_EQ(recent.value().at(2).nodeName, nodeOne());

    // 连续失败从最近一条往回数，遇到一次成功就停。
    Result<qint64> streak = DbTelemetry::egressFailStreak(*database);
    ASSERT_TRUE(streak.isOk()) << streak.error().toStdString();
    EXPECT_EQ(streak.value(), 2);
    ASSERT_TRUE(DbTelemetry::recordEgressHealth(*database, true, qint64(300), QString(), QString(),
                                                QString(), QStringLiteral("manual"))
                    .isOk());
    streak = DbTelemetry::egressFailStreak(*database);
    ASSERT_TRUE(streak.isOk()) << streak.error().toStdString();
    EXPECT_EQ(streak.value(), 0);

    // 时间窗过滤：一年前到现在，四条都在；一年后起算，一条都没有。
    const qint64 nowNanos = TimeUtil::nowUtcNanos();
    Result<QVector<DbTelemetry::HealthRow>> wide
        = DbTelemetry::egressHealthSince(*database, nowNanos - 365 * kSecondsPerDay * kNanosPerSec);
    ASSERT_TRUE(wide.isOk()) << wide.error().toStdString();
    EXPECT_EQ(wide.value().size(), 4);
    Result<QVector<DbTelemetry::HealthRow>> none
        = DbTelemetry::egressHealthSince(*database, nowNanos + kSecondsPerDay * kNanosPerSec);
    ASSERT_TRUE(none.isOk()) << none.error().toStdString();
    EXPECT_TRUE(none.value().isEmpty());
}

TEST(DatabaseTelemetry, long_error_text_is_clipped_so_the_header_can_show_it)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    const QString noisy = QString(400, QLatin1Char('x'));
    ASSERT_TRUE(DbTelemetry::recordEgressHealth(*database, false, std::nullopt, noisy, QString(),
                                                QString(), QStringLiteral("periodic"))
                    .isOk());
    Result<QVector<DbTelemetry::HealthRow>> recent = DbTelemetry::recentEgressHealth(*database, 1);
    ASSERT_TRUE(recent.isOk()) << recent.error().toStdString();
    ASSERT_EQ(recent.value().size(), 1);
    const QString error = recent.value().takeFirst().error;
    ASSERT_FALSE(error.isNull());
    // 160 字 + 省略号
    EXPECT_EQ(charCount(error), 161) << error.toStdString();
    EXPECT_EQ(error.at(error.size() - 1), QChar(0x2026));
}

TEST(DatabaseTelemetry, switches_are_recorded_but_identical_consecutive_ones_collapse)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    auto record = [](const QString &from, const QString &fromName, const QString &to,
                     const QString &toName, const QString &reason) {
        DbTelemetry::SwitchRecord entry;
        entry.fromNode = from;
        entry.fromName = fromName;
        entry.toNode = to;
        entry.toName = toName;
        entry.reason = reason;
        entry.latencyMs = std::nullopt;
        return entry;
    };

    ASSERT_TRUE(DbTelemetry::recordEgressSwitch(*database,
                                                record(QStringLiteral("a"), stemA(),
                                                       QStringLiteral("b"), stemB(), probeTimeout()))
                    .isOk());
    // 探测抖动：同一个目标、同一个原因，不该刷第二行。
    ASSERT_TRUE(DbTelemetry::recordEgressSwitch(*database,
                                                record(QStringLiteral("a"), stemA(),
                                                       QStringLiteral("b"), stemB(), probeTimeout()))
                    .isOk());
    ASSERT_TRUE(
        DbTelemetry::recordEgressSwitch(
            *database,
            record(QStringLiteral("b"), stemB(), QStringLiteral("c"), stemC(), manualSwitch()))
            .isOk());

    Result<QVector<DbTelemetry::SwitchRow>> rows
        = DbTelemetry::recentEgressSwitches(*database, 10);
    ASSERT_TRUE(rows.isOk()) << rows.error().toStdString();
    // 重复的那条应该被合掉
    ASSERT_EQ(rows.value().size(), 2);
    EXPECT_EQ(rows.value().at(0).toName, stemC());
    EXPECT_EQ(rows.value().at(1).reason, probeTimeout());

    Result<qint64> today = DbTelemetry::switchCountToday(*database);
    ASSERT_TRUE(today.isOk()) << today.error().toStdString();
    EXPECT_EQ(today.value(), 2);
}

/// 切换记录是真的留在库里、并按**本地**日算今天：`at` 存 UTC，东八区凌晨那
/// 八小时的记录以前会被 `substr(at,1,10)` 判成昨天，表头于是谎报"今日 0 次"。
TEST(DatabaseTelemetry, switch_history_spans_local_days_and_survives_pruning)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    const qint64 nowNanos = TimeUtil::nowUtcNanos();
    const qint64 elapsed = localSecondsFromMidnight(nowNanos);
    const qint64 midnightNanos = nowNanos - elapsed * kNanosPerSec;
    const QString justAfter = TimeUtil::formatRfc3339(midnightNanos + kNanosPerSec, false);
    const QString justBefore = TimeUtil::formatRfc3339(midnightNanos - kNanosPerSec, false);
    recordAt(*database, justAfter, stemA(), stemB());
    recordAt(*database, justBefore, stemB(), stemC());
    recordAt(*database, TimeUtil::formatRfc3339(midnightNanos - 3 * kSecondsPerDay * kNanosPerSec,
                                                false),
             stemC(), stemD());

    Result<qint64> today = DbTelemetry::switchCountToday(*database);
    ASSERT_TRUE(today.isOk()) << today.error().toStdString();
    // 只有本地零点之后的那一条算今天
    EXPECT_EQ(today.value(), 1);

    Result<qint64> total = DbTelemetry::switchHistoryCount(*database);
    ASSERT_TRUE(total.isOk()) << total.error().toStdString();
    // 表头那句「共 N 条」说的是保留期内的全部，不只今天
    EXPECT_EQ(total.value(), 3);

    // 跨过本地零点的两条必须分到两天
    EXPECT_NE(DbTelemetry::switchDay(justAfter), DbTelemetry::switchDay(justBefore));

    recordAt(*database,
             TimeUtil::formatRfc3339(nowNanos
                                         - (kRetentionSwitchesDays + 1) * kSecondsPerDay * kNanosPerSec,
                                     false),
             stemD(), stemE());
    ASSERT_TRUE(DbTelemetry::pruneTelemetry(*database).isOk());

    // 超出保留期的那条才该被裁掉
    Result<qint64> afterPrune = DbTelemetry::switchHistoryCount(*database);
    ASSERT_TRUE(afterPrune.isOk()) << afterPrune.error().toStdString();
    EXPECT_EQ(afterPrune.value(), 3);
}

TEST(DatabaseTelemetry, rule_hits_accumulate_per_day)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    const QString at = TimeUtil::nowRfc3339();
    auto hit = [&at](const QString &rule, const QString &target, qint64 count) {
        DbTelemetry::RuleHit entry;
        entry.rule = rule;
        entry.target = target;
        entry.count = count;
        entry.at = at;
        return entry;
    };

    ASSERT_TRUE(DbTelemetry::recordRuleHits(
                    *database, {hit(QStringLiteral("GEOIP,CN,DIRECT"), QStringLiteral("DIRECT"), 1),
                                hit(QStringLiteral("MATCH,PROXY"), QStringLiteral("PROXY"), 1)})
                    .isOk());
    Result<qint64> second
        = DbTelemetry::recordRuleHits(*database,
                                      {hit(QStringLiteral("GEOIP,CN,DIRECT"),
                                          QStringLiteral("DIRECT"), 3)});
    ASSERT_TRUE(second.isOk()) << second.error().toStdString();
    EXPECT_EQ(second.value(), 1);

    // 昨天的账不能混进今天。
    {
        auto lease = database->mainPool()->acquire();
        ASSERT_TRUE(
            lease->execute(QStringLiteral(
                "INSERT INTO rule_hits (day, rule, target, hits) VALUES ('2000-01-01', 'OLD,X', "
                "'X', 99)"))
                .isOk());
    }

    Result<QVector<DbTelemetry::RuleHitRow>> today = DbTelemetry::ruleHitsToday(*database);
    ASSERT_TRUE(today.isOk()) << today.error().toStdString();
    // 只该有今天的两条
    ASSERT_EQ(today.value().size(), 2);
    EXPECT_EQ(today.value().at(0).rule.toStdString(), std::string("GEOIP,CN,DIRECT"));
    // 一次 +1、再一次 +3：同一天同一条规则必须累加成 4，而不是被覆盖成 3。
    EXPECT_EQ(today.value().at(0).hits, 4);
    EXPECT_EQ(today.value().at(1).target.toStdString(), std::string("PROXY"));

    Result<qint64> emptyBatch = DbTelemetry::recordRuleHits(*database, {});
    ASSERT_TRUE(emptyBatch.isOk()) << emptyBatch.error().toStdString();
    EXPECT_EQ(emptyBatch.value(), 0);
}

TEST(DatabaseTelemetry, fetch_history_is_per_source_and_pruned_to_the_newest_slice)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    const QString sourceId = createSourceId(*database, QStringLiteral("s1"));
    ASSERT_FALSE(sourceId.isEmpty());

    for (qint64 index = 0; index < DbTelemetry::kFetchHistoryKeep + 5; ++index) {
        ASSERT_TRUE(DbTelemetry::recordFetchHistory(*database, sourceId, index % 2 == 0, index,
                                                     QStringLiteral("ok"))
                        .isOk());
    }
    ASSERT_TRUE(
        DbTelemetry::recordFetchHistory(*database, QStringLiteral("other"), false, 0,
                                        connectionTimeout())
            .isOk());
    ASSERT_TRUE(DbTelemetry::pruneTelemetry(*database).isOk());

    Result<QVector<DbTelemetry::FetchHistoryRow>> kept
        = DbTelemetry::fetchHistoryFor(*database, sourceId, 100);
    ASSERT_TRUE(kept.isOk()) << kept.error().toStdString();
    // 每个源只留最近 40 条
    ASSERT_EQ(kept.value().size(), DbTelemetry::kFetchHistoryKeep);
    // 新的在前
    EXPECT_EQ(kept.value().first().nodeCount, DbTelemetry::kFetchHistoryKeep + 4);

    // 别的源不受这个源的裁剪影响。
    Result<QVector<DbTelemetry::FetchHistoryRow>> other
        = DbTelemetry::fetchHistoryFor(*database, QStringLiteral("other"), 100);
    ASSERT_TRUE(other.isOk()) << other.error().toStdString();
    ASSERT_EQ(other.value().size(), 1);
    EXPECT_EQ(other.value().first().status, connectionTimeout());

    Result<QVector<QPair<QString, DbTelemetry::FetchHistoryRow>>> all
        = DbTelemetry::fetchHistoryAll(*database, 1000);
    ASSERT_TRUE(all.isOk()) << all.error().toStdString();
    ASSERT_EQ(all.value().size(), int(DbTelemetry::kFetchHistoryKeep) + 1);
    for (const auto &entry : all.value()) {
        EXPECT_TRUE(entry.first == sourceId || entry.first == QStringLiteral("other"));
    }
}

/// Rust 的 export.rs 没有自带测试，这里补一个覆盖导出三条路径的冒烟用例：
/// 排序键 (latency_ms 缺省排最后, 名称)、质量闸门与 limit、以及推送导出的两道排除。
TEST(DatabaseExport, exportFiltersSortsAndSkipsDisabledSources)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openTempDatabase(directory);
    ASSERT_NE(database, nullptr);

    const QString sourceId = createSourceId(*database, QStringLiteral("push"));
    ASSERT_FALSE(sourceId.isEmpty());
    const QString googleProxy = QStringLiteral("google_proxy");

    insertShardNode(*database, sourceId, QStringLiteral("n-fast"), QStringLiteral("BBB"),
                    QStringLiteral("1.2.3.4"), qint64(100), true, googleProxy, std::nullopt);
    insertShardNode(*database, sourceId, QStringLiteral("n-tie"), QStringLiteral("AAA"),
                    QStringLiteral("1.2.3.4"), qint64(100), true, googleProxy, std::nullopt);
    insertShardNode(*database, sourceId, QStringLiteral("n-slow"), QStringLiteral("SLOW"),
                    QStringLiteral("1.2.3.4"), qint64(5000), true, googleProxy, std::nullopt);
    insertShardNode(*database, sourceId, QStringLiteral("n-edge"), QStringLiteral("EDGE"),
                    QStringLiteral("1.2.3.4"), qint64(1), true, googleProxy, std::nullopt);
    insertShardNode(*database, sourceId, QStringLiteral("n-dead"), QStringLiteral("DEAD"),
                    QStringLiteral("1.2.3.4"), qint64(50), false, googleProxy, std::nullopt);
    insertShardNode(*database, sourceId, QStringLiteral("n-plain"), QStringLiteral("PLAIN"),
                    QStringLiteral("1.2.3.4"), qint64(60), true, QStringLiteral("tcp"),
                    std::nullopt);
    insertShardNode(*database, sourceId, QStringLiteral("n-none"), QStringLiteral("NONE"),
                    QStringLiteral("1.2.3.4"), std::nullopt, std::nullopt, QString(), std::nullopt);

    Result<QVector<Models::Node>> plain
        = DbExport::exportNodes(*database, std::nullopt, sourceId);
    ASSERT_TRUE(plain.isOk()) << plain.error().toStdString();
    ASSERT_EQ(plain.value().size(), 7);
    // 排序键 (latency_ms 缺省 = i64::MAX, 名称)：1 < 50 < 60 < 100(AAA) < 100(BBB) < 5000 < 未测。
    EXPECT_EQ(plain.value().at(0).id.toStdString(), std::string("n-edge"));
    EXPECT_EQ(plain.value().at(1).id.toStdString(), std::string("n-dead"));
    EXPECT_EQ(plain.value().at(2).id.toStdString(), std::string("n-plain"));
    EXPECT_EQ(plain.value().at(3).id.toStdString(), std::string("n-tie"));
    EXPECT_EQ(plain.value().at(4).id.toStdString(), std::string("n-fast"));
    EXPECT_EQ(plain.value().at(5).id.toStdString(), std::string("n-slow"));
    EXPECT_EQ(plain.value().last().id.toStdString(), std::string("n-none"));

    Result<QVector<Models::Node>> limited
        = DbExport::exportNodesFiltered(*database, std::nullopt, sourceId, false, std::nullopt,
                                        std::nullopt, quint32(1));
    ASSERT_TRUE(limited.isOk()) << limited.error().toStdString();
    ASSERT_EQ(limited.value().size(), 1);
    EXPECT_EQ(limited.value().first().id.toStdString(), std::string("n-edge"));

    // 质量闸门：>1ms、<=2000ms、latency_ok=1、google_proxy、公网非诱饵 host。
    Result<QVector<Models::Node>> quality
        = DbExport::exportNodesFiltered(*database, std::nullopt, sourceId, true, std::nullopt,
                                        std::nullopt, std::nullopt);
    ASSERT_TRUE(quality.isOk()) << quality.error().toStdString();
    ASSERT_EQ(quality.value().size(), 2);
    EXPECT_EQ(quality.value().at(0).id.toStdString(), std::string("n-tie"));
    EXPECT_EQ(quality.value().at(1).id.toStdString(), std::string("n-fast"));

    // 源标记不推送 + 名称命中排除关键词。
    {
        auto lease = database->mainPool()->acquire();
        ASSERT_TRUE(lease->execute(QStringLiteral("UPDATE sources SET push_disabled = 1")).isOk());
    }
    Result<QPair<QVector<Models::Node>, size_t>> blocked
        = DbExport::exportPushableNodes(*database, std::nullopt, QString());
    ASSERT_TRUE(blocked.isOk()) << blocked.error().toStdString();
    EXPECT_TRUE(blocked.value().first.isEmpty());
    EXPECT_EQ(blocked.value().second, size_t(2));

    {
        auto lease = database->mainPool()->acquire();
        ASSERT_TRUE(
            lease->execute(QStringLiteral("UPDATE sources SET push_disabled = 0")).isOk());
    }
    Result<QPair<QVector<Models::Node>, size_t>> byKeyword
        = DbExport::exportPushableNodes(*database, std::nullopt, QStringLiteral("aaa"));
    ASSERT_TRUE(byKeyword.isOk()) << byKeyword.error().toStdString();
    ASSERT_EQ(byKeyword.value().first.size(), 1);
    EXPECT_EQ(byKeyword.value().first.first().id.toStdString(), std::string("n-fast"));
    EXPECT_EQ(byKeyword.value().second, size_t(1));
}
