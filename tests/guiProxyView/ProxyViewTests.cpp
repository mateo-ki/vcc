#include <gtest/gtest.h>

#include "gui/ProxyView.h"
#include "parser/Subscription.h"

using namespace Gui;
using Models::NodeSummary;

namespace {

NodeSummary node(const QString &id, const QString &name, const QString &server,
                 const QString &country = QString(), bool latencyOk = false,
                 std::optional<qint64> latency = std::nullopt)
{
    NodeSummary summary;
    summary.id = id;
    summary.name = name;
    summary.server = server;
    summary.country = country;  // null = Rust 的 None，走 guessCountry
    summary.latencyOk = latencyOk ? std::optional<bool>(true) : std::optional<bool>();
    summary.latencyMs = latency;
    return summary;
}

QStringList ids(const ProxyView &view)
{
    QStringList out;
    for (const ProxyViewRow &row : view.rows) {
        out << row.node.id;
    }
    return out;
}

}

TEST(ProxyViewFilter, regionAllKeepsEverythingAndEchoesTheCacheKey)
{
    const QVector<NodeSummary> all = {
        node("a", "Hong Kong 1", "1.1.1.1", QStringLiteral("HK")),
        node("b", "Tokyo 2", "2.2.2.2", QStringLiteral("JP")),
    };
    const ProxyView view =
        buildProxyView(all, QStringLiteral("all"), QString(), ProxySortBy::Latency, 7);
    EXPECT_EQ(view.rows.size(), 2);
    EXPECT_EQ(view.rev, 7u);
    EXPECT_EQ(view.region.toStdString(), std::string("all"));
    EXPECT_TRUE(view.search.isEmpty());
}

TEST(ProxyViewFilter, singleRegionPillKeepsOnlyThatCountry)
{
    const QVector<NodeSummary> all = {
        node("a", "x", "1.1.1.1", QStringLiteral("HK")),
        node("b", "y", "2.2.2.2", QStringLiteral("JP")),
        node("c", "z", "3.3.3.3", QStringLiteral("hk")),
    };
    const ProxyView view = buildProxyView(all, QStringLiteral("HK"), QString(), ProxySortBy::Name, 0);
    // 国家码统一大写后再比，所以小写 hk 也算命中。
    EXPECT_EQ(ids(view), QStringList({QStringLiteral("a"), QStringLiteral("c")}));
}

TEST(ProxyViewFilter, otherIsTheComplementOfTheEightKnownCodes)
{
    const QVector<NodeSummary> all = {
        node("hk", "x", "1.1.1.1", QStringLiteral("HK")),
        node("gb", "x", "1.1.1.1", QStringLiteral("GB")),
        node("de", "x", "1.1.1.1", QStringLiteral("DE")),
        node("vn", "x", "1.1.1.1", QStringLiteral("VN")),
    };
    const ProxyView view =
        buildProxyView(all, QStringLiteral("OTHER"), QString(), ProxySortBy::Name, 0);
    // 药丸上没有 GB/DE，但它们仍在补集之外 —— 这正是 Rust 那张表比药丸多的两个码。
    EXPECT_EQ(ids(view), QStringList({QStringLiteral("vn")}));
}

TEST(ProxyViewFilter, searchHitsNameServerAndCountry)
{
    const QVector<NodeSummary> all = {
        node("by-name", "Osaka-Leaf", "10.0.0.1", QStringLiteral("JP")),
        node("by-server", "Else", "203.0.113.9", QStringLiteral("US")),
        node("by-country", "Else", "10.0.0.2", QStringLiteral("SG")),
        node("nope", "Else", "10.0.0.3", QStringLiteral("KR")),
    };
    EXPECT_EQ(ids(buildProxyView(all, QStringLiteral("all"), QStringLiteral("osaka"),
                                 ProxySortBy::Name, 0)),
              QStringList({QStringLiteral("by-name")}));
    EXPECT_EQ(ids(buildProxyView(all, QStringLiteral("all"), QStringLiteral("203.0.113"),
                                 ProxySortBy::Name, 0)),
              QStringList({QStringLiteral("by-server")}));
    EXPECT_EQ(ids(buildProxyView(all, QStringLiteral("all"), QStringLiteral("sg"),
                                 ProxySortBy::Name, 0)),
              QStringList({QStringLiteral("by-country")}));
}

TEST(ProxyViewFilter, emptySearchStringKeepsEverything)
{
    const QVector<NodeSummary> all = {node("a", "x", "1.1.1.1", QStringLiteral("HK"))};
    EXPECT_EQ(buildProxyView(all, QStringLiteral("all"), QString(), ProxySortBy::Name, 0)
                  .rows
                  .size(),
              1);
}

TEST(ProxyViewSort, latencyPutsTestedNodesFirstThenFastThenName)
{
    const QVector<NodeSummary> all = {
        node("slow", "A", "1.1.1.1", QString(), true, qint64(300)),
        node("fast", "B", "1.1.1.1", QString(), true, qint64(50)),
        node("untested", "C", "1.1.1.1"),
        node("failed", "D", "1.1.1.1", QString(), false, qint64(1)),
        node("tie2", "Z", "1.1.1.1", QString(), true, qint64(50)),
    };
    const ProxyView view =
        buildProxyView(all, QStringLiteral("all"), QString(), ProxySortBy::Latency, 0);
    // latency_ok 不是 true 的一律排后面，但尾部仍按 (latency_ms, 名字) 排：
    // "失败但历史上记到 1ms" 的节点因此排在 "从没测过"（Rust 用 i64::MAX 兜底）之前。
    // 这条是实跑翻出来的：我一开始把期望写成 untested 在前，实现是对的。
    EXPECT_EQ(ids(view),
              QStringList({QStringLiteral("fast"), QStringLiteral("tie2"), QStringLiteral("slow"),
                           QStringLiteral("failed"), QStringLiteral("untested")}));
}

TEST(ProxyViewSort, nameSortsOnLowercasedName)
{
    const QVector<NodeSummary> all = {
        node("c", "beta", "1.1.1.1"),
        node("a", "Alpha", "1.1.1.1"),
        node("b", "gamma", "1.1.1.1"),
    };
    const ProxyView view =
        buildProxyView(all, QStringLiteral("all"), QString(), ProxySortBy::Name, 0);
    EXPECT_EQ(ids(view),
              QStringList({QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
    // 行里预算好的小写名，网格直接拿来画，不再每次现算。
    EXPECT_EQ(view.rows.at(0).lowerName.toStdString(), std::string("alpha"));
}

TEST(ProxyViewFilter, countryFallsBackToGuessingAndIsUppercased)
{
    const QVector<NodeSummary> all = {node("a", "USA-LA-01", "1.1.1.1")};
    const ProxyView view =
        buildProxyView(all, QStringLiteral("all"), QString(), ProxySortBy::Name, 0);
    ASSERT_EQ(view.rows.size(), 1);
    // 库里 country 为空时用 parser 的 guessCountry(name)，并且大写（区域比较靠这个）。
    EXPECT_EQ(view.rows.at(0).country, Parser::guessCountry(QStringLiteral("USA-LA-01")).toUpper());
}
