#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/Result.h"

// Windows 平台层里"和进程/权限有关"的部分，对应 Rust src/platform/mod.rs：
// 提权检测与交接、作业对象、孤儿内核进程清理、工作集修剪、错误弹框。
//
// 注意这里的几条"动手"函数（requestElevation / killOrphanProxyCores /
// killDuplicateVpnParserInstances / showError）会弹 UAC、杀进程、弹模态框，
// 只在启动路径或用户显式操作里调用，测试与构建期间一律不许碰。
namespace Platform {

/// 提权交接时由新进程自己给出的参数：当前进程里同名的要先剔掉，否则同一个 flag 会出现两次。
/// 对应 Rust 的 `HANDOFF_OWNED_ARGS`。
QStringList handoffOwnedArgs();

/// 组装提权实例的命令行：继承当前进程的参数，但把 `extra` 里会重新给出的 flag 去掉。
///
/// `--start-proxy-node` 出现两次时 clap 直接 `ArgumentConflict` 并 `exit(2)`，提权实例在写下
/// 第一行日志之前就死掉（2026-10-05 现象：点 TUN 模式启动，一闪就没）。程序从快捷方式启动时
/// 没有这个参数，所以只有"带节点启动"之后（安装器/开机自启都是这样）才会复现。
/// 对应 Rust `elevated_arguments`。
QStringList elevatedArguments(const QStringList &current, const QStringList &extra);

/// Windows 命令行引号规则（MSVC argv 解析的那套）：空格与引号触发加引号，
/// 引号前的反斜杠成对加倍，结尾的反斜杠也要加倍。对应 Rust `quote_windows_argument`。
QString quoteWindowsArgument(const QString &value);

/// 把参数表拼成一整条命令行（每个参数各自按 Windows 规则转义，空格连接）。
/// 纯函数：requestElevation 用它，测试也用它。
QString commandLineForArguments(const QStringList &arguments);

/// 提权交接的 flag 名，必须与 Rust 侧 `--elevated-proxy-config` 一致：
/// 机器上可能还装着 Rust 建的实例，它会把这个 flag 交给本程序。
QString elevatedProxyConfigFlag();

/// base64url（无填充）编码交接配置。字母表与 Rust base64 0.22 的 URL_SAFE_NO_PAD 相同，
/// 所以 Rust 建的实例写下的值能被这边原样解开，反之亦然。
QString encodeHandoffBlob(const QByteArray &payload);
/// 解码交接配置；非法 base64url 时返回错误（Rust 侧是 `context("读取提权代理配置失败")`）。
Result<QByteArray> decodeHandoffBlob(const QString &encoded);

/// 当前进程是否以管理员权限运行。Rust 用的是 `IsUserAnAdmin()`，这边保持一致：
/// 换成 token 检查会改变判据（例如受限令牌/虚拟化打开时两者结论不同）。
bool isElevated();

/// 用 `runas` 动词重启自己并带上 `extraArgs`（会弹 UAC）。对应 Rust `request_elevation`。
Status requestElevation(const QStringList &extraArgs);

/// 把内存工作集交还给系统（收进托盘、切页之后调用）。对应 Rust `trim_working_set`。
void trimWorkingSet();

/// 作业对象的 RAII 句柄，析构时 CloseHandle；句柄关闭即触发 KILL_ON_JOB_CLOSE。
class JobObjectGuard {
public:
    JobObjectGuard() = default;
    explicit JobObjectGuard(void *handle)
        : handle_(handle)
    {
    }
    JobObjectGuard(const JobObjectGuard &) = delete;
    JobObjectGuard &operator=(const JobObjectGuard &) = delete;
    JobObjectGuard(JobObjectGuard &&other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }
    JobObjectGuard &operator=(JobObjectGuard &&other) noexcept
    {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~JobObjectGuard();

    void *handle() const { return handle_; }
    bool isValid() const { return handle_ != nullptr; }

private:
    void close();
    void *handle_ = nullptr;
};

/// 创建本程序自己的作业对象（KILL_ON_JOB_CLOSE），失败返回无效 guard。
/// 对应 Rust `init_process_job`。
JobObjectGuard initProcessJob();

/// 幂等地惰性初始化全局作业对象。对应 Rust `ensure_job_initialized`。
void ensureJobInitialized();

/// 当前进程是否被圈进"本程序自己创建的那个作业对象"。
///
/// 判据刻意用我们自己的作业句柄去比，而不是 `hJob = NULL`：NULL 是"和调用者的作业比较"，
/// 两边都没作业时返回什么由不得我们控制，拿它当"我在不在作业里"会得出含糊结论
/// （我第一版就是这么写，测试直接假失败）。对应 Rust `current_process_in_our_job`。
bool currentProcessInOurJob();

/// 把已经启动的子进程（内核）登记进作业对象：应用退出时作业关闭，内核跟着死。
/// 对应 Rust `assign_child_process_id`。
void assignChildProcessId(quint32 pid);

/// 孤儿内核清理的匹配名单：mihomo.exe / xray.exe / sing-box.exe（Rust 里写死的是这三个，
/// 这边改用 Kernel::kindExecutableName 推导，集合一致）。
QStringList orphanCoreImageNames();

/// 纯判据：这个映像名（大小写不敏感）是不是要清理的内核进程。
bool isOrphanProxyCoreImage(const QString &imageName);

/// 纯判据：这个映像名是不是本程序自己（用来找重复实例）。
bool isDuplicateAppImage(const QString &imageName);

struct ProcessEntry {
    quint32 pid = 0;
    QString imageName;
};

/// ToolHelp 快照的可读部分（不杀任何东西），供清理函数与测试共用。
QVector<ProcessEntry> snapshotProcesses();

/// 杀掉残留的内核进程，返回杀掉的数量。对应 Rust `kill_orphan_proxy_cores`：
/// 判据是"映像名命中"，不看路径也不看作业成员身份（Rust 就是这样，照抄）。危险函数。
int killOrphanProxyCores();

/// 杀掉除自己以外的 `vpn-parser.exe` 实例，返回杀掉的数量；杀过就刷一次通知区域。
/// 对应 Rust `kill_duplicate_vpn_parser_instances`。危险函数。
int killDuplicateVpnParserInstances();

/// 弹一个 MB_OK | MB_ICONERROR 的原生错误框（Rust 参数顺序是 text 在前、title 在后）。
void showError(const QString &title, const QString &text);

}
