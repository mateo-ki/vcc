#pragma once

#include "gui/Page.h"
#include "proxy/MihomoConfig.h"

#include <QString>

namespace Gui {

/// 徽章：文字 + 配色角色（取 Theme 已有的 QSS objectName，不新造颜色）。
/// 对应 Rust `widgets.rs::proxy_state_label` 与 `views/proxy.rs` 里"预备"那一支。
struct StatusBadge {
    QString text;
    QString role;
};

/// 一个内核都没装上、代理也不在跑 → 「预备」；否则用状态本身的文案。
StatusBadge statusBadgeFor(Proxy::ProxyState state, bool hasKernel, bool proxyActive);

}

QT_BEGIN_NAMESPACE
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QTabBar;
QT_END_NAMESPACE

namespace Gui {

/// 控制台页，对应 Rust `gui/views/proxy.rs` 的 `ui_proxy`。
///
/// Rust 那页每帧重建，这里是常驻 widget：`refresh()` 负责从运行态取一次状态、
/// 重画卡片、并把当前分栏该刷的东西刷一遍。
class ProxyPage : public Page {
    Q_OBJECT

public:
    /// 六个分栏，顺序与 Rust 的 proxy_panel_tab 调用一致。
    enum class Panel { Nodes, Connections, Rules, Network, Kernels, Logs };

    explicit ProxyPage(App::AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

    Panel currentPanel() const;
    void selectPanel(Panel panel);
    QString stateBadgeText() const;
    QString stateMessageText() const;
    QString kernelInfoText() const;

private:
    QWidget *buildStatusCard();
    QWidget *buildLogsPanel();

    QTabBar *panels_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLabel *stateBadge_ = nullptr;
    QLabel *stateMessage_ = nullptr;
    QLabel *kernelInfo_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
};

QWidget *createProxyPage(App::AppContext &context, QWidget *parent = nullptr);

}
