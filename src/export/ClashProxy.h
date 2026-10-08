#pragma once

#include <optional>

#include <QVector>

#include "core/JsonUtil.h"
#include "models/Node.h"

// 对应 Rust 的 src/export.rs：把解析好的节点变成 Clash/Mihomo 认识的代理对象。
// type 名（ss/vmess/vless/trojan/hysteria2/hysteria/socks5/http/tuic/anytls/
// wireguard/ssr）与字段名都是客户端按字面匹配的，必须照抄；连"删哪些键"也照抄，
// 因为订阅来源的 config 里带着 v2rayN 的原始字段（add/id/aid/net/scy/ps/v）。
namespace Export {

/// 生成 Clash 代理列表。重名按 `base`、`base-2`、`base-3`… 递增，
/// 名字净化后为空则退回 `node-{序号}`（Rust 的 unique_name）。
QVector<Json> nodesToClashProxies(const QVector<Models::Node> &nodes);

/// 单个节点转 Clash 代理对象；协议不认识时返回 nullopt（Rust 的 Option<Value>）。
std::optional<Json> nodeToClashProxy(const Models::Node &node);

}
