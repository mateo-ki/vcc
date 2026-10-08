#include "proxy/Diagnostics.h"

#include <QRegularExpression>

#include "core/Log.h"

namespace Proxy {

QString lastNonemptyLine(const QString &text)
{
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]")));
    for (int index = lines.size() - 1; index >= 0; --index) {
        const QString trimmed = lines.at(index).trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return QStringLiteral("未知错误");
}

QString stripAnsi(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) != QLatin1Char('\x1b')) {
            out.append(text.at(index));
            continue;
        }
        // ESC [ ... 字母：整段丢掉；ESC 后面不是 '[' 时只丢掉 ESC 本身。
        if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('[')) {
            index += 2;
            while (index < text.size()) {
                const QChar character = text.at(index);
                ++index;
                if (character.isLetter()) {
                    break;
                }
            }
            --index;
        }
    }
    return out;
}

QString formatKernelDiagnostic(const QString &raw)
{
    const QString cleaned = stripAnsi(raw);
    const QString lower = cleaned.toLower();
    if (lower.contains(QStringLiteral("forcibly closed"))
        || lower.contains(QStringLiteral("connection reset"))) {
        return QStringLiteral("远程服务器强制重置/关闭连接 (目标节点已不可用)");
    }
    if (lower.contains(QStringLiteral("timeout")) || lower.contains(QStringLiteral("deadline exceeded"))) {
        return QStringLiteral("连接远程服务器超时 (节点无响应)");
    }
    if (lower.contains(QStringLiteral("connection refused"))) {
        return QStringLiteral("远程服务器拒绝连接 (端口未开放或节点已失效)");
    }
    if (lower.contains(QStringLiteral("certificate"))
        || lower.contains(QStringLiteral("tls: handshake"))) {
        return QStringLiteral("TLS 证书验证或握手失败");
    }
    if (lower.contains(QStringLiteral("websocket"))) {
        return QStringLiteral("WebSocket 握手失败 (协议或路径配置有误)");
    }
    if (lower.contains(QStringLiteral("start initial configuration"))
        || lower.contains(QStringLiteral("in progress"))) {
        return QStringLiteral("内核初次启动规则加载中，正在自动建立连接");
    }
    if (lower.contains(QStringLiteral("outbound connection to"))) {
        return QStringLiteral("节点连接超时或无响应 (目标服务器无法连通)");
    }
    const int marker = cleaned.indexOf(QStringLiteral("msg=\""));
    if (marker >= 0) {
        const int start = marker + 5;
        const int end = cleaned.indexOf(QLatin1Char('"'), start);
        if (end > start) {
            return cleaned.mid(start, end - start);
        }
    }
    return lastNonemptyLine(cleaned);
}

SysProxy::Snapshot sanitizeSelfReferential(const SysProxy::Snapshot &snapshot,
                                           const QString &server)
{
    const bool pointsAtUs = snapshot.enabled && !snapshot.server.isNull()
        && snapshot.server.contains(server.trimmed());
    if (!pointsAtUs) {
        return snapshot;
    }
    Log::warn(QStringLiteral(
        "系统代理已指向我方端口（上次未正常还原），还原目标改为关闭系统代理: %1").arg(server));
    SysProxy::Snapshot cleared;
    cleared.enabled = false;
    cleared.server = QString();
    cleared.overrideList = QString();
    return cleared;
}

}
