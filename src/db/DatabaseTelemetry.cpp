#include "db/DatabaseTelemetry.h"

#include <QDateTime>
#include <QString>
#include <QTimeZone>

#include <algorithm>
#include <optional>

#include "core/TimeUtil.h"
#include "db/ConnectionPool.h"
#include "db/Database.h"
#include "db/Sqlite.h"

namespace {

constexpr qint64 kNanosPerSec = 1000000000LL;
constexpr qint64 kSecondsPerDay = 86400LL;

/// 健康探测记录保留 7 天：健康条只画 24 小时，留一周够回看"昨晚是不是又挂了"。
constexpr qint64 kRetentionHealthDays = 7;
/// 规则命中按天累计，留 7 天。
constexpr qint64 kRetentionHitsDays = 7;
/// 省略号 U+2026。
constexpr char32_t kEllipsis = 0x2026;
/// clip 的上限：160 个 **char**（码点），不是字节。
constexpr int kClipMaxChars = 160;

/// Rust 的 `str::chars().count()`：代理对算一个 char，所以不能直接用 QString::size()。
int codePointCount(const QString &text)
{
    int count = 0;
    for (int index = 0; index < text.size(); ++index) {
        if (!QChar::isLowSurrogate(text.at(index).unicode())) {
            count += 1;
        }
    }
    return count;
}

/// Rust 的 `chars().take(max).collect()`：按码点截断，绝不把代理对切一半。
QString takeCodePoints(const QString &text, int max)
{
    QString out;
    int count = 0;
    for (int index = 0; index < text.size() && count < max;) {
        const QChar current = text.at(index);
        if (QChar::isHighSurrogate(current.unicode()) && index + 1 < text.size()
            && QChar::isLowSurrogate(text.at(index + 1).unicode())) {
            out.append(current);
            out.append(text.at(index + 1));
            index += 2;
        } else if (!QChar::isLowSurrogate(current.unicode())) {
            out.append(current);
            index += 1;
        } else {
            // 落单的低位代理：Rust 的 String 里不会出现，这里按一个 char 收掉。
            out.append(current);
            index += 1;
        }
        count += 1;
    }
    return out;
}

/// 把错误文本压到一行里放得下的长度：健康条上直接显示它。
QString clip(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (codePointCount(trimmed) <= kClipMaxChars) {
        return trimmed;
    }
    QString out = takeCodePoints(trimmed, kClipMaxChars);
    out.append(QString::fromUcs4(&kEllipsis, 1));
    return out;
}

Json optionalLatencyJson(const std::optional<qint64> &value)
{
    return value.has_value() ? Json(*value) : Json();
}

/// `SELECT at, ok, latency_ms, error, node_id, node_name, kind` 的行映射。
/// 可空文本列直接 `text()`：SQL NULL 在 QString 上就是 null 态，对应 Rust 的 None。
DbTelemetry::HealthRow readHealthRow(const Statement &row)
{
    DbTelemetry::HealthRow health;
    health.at = row.text("at");
    health.ok = row.int64("ok");
    health.latencyMs = row.optionalInt64("latency_ms");
    health.error = row.text("error");
    health.nodeId = row.text("node_id");
    health.nodeName = row.text("node_name");
    health.kind = row.text("kind");
    return health;
}

DbTelemetry::SwitchRow readSwitchRow(const Statement &row)
{
    DbTelemetry::SwitchRow sw;
    sw.at = row.text("at");
    sw.fromNode = row.text("from_node");
    sw.fromName = row.text("from_name");
    sw.toNode = row.text("to_node");
    sw.toName = row.text("to_name");
    sw.reason = row.text("reason");
    sw.latencyMs = row.optionalInt64("latency_ms");
    return sw;
}

DbTelemetry::RuleHitRow readRuleHitRow(const Statement &row)
{
    DbTelemetry::RuleHitRow hit;
    hit.rule = row.text("rule");
    hit.target = row.text("target");
    hit.hits = row.int64("hits");
    hit.lastAt = row.text("last_at");
    return hit;
}

DbTelemetry::FetchHistoryRow readFetchRow(const Statement &row)
{
    DbTelemetry::FetchHistoryRow history;
    history.at = row.text("at");
    history.ok = row.int64("ok");
    history.nodeCount = row.int64("node_count");
    history.status = row.text("status");
    return history;
}

/// 对应 `try_get::<String, _>(...).unwrap_or_default()`：NULL 读出来是空串而不是 None。
QString textDefault(const Statement &row, const char *column)
{
    const QString value = row.text(column);
    if (value.isNull()) {
        return QStringLiteral("");
    }
    return value;
}

qint64 intDefault(const Statement &row, const char *column)
{
    const int index = row.indexOf(column);
    if (row.isNull(index)) {
        return 0;
    }
    return row.int64(index);
}

/// 本地墙上时钟今天已经过了多少秒，对应 chrono 的 `Local::now().num_seconds_from_midnight()`
/// （截断到整秒，不含小数）。
qint64 localSecondsFromMidnight(qint64 epochNanos)
{
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(epochNanos / 1000000LL,
                                                           QTimeZone::systemTimeZone());
    const QTime time = local.time();
    return qint64(time.hour()) * 3600 + qint64(time.minute()) * 60 + qint64(time.second());
}

/// 绑一个文本参数后执行，对应 Rust 的 `.bind(x).execute(&self.pool).await?`。
Status runBound(ConnectionPool::Lease &lease, const char *sql, const QString &value)
{
    Result<Statement> prepared = lease->prepare(QLatin1String(sql));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, value);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }
    return Status::succeeded();
}

Status runBound(ConnectionPool::Lease &lease, const char *sql, qint64 value)
{
    Result<Statement> prepared = lease->prepare(QLatin1String(sql));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindInt64(1, value);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }
    return Status::succeeded();
}

}

namespace DbTelemetry {

Json HealthRow::toJson() const
{
    Json value;
    value["at"] = at.isNull() ? Json() : Json(at.toStdString());
    value["ok"] = ok;
    value["latency_ms"] = optionalLatencyJson(latencyMs);
    value["error"] = JsonUtil::optText(error);
    value["node_id"] = JsonUtil::optText(nodeId);
    value["node_name"] = JsonUtil::optText(nodeName);
    value["kind"] = kind.isNull() ? Json() : Json(kind.toStdString());
    return value;
}

Json SwitchRow::toJson() const
{
    Json value;
    value["at"] = at.isNull() ? Json() : Json(at.toStdString());
    value["from_node"] = JsonUtil::optText(fromNode);
    value["from_name"] = JsonUtil::optText(fromName);
    value["to_node"] = JsonUtil::optText(toNode);
    value["to_name"] = JsonUtil::optText(toName);
    value["reason"] = reason.isNull() ? Json() : Json(reason.toStdString());
    value["latency_ms"] = optionalLatencyJson(latencyMs);
    return value;
}

Json RuleHitRow::toJson() const
{
    Json value;
    value["rule"] = rule.isNull() ? Json() : Json(rule.toStdString());
    value["target"] = target.isNull() ? Json() : Json(target.toStdString());
    value["hits"] = hits;
    value["last_at"] = JsonUtil::optText(lastAt);
    return value;
}

Json FetchHistoryRow::toJson() const
{
    Json value;
    value["at"] = at.isNull() ? Json() : Json(at.toStdString());
    value["ok"] = ok;
    value["node_count"] = nodeCount;
    value["status"] = status.isNull() ? Json() : Json(status.toStdString());
    return value;
}

QString switchDay(const QString &at)
{
    const Result<qint64> parsed = TimeUtil::parseRfc3339(at);
    if (parsed.isOk()) {
        return TimeUtil::localDay(parsed.value());
    }
    // 解析不出来就退回 UTC 前缀：Rust 的 `at.chars().take(10)`，按 char 不是字节。
    return takeCodePoints(at, 10);
}

Status recordEgressSwitch(Database &database, const SwitchRecord &record)
{
    // 和上一条完全一样的变更（同一个 to、同一个原因）不重复记，
    // 否则探测抖动会把历史刷成一片复制粘贴。
    static const char *kPreviousSignature =
        "SELECT to_node || '|' || reason FROM egress_switches ORDER BY id DESC LIMIT 1";
    static const char *kInsertSwitch =
        "INSERT INTO egress_switches "
        "(at, from_node, from_name, to_node, to_name, reason, latency_ms) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";

    auto lease = database.mainPool()->acquire();
    Result<Statement> previous = lease->prepare(QLatin1String(kPreviousSignature));
    if (previous.isErr()) {
        return Status::failed(previous.error());
    }
    QString stored;
    {
        Statement statement = previous.takeValue();
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Status::failed(stepped.error());
        }
        if (stepped.value()) {
            stored = statement.text(0);
        }
    }
    const QString signature
        = (record.toNode.isNull() ? QStringLiteral("") : record.toNode) + QLatin1Char('|')
        + clip(record.reason);
    // to_node 为 NULL 时 SQL 的 `||` 整式结果是 NULL，读回来是 null QString：
    // 和签名永远不相等，于是照抄 Rust 的行为——这种记录不去重。
    if (!stored.isNull() && stored == signature) {
        return Status::succeeded();
    }

    Result<Statement> inserted = lease->prepare(QLatin1String(kInsertSwitch));
    if (inserted.isErr()) {
        return Status::failed(inserted.error());
    }
    Statement statement = inserted.takeValue();
    statement.bindText(1, TimeUtil::nowRfc3339());
    statement.bindText(2, record.fromNode);
    statement.bindText(3, record.fromName);
    statement.bindText(4, record.toNode);
    statement.bindText(5, record.toName);
    statement.bindText(6, clip(record.reason));
    if (record.latencyMs.has_value()) {
        statement.bindInt64(7, *record.latencyMs);
    } else {
        statement.bindNull(7);
    }
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }
    return Status::succeeded();
}

Status recordEgressHealth(Database &database, bool ok, const std::optional<qint64> &latencyMs,
                          const QString &error, const QString &nodeId, const QString &nodeName,
                          const QString &kind)
{
    static const char *kInsertHealth =
        "INSERT INTO egress_health (at, ok, latency_ms, error, node_id, node_name, kind) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";

    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kInsertHealth));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, TimeUtil::nowRfc3339());
    statement.bindInt64(2, ok ? 1 : 0);
    if (latencyMs.has_value()) {
        statement.bindInt64(3, *latencyMs);
    } else {
        statement.bindNull(3);
    }
    // error 是 Option：null QString 要写 SQL NULL，非空才 clip（空串 clip 完还是空串）。
    statement.bindText(4, error.isNull() ? QString() : clip(error));
    statement.bindText(5, nodeId);
    statement.bindText(6, nodeName);
    statement.bindText(7, kind);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }
    return Status::succeeded();
}

Result<QVector<HealthRow>> recentEgressHealth(Database &database, qint64 limit)
{
    static const char *kSelect =
        "SELECT at, ok, latency_ms, error, node_id, node_name, kind FROM egress_health "
        "ORDER BY id DESC LIMIT ?1";

    QVector<HealthRow> rows;
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kSelect));
    if (prepared.isErr()) {
        return Result<QVector<HealthRow>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindInt64(1, limit);
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QVector<HealthRow>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        rows.append(readHealthRow(statement));
    }
    return Result<QVector<HealthRow>>::withValue(rows);
}

Result<QVector<HealthRow>> egressHealthSince(Database &database, qint64 sinceNanos)
{
    static const char *kSelect =
        "SELECT at, ok, latency_ms, error, node_id, node_name, kind FROM egress_health "
        "WHERE at >= ?1 ORDER BY id ASC";

    QVector<HealthRow> rows;
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kSelect));
    if (prepared.isErr()) {
        return Result<QVector<HealthRow>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    // 绑定的是 DateTime<Utc>::to_rfc3339()：和 `at` 列同格式的字符串比较。
    statement.bindText(1, TimeUtil::formatRfc3339(sinceNanos, false));
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QVector<HealthRow>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        rows.append(readHealthRow(statement));
    }
    return Result<QVector<HealthRow>>::withValue(rows);
}

Result<qint64> egressFailStreak(Database &database)
{
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QStringLiteral(
        "SELECT ok FROM egress_health ORDER BY id DESC LIMIT 200"));
    if (prepared.isErr()) {
        return Result<qint64>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    qint64 streak = 0;
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<qint64>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        // take_while(ok == 0)：遇到一次成功就停。
        if (statement.int64(0) != 0) {
            break;
        }
        streak += 1;
    }
    return Result<qint64>::withValue(streak);
}

Result<QVector<SwitchRow>> recentEgressSwitches(Database &database, qint64 limit)
{
    static const char *kSelect =
        "SELECT at, from_node, from_name, to_node, to_name, reason, latency_ms "
        "FROM egress_switches ORDER BY id DESC LIMIT ?1";

    QVector<SwitchRow> rows;
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kSelect));
    if (prepared.isErr()) {
        return Result<QVector<SwitchRow>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindInt64(1, limit);
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QVector<SwitchRow>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        rows.append(readSwitchRow(statement));
    }
    return Result<QVector<SwitchRow>>::withValue(rows);
}

Result<qint64> switchCountToday(Database &database)
{
    // 本地今天零点 = 现在减去"今天已经过了多少秒"。这样算不用构造时区化的
    // NaiveDateTime，也就没有 DST 歧义（零点切换的那几天构造不出合法时刻）。
    // 注意：DST 切换日这个减法算出来的不是精确的本地零点，Rust 也是如此，照抄。
    const qint64 nowNanos = TimeUtil::nowUtcNanos();
    const qint64 elapsed = localSecondsFromMidnight(nowNanos);
    const QString startUtc
        = TimeUtil::formatRfc3339(nowNanos - elapsed * kNanosPerSec, false);

    static const char *kCount = "SELECT COUNT(*) FROM egress_switches WHERE at >= ?1";
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kCount));
    if (prepared.isErr()) {
        return Result<qint64>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, startUtc);
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<qint64>::withError(stepped.error());
    }
    if (!stepped.value()) {
        return Result<qint64>::withError(QStringLiteral("egress_switches 计数查询没有返回行"));
    }
    return Result<qint64>::withValue(statement.int64(0));
}

Result<qint64> switchHistoryCount(Database &database)
{
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared
        = lease->prepare(QStringLiteral("SELECT COUNT(*) FROM egress_switches"));
    if (prepared.isErr()) {
        return Result<qint64>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Result<qint64>::withError(stepped.error());
    }
    if (!stepped.value()) {
        return Result<qint64>::withError(QStringLiteral("egress_switches 计数查询没有返回行"));
    }
    return Result<qint64>::withValue(statement.int64(0));
}

Result<qint64> recordRuleHits(Database &database, const QVector<RuleHit> &hits)
{
    static const char *kUpsert =
        "INSERT INTO rule_hits (day, rule, target, hits, first_at, last_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?5) "
        "ON CONFLICT(day, rule, target) DO UPDATE SET "
        "hits = hits + excluded.hits, last_at = excluded.last_at";

    if (hits.isEmpty()) {
        return Result<qint64>::withValue(0);
    }
    const QString day = TimeUtil::localDay(TimeUtil::nowUtcNanos());
    // 一整批包在一个事务里：每条 UPSERT 各占一个隐式事务的话，一次刷新几十条就是几十次
    // WAL 提交。
    Transaction transaction(database.mainPool()->acquire());
    Connection *connection = transaction.connection();
    if (!connection) {
        return Result<qint64>::withError(QStringLiteral("事务不可用"));
    }
    qint64 written = 0;
    for (const RuleHit &hit : hits) {
        Result<Statement> prepared = connection->prepare(QLatin1String(kUpsert));
        if (prepared.isErr()) {
            return Result<qint64>::withError(prepared.error());
        }
        Statement statement = prepared.takeValue();
        statement.bindText(1, day);
        statement.bindText(2, clip(hit.rule));
        statement.bindText(3, clip(hit.target));
        statement.bindInt64(4, std::max<qint64>(hit.count, 1));
        statement.bindText(5, hit.at);
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<qint64>::withError(stepped.error());
        }
        written += 1;
    }
    const Status committed = transaction.commit();
    if (committed.isErr()) {
        return Result<qint64>::withError(committed.error());
    }
    return Result<qint64>::withValue(written);
}

Result<QVector<RuleHitRow>> ruleHitsToday(Database &database)
{
    static const char *kSelect =
        "SELECT rule, target, hits, last_at FROM rule_hits WHERE day = ?1 "
        "ORDER BY hits DESC, rule ASC";

    const QString day = TimeUtil::localDay(TimeUtil::nowUtcNanos());
    QVector<RuleHitRow> rows;
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kSelect));
    if (prepared.isErr()) {
        return Result<QVector<RuleHitRow>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, day);
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QVector<RuleHitRow>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        rows.append(readRuleHitRow(statement));
    }
    return Result<QVector<RuleHitRow>>::withValue(rows);
}

Status recordFetchHistory(Database &database, const QString &sourceId, bool ok, qint64 nodeCount,
                          const QString &status)
{
    static const char *kInsert =
        "INSERT INTO source_fetch_history (source_id, at, ok, node_count, status) "
        "VALUES (?1, ?2, ?3, ?4, ?5)";

    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kInsert));
    if (prepared.isErr()) {
        return Status::failed(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, sourceId);
    statement.bindText(2, TimeUtil::nowRfc3339());
    statement.bindInt64(3, ok ? 1 : 0);
    statement.bindInt64(4, nodeCount);
    statement.bindText(5, clip(status));
    const Result<bool> stepped = statement.step();
    if (stepped.isErr()) {
        return Status::failed(stepped.error());
    }
    return Status::succeeded();
}

Result<QVector<FetchHistoryRow>> fetchHistoryFor(Database &database, const QString &sourceId,
                                                 qint64 limit)
{
    static const char *kSelect =
        "SELECT at, ok, node_count, status FROM source_fetch_history "
        "WHERE source_id = ?1 ORDER BY id DESC LIMIT ?2";

    QVector<FetchHistoryRow> rows;
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kSelect));
    if (prepared.isErr()) {
        return Result<QVector<FetchHistoryRow>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindText(1, sourceId);
    statement.bindInt64(2, limit);
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QVector<FetchHistoryRow>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        rows.append(readFetchRow(statement));
    }
    return Result<QVector<FetchHistoryRow>>::withValue(rows);
}

Result<QVector<QPair<QString, FetchHistoryRow>>> fetchHistoryAll(Database &database, qint64 limit)
{
    static const char *kSelect =
        "SELECT source_id, at, ok, node_count, status FROM source_fetch_history "
        "ORDER BY id DESC LIMIT ?1";

    QVector<QPair<QString, FetchHistoryRow>> rows;
    auto lease = database.mainPool()->acquire();
    Result<Statement> prepared = lease->prepare(QLatin1String(kSelect));
    if (prepared.isErr()) {
        return Result<QVector<QPair<QString, FetchHistoryRow>>>::withError(prepared.error());
    }
    Statement statement = prepared.takeValue();
    statement.bindInt64(1, limit);
    for (;;) {
        const Result<bool> stepped = statement.step();
        if (stepped.isErr()) {
            return Result<QVector<QPair<QString, FetchHistoryRow>>>::withError(stepped.error());
        }
        if (!stepped.value()) {
            break;
        }
        // try_get().unwrap_or_default()/unwrap_or(0)：NULL 列读成空串和 0，不报错。
        FetchHistoryRow history;
        history.at = textDefault(statement, "at");
        history.ok = intDefault(statement, "ok");
        history.nodeCount = intDefault(statement, "node_count");
        history.status = textDefault(statement, "status");
        rows.append({textDefault(statement, "source_id"), history});
    }
    return Result<QVector<QPair<QString, FetchHistoryRow>>>::withValue(rows);
}

Status pruneTelemetry(Database &database)
{
    const qint64 nowNanos = TimeUtil::nowUtcNanos();
    const QString healthCut = TimeUtil::formatRfc3339(
        nowNanos - kRetentionHealthDays * kSecondsPerDay * kNanosPerSec, false);
    const QString switchCut = TimeUtil::formatRfc3339(
        nowNanos - kRetentionSwitchesDays * kSecondsPerDay * kNanosPerSec, false);
    // 命中按本地日聚合，裁剪界也是**本地**日：先减 7 天再换到本地时区取日历日。
    const QString hitsCut = TimeUtil::localDay(nowNanos
                                               - kRetentionHitsDays * kSecondsPerDay * kNanosPerSec);

    static const char *kDeleteHealth = "DELETE FROM egress_health WHERE at < ?1";
    static const char *kDeleteSwitches = "DELETE FROM egress_switches WHERE at < ?1";
    static const char *kDeleteHits = "DELETE FROM rule_hits WHERE day < ?1";
    static const char *kDeleteFetchHistory =
        "DELETE FROM source_fetch_history WHERE id NOT IN ( "
        "SELECT id FROM ( "
        "SELECT id, ROW_NUMBER() OVER (PARTITION BY source_id ORDER BY id DESC) AS rn "
        "FROM source_fetch_history "
        ") WHERE rn <= ?1)";

    auto lease = database.mainPool()->acquire();

    Status status = runBound(lease, kDeleteHealth, healthCut);
    if (status.isErr()) {
        return status;
    }
    status = runBound(lease, kDeleteSwitches, switchCut);
    if (status.isErr()) {
        return status;
    }
    status = runBound(lease, kDeleteHits, hitsCut);
    if (status.isErr()) {
        return status;
    }
    return runBound(lease, kDeleteFetchHistory, kFetchHistoryKeep);
}

}
