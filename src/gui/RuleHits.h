#pragma once

#include "db/DatabaseTelemetry.h"

#include <QHash>
#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

namespace Gui {

/// 今日规则命中：对应 Rust `gui/mod.rs` 的 rule_seen / rule_tally / flush_rule_hits
/// 与 `gui/views/proxy.rs:1084-1118` 那张卡片。这里只做纯计算，页面负责定时器与落库，
/// 这样离屏测试能直接喂快照进来，不用真起内核。

/// 一条连接对命中统计的全部贡献。
struct ConnectionHitInput {
    QString id;
    QString rule;
    QString target;
};

/// Rust 的 BgMsg::TrafficSnapshot 那一段：连接快照每秒重复推一遍，所以按
/// "第一次见到这个 id" 计一次，长连接不会被反复累加；去重表超过 2 万条整张丢掉
/// （计数已经并进 tally），防长跑内存。
void accumulateRuleHits(QHash<QString, QPair<QString, QString>> &seen,
                        QMap<QPair<QString, QString>, qint64> &tally,
                        const QVector<ConnectionHitInput> &connections);

/// Rust 的 flush_rule_hits：把攒下的增量倒成一批待写行并清空。空表返回空表。
QVector<DbTelemetry::RuleHit> drainRuleHits(QMap<QPair<QString, QString>, qint64> &tally,
                                            const QString &at);

/// 卡片的一行。
struct RuleHitRowView {
    QString rule;
    QString target;
    qint64 hits = 0;
};

/// 命中卡的可观察面。Rust 用 `if !rule_hits_today.is_empty()` 包住整张卡，
/// 所以没有命中时不是"显示 0"，是整张卡不存在。
struct RuleHitCard {
    bool visible = false;
    QString summary;
    QVector<RuleHitRowView> rows;
};

RuleHitCard buildRuleHitCard(const QVector<DbTelemetry::RuleHitRow> &hits);

}
