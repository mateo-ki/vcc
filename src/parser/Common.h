#pragma once

#include <QRegularExpression>
#include <QString>

#include "models/Node.h"

// parser/common.rs 的对应物：节点名清洗、质量打分、去重择优、国家推断。
// Rust 侧被 parser 之外（db / export / extractor）复用的公共项优先放这里，避免反向依赖 parser。
namespace Parser {

/// Rust 的 `Option<(String, String)>`（code, name），空字符串表示 None。
struct CountryGuess {
    QString code;
    QString name;
};

namespace Common {

/// 对应 Rust `trim_matches(|c| re.is_match(c.to_string()))`：反复剥掉开头/结尾被正则命中的字符。
/// 与 Qt 的 `trimmed()` 不同，它由正则决定“什么算边界”，所以必须逐字符模拟 Rust 的 searcher。
QString trimMatched(const QString& text, const QRegularExpression& re);
QString trimMatchedHead(const QString& text, const QRegularExpression& re);
QString trimMatchedTail(const QString& text, const QRegularExpression& re);

/// 对应 Rust `RE_*.replace_all(haystack, replacement)`；`$1`/`$10` 按 Rust 的展开规则解析。
QString replaceAll(const QRegularExpression& re, const QString& haystack, const QString& replacement);

/// `RE_.*?ZWJ`：与 Rust 一样返回“第一个命中的 emoji 正则”，供 URI 提取截断用。
int firstEmojiMatchLength(const QString& line);

QString cleanNodeName(const QString& name);
int nameQuality(const QString& name);
/// 去重择优：质量优先，同质量比名称字典序（Rust `(b.name.cmp(&a.name), b.id.cmp(&a.id))`）。
bool preferNode(const Models::Node& a, const Models::Node& b);
/// 覆盖节点名称：先剥前缀/后缀噪声，再走 clean_node_name，最后重算 country。
void applyNodeName(Models::Node& node, const QString& rawName);

QString md5Hex(const QString& value);
int defaultPortFor(const QString& protocol);
CountryGuess guessCountry(const QString& name);

// ---- 正则（与 Rust 的 lazy_static 一一对应；CJK 一律用 \x{…} 转义，避免源码里的字面量丢字符）----
const QRegularExpression& reRegionIndicator();
const QRegularExpression& reEmojiMiscSymbols();
const QRegularExpression& reEmojiPictographs();
const QRegularExpression& reEmojiTransport();
const QRegularExpression& reZwj();
const QRegularExpression& reVariationSelectors();
const QRegularExpression& reSpaces();
const QRegularExpression& reBrackets();
const QRegularExpression& rePromo();
const QRegularExpression& reTraffic();
const QRegularExpression& reTgHandle();
const QRegularExpression& reTgAdLink();
const QRegularExpression& reDomainHeadPrefix();
const QRegularExpression& reTrailingMeta();
const QRegularExpression& reNameTailMeta();

} // namespace Common
} // namespace Parser
