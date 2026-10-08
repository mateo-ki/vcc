#include "sysproxy/RegRunner.h"

#include <QProcess>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace SysProxy {

ProcessResult runHidden(const QString &program, const QStringList &arguments)
{
    ProcessResult result;
#ifdef Q_OS_WIN
    QProcess process;
    // 不弹控制台窗口：这套代码在启动路径上被调用。
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *createArguments) {
        createArguments->flags |= CREATE_NO_WINDOW;
    });
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return result;
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        result.started = true;
        return result;
    }
    result.started = true;
    result.finished = process.exitStatus() == QProcess::NormalExit;
    result.ok = result.finished && process.exitCode() == 0;
    result.exitCode = process.exitCode();
    result.stdOut = process.readAllStandardOutput();
    result.stdErr = process.readAllStandardError();
#else
    Q_UNUSED(program);
    Q_UNUSED(arguments);
#endif
    return result;
}

}
