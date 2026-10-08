#pragma once

#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <optional>

// parser/common.rs：订阅文本/URI 的字符串级工具。uri.rs 与 mod.rs 都依赖这里，
// 所以这一层不能反向依赖 parser 的其他头文件。
namespace Parser {
namespace Common {

/// 对应 Rust 的 `URI_RE`：自由文本里的分享链接（跨字段可见，Uri 侧不用）。
const QRegularExpression& uriRegex();
/// 对应 Rust 的 `LINE_URI_RE`：整行就是一条链接。
const QRegularExpression& lineUriRegex();

/// 展开订阅地址里的日期占位符：{Ymd} {ymd} {Y} {mm} {m} {dd} {d}（按 Rust 的替换顺序）。
QString expandUrlTemplate(const QString& text);

/// \r\n / \r 统一成 \n，再按固定顺序还原 HTML 实体。
QString normalizeSubscriptionText(const QString& input);
/// 单条 URI 的清洗：trim → 实体 → 去掉 markdown 的 <> 包裹 → 去掉尾部标点。
QString normalizeUri(const QString& uri);

/// 只在"猜整段文本是不是 base64"时用：长度、字符占比、可打印性都要过。
std::optional<QString> decodeMaybeBase64(const QString& input);
/// 解一段 base64（四种引擎按 Rust 的顺序轮询），解出合法 UTF-8 才算成功。
std::optional<QString> decodeBase64Str(const QString& input);

/// 从自由文本里抽链接：先按行整行匹配，一行都没有时再对全文做一次全局搜索。
QStringList extractUris(const QString& content);

/// percent_encoding::percent_decode_str(...).decode_utf8_lossy()。
QString percentDecode(const QString& text);
/// url crate 的 USERINFO 集：socks/http 直接取 username()，不再解码，所以要按这个集重编码。
QString encodeUserinfo(const QString& text);

/// 第一个 '?' 或 '#' 之前的部分。
QString stripQueryFragment(const QString& text);

/// (host, portText)；portText 可能是空串（"host:"）。
struct HostPort {
    QString host;
    QString port;
};
std::optional<HostPort> splitHostPort(const QString& text);

/// (userinfo, host, port)。
struct UserinfoHostPort {
    QString userinfo;
    QString host;
    int port = 0;
};
std::optional<UserinfoHostPort> splitUserinfoHostPort(const QString& text);

/// 键不解码、值 percent_decode；空键丢弃；重复键后者覆盖前者（Rust HashMap::insert）。
QMap<QString, QString> parseQuery(const QString& query);

/// Url::parse 失败时的手工拆解，对应 Rust 返回的
/// (scheme, userinfo, port, host, query, fragment) 六元组（注意 port 在 host 前面）。
struct Authority {
    QString scheme;
    QString userinfo;
    int port = 0;
    QString host;
    QString query;
    QString fragment;
};
std::optional<Authority> parseAuthority(const QString& uri);

/// 8-4-4-4-12 的十六进制 UUID（Rust 判的是 UTF-8 字节长度 36）。
bool looksLikeUuid(const QString& text);

/// 只按 UTF-8 字节数（Rust 的 str::len()），用于复刻 `len() < 16` / `len() != 36` 这类判断。
qsizetype utf8Length(const QString& text);
/// Rust `char::is_whitespace`：按 Unicode White_Space 属性判断。
bool isUnicodeSpace(QChar ch);
/// Rust 的 `to_ascii_lowercase`：只动 A-Z，非 ASCII 原样保留。
QString toAsciiLower(const QString& text);

} // namespace Common
} // namespace Parser
