#include "gui/ProxyView.h"

#include <algorithm>
#include <limits>

#include "parser/Subscription.h"

namespace Gui {

const QVector<QString> kRegionCodes = {QStringLiteral("all"), QStringLiteral("HK"),
                                       QStringLiteral("JP"), QStringLiteral("US"),
                                       QStringLiteral("SG"), QStringLiteral("TW"),
                                       QStringLiteral("KR"), QStringLiteral("OTHER")};

namespace {

/// "其它"的补集就是这八个码 —— 注意它比界面上的药丸多 GB 与 DE：
/// 那两个地区有判定但没有独立药丸，归到"其它"里就等于把它们从补集里漏出去。
const QVector<QString> kKnownRegions = {QStringLiteral("HK"), QStringLiteral("JP"),
                                        QStringLiteral("US"), QStringLiteral("SG"),
                                        QStringLiteral("TW"), QStringLiteral("KR"),
                                        QStringLiteral("GB"), QStringLiteral("DE")};

QString upperCountry(const Models::NodeSummary &node)
{
    const QString guessed = node.country.isNull() ? Parser::guessCountry(node.name) : node.country;
    return guessed.toUpper();
}

bool latencyUntested(const Models::NodeSummary &node)
{
    return !(node.latencyOk.has_value() && *node.latencyOk);
}

qint64 latencyKey(const Models::NodeSummary &node)
{
    return node.latencyMs.has_value() ? *node.latencyMs : std::numeric_limits<qint64>::max();
}

}

ProxyView buildProxyView(const QVector<Models::NodeSummary> &all, const QString &region,
                         const QString &search, ProxySortBy sort, quint64 rev)
{
    ProxyView view;
    view.rev = rev;
    view.region = region;
    view.search = search;
    view.sort = sort;
    view.rows.reserve(all.size());

    for (const Models::NodeSummary &node : all) {
        const QString country = upperCountry(node);
        if (region != QLatin1String("all")) {
            const bool hit = region == QLatin1String("OTHER")
                ? !kKnownRegions.contains(country)
                : country == region;
            if (!hit) {
                continue;
            }
        }
        const QString lowerName = node.name.toLower();
        if (!search.isEmpty() && !lowerName.contains(search)
            && !node.server.toLower().contains(search) && !country.toLower().contains(search)) {
            continue;
        }
        ProxyViewRow row;
        row.node = node;
        row.country = country;
        row.lowerName = lowerName;
        view.rows.push_back(row);
    }

    std::stable_sort(view.rows.begin(), view.rows.end(),
                     [sort](const ProxyViewRow &a, const ProxyViewRow &b) {
                         if (sort == ProxySortBy::Name) {
                             return a.lowerName < b.lowerName;
                         }
                         // (未测通, 延迟, 名字)：测通的在前、快的在前，同分按名字。
                         if (latencyUntested(a.node) != latencyUntested(b.node)) {
                             return !latencyUntested(a.node);
                         }
                         if (latencyKey(a.node) != latencyKey(b.node)) {
                             return latencyKey(a.node) < latencyKey(b.node);
                         }
                         return a.lowerName < b.lowerName;
                     });
    return view;
}

}
