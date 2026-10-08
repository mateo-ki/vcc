#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <utility>

// 节点选路调度器的移植，对应 Rust 的 src/router.rs（EWMA 延迟 + 断路器 + 综合打分 + 滞后切换）。
//
// 取代原来"连续 3 次超阈值就切、切完 3 分钟冷却"的朴素判断。设计要点（照抄 Rust 的模块注释）：
//
// - **EWMA**：单次尖峰（一次 GC、一个后台大下载）不该把节点判死，指数加权移动
//   平均让历史平滑衰减。
// - **断路器**：连续 `breaker_failures` 次验证失败就把节点拉黑 `breaker_cooldown`，
//   期间不参与候选也不被选中，避免反复撞同一个死节点。
// - **打分**：延迟主导，带宽作折扣，失败次数按惩罚项累加，分数越低越好。
// - **滞后（hysteresis）**：只有当候选明显比当前节点好（默认好 25%）才切，否则
//   在两个分数接近的节点之间来回跳，每次跳都要断一次连接。
// - **带宽保护**：可选，当前节点持续低于阈值 KB/s 也按劣化处理。
//
// 这一层只有纯逻辑加一个单调时钟：不碰 Qt Widgets、不碰数据库、不做网络。所有"现在几点"
// 都是参数（默认取进程单调时钟），这样测试能像 Rust 的 `mod tests` 那样注入假时间。
namespace Router {

/// 对应 Rust 的 `std::time::Instant`：单调时钟的纳秒读数，只有差值有意义。
using Instant = qint64;
/// 对应 Rust 的 `std::time::Duration`：非负纳秒。
using Duration = qint64;

inline constexpr qint64 kNanosPerSecond = 1000000000LL;

/// `Duration::from_secs(secs)`
inline Duration durationFromSecs(qint64 secs)
{
    return secs * kNanosPerSecond;
}
/// `duration.as_secs()`（整数秒，向下取整）
inline qint64 durationSecs(Duration duration)
{
    return duration / kNanosPerSecond;
}

/// 对应 Rust 的 `Instant::now()`：进程启动以来的单调纳秒（不受系统时钟回拨影响）。
Instant nowMonotonic();

struct RouterConfig;

/// 对应 Rust 的 `router::Sample`。
struct Sample {
    /// 真实代理握手是否成功。
    bool ok = false;
    /// 本次实测延迟（ms），`ok == false` 时可以是超时值。
    std::optional<quint64> latencyMs;
    /// 本次采样的下行速率（KB/s），只有跑流量时才有。
    std::optional<double> downloadKbps;

    bool operator==(const Sample &) const = default;
};

/// 对应 Rust 的 `router::NodeHealth`。
///
/// Rust 的 `NodeHealth::new` 是私有的（只有 `Router::entry` 用），这里同样只当内部构造用。
struct NodeHealth {
    double ewmaMs = 0.0;
    double ewmaDownloadKbps = 0.0;
    quint32 samples = 0;
    quint32 consecutiveFailures = 0;
    /// 断路器打开的截止时间；有值且未到期表示该节点暂时不可用。
    std::optional<Instant> breakerOpenUntil;
    Instant lastUpdate = 0;

    static NodeHealth create(Instant now);

    bool breakerOpen(Instant now) const
    {
        return breakerOpenUntil.has_value() && *breakerOpenUntil > now;
    }

    /// 分数越低越好。单位是"等效毫秒"：带宽每 KB/s 折 0.25ms，每次连续失败罚
    /// 一整段超时时长，断路器打开则直接判为不可选。
    /// 对应 Rust 的 `Option<f64>`：不可选时返回空值。
    std::optional<double> score(const RouterConfig &config, Instant now) const;
};

/// 对应 Rust 的 `router::RouterConfig`。默认值取 Rust `impl Default for RouterConfig` 那一套，
/// 直接写成成员初值，所以 `RouterConfig{}` 与 `RouterConfig::defaults()` 等价。
struct RouterConfig {
    /// EWMA 平滑系数：新样本权重。0.3 ≈ 记忆最近 3-4 次。
    double alpha = 0.3;
    /// 延迟劣化阈值（ms），超过即算一次"差样本"。
    quint64 latencyThresholdMs = 1500;
    /// 连续失败几次开断路。
    quint32 breakerFailures = 2;
    /// 断路冷却时长。
    Duration breakerCooldown = durationFromSecs(300);
    /// 候选必须比当前节点好多少才切（0.25 = 便宜 25% 以上）。
    double hysteresisRatio = 0.75;
    /// 两次自动切换的最小间隔。
    Duration minSwitchInterval = durationFromSecs(120);
    /// 连续几次"EWMA 已越过阈值"才真的切：单次尖峰就算把 EWMA 抬上去也不动，
    /// 60 秒一次探测时相当于要求持续 2 分钟劣化。
    quint32 degradedStreak = 2;
    /// 带宽保护：低于该 KB/s 连续 N 次算劣化；`threshold == 0` 表示关闭。
    double bandwidthThresholdKbps = 0.0;
    quint32 bandwidthBadStreak = 3;
    /// 打分时带宽折扣的封顶与权重（避免大下载节点无限加分）。
    double bandwidthCapKbps = 2048.0;
    double bandwidthWeight = 0.25;
    double failurePenaltyMs = 3000.0;

    static RouterConfig defaults()
    {
        return RouterConfig();
    }

    bool operator==(const RouterConfig &) const = default;
};

/// 对应 Rust 的 `router::Decision`（枚举带载荷 → 判别式 + 载荷的结构体）。
enum class DecisionKind {
    /// 留在当前节点。
    Stay,
    /// 切到指定节点，附带可读原因。
    SwitchTo,
    /// 当前节点断路器刚打开。
    BreakerOpened,
};

struct Decision {
    DecisionKind kind = DecisionKind::Stay;
    /// `SwitchTo` 的目标节点 / `BreakerOpened` 的当事节点；`Stay` 时为空。
    QString nodeId;
    QString reason;

    static Decision stay();
    static Decision switchTo(QString nodeIdValue, QString reasonValue);
    static Decision breakerOpened(QString nodeIdValue, QString reasonValue);

    bool isStay() const { return kind == DecisionKind::Stay; }
    bool isSwitchTo() const { return kind == DecisionKind::SwitchTo; }
    bool isBreakerOpened() const { return kind == DecisionKind::BreakerOpened; }

    bool operator==(const Decision &) const = default;
};

/// 对应 Rust 的 `ranking()` 返回元组 `(String, Option<f64>, NodeHealth)`。
struct RankingRow {
    QString nodeId;
    std::optional<double> score;
    NodeHealth health;
};

/// 对应 Rust 的 `router::Router`。
///
/// 类名不叫 `Router`：C++ 里成员类型与所在命名空间同名会让 `Router::` 限定名歧义
/// （同 `IpRisk::Risk` 的处理）。Rust 的 `#[derive(Default)]` 由"构造参数带默认值"覆盖。
class NodeRouter {
public:
    explicit NodeRouter(RouterConfig config = RouterConfig::defaults());

    void setActive(const QString &nodeId);
    /// Rust 的 `Option<&str>`。
    std::optional<QString> active() const;
    /// Rust 的 `Option<&NodeHealth>`。这里是快照拷贝：Rust 借出的那份只有读用法，
    /// 写一律走 `observe`/`seed`/`note_kernel_latency`。
    std::optional<NodeHealth> health(const QString &nodeId) const;

    /// 现在被断路器拉黑的节点。url-test 开着时，app 层不当场抢方向盘换节点，而是
    /// 把这些名字从**下一批候选**里摘掉（对应 `prime_url_test_candidates`）。
    QStringList blacklisted(Instant now = nowMonotonic()) const;

    /// 喂一次当前节点的实测样本，返回该不该切换。
    Decision observe(const QString &nodeId, Sample sample, Instant now = nowMonotonic());

    /// 除 `exclude` 外、断路器没打开的节点里分数最低的那个。
    std::optional<std::pair<QString, double>> bestCandidate(const QString &exclude,
                                                            Instant now = nowMonotonic()) const;

    /// 用数据库里最近一次测速结果给候选打底：调度器刚起来时没有任何历史样本，
    /// 不播种就没有候选可切。已有实测样本的节点不覆盖。
    void seed(const QString &nodeId, quint64 latencyMs, bool ok, Instant now = nowMonotonic());

    /// 内核 url-test 每 `interval` 秒自己测一轮成员延迟，那份历史是现成的。拿它刷新
    /// 候选的 EWMA，让 app 层和内核用同一份延迟口径；不动失败计数 —— "端到端通不通"
    /// 仍然只有我们那次 generate_204 探测知道。
    void noteKernelLatency(const QString &nodeId, quint64 latencyMs,
                           Instant now = nowMonotonic());

    /// 供界面展示：按分数排序的候选（分数越低越好）。
    QVector<RankingRow> ranking(Instant now = nowMonotonic()) const;

    /// Rust 的 `pub cfg`。测试直接拿它喂 `NodeHealth::score`。
    RouterConfig cfg;

private:
    NodeHealth &entry(const QString &nodeId, Instant now);
    bool bandwidthDegraded(const QString &nodeId, Instant now);

    QMap<QString, NodeHealth> states_;
    QMap<QString, quint32> bandwidthStreak_;
    QMap<QString, quint32> degradedStreaks_;
    std::optional<QString> active_;
    std::optional<Instant> lastSwitch_;
};

/// 自动切换前的替补复检结果，对应 Rust 的 `router::BackupCandidate`：`ok` 里是复检过、
/// 端口连得上的节点。
///
/// 前四个字段与 `ProxyProbe::ReachTarget` 一一对应（id/name/host/port），`lastLatencyMs`
/// 是选路侧才有的数据；映射由调用方做，这一层不依赖网络模块。
struct BackupCandidate {
    QString id;
    QString name;
    QString host;
    quint16 port = 0;
    /// 上一次测速延迟；只用来排序，不当成"现在还活着"的证据。
    std::optional<quint64> lastLatencyMs;

    bool operator==(const BackupCandidate &) const = default;
};

/// 自动切换的替补挑选：先按上次数据把顺序排出来，真正的"能不能连"交给
/// `ProxyProbe::firstReachable` 的 TCP 复检。理由很实在 —— 2026-10-07 那次
/// 自动切换就是照着 9 小时前的 90ms 记录切进了一个端口已经没人监听的节点。
QVector<BackupCandidate> pickBackups(const QVector<BackupCandidate> &all, const QString &currentId,
                                     const QSet<QString> &blacklist, const QString &keywords,
                                     size_t limit);

}
