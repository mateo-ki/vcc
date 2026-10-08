#pragma once

#include <QStringList>

namespace Geo {

/// mihomo 下载 geo 库用的镜像域名。它们必须钉直连：走代理会被丢进 MATCH,PROXY，
/// 实测 10 秒 TLS 握手超时（同一 URL 本机直连 1.4 秒）。
inline const QStringList kMirrorDirectHosts = {QStringLiteral("gh-proxy.com"),
                                               QStringLiteral("ghfast.top")};

}
