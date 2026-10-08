#pragma once

#include <QByteArray>
#include <QString>

#include "core/Result.h"

// 语义逐字对齐 Rust base64 0.22：STANDARD 引擎要求规范填充、拒绝残余比特，
// URL_SAFE_NO_PAD 引擎不允许任何填充。调用点用哪个引擎由 Rust 侧决定，
// 解不开就意味着"这不是 base64 正文"，进而走明文链接提取分支。
namespace Encoding {

enum class Engine {
    Standard,       // A-Za-z0-9+/ + '=' 规范填充
    UrlSafeNoPad,   // A-Za-z0-9-_ 无填充
};

QByteArray encode(const QByteArray &input, Engine engine);

/// 宽松解码：两种字母表都接受、填充可有可无。仅用于"猜正文是不是 base64"的分支。
Result<QByteArray> decodeLenient(const QByteArray &input);
/// 严格解码，等价于 Rust 的 STANDARD / URL_SAFE_NO_PAD。
Result<QByteArray> decode(const QByteArray &input, Engine engine);

QString toHexLower(const QByteArray &bytes);
Result<QByteArray> fromHex(const QString &hex);

}
