#pragma once

// Windows 平台层的总入口，对应 Rust 的 `src/platform/mod.rs`：
// mod.rs 除了自己的函数外还 `pub use tray::{refresh_notification_area,
// restore_main_window_native, round_window_corners, TrayEvent, TrayHandle}`，
// 所以调用方只需要 include 这一个头就能拿到全部平台能力。
#include "platform/Firewall.h"
#include "platform/ProcessControl.h"
#include "platform/SingleInstance.h"
#include "platform/Tray.h"
#include "platform/WindowHelpers.h"
