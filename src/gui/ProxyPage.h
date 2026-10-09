#pragma once

#include "gui/Page.h"
#include "gui/ProxyView.h"
#include "models/Node.h"
#include "proxy/MihomoConfig.h"

#include <QString>
#include <QStringList>
#include <QVector>

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
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
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
    QString regionFilter() const { return regionFilter_; }
    QString searchText() const { return search_; }
    ProxySortBy sortBy() const { return sortBy_; }
    QString countText() const;
    QStringList regionPillCodes() const;
    int rowCount() const { return view_.rows.size(); }

signals:
    /// 行上的动作只发信号，不在页面里动内核：切换要写系统代理、要占端口，
    /// 那些是外壳（busy / status / 切换原因"手动切换"）的职责。
    void nodeSwitchRequested(const QString &nodeId, const QString &nodeName);
    void nodeDetailsRequested(const QString &nodeId);
    void nodeSpeedtestRequested(const QString &nodeId);
    void nodeIpRiskRequested(const QString &nodeId);
    void nodeEgressLockRequested(const QString &nodeId, const QString &nodeName);

private:
    QWidget *buildStatusCard();
    QWidget *buildLogsPanel();
    QWidget *buildNodesPanel();

    /// 从库里重取一次"可用节点"（worker 线程读，回到 GUI 线程落视图）。
    void reloadNodes();
    /// 按 (地区, 搜索, 排序) 重画栅格；视图本身由 buildProxyView 算。
    void applyView();
    /// 列数跟着宽度走：>=920 四列，>=660 三列，否则两列（Rust 同源）。
    int gridColumns() const;
    QWidget *makeNodeRow(const ProxyViewRow &row);

    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    QTabBar *panels_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLabel *stateBadge_ = nullptr;
    QLabel *stateMessage_ = nullptr;
    QLabel *kernelInfo_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;

    // 可用节点栅格的视图状态（对应 Rust 的 proxy_region_filter / proxy_search /
    // proxy_sort_by / proxy_nodes / proxy_nodes_rev / proxy_view）。
    QVector<Models::NodeSummary> allNodes_;
    ProxyView view_;
    QScrollArea *nodeScroll_ = nullptr;
    QGridLayout *grid_ = nullptr;
    QWidget *gridHost_ = nullptr;
    QLabel *countLabel_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QVector<QPushButton *> regionPills_;
    QPushButton *sortLatency_ = nullptr;
    QPushButton *sortName_ = nullptr;
    QString regionFilter_ = QStringLiteral("all");
    QString search_;
    ProxySortBy sortBy_ = ProxySortBy::Latency;
    quint64 dataRev_ = 0;
    bool nodesRequested_ = false;
};

QWidget *createProxyPage(App::AppContext &context, QWidget *parent = nullptr);

}
