#pragma once

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "models/Node.h"
#include "parser/Common.h"

// parser/mod.rs：订阅文本 → 节点列表，以及节点名/国家/去重这些跨模块公共项。
namespace Parser {

/// 对应 `pub use common::{...}`，让调用方像 Rust 一样只 use parser。
using Common::decodeMaybeBase64;
using Common::expandUrlTemplate;
using Common::extractUris;
using Common::normalizeSubscriptionText;

/// 自由文本订阅 → 节点：支持明文链接、base64、Clash YAML 以及混排文本。
QVector<Models::Node> parseSubscription(const QString& content, const QString& sourceId);

/// 看起来像 Clash YAML（Rust 是私有 fn，这里为了被测出来放到头文件）。
bool looksLikeClash(const QString& content);

/// 名称质量分（Rust 私有 fn，同上）。
int nameQuality(const QString& name);
/// 去重时 a 是否比 b 更值得保留（Rust 私有 fn，同上）。
bool preferNode(const Models::Node& a, const Models::Node& b);

/// 按指纹去重，保留更优的那条。Rust 用 HashMap（顺序随机），这里用指纹升序保证可复现。
QVector<Models::Node> dedupNodes(const QVector<Models::Node>& nodes);

/// 组装节点：空名字退化成 server:port，country 由 guessCountry(name) 得出。
Models::Node makeNode(const QString& protocol, const QString& name, const QString& server,
                      int port, const QString& raw, const Json& config, const QString& sourceId);

/// 从名称推断国家码（"HK"/"US"…），null QString = Rust 的 None。
QString guessCountry(const QString& name);

/// 清掉节点名里的广告/推广/流量信息，并按国家码补一个中文前缀。
/// countryHint 用 null QString 表示 Rust 的 None。
QString cleanNodeName(const QString& name, const QString& countryHint, const QString& server,
                      int port);

/// 空 config 的辅助函数（Rust 标了 #[allow(dead_code)]）。
Json emptyConfig();

} // namespace Parser
