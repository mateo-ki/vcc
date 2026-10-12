#pragma once

#include <QString>
#include <QStringList>

namespace ProxyProbe {

/// 这个头为什么住在 proxy/ 而不是 proxy_probe/：vccProbe 本来就依赖 vccProxy，
/// 而 MihomoConfig / ProxyManager 也要用这几个地址 —— 放对面会让 vccProxy 反向依赖
/// vccProbe，两个目标缠成链接环。Rust 侧同一个地址也出现在 `proxy/mod.rs` 里。
/// 真实连通性判定用的探针地址：走完整 HTTPS 请求，只有 204 才算通。
inline const QString kProxyConnectivityTestUrl =
    QStringLiteral("https://www.google.com/generate_204");

/// 把设置里那段"每行一个 URL"的基准站点文本切成列表：去空白行、去首尾空格、按出现
/// 顺序去重。空列表 = 用户没填 = 调用方退回内置的那一条。
inline QStringList probeBaselineUrls(const QString &settingsText)
{
    QStringList out;
    const QStringList lines = settingsText.split(QChar(10));
    for (const QString &raw : lines) {
        const QString url = raw.trimmed();
        if (!url.isEmpty() && !out.contains(url)) {
            out.append(url);
        }
    }
    return out;
}

}
