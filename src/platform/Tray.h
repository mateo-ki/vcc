#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "core/Result.h"

// 托盘：Shell_NotifyIconW + 一条自己的隐藏消息窗口 + wndproc，对应 Rust src/platform/tray.rs。
//
// 为什么不用 QSystemTrayIcon：三条可观察行为逼着走原生 Win32。
// 1. 跨进程唤醒握手。第二个实例（可能是这台机器上还装着的 Rust 建的 vpn-parser.exe）是
//    靠 `FindWindowW("VPNParserTrayWindowClass")` + `PostMessageW(WM_USER + 101)` 加
//    `RegisterWindowMessageW("VPN_PARSER_SHOW_WINDOW_MSG")` 广播来叫醒本程序的。QSystemTrayIcon
//    的内部窗口类名我们改不了，别的进程也就找不到、叫不醒。
// 2. 右键菜单必须在托盘线程里用 TrackPopupMenu(TPM_RETURNCMD) 同步弹：主窗口收进托盘后
//    UI 线程不再跑帧，QMenu 那套要等 UI 线程事件循环，菜单永远显示不出来。
// 3. 左键的「把窗口掀回来」要由托盘线程自己 ShowWindowAsync（reveal_main_window_from_tray
//    的注释记录了 2026-10-05 / 2026-10-08 两次卡死），Qt 的信号槽只会把这件事推回 UI 线程。
namespace Platform {

/// Rust `TRAY_WINDOW_CLASS`：类名是跨进程唤醒的寻址方式，不能改。
QString trayWindowClass();
/// Rust `MAIN_WINDOW_TITLE`。
QString mainWindowTitle();
/// Rust `WAKEUP_MSG_NAME`（不带结尾 NUL，广播注册时再加）。
QString wakeupMessageName();

/// Win32 消息号（值取自 winuser.h，.cpp 里用 static_assert 校验过）。
quint32 wakeupInstanceId();      // WM_USER + 101
quint32 trayCallbackMessage();   // WM_USER + 100
quint32 trayIconId();            // 1001
quint32 cmdShow();               // 2001
quint32 cmdToggleProxy();        // 2002
quint32 cmdExit();               // 2003

enum class TrayEvent {
    ShowWindow,
    ToggleProxy,
    Quit,
};

struct TrayMenuItem {
    quint32 id = 0;
    QString text;
    bool separator = false;

    bool operator==(const TrayMenuItem &other) const
    {
        return id == other.id && text == other.text && separator == other.separator;
    }
};

/// 右键菜单的内容：显示主界面 / 启动代理|停止代理 / 分隔线 / 退出程序。
/// 标签与顺序照抄 Rust，中文由 tools/gen-platform-strings.mjs 从 Rust 源生成。
QVector<TrayMenuItem> menuItemsFor(bool proxyRunning);

/// 菜单命令号 → 事件（Rust 里是 match cmd，落到 default 分支就没有事件）。
std::optional<TrayEvent> trayEventForCommand(quint32 command);

/// 图标回调消息里，哪些鼠标事件算「左键选中」（含 NIN_SELECT / NIN_KEYSELECT）。
/// Rust 那一串 `event == WM_LBUTTONUP || ... || event == (WM_USER + 0) || event == (WM_USER + 1)`。
bool isTraySelectEvent(quint32 event);
/// 哪些事件算「弹上下文菜单」：只认抬起和 WM_CONTEXTMENU（以前连 WM_RBUTTONDOWN 也弹，
/// 于是按下弹的那层被抬起这一下顶掉，看起来就是"点了没反应"）。
bool isTrayContextEvent(quint32 event);

/// NOTIFYICONDATAW.szTip 的可用字符数：数组长度减一（Rust 用的是 `szTip.len() - 1`，
/// 现代 SDK 里 szTip 是 WCHAR[128]，所以这个值是 127，不是老文档里的 63）。
int trayTipCapacity();
/// 把提示文本截到 szTip 装得下的长度。
QString truncateTrayTip(const QString &text, int capacity = trayTipCapacity());
/// 托盘默认提示（Rust 写死的那条中文）。
QString defaultTrayTooltip();

/// 第二个实例发现锁被占时走这条：给已存在的实例发唤醒消息，再把窗口掀起来。
void notifyExistingInstance();

/// 托盘句柄：UI 侧（对应 Rust 的 main.rs / gui/mod.rs）通过它读事件、改状态。
///
/// 与 Rust 的差别只有一处：Rust 的 `TrayHandle::new()` 立刻起线程并显示图标，这里把
/// "起线程"挪到 `start()`，因为构建/测试期间绝不允许往通知区域塞图标。
class TrayHandle {
public:
    /// 内部状态（事件队列 / 更新队列 / 线程），定义在 Tray.cpp。
    /// 之所以是 public：托盘线程的 wndproc 与 updater 线程都要碰它，
    /// 那些是命名空间里的自由函数（Win32 回调不能是有捕获的 lambda）。
    struct Private;

    TrayHandle();
    ~TrayHandle();
    TrayHandle(const TrayHandle &) = delete;
    TrayHandle &operator=(const TrayHandle &) = delete;

    /// 启动托盘线程（建窗口 + NIM_ADD）。重复调用无效果。
    void start();
    bool isStarted() const;

    /// 对应 Rust `set_waker`：每帧注册一次，用来把 UI 线程叫醒来取事件。
    void setWaker(std::function<void()> waker);
    /// 对应 Rust `try_recv_event`：非阻塞取一条事件，没有就空。
    std::optional<TrayEvent> tryRecvEvent();
    void setProxyRunning(bool running);
    void setTooltip(const QString &tooltip);
    /// 对应 Rust `shutdown`：删图标、销毁托盘窗口、结束消息循环。析构时自动调用。
    void shutdown();

private:
    struct Private;
    std::shared_ptr<Private> d_;
};

}
