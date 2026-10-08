#include "platform/WindowHelpers.h"

#include <cstring>

#include "core/Log.h"
#include "platform/Messages.h"
#include "platform/Tray.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#endif

namespace Platform {

namespace {

#ifdef Q_OS_WIN
/// GetClassNameW 的结果转成 QString，等价于 Rust 的 `String::from_utf16_lossy(&buf[..len])`。
QString windowClassName(HWND hwnd)
{
    WCHAR buffer[64] = {};
    const int length = GetClassNameW(hwnd, buffer,
                                     static_cast<int>(sizeof(buffer) / sizeof(WCHAR)));
    if (length <= 0) {
        return QString();
    }
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer), length);
}

QString hwndText(HWND hwnd)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(hwnd)), 0, 16);
}

/// reveal 用：在"本进程的、非托盘的、当前不可见（或最小化）"的窗口里挑面积最大的那个。
struct RevealPick {
    HWND best = nullptr;
    LONG bestArea = 0;
};
#endif

} // namespace

bool skipsWindowClass(const QString &className)
{
    // Rust 的三条枚举都用 `class_name != TRAY_WINDOW_CLASS` 过滤掉托盘那条隐藏窗口，
    // 否则会去 ShowWindow 一个永远不该显示的窗口。
    return className != trayWindowClass();
}

bool windowIsAlreadyShown(bool visible, bool iconic)
{
    // `IsWindowVisible(hwnd) != 0 && IsIconic(hwnd) == 0` → "本来就看得见，别碰"。
    return visible && !iconic;
}

#ifdef Q_OS_WIN

namespace {

BOOL CALLBACK restoreEnumProc(HWND hwnd, LPARAM lparam)
{
    const auto currentPid = static_cast<DWORD>(lparam);
    DWORD pid = 0;
    const DWORD windowThread = GetWindowThreadProcessId(hwnd, &pid);
    // 只动"调用线程自己拥有的窗口"：跨线程 ShowWindow/SetForegroundWindow 要往目标线程
    // 发消息并等它处理，AttachThreadInput 还要等前台那个线程配合 —— 托盘线程就是这么卡死的
    // （2026-10-05 实测：左键一次之后该线程不再取任何消息，托盘左/右键全无反应）。
    if (pid == currentPid && windowThread == GetCurrentThreadId()) {
        const QString className = windowClassName(hwnd);
        if (skipsWindowClass(className)) {
            Log::startup(QStringLiteral("restore_main_window_native restoring hwnd=%1, class=%2")
                             .arg(hwndText(hwnd), className));
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            BringWindowToTop(hwnd);
            SetForegroundWindow(hwnd);
            // 置顶再取消：穿透其它窗口的层级覆盖。
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            PostMessageW(hwnd, WM_PAINT, 0, 0);
        }
    }
    return TRUE;
}

BOOL CALLBACK revealEnumProc(HWND hwnd, LPARAM lparam)
{
    const DWORD currentPid = GetCurrentProcessId();
    DWORD pid = 0;
    if (GetWindowThreadProcessId(hwnd, &pid) == 0 || pid != currentPid) {
        return TRUE;
    }
    if (!skipsWindowClass(windowClassName(hwnd))) {
        return TRUE;
    }
    if (windowIsAlreadyShown(IsWindowVisible(hwnd) != 0, IsIconic(hwnd) != 0)) {
        return TRUE; // 本来就看得见，别碰
    }
    RECT rect {};
    if (GetWindowRect(hwnd, &rect) == 0) {
        return TRUE;
    }
    const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
    auto *pick = reinterpret_cast<RevealPick *>(lparam);
    if (area > pick->bestArea) {
        pick->bestArea = area;
        pick->best = hwnd;
    }
    return TRUE;
}

BOOL CALLBACK roundEnumProc(HWND hwnd, LPARAM lparam)
{
    const auto currentPid = static_cast<DWORD>(lparam);
    DWORD pid = 0;
    const DWORD windowThread = GetWindowThreadProcessId(hwnd, &pid);
    if (pid == currentPid && windowThread == GetCurrentThreadId()) {
        const QString className = windowClassName(hwnd);
        if (skipsWindowClass(className)) {
            const DWORD preference = DWMWCP_ROUND;
            const HRESULT result = DwmSetWindowAttribute(
                hwnd, static_cast<DWORD>(DWMWA_WINDOW_CORNER_PREFERENCE), &preference,
                static_cast<DWORD>(sizeof(preference)));
            // Win10 上没有 33 号属性，返回错误就静默跳过 —— 那边无边框窗口本来也圆不了。
            Log::startup(QStringLiteral("round_window_corners hwnd=%1 class=%2 hr=%3")
                             .arg(hwndText(hwnd), className,
                                  QStringLiteral("%1")
                                      .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'))
                                      .toUpper()));
        }
    }
    return TRUE;
}

BOOL CALLBACK trayToolbarEnumProc(HWND hwnd, LPARAM)
{
    if (windowClassName(hwnd) == QLatin1String("ToolbarWindow32")) {
        RECT rect {};
        GetClientRect(hwnd, &rect);
        for (LONG x = 0; x < rect.right; x += 12) {
            for (LONG y = 0; y < rect.bottom; y += 12) {
                const LPARAM lparam = static_cast<LPARAM>(
                    (static_cast<quint32>(y) << 16) | (static_cast<quint32>(x) & 0xFFFF));
                SendMessageW(hwnd, WM_MOUSEMOVE, 0, lparam);
            }
        }
    }
    return TRUE;
}

} // namespace

void restoreMainWindowNative()
{
    EnumWindows(restoreEnumProc, static_cast<LPARAM>(GetCurrentProcessId()));
}

void revealMainWindowFromTray()
{
    // 只用 ShowWindowAsync：会等待的那几个（SetForegroundWindow / AttachThreadInput /
    // 裸 ShowWindow）都把这条托盘线程的消息循环卡死过（2026-10-05、2026-10-08 两次实测）。
    RevealPick pick;
    EnumWindows(revealEnumProc, reinterpret_cast<LPARAM>(&pick));
    if (pick.best == nullptr) {
        Log::startup(Messages::revealNoWindow());
    } else {
        const QString hex = QStringLiteral("%1")
                                .arg(static_cast<qulonglong>(
                                         reinterpret_cast<quintptr>(pick.best)), 0, 16);
        Log::startup(Messages::revealPosted().replace(QStringLiteral("{:x}"), hex));
    }
}

void roundWindowCorners()
{
    EnumWindows(roundEnumProc, static_cast<LPARAM>(GetCurrentProcessId()));
}

void refreshNotificationArea()
{
    const QString trayName = QStringLiteral("Shell_TrayWnd");
    const QString overflowName = QStringLiteral("NotifyIconOverflowWindow");
    const HWND trayWindow = FindWindowW(reinterpret_cast<LPCWSTR>(trayName.utf16()), nullptr);
    if (trayWindow != nullptr) {
        EnumChildWindows(trayWindow, trayToolbarEnumProc, 0);
    }
    const HWND overflowWindow = FindWindowW(reinterpret_cast<LPCWSTR>(overflowName.utf16()), nullptr);
    if (overflowWindow != nullptr) {
        EnumChildWindows(overflowWindow, trayToolbarEnumProc, 0);
    }
}

#else // !Q_OS_WIN

void restoreMainWindowNative() {}
void revealMainWindowFromTray() {}
void roundWindowCorners() {}
void refreshNotificationArea() {}

#endif

}
