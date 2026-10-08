#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "core/JsonUtil.h"
#include "core/Result.h"
#include "kernel/KernelRelease.h"
#include "kernel/KernelSource.h"
#include "kernel/KernelTypes.h"

namespace Kernel {

/// 对应 Rust `kernel::KernelInfo`：某个内核在这台机器上的现状。
/// `path` / `version` 用 null QString 表示 Rust 的 `Option<PathBuf>` / `Option<String>`。
struct Info {
    Kind kind = Kind::Mihomo;
    QString path;
    QString version;
    bool available = false;
    QString message;

    Json toJson() const;
    static Info fromJson(const Json &value);
};

/// 对应 Rust `pub const GODEBUG_COMPAT`：Go 运行时的 CPU 特性总开关（虚拟机 CPUID 撒谎时用）。
using EnvAssignments = QList<QPair<QString, QString>>;
const EnvAssignments &godebugCompat();

/// 对应 Rust `managed_kernel_root()`：`<当前工作目录>/data/kernels`，原生分隔符。
QString managedKernelRoot();

/// 对应 Rust `kernel_candidates`：**工作目录那条排在可执行文件目录之前**，
/// currentExe / currentDir 传 null QString 表示 Rust 的 `None`。
QStringList kernelCandidates(Kind kind, const QString &currentExe, const QString &currentDir);

/// 对应 Rust `find_first_existing_candidate`：按顺序取第一个"是文件"的。
std::optional<QString> findFirstExistingCandidate(const QStringList &candidates);

/// 对应 Rust `find_kernel_executable` 里的环境变量那一步（`MIHOMO_PATH` 等）。
QString environmentOverride(Kind kind);

/// 三个外部输入都注入的版本，测试用它；`findKernelExecutable` 就是它的现值包装。
std::optional<QString> findKernelExecutableWith(const QString &envOverride,
                                                const QString &currentExe,
                                                const QString &currentDir,
                                                Kind kind);
std::optional<QString> findKernelExecutable(Kind kind);
std::optional<QString> findMihomoExecutable();

/// "这个内核装好了吗"——`selectProxyKernel*` / `missingKernelFor` 的缺省判据。
bool kernelIsAvailable(Kind kind);

/// 对应 Rust `parse_version_output`：第一行非空（trim 后）输出。
std::optional<QString> parseVersionOutput(const QString &output);

/// 对应 Rust `classify_version_probe`，把"跑一下内核拿版本"的结果分类。
/// Rust 返回 `Result<Option<String>, String>`，其中 `Ok(None)` 不可达（输出为空必走 Err 分支），
/// 所以这里用 `Result<QString>`；`code` 为 nullopt 表示进程被杀/崩溃，没有退出码。
Result<QString> classifyVersionProbe(bool exitOk, const std::optional<int> &code, const QString &output);

/// 对应 Rust `read_kernel_version` / `read_kernel_version_with_env`：
/// 用 kind 的 `--version` 参数跑一次 `path`（CREATE_NO_WINDOW，5 秒总预算）。
/// 传可执行文件路径而不是自己找内核，测试因此能指向无害的桩。
Result<QString> readKernelVersion(Kind kind, const QString &path);
Result<QString> readKernelVersionWithEnv(Kind kind, const QString &path, const EnvAssignments &env);

/// 注入版"装好了吗"，对应 Rust 的 `impl Fn(KernelKind) -> bool`。
using Availability = std::function<bool(Kind)>;

/// 对应 Rust `select_proxy_kernel_with`：首选不可用时退让，且必须仍支持该协议。
Result<Kind> selectProxyKernelWithAvailability(const QString &protocol, const Availability &available);
/// 对应 Rust `select_proxy_kernel`。
Result<Kind> selectProxyKernel(const QString &protocol);

/// 对应 Rust `select_proxy_kernel_preference`：`systemModeAllowed` 即 Rust 形参 `allow_xray`
/// （只有系统代理模式才允许退到 Xray，TUN 下 Xray 直接报错）。
Result<Kind> selectProxyKernelPreference(const QString &protocol, Preference preference,
                                        bool systemModeAllowed);
/// 同上，但把安装检查注入进来，便于单测。
Result<Kind> selectProxyKernelPreferenceWithAvailability(const QString &protocol,
                                                         Preference preference,
                                                         bool systemModeAllowed,
                                                         const Availability &available);

/// 对应 Rust `missing_kernel_for`：点「启动代理」前的预检，返回"需要先去下载哪个内核"，
/// nullopt 表示可以直接启动。
std::optional<Kind> missingKernelFor(const QString &protocol, Preference preference, bool systemMode,
                                     const Availability &available);

/// 对应 Rust `KernelManager`。`detect` 在 Rust 里是 async，这里是同步实现（5 秒超时的
/// 子进程探测），调用方要自己放到后台线程。
class Manager {
public:
    Info infoForMissing(Kind kind) const;
    Info detect(Kind kind) const;
    /// 顺序固定为 mihomo → xray → sing-box，和 Rust `detect_all` 一致。
    QList<Info> detectAll() const;
};

/// 对应 Rust `check_updates_with_proxy` 里的 `fallback` 闭包：
/// 联网查 Release 属于 updater 模块，这里只搬"查失败时怎么写"这一支（可单测、不联网）。
ReleaseInfo releaseInfoFromCheck(const Result<ReleaseInfo> &checked, const Info &info);

}
