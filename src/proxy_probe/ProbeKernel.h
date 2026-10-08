#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>
#include <vector>

#include "core/Result.h"
#include "proxy_probe/ProbeTypes.h"

class QTcpServer;

namespace ProxyProbe {

/// 真实测速的并发常量，取值与 Rust proxy_probe.rs 一致。
/// kPersistentMihomoBatchSize（常驻内核一批多少节点）在 ProbeTypes.h 里。
inline constexpr qsizetype kProxyConcurrency = 96;
inline constexpr qsizetype kMihomoBatchSize = 256;
inline constexpr qsizetype kMihomoBatchConcurrency = 3;
/// `mihomo -t` 配置校验的并发度（隔离无效代理时一层最多同时起这么多个校验进程）。
inline constexpr qsizetype kValidateConcurrency = 8;

/// mihomo 控制器连接超时（Rust reqwest connect_timeout 2s）。
inline constexpr int kControllerConnectTimeoutMs = 2000;
/// 控制器请求的整体时限：timeout + 2s（Rust 的 `timeout + Duration::from_secs(2)`）。
inline constexpr int kControllerRequestSlackMs = 2000;
/// Rust 的 `timeout.as_millis().clamp(1_000, 15_000)`：传给 /delay 的 timeout 参数。
int clampDelayTimeoutMs(int timeoutMs);
/// Rust 的控制器就绪预算：`min(8s + 5ms * 代理数, 60s)`。加载上千个代理远不止 5 秒，
/// 预算不跟着节点数缩放就会把大批次误报成失败。
int controllerStartupBudgetMs(qsizetype proxyCount);

/// 对应 Rust `TcpListener::bind("127.0.0.1:0")` + 把 listener 攥到写完配置再 drop：
/// 端口是"占住的"，不是"猜的"。sing-box / xray 一批要 64 个，一次申请完。
class PortReservations {
public:
    PortReservations();
    ~PortReservations();
    PortReservations(const PortReservations &) = delete;
    PortReservations &operator=(const PortReservations &) = delete;

    /// 占一个临时端口并把 listener 留在手里。
    Result<quint16> reserve();
    /// 连续占 count 个端口（Rust 的 for _ in &nodes 循环）。
    Status reserveMany(qsizetype count, QVector<quint16> *ports);
    /// 释放全部占位（Rust 的 `drop(reservations)`）：内核马上要 bind 这些端口了。
    void release();

    QVector<quint16> ports() const { return ports_; }
    qsizetype count() const { return ports_.size(); }

private:
    // std::vector 而不是 QVector：元素是只准移动的 unique_ptr，QVector 的 element ops
    // 要求可拷贝，会直接编译失败。
    std::vector<std::unique_ptr<QTcpServer>> listeners_;
    QVector<quint16> ports_;
};

/// 一次内核运行的临时目录（Rust `std::env::temp_dir().join(format!("vpn-parser-{}-{}",
/// prefix, Uuid::new_v4()))`）。析构兜底删除，正常路径上按 Rust 的位置显式 remove。
class TempWorkDir {
public:
    TempWorkDir() = default;
    static Result<TempWorkDir> create(const QString &prefix);
    ~TempWorkDir();
    TempWorkDir(TempWorkDir &&other) noexcept;
    TempWorkDir &operator=(TempWorkDir &&other) noexcept;
    TempWorkDir(const TempWorkDir &) = delete;
    TempWorkDir &operator=(const TempWorkDir &) = delete;

    QString root() const { return root_; }
    QString filePath(const QString &name) const;
    bool isValid() const { return !root_.isEmpty(); }
    Status remove();

private:
    explicit TempWorkDir(QString root) : root_(std::move(root))
    {
    }

    QString root_;
};

/// 纯文本部分：写完文件/读日志/起进程这些 io 失败都用 Qt 的英文错误文本回报，
/// 与 Rust 直接 `?` 冒泡 io::Error 的做法保持"不新增中文文案"的一致效果。
Status writeTextFile(const QString &path, const QString &text);
/// Rust `tokio::fs::read_to_string(&log).await.unwrap_or_default()`：读不到就当空。
QString readLogFile(const QString &path);

/// 对应 `compact_diagnostic`：取最后一行非空文本，再截到 240 个字符（Rust 按 chars 计数，
/// 即 Unicode 码点，不是 UTF-16 单元）。全空时给 Rust 的兜底文案。
QString compactDiagnostic(const QString &text);
/// Rust `text.chars().take(n).collect()`。
QString takeCodePoints(const QString &text, qsizetype count);

/// Rust `utf8_percent_encode(name, NON_ALPHANUMERIC)`：除 [A-Za-z0-9] 之外每个 UTF-8
/// 字节都写成 %XX（大写十六进制）。控制器 URL 的路径分量用它。
QString percentEncodePath(const QString &text);
/// reqwest `query(&[..])` 用的 application/x-www-form-urlencoded：保留字母数字与
/// `*-._`，空格写成 `+`，其余 %XX。
QString formEncodeQuery(const QString &text);

/// 起内核进程：`CREATE_NO_WINDOW`、stdin 关闭、stdout+stderr 合并写日志文件，
/// 并把 PID 登记进本程序的作业对象（应用退出时内核跟着走）。
/// 只操作自己刚创建的 QProcess —— 绝不碰机器上已在跑的 Rust 实例。
Result<std::unique_ptr<QProcess>> startKernelProcess(const QString &program,
                                                     const QStringList &arguments,
                                                     const QString &logPath);

/// `mihomo -t` 那类"跑完拿输出"的调用，对应 Rust `command.output()`。
/// Rust 不带超时，这里给一个足够宽的预算，免得卡死测速线程；超时就杀自己起的进程。
struct ProcessOutput {
    bool started = false;
    /// 进程正常退出（对应 Rust `status.code().is_some()`）。
    bool finished = false;
    bool ok = false;
    int exitCode = -1;
    QString stdOut;
    QString stdErr;
    /// 只有 started 为 false 时才有意义（QProcess 自己的英文错误文本）。
    QString error;
};
ProcessOutput runProcessToOutput(const QString &program, const QStringList &arguments,
                                 const QString &workingDirectory, int budgetMs);

/// 对应 Rust 的 `child.try_wait()` 返回 `Some(status)`：进程是不是已经结束了。
/// exitText 拿 Rust 的 `ExitStatus` Display 文本（"exit status: N" / 崩溃）。
bool processExited(const QProcess *process, QString *exitText);
/// 进程还活着吗（Rust 的 try_wait 为 None）。
bool processRunning(const QProcess *process);

/// Rust `let _ = child.kill(); let _ = child.wait();`。
Status killProcess(QProcess *process);

/// wait_for_controller / wait_for_sing_box_port / xray 内联循环的公共形状：
/// 每 pollMs 试一次 TCP connect，进程提前退出就立刻判失败。
bool waitUntilPortReady(QProcess &process, quint16 port, int budgetMs, int pollMs);
/// 单次 connect（Rust `TcpStream::connect(("127.0.0.1", port))`）。
bool portAcceptsConnection(quint16 port, int connectTimeoutMs);

/// Rust 的 `nodes.chunks(batch_size.max(1))`：批次顺序 = 输入顺序。三个内核共用。
QVector<QVector<Models::Node>> chunkNodes(const QVector<Models::Node> &nodes, qsizetype batchSize);

/// 经 `http://127.0.0.1:<port>` 打一次测试地址：只有 204 算通（Rust 的
/// probe_connectivity_target 与 xray 内联那份的公共部分）。error 是**不带目标名前缀**的
/// 失败文本，调用方决定要不要拼 "Google: "（sing-box 拼，xray 不拼）。
struct ConnectivityCheck {
    bool ok = false;
    QString error;
};
ConnectivityCheck checkConnectivityThroughLocalProxy(quint16 port, const QString &targetUrl,
                                                     int timeoutMs);

/// Rust 的 `elapsed.as_millis().max(1)`：真实测到的延迟至少写 1ms，0 会被当成没测到。
qint64 probeLatencyMs(qint64 elapsedMs);

/// `results.iter().filter(|result| result.ok).count()`。
qsizetype countPassed(const QVector<ProbeResult> &results);

/// done/ok 的增量计数：8192 的批次如果每个探针完成都重扫一遍 results，进度回调节本身
/// 就把测速拖平（Rust 同一个注释里的账：一千三百万次迭代）。
struct ProgressCounter {
    qsizetype done = 0;
    qsizetype ok = 0;

    void markResult(bool wasOk)
    {
        ++done;
        if (wasOk) {
            ++ok;
        }
    }
};

/// emit_proxy_progress：fail = done - ok。
void emitProgress(const ProgressFn &progress, qsizetype done, qsizetype ok, qsizetype total,
                  qint64 elapsedMs, std::optional<qsizetype> batchIndex = std::nullopt,
                  std::optional<qsizetype> batchTotal = std::nullopt);
/// report_proxy_progress：从槽位数组现算 done/ok（Rust 在建配/隔离之后各报一次）。
void reportProgress(const ProgressFn &progress,
                    const QVector<std::optional<ProbeResult>> &results, qsizetype total,
                    qint64 elapsedMs);

/// 非空槽位数量，对应 `results.iter().filter(|slot| slot.is_some()).count()`。
qsizetype countedResults(const QVector<std::optional<ProbeResult>> &results);

}
