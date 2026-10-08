#pragma once

#include <QString>

#include <optional>

#include "core/JsonUtil.h"
#include "core/Result.h"

// Windows 系统代理（WinINet）。快照的生命周期独立于单个内核进程：首次启用时捕获，
// 节点切换和连接失败都不恢复，只有显式停止与退出才恢复。
namespace SysProxy {

constexpr const char *kInternetSettingsKey =
    R"(HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings)";

struct Snapshot {
    bool enabled = false;
    QString server;
    QString overrideList;

    bool operator==(const Snapshot &other) const
    {
        return enabled == other.enabled && server == other.server
            && overrideList == other.overrideList;
    }

    Json toJson() const;
};

Result<Snapshot> capture();
Status enable(const QString &server);
Status restore(const Snapshot &snapshot);

/// 读取 Windows 系统代理（v2rayN/Clash 等工具会写这里），返回 `http://host:port`。
/// 未启用、非 Windows 或读取失败时返回 null QString。阻塞式注册表查询，别在热点路径调用。
QString currentSystemProxyUrl();

/// ProxyServer 可能是 "127.0.0.1:10808"，也可能是 "http=...;https=...;ftp=..."。
/// 无可用形式时返回 null QString。
QString normalizeProxyServer(const QString &server);

/// 解析 `reg.exe query` 的输出。null QString = 没有这个值。
QString parseRegValue(const QString &output, const QString &name);
std::optional<bool> parseDword(const QString &value);

/// 注册表里某个值的原文；不存在时返回 null QString。阻塞式调用 reg.exe。
QString queryRegValue(const QString &name);
/// 删除一个值。值本来就不存在不算错误（恢复快照的正常情况）。
Status deleteRegValue(const QString &name);

/// 纯函数入口，测试用：reg.exe 的原始字节 → 可显示文本（GBK 乱码会被过滤掉）。
QString commandOutputText(const QByteArray &bytes);

}
