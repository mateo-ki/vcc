#pragma once

#include "router/Router.h"

#include <QMainWindow>
#include <QString>
#include <QVector>

#include <optional>

QT_BEGIN_NAMESPACE
class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QWidget;
QT_END_NAMESPACE

namespace App {
class AppContext;
}

namespace Speedtest {
struct SpeedTestOptions;
}

namespace Gui {

class ProxyPage;
class NodesPage;
class SourcesPage;

/// 侧栏五个页签，顺序与 Rust `views/sidebar.rs` 的 nav_item 调用一致。
enum class Tab { Proxy, Nodes, Sources, Settings, Api };

constexpr int kTabCount = 5;

/// 窗口骨架：左侧 220px 导航 + 右侧内容栈 + 底部状态条。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(App::AppContext &context, QWidget *parent = nullptr);
    ~MainWindow() override;

    void selectTab(Tab tab);
    /// 提权交接起来的实例：按命令行递过来的节点直接把代理起掉（Rust 的 initial_proxy_node，
    /// 它不进出口切换记录 —— 那不是"切换"，是启动）。
    void startHandoffNode(const QString &nodeId);
    Tab currentTab() const;
    QWidget *statusActions() const;

    /// 拉取完要不要自动测速的判据（拆出来是为了能单测）：设置里"自动测速"开着，
    /// 并且这一次确实解析出了节点。
    static bool shouldAutoSpeedtest(bool autoSpeedtestEnabled, int nodeCount);

    /// 底部那颗「测速全部」真正用的参数。Rust 只有一个强度旋钮（`speedtest_intensity`），
    /// 每一条测速入口都从同一个 `speed_opts()` 取 —— 所以这里必须转给节点池那一份，
    /// 而不是自己钉一个档位（拆出来是为了能单测）。
    Speedtest::SpeedTestOptions speedtestAllOptions() const;

    /// 唤出命令面板（Ctrl+K / Ctrl+P）
    void showCommandPalette();

protected:
    /// Ctrl+V 的全局导入。用事件过滤器而不是 QShortcut：快捷键一旦命中就会把按键吃掉，
    /// 输入框里的粘贴就没了（`tests/guiSourcesPage/PasteShortcutTests.cpp` 记着这个故障）。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildSidebar();
    QWidget *buildStatusStrip();
    void setupStatusActions();
    void updateStatusControls();
    /// 启动/切换失败或降级时把这颗节点标成不可用（对应 Rust 的 update_node_speed(false, google_proxy)）。
    void markNodeUnavailable(const QString &nodeId);

    /// 换出口的唯一一条路：手动点卡片和自动切换都走它（Rust `switch_proxy_node`）。
    /// reason 会进出口切换记录，所以"谁让我切的"在「查看切换记录」里查得到。
    void switchProxyNode(const QString &nodeId, const QString &nodeName, const QString &reason);

    // ---- 故障自动切换（Rust mod.rs:3327-3626 那台发动机）----
    /// 60 秒闸门在函数里，定时器只负责"想起来要看一眼"。探测本身不受开关影响。
    void checkFailover();
    void handleFailoverProbe(const QString &nodeId, const QString &nodeName, bool ok, qint64 latencyMs,
                             const QString &error);
    void applyRouterDecision(const QString &nodeId, const QString &nodeName, const Router::Decision &decision);
    void beginFailoverSwitch(const QString &currentId, const QString &preferredId, const QString &reason);
    void onFailoverTarget(const Router::BackupCandidate &chosen, const QString &failure);
    void recordFailoverMiss(const QString &fromId, const QString &fromName, const QString &reason);
    void syncRouterConfig();

    /// TUN 模式 + 没提权：把当前配置交给一个提权的自己重启，本进程立刻退出让出单实例锁。
    /// 返回 true = 已经发起（或已经报错），调用方必须放弃本次启动。
    bool requestTunElevationHandoff(const QString &nodeId);
    void seedRouterCandidates();

    void toggleProxy();
    /// 控制台节点卡右键「锁定为出口」（Rust proxy.rs:736-741）：关掉自动选路、存配置、
    /// 报状态，最后按"手动切换"把出口定到那颗节点。
    void lockEgress(const QString &nodeId, const QString &nodeName);
    void checkIpRisk();
    void toggleProxyMode();
    void speedtestAll();

    void initTray();
    void updateTray();
    void closeEvent(QCloseEvent *event) override;

    App::AppContext &context_;
    QStackedWidget *pages_ = nullptr;
    ProxyPage *proxyPage_ = nullptr;
    NodesPage *nodesPage_ = nullptr;
    SourcesPage *sourcesPage_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QWidget *statusActions_ = nullptr;
    QVector<QPushButton *> navButtons_;

    // 托盘控件
    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QAction *actShow_ = nullptr;
    QAction *actToggleProxy_ = nullptr;
    QAction *actQuit_ = nullptr;
    bool forceQuit_ = false;

    // 底部控制按钮
    QPushButton *btnSpeedtestAll_ = nullptr;
    QPushButton *btnModeChip_ = nullptr;
    QPushButton *btnIpRisk_ = nullptr;
    QPushButton *btnToggleProxy_ = nullptr;

    QString selectedNodeId_;
    QString selectedNodeName_;
    QString selectedNodeServer_;

    QTimer *failoverTimer_ = nullptr;
    Router::NodeRouter failoverRouter_;
    /// 上一次探测的时刻（单调时钟 ms）；nullopt = 这轮还没探过。
    std::optional<Router::Instant> failoverProbeAt_;
    bool failoverSearching_ = false;
    std::optional<QPair<QString, QString>> failoverFrom_;
    int failoverSwitches_ = 0;
    QString failoverLastReason_;
    /// 提权实例已经接管：父进程要"真的退掉"，不能走 aboutToQuit 那套退出清理
    /// （否则会把子进程刚接手的代理停掉、把系统代理还原成关闭）。
    bool closeAfterElevation_ = false;
};

}
