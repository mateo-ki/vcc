#pragma once

#include <QString>

#include <optional>

#include "models/Node.h"

// parser/uri.rs：单条分享链接 → Node。
namespace Parser {

/// 对应 Rust `url::Url` 在本模块用到的那几个访问器。
/// Rust 侧只做 `Url::parse(uri)` 然后读 scheme/username/password/host_str/port/query/fragment，
/// 这里的字段全部是"序列化后的原文片段"（除 host 外不再解码），与 url crate 行为一致：
/// url crate 会按 USERINFO/QUERY/FRAGMENT 集重编码，但这些集合都不含 '%'，
/// 所以 uri.rs 里 percent_decode(X()) 等价于对原文片段直接 percent_decode。
struct Url {
    QString scheme;                    ///< 已按 Rust 的规则小写化
    QString username;                  ///< 未解码
    QString password;                  ///< null = Rust 的 None
    QString host;                      ///< 空 = Rust host_str() 的 None
    int port = -1;                     ///< -1 = Rust port() 的 None
    QString query;                     ///< '?' 之后原文，无 '?' 时为 null
    QString fragment;                  ///< '#' 之后原文，无 '#' 时为 null

    bool hasQuery() const { return !query.isNull(); }
    bool hasFragment() const { return !fragment.isNull(); }
};

/// 对应 `Url::parse`：不接受相对 URL；scheme 必须是 [A-Za-z][A-Za-z0-9+.-]* 且带 "//"；
/// 端口必须是纯数字且不超过 65535；host 含 WHATWG 禁止字符则整体报错（Rust 报错后
/// 调用方会退回 parse_authority）。
std::optional<Url> parseUrl(const QString& input);

namespace Uri {

/// 对应 Rust `parse_uri`：sourceId 用 null QString 表示 Rust 的 None。
std::optional<Models::Node> parseUri(const QString& uri, const QString& sourceId);

} // namespace Uri
} // namespace Parser
