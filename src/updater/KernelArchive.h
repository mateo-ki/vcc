#pragma once

#include <QByteArray>
#include <QStringList>

#include <optional>

#include "core/Result.h"
#include "kernel/KernelTypes.h"

namespace Updater {

/// 对应 Rust `zip`  crate 的 `entry.enclosed_name()`：条目名里出现绝对路径
/// （根目录 / 盘符前缀）或 `..` 就整条丢弃，只有 `.` 分量则视为空名同样丢弃。
/// 返回净化后的相对分量列表（最后一项就是 Rust 那边 `path.file_name()` 拿到的名字）。
/// 纯函数，单测直接打它。
std::optional<QStringList> enclosedNameComponents(const QString &entryName);

/// 对应 Rust 私有 `extract_executable`：从内核 ZIP 的字节里取出可执行文件写到 `target`。
/// Xray 还额外取 `geoip.dat` / `geosite.dat` 到 `target` 的同级目录（正式内核和暂存内核
/// 都在内核自己的目录里，所以 geo 数据跟着落在同一个目录）。
/// 找不到可执行文件时报 "ZIP 中未找到 <exe>"，与 Rust 一致。
Status extractExecutable(const QByteArray &archive, Kernel::Kind kind, const QString &target);

}
