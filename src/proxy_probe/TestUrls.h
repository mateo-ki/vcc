#pragma once

#include <QString>

namespace ProxyProbe {

/// 真实连通性判定用的探针地址：走完整 HTTPS 请求，只有 204 才算通。
inline const QString kProxyConnectivityTestUrl =
    QStringLiteral("https://www.google.com/generate_204");

}
