#include "router/Router.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <limits>

#include "models/Settings.h"
#include "router/RouterMessages.h"

namespace Router {

Instant nowMonotonic()
{
    // 对应 Rust 的 Instant::now()：单调、只会往前走。第一次调用时起表，之后读纳秒增量。
    // Rust 的 Instant 没有 epoch，只有差值有意义，所以这里的"起点"同样不外露。
    static const QElapsedTimer timer = [] {
        QElapsedTimer clock;
        clock.start();
        return clock;
    }();
    return timer.nsecsElapsed();
}

namespace {

/// Rust 的 `{:.0}`：对二进制精确值做"四舍五入到偶数"（0.5→0、1.5→2、2.5→2、200.5→200），
/// 并且值小于 0 时保留负号（-0.5 打成 `-0`）。用整数比较而不是 nearbyint，是为了不受
/// 运行期舍入模式影响。
QString fixed0(double value)
{
    if (!std::isfinite(value) || std::abs(value) >= 9.0e18) {
        // 分数只有在这种病值下才会走到这里（例如 current_score 兜底成 f64::MAX，
        // 但那条路径在 format! 之前就已返回 Stay）。Rust 会打完整位数，这里退回 Qt 的
        // 十进制表示，实际调用路径碰不到。
        return QString::number(value, 'f', 0);
    }
    const bool negative = std::signbit(value);
    const double magnitude = std::abs(value);
    double rounded = std::floor(magnitude);
    const double fraction = magnitude - rounded;
    if (fraction > 0.5) {
        rounded += 1.0;
    } else if (fraction == 0.5 && std::fmod(rounded, 2.0) != 0.0) {
        rounded += 1.0;
    }
    QString text = QString::number(static_cast<qulonglong>(rounded));
    if (negative) {
        text.prepend(QLatin1Char('-'));
    }
    return text;
}

/// Rust 的 `format!` 模板：把 `{}`、`{:.0}`、`{detail}` 这些占位符按出现顺序换成参数。
/// 模板本身是从 router.rs 逐字生成的（RouterMessages），所以参数顺序与 Rust 一致。
QString applySpec(const QString &spec, const QStringList &values)
{
    QString out;
    qsizetype cursor = 0;
    qsizetype argument = 0;
    while (true) {
        const qsizetype open = spec.indexOf(QLatin1Char('{'), cursor);
        if (open < 0) {
            break;
        }
        const qsizetype close = spec.indexOf(QLatin1Char('}'), open);
        if (close < 0) {
            break;
        }
        out.append(spec.mid(cursor, open - cursor));
        // 参数用完了就留空串：Rust 的 format! 是编译期检查的，这里只会防御误改模板。
        out.append(argument < values.size() ? values.at(argument) : QString());
        argument += 1;
        cursor = close + 1;
    }
    out.append(spec.mid(cursor));
    return out;
}

/// Rust 的 `HashMap::get(..).copied().unwrap_or(0)`。
quint32 streakValue(const QMap<QString, quint32> &streaks, const QString &nodeId)
{
    return streaks.value(nodeId, 0);
}

} // namespace

NodeHealth NodeHealth::create(Instant now)
{
    NodeHealth health;
    health.ewmaMs = 0.0;
    health.ewmaDownloadKbps = 0.0;
    health.samples = 0;
    health.consecutiveFailures = 0;
    health.breakerOpenUntil = std::nullopt;
    health.lastUpdate = now;
    return health;
}

std::optional<double> NodeHealth::score(const RouterConfig &config, Instant now) const
{
    if (breakerOpen(now) || samples == 0) {
        return std::nullopt;
    }
    const double bandwidthDiscount =
        std::min(ewmaDownloadKbps, config.bandwidthCapKbps) * config.bandwidthWeight;
    return ewmaMs - bandwidthDiscount
        + double(quint32(consecutiveFailures)) * config.failurePenaltyMs;
}

Decision Decision::stay()
{
    Decision decision;
    decision.kind = DecisionKind::Stay;
    return decision;
}

Decision Decision::switchTo(QString nodeIdValue, QString reasonValue)
{
    Decision decision;
    decision.kind = DecisionKind::SwitchTo;
    decision.nodeId = std::move(nodeIdValue);
    decision.reason = std::move(reasonValue);
    return decision;
}

Decision Decision::breakerOpened(QString nodeIdValue, QString reasonValue)
{
    Decision decision;
    decision.kind = DecisionKind::BreakerOpened;
    decision.nodeId = std::move(nodeIdValue);
    decision.reason = std::move(reasonValue);
    return decision;
}

NodeRouter::NodeRouter(RouterConfig config) : cfg(std::move(config)) {}

void NodeRouter::setActive(const QString &nodeId)
{
    active_ = nodeId;
}

std::optional<QString> NodeRouter::active() const
{
    return active_;
}

std::optional<NodeHealth> NodeRouter::health(const QString &nodeId) const
{
    const auto found = states_.constFind(nodeId);
    if (found == states_.constEnd()) {
        return std::nullopt;
    }
    return found.value();
}

QStringList NodeRouter::blacklisted(Instant now) const
{
    QStringList ids;
    for (auto it = states_.constBegin(); it != states_.constEnd(); ++it) {
        if (it.value().breakerOpen(now)) {
            ids.append(it.key());
        }
    }
    return ids;
}

NodeHealth &NodeRouter::entry(const QString &nodeId, Instant now)
{
    auto found = states_.find(nodeId);
    if (found == states_.end()) {
        found = states_.insert(nodeId, NodeHealth::create(now));
    }
    return found.value();
}

Decision NodeRouter::observe(const QString &nodeId, Sample sample, Instant now)
{
    const double alpha = cfg.alpha;
    const quint32 breakerFailures = cfg.breakerFailures;

    {
        NodeHealth &state = entry(nodeId, now);
        state.lastUpdate = now;
        if (sample.ok) {
            state.consecutiveFailures = 0;
            state.breakerOpenUntil = std::nullopt;
        } else {
            state.consecutiveFailures += 1;
        }
        // 成功与失败两条分支在 Rust 里写的是同一段 EWMA 更新（失败样本也要用超时值抬一下
        // EWMA，否则一个"完全不通"的节点分数反而比慢节点更好看），所以这里合并成一次。
        if (sample.latencyMs.has_value()) {
            const double ms = double(*sample.latencyMs);
            state.ewmaMs = state.samples == 0 ? ms : alpha * ms + (1.0 - alpha) * state.ewmaMs;
        }
        state.samples += 1;
    }

    // 劣化计数看「这一次原始样本是否超阈值」，评分看 EWMA。分开是有原因的：
    // 一次 6s 尖峰会把 EWMA 抬到阈值以上且迟迟落不回来，如果计数也看 EWMA，
    // 后面几个正常样本照样被算成劣化，尖峰就被延后放大成一次切换。
    const bool rawOverThreshold = sample.ok && sample.latencyMs.has_value()
        && *sample.latencyMs > cfg.latencyThresholdMs;
    const quint32 streak =
        rawOverThreshold ? streakValue(degradedStreaks_, nodeId) + 1 : quint32(0);
    degradedStreaks_.insert(nodeId, streak);
    const bool degradedByLatency = streak >= cfg.degradedStreak;

    if (sample.downloadKbps.has_value()) {
        const double kbps = *sample.downloadKbps;
        NodeHealth &state = entry(nodeId, now);
        state.ewmaDownloadKbps = state.ewmaDownloadKbps == 0.0
            ? kbps
            : alpha * kbps + (1.0 - alpha) * state.ewmaDownloadKbps;
    }

    const bool opened = [this, &nodeId, &now, breakerFailures] {
        const Duration cooldown = cfg.breakerCooldown;
        NodeHealth &state = entry(nodeId, now);
        if (state.consecutiveFailures >= breakerFailures
            && !state.breakerOpen(now + durationFromSecs(1))) {
            state.breakerOpenUntil = now + cooldown;
            return true;
        }
        return false;
    }();
    if (opened) {
        const QString reason = applySpec(Messages::breakerReason(),
                                         {QString::number(breakerFailures),
                                          QString::number(durationSecs(cfg.breakerCooldown) / 60)});
        lastSwitch_ = now;
        return Decision::breakerOpened(nodeId, reason);
    }

    // Rust 的 `||` 短路：延迟已经判劣化时不会再调用 bandwidth_degraded，
    // 也就不会推进带宽计数。顺序必须一样，否则带宽 streak 会多算。
    const bool slow = degradedByLatency || bandwidthDegraded(nodeId, now);
    if (!slow) {
        return Decision::stay();
    }

    if (lastSwitch_.has_value() && (now - *lastSwitch_) < cfg.minSwitchInterval) {
        return Decision::stay();
    }

    const auto candidate = bestCandidate(nodeId, now);
    if (!candidate.has_value()) {
        return Decision::stay();
    }
    const QString target = candidate->first;
    const double targetScore = candidate->second;
    const double currentScore = [this, &nodeId, &now] {
        const auto found = states_.constFind(nodeId);
        if (found == states_.constEnd()) {
            return std::numeric_limits<double>::max();
        }
        const std::optional<double> score = found.value().score(cfg, now);
        return score.value_or(std::numeric_limits<double>::max());
    }();
    if (targetScore >= currentScore * cfg.hysteresisRatio) {
        return Decision::stay();
    }

    lastSwitch_ = now;
    bandwidthStreak_.insert(nodeId, 0);
    const QString detail = degradedByLatency
        ? applySpec(Messages::latencyDetail(),
                    {fixed0(currentScore), QString::number(cfg.latencyThresholdMs)})
        : applySpec(Messages::bandwidthDetail(), {fixed0(cfg.bandwidthThresholdKbps)});
    return Decision::switchTo(
        target,
        applySpec(Messages::switchReason(),
                  {detail, fixed0(currentScore), fixed0(targetScore)}));
}

bool NodeRouter::bandwidthDegraded(const QString &nodeId, Instant now)
{
    if (cfg.bandwidthThresholdKbps <= 0.0) {
        return false;
    }
    const auto found = states_.constFind(nodeId);
    // 只有确实采到过带宽样本才参与判定，否则空闲时会被误判成"慢"。
    const double ewma = found == states_.constEnd() ? 0.0 : found.value().ewmaDownloadKbps;
    const bool measured = found != states_.constEnd() && found.value().ewmaDownloadKbps > 0.0;
    const quint32 streak =
        (measured && ewma < cfg.bandwidthThresholdKbps)
            ? streakValue(bandwidthStreak_, nodeId) + 1
            : quint32(0);
    bandwidthStreak_.insert(nodeId, streak);
    Q_UNUSED(now); // Rust 里这个参数也只是 `let _ = now;`，保留签名一致。
    return streak >= cfg.bandwidthBadStreak;
}

std::optional<std::pair<QString, double>> NodeRouter::bestCandidate(const QString &exclude,
                                                                    Instant now) const
{
    std::optional<std::pair<QString, double>> best;
    for (auto it = states_.constBegin(); it != states_.constEnd(); ++it) {
        if (it.key() == exclude) {
            continue;
        }
        const std::optional<double> score = it.value().score(cfg, now);
        if (!score.has_value()) {
            continue;
        }
        // 对应 min_by：并列时取先遇到的那个（`<` 为假就不换，NaN 也当并列）。
        if (!best.has_value() || *score < best->second) {
            best = std::make_pair(it.key(), *score);
        }
    }
    return best;
}

void NodeRouter::seed(const QString &nodeId, quint64 latencyMs, bool ok, Instant now)
{
    NodeHealth &state = entry(nodeId, now);
    if (state.samples > 0) {
        return;
    }
    state.ewmaMs = double(latencyMs);
    state.consecutiveFailures = ok ? 0 : 1;
    state.samples = 1;
}

void NodeRouter::noteKernelLatency(const QString &nodeId, quint64 latencyMs, Instant now)
{
    if (latencyMs == 0) {
        // mihomo 对没测出来的成员写 0，不是"0ms 飞快"。
        return;
    }
    const double alpha = cfg.alpha;
    NodeHealth &state = entry(nodeId, now);
    state.ewmaMs = state.samples == 0
        ? double(latencyMs)
        : alpha * double(latencyMs) + (1.0 - alpha) * state.ewmaMs;
    state.samples = std::max(state.samples, quint32(1));
    state.lastUpdate = now;
}

QVector<RankingRow> NodeRouter::ranking(Instant now) const
{
    QVector<RankingRow> rows;
    rows.reserve(qsizetype(states_.size()));
    for (auto it = states_.constBegin(); it != states_.constEnd(); ++it) {
        RankingRow row;
        row.nodeId = it.key();
        row.score = it.value().score(cfg, now);
        row.health = it.value();
        rows.append(row);
    }
    // Rust 的 sort_by 是稳定排序，这里同样用 stable_sort；比较把"无分数"当成 f64::MAX。
    std::stable_sort(rows.begin(), rows.end(), [](const RankingRow &a, const RankingRow &b) {
        const double left = a.score.value_or(std::numeric_limits<double>::max());
        const double right = b.score.value_or(std::numeric_limits<double>::max());
        return left < right;
    });
    return rows;
}

QVector<BackupCandidate> pickBackups(const QVector<BackupCandidate> &all, const QString &currentId,
                                     const QSet<QString> &blacklist, const QString &keywords,
                                     size_t limit)
{
    QVector<const BackupCandidate *> pool;
    pool.reserve(all.size());
    for (const BackupCandidate &node : all) {
        if (node.id == currentId) {
            continue;
        }
        if (blacklist.contains(node.id)) {
            continue;
        }
        if (Models::nameExcluded(node.name, keywords)) {
            continue;
        }
        if (!node.lastLatencyMs.has_value()) {
            continue;
        }
        pool.append(&node);
    }
    // Rust 的 sort_by_key 是稳定排序，并列延迟保持输入顺序。
    std::stable_sort(pool.begin(), pool.end(), [](const BackupCandidate *a, const BackupCandidate *b) {
        return a->lastLatencyMs.value_or(std::numeric_limits<quint64>::max())
            < b->lastLatencyMs.value_or(std::numeric_limits<quint64>::max());
    });
    QVector<BackupCandidate> picked;
    const qsizetype take = std::min<qsizetype>(pool.size(), qsizetype(limit));
    picked.reserve(take);
    for (qsizetype index = 0; index < take; ++index) {
        picked.append(*pool.at(index));
    }
    return picked;
}

}
