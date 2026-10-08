#include <gtest/gtest.h>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <string>

#include "router/Router.h"
#include "router/RouterMessages.h"
#include "RouterTestCjk.h"

// 对应 vpn-parser/src/router.rs 的两个测试模块：`mod tests`（12 个 #[test]）和
// `mod backup_tests`（3 个 #[test]）。这里逐条移植，测试名保持 Rust 原名（CamelCase），
// 判定顺序、阈值和期望的决策分支都和 Rust 一样。
//
// Rust 用 `Instant::now()` 当基准再 `+ Duration`，本质只关心差值；这里把基准固定成 0，
// 于是假时钟能像 Rust 测试那样精确注入，也不依赖真实时间。
namespace {

constexpr Router::Instant kT0 = 0;

/// Rust 测试里的 `fn sample(ok, ms)`：只带延迟，不带带宽。
Router::Sample sample(bool ok, quint64 ms)
{
    Router::Sample value;
    value.ok = ok;
    value.latencyMs = ms;
    value.downloadKbps = std::nullopt;
    return value;
}

/// Rust 测试里的 `fn router()`：阈值 600ms、切换最小间隔 0，其余走 Default。
Router::NodeRouter router()
{
    Router::RouterConfig cfg = Router::RouterConfig::defaults();
    cfg.latencyThresholdMs = 600;
    cfg.minSwitchInterval = Router::durationFromSecs(0);
    return Router::NodeRouter(cfg);
}

/// 对应 Rust 的 `r.health(id).unwrap()`：拿不到就当场记一条失败并返回默认值，
/// 免得解引用空 optional 直接把测试进程打挂。
Router::NodeHealth healthOf(const Router::NodeRouter &router, const QString &nodeId)
{
    const std::optional<Router::NodeHealth> found = router.health(nodeId);
    EXPECT_TRUE(found.has_value()) << "health(" << nodeId.toStdString() << ") is None";
    return found.value_or(Router::NodeHealth());
}

QString decisionKindName(Router::DecisionKind kind)
{
    switch (kind) {
    case Router::DecisionKind::Stay:
        return QStringLiteral("Stay");
    case Router::DecisionKind::SwitchTo:
        return QStringLiteral("SwitchTo");
    case Router::DecisionKind::BreakerOpened:
        return QStringLiteral("BreakerOpened");
    }
    return QStringLiteral("?");
}

/// 对应 Rust 的 `{decision:?}` / `{other:?}`：断言失败时把整条决策打出来。
/// 返回 std::string 是因为 gtest 的流式消息不认识 QString。
std::string decisionText(const Router::Decision &decision)
{
    const QString text = QStringLiteral("%1 node=%2 reason=%3")
                             .arg(decisionKindName(decision.kind), decision.nodeId, decision.reason);
    return text.toStdString();
}

} // namespace

// ---- mod tests ----

// #[test] fn degraded_streak_is_required_before_switching
TEST(Router, DegradedStreakIsRequiredBeforeSwitching)
{
    Router::NodeRouter r = router();
    for (int index = 0; index < 4; ++index) {
        r.observe(QStringLiteral("current"), sample(true, 300), kT0);
        r.observe(QStringLiteral("better"), sample(true, 200), kT0);
    }
    // 第一次尖峰：EWMA 越线但只算 1 次劣化，不该切。
    EXPECT_TRUE(r.observe(QStringLiteral("current"), sample(true, 6000), kT0).isStay());
    // 中间恢复正常两次，计数被清零，再来一次尖峰仍然不该切。
    r.observe(QStringLiteral("current"), sample(true, 300), kT0);
    r.observe(QStringLiteral("current"), sample(true, 300), kT0);
    EXPECT_TRUE(r.observe(QStringLiteral("current"), sample(true, 6000), kT0).isStay());
}

// #[test] fn one_spike_never_switches_even_when_a_better_candidate_exists
TEST(Router, OneSpikeNeverSwitchesEvenWhenABetterCandidateExists)
{
    Router::NodeRouter r = router();
    for (int index = 0; index < 4; ++index) {
        r.observe(QStringLiteral("current"), sample(true, 300), kT0);
        r.observe(QStringLiteral("better"), sample(true, 200), kT0);
    }
    // 一次 6s 尖峰：better 分数本来就比 current 好，但 EWMA 还没越过阈值，不该切。
    EXPECT_TRUE(r.observe(QStringLiteral("current"), sample(true, 6000), kT0).isStay());
    // 持续劣化（每次都慢）才会真的切。
    for (int index = 0; index < 4; ++index) {
        const Router::Decision decision = r.observe(QStringLiteral("current"), sample(true, 2000), kT0);
        if (decision.isSwitchTo()) {
            EXPECT_EQ(decision.nodeId, QStringLiteral("better"));
            return;
        }
    }
    FAIL() << "no switch even after sustained degradation";
}

// #[test] fn one_spike_raises_the_score_but_recovers_without_a_target
TEST(Router, OneSpikeRaisesTheScoreButRecoversWithoutATarget)
{
    Router::NodeRouter r = router();
    for (int index = 0; index < 5; ++index) {
        EXPECT_TRUE(r.observe(QStringLiteral("a"), sample(true, 200), kT0).isStay());
    }
    // EWMA 不是"完全免疫尖峰"：一次 5s 会把分数抬到阈值以上。但只有存在明显
    // 更好的候选时才会真的切，否则就是 Stay。
    EXPECT_TRUE(r.observe(QStringLiteral("a"), sample(true, 5000), kT0).isStay());
    EXPECT_GT(healthOf(r, QStringLiteral("a")).ewmaMs, 600.0);
    // 之后连续正常样本要能自己回到阈值以下（这就是 EWMA 的记忆长度）。
    for (int index = 0; index < 6; ++index) {
        EXPECT_TRUE(r.observe(QStringLiteral("a"), sample(true, 200), kT0).isStay());
    }
    const Router::NodeHealth tail = healthOf(r, QStringLiteral("a"));
    EXPECT_LT(tail.ewmaMs, 600.0) << "ewma=" << tail.ewmaMs;
}

// #[test] fn two_failures_open_the_breaker_and_blacklist_the_node
TEST(Router, TwoFailuresOpenTheBreakerAndBlacklistTheNode)
{
    Router::NodeRouter r = router();
    r.observe(QStringLiteral("bad"), sample(false, 8000), kT0);
    const Router::Decision decision = r.observe(QStringLiteral("bad"), sample(false, 8000), kT0);
    EXPECT_TRUE(decision.isBreakerOpened()) << decisionText(decision);
    EXPECT_TRUE(healthOf(r, QStringLiteral("bad")).breakerOpen(kT0));
    // 断路节点不会成为候选。
    EXPECT_FALSE(r.bestCandidate(QStringLiteral("other"), kT0).has_value());
}

// #[test] fn hysteresis_blocks_switching_to_a_similarly_good_node
TEST(Router, HysteresisBlocksSwitchingToASimilarlyGoodNode)
{
    Router::NodeRouter r = router();
    for (int index = 0; index < 4; ++index) {
        r.observe(QStringLiteral("current"), sample(true, 900), kT0);
        r.observe(QStringLiteral("similar"), sample(true, 850), kT0);
    }
    // current 已经劣化（900 > 600），但 similar 只好一点，不该切。
    const Router::Decision decision = r.observe(QStringLiteral("current"), sample(true, 900), kT0);
    EXPECT_TRUE(decision.isStay()) << decisionText(decision);
}

// #[test] fn switches_when_a_candidate_is_much_better
TEST(Router, SwitchesWhenACandidateIsMuchBetter)
{
    Router::NodeRouter r = router();
    for (int index = 0; index < 4; ++index) {
        r.observe(QStringLiteral("current"), sample(true, 1200), kT0);
        r.observe(QStringLiteral("great"), sample(true, 120), kT0);
    }
    const Router::Decision decision = r.observe(QStringLiteral("current"), sample(true, 1200), kT0);
    ASSERT_TRUE(decision.isSwitchTo()) << "expected switch, got " << decisionText(decision);
    EXPECT_EQ(decision.nodeId, QStringLiteral("great"));
    EXPECT_TRUE(decision.reason.contains(Router::Messages::latencyHead()))
        << decision.reason.toStdString();
}

// #[test] fn min_switch_interval_suppresses_flapping
TEST(Router, MinSwitchIntervalSuppressesFlapping)
{
    Router::RouterConfig cfg = Router::RouterConfig::defaults();
    cfg.latencyThresholdMs = 600;
    cfg.minSwitchInterval = Router::durationFromSecs(120);
    Router::NodeRouter r(cfg);
    r.observe(QStringLiteral("great"), sample(true, 100), kT0);
    // 第 1 次劣化只计数，第 2 次才真的切。
    EXPECT_TRUE(r.observe(QStringLiteral("current"), sample(true, 1200), kT0).isStay());
    EXPECT_TRUE(r.observe(QStringLiteral("current"), sample(true, 1200), kT0).isSwitchTo());
    // 刚切过一次，紧接着又劣化也不该再切（否则一次抖动就连跳两个节点）。
    EXPECT_TRUE(r.observe(QStringLiteral("current"), sample(true, 1200), kT0).isStay());
}

// #[test] fn bandwidth_guard_triggers_switch_when_enabled
TEST(Router, BandwidthGuardTriggersSwitchWhenEnabled)
{
    Router::RouterConfig cfg = Router::RouterConfig::defaults();
    cfg.latencyThresholdMs = 5000;
    cfg.minSwitchInterval = Router::durationFromSecs(0);
    cfg.bandwidthThresholdKbps = 200.0;
    cfg.bandwidthBadStreak = 2;
    Router::NodeRouter r(cfg);

    Router::Sample slow;
    slow.ok = true;
    slow.latencyMs = 150;
    slow.downloadKbps = 20.0;
    Router::Sample fast;
    fast.ok = true;
    fast.latencyMs = 200;
    fast.downloadKbps = 1500.0;

    for (int index = 0; index < 3; ++index) {
        r.observe(QStringLiteral("slow"), slow, kT0);
        r.observe(QStringLiteral("fast"), fast, kT0);
    }
    const Router::Decision decision = r.observe(QStringLiteral("slow"), slow, kT0);
    ASSERT_TRUE(decision.isSwitchTo()) << "expected bandwidth switch, got "
                                       << decisionText(decision);
    EXPECT_EQ(decision.nodeId, QStringLiteral("fast"));
    EXPECT_TRUE(decision.reason.contains(Router::Messages::bandwidthHead()))
        << decision.reason.toStdString();
}

// #[test] fn idle_nodes_are_not_judged_by_bandwidth
TEST(Router, IdleNodesAreNotJudgedByBandwidth)
{
    Router::RouterConfig cfg = Router::RouterConfig::defaults();
    cfg.latencyThresholdMs = 5000;
    cfg.minSwitchInterval = Router::durationFromSecs(0);
    cfg.bandwidthThresholdKbps = 200.0;
    cfg.bandwidthBadStreak = 2;
    Router::NodeRouter r(cfg);
    for (int index = 0; index < 6; ++index) {
        // 只有延迟样本、没有带宽样本：不该被"带宽不足"判定拖去切换。
        EXPECT_TRUE(r.observe(QStringLiteral("idle"), sample(true, 100), kT0).isStay());
    }
}

// #[test] fn failure_penalty_makes_an_unstable_node_lose_to_a_stable_one
TEST(Router, FailurePenaltyMakesAnUnstableNodeLoseToAStableOne)
{
    Router::NodeRouter r = router();
    for (int index = 0; index < 3; ++index) {
        r.observe(QStringLiteral("jittery"), sample(true, 300), kT0);
    }
    r.observe(QStringLiteral("jittery"), sample(false, 1000), kT0);
    r.observe(QStringLiteral("jittery"), sample(true, 300), kT0);
    r.observe(QStringLiteral("steady"), sample(true, 320), kT0);
    const std::optional<double> jittery = healthOf(r, QStringLiteral("jittery")).score(r.cfg, kT0);
    const std::optional<double> steady = healthOf(r, QStringLiteral("steady")).score(r.cfg, kT0);
    ASSERT_TRUE(jittery.has_value());
    ASSERT_TRUE(steady.has_value());
    EXPECT_GT(*jittery, *steady) << "jittery=" << *jittery << " steady=" << *steady;
}

// #[test] fn kernel_latency_makes_unmeasured_nodes_rankable
// url-test 开着时，候选的延迟来自内核 history（app 层不再自己逐个探测）。
// 这条路径以前完全没有数据，调度器等于瞎子。
TEST(Router, KernelLatencyMakesUnmeasuredNodesRankable)
{
    Router::NodeRouter r = router();
    r.noteKernelLatency(QStringLiteral("kernel-only"), 120, kT0);
    const std::optional<Router::NodeHealth> kernelOnly = r.health(QStringLiteral("kernel-only"));
    ASSERT_TRUE(kernelOnly.has_value());
    EXPECT_DOUBLE_EQ(kernelOnly->ewmaMs, 120.0);
    EXPECT_TRUE(kernelOnly->score(r.cfg, kT0).has_value());

    // 内核没测出来的成员写 delay=0，不能被当成"0ms 的超快节点"。
    r.noteKernelLatency(QStringLiteral("never-measured"), 0, kT0);
    EXPECT_FALSE(r.health(QStringLiteral("never-measured")).has_value());

    // 已有 EWMA 按 alpha 混合，且失败计数不受内核数据影响。
    r.observe(QStringLiteral("flaky"), sample(false, 5000), kT0);
    const quint32 failures = healthOf(r, QStringLiteral("flaky")).consecutiveFailures;
    r.noteKernelLatency(QStringLiteral("flaky"), 100, kT0 + Router::durationFromSecs(1));
    const Router::NodeHealth state = healthOf(r, QStringLiteral("flaky"));
    EXPECT_EQ(state.consecutiveFailures, failures);
    EXPECT_GT(state.ewmaMs, 100.0) << "ewma=" << state.ewmaMs;
    EXPECT_LT(state.ewmaMs, 5000.0) << "ewma=" << state.ewmaMs;
}

// #[test] fn blacklisted_lists_only_nodes_with_an_open_breaker
// url-test 开着时，app 层靠这份名单把坏节点从下一批候选里摘掉。
TEST(Router, BlacklistedListsOnlyNodesWithAnOpenBreaker)
{
    Router::NodeRouter r = router();
    EXPECT_TRUE(r.blacklisted(kT0).isEmpty());
    r.observe(QStringLiteral("dead"), sample(false, 5000), kT0);
    EXPECT_TRUE(r.blacklisted(kT0).isEmpty()) << "a single failure must not open the breaker";
    r.observe(QStringLiteral("dead"), sample(false, 5000), kT0);
    EXPECT_EQ(r.blacklisted(kT0), QStringList{QStringLiteral("dead")});
    // 冷却结束后必须放行，否则它永远进不了候选。
    EXPECT_TRUE(
        r.blacklisted(kT0 + r.cfg.breakerCooldown + Router::durationFromSecs(1)).isEmpty());
}

// ---- mod backup_tests ----

namespace {

QSet<QString> none()
{
    return QSet<QString>();
}

/// Rust 测试里的 `fn node(id, name, ms)`，host 同样取 id 的首字节（`id.as_bytes()[0]`）。
Router::BackupCandidate node(const QString &id, const QString &name, std::optional<quint64> ms)
{
    Router::BackupCandidate candidate;
    candidate.id = id;
    candidate.name = name;
    // Rust 的 id.as_bytes()[0]：空 id 会 panic，这里没有这种输入，取值就够了。
    const QByteArray bytes = id.toUtf8();
    const quint8 firstByte = bytes.isEmpty() ? quint8(0) : quint8(bytes.at(0));
    candidate.host = QStringLiteral("10.0.0.%1").arg(firstByte);
    candidate.port = 443;
    candidate.lastLatencyMs = ms;
    return candidate;
}

QStringList idsOf(const QVector<Router::BackupCandidate> &picked)
{
    QStringList ids;
    for (const Router::BackupCandidate &candidate : picked) {
        ids.append(candidate.id);
    }
    return ids;
}

} // namespace

// #[test] fn backups_are_ordered_by_last_latency_and_skip_the_current_node
TEST(RouterBackups, BackupsAreOrderedByLastLatencyAndSkipTheCurrentNode)
{
    const QVector<Router::BackupCandidate> all = {
        node(QStringLiteral("c"), RouterTestCjk::currentNodeName(), 90),
        node(QStringLiteral("b"), RouterTestCjk::midNodeName(), 300),
        node(QStringLiteral("a"), RouterTestCjk::fastestNodeName(), 120),
    };
    const QVector<Router::BackupCandidate> picked =
        Router::pickBackups(all, QStringLiteral("c"), none(), QString(), 8);
    // 当前节点要排掉，顺序按上次延迟。
    EXPECT_EQ(idsOf(picked), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

// #[test] fn blacklisted_and_untested_and_excluded_names_never_become_backups
TEST(RouterBackups, BlacklistedAndUntestedAndExcludedNamesNeverBecomeBackups)
{
    const QVector<Router::BackupCandidate> all = {
        node(QStringLiteral("x"), RouterTestCjk::brokenNodeName(), 100),
        node(QStringLiteral("y"), RouterTestCjk::untestedNodeName(), std::nullopt),
        node(QStringLiteral("z"), RouterTestCjk::expiredThrottledNodeName(), 110),
        node(QStringLiteral("w"), RouterTestCjk::usableNodeName(), 500),
    };
    QSet<QString> black;
    black.insert(QStringLiteral("x"));
    const QVector<Router::BackupCandidate> picked = Router::pickBackups(
        all, QStringLiteral("cur"), black, RouterTestCjk::excludeKeywords(), 8);
    // 拉黑的、没测过的、命中排除词的都不该当替补。
    EXPECT_EQ(idsOf(picked), QStringList{QStringLiteral("w")});
}

// #[test] fn limit_caps_the_shortlist_and_an_empty_pool_is_honest
TEST(RouterBackups, LimitCapsTheShortlistAndAnEmptyPoolIsHonest)
{
    QVector<Router::BackupCandidate> all;
    for (quint64 index = 0; index < 20; ++index) {
        all.append(node(QStringLiteral("n%1").arg(index), QStringLiteral("N%1").arg(index), index));
    }
    EXPECT_EQ(Router::pickBackups(all, QStringLiteral("cur"), none(), QString(), 6).size(), 6);
    EXPECT_TRUE(Router::pickBackups({}, QStringLiteral("cur"), none(), QString(), 8).isEmpty());
    // 只剩当前节点一个候选时，必须回空 —— 这就是"没切成"那种情况。
    const QVector<Router::BackupCandidate> onlyOne = {
        node(QStringLiteral("c"), RouterTestCjk::onlyChildNodeName(), 1),
    };
    EXPECT_TRUE(Router::pickBackups(onlyOne, QStringLiteral("c"), none(), QString(), 8).isEmpty());
}
