#pragma once

#include <QString>

#include "sysproxy/SystemProxy.h"

namespace Proxy {

/// 取最后一行非空文本（内核日志的报错总在最后）。
QString lastNonemptyLine(const QString &text);
/// 去掉 ANSI 转义序列：内核日志被重定向到文件时仍会带颜色码。
QString stripAnsi(const QString &text);
/// 把内核原始日志翻译成人能看懂的一句话。
QString formatKernelDiagnostic(const QString &raw);

/// 上一次会话被强杀后，注册表里留下的就是我们自己的端口。把它当成"要还原的原值"，
/// 停止代理时就会把系统代理指回已经死掉的端口（浏览器直接断网），
/// 所以这种情况的还原目标改为"关闭系统代理"。
SysProxy::Snapshot sanitizeSelfReferential(const SysProxy::Snapshot &snapshot,
                                            const QString &server);

}
