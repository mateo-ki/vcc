#pragma once

#include <QString>

// 原生窗口/DWM 辅助函数，对应 Rust src/platform/tray.rs 里那几条不是托盘图标的部分。
// 它们被 main.rs / gui/mod.rs 直接调用（`platform::restore_main_window_native()` 等），
// 所以即使托盘换成别的实现，这几条也必须原样存在。
namespace Platform {

/// 把「收进托盘」的主窗口掀回可见 —— 只能这么干，没有第二条路。
///
/// 窗口 `Visible(false)` 之后 winit 收不到 WM_PAINT，`App::update()` 一帧都不跑
/// （实测探针：隐藏后每 250ms 发一次 `request_repaint()`，5 秒内只多跑了 1 帧，
/// 就是发出隐藏命令的那一帧），而托盘事件全是在 `update()` 里被取走的 —— 于是
/// 「显示主界面 / 停止代理 / 退出」三条都只能把事件塞进 channel，永远没人消费。
///
/// 只动"调用线程自己拥有的窗口"：跨线程 ShowWindow/SetForegroundWindow 要往目标线程
/// 发消息并等它处理，AttachThreadInput 还要等前台那个线程配合 —— 托盘线程就是这么卡死的
/// （2026-10-05 实测：左键一次之后该线程不再取任何消息，托盘左/右键全无反应）。
/// 对应 Rust `restore_main_window_native`。
void restoreMainWindowNative();

/// 托盘线程专用的掀窗口路径：只用 `ShowWindowAsync`，一次都不许用会等待的那几个
/// （SetForegroundWindow / AttachThreadInput / 裸 ShowWindow 都把这条线程卡死过，
/// 2026-10-08 实测：窗口在托盘里时左键的 ShowWindow 再也没回来）。
/// 挑"本进程里面积最大的不可见非托盘窗口"投递一次显示。对应 Rust `reveal_main_window_from_tray`。
void revealMainWindowFromTray();

/// 让 Win11 给窗口圆角。`with_decorations(false)` 之后系统那条属性不再作用
/// （它管的是非客户区），无边框窗口的四个角是方的，只能自己向 DWM 要。
/// Win10 上没有 33 号属性，返回错误就静默跳过 —— 那边无边框窗口本来也圆不了。
/// 对应 Rust `round_window_corners`。
void roundWindowCorners();

/// 清理 Windows 任务栏通知区域可能残留的幽灵图标（由于异常退出未调 NIM_DELETE 的旧进程图标）。
/// 通过向通知栏 ToolbarWindow32 发送微小鼠标移动消息，促使系统重绘并移除已死亡进程的图标。
/// 对应 Rust `refresh_notification_area`。
void refreshNotificationArea();

/// 纯判据：枚举窗口时这个类名的窗口要不要跳过。
/// Rust 的三条枚举（restore / reveal / round）都用 `class_name != TRAY_WINDOW_CLASS` 过滤。
bool skipsWindowClass(const QString &className);

/// 纯判据：reveal 时这个窗口算"本来就看得见，别碰"吗（可见且非最小化）。
/// 两个入参把 Win32 的返回值翻成 bool，方便脱离 user32 测试。
bool windowIsAlreadyShown(bool visible, bool iconic);

}
