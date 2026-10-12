#include "platform/Tray.h"

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Log.h"
#include "platform/Messages.h"
#include "platform/WindowHelpers.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <shellapi.h>
// OpenDesktopW / SetThreadDesktop 由 WinUser.h 声明、user32 导出，windows.h 已经带上了。

// 托盘消息号：Rust 用 `WM_USER + 100/101`，菜单命令号是 2001/2002/2003。
// 这几个数字是跨进程/跨实现约定（还装着 Rust 建的实例时它会往这些号发消息），
// 所以头文件里只放数字，真正的宏在这里用 static_assert 钉死。
static_assert(WM_USER == 0x0400, "WM_USER 变了，托盘消息号要和 Rust 对齐");
static_assert(WM_LBUTTONDOWN == 0x0201, "WM_LBUTTONDOWN 值不对");
static_assert(WM_LBUTTONUP == 0x0202, "WM_LBUTTONUP 值不对");
static_assert(WM_LBUTTONDBLCLK == 0x0203, "WM_LBUTTONDBLCLK 值不对");
static_assert(WM_RBUTTONUP == 0x0205, "WM_RBUTTONUP 值不对");
static_assert(WM_CONTEXTMENU == 0x007B, "WM_CONTEXTMENU 值不对");
#endif

namespace Platform {

namespace {

struct TrayUpdate {
    enum class Kind {
        SetProxyRunning,
        SetTooltip,
        Shutdown,
    };
    Kind kind = Kind::SetProxyRunning;
    bool running = false;
    QString tooltip;
};

} // namespace

QString trayWindowClass()
{
    return QStringLiteral("VPNParserTrayWindowClass");
}

QString mainWindowTitle()
{
    return QStringLiteral("VCC");
}

QString wakeupMessageName()
{
    return QStringLiteral("VCC_SHOW_WINDOW_MSG");
}

#ifdef Q_OS_WIN
quint32 wakeupInstanceId() { return static_cast<quint32>(WM_USER + 101); }
quint32 trayCallbackMessage() { return static_cast<quint32>(WM_USER + 100); }
#else
quint32 wakeupInstanceId() { return 0x400 + 101; }
quint32 trayCallbackMessage() { return 0x400 + 100; }
#endif

quint32 trayIconId() { return 1001; }
quint32 cmdShow() { return 2001; }
quint32 cmdToggleProxy() { return 2002; }
quint32 cmdExit() { return 2003; }

QVector<TrayMenuItem> menuItemsFor(bool proxyRunning)
{
    // 顺序与标签照抄 Rust 的三段 AppendMenuW + 一条分隔线；运行中显示"停止代理"，否则"启动代理"。
    QVector<TrayMenuItem> items;
    items.append(TrayMenuItem { cmdShow(), Messages::menuShow(), false });
    items.append(TrayMenuItem { cmdToggleProxy(),
                                proxyRunning ? Messages::menuStopProxy() : Messages::menuStartProxy(),
                                false });
    items.append(TrayMenuItem { 0, QString(), true });
    items.append(TrayMenuItem { cmdExit(), Messages::menuExit(), false });
    return items;
}

std::optional<TrayEvent> trayEventForCommand(quint32 command)
{
    if (command == cmdShow()) {
        return TrayEvent::ShowWindow;
    }
    if (command == cmdToggleProxy()) {
        return TrayEvent::ToggleProxy;
    }
    if (command == cmdExit()) {
        return TrayEvent::Quit;
    }
    // TrackPopupMenu 返回 0（用户点了菜单外面）时 Rust 走 `_ => {}`，不产生事件。
    return std::nullopt;
}

bool isTraySelectEvent(quint32 event)
{
    // 左键按下/抬起/双击，加上 Win11 的 NIN_SELECT(WM_USER+0) 与 NIN_KEYSELECT(WM_USER+1)。
    // 数字在文件开头用 static_assert 和 winuser.h 钉过，这里直接写值，判据与 Rust 逐条一致。
    return event == 0x0202u // WM_LBUTTONUP
        || event == 0x0203u // WM_LBUTTONDBLCLK
        || event == 0x0201u // WM_LBUTTONDOWN
        || event == 0x0400u // WM_USER + 0  == NIN_SELECT
        || event == 0x0401u; // WM_USER + 1 == NIN_KEYSELECT
}

bool isTrayContextEvent(quint32 event)
{
    // 只认抬起（和 WM_CONTEXTMENU）：以前连 WM_RBUTTONDOWN(0x0204) 也弹一次，
    // 于是按下弹的那层会被抬起这一下直接顶掉 —— 日志里表现为一次右键两行
    // "popping up context menu"、外层那行 `cmd=0`，看起来就是"点了没反应"。
    return event == 0x0205u || event == 0x007Bu;
}

int trayTipCapacity()
{
#ifdef Q_OS_WIN
    NOTIFYICONDATAW probe {};
    return static_cast<int>(sizeof(probe.szTip) / sizeof(WCHAR)) - 1;
#else
    return 127;
#endif
}

QString truncateTrayTip(const QString &text, int capacity)
{
    if (capacity <= 0 || text.size() <= capacity) {
        return text;
    }
    return text.left(capacity);
}

QString defaultTrayTooltip()
{
    return Messages::trayTooltip();
}

void notifyExistingInstance()
{
#ifdef Q_OS_WIN
    const QString className = trayWindowClass();
    const HWND hwnd = FindWindowW(reinterpret_cast<LPCWSTR>(className.utf16()), nullptr);
    if (hwnd != nullptr) {
        PostMessageW(hwnd, static_cast<UINT>(wakeupInstanceId()), 0, 0);
    } else {
        const QString broadcastName = wakeupMessageName();
        const UINT messageId = RegisterWindowMessageW(
            reinterpret_cast<LPCWSTR>(broadcastName.utf16()));
        if (messageId != 0) {
            PostMessageW(HWND_BROADCAST, messageId, 0, 0);
        }
    }
    restoreMainWindowNative();
#endif
}

// ---------------------------------------------------------------------------
// TrayHandle
// ---------------------------------------------------------------------------

struct TrayHandle::Private {
    std::mutex mutex;
    std::condition_variable updateReady;
    std::vector<TrayEvent> events;
    std::vector<TrayUpdate> updates;
    bool updatesClosed = false;

    std::mutex wakerMutex;
    std::function<void()> waker;

    std::atomic<bool> proxyRunning { false };
    std::atomic<bool> started { false };
    /// shutdown() 在托盘线程还没跑起来时被调过：那条线程看到它就别再往通知区域塞图标。
    std::atomic<bool> stopRequested { false };
    std::atomic<quintptr> trayHwnd { 0 };
    QString pendingTooltip;

    std::thread trayThread;
    std::thread updaterThread;

    void pushEvent(TrayEvent event)
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(event);
    }

    /// 对应 Rust 的 `event_rx.try_recv()`：取一条就走，空了就返回 false。
    bool popEvent(TrayEvent &out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (events.empty()) {
            return false;
        }
        out = events.front();
        events.erase(events.begin());
        return true;
    }

    void pushUpdate(TrayUpdate update)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (updatesClosed) {
                return;
            }
            updates.push_back(std::move(update));
        }
        updateReady.notify_one();
    }

    void closeUpdates()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            updatesClosed = true;
        }
        updateReady.notify_all();
    }

    bool popUpdate(TrayUpdate &out)
    {
        std::unique_lock<std::mutex> lock(mutex);
        updateReady.wait(lock, [this] { return !updates.empty() || updatesClosed; });
        if (updates.empty()) {
            return false;
        }
        out = updates.front();
        updates.erase(updates.begin());
        return true;
    }

    /// 锁只用来把 std::function 取出来；回调必须在锁外调用 —— UI 侧的 waker 会进界面库内部状态，
    /// 持锁调用会和 UI 线程每帧的 `setWaker` 形成锁序反转。
    std::function<void()> wakerCopy()
    {
        std::lock_guard<std::mutex> lock(wakerMutex);
        return waker;
    }
};

namespace {

void triggerWaker(TrayHandle::Private *priv)
{
    const std::function<void()> callback = priv->wakerCopy();
    if (callback) {
        callback();
    }
}

#ifdef Q_OS_WIN

/// 把文本写进 NOTIFYICONDATAW 的 szTip：截到装得下的长度再补结尾零，
/// 等价于 Rust 的 `tip.encode_utf16().chain(Some(0)).take(nid.szTip.len() - 1)`。
void copyIntoTipField(WCHAR *field, const QString &text)
{
    const int capacity = trayTipCapacity();
    const QString clipped = truncateTrayTip(text, capacity);
    int count = static_cast<int>(clipped.size());
    if (count > capacity) {
        count = capacity;
    }
    for (int i = 0; i < count; ++i) {
        field[i] = clipped.at(i).unicode();
    }
    field[count] = 0;
}

/// Rust 那边是 `static GLOBAL_CONTEXT: Mutex<Option<TrayContext>>`：一个进程一个托盘。
/// wnd_proc 必须在很短的锁里把要用的东西取出来就放锁，再进 user32 —— 持锁进
/// TrackPopupMenu 的模态循环会让再入路径在同线程二次加锁，直接死锁
/// （托盘从此点击无反应，菜单也永远显示不出来）。
struct TrayContext {
    TrayHandle::Private *priv = nullptr;
    quint32 customWakeupMsg = 0;
};

std::mutex g_contextMutex;
TrayContext *g_context = nullptr;

void setGlobalContext(TrayContext *context)
{
    std::lock_guard<std::mutex> lock(g_contextMutex);
    g_context = context;
}

std::optional<TrayContext> globalContextSnapshot()
{
    std::lock_guard<std::mutex> lock(g_contextMutex);
    if (g_context == nullptr) {
        return std::nullopt;
    }
    return *g_context;
}

void updateTrayTooltip(TrayHandle::Private *priv, const QString &tooltip)
{
    const quintptr raw = priv->trayHwnd.load();
    if (raw == 0) {
        return;
    }
    NOTIFYICONDATAW nid {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = reinterpret_cast<HWND>(raw);
    nid.uID = trayIconId();
    nid.uFlags = NIF_TIP;
    copyIntoTipField(nid.szTip, tooltip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void runTrayUpdater(TrayHandle::Private *priv)
{
    while (true) {
        TrayUpdate update;
        if (!priv->popUpdate(update)) {
            break;
        }
        switch (update.kind) {
        case TrayUpdate::Kind::SetProxyRunning:
            priv->proxyRunning.store(update.running);
            break;
        case TrayUpdate::Kind::SetTooltip:
            updateTrayTooltip(priv, update.tooltip);
            break;
        case TrayUpdate::Kind::Shutdown: {
            const quintptr raw = priv->trayHwnd.load();
            if (raw != 0) {
                PostMessageW(reinterpret_cast<HWND>(raw), WM_CLOSE, 0, 0);
            }
            return;
        }
        }
    }
}

void showTrayContextMenu(HWND hwnd, TrayHandle::Private *priv)
{
    POINT point {};
    GetCursorPos(&point);
    // 前台切换失败时 TrackPopupMenu 会弹出一个看不见的菜单并永久阻塞托盘线程
    // （菜单等不到任何点击），表现为托盘从此点击无反应。必须确保切换成功。
    if (SetForegroundWindow(hwnd) == 0) {
        const HWND foreWindow = GetForegroundWindow();
        const DWORD foreThread = GetWindowThreadProcessId(foreWindow, nullptr);
        const DWORD currentThread = GetCurrentThreadId();
        if (foreThread != 0 && foreThread != currentThread) {
            AttachThreadInput(foreThread, currentThread, TRUE);
            SetForegroundWindow(hwnd);
            AttachThreadInput(foreThread, currentThread, FALSE);
        }
        SetForegroundWindow(hwnd);
    }

    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    const bool running = priv->proxyRunning.load(std::memory_order_relaxed);
    for (const TrayMenuItem &item : menuItemsFor(running)) {
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(item.id),
                    reinterpret_cast<LPCWSTR>(item.text.utf16()));
    }

    const UINT command = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    PostMessageW(hwnd, 0, 0, 0); // Microsoft KB135788 requirement

    Log::startup(QStringLiteral("Tray popup menu choice: cmd=%1").arg(command));

    const std::optional<TrayEvent> event = trayEventForCommand(command);
    if (!event.has_value()) {
        return;
    }
    priv->pushEvent(*event);
    triggerWaker(priv);
    // 和左键那条一样：菜单选完之后由托盘线程自己把窗口掀开，UI 线程这时候可能压根不跑帧。
    revealMainWindowFromTray();
}

LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const std::optional<TrayContext> snapshot = globalContextSnapshot();
    if (snapshot.has_value()) {
        const quint32 custom = snapshot->customWakeupMsg;
        const bool isWakeup = msg == wakeupInstanceId() || (custom != 0 && msg == custom);
        if (isWakeup) {
            Log::startup(QStringLiteral("wnd_proc received instance wakeup message"));
            // 窗口恢复一律交给 UI 线程（见 TrayEvent::ShowWindow 的处理）：托盘线程
            // 去做跨线程 ShowWindow/SetForegroundWindow 会把这条消息循环卡死，
            // 之后托盘左键右键全都没反应（2026-10-05 实测：整条链路停在
            // restore_main_window_native 里，日志再无一行托盘事件）。
            //
            // Rust 这一支是持着 GLOBAL_CONTEXT 锁做下面三件事的；这里统一改成
            // "短锁取快照、锁外调 user32"，理由和下面 click 分支那条注释一样
            // （持锁进跨线程调用会和 UI 侧的取锁动作互等）。
            snapshot->priv->pushEvent(TrayEvent::ShowWindow);
            triggerWaker(snapshot->priv);
            revealMainWindowFromTray();
            return 0;
        }
    }

    switch (msg) {
    case WM_USER + 100: {
        const quint32 event = static_cast<quint32>(static_cast<quintptr>(lParam) & 0xFFFFu);
        Log::startup(QStringLiteral(
                         "WM_TRAY_CALLBACK received: event=0x%1 (lparam=0x%2, wparam=0x%3)")
                         .arg(event, 4, 16, QLatin1Char('0'))
                         .arg(static_cast<qulonglong>(lParam), 0, 16)
                         .arg(static_cast<qulonglong>(wParam), 0, 16));

        if (isTraySelectEvent(event)) {
            Log::startup(QStringLiteral("Tray click event matched -> queueing show-window event"));
            const std::optional<TrayContext> click = globalContextSnapshot();
            if (!click.has_value()) {
                return 0;
            }
            // 和右键那条一样：先在短锁里取出需要的东西就立刻放锁。持锁进
            // user32（reveal/waker 都会回界面库与 Win32）等于把 GLOBAL_CONTEXT
            // 压在跨线程调用底下，UI 线程那侧任何取锁的动作都会和托盘互等。
            click->priv->pushEvent(TrayEvent::ShowWindow);
            triggerWaker(click->priv);
            // 左键就是「把窗口叫回来」，而收进托盘时 UI 线程根本不跑帧，
            // 所以这一下必须由托盘线程自己掀开（见 reveal 的注释）。
            revealMainWindowFromTray();
        } else if (isTrayContextEvent(event)) {
            Log::startup(QStringLiteral("Tray right click event matched -> popping up context menu"));
            const std::optional<TrayContext> click = globalContextSnapshot();
            if (!click.has_value()) {
                return 0;
            }
            showTrayContextMenu(hwnd, click->priv);
        }
        return 0;
    }
    case WM_CLOSE: {
        Log::startup(QStringLiteral("Tray wnd_proc WM_CLOSE -> deleting tray icon"));
        NOTIFYICONDATAW nid {};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwnd;
        nid.uID = trayIconId();
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY: {
        Log::startup(
            QStringLiteral("Tray wnd_proc WM_DESTROY -> deleting tray icon and posting quit"));
        NOTIFYICONDATAW nid {};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwnd;
        nid.uID = trayIconId();
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void runTrayLoop(TrayHandle::Private *priv)
{
    // Ensure tray thread is attached to the interactive user desktop ("default") where Shell_TrayWnd resides
    {
        WCHAR desktopName[] = L"default";
        const HDESK desk = OpenDesktopW(desktopName, 0, FALSE, 0x01FF);
        if (desk != nullptr) {
            const BOOL res = SetThreadDesktop(desk);
            Log::startup(QStringLiteral("Tray thread SetThreadDesktop('default'): %1").arg(res));
        }
    }

    const QString className = trayWindowClass();
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const QString broadcastName = wakeupMessageName();
    const quint32 customWakeupMsg =
        RegisterWindowMessageW(reinterpret_cast<LPCWSTR>(broadcastName.utf16()));

    // QString::utf16() 自带结尾零，等价于 Rust 那边 encode_utf16().chain(once(0)) 的缓冲区；
    // className / broadcastName 是局部量，活到 CreateWindowExW 返回为止。
    LPCWSTR classNamePtr = reinterpret_cast<LPCWSTR>(className.utf16());

    WNDCLASSW wndClass {};
    wndClass.lpfnWndProc = trayWndProc;
    wndClass.hInstance = instance;
    wndClass.lpszClassName = classNamePtr;
    RegisterClassW(&wndClass);

    const HWND hwnd = CreateWindowExW(0, classNamePtr, classNamePtr, WS_OVERLAPPED, 0, 0, 0, 0,
                                      nullptr, nullptr, instance, nullptr);
    Log::startup(QStringLiteral("Tray window created: hwnd=0x%1")
                     .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(hwnd)), 0, 16));
    if (hwnd == nullptr) {
        priv->closeUpdates();
        return;
    }
    if (priv->stopRequested.load()) {
        // shutdown() 在线程真正起起来之前就被调过：不建图标，直接收摊（析构那边要 join 这条线程）。
        DestroyWindow(hwnd);
        priv->trayHwnd.store(0);
        priv->closeUpdates();
        return;
    }
    priv->trayHwnd.store(reinterpret_cast<quintptr>(hwnd));

    // 局部变量活得比消息循环久，wnd_proc 才能一直取到它（Rust 用的是一个 static）。
    TrayContext context;
    context.priv = priv;
    context.customWakeupMsg = customWakeupMsg;
    setGlobalContext(&context);

    const HICON icon = [] {
        HICON loaded = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        if (loaded == nullptr) {
            loaded = LoadIconW(nullptr, IDI_APPLICATION);
        }
        return loaded;
    }();

    NOTIFYICONDATAW nid {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = trayIconId();
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = static_cast<UINT>(trayCallbackMessage());
    nid.hIcon = icon;
    copyIntoTipField(nid.szTip, defaultTrayTooltip());

    Shell_NotifyIconW(NIM_DELETE, &nid);
    const BOOL added = Shell_NotifyIconW(NIM_ADD, &nid);
    Log::startup(QStringLiteral("Shell_NotifyIconW NIM_ADD result: %1").arg(added));

    priv->updaterThread = std::thread([priv] { runTrayUpdater(priv); });

    // Rust 在 NIM_ADD 之后才把 updater 线程起来，之前排队的 set_tooltip 会紧随其后以
    // NIM_MODIFY 落地；这边的"未启动即调用"走 pendingTooltip，补一次同样的效果。
    if (!priv->pendingTooltip.isEmpty()) {
        updateTrayTooltip(priv, priv->pendingTooltip);
    }

    MSG msg {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd);
    setGlobalContext(nullptr);
    priv->trayHwnd.store(0);
    // Rust 靠 drop(sender) 让 updater 线程的 recv() 报错退出；这里显式关队，语义一致。
    priv->closeUpdates();
}

#endif // Q_OS_WIN

} // namespace

TrayHandle::TrayHandle()
    : d_(std::make_shared<Private>())
{
}

TrayHandle::~TrayHandle()
{
    shutdown();
#ifdef Q_OS_WIN
    if (d_->trayThread.joinable()) {
        d_->trayThread.join();
    }
    if (d_->updaterThread.joinable()) {
        d_->updaterThread.join();
    }
#endif
}

void TrayHandle::start()
{
#ifdef Q_OS_WIN
    if (d_->started.exchange(true)) {
        return;
    }
    // 线程持有 shared_ptr，即使 TrayHandle 先析构也不会把 Private 抽走。
    std::shared_ptr<Private> priv = d_;
    d_->trayThread = std::thread([priv] { runTrayLoop(priv.get()); });
#endif
}

bool TrayHandle::isStarted() const
{
    return d_->started.load();
}

void TrayHandle::setWaker(std::function<void()> waker)
{
    std::lock_guard<std::mutex> lock(d_->wakerMutex);
    d_->waker = std::move(waker);
}

std::optional<TrayEvent> TrayHandle::tryRecvEvent()
{
    TrayEvent event = TrayEvent::ShowWindow;
    if (!d_->popEvent(event)) {
        return std::nullopt;
    }
    return event;
}

void TrayHandle::setProxyRunning(bool running)
{
    if (!d_->started.load()) {
        // 还没起线程（构建/测试期就是这个状态）：直接记状态，别去碰通知区域。
        d_->proxyRunning.store(running);
        return;
    }
    TrayUpdate update;
    update.kind = TrayUpdate::Kind::SetProxyRunning;
    update.running = running;
    d_->pushUpdate(update);
}

void TrayHandle::setTooltip(const QString &tooltip)
{
    if (!d_->started.load()) {
        d_->pendingTooltip = tooltip;
        return;
    }
    TrayUpdate update;
    update.kind = TrayUpdate::Kind::SetTooltip;
    update.tooltip = tooltip;
    d_->pushUpdate(update);
}

void TrayHandle::shutdown()
{
    d_->stopRequested.store(true);
    if (!d_->started.exchange(false)) {
        d_->closeUpdates();
        return;
    }
#ifdef Q_OS_WIN
    TrayUpdate update;
    update.kind = TrayUpdate::Kind::Shutdown;
    d_->pushUpdate(update);
    if (d_->trayHwnd.load() == 0) {
        // 托盘线程还没把窗口建起来（或建窗失败）：没有 hwnd 可投 WM_CLOSE，
        // 关队让那条线程自己收摊，免得析构 join 卡死。
        d_->closeUpdates();
    }
#endif
}

}
