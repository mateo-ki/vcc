#pragma once

#include <optional>

#include <QPair>
#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "core/Result.h"

class Database;

/// 遥测读模型：出口健康、切换历史、规则命中、订阅源拉取历史（对应 db/telemetry.rs）。
///
/// 这四张表**必须在主库**：`data/subscriptions/*.db` 那些分片每次拉订阅都整库重建再改名
/// （`replace_source_nodes`），历史放那儿等于每天清零。
///
/// 保留期见下面的常量，启动时和每次写入路径上按配额裁剪，不另开定时器。
namespace DbTelemetry {

/// 切换历史留 90 天（Rust 侧唯一 pub 的保留期常量，界面提示要用它）。
inline constexpr qint64 kRetentionSwitchesDays = 90;
/// 每个源只留最近 40 次拉取记录（Rust 侧私有，但裁剪和测试都要按它断言）。
inline constexpr qint64 kFetchHistoryKeep = 40;

/// 一条出口健康实测。`at` 直接是库里的 TEXT（UTC `+00:00`），不是 DateTime：
/// Rust 侧也是 String 原样读出，所以 JSON 里也原样输出，不转 `Z`。
struct HealthRow {
    QString at;
    qint64 ok = 0;
    std::optional<qint64> latencyMs;
    QString error;
    QString nodeId;
    QString nodeName;
    QString kind;

    Json toJson() const;
};

/// 一条出口变更历史。
struct SwitchRow {
    QString at;
    QString fromNode;
    QString fromName;
    QString toNode;
    QString toName;
    QString reason;
    std::optional<qint64> latencyMs;

    Json toJson() const;
};

/// 这一批里有多少条连接。连接按 id 去重后累加，所以这里一次刷 30 秒的增量。
struct RuleHit {
    QString rule;
    QString target;
    qint64 count = 0;
    QString at;
};

/// 按 (rule, target, 本地日) 聚合后的命中行。
struct RuleHitRow {
    QString rule;
    QString target;
    qint64 hits = 0;
    QString lastAt;

    Json toJson() const;
};

/// 一次订阅源拉取结果。
struct FetchHistoryRow {
    QString at;
    qint64 ok = 0;
    qint64 nodeCount = 0;
    QString status;

    Json toJson() const;
};

/// 一次出口变更。用结构体而不是八个位置参数：调用点全在 GUI 里，位置参数写错顺序
/// 编译器不会拦（对应 Rust 的 `SwitchRecord`，`Default` 派生 = 全空字段）。
struct SwitchRecord {
    QString fromNode;
    QString fromName;
    QString toNode;
    QString toName;
    QString reason;
    std::optional<qint64> latencyMs;
};

/// 一条记录落在哪个本地日历日。`at` 存的全是 UTC，直接取前 10 个字符会把
/// 北京时间早上 8 点之前的记录算到昨天头上。解析失败退回 `at` 的前 10 个 char。
QString switchDay(const QString &at);

Status recordEgressSwitch(Database &database, const SwitchRecord &record);
Status recordEgressHealth(Database &database, bool ok, const std::optional<qint64> &latencyMs,
                          const QString &error, const QString &nodeId, const QString &nodeName,
                          const QString &kind);
/// 最近 N 次实测，新→旧。
Result<QVector<HealthRow>> recentEgressHealth(Database &database, qint64 limit);
/// 画健康条用：`sinceNanos`（UTC 纳秒，对应 Rust 的 `DateTime<Utc>`）之后按时间正序的全部实测。
Result<QVector<HealthRow>> egressHealthSince(Database &database, qint64 sinceNanos);
/// 当前连续失败次数（最近一段连续的 `ok=0`）。表头那句"连续 N 次不通"就报这个数。
Result<qint64> egressFailStreak(Database &database);
Result<QVector<SwitchRow>> recentEgressSwitches(Database &database, qint64 limit);
/// 今天换了几次——按**本地**今天零点起算。
Result<qint64> switchCountToday(Database &database);
/// 库里还留着多少条切换记录（保留期之内，不只今天）。
Result<qint64> switchHistoryCount(Database &database);
/// 把这一秒采到的命中累加进按天聚合的表。一整批包在一个事务里。
Result<qint64> recordRuleHits(Database &database, const QVector<RuleHit> &hits);
/// 今天每条规则的命中数，按命中从多到少。
Result<QVector<RuleHitRow>> ruleHitsToday(Database &database);
Status recordFetchHistory(Database &database, const QString &sourceId, bool ok, qint64 nodeCount,
                          const QString &status);
/// 某个源最近 N 次拉取，新→旧。
Result<QVector<FetchHistoryRow>> fetchHistoryFor(Database &database, const QString &sourceId,
                                                 qint64 limit);
/// 一次性取所有源的最近 N 条拉取历史，调用方自己按 source_id 分桶。
Result<QVector<QPair<QString, FetchHistoryRow>>> fetchHistoryAll(Database &database, qint64 limit);
/// 裁剪到保留期内。启动时跑一次，另外每写 200 条健康记录顺手跑一次。
Status pruneTelemetry(Database &database);

}
