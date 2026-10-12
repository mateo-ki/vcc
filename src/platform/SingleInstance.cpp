#include "platform/SingleInstance.h"

#include <QThread>

#include "core/Log.h"
#include "platform/ProcessControl.h"
#include "platform/Tray.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Platform {

namespace {

// VCC 的独立命名互斥量，带 Local\ 会话前缀，不与原版 vpn-parser 产生互斥冲突。
const char *const kMutexName = "Local\\VCC-7F61E55C-DB54-49C6-AB9E-986AEBFDD56B";

}

QString singleInstanceMutexName()
{
    return QString::fromLatin1(kMutexName);
}

QVector<HandoffPollStep> handoffPollPlan()
{
    QVector<HandoffPollStep> plan;
    plan.reserve(kHandoffMaxAttempts);
    for (int attempt = 1; attempt <= kHandoffMaxAttempts; ++attempt) {
        plan.append(HandoffPollStep { attempt, kHandoffAttemptDelayMs });
    }
    return plan;
}

#ifdef Q_OS_WIN

SingleInstanceGuard::~SingleInstanceGuard()
{
    close();
}

void SingleInstanceGuard::close()
{
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

Result<std::optional<SingleInstanceGuard>> acquireSingleInstanceNamed(const QString &name)
{
    const HANDLE handle = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(name.utf16()));
    if (handle == nullptr) {
        return Result<std::optional<SingleInstanceGuard>>::withError(
            QStringLiteral("CreateMutexW failed: %1").arg(static_cast<quint32>(GetLastError())));
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已经有实例持着这把锁：句柄关掉（CloseHandle 就是释放），交给调用方去唤醒对方。
        CloseHandle(handle);
        return Result<std::optional<SingleInstanceGuard>>::withValue(std::nullopt);
    }
    return Result<std::optional<SingleInstanceGuard>>::withValue(SingleInstanceGuard(handle));
}

Result<std::optional<SingleInstanceGuard>> acquireSingleInstance()
{
    Result<std::optional<SingleInstanceGuard>> result =
        acquireSingleInstanceNamed(singleInstanceMutexName());
    if (result.isOk() && !result.value().has_value()) {
        notifyExistingInstance();
    }
    return result;
}

Result<std::optional<SingleInstanceGuard>> acquireSingleInstanceForHandoff()
{
    // 提权交接启动：父进程正在准备退出并释放互斥量。
    // 轮询等待最多 3 秒，拿到互斥量并接管，确保系统中永远由唯一的活动实例持有互斥量。
    std::optional<SingleInstanceGuard> guard;
    for (const HandoffPollStep &step : handoffPollPlan()) {
        // 用不带唤醒的那条：这一轮等的是"父进程正在退出并释放锁"，去唤醒/掀它的窗口
        // 只会把一个马上要消失的窗口重新拉到前台（白框的另一半来源）。
        Result<std::optional<SingleInstanceGuard>> attempt =
            acquireSingleInstanceNamed(singleInstanceMutexName());
        if (attempt.isErr()) {
            return attempt;
        }
        if (attempt.value().has_value()) {
            Log::startup(QStringLiteral("Acquired single instance mutex on attempt %1.")
                             .arg(step.attempt));
            guard = std::move(*attempt.value());
            break;
        }
        QThread::msleep(static_cast<unsigned long>(step.sleepMsAfterFailure));
    }
    if (!guard.has_value()) {
        Log::startup(
            QStringLiteral("Parent process still holds mutex after 3s; killing duplicate parent/zombie instances..."));
        const int killed = killDuplicateVpnParserInstances();
        Log::startup(QStringLiteral("Killed %1 duplicate instances; retrying mutex acquisition...")
                         .arg(killed));
        return acquireSingleInstance();
    }
    return Result<std::optional<SingleInstanceGuard>>::withValue(std::move(guard));
}

#else // !Q_OS_WIN

SingleInstanceGuard::~SingleInstanceGuard()
{
    close();
}

void SingleInstanceGuard::close()
{
    handle_ = nullptr;
}

Result<std::optional<SingleInstanceGuard>> acquireSingleInstanceNamed(const QString &name)
{
    Q_UNUSED(name);
    return Result<std::optional<SingleInstanceGuard>>::withValue(SingleInstanceGuard());
}

Result<std::optional<SingleInstanceGuard>> acquireSingleInstance()
{
    return acquireSingleInstanceNamed(singleInstanceMutexName());
}

Result<std::optional<SingleInstanceGuard>> acquireSingleInstanceForHandoff()
{
    return acquireSingleInstance();
}

#endif

}
