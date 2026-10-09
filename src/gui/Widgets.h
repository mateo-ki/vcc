#pragma once

#include <QString>

#include <cstdint>

namespace Gui {

/// 对应 Rust `gui::widgets::format_bytes`：GB / MB / KB 三档除完之后按位数收尾。
QString formatBytes(quint64 bytes);
QString formatSpeed(quint64 bytesPerSecond);

}

