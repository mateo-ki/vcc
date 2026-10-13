// 今日规则命中卡（清单里的第 9 条）。纯计算部分直接喂快照/命中行，卡片部分走
// ProxyPage 的可观察面；最后一条真的落库再读回来，验 recordRuleHits/ruleHitsToday
// 与 Rust 的 (day, rule, target) 聚合语义一致。
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QTabBar>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "db/DatabaseTelemetry.h"
#include "gui/ProxyPage.h"
#include "gui/RuleHits.h"
#include "gui/Theme.h"
#include "gui/UiText.h"

using namespace Gui;

namespace {

std::shared_ptr<App::AppContext> makeContext(const QString &directory)
{
    App::AppContext::Options options;
    options.dbPath = QDir(directory).filePath(QStringLiteral("vpn-parser.db"));
    const Result<std::shared_ptr<App::AppContext>> created = App::AppContext::create(options);
    if (created.isErr()) {
        ADD_FAILURE() << created.error().toStdString();
        return nullptr;
    }
    return created.value();
}

ProxyPage *makePage(std::shared_ptr<App::AppContext> &context)
{
    auto *page = new ProxyPage(*context);
    page->resize(980, 720);
    page->selectPanel(ProxyPage::Panel::Rules);
    return page;
}

DbTelemetry::RuleHitRow hit(const QString &rule, const QString &target, qint64 hits)
{
    DbTelemetry::RuleHitRow row;
    row.rule = rule;
    row.target = target;
    row.hits = hits;
    return row;
}

ConnectionHitInput conn(const QString &id, const QString &rule, const QString &target)
{
    return ConnectionHitInput{id, rule, target};
}

}

TEST(RuleHitsTests, cardIsAbsentWhenNothingMatched)
{
    // Rust 是 `if !self.rule_hits_today.is_empty() { 卡片 }`：空不是"显示 0"，是没有这张卡。
    EXPECT_FALSE(buildRuleHitCard({}).visible);
    EXPECT_TRUE(buildRuleHitCard({}).summary.isEmpty());

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    page->setRuleHitsForTest({});
    EXPECT_FALSE(page->ruleHitsCardVisible());
    EXPECT_TRUE(page->ruleHitsSummaryText().isEmpty());

    page->setRuleHitsForTest({hit("MATCH,PROXY", "PROXY", 1)});
    EXPECT_TRUE(page->ruleHitsCardVisible());

    delete page;
}

TEST(RuleHitsTests, summaryAddsTotalsAndMatchShareExactlyLikeRust)
{
    const QVector<DbTelemetry::RuleHitRow> hits = {
        hit("MATCH,PROXY", "PROXY", 3),
        hit("GEOIP,CN,DIRECT", "DIRECT", 1),
    };
    const RuleHitCard card = buildRuleHitCard(hits);
    // total = 4，走兜底的 = 3，占比按 Rust 的整数除法 = 75。
    EXPECT_EQ(card.summary.toStdString(),
              UiText::hitsSummary().arg(4).arg(3).arg(75).toStdString());

    // 只有大写 MATCH 前缀算兜底（Rust 的 starts_with("MATCH") 是大小写敏感的）。
    const RuleHitCard lower = buildRuleHitCard({hit("match,PROXY", "PROXY", 5)});
    EXPECT_EQ(lower.summary.toStdString(),
              UiText::hitsSummary().arg(5).arg(0).arg(0).toStdString());

    // 全兜底是 100%，全不兜底是 0%。
    EXPECT_EQ(buildRuleHitCard({hit("MATCH,PROXY", "PROXY", 2)}).summary.toStdString(),
              UiText::hitsSummary().arg(2).arg(2).arg(100).toStdString());
}

TEST(RuleHitsTests, onlyTheFirstEightRowsAreShown)
{
    QVector<DbTelemetry::RuleHitRow> hits;
    for (int i = 0; i < 10; ++i) {
        hits.push_back(hit(QStringLiteral("R%1").arg(i), QStringLiteral("T%1").arg(i), 100 - i));
    }
    const RuleHitCard card = buildRuleHitCard(hits);
    ASSERT_EQ(card.rows.size(), 8) << "Rust 是 take(8)，不是全画";
    EXPECT_EQ(card.rows.at(0).rule.toStdString(), "R0");
    EXPECT_EQ(card.rows.at(7).rule.toStdString(), "R7");
    // 库里已经是 hits DESC，显示顺序照抄，不再自己排。
    EXPECT_EQ(card.rows.at(0).hits, 100);
    EXPECT_EQ(card.rows.at(7).hits, 93);
}

TEST(RuleHitsTests, rowsRenderRuleArrowTargetAndCount)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    page->setRuleHitsForTest({hit("GEOIP,CN,DIRECT,no-resolve", "DIRECT", 7)});
    ASSERT_TRUE(page->ruleHitsCardVisible());

    QStringList labels;
    for (auto *label : page->findChildren<QLabel *>()) {
        labels.push_back(label->text());
    }
    EXPECT_TRUE(labels.contains(QStringLiteral("GEOIP,CN,DIRECT,no-resolve")));
    EXPECT_TRUE(labels.contains(UiText::hitsArrow().arg(QStringLiteral("DIRECT"))));
    EXPECT_TRUE(labels.contains(UiText::hitsCount().arg(7)));

    delete page;
}

TEST(RuleHitsTests, tallyCountsEachConnectionIdOnlyOnce)
{
    QHash<QString, QPair<QString, QString>> seen;
    QMap<QPair<QString, QString>, qint64> tally;

    const QVector<ConnectionHitInput> batch = {
        conn("1", "MATCH,PROXY", "PROXY"),
        conn("2", "MATCH,PROXY", "PROXY"),
        conn("3", "GEOIP,CN,DIRECT", "DIRECT"),
    };
    accumulateRuleHits(seen, tally, batch);
    // 同一批再来一遍（快照每秒重复推）不能重复计数。
    accumulateRuleHits(seen, tally, batch);
    ASSERT_EQ(tally.size(), 2);
    EXPECT_EQ(tally[qMakePair(QStringLiteral("MATCH,PROXY"), QStringLiteral("PROXY"))], 2);
    EXPECT_EQ(tally[qMakePair(QStringLiteral("GEOIP,CN,DIRECT"), QStringLiteral("DIRECT"))], 1);

    accumulateRuleHits(seen, tally, {conn("4", "MATCH,PROXY", "PROXY")});
    EXPECT_EQ(tally[qMakePair(QStringLiteral("MATCH,PROXY"), QStringLiteral("PROXY"))], 3);

    const QString at = QStringLiteral("2026-10-10T00:00:00.000000000+00:00");
    const QVector<DbTelemetry::RuleHit> records = drainRuleHits(tally, at);
    ASSERT_EQ(records.size(), 2);
    EXPECT_TRUE(tally.isEmpty()) << "刷过之后增量表必须清空，否则会重复累加";
    // 增量表是有序容器，倒出来的顺序按 (rule,target) 键排，不按插入序 —— 断言只认键。
    qint64 matchProxy = -1;
    qint64 geoip = -1;
    for (const auto &record : records) {
        EXPECT_EQ(record.at.toStdString(), at.toStdString());
        if (record.rule == QLatin1String("MATCH,PROXY")) {
            EXPECT_EQ(record.target.toStdString(), "PROXY");
            matchProxy = record.count;
        } else if (record.rule == QLatin1String("GEOIP,CN,DIRECT")) {
            geoip = record.count;
        }
    }
    EXPECT_EQ(matchProxy, 3);
    EXPECT_EQ(geoip, 1);

    // 去重表长爆了要整张丢掉（计数已经在 tally 里），Rust 的 2 万条兜底。
    QHash<QString, QPair<QString, QString>> big;
    QMap<QPair<QString, QString>, qint64> bigTally;
    for (int i = 0; i < 20001; ++i) {
        accumulateRuleHits(big, bigTally, {conn(QString::number(i), "R", "T")});
    }
    EXPECT_TRUE(big.isEmpty());
    EXPECT_EQ(bigTally[qMakePair(QStringLiteral("R"), QStringLiteral("T"))], 20001);
}

TEST(RuleHitsTests, flushWritesTheDayTableAndReadsBack)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    // 三条连接、两个 (rule,target) 分组：落库后今日命中应该正好读出两行，多的在前。
    page->feedConnectionsForTest({
        conn("1", "MATCH,PROXY", "PROXY"),
        conn("2", "MATCH,PROXY", "PROXY"),
        conn("3", "GEOIP,CN,DIRECT", "DIRECT"),
    });
    page->flushRuleHitsForTest();

    QElapsedTimer clock;
    clock.start();
    while (!page->ruleHitsCardVisible() && clock.elapsed() < 10000) {
        QCoreApplication::processEvents();
    }
    ASSERT_TRUE(page->ruleHitsCardVisible()) << "刷进库再读回来之后卡片没出现";

    ASSERT_EQ(page->ruleHitsRows().size(), 2);
    EXPECT_EQ(page->ruleHitsRows().at(0).rule.toStdString(), "MATCH,PROXY");
    EXPECT_EQ(page->ruleHitsRows().at(0).hits, 2);
    EXPECT_EQ(page->ruleHitsRows().at(1).rule.toStdString(), "GEOIP,CN,DIRECT");
    EXPECT_EQ(page->ruleHitsRows().at(1).hits, 1);
    EXPECT_EQ(page->ruleHitsSummaryText().toStdString(),
              UiText::hitsSummary().arg(3).arg(2).arg(66).toStdString());

    // 同一批连接再喂一次不该再加（id 去重跨 flush 仍然有效）。
    page->feedConnectionsForTest({
        conn("1", "MATCH,PROXY", "PROXY"),
        conn("2", "MATCH,PROXY", "PROXY"),
        conn("3", "GEOIP,CN,DIRECT", "DIRECT"),
    });
    page->flushRuleHitsForTest();
    QCoreApplication::processEvents();

    const auto today = DbTelemetry::ruleHitsToday(context->database());
    ASSERT_TRUE(today.isOk());
    qint64 total = 0;
    for (const auto &row : today.value()) {
        total += row.hits;
    }
    EXPECT_EQ(total, 3) << "重复快照把命中算重了";

    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
