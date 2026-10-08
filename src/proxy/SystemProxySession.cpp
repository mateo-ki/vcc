#include "proxy/SystemProxySession.h"

#include "core/Log.h"
#include "proxy/Diagnostics.h"
#include "sysproxy/TerminalProxy.h"

namespace Proxy {

Result<SysProxy::Snapshot> activateSystemProxy(const QString &server,
                                              std::optional<quint16> terminalProxyPort)
{
    const Result<SysProxy::Snapshot> captured = SysProxy::capture();
    if (captured.isErr()) {
        return Result<SysProxy::Snapshot>::withError(captured.error());
    }
    const SysProxy::Snapshot snapshot = sanitizeSelfReferential(captured.value(), server);
    const Status enabled = SysProxy::enable(server);
    if (enabled.isErr()) {
        // 开失败就立刻按原值还原，不留半套系统状态。
        const Status rolledBack = SysProxy::restore(snapshot);
        if (rolledBack.isErr()) {
            Log::warn(QStringLiteral("回滚系统代理也失败: %1").arg(rolledBack.error()));
        }
        return Result<SysProxy::Snapshot>::withError(enabled.error());
    }
    if (terminalProxyPort.has_value()) {
        const Result<TerminalProxy::Report> report = TerminalProxy::enable(*terminalProxyPort);
        if (report.isOk()) {
            Log::info(QStringLiteral("终端代理已启用: %1").arg(report.value().summary()));
        } else {
            Log::warn(QStringLiteral("终端代理启用失败: %1").arg(report.error()));
        }
    }
    return Result<SysProxy::Snapshot>::withValue(snapshot);
}

Status restoreSystemProxy(const SysProxy::Snapshot &snapshot, bool clearTerminalProxy)
{
    const Status restored = SysProxy::restore(snapshot);
    if (restored.isErr()) {
        return restored;
    }
    if (clearTerminalProxy) {
        const Result<TerminalProxy::Report> report = TerminalProxy::disable();
        if (report.isOk()) {
            Log::info(QStringLiteral("终端代理已清除: %1").arg(report.value().summary()));
        } else {
            Log::warn(QStringLiteral("终端代理清除失败: %1").arg(report.error()));
        }
    }
    return Status::succeeded();
}

}
