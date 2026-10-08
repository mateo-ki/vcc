#include "platform/ProcessControl.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <cstring>
#include <memory>
#include <mutex>

#include "core/Encoding.h"
#include "core/Log.h"
#include "kernel/KernelTypes.h"
#include "platform/Format.h"
#include "platform/Messages.h"
#include "platform/Tray.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <jobapi.h>
#include <jobapi2.h>
#include <shlobj.h> // IsUserAnAdmin 在这里声明（不在 shellapi.h 里）
#include <shellapi.h>
#include <tlhelp32.h>

#include "platform/WindowHelpers.h"
#endif

namespace Platform {

namespace {

/// Rust 的 `HANDOFF_OWNED_ARGS`：提权实例会自己重新给出这些 flag，继承命令行时要先剔掉。
const char *const kHandoffOwnedArgs[] = { "--start-proxy-node", "--elevated-proxy-config" };

#ifdef Q_OS_WIN
// GLOBAL_JOB 在 Rust 里是个 `OnceLock<Option<JobObjectGuard>>`：OnceLock 的槽位永不析构，
// 所以那个作业句柄一直活到进程结束（进程终止时由系统收回）。这里刻意用 new 泄漏同一个
// 语义：静态对象在退出时才 CloseHandle 的话，会在 atexit 阶段就把作业关掉，
// 反而把还活着的东西一起带走了。
JobObjectGuard *g_job = nullptr;
std::mutex g_jobMutex;

JobObjectGuard *jobHandleSlot()
{
    std::lock_guard<std::mutex> lock(g_jobMutex);
    if (g_job == nullptr) {
        g_job = new JobObjectGuard(initProcessJob());
    }
    return g_job;
}

/// ToolHelp 的 szExeFile 是 [WCHAR;32]，Rust 取到第一个 NUL 为止再 from_utf16_lossy。
QString imageNameOf(const PROCESSENTRY32W &entry)
{
    const int capacity = static_cast<int>(sizeof(entry.szExeFile) / sizeof(WCHAR));
    int length = 0;
    while (length < capacity && entry.szExeFile[length] != 0) {
        length += 1;
    }
    if (length <= 0) {
        return QString();
    }
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(entry.szExeFile), length);
}
#endif

} // namespace

QStringList handoffOwnedArgs()
{
    QStringList args;
    for (const char *flag : kHandoffOwnedArgs) {
        args << QString::fromLatin1(flag);
    }
    return args;
}

QString elevatedProxyConfigFlag()
{
    return QStringLiteral("--elevated-proxy-config");
}

QStringList elevatedArguments(const QStringList &current, const QStringList &extra)
{
    // 逐字照抄 Rust elevated_arguments 的循环：命中的 flag 连同它后面那个值一起跳过。
    QStringList args;
    args.reserve(current.size() + extra.size());
    for (int index = 0; index < current.size(); ++index) {
        const QString token = current.at(index);
        bool owned = false;
        for (const QString &flag : handoffOwnedArgs()) {
            if (token == flag || token.startsWith(flag + QLatin1Char('='))) {
                owned = true;
                break;
            }
        }
        if (owned) {
            const bool takesSeparateValue = !token.contains(QLatin1Char('='));
            const bool nextIsValue = index + 1 < current.size()
                && !current.at(index + 1).startsWith(QStringLiteral("--"));
            if (takesSeparateValue && nextIsValue) {
                index += 1;
            }
        } else {
            args << token;
        }
    }
    args << extra;
    return args;
}

QString quoteWindowsArgument(const QString &value)
{
    // 非空且不含空白/引号就原样返回（Rust 用 char::is_whitespace，这里用 QChar::isSpace，
    // 两者都是 Unicode White_Space 判据；代理对的两半既不是空格也不是引号，原样透传）。
    bool needsQuoting = value.isEmpty();
    if (!value.isEmpty()) {
        for (const QChar ch : value) {
            if (ch.isSpace() || ch == QLatin1Char('"')) {
                needsQuoting = true;
                break;
            }
        }
    }
    if (!needsQuoting) {
        return value;
    }

    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar ch : value) {
        if (ch == QLatin1Char('\\')) {
            backslashes += 1;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            quoted += QLatin1Char('"');
        } else {
            quoted += QString(backslashes, QLatin1Char('\\'));
            quoted += ch;
        }
        backslashes = 0;
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

QString commandLineForArguments(const QStringList &arguments)
{
    QStringList quoted;
    quoted.reserve(arguments.size());
    for (const QString &argument : arguments) {
        quoted << quoteWindowsArgument(argument);
    }
    return quoted.join(QLatin1Char(' '));
}

QString encodeHandoffBlob(const QByteArray &payload)
{
    return QString::fromLatin1(Encoding::encode(payload, Encoding::Engine::UrlSafeNoPad));
}

Result<QByteArray> decodeHandoffBlob(const QString &encoded)
{
    return Encoding::decode(encoded.toLatin1(), Encoding::Engine::UrlSafeNoPad);
}

#ifdef Q_OS_WIN

bool isElevated()
{
    // Rust 用的是 shell32 的 IsUserAnAdmin()，不是 token 检查：判据保持一致才不会出现
    // "同一台机器上 Rust 实例觉得已提权、C++ 实例觉得没提权"这种分裂。
    return IsUserAnAdmin() != 0;
}

Status requestElevation(const QStringList &extraArgs)
{
    const QString executable = QCoreApplication::applicationFilePath();
    if (executable.isEmpty()) {
        return Status::failed(QStringLiteral("current_exe failed: applicationFilePath() 为空"));
    }
    // Rust: std::env::args().skip(1)
    QStringList current = QCoreApplication::arguments();
    if (!current.isEmpty()) {
        current.removeFirst();
    }
    const QStringList args = elevatedArguments(current, extraArgs);
    const QString parameters = commandLineForArguments(args);

    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", reinterpret_cast<LPCWSTR>(executable.utf16()),
                                           reinterpret_cast<LPCWSTR>(parameters.utf16()), nullptr,
                                           SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        const QString pointerText = QStringLiteral("0x%1")
                                        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(result)), 0, 16);
        return Status::failed(Messages::elevationFailed().replace(QStringLiteral("{result:?}"),
                                                                  pointerText));
    }
    return Status::succeeded();
}

void trimWorkingSet()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

JobObjectGuard initProcessJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return JobObjectGuard();
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    std::memset(&info, 0, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    const BOOL res = SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info,
                                             static_cast<DWORD>(sizeof(info)));
    if (res == 0) {
        CloseHandle(job);
        return JobObjectGuard();
    }

    // 故意**不**把当前进程自己放进这个作业。之前放进去过，代价实测很硬：提权交接出去的
    // 第二个实例也算在这个作业里，于是 `KILL_ON_JOB_CLOSE` 让"父进程退出/被杀"
    // 连带把提权实例和它的 mihomo 一起打死，9980 当场变死端口（2026-10-05 踩过）。
    // 需要跟着一起死的只有内核，而它们本来就由 `assign_child_process_id` 显式登记。
    return JobObjectGuard(job);
}

void ensureJobInitialized()
{
    jobHandleSlot();
}

bool currentProcessInOurJob()
{
    // 判据刻意用我们自己的作业句柄去比，而不是 `hJob = NULL`：NULL 是"和调用者的作业比较"，
    // 两边都没作业时返回什么由不得我们控制，拿它当"我在不在作业里"会得出含糊结论
    // （Rust 第一版就是这么写，测试直接假失败）。
    ensureJobInitialized();
    const JobObjectGuard *job = jobHandleSlot();
    if (job == nullptr || !job->isValid()) {
        return false;
    }
    BOOL result = 0;
    IsProcessInJob(GetCurrentProcess(), job->handle(), &result);
    return result != 0;
}

void assignChildProcessId(quint32 pid)
{
    ensureJobInitialized();
    const JobObjectGuard *job = jobHandleSlot();
    if (job == nullptr || !job->isValid()) {
        return;
    }
    const HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
    if (handle == nullptr) {
        return;
    }
    AssignProcessToJobObject(job->handle(), handle);
    CloseHandle(handle);
}

QVector<ProcessEntry> snapshotProcesses()
{
    QVector<ProcessEntry> entries;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    // Rust 只查了 is_null()，但失败值其实是 INVALID_HANDLE_VALUE：那种情况下
    // Process32FirstW 直接返回 FALSE，结果同样是"一个都不处理"，这里把两种都挡掉。
    if (snapshot == nullptr || snapshot == INVALID_HANDLE_VALUE) {
        return entries;
    }

    PROCESSENTRY32W entry;
    std::memset(&entry, 0, sizeof(entry));
    entry.dwSize = static_cast<DWORD>(sizeof(entry));

    if (Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            entries.append(ProcessEntry { entry.th32ProcessID, imageNameOf(entry) });
        } while (Process32NextW(snapshot, &entry) != FALSE);
    }

    CloseHandle(snapshot);
    return entries;
}

int killOrphanProxyCores()
{
    int killedCount = 0;
    for (const ProcessEntry &entry : snapshotProcesses()) {
        if (!isOrphanProxyCoreImage(entry.imageName)) {
            continue;
        }
        // Rust 的判据就到映像名为止：不比对 exe 路径、也不查作业成员身份，
        // 命中名字就 OpenProcess(PROCESS_TERMINATE) + TerminateProcess(pid, 1)。照抄。
        const HANDLE processHandle = OpenProcess(PROCESS_TERMINATE, FALSE, entry.pid);
        if (processHandle == nullptr) {
            continue;
        }
        if (TerminateProcess(processHandle, 1) != 0) {
            killedCount += 1;
            Log::info(QStringLiteral("%1 pid=%2 exe=%3")
                          .arg(Messages::orphanCoreKilled())
                          .arg(entry.pid)
                          .arg(entry.imageName.toLower()));
        }
        CloseHandle(processHandle);
    }
    return killedCount;
}

int killDuplicateVpnParserInstances()
{
    const quint32 currentPid = static_cast<quint32>(GetCurrentProcessId());
    int killedCount = 0;
    for (const ProcessEntry &entry : snapshotProcesses()) {
        if (entry.pid == 0 || entry.pid == currentPid) {
            continue;
        }
        if (!isDuplicateAppImage(entry.imageName)) {
            continue;
        }
        const HANDLE processHandle = OpenProcess(PROCESS_TERMINATE, FALSE, entry.pid);
        if (processHandle == nullptr) {
            continue;
        }
        if (TerminateProcess(processHandle, 1) != 0) {
            killedCount += 1;
            Log::startup(QStringLiteral("Cleaned up stale duplicate vpn-parser.exe process: pid=%1")
                             .arg(entry.pid));
        }
        CloseHandle(processHandle);
    }

    if (killedCount > 0) {
        refreshNotificationArea();
    }
    return killedCount;
}

void showError(const QString &title, const QString &text)
{
    // MessageBoxW 的形参顺序是 (hwnd, text, caption, type)：Rust 传的也是 text 在前。
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(text.utf16()),
                reinterpret_cast<LPCWSTR>(title.utf16()), MB_OK | MB_ICONERROR);
}

#else // !Q_OS_WIN

bool isElevated()
{
    return false;
}

Status requestElevation(const QStringList &extraArgs)
{
    Q_UNUSED(extraArgs);
    return Status::failed(Messages::tunNotSupported());
}

void trimWorkingSet() {}

JobObjectGuard initProcessJob()
{
    return JobObjectGuard();
}

void ensureJobInitialized() {}

bool currentProcessInOurJob()
{
    return false;
}

void assignChildProcessId(quint32 pid)
{
    Q_UNUSED(pid);
}

QVector<ProcessEntry> snapshotProcesses()
{
    return {};
}

int killOrphanProxyCores()
{
    return 0;
}

int killDuplicateVpnParserInstances()
{
    return 0;
}

void showError(const QString &title, const QString &text)
{
    Log::error(QStringLiteral("startup error: %1: %2").arg(title, text));
}

#endif

QStringList orphanCoreImageNames()
{
    // Rust 在 kill_orphan_proxy_cores 里写死 ["mihomo.exe", "sing-box.exe", "xray.exe"]，
    // 这里用 Kernel::kindExecutableName 推导同一个集合（判据是不区分大小写的相等比较，
    // 名单顺序不影响结果）。
    QStringList names;
    for (const Kernel::Kind kind : { Kernel::Kind::Mihomo, Kernel::Kind::SingBox, Kernel::Kind::Xray }) {
        names << Kernel::kindExecutableName(kind);
    }
    return names;
}

bool isOrphanProxyCoreImage(const QString &imageName)
{
    const QString lowered = imageName.toLower();
    for (const QString &name : orphanCoreImageNames()) {
        if (lowered == name.toLower()) {
            return true;
        }
    }
    return false;
}

bool isDuplicateAppImage(const QString &imageName)
{
    // Rust 比较的是 "vpn-parser.exe"（产品可执行文件名，安装版与绿色版同名）。
    return imageName.toLower() == QLatin1String("vpn-parser.exe");
}

JobObjectGuard::~JobObjectGuard()
{
    close();
}

void JobObjectGuard::close()
{
#ifdef Q_OS_WIN
    if (handle_ != nullptr) {
        CloseHandle(handle_);
    }
#endif
    handle_ = nullptr;
}

}
