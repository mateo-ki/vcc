#pragma once

#include "gui/KernelPanel.h"
#include "gui/Page.h"
#include "gui/Pages.h"
#include "gui/ProxyView.h"
#include "models/Node.h"
#include "proxy/ConfigTypes.h"
#include "proxy/MihomoConfig.h"
#include "updater/KernelUpdater.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
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

/// 控制台页，对应 Rust `gui/views/proxy.rs` 的 `ui_proxy`。
class ProxyPage : public Page {
    Q_OBJECT

public:
    /// 六个分栏，顺序与 Rust 的 proxy_panel_tab 调用一致。
    enum class Panel { Nodes, Connections, Rules, Network, Kernels, Logs };

    explicit ProxyPage(App::AppContext &context, QWidget *parent = nullptr);
    ~ProxyPage() override;

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

    // 内核表的可观察面（离屏测试与外壳都用这几个，不伸手拿私有控件）。
    int kernelRowCount() const { return kernelRows_.size(); }
    QString kernelRowText(int index) const;
    QString kernelProgressText() const;
    bool kernelBusy() const { return kernelBusy_; }
    /// 外壳收到下载进度后灌进来；nullopt = 没有作业在跑，进度条整条消失。
    void setKernelProgress(const std::optional<Kernel::UpdateProgress> &progress);
    void setKernelBusy(bool busy);

    // 选中节点
    QString selectedNodeId() const { return selectedNodeId_; }
    QString selectedNodeName() const { return selectedNodeName_; }
    void setSelectedNode(const QString &id, const QString &name);

signals:
    /// 行上的动作只发信号，不在页面里动内核：切换要写系统代理、要占端口，
    /// 那些是外壳（busy / status / 切换原因"手动切换"）的职责。
    void nodeSwitchRequested(const QString &nodeId, const QString &nodeName);
    void nodeDetailsRequested(const QString &nodeId);
    void nodeSpeedtestRequested(const QString &nodeId);
    void nodeIpRiskRequested(const QString &nodeId);
    void nodeEgressLockRequested(const QString &nodeId, const QString &nodeName);
    /// 内核这一栏同理：起下载要占目录、要提权、要跟代理状态打架，归外壳编排。
    void kernelUpdateRequested(Kernel::Kind kind);
    void kernelCancelRequested();
    /// 状态条文案由外壳持有（Rust 的 self.status 是应用级字段，不是这页的）。
    void statusMessageRequested(const QString &message);

private slots:
    void onPollTimer();
    void onPanelSwitch();

private:
    QWidget *buildStatusCard();
    QWidget *buildNodesPanel();
    QWidget *buildConnectionsPanel();
    QWidget *buildRulesPanel();
    QWidget *buildNetworkPanel();
    QWidget *buildKernelsPanel();
    QWidget *buildLogsPanel();

    // 状态卡片
    void refreshStatusCard();

    // 可用节点栅格
    void reloadNodes();
    /// force=true（数据/筛选变了）永远重建网格；force=false（resize）只在列数与
    /// 视图指纹不变时跳过，拖窗口不再每帧重建整张节点网格。
    void applyView(bool force = true);
    int gridColumns() const;
    /// status 由 applyView 整网格取一次传进来：每张卡都调 proxy().status() 会
    /// 反复抢 runtimeMutex_（代理启动/切换期间该锁被秒级持有）。
    QWidget *makeNodeRow(const ProxyViewRow &row, const Proxy::ProxyRuntimeStatus &status);

    // 活跃连接
    struct ActiveConn {
        QString id;
        QString network;
        QString host;
        QString destination;
        QString process;
        QString rule;
        quint64 upload = 0;
        quint64 download = 0;
    };
    void reloadConnections();
    void updateConnectionsTable();
    void closeConnection(const QString &id);
    void closeAllConnections();

    // 分流规则
    void reloadRules();
    void refreshRulesTable();
    void saveRules();
    void saveRoutingMode();
    void updateGeoData();
    void applyRecommendedRules();
    void autoOptimizeRules();
    void prependPresetRules(const QVector<Proxy::TrafficRule> &preset);

    // 网络设置
    void reloadNetwork();
    void saveNetworkConfig();

    // 内核表
    void reloadKernels();
    void layoutKernelColumns();

    // 日志
    void reloadLogs();
    void clearLogs();
    void copyLogs();

    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    QTabBar *panels_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QTimer *pollTimer_ = nullptr;

    // 状态卡片各读数控件
    QLabel *stateBadge_ = nullptr;
    QLabel *stateMessage_ = nullptr;
    QLabel *statusNodeName_ = nullptr;
    QLabel *statusNodeDetail_ = nullptr;
    QLabel *statusHealth_ = nullptr;
    QLabel *statusLatency_ = nullptr;
    QLabel *statusConnCount_ = nullptr;
    QLabel *kernelInfo_ = nullptr;
    QLabel *statusRates_ = nullptr;
    QLabel *statusSessionTraffic_ = nullptr;
    QLabel *statusFailover_ = nullptr;

    QString selectedNodeId_;
    QString selectedNodeName_;
    quint64 uploadTotal_ = 0;
    quint64 downloadTotal_ = 0;
    quint64 lastUploadTotal_ = 0;
    quint64 lastDownloadTotal_ = 0;
    quint64 uploadRate_ = 0;
    quint64 downloadRate_ = 0;
    qint64 lastPollTimestamp_ = 0;
    int failoverSwitches_ = 0;

    // 可用节点栅格
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
    // 上一次真正渲染出来的网格指纹（resize 路径据此跳过重建）。
    int lastGridColumns_ = -1;
    int lastRenderedRows_ = -1;
    QString lastRenderedSelectedId_;

    // 活跃连接分栏
    QLineEdit *connFilterEdit_ = nullptr;
    QLabel *connCountLabel_ = nullptr;
    QPushButton *connCloseAllBtn_ = nullptr;
    QTableWidget *connTable_ = nullptr;
    QWidget *connEmptyCard_ = nullptr;
    QLabel *connEmptyLabel_ = nullptr;
    QVector<ActiveConn> connections_;

    // 分流规则分栏
    Proxy::MihomoConfigOptions rulesOptions_;
    QButtonGroup *routingModeGroup_ = nullptr;
    QLabel *routingModeDetailLabel_ = nullptr;
    QPushButton *saveModeBtn_ = nullptr;
    QPushButton *geoUpdateBtn_ = nullptr;
    QLabel *geoStatusLabel_ = nullptr;
    bool geoUpdating_ = false;
    QLabel *rulesHitSummaryLabel_ = nullptr;
    QWidget *rulesHitsListWidget_ = nullptr;
    QVBoxLayout *rulesHitsListBox_ = nullptr;
    QTableWidget *rulesTable_ = nullptr;
    QComboBox *newRuleKindCombo_ = nullptr;
    QLineEdit *newRuleValueEdit_ = nullptr;
    QComboBox *newRuleActionCombo_ = nullptr;
    QPushButton *addRuleBtn_ = nullptr;
    QPushButton *recRulesBtn_ = nullptr;
    QPushButton *optRulesBtn_ = nullptr;
    QPushButton *saveRulesBtn_ = nullptr;
    QLabel *newRuleHintLabel_ = nullptr;

    // 网络设置分栏
    Proxy::MihomoConfigOptions networkOptions_;
    QRadioButton *netModeSystemRadio_ = nullptr;
    QRadioButton *netModeTunRadio_ = nullptr;
    QComboBox *netKernelPrefCombo_ = nullptr;
    QSpinBox *netMixedPortSpin_ = nullptr;
    QSpinBox *netControllerPortSpin_ = nullptr;
    QCheckBox *netAllowLanCheck_ = nullptr;
    QCheckBox *netTerminalProxyCheck_ = nullptr;
    QWidget *netTunGroup_ = nullptr;
    QComboBox *netTunStackCombo_ = nullptr;
    QCheckBox *netTunAutoRouteCheck_ = nullptr;
    QCheckBox *netTunAutoDetectCheck_ = nullptr;
    QCheckBox *netDnsEnabledCheck_ = nullptr;
    QCheckBox *netDnsIpv6Check_ = nullptr;
    QComboBox *netDnsEnhancedCombo_ = nullptr;
    QLineEdit *netDnsPrimaryEdit_ = nullptr;
    QLineEdit *netDnsSecondaryEdit_ = nullptr;
    QPushButton *saveNetworkBtn_ = nullptr;
    QLabel *netEditableNoticeLabel_ = nullptr;

    // 内核管理分栏
    void openKernelDownloadModal(Kernel::Kind kind);
    void startKernelUpdate(Kernel::Kind kind, Kernel::Source source, const QString &localFile);
    void cancelKernelDownload();

    QList<Kernel::Info> kernelInfos_;
    QList<Kernel::ReleaseInfo> kernelReleases_;
    QList<KernelRow> kernelRows_;
    std::optional<Kernel::UpdateProgress> kernelProgress_;
    std::optional<Updater::CancelToken> cancelToken_;
    QWidget *kernelTable_ = nullptr;
    QGridLayout *kernelGrid_ = nullptr;
    QLabel *kernelProgressLabel_ = nullptr;
    QPushButton *kernelCheckButton_ = nullptr;
    QPushButton *kernelCancelButton_ = nullptr;
    bool kernelBusy_ = false;
    bool kernelLoading_ = false;

    // 运行日志分栏
    QLabel *logKernelTitleLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QCheckBox *logAutoScrollCheck_ = nullptr;
    QPushButton *logCopyBtn_ = nullptr;
    QPushButton *logClearBtn_ = nullptr;
};

}
