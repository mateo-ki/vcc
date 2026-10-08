#pragma once

#include <optional>

#include "core/Result.h"
#include "sysproxy/SystemProxy.h"

namespace Proxy {

/// 启用系统代理并返回"停止时要还原成什么"。terminalProxyPort 非空时同时写终端代理
/// 环境变量（Git / curl / npm / pip 只认环境变量，不写它们就是"系统代理开了，
/// 终端里还是连不上"）。
Result<SysProxy::Snapshot> activateSystemProxy(const QString &server,
                                               std::optional<quint16> terminalProxyPort);

/// 还原系统代理；clearTerminalProxy 为真时顺手清掉我们自己写过的终端变量。
Status restoreSystemProxy(const SysProxy::Snapshot &snapshot, bool clearTerminalProxy);

}
