#pragma once

#include <QString>

namespace Ids {

/// 小写、无花括号的 UUID v4，和 Rust `Uuid::new_v4().to_string()` 一致。
QString newUuidV4();
/// 对应 `Uuid::new_v4().simple()`：32 位十六进制、无连字符。
QString newUuidV4Simple();

}
