#pragma once

#include <QHostAddress>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

#include "core/Result.h"
#include "kernel/KernelTypes.h"

/// src/speedtest/mod.rs 的类型面。测速本身在 SpeedTester.h。
namespace Speedtest {

/// 对应 Rust 的 `enum SpeedtestIntensity`（serde 用 snake_case）。
/// 档位只影响两个超时（见 prefilterTimeoutMs / proxyTimeoutMs），并发仍由设置决定。
enum class SpeedtestIntensity {
    Fast,
    Standard,
    Deep,
};

/// serde 的 `rename_all = "snake_case"` 取值：fast / standard / deep。
QString intensityToText(SpeedtestIntensity intensity);
/// 解析失败退回默认档（Standard），对应 serde 的 #[default]。
SpeedtestIntensity intensityFromText(const QString &text);

/// Rust 的 `#[default] Standard`。C++ 的值初始化（`SpeedtestIntensity{}`）会落到第一个
/// 枚举数 Fast，所以凡是要「拿默认档」的地方都用这个函数，别依赖零初始化。
inline constexpr SpeedtestIntensity defaultIntensity()
{
    return SpeedtestIntensity::Standard;
}

QString intensityLabel(SpeedtestIntensity intensity);
QString intensityDescription(SpeedtestIntensity intensity);

/// TCP 预筛只是真实代理握手前的粗筛，所以用一个与（更长的）真代理超时无关的短超时。
/// 对应 Rust 的 `TCP_PREFILTER_TIMEOUT_MS`（:75）。
inline constexpr int kTcpPrefilterTimeoutMs = 800;

/// 对应 Rust 的 `struct SpeedTestOptions`（:57），默认值见 `defaultOptions()`。
/// 后五项对应「系统设置 → 测速优化」的开关，默认全关 = 保持老行为。
struct SpeedTestOptions {
    /// 并发（run_speedtest 里 clamp 到 1..512）。
    int concurrency = 128;
    /// TCP 连接超时毫秒。
    int timeoutMs = 1500;
    /// 下载探测是尽力而为且慢，默认关。
    bool measureDownload = false;
    int downloadTestMs = 800;
    SpeedtestIntensity intensity = SpeedtestIntensity::Standard;
    /// 三个内核并行验证。
    bool parallelKernels = false;
    /// 常驻内核：一整轮只冷启动一个内核实例（大批次）。
    bool persistentKernel = false;
    /// N 分钟内已验证的节点直接沿用结果，0 = 不启用。
    int reuseVerifiedMinutes = 0;
    /// 只对「可疑」的失败节点做隔离复测。
    bool conditionalIsolation = false;
    /// 下载测速只对延迟前 N 名做（0 = 所有通过节点都测）。
    int downloadTopN = 0;
};

/// 对应 Rust 的 `impl Default for SpeedTestOptions`（:96）。
inline SpeedTestOptions defaultOptions()
{
    return SpeedTestOptions();
}

/// 对应 Rust run_speedtest 里按档位算出的预筛超时（:479-489）。
/// 抽成函数是为了能离线测（Rust 里是内联 match，测不到）。
qint64 prefilterTimeoutMs(const SpeedTestOptions &options);
/// 同上，真代理握手的超时（毫秒）。
qint64 proxyTimeoutMs(const SpeedTestOptions &options);

/// 对应 Rust 的 `enum SpeedTestPhase`（:77）。
enum class SpeedTestPhase {
    TcpPrefilter,
    RealProxy,
};

/// 对应 Rust 的 `struct SpeedTestProgress`（:83）。elapsed 是本轮开始以来的毫秒数
/// （Rust 是 Duration，界面只用毫秒，这里直接给 qint64 ms）。
struct SpeedTestProgress {
    SpeedTestPhase phase = SpeedTestPhase::TcpPrefilter;
    size_t done = 0;
    size_t total = 0;
    size_t ok = 0;
    size_t fail = 0;
    qint64 elapsedMs = 0;
    /// 真代理阶段当前在跑哪个内核；串行时为 None，并行时不报进度。
    std::optional<Kernel::Kind> kernel;
    std::optional<size_t> batchIndex;
    std::optional<size_t> batchTotal;
};

/// 对应 Rust 的 `Option<impl Fn(SpeedTestProgress) + Send + Sync>`：空 = None。
/// 回调只在驱动线程（调用 runSpeedtest 的那个线程）上被调用，顺序与完成顺序一致。
using ProgressFn = std::function<void(const SpeedTestProgress &)>;

/// 对应 Rust 的 `Option<impl Fn(usize, usize) + Send + Sync>`（测速全部用的计数回调）。
using CountProgressFn = std::function<void(size_t done, size_t total)>;

/// 对应 Rust 的 `(bool, Option<i64>, Option<f64>, String)` 四元组。
struct ProbeOutcome {
    bool ok = false;
    std::optional<qint64> latencyMs;
    std::optional<double> downloadKbps;
    QString message;
};

/// 一个 (host, port) 的解析结果，对应 Rust 的 `Result<Vec<SocketAddr>, String>`。
using ResolvedAddresses = Result<QVector<QHostAddress>>;

}
