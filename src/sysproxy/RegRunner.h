#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace SysProxy {

/// 一个隐藏控制台窗口跑完的外部进程（reg.exe）。
struct ProcessResult {
    bool started = false;
    bool ok = false;
    /// 子进程正常退出（不是被杀/超时）。对应 Rust `status.code()` 的 Option。
    bool finished = false;
    int exitCode = -1;
    QByteArray stdOut;
    QByteArray stdErr;
};

ProcessResult runHidden(const QString &program, const QStringList &arguments);

}
