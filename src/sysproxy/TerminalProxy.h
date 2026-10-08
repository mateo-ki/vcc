#pragma once

#include <QString>
#include <QStringList>

#include "core/Result.h"

/// 终端代理环境变量（`HKCU\Environment`）。
///
/// Windows 上只有走 WinINet/WinHTTP 的程序才读"系统代理"，Git Bash、curl、git、
/// npm、pip 这些只认 `HTTP_PROXY` / `HTTPS_PROXY` 环境变量 —— 这就是"系统代理开了，
/// 终端里 git clone 还是超时"的原因。这里只写用户级变量，不需要管理员。
namespace TerminalProxy {

extern const QStringList kProxyVars;
extern const QString kNoProxyVar;
/// 本地与内网一律不走代理，否则局域网打印/开发服务器会被绕进隧道。
QString noProxyValue();

/// 我们自己写进去的值长这样，用来区分"用户原有的代理设置"。
bool isOurs(const QString &value);

struct Report {
    QStringList written;
    QStringList removed;
    /// 用户自己设置的、我们没有动的变量。
    QStringList untouched;

    QString summary() const;
};

Result<Report> enable(quint16 port);
Result<Report> disable();

/// 纯函数入口，测试用。
QString readEnvValue(const QString &name);

}
