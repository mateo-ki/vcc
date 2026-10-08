#pragma once

#include <QString>

#include "core/JsonUtil.h"
#include "models/Node.h"

namespace Parser {

/// 代理端点的稳定身份（忽略显示名与来源）：同 server:port + 同凭据即同一节点，
/// 即使免费列表里名字不同。
QString nodeFingerprint(const Models::Node &node);

/// ss→shadowsocks、hy2→hysteria2、socks→socks5、https→http 等归一化。
QString normalizeProtocol(const QString &protocol);

/// 按协议挑凭据字段组成指纹的最后一位。
QString credentialToken(const QString &protocol, const Json &config);

}
