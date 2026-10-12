#pragma once

#include "gui/KernelPanel.h"
#include "gui/HealthStrip.h"
#include "gui/TrafficSparkline.h"
#include "db/DatabaseTelemetry.h"
#include "gui/Page.h"
#include "gui/Pages.h"
#include "gui/NodeCard.h"
#include "gui/ProxyView.h"
#include "gui/RuleHits.h"
#include "gui/RulesPanel.h"
#include "router/Router.h"
#include "models/Node.h"
#include "proxy/ConfigTypes.h"
#include "proxy/MihomoConfig.h"
#include "updater/KernelUpdater.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
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
    /// 整网格重建了几次：切页卡顿的判据（内容没变就必须是 0 增量）。
    int gridRebuildCount() const { return gridRebuilds_; }
    // 可用节点栅格是分页画的（一页 50 张卡）：网格只保留当前页的卡片，
    // 245 个节点不再等于 245 张常驻卡片。
    int nodePage() const { return nodePage_; }
    int nodePageCount() const;
    int nodePageSize() const { return nodePageSize_; }
    // 当前页真正摆进网格的卡片数（末页会少于一页的额度）。
    int nodePageCardCount() const;
    /// 当前页**真的看得见**的卡片数：池化复用只藏不删，所以"在布局里"和"看得见"是两件事
    /// （历史上那次"有数没卡片"就是差在这一层）。
    int visibleCardCount() const;
    void setNodePageForTest(int page)
    {
        nodePage_ = page;
        applyView();
    }

    // 内核表的可观察面（离屏测试与外壳都用这几个，不伸手拿私有控件）。
    int kernelRowCount() const { return kernelRows_.size(); }
    QString kernelRowText(int index) const;
    QString kernelProgressText() const;
    bool kernelBusy() const { return kernelBusy_; }
    /// 节点池变了（拉取完成、全量测速完成、切节点写了延迟）之后作废这一页缓存的列表，
    /// 下一次 refresh() 重新查库。对应 Rust 的 request_full_reload —— 缺了这一步，
    /// refresh() 见 allNodes_ 非空就只重排旧数据，控制台能一直挂着开屏那一刻的列表。
    void requestFullReload()
    {
        allNodes_.clear();
        ++dataRev_;
        lastViewSignature_ = 0;
    }

    /// 外壳的出口探针落库之后叫一声：状态卡那行「实测 …」与健康条读的是这张表。
    void noteEgressHealthUpdated()
    {
        reloadEgressHealth();
        refreshStatusCard();
    }
    /// 自动切换的计数与最近一次判定由外壳推进来（Rust 的 failover_switches / last_reason
    /// 是应用级字段，不是这一页的）。
    void setFailoverStatus(int switches, const QString &lastReason);
    /// 故障切换挑替补用的候选集（Rust 的 proxy_nodes）。
    QVector<Router::BackupCandidate> backupCandidates() const;

    /// 探针/测试用：直接跑一次 1.5 s 轮询要做的全部工作，好把它的耗时量出来
    /// （生产路径就是 `pollTimer_ -> onPollTimer`，这里不另写一份逻辑）。
    void runPollCycleOnce()
    {
        onPollTimer();
    }
    /// 外壳收到下载进度后灌进来；nullopt = 没有作业在跑，进度条整条消失。
    void setKernelProgress(const std::optional<Kernel::UpdateProgress> &progress);
    void setKernelBusy(bool busy);

    // 分流规则栏的可观察面（离屏断言用，和内核表同一套思路）。
    QString routingModeSubtitleText() const;
    QString routingModeDetailText() const;
    /// Rust `show_custom_rules`：模式不是「自定义规则」时，预设卡片和规则清单整段收起。
    bool customRulesVisible() const;
    QString configBusyNoticeText() const;
    QString rulesBusyNoticeText() const;
    QString injectedNoticeText() const;
    QString processNameWarningText() const;
    // 网络分栏的可观察面（离屏断言用）。
    QString networkNoticeText() const;
    bool networkControlsEnabled() const;
    bool networkTunGroupVisible() const;
    int networkDnsRowCount() const;
    QString networkDnsRowLabel(int index) const;
    QString ruleRowMatcherText(int row) const;
    /// 命中卡：空数据时整张卡不存在（Rust 的 `if !rule_hits_today.is_empty()`）。
    void setRuleHitsForTest(const QVector<DbTelemetry::RuleHitRow> &hits);
    bool ruleHitsCardVisible() const;
    QString ruleHitsSummaryText() const;
    QVector<RuleHitRowView> ruleHitsRows() const;
    /// 喂一批连接快照进命中统计（离屏测试用：真连接要内核在跑才拿得到）。
    void feedConnectionsForTest(const QVector<ConnectionHitInput> &connections);
    /// 立刻把攒下的增量写库（Rust 是 30 秒才刷一次，测试不等计时）。
    void flushRuleHitsForTest() { flushRuleHits(); }
    QString ruleRowActionText(int row) const;
    int ruleRowCount() const { return rulesTable_ == nullptr ? 0 : rulesTable_->rowCount(); }
    /// Rust 的 self.proxy_busy：配置作业在飞时整栏禁用并显示提示。页面自己的保存在飞
    /// 会自己置位，外壳有更大的动作（启停代理）时也能从外面灌进来。
    void setRulesBusy(bool busy);
    bool rulesBusy() const { return rulesBusy_; }
    /// 装载工作副本并同步控件：分流规则栏的断言都得先摆一份已知配置。
    void setRulesConfigForTest(const Proxy::MihomoConfigOptions &options);
    /// 网络分栏同样要能摆已知配置（禁用判据、TUN 段、DNS 行数都跟着它走）。
    void setNetworkConfigForTest(const Proxy::MihomoConfigOptions &options);

    // 选中节点
    QString selectedNodeId() const { return selectedNodeId_; }
    QString selectedNodeName() const { return selectedNodeName_; }
    void setSelectedNode(const QString &id, const QString &name);

    /// 状态卡那两行曾经一直是死的（只有初始文案），离屏断言从这里取证它们真的跟着选中节点变。
    QString statusNodeDetailText() const { return statusNodeDetail_ ? statusNodeDetail_->text() : QString(); }
    QString statusLatencyText() const { return statusLatency_ ? statusLatency_->text() : QString(); }
    bool healthStripVisible() const { return statusHealthStrip_ != nullptr && statusHealthStrip_->isVisible(); }
    /// 灌一段历史进曲线并立刻刷状态卡（离屏测试用：真曲线要代理在跑才有样本）。
    void setTrafficHistoryForTest(const QVector<QPair<double, double>> &points)
    {
        trafficHistory_ = points;
        refreshStatusCard();
    }
    int trafficPointCount() const { return trafficHistory_.size(); }
    /// 曲线与上下行读数各自所在行的 y（Rust 2026-10-08 把读数从第二行挪到第三行，
    /// 第二行整条让给流量图）。离屏断言用这两个数判定，不去猜文案。
    int statusTrafficY() const { return statusTraffic_ ? statusTraffic_->mapTo(this, QPoint(0, 0)).y() : -1; }
    int statusRatesY() const { return statusRates_ ? statusRates_->mapTo(this, QPoint(0, 0)).y() : -1; }
    QString switchLinkText() const { return switchLogLink_ ? switchLogLink_->text() : QString(); }
    /// 「查看切换记录」那颗标签本体。他要求它是绿字，而 `<a>` 的取色不看 QSS 的 `color`，
    /// 所以"到底画出来是什么色"只能拿真控件的像素判定。
    QWidget *switchLinkWidget() const { return switchLogLink_; }
    /// 页签标题（Rust 的「活跃连接 (N)」带数量）。
    QString panelTabTitle(int index) const { return panels_ ? panels_->tabText(index) : QString(); }

    /// 卡片右键菜单的构造（不弹出）。公开是为了让守卫能在**不弹模态框**的前提下断言
    /// 菜单里到底有哪几条 —— exec() 会阻塞测试，也会把窗口拍到桌面上。
    QMenu *buildNodeCardMenu(NodeCardFrame *card);

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
    /// 新装包第一次打开时：拉远端内核版本 + 补缺的内核 + 补缺的 geo 库（三样都齐就直接
    /// 返回，不敲 GitHub）。vcc 独有；Rust 要用户在内核面板手工点下载、手工点"一键更新"。
    /// 注意别把它写进 signals 区 —— 那是信号声明区，moc 会替它生成定义。
    void autoPrepareOnStartup();

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
    /// 重画网格：签名（数据版本/筛选/排序/列数/选中/代理状态）没变就一张都不动。
    void applyView();
    /// 上面那个签名，逐行混合，见 applyView 的注释。
    quint64 viewSignature(const Proxy::ProxyRuntimeStatus &status) const;
    int gridColumns() const;
    /// 卡片池：切筛选/搜索时按下标复用，只 bind 新内容，不重建控件树。
    /// status 由 applyView 整网格取一次传进来：每张卡都调 proxy().status() 会
    /// 反复抢 runtimeMutex_（代理启动/切换期间该锁被秒级持有）。
    void rebuildNodeGrid(const Proxy::ProxyRuntimeStatus &status);
    /// 卡片右键菜单（Rust proxy.rs:700 的 response.context_menu）：查看详情 / 测速节点 /
    /// IP 检测，自动选路开着且不是当前出口时再加一条「锁定为出口」。动作一律发信号，
    /// 由外壳执行 —— 与左键切换同一个规矩。
    void showNodeCardMenu(NodeCardFrame *card, const QPoint &globalPos);
    /// 自动选路（url-test）是否开着。Rust 那边是 proxy_config.url_test，vcc 的运行时配置同源。
    bool autoRoutingEnabled() const;
    /// 分页条：页码文案 + 上/下页可用位（对齐 Rust views/nodes.rs:350-373）。
    void updateNodePager();

    // 活跃连接
    struct ActiveConn {
        QString id;
        QString network;
        QString host;
        QString destination;
        QString process;
        QString rule;
        /// 规则把这条连接派给了谁（组名 / DIRECT）；命中统计按 (rule, target) 计数。
        QString target;
        quint64 upload = 0;
        quint64 download = 0;
    };
    void reloadConnections();
    void updateConnectionsTable();
    void closeConnection(const QString &id);
    void closeAllConnections();

    // 分流规则
    void reloadRules();
    /// 今日规则命中：连接快照按 id 去重累加，攒够 30 秒才落库一次（Rust 的 flush_rule_hits）。
    void recordConnectionHits();
    void flushRuleHits();
    void reloadRuleHits();
    void syncRulesWidgets();
    void refreshRulesTable();
    void updateRuleHitsView();
    /// 命中行的指纹：没变就一行都不重排。
    static QString ruleHitsRowsSignature(const RuleHitCard &card);
    void saveRules();
    void saveRoutingMode();
    void updateGeoData();
    void confirmRecommendedRules();
    void autoOptimizeRules();
    void prependPresetRules(const QVector<Proxy::TrafficRule> &preset);
    /// 副标题/详情/「预设+清单」整段的可见性，全部由当前分流模式决定。
    void applyRoutingModeView();
    /// 新增规则表单：TCP/UDP/QUIC 没有匹配值，PROCESS-NAME 只在 TUN 下有意义。
    void applyRuleDraftKindView();
    void applyInjectionNotice();
    void setRulesEditable(bool editable);

    // 网络设置
    void reloadNetwork();
    void syncNetworkWidgets();
    /// busy 或运行状态变了都要重算：整片禁用 + 那条“停止代理后可修改运行配置”。
    void applyNetworkEditable();
    /// 上游 DNS 行按 nameservers 重建（条数是动态的，对应 Rust 的逐条渲染）。
    void rebuildDnsRows();
    void saveNetworkConfig();

    // 内核表
    void reloadKernels(bool fetchRemote = false);
    void layoutKernelColumns();
    void refreshKernelRows();

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
    void reloadEgressHealth();
    QVector<DbTelemetry::HealthRow> healthRows_;
    qint64 healthFailStreak_ = 0;
    QString failoverReason_;
    /// 状态卡上的 24 小时出口健康条（只在运行/降级时出现，Rust proxy.rs:200-205）。
    HealthStrip *statusHealthStrip_ = nullptr;
    /// 状态卡第二行右侧的实时上下行曲线（Rust 把它画在端口读数后面，吃掉整行剩余宽度）。
    TrafficSparkline *statusTraffic_ = nullptr;
    QVector<QPair<double, double>> trafficHistory_;
    /// 「查看切换记录（今日 N 次）」：Rust 是点一下才查库，所以计数也缓存在这里。
    QLabel *switchLogLink_ = nullptr;
    qint64 switchToday_ = 0;
    qint64 switchTotal_ = 0;
    void openSwitchLog();
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
    int gridRebuilds_ = 0;
    QVector<NodeCardFrame *> nodeCards_;
    int nodePage_ = 1;
    int nodePageSize_ = 50;
    QLabel *nodePageLabel_ = nullptr;
    QPushButton *nodePrevPageBtn_ = nullptr;
    QPushButton *nodeNextPageBtn_ = nullptr;
    /// 上一轮网格占了几行：行拉伸因子要按它清干净，否则筛选后旧行还在撑高。
    int lastGridRows_ = 0;
    /// 曾经拉伸过的最大列数：列数变小时要据此把多出来的列的拉伸清掉。
    int gridColumnHighWater_ = 0;

    quint64 lastViewSignature_ = 0;

    // 活跃连接分栏
    QLineEdit *connFilterEdit_ = nullptr;
    QLabel *connCountLabel_ = nullptr;
    QPushButton *connClearFilterBtn_ = nullptr;
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
    QFrame *hitsCard_ = nullptr;
    QVector<DbTelemetry::RuleHitRow> ruleHits_;
    QHash<QString, QPair<QString, QString>> ruleSeen_;
    QMap<QPair<QString, QString>, qint64> ruleTally_;
    qint64 lastHitFlushMs_ = 0;
    bool ruleHitsRequested_ = false;
    QString lastRuleRowsSignature_;
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
    QLabel *routingModeSubtitleLabel_ = nullptr;
    QLabel *configBusyNoticeLabel_ = nullptr;
    QLabel *rulesBusyNoticeLabel_ = nullptr;
    QLabel *injectedNoticeLabel_ = nullptr;
    QLabel *processNameWarnLabel_ = nullptr;
    QWidget *customRulesSection_ = nullptr;
    QDialog *recommendedConfirmDialog_ = nullptr;
    QList<QWidget *> rulesEditableWidgets_;
    bool rulesBusy_ = false;
    bool rulesLoaded_ = false;
    /// 正在整表重灌规则：这期间 QTableWidgetItem 的 itemChanged 不是用户改动。
    bool syncingRulesTable_ = false;

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
    /// DNS/TUN 栏里的三块内块：运行配置、TUN 参数、DNS。每块都得有自己的底。
    QFrame *netRunBlock_ = nullptr;
    QFrame *netDnsBlock_ = nullptr;
    QComboBox *netTunStackCombo_ = nullptr;
    QCheckBox *netTunAutoRouteCheck_ = nullptr;
    QCheckBox *netTunAutoDetectCheck_ = nullptr;
    QCheckBox *netDnsEnabledCheck_ = nullptr;
    QCheckBox *netDnsIpv6Check_ = nullptr;
    QComboBox *netDnsEnhancedCombo_ = nullptr;
    QWidget *netEditableHost_ = nullptr;
    QWidget *dnsRowsHost_ = nullptr;
    QVBoxLayout *dnsRowsBox_ = nullptr;
    QVector<QLineEdit *> dnsEdits_;
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
    /// 本地内核探测（跑子进程，318 ms）在后台线程还没回来。
    bool kernelDetecting_ = false;

    // 运行日志分栏
    QLabel *logSubtitleLabel_ = nullptr;
    QLabel *logEmptyLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QCheckBox *logAutoScrollCheck_ = nullptr;
    QPushButton *logCopyBtn_ = nullptr;
    QPushButton *logClearBtn_ = nullptr;
};

}
