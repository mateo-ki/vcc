#pragma once

#include <QByteArray>
#include <QString>

#include "core/Result.h"

namespace Crypto {

/// 对应 Rust `format!("{:x}", Sha256::digest(bytes))`。
QString sha256Hex(const QByteArray &bytes);
/// 大文件（内核 ZIP）按块读，避免把整包塞进内存。
Result<QString> sha256HexOfFile(const QString &path);

}
