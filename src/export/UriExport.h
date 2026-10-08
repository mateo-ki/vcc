#pragma once

#include <QString>
#include <QVector>

#include "models/Node.h"

// 对应 Rust src/export.rs 的 share URI 部分：一律从 config 重建，不直接回吐 raw，
// 因为大量免费订阅把 vless 标成 ss://，照抄 raw 会让 v2rayN 导不进/连不上。
namespace Export {

/// 一行一个分享链接；重复链接只保留第一次出现的顺序。
QVector<QString> nodesToShareUris(const QVector<Models::Node> &nodes);

/// 单个节点的分享链接。null QString = Rust 的 None（协议不认识且 raw 的 scheme
/// 也和协议不一致），调用方用 isNull() 判断。
QString nodeToShareUri(const Models::Node &node);

}
