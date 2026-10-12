#include "gui/RuleHits.h"

#include "gui/UiText.h"

#include <algorithm>

namespace Gui {

void accumulateRuleHits(QHash<QString, QPair<QString, QString>> &seen,
                        QMap<QPair<QString, QString>, qint64> &tally,
                        const QVector<ConnectionHitInput> &connections)
{
    for (const auto &conn : connections) {
        const auto key = qMakePair(conn.rule, conn.target);
        if (!seen.contains(conn.id)) {
            tally[key] += 1;
            seen.insert(conn.id, key);
        }
    }
    if (seen.size() > 20000) {
        seen.clear();
    }
}

QVector<DbTelemetry::RuleHit> drainRuleHits(QMap<QPair<QString, QString>, qint64> &tally,
                                            const QString &at)
{
    QVector<DbTelemetry::RuleHit> records;
    for (auto it = tally.begin(); it != tally.end(); ++it) {
        DbTelemetry::RuleHit hit;
        hit.rule = it.key().first;
        hit.target = it.key().second;
        hit.count = it.value();
        hit.at = at;
        records.push_back(hit);
    }
    tally.clear();
    return records;
}

RuleHitCard buildRuleHitCard(const QVector<DbTelemetry::RuleHitRow> &hits)
{
    RuleHitCard card;
    if (hits.isEmpty()) {
        return card;
    }
    card.visible = true;

    qint64 total = 0;
    qint64 matched = 0;
    for (const auto &hit : hits) {
        total += hit.hits;
        if (hit.rule.startsWith(QStringLiteral("MATCH"))) {
            matched += hit.hits;
        }
    }
    const qint64 percent = total > 0 ? matched * 100 / total : 0;
    card.summary
        = UiText::hitsSummary().arg(total).arg(matched).arg(percent);

    for (const auto &hit : hits) {
        if (card.rows.size() >= 8) {
            break;
        }
        card.rows.push_back({hit.rule, hit.target, hit.hits});
    }
    return card;
}

}
