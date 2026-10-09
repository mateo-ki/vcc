#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

namespace App {
class AppContext;
}

namespace Gui {

class ProxyPage;
class NodesPage;

/// 侧栏五个页签，顺序与 Rust `views/sidebar.rs` 的 nav_item 调用一致。
enum class Tab { Proxy, Nodes, Sources, Settings, Api };

constexpr int kTabCount = 5;

/// 窗口骨架：左侧 220px 导航 + 右侧内容栈 + 底部状态条。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(App::AppContext &context, QWidget *parent = nullptr);

    void selectTab(Tab tab);
    Tab currentTab() const;
    QWidget *statusActions() const;

private:
    QWidget *buildSidebar();
    QWidget *buildStatusStrip();
    void setupStatusActions();
    void updateStatusControls();

    void toggleProxy();
    void checkIpRisk();
    void toggleProxyMode();
    void speedtestAll();

    App::AppContext &context_;
    QStackedWidget *pages_ = nullptr;
    ProxyPage *proxyPage_ = nullptr;
    NodesPage *nodesPage_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QWidget *statusActions_ = nullptr;
    QVector<QPushButton *> navButtons_;

    // 底部控制按钮
    QPushButton *btnSpeedtestAll_ = nullptr;
    QPushButton *btnModeChip_ = nullptr;
    QPushButton *btnIpRisk_ = nullptr;
    QPushButton *btnToggleProxy_ = nullptr;

    QString selectedNodeId_;
    QString selectedNodeName_;
    QString selectedNodeServer_;
};

}
