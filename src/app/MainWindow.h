#pragma once

#include <QMainWindow>
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

/// 侧栏五个页签，顺序与 Rust `views/sidebar.rs` 的 nav_item 调用一致。
enum class Tab { Proxy, Nodes, Sources, Settings, Api };

constexpr int kTabCount = 5;

/// 窗口骨架：左侧 220px 导航 + 右侧内容栈 + 底部状态条。
/// 对应 Rust `gui/mod.rs` 的 SidePanel / CentralPanel / TopBottomPanel(bottom) 三件套。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(App::AppContext &context, QWidget *parent = nullptr);

    void selectTab(Tab tab);
    Tab currentTab() const;
    /// 底部状态条右侧那块：控制按钮（启动/停止代理、IP 检测、模式 chip、测速全部）
    /// 由控制台层往这里加，顺序决定靠右程度（先加的在最右）。
    QWidget *statusActions() const;

private:
    QWidget *buildSidebar();
    QWidget *buildStatusStrip();

    App::AppContext &context_;
    QStackedWidget *pages_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QWidget *statusActions_ = nullptr;
    QVector<QPushButton *> navButtons_;
};

}
