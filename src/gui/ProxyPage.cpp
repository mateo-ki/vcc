#include "gui/NodeCard.h"
#include "gui/ProxyPage.h"
#include "core/TimeUtil.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QFont>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "app/AppContext.h"
#include "core/Log.h"
#include "db/Database.h"
#include "geo/Geo.h"
#include "gui/CountryNames.h"
#include "gui/SwitchLogDialog.h"
#include "gui/FlowLayout.h"
#include "gui/IpRiskDialog.h"
#include "gui/KernelDownloadDialog.h"
#include "gui/KernelPanel.h"
#include "gui/NodeText.h"
#include "gui/Prepare.h"
#include "gui/Theme.h"
#include "kernel/KernelSource.h"
#include "updater/KernelUpdater.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "kernel/KernelManager.h"
#include "net/HttpClient.h"
#include "proxy/ConfigTypes.h"
#include "proxy/MihomoConfig.h"
#include "proxy/ProxyManager.h"
#include "sysproxy/Format.h"
#include "updater/KernelUpdater.h"

namespace {

/// 规则表"动作"列的委托：单元格只存文本，点进去才现造一颗 QComboBox（Qt 一次只开一个
/// 编辑器）。
/// Why：以前每行常驻一颗下拉框，实测"造控件 + 填三项 + 塞进表格"要 ~14 ms/行，120 条
/// 规则光是进这一栏就是 1.7 s —— 这就是"点进分流规则卡一下"的大头。Rust 那边是 egui
/// 即时模式：行上只画当前动作，点开才展开，不保留任何控件；委托编辑器是 Qt 侧等价的形状，
/// 成本从"每行一份"变成"每次编辑一份"。
class RuleActionDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = Gui::makeComboBox(parent);
        for (const Proxy::RuleAction action : {Proxy::RuleAction::Direct, Proxy::RuleAction::Proxy,
                                               Proxy::RuleAction::Reject}) {
            combo->addItem(Gui::ruleActionText(action), int(action));
        }
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (combo == nullptr) {
            return;
        }
        combo->setCurrentIndex(index.data(Qt::UserRole).toInt());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        const auto *combo = qobject_cast<const QComboBox *>(editor);
        if (combo == nullptr) {
            return;
        }
        const int action = combo->currentData().toInt();
        model->setData(index, action, Qt::UserRole);
        model->setData(index, Gui::ruleActionText(Proxy::RuleAction(action)), Qt::DisplayRole);
    }

    /// 这一格看着仍要像个下拉：文本右边补一颗向下的小箭头（Qt 默认只画文本）。
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem tuned = option;
        initStyleOption(&tuned, index);
        QStyledItemDelegate::paint(painter, tuned, index);
        if (tuned.widget == nullptr) {
            // 离屏 grab() 之类的调用方可能不带 widget，箭头是"锦上添花"，没 widget 就到此为止。
            return;
        }

        QStyleOption arrow;
        arrow.initFrom(tuned.widget);
        const int size = 8;
        arrow.rect = QRect(tuned.rect.right() - size - 8, tuned.rect.center().y() - size / 2, size,
                           size);
        tuned.widget->style()->drawPrimitive(QStyle::PE_IndicatorArrowDown, &arrow, painter,
                                             tuned.widget);
    }
};

QString rateLineText(quint64 down, quint64 up)
{
    // Rust 的速率是两个图标 + 两个数值；Qt 侧只有一行文本，箭头按码点给（不手打）。
    return QStringLiteral("%1 %2 %3 %4")
        .arg(QString(QChar(0x2193)), Gui::formatSpeed(down), QString(QChar(0x2191)),
             Gui::formatSpeed(up));
}


bool anyKernelInstalled()
{
    for (Kernel::Kind kind : {Kernel::Kind::Mihomo, Kernel::Kind::SingBox, Kernel::Kind::Xray}) {
        if (Kernel::kernelIsAvailable(kind)) {
            return true;
        }
    }
    return false;
}

/// Rust 的 ui.strong("……")：同字号加粗的小标题，不是 section_title 那一级。
QLabel *makeStrongLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel *makeSectionTitle(const QString &title, const QString &subtitle, QWidget *parent,
                         QLabel **subtitleOut = nullptr)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(2);

    auto *t = new QLabel(title, container);
    t->setObjectName(QStringLiteral("Title"));
    t->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700;"));
    layout->addWidget(t);

    if (!subtitle.isEmpty()) {
        auto *s = new QLabel(subtitle, container);
        s->setObjectName(QStringLiteral("Subtitle"));
        Gui::Theme::setWidgetCss(s, QStringLiteral("font-size: 12px; color: {muted};"));
        s->setWordWrap(true);
        layout->addWidget(s);
        if (subtitleOut != nullptr) {
            *subtitleOut = s;
        }
    }

    if (parent != nullptr && parent->layout() != nullptr) {
        parent->layout()->addWidget(container);
    }
    return t;
}

} // namespace

namespace Gui {

StatusBadge statusBadgeFor(Proxy::ProxyState state, bool hasKernel, bool proxyActive)
{
    if (Prepare::isPrepared(hasKernel, proxyActive)) {
        return {UiText::prepared(), QStringLiteral("Accent")};
    }
    switch (state) {
    case Proxy::ProxyState::Stopped:
        return {UiText::stateStopped(), QStringLiteral("Muted")};
    case Proxy::ProxyState::Starting:
        return {UiText::stateStarting(), QStringLiteral("Warning")};
    case Proxy::ProxyState::Running:
        return {UiText::stateRunning(), QStringLiteral("Success")};
    case Proxy::ProxyState::Degraded:
        return {UiText::stateDegraded(), QStringLiteral("Warning")};
    case Proxy::ProxyState::Stopping:
        return {UiText::stateStopping(), QStringLiteral("Warning")};
    case Proxy::ProxyState::Failed:
        return {UiText::stateFailed(), QStringLiteral("Danger")};
    }
    return {UiText::stateStopped(), QStringLiteral("Muted")};
}

ProxyPage::ProxyPage(App::AppContext &context, QWidget *parent)
    : Page(context, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);

    // 顶部状态监控卡片
    root->addWidget(buildStatusCard());

    // 六个分栏选项卡
    panels_ = new QTabBar(this);
    panels_->setExpanding(false);
    panels_->setDrawBase(false);
    panels_->addTab(UiText::panelNodes());
    panels_->addTab(UiText::panelConnections());
    panels_->addTab(UiText::panelRules());
    panels_->addTab(UiText::panelNetwork());
    panels_->addTab(UiText::panelKernels());
    panels_->addTab(UiText::panelLogs());
    root->addWidget(panels_);
    root->addSpacing(8);

    // 六个分栏堆叠
    stack_ = new QStackedWidget(this);
    auto *nodesPanel = buildNodesPanel();
    stack_->addWidget(nodesPanel);
    stack_->addWidget(buildConnectionsPanel());
    stack_->addWidget(buildRulesPanel());
    stack_->addWidget(buildNetworkPanel());
    stack_->addWidget(buildKernelsPanel());
    stack_->addWidget(buildLogsPanel());
    nodesPanel->lower();

    connect(panels_, &QTabBar::currentChanged, this, [this](int index) {
        stack_->setCurrentIndex(index);
        onPanelSwitch();
    });
    root->addWidget(stack_, 1);

    // 定时轮询：每 1.5 秒更新流量读数与连接状态
    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &ProxyPage::onPollTimer);
    pollTimer_->start(1500);
    // 出口实测本身住到外壳（`MainWindow::checkFailover`，Rust 的 check_failover 也在应用层）：
    // 一次探测要同时喂状态卡那行「实测 …」、健康条和断路器判定，两处各探一次就是两份真相。
    // 这里只负责开机读一次历史，否则重启后这一行要空白整整一分钟。
    reloadEgressHealth();
    // 打开控制台就把缺的内核补上（后台一条任务，不挡界面；三颗都齐时它直接返回）。
    autoPrepareOnStartup();

    // 六个分栏是这儿一次性建出来的，输入框/旋钮/勾选框/单选钮/下拉框统一在这里挂自带样式。
    // 注意别放进 refresh()：那是 1.5 秒一次的轮询，重挂 QSS 等于全页重解析样式。
    Gui::decorateFieldsIn(this);

    refresh();
}

ProxyPage::~ProxyPage() = default;

namespace {

/// 库里的 `at` 是 UTC 纳秒时间戳字符串，表头只要"几点几分"（Rust 的 clock_of）。
/// 认两种形状：ISO 的 "...THH:MM..." 和已经截好的 "HH:MM..."；都不认就原样给出去，
/// 至少不骗人。
// clockOf 在 gui/Widgets.h（状态卡与健康条/切换记录共用一份）。

} // namespace

QWidget *ProxyPage::buildStatusCard()
{
    auto *card = new QFrame(this);
    Theme::decorateCard(card);
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(Theme::kPadCard, 14, Theme::kPadCard, 14);
    box->setSpacing(8);

    // 第一行：状态徽章 · 节点名称 · 节点详情 · 连通健康状态
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(10);

    stateBadge_ = new QLabel(card);
    row1->addWidget(stateBadge_);

    statusNodeName_ = new QLabel(QStringLiteral("未选择代理节点"), card);
    statusNodeName_->setObjectName(QStringLiteral("Title"));
    statusNodeName_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700;"));
    row1->addWidget(statusNodeName_);

    statusNodeDetail_ = new QLabel(card);
    statusNodeDetail_->setObjectName(QStringLiteral("Subtitle"));
    row1->addWidget(statusNodeDetail_);

    row1->addStretch();

    statusHealth_ = new QLabel(card);
    statusHealth_->setObjectName(QStringLiteral("Subtitle"));
    row1->addWidget(statusHealth_);

    // 24 小时出口健康条：Rust 把它接在同一行「实测 …」后面（proxy.rs:200-205），
    // 只有运行/降级才出现 —— 没在跑的时候画一条全灰的格子是废话。
    statusHealthStrip_ = new HealthStrip(card);
    statusHealthStrip_->setVisible(false);
    row1->addWidget(statusHealthStrip_);

    stateMessage_ = new QLabel(card);
    stateMessage_->setObjectName(QStringLiteral("Subtitle"));
    stateMessage_->setVisible(false);
    row1->addWidget(stateMessage_);

    box->addLayout(row1);

    // 分隔线
    auto *sep = new QFrame(card);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName(QStringLiteral("Muted"));
    Gui::Theme::setWidgetCss(sep, QStringLiteral("color: {borderSubtle}; max-height: 1px;"));
    box->addWidget(sep);

    // 第二行：延迟 · 活跃连接数 · 端口配置 · 实时速率
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(12);

    statusLatency_ = new QLabel(QStringLiteral("延迟: -"), card);
    statusLatency_->setObjectName(QStringLiteral("Subtitle"));
    row2->addWidget(statusLatency_);

    auto *dot1 = new QLabel(QStringLiteral("·"), card);
    dot1->setObjectName(QStringLiteral("Muted"));
    row2->addWidget(dot1);

    statusConnCount_ = new QLabel(QStringLiteral("活跃连接: 0"), card);
    statusConnCount_->setObjectName(QStringLiteral("Subtitle"));
    row2->addWidget(statusConnCount_);

    auto *dot2 = new QLabel(QStringLiteral("·"), card);
    dot2->setObjectName(QStringLiteral("Muted"));
    row2->addWidget(dot2);

    kernelInfo_ = new QLabel(card);
    kernelInfo_->setObjectName(QStringLiteral("Muted"));
    row2->addWidget(kernelInfo_);

    // 曲线吃掉这一行剩下的整条宽度（Rust proxy.rs:247：读数在左，图在右）。
    statusTraffic_ = new TrafficSparkline(card);
    row2->addWidget(statusTraffic_, 1);

    box->addLayout(row2);

    // 第三行：自动切换状态 · 会话总计
    auto *row3 = new QHBoxLayout();
    row3->setSpacing(12);

    statusFailover_ = new QLabel(QStringLiteral("自动切换: 本次运行已切换 0 次"), card);
    statusFailover_->setObjectName(QStringLiteral("Muted"));
    statusFailover_->setStyleSheet(QStringLiteral("font-size: 11px;"));
    row3->addWidget(statusFailover_);

    // 「查看切换记录（今日 N 次）」紧跟在自动切换后面（Rust proxy.rs:284-291 的 ui.link）。
    switchLogLink_ = new QLabel(card);
    switchLogLink_->setTextFormat(Qt::RichText);
    switchLogLink_->setCursor(Qt::PointingHandCursor);
    Gui::Theme::setWidgetCss(switchLogLink_, QStringLiteral("color: {success}; font-size: 11px;"));
    connect(switchLogLink_, &QLabel::linkActivated, this, [this](const QString &) { openSwitchLog(); });
    row3->addWidget(switchLogLink_);

    // 上下行读数从第二行挪到这里：Rust 2026-10-08 把它排在切换记录之后、会话累计之前。
    statusRates_ = new QLabel(rateLineText(0, 0), card);
    statusRates_->setObjectName(QStringLiteral("Subtitle"));
    statusRates_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row3->addWidget(statusRates_);

    row3->addStretch();

    statusSessionTraffic_ = new QLabel(UiText::sessionTraffic()
                                            .arg(QStringLiteral("0 B"), QStringLiteral("0 B")), card);
    statusSessionTraffic_->setObjectName(QStringLiteral("Muted"));
    statusSessionTraffic_->setStyleSheet(QStringLiteral("font-size: 11px;"));
    row3->addWidget(statusSessionTraffic_);

    box->addLayout(row3);

    return card;
}

void ProxyPage::refreshStatusCard()
{
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();
    const bool isRunning = status.localProxyActive();
    const bool hasKernel = anyKernelInstalled();

    const StatusBadge badge = statusBadgeFor(status.state, hasKernel, isRunning);
    if (stateBadge_ != nullptr) {
        stateBadge_->setText(badge.text);
        if (stateBadge_->objectName() != badge.role) {
            stateBadge_->setObjectName(badge.role);
            stateBadge_->style()->unpolish(stateBadge_);
            stateBadge_->style()->polish(stateBadge_);
        }
    }

    if (stateMessage_ != nullptr) {
        stateMessage_->setText(status.message.isEmpty() ? badge.text : status.message);
    }

    if (kernelInfo_ != nullptr) {
        QString text;
        if (status.kernel.has_value()) {
            text += Kernel::kindDisplayName(*status.kernel) + QStringLiteral(" · ");
        }
        text += QStringLiteral("mixed %1 · controller %2")
                    .arg(status.mixedPort)
                    .arg(status.controllerPort);
        kernelInfo_->setText(text);
    }

    if (statusNodeName_ != nullptr) {
        statusNodeName_->setText(selectedNodeName_.isEmpty() ? QStringLiteral("未选择代理节点") : selectedNodeName_);
    }

    // 选中节点那一行与「延迟:」：Rust 用的是 self.nodes 里的整条节点（views/proxy.rs:126-140
    // 与 216-228），不是筛选后的网格 —— 选中的节点被地区/搜索过滤掉时，这两行照样要说真话。
    const Models::NodeSummary *selected = nullptr;
    for (const Models::NodeSummary &node : allNodes_) {
        if (node.id == selectedNodeId_) {
            selected = &node;
            break;
        }
    }
    if (statusNodeDetail_ != nullptr) {
        if (selected == nullptr) {
            // Rust 那边没有选中节点时整段不渲染，不是打一个"-"。
            statusNodeDetail_->clear();
        } else {
            const CountryDisplay country = countryFlagAndName(selected->country);
            statusNodeDetail_->setText(QStringLiteral("%1 %2 · %3:%4")
                                           .arg(country.flag, country.name, selected->server)
                                           .arg(selected->port));
        }
    }
    if (statusLatency_ != nullptr) {
        const QString latency = selected != nullptr && selected->latencyMs.has_value()
            ? QStringLiteral("%1 ms").arg(*selected->latencyMs) : QStringLiteral("-");
        statusLatency_->setText(QStringLiteral("延迟: %1").arg(latency));
    }

    if (statusHealth_ != nullptr) {
        // 状态阶梯照抄 Rust views/proxy.rs:143-186：lastError 优先，其次 Running 时看
        // 最近一次实测（成功=「实测 HH:MM ✓ 218 ms」，失败=「实测 HH:MM ✗ …（连续 N 次）」，
        // 一条都没有=「已启动 · 等待首次实测（≤60 秒）」），再其次 Degraded=「降级运行中」。
        // 「已连通」必须以实测为准，不能只看 ProxyState —— 内核自己说在跑不代表出口通。
        QString text;
        QString targetRole = QStringLiteral("Muted");
        if (!status.lastError.isEmpty()) {
            text = status.lastError;
            targetRole = QStringLiteral("Danger");
        } else if (isRunning) {
            if (healthRows_.isEmpty()) {
                text = QStringLiteral("已启动 · 等待首次实测（≤60 秒）");
            } else if (healthRows_.first().ok == 1) {
                const QString ms = healthRows_.first().latencyMs.has_value()
                    ? QStringLiteral("%1 ms").arg(*healthRows_.first().latencyMs)
                    : QStringLiteral("—");
                text = QStringLiteral("实测 %1 ✓ %2").arg(clockOf(healthRows_.first().at), ms);
                targetRole = QStringLiteral("Success");
            } else {
                const QString why = healthRows_.first().error.isEmpty()
                    ? QStringLiteral("不通") : healthRows_.first().error;
                text = QStringLiteral("实测 %1 ✗ %2（连续 %3 次）")
                           .arg(clockOf(healthRows_.first().at), why)
                           .arg(qMax<qint64>(healthFailStreak_, 1));
                targetRole = QStringLiteral("Danger");
            }
        } else if (status.state == Proxy::ProxyState::Degraded) {
            text = QStringLiteral("降级运行中");
            targetRole = QStringLiteral("Warning");
        } else {
            text = QStringLiteral("未运行");
        }
        statusHealth_->setText(text);
        if (statusHealth_->objectName() != targetRole) {
            statusHealth_->setObjectName(targetRole);
            statusHealth_->style()->unpolish(statusHealth_);
            statusHealth_->style()->polish(statusHealth_);
        }
    }

    if (switchLogLink_ != nullptr) {
        // 锚点自己的取色走 Qt 富文本的链接色（蓝），QSS 的 `color` 压不住它，所以把
        // 调色板的 success 直接写进 style。refresh() 每 1.5 秒重跑，换主题后下一拍跟上新色。
        switchLogLink_->setText(
            QStringLiteral("<a href=\"switch\" style=\"color:%1; text-decoration:none;\">%2</a>")
                .arg(Gui::Theme::current().success.name(QColor::HexRgb),
                     UiText::switchLogLink().arg(switchToday_)));
    }

    if (statusTraffic_ != nullptr) {
        // Rust 的 running 是**严格** Running（mod.rs:1301）：降级/启动中都不再采样，
        // 并且一旦离开 Running 就把读数和整条历史抹平，不留旧曲线冒充实时。
        const bool polling = status.state == Proxy::ProxyState::Running;
        if (!polling && (!trafficHistory_.isEmpty() || downloadRate_ > 0 || uploadRate_ > 0)) {
            trafficHistory_.clear();
            downloadRate_ = 0;
            uploadRate_ = 0;
        }
        statusTraffic_->setActive(polling);
        statusTraffic_->setHistory(trafficHistory_);
    }

    if (statusHealthStrip_ != nullptr) {
        const bool showStrip = isRunning || status.state == Proxy::ProxyState::Degraded;
        statusHealthStrip_->setVisible(showStrip);
        if (showStrip) {
            statusHealthStrip_->setBuckets(
                healthHourBuckets(healthRows_, TimeUtil::nowUtcNanos()));
        }
    }

    if (statusConnCount_ != nullptr) {
        statusConnCount_->setText(QStringLiteral("活跃连接: %1").arg(connections_.size()));
    }

    if (statusRates_ != nullptr) {
        if (isRunning) {
            statusRates_->setText(rateLineText(downloadRate_, uploadRate_));
        } else {
            statusRates_->setText(rateLineText(0, 0));
        }
    }

    if (statusSessionTraffic_ != nullptr) {
        statusSessionTraffic_->setText(UiText::sessionTraffic()
                                           .arg(formatBytes(uploadTotal_), formatBytes(downloadTotal_)));
    }

    if (statusFailover_ != nullptr) {
        // Rust views/proxy.rs:268-283：整行还要装速率与会话累计，判定动辄六七十字（节点名
        // 在里面）会把后面的读数折到第二行，所以超过 30 个字就截断加省略号。
        QString line = QStringLiteral("自动切换: 本次运行已切换 %1 次").arg(failoverSwitches_);
        if (!failoverReason_.isEmpty()) {
            QString detail = failoverReason_;
            if (detail.size() > 30) {
                detail = detail.left(30) + QStringLiteral("…");
            }
            line += QStringLiteral(" · %1").arg(detail);
        }
        statusFailover_->setText(line);
    }
}

void ProxyPage::setFailoverStatus(int switches, const QString &lastReason)
{
    failoverSwitches_ = switches;
    failoverReason_ = lastReason;
    refreshStatusCard();
}

QVector<Router::BackupCandidate> ProxyPage::backupCandidates() const
{
    // Rust 的 begin_failover_switch 吃的是 self.proxy_nodes —— 也就是这一页按最新测速
    // 看到的这批节点，不是整库。
    QVector<Router::BackupCandidate> out;
    out.reserve(allNodes_.size());
    for (const Models::NodeSummary &node : allNodes_) {
        Router::BackupCandidate candidate;
        candidate.id = node.id;
        candidate.name = node.name;
        candidate.host = node.server;
        candidate.port = node.port;
        if (node.latencyMs.has_value() && *node.latencyMs >= 0) {
            candidate.lastLatencyMs = quint64(*node.latencyMs);
        }
        out.push_back(candidate);
    }
    return out;
}

void ProxyPage::reloadEgressHealth()
{
    const auto rows = DbTelemetry::recentEgressHealth(context_.database(), 24);
    healthRows_ = rows.isOk() ? rows.value() : QVector<DbTelemetry::HealthRow>();
    const auto streak = DbTelemetry::egressFailStreak(context_.database());
    healthFailStreak_ = streak.isOk() ? streak.value() : 0;
}

void ProxyPage::setSelectedNode(const QString &id, const QString &name)
{
    selectedNodeId_ = id;
    selectedNodeName_ = name;
    refreshStatusCard();
    if (currentPanel() == Panel::Nodes) {
        applyView();
    }
}

QWidget *ProxyPage::buildNodesPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("NodesPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(12);

    // 过滤与排序综合卡片栏（与菜单栏保持优雅间距并提供干净的卡片背景）
    auto *filterCard = new QFrame(panel);
    Theme::decorateCard(filterCard);
    filterCard->setObjectName(QStringLiteral("FilterBarCard"));
    Gui::Theme::setWidgetCss(filterCard, QStringLiteral(
        "QFrame#FilterBarCard { background-color: {panel}; border: 1px solid {borderSubtle}; border-radius: 12px; }"
    ));
    auto *filterBox = new QVBoxLayout(filterCard);
    filterBox->setContentsMargins(14, 10, 14, 10);
    filterBox->setSpacing(8);

    // 地区筛选药丸
    auto *regionRow = new QWidget(filterCard);
    auto *regionFlow = new FlowLayout(regionRow, 0, 6, 6);
    auto *regionCaption = new QLabel(UiText::regionLabel(), regionRow);
    regionCaption->setObjectName(QStringLiteral("Subtitle"));
    Gui::Theme::setWidgetCss(regionCaption, QStringLiteral("font-weight: 600; font-size: 13px; color: {textSecondary};"));
    regionFlow->addWidget(regionCaption);

    const QStringList codes = regionPillCodes();
    const QVector<QString> labels = {UiText::regionAll(), UiText::regionHK(),
                                     UiText::regionJP(),  UiText::regionUS(),
                                     UiText::regionSG(),  UiText::regionTW(),
                                     UiText::regionKR(),  UiText::regionOther()};
    for (int index = 0; index < codes.size(); ++index) {
        const QString code = codes.at(index);
        auto *pill = new QPushButton(labels.at(index), regionRow);
        pill->setObjectName(QStringLiteral("Pill"));
        pill->setCheckable(true);
        pill->setProperty("region", code);
        pill->setChecked(code == regionFilter_);
        connect(pill, &QPushButton::clicked, this, [this, code]() {
            nodePage_ = 1;
            regionFilter_ = code;
            for (QPushButton *other : regionPills_) {
                other->setChecked(other->property("region").toString() == code);
            }
            applyView();
        });
        regionPills_.push_back(pill);
        regionFlow->addWidget(pill);
    }
    filterBox->addWidget(regionRow);

    // 排序与搜索栏
    auto *sortRow = new QWidget(filterCard);
    auto *sortFlow = new FlowLayout(sortRow, 0, 6, 6);
    auto *sortCaption = new QLabel(UiText::sortLabel(), sortRow);
    sortCaption->setObjectName(QStringLiteral("Muted"));
    Gui::Theme::setWidgetCss(sortCaption, QStringLiteral("font-weight: 600; font-size: 13px; color: {textSecondary};"));
    sortFlow->addWidget(sortCaption);

    sortLatency_ = new QPushButton(UiText::sortByLatency(), sortRow);
    sortLatency_->setObjectName(QStringLiteral("Pill"));
    sortLatency_->setCheckable(true);
    sortLatency_->setChecked(sortBy_ == ProxySortBy::Latency);
    sortName_ = new QPushButton(UiText::sortByName(), sortRow);
    sortName_->setObjectName(QStringLiteral("Pill"));
    sortName_->setCheckable(true);
    sortName_->setChecked(sortBy_ == ProxySortBy::Name);

    connect(sortLatency_, &QPushButton::clicked, this, [this]() {
        nodePage_ = 1;
        sortBy_ = ProxySortBy::Latency;
        sortLatency_->setChecked(true);
        sortName_->setChecked(false);
        applyView();
    });
    connect(sortName_, &QPushButton::clicked, this, [this]() {
        nodePage_ = 1;
        sortBy_ = ProxySortBy::Name;
        sortLatency_->setChecked(false);
        sortName_->setChecked(true);
        applyView();
    });

    sortFlow->addWidget(sortLatency_);
    sortFlow->addWidget(sortName_);

    countLabel_ = new QLabel(countText(), sortRow);
    countLabel_->setObjectName(QStringLiteral("Muted"));
    sortFlow->addWidget(countLabel_);

    searchEdit_ = new QLineEdit(sortRow);
    searchEdit_->setPlaceholderText(UiText::searchHint());
    searchEdit_->setFixedWidth(180);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        nodePage_ = 1;
        search_ = text.trimmed().toLower();
        applyView();
    });
    sortFlow->addWidget(searchEdit_);

    filterBox->addWidget(sortRow);
    box->addWidget(filterCard);

    // 节点网格滚动区
    nodeScroll_ = new QScrollArea(panel);
    nodeScroll_->setWidgetResizable(true);
    nodeScroll_->setFrameShape(QFrame::NoFrame);
    nodeScroll_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    gridHost_ = new QWidget(nodeScroll_);
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 6, 0, 8);
    grid_->setSpacing(12);
    nodeScroll_->setWidget(gridHost_);
    box->addWidget(nodeScroll_, 1);

    auto *pagerRow = new QHBoxLayout();
    pagerRow->setSpacing(8);
    nodePageLabel_ = new QLabel(panel);
    nodePageLabel_->setObjectName(QStringLiteral("Muted"));
    pagerRow->addStretch();
    pagerRow->addWidget(nodePageLabel_);
    nodePrevPageBtn_ = new QPushButton(UiText::pagerPrev(), panel);
    nodePrevPageBtn_->setObjectName(QStringLiteral("PagerPrev"));
    connect(nodePrevPageBtn_, &QPushButton::clicked, this, [this]() {
        if (nodePage_ > 1) {
            nodePage_ -= 1;
            applyView();
        }
    });
    pagerRow->addWidget(nodePrevPageBtn_);
    nodeNextPageBtn_ = new QPushButton(UiText::pagerNext(), panel);
    nodeNextPageBtn_->setObjectName(QStringLiteral("PagerNext"));
    connect(nodeNextPageBtn_, &QPushButton::clicked, this, [this]() {
        if (nodePage_ < nodePageCount()) {
            nodePage_ += 1;
            applyView();
        }
    });
    pagerRow->addWidget(nodeNextPageBtn_);
    box->addLayout(pagerRow);
    updateNodePager();

    return panel;
}

QWidget *ProxyPage::buildConnectionsPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("ConnectionsPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);

    makeSectionTitle(QStringLiteral("活跃连接监控"),
                     QStringLiteral("实时查看走代理与直连的进程、目标域名及传输流量，支持一键断开异常连接"),
                     panel);

    auto *bar = new QHBoxLayout();
    bar->setSpacing(8);

    auto *filterLabel = new QLabel(UiText::connFilterLabel(), panel);
    filterLabel->setObjectName(QStringLiteral("Subtitle"));
    bar->addWidget(filterLabel);

    connFilterEdit_ = new QLineEdit(panel);
    connFilterEdit_->setProperty("testName", QStringLiteral("ConnFilterEdit"));
    connFilterEdit_->setPlaceholderText(QStringLiteral("按进程、域名或 IP 过滤"));
    connFilterEdit_->setFixedWidth(200);
    connect(connFilterEdit_, &QLineEdit::textChanged, this, [this]() { updateConnectionsTable(); });
    bar->addWidget(connFilterEdit_);

    // Rust 只在筛选框非空时才画"清空"这颗。
    connClearFilterBtn_ = new QPushButton(QStringLiteral("清空"), panel);
    connect(connClearFilterBtn_, &QPushButton::clicked, this, [this]() {
        if (connFilterEdit_ != nullptr) {
            connFilterEdit_->clear();
        }
    });
    connClearFilterBtn_->setProperty("testName", QStringLiteral("ClearFilterButton"));
    connClearFilterBtn_->setVisible(false);
    bar->addWidget(connClearFilterBtn_);

    bar->addSpacing(12);

    connCountLabel_ = new QLabel(QStringLiteral("当前连接总数: 0"), panel);
    connCountLabel_->setObjectName(QStringLiteral("Subtitle"));
    bar->addWidget(connCountLabel_);

    bar->addStretch();

    connCloseAllBtn_ = new QPushButton(QStringLiteral("断开全部连接"), panel);
    connCloseAllBtn_->setObjectName(QStringLiteral("Danger"));
    connCloseAllBtn_->setToolTip(QStringLiteral("向内核发送指令关闭所有活动的网络连接"));
    connect(connCloseAllBtn_, &QPushButton::clicked, this, &ProxyPage::closeAllConnections);
    bar->addWidget(connCloseAllBtn_);

    box->addLayout(bar);

    connEmptyCard_ = new QFrame(panel);
    Theme::decorateCard(static_cast<QFrame *>(connEmptyCard_));
    auto *emptyBox = new QVBoxLayout(connEmptyCard_);
    emptyBox->setContentsMargins(18, 24, 18, 24);
    connEmptyLabel_ = new QLabel(UiText::connEmptyStopped(), connEmptyCard_);
    connEmptyLabel_->setObjectName(QStringLiteral("Muted"));
    connEmptyLabel_->setAlignment(Qt::AlignCenter);
    emptyBox->addWidget(connEmptyLabel_);
    box->addWidget(connEmptyCard_);

    connTable_ = new QTableWidget(panel);
    connTable_->setColumnCount(7);
    connTable_->setHorizontalHeaderLabels({QStringLiteral("网络"), QStringLiteral("进程"),
                                           QStringLiteral("目标地址"), QStringLiteral("分流规则"),
                                           QStringLiteral("上传"), QStringLiteral("下载"),
                                           QStringLiteral("操作")});
    connTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    connTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    connTable_->horizontalHeader()->setStretchLastSection(false);
    connTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    connTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connTable_->verticalHeader()->setVisible(false);
    connTable_->setShowGrid(false);
    connTable_->setAlternatingRowColors(true);
    connTable_->verticalHeader()->setDefaultSectionSize(38);
    connTable_->setColumnWidth(0, 65);
    connTable_->setColumnWidth(1, 150);
    connTable_->setColumnWidth(3, 140);
    connTable_->setColumnWidth(4, 95);
    connTable_->setColumnWidth(5, 95);
    connTable_->setColumnWidth(6, 85);
    box->addWidget(connTable_, 1);

    connTable_->setVisible(false);
    return panel;
}

void ProxyPage::reloadConnections()
{
    const auto status = context_.proxy().status();
    if (!status.localProxyActive()) {
        connections_.clear();
        if (currentPanel() == Panel::Connections) {
            updateConnectionsTable();
        }
        return;
    }

    const quint16 port = status.controllerPort;
    const QString secret = status.controllerSecret;

    auto connectionsBox = std::make_shared<QVector<ActiveConn>>();
    auto uploadTotalBox = std::make_shared<quint64>(0);
    auto downloadTotalBox = std::make_shared<quint64>(0);

    context_.runAsync(
        [port, secret, connectionsBox, uploadTotalBox, downloadTotalBox]() {
            Net::Request request;
            request.url = QStringLiteral("http://127.0.0.1:%1/connections").arg(port);
            request.method = QByteArrayLiteral("GET");
            request.timeoutMs = 1200;
            if (!secret.isEmpty()) {
                request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(secret)});
            }
            const auto res = Net::send(request);
            if (!res.isOk() || !res.value().isSuccess()) {
                return;
            }
            try {
                const auto json = Json::parse(res.value().body.toStdString());
                if (json.contains("uploadTotal") && json["uploadTotal"].is_number()) {
                    *uploadTotalBox = json["uploadTotal"].get<quint64>();
                }
                if (json.contains("downloadTotal") && json["downloadTotal"].is_number()) {
                    *downloadTotalBox = json["downloadTotal"].get<quint64>();
                }
                if (json.contains("connections") && json["connections"].is_array()) {
                    for (const auto &item : json["connections"]) {
                        ActiveConn conn;
                        conn.id = JsonUtil::textOrEmpty(item, "id");
                        conn.upload = JsonUtil::uintOr(item, "upload", 0);
                        conn.download = JsonUtil::uintOr(item, "download", 0);

                        // Rust 的 BgMsg 装配：rule 与 rulePayload 都在才拼成 "R(P)"，
                        // 只有 rule 就单写，两个都没有是 "-"；target 取 payload，空则 "-"。
                        const QString rule = JsonUtil::textOrEmpty(item, "rule");
                        const QString payload = JsonUtil::textOrEmpty(item, "rulePayload");
                        if (!rule.isEmpty() && !payload.isEmpty()) {
                            conn.rule = QStringLiteral("%1(%2)").arg(rule, payload);
                        } else if (!rule.isEmpty()) {
                            conn.rule = rule;
                        } else {
                            conn.rule = QStringLiteral("-");
                        }
                        conn.target = payload.isEmpty() ? QStringLiteral("-") : payload;

                        if (item.contains("metadata") && item["metadata"].is_object()) {
                            const auto &meta = item["metadata"];
                            conn.network = JsonUtil::textOrEmpty(meta, "network");
                            if (conn.network.isEmpty()) {
                                conn.network = QStringLiteral("tcp");
                            }
                            conn.host = JsonUtil::textOrEmpty(meta, "host");
                            const QString dstIp = JsonUtil::textOrEmpty(meta, "destinationIP");
                            const QString dstPort = JsonUtil::textOrEmpty(meta, "destinationPort");
                            if (!dstIp.isEmpty() && !dstPort.isEmpty()) {
                                conn.destination = QStringLiteral("%1:%2").arg(dstIp, dstPort);
                            } else {
                                conn.destination = dstIp.isEmpty() ? dstPort : dstIp;
                            }
                            QString proc = JsonUtil::textOrEmpty(meta, "process");
                            if (proc.isEmpty()) {
                                proc = JsonUtil::textOrEmpty(meta, "processPath");
                            }
                            conn.process = proc.isEmpty() ? QStringLiteral("-") : proc;
                        } else {
                            conn.network = QStringLiteral("tcp");
                            conn.process = QStringLiteral("-");
                        }
                        connectionsBox->push_back(conn);
                    }
                }
            } catch (...) {
            }
        },
        [this, connectionsBox, uploadTotalBox, downloadTotalBox]() {
            connections_ = *connectionsBox;
            recordConnectionHits();
            const quint64 newUp = *uploadTotalBox;
            const quint64 newDown = *downloadTotalBox;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();

            if (lastPollTimestamp_ > 0 && now > lastPollTimestamp_) {
                const double secs = double(now - lastPollTimestamp_) / 1000.0;
                if (secs > 0.1) {
                    if (newUp >= lastUploadTotal_) {
                        uploadRate_ = static_cast<quint64>((newUp - lastUploadTotal_) / secs);
                    }
                    if (newDown >= lastDownloadTotal_) {
                        downloadRate_ = static_cast<quint64>((newDown - lastDownloadTotal_) / secs);
                    }
                }
            }
            lastUploadTotal_ = newUp;
            lastDownloadTotal_ = newDown;
            uploadTotal_ = newUp;
            downloadTotal_ = newDown;
            lastPollTimestamp_ = now;

            // 一个点 = 一次轮询，单位 KB/s，满 150 丢最旧（Rust mod.rs:1206-1211）。
            trafficHistory_.push_back(QPair<double, double>(downloadRate_ / 1024.0,
                                                            uploadRate_ / 1024.0));
            while (trafficHistory_.size() > kTrafficHistoryPoints) {
                trafficHistory_.removeFirst();
            }

            if (currentPanel() == Panel::Connections) {
                updateConnectionsTable();
            } else if (currentPanel() == Panel::Rules) {
                updateRuleHitsView();
            }
            refreshStatusCard();
        }, this);
}

void ProxyPage::openSwitchLog()
{
    // 和 Rust 一样：点一下才查（reload_egress_switches 全项目只有那一个调用点），
    // 查完把"今日 N 次"缓存下来，页面上那行链接文案下次刷新就跟着变。
    const auto rows = DbTelemetry::recentEgressSwitches(context_.database(), 300);
    const auto today = DbTelemetry::switchCountToday(context_.database());
    const auto total = DbTelemetry::switchHistoryCount(context_.database());
    switchToday_ = today.isOk() ? today.value() : 0;
    switchTotal_ = total.isOk() ? total.value() : 0;
    auto *dialog = new SwitchLogDialog(rows.isOk() ? rows.value()
                                                   : QVector<DbTelemetry::SwitchRow>{},
                                       switchToday_, switchTotal_, this);
    dialog->show();
}

void ProxyPage::updateConnectionsTable()
{
    // Rust 的判据是 state == Running（proxy.rs:961/975）：Degraded 也算"未在运行中"，
    // 之前用 localProxyActive() 会把降级态算成运行中。
    const bool isRunning = context_.proxy().status().state == Proxy::ProxyState::Running;
    if (panels_ != nullptr) {
        // Rust proxy.rs:337-344：有连接才在页签上带数量，空列表就是光秃秃的「活跃连接」。
        panels_->setTabText(int(Panel::Connections), connections_.isEmpty()
            ? UiText::panelConnections()
            : UiText::panelConnectionsCounted().arg(connections_.size()));
    }
    if (connClearFilterBtn_ != nullptr) {
        connClearFilterBtn_->setVisible(connFilterEdit_ != nullptr
                                         && !connFilterEdit_->text().isEmpty());
    }
    if (!isRunning) {
        if (connTable_ != nullptr) connTable_->setVisible(false);
        if (connEmptyCard_ != nullptr) connEmptyCard_->setVisible(true);
        if (connEmptyLabel_ != nullptr) {
            connEmptyLabel_->setText(UiText::connEmptyStopped());
        }
        if (connCountLabel_ != nullptr) {
            connCountLabel_->setText(QStringLiteral("当前连接总数: %1").arg(connections_.size()));
        }
        if (connCloseAllBtn_ != nullptr) connCloseAllBtn_->setEnabled(false);
        return;
    }

    if (connCloseAllBtn_ != nullptr) {
        connCloseAllBtn_->setEnabled(!connections_.isEmpty());
    }
    if (connCountLabel_ != nullptr) {
        connCountLabel_->setText(QStringLiteral("当前连接总数: %1").arg(connections_.size()));
    }

    const QString filter = connFilterEdit_ != nullptr ? connFilterEdit_->text().trimmed().toLower() : QString();
    QVector<ActiveConn> filtered;
    for (const auto &c : connections_) {
        if (filter.isEmpty() || c.process.toLower().contains(filter)
            || c.host.toLower().contains(filter) || c.destination.toLower().contains(filter)
            || c.rule.toLower().contains(filter)) {
            filtered.push_back(c);
        }
    }

    if (filtered.isEmpty()) {
        if (connTable_ != nullptr) connTable_->setVisible(false);
        if (connEmptyCard_ != nullptr) connEmptyCard_->setVisible(true);
        if (connEmptyLabel_ != nullptr) {
            connEmptyLabel_->setText(connections_.isEmpty()
                                         ? UiText::connEmptyIdle()
                                         : UiText::connEmptyNoMatch());
        }
        return;
    }

    if (connEmptyCard_ != nullptr) connEmptyCard_->setVisible(false);
    if (connTable_ != nullptr) {
        connTable_->setUpdatesEnabled(false);
        connTable_->setVisible(true);
        connTable_->setRowCount(filtered.size());
        for (int row = 0; row < filtered.size(); ++row) {
            const auto &c = filtered.at(row);
            auto setCellText = [this, row](int col, const QString &text) {
                QTableWidgetItem *item = connTable_->item(row, col);
                if (item == nullptr) {
                    item = new QTableWidgetItem(text);
                    connTable_->setItem(row, col, item);
                } else if (item->text() != text) {
                    item->setText(text);
                }
            };

            setCellText(0, c.network.toUpper());
            setCellText(1, c.process);
            const QString target = !c.host.isEmpty() ? QStringLiteral("%1 (%2)").arg(c.host, c.destination) : c.destination;
            setCellText(2, target);
            setCellText(3, c.rule);
            setCellText(4, formatBytes(c.upload));
            setCellText(5, formatBytes(c.download));

            auto *btn = qobject_cast<QPushButton *>(connTable_->cellWidget(row, 6));
            const QString id = c.id;
            if (btn == nullptr) {
                btn = new QPushButton(QStringLiteral("断开"), connTable_);
                btn->setObjectName(QStringLiteral("Danger"));
                btn->setToolTip(UiText::connDisconnectHover());
                connect(btn, &QPushButton::clicked, this, [this, id]() { closeConnection(id); });
                connTable_->setCellWidget(row, 6, btn);
            } else {
                btn->disconnect();
                connect(btn, &QPushButton::clicked, this, [this, id]() { closeConnection(id); });
            }
        }
        connTable_->setUpdatesEnabled(true);
    }
}

void ProxyPage::closeConnection(const QString &id)
{
    const auto status = context_.proxy().status();
    const quint16 port = status.controllerPort;
    const QString secret = status.controllerSecret;

    context_.runAsync(
        [port, secret, id]() {
            Net::Request request;
            request.url = QStringLiteral("http://127.0.0.1:%1/connections/%2").arg(port).arg(id);
            request.method = QByteArrayLiteral("DELETE");
            request.timeoutMs = 1500;
            if (!secret.isEmpty()) {
                request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(secret)});
            }
            Net::send(request);
        },
        [this]() {
            emit statusMessageRequested(QStringLiteral("已断开连接"));
            reloadConnections();
        }, this);
}

void ProxyPage::closeAllConnections()
{
    const auto status = context_.proxy().status();
    const quint16 port = status.controllerPort;
    const QString secret = status.controllerSecret;

    context_.runAsync(
        [port, secret]() {
            Net::Request request;
            request.url = QStringLiteral("http://127.0.0.1:%1/connections").arg(port);
            request.method = QByteArrayLiteral("DELETE");
            request.timeoutMs = 1500;
            if (!secret.isEmpty()) {
                request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(secret)});
            }
            Net::send(request);
        },
        [this]() {
            emit statusMessageRequested(QStringLiteral("已断开全部连接"));
            connections_.clear();
            updateConnectionsTable();
        }, this);
}

QWidget *ProxyPage::buildRulesPanel()
{
    auto *scroll = new QScrollArea(stack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    auto *panel = new QWidget(scroll);
    auto *box = new QVBoxLayout(panel);
    // 左右必须是 0：状态卡直接挂在页面根布局（18px 页边距）上，这里再加 12px 就会让
    // 本分栏的卡片比顶卡缩进一圈。其余四个分栏（可用节点/活跃连接/内核管理/运行日志）
    // 都是 0，只有这两处曾经不一样。
    box->setContentsMargins(0, 10, 0, 16);
    box->setSpacing(16);

    // 1. 今日规则命中：Rust 用 `if !self.rule_hits_today.is_empty()` 包住整张卡，
    // 所以一条都没命中时不是"显示 0"，是这张卡根本不存在。
    hitsCard_ = new QFrame(panel);
    Theme::decorateCard(hitsCard_);
    auto *hitsBox = new QVBoxLayout(hitsCard_);
    hitsBox->setContentsMargins(18, 14, 18, 14);
    hitsBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("今日规则命中"), QStringLiteral("从本进程启动开始统计"), hitsCard_);

    auto *hitsInner = new QFrame(hitsCard_);
    Theme::decorateInnerBlock(hitsInner);
    auto *hitsInnerBox = new QVBoxLayout(hitsInner);
    hitsInnerBox->setContentsMargins(14, 10, 14, 10);
    hitsInnerBox->setSpacing(6);

    rulesHitSummaryLabel_ = new QLabel(hitsInner);
    rulesHitSummaryLabel_->setObjectName(QStringLiteral("Subtitle"));
    Gui::Theme::setWidgetCss(rulesHitSummaryLabel_, QStringLiteral("font-size: 13px; color: {textSecondary}; font-weight: 500;"));
    hitsInnerBox->addWidget(rulesHitSummaryLabel_);

    rulesHitsListWidget_ = new QWidget(hitsInner);
    rulesHitsListBox_ = new QVBoxLayout(rulesHitsListWidget_);
    rulesHitsListBox_->setContentsMargins(0, 0, 0, 0);
    rulesHitsListBox_->setSpacing(4);
    hitsInnerBox->addWidget(rulesHitsListWidget_);

    hitsBox->addWidget(hitsInner);
    box->addWidget(hitsCard_);
    hitsCard_->setVisible(false);

    // 2. 分流模式选择
    auto *modeCard = new QFrame(panel);
    Theme::decorateCard(modeCard);
    auto *modeBox = new QVBoxLayout(modeCard);
    modeBox->setContentsMargins(18, 14, 18, 14);
    modeBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("分流模式"), routingModeDescription(Proxy::RoutingMode::Rule),
                     modeCard, &routingModeSubtitleLabel_);

    auto *modeRadioBox = new QHBoxLayout();
    modeRadioBox->setSpacing(14);
    routingModeGroup_ = new QButtonGroup(modeCard);

    const QVector<QPair<Proxy::RoutingMode, QString>> modes = {
        {Proxy::RoutingMode::Rule, QStringLiteral("自定义规则")},
        {Proxy::RoutingMode::ChinaDirect, QStringLiteral("中国大陆直连")},
        {Proxy::RoutingMode::GfwList, QStringLiteral("GFW 列表代理")},
        {Proxy::RoutingMode::Global, QStringLiteral("全局代理")},
        {Proxy::RoutingMode::Direct, QStringLiteral("全部直连")},
    };

    for (int i = 0; i < modes.size(); ++i) {
        auto *radio = new QRadioButton(modes[i].second, modeCard);
        radio->setProperty("routingMode", int(modes[i].first));
        routingModeGroup_->addButton(radio, int(modes[i].first));
        modeRadioBox->addWidget(radio);
        if (i == 0) radio->setChecked(true);
    }
    modeRadioBox->addStretch();

    auto *modeInner = new QFrame(modeCard);
    Theme::decorateInnerBlock(modeInner);
    auto *modeInnerBox = new QVBoxLayout(modeInner);
    modeInnerBox->setContentsMargins(14, 10, 14, 10);
    modeInnerBox->setSpacing(6);
    modeInnerBox->addLayout(modeRadioBox);
    modeBox->addWidget(modeInner);

    routingModeDetailLabel_ = new QLabel(routingModeDetail(Proxy::RoutingMode::Rule), modeCard);
    routingModeDetailLabel_->setObjectName(QStringLiteral("Subtitle"));
    Gui::Theme::setWidgetCss(routingModeDetailLabel_, QStringLiteral("font-size: 12px; color: {muted};"));
    routingModeDetailLabel_->setWordWrap(true);
    modeBox->addWidget(routingModeDetailLabel_);

    connect(routingModeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        rulesOptions_.routingMode = Proxy::RoutingMode(id);
        applyRoutingModeView();
    });

    auto *saveModeLine = new QHBoxLayout();
    saveModeBtn_ = new QPushButton(QStringLiteral("保存分流模式"), modeCard);
    saveModeBtn_->setObjectName(QStringLiteral("Primary"));
    saveModeBtn_->setProperty("testName", QStringLiteral("SaveModeButton"));
    connect(saveModeBtn_, &QPushButton::clicked, this, &ProxyPage::saveRoutingMode);
    saveModeLine->addWidget(saveModeBtn_);
    saveModeLine->addStretch();
    modeBox->addLayout(saveModeLine);

    configBusyNoticeLabel_ = new QLabel(UiText::busyProcessingConfig(), modeCard);
    configBusyNoticeLabel_->setObjectName(QStringLiteral("Subtitle"));
    Gui::Theme::setWidgetCss(configBusyNoticeLabel_, QStringLiteral("font-size: 12px; color: {muted};"));
    configBusyNoticeLabel_->setVisible(false);
    modeBox->addWidget(configBusyNoticeLabel_);

    box->addWidget(modeCard);
    rulesEditableWidgets_ << modeInner << saveModeBtn_;

    // 3. Geo 规则数据库维护
    auto *geoCard = new QFrame(panel);
    Theme::decorateCard(geoCard);
    auto *geoBox = new QVBoxLayout(geoCard);
    geoBox->setContentsMargins(18, 14, 18, 14);
    geoBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("Geo 规则数据库维护"),
                     QStringLiteral("在线更新 GeoIP (中国大陆IP库) 与 GeoSite (域名分流库) 规则文件"), geoCard);

    auto *geoInner = new QFrame(geoCard);
    Theme::decorateInnerBlock(geoInner);
    auto *geoInnerBox = new QHBoxLayout(geoInner);
    geoInnerBox->setContentsMargins(14, 10, 14, 10);
    geoInnerBox->setSpacing(10);

    geoUpdateBtn_ = new QPushButton(QStringLiteral("一键更新 Geo 规则"), geoInner);
    geoUpdateBtn_->setObjectName(QStringLiteral("Primary"));
    geoUpdateBtn_->setProperty("testName", QStringLiteral("GeoUpdateButton"));
    connect(geoUpdateBtn_, &QPushButton::clicked, this, &ProxyPage::updateGeoData);
    geoInnerBox->addWidget(geoUpdateBtn_);

    geoStatusLabel_ = new QLabel(geoInner);
    geoStatusLabel_->setObjectName(QStringLiteral("Subtitle"));
    geoInnerBox->addWidget(geoStatusLabel_);
    geoInnerBox->addStretch();
    geoBox->addWidget(geoInner);

    box->addWidget(geoCard);

    customRulesSection_ = new QWidget(panel);
    customRulesSection_->setObjectName(QStringLiteral("CustomRulesSection"));
    auto *customSectionBox = new QVBoxLayout(customRulesSection_);
    customSectionBox->setContentsMargins(0, 0, 0, 0);
    customSectionBox->setSpacing(16);
    box->addWidget(customRulesSection_);

    // 4. 常用场景分流预设
    auto *presetCard = new QFrame(customRulesSection_);
    Theme::decorateCard(presetCard);
    auto *presetBox = new QVBoxLayout(presetCard);
    presetBox->setContentsMargins(18, 14, 18, 14);
    presetBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("常用场景分流预设（一键添加至规则列表）"),
                     QStringLiteral("点击对应场景可一键合并预设分流规则，并在保存后自动热重载生效"), presetCard);

    auto *presetInner = new QFrame(presetCard);
    Theme::decorateInnerBlock(presetInner);
    auto *presetInnerBox = new QVBoxLayout(presetInner);
    presetInnerBox->setContentsMargins(12, 12, 12, 12);
    presetInnerBox->setSpacing(8);

    // Rust 这一排是 `ui.horizontal_wrapped`：放不下就换行。用三列 QGridLayout 会把
    // 整栏的最小宽度顶到 ~1560px（三个长标签按钮并排），分栏因此横向溢出、卡片右边缘
    // 冲出顶部状态卡（缺陷 #33）。换成和地区药丸同一套 FlowLayout。
    auto *presetRow = new QWidget(presetInner);
    auto *presetFlow = new FlowLayout(presetRow, 0, 8, 8);

    const QString presetBtnStyle = QStringLiteral(
        "QPushButton { background-color: {panel}; color: {textPrimary}; border: 1px solid {border}; border-radius: 8px; padding: 10px 14px; font-weight: 600; font-size: 13px; text-align: left; }"
        "QPushButton:hover { background-color: {accentBg}; border-color: {accent}; color: {accentStrong}; }"
        "QPushButton:pressed { background-color: {accentBg}; border-color: {accent}; }"
    );

    auto *btnAi = new QPushButton(QStringLiteral("+ AI 服务分流 (ChatGPT / Claude / Gemini)"), presetInner);
    btnAi->setObjectName(QStringLiteral("PresetAiButton"));
    Gui::Theme::setWidgetCss(btnAi, presetBtnStyle);
    connect(btnAi, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::aiServiceRules()); });
    btnAi->setToolTip(UiText::presetAiHover());
    presetFlow->addWidget(btnAi);

    auto *btnMedia = new QPushButton(QStringLiteral("+ 海外主流流媒体 (YouTube / Netflix / Spotify)"), presetInner);
    btnMedia->setObjectName(QStringLiteral("PresetMediaButton"));
    Gui::Theme::setWidgetCss(btnMedia, presetBtnStyle);
    connect(btnMedia, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::streamingMediaRules()); });
    btnMedia->setToolTip(UiText::presetMediaHover());
    presetFlow->addWidget(btnMedia);

    auto *btnSocial = new QPushButton(QStringLiteral("+ 即时通讯 (Telegram / Discord / X)"), presetInner);
    btnSocial->setObjectName(QStringLiteral("PresetSocialButton"));
    Gui::Theme::setWidgetCss(btnSocial, presetBtnStyle);
    connect(btnSocial, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::socialTelegramRules()); });
    btnSocial->setToolTip(UiText::presetSocialHover());
    presetFlow->addWidget(btnSocial);

    auto *btnDev = new QPushButton(QStringLiteral("+ 国内开发镜像直连 (npm / 清华源 / 阿里源)"), presetInner);
    btnDev->setObjectName(QStringLiteral("PresetDevButton"));
    Gui::Theme::setWidgetCss(btnDev, presetBtnStyle);
    connect(btnDev, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::devMirrorsRules()); });
    btnDev->setToolTip(UiText::presetDevHover());
    presetFlow->addWidget(btnDev);

    auto *btnGames = new QPushButton(QStringLiteral("+ 游戏平台直连 (Steam / Epic)"), presetInner);
    btnGames->setObjectName(QStringLiteral("PresetGamesButton"));
    Gui::Theme::setWidgetCss(btnGames, presetBtnStyle);
    connect(btnGames, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::gamePlatformsRules()); });
    btnGames->setToolTip(UiText::presetGameHover());
    presetFlow->addWidget(btnGames);

    auto *btnAd = new QPushButton(QStringLiteral("🛡️ 广告与 QUIC 拦截"), presetInner);
    btnAd->setObjectName(QStringLiteral("PresetAdButton"));
    Gui::Theme::setWidgetCss(btnAd, presetBtnStyle);
    connect(btnAd, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::adblockQuicRules()); });
    btnAd->setToolTip(UiText::presetAdHover());
    presetFlow->addWidget(btnAd);

    presetInnerBox->addWidget(presetRow);
    presetBox->addWidget(presetInner);
    customSectionBox->addWidget(presetCard);

    // 5. 自定义规则列表与新增表单
    auto *customCard = new QFrame(customRulesSection_);
    Theme::decorateCard(customCard);
    auto *customBox = new QVBoxLayout(customCard);
    customBox->setContentsMargins(18, 14, 18, 14);
    customBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("自定义规则"), QStringLiteral("从上到下匹配，未命中流量默认走代理"), customCard);

    rulesTable_ = new QTableWidget(customCard);
    rulesTable_->setObjectName(QStringLiteral("RulesTable"));
    Gui::Theme::setWidgetCss(rulesTable_, QStringLiteral("QTableWidget { background-color: {panel}; border: 1px solid {borderSubtle}; border-radius: 8px; }"));
    rulesTable_->setColumnCount(4);
    // Rust 的规则行是「匹配条件 + 动作下拉 + ↑↓×」直接排下来，整栏没有表头
    // （header_cell 只用在活跃连接那张表上），所以这里也不造四个列名。
    rulesTable_->horizontalHeader()->setVisible(false);
    rulesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    rulesTable_->setColumnWidth(1, 110);
    rulesTable_->setColumnWidth(2, 90);
    rulesTable_->setColumnWidth(3, 70);
    rulesTable_->verticalHeader()->setVisible(false);
    rulesTable_->verticalHeader()->setDefaultSectionSize(38);
    rulesTable_->setShowGrid(false);
    rulesTable_->setAlternatingRowColors(true);
    // 动作列走委托编辑：整张表同时只存在一颗下拉框，而不是每行一颗常驻控件。
    rulesTable_->setItemDelegateForColumn(1, new RuleActionDelegate(rulesTable_));
    // Rust 是"点一下那个 select 就展开"，所以单击已经选中的格子即可进入编辑。
    rulesTable_->setEditTriggers(QAbstractItemView::SelectedClicked
                                 | QAbstractItemView::EditKeyPressed);
    connect(rulesTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (syncingRulesTable_ || item == nullptr || item->column() != 1) {
            return;
        }
        const int row = item->row();
        if (row < 0 || row >= rulesOptions_.rules.size()) {
            return;
        }
        rulesOptions_.rules[row].action = Proxy::RuleAction(item->data(Qt::UserRole).toInt());
    });
    rulesTable_->setMinimumHeight(240);
    customBox->addWidget(rulesTable_);

    // 新增规则行块
    auto *formInner = new QFrame(customCard);
    Theme::decorateInnerBlock(formInner);
    auto *formBox = new QVBoxLayout(formInner);
    formBox->setContentsMargins(14, 10, 14, 10);
    formBox->setSpacing(8);

    auto *newRuleRow = new QHBoxLayout();
    newRuleRow->setSpacing(8);

    // Rust 的下拉项是 RuleDraftKind::ALL（域名 / 域名后缀 / GeoSite / ... / TCP / UDP / QUIC），
    // 不是 mihomo 的规则名。手打过的 "DOMAIN-SUFFIX" 那批已经删了。
    newRuleKindCombo_ = makeComboBox(formInner);
    newRuleKindCombo_->setObjectName(QStringLiteral("NewRuleKindCombo"));
    for (RuleDraftKind kind : ruleDraftAll()) {
        newRuleKindCombo_->addItem(ruleDraftLabel(kind), int(kind));
    }
    newRuleKindCombo_->setCurrentIndex(int(RuleDraftKind::DomainSuffix));
    newRuleRow->addWidget(newRuleKindCombo_);

    newRuleValueEdit_ = new QLineEdit(formInner);
    newRuleValueEdit_->setObjectName(QStringLiteral("NewRuleValueEdit"));
    newRuleValueEdit_->setPlaceholderText(QStringLiteral("匹配值"));
    newRuleRow->addWidget(newRuleValueEdit_, 1);

    newRuleActionCombo_ = makeComboBox(formInner);
    newRuleActionCombo_->setObjectName(QStringLiteral("NewRuleActionCombo"));
    for (Proxy::RuleAction action : {Proxy::RuleAction::Direct, Proxy::RuleAction::Proxy,
                                     Proxy::RuleAction::Reject}) {
        newRuleActionCombo_->addItem(ruleActionText(action), int(action));
    }
    newRuleActionCombo_->setCurrentIndex(int(Proxy::RuleAction::Proxy));
    newRuleRow->addWidget(newRuleActionCombo_);

    connect(newRuleKindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyRuleDraftKindView(); });
    applyRuleDraftKindView();

    addRuleBtn_ = new QPushButton(QStringLiteral("添加规则"), formInner);
    addRuleBtn_->setObjectName(QStringLiteral("AddRuleButton"));
    connect(addRuleBtn_, &QPushButton::clicked, this, [this]() {
        const auto kind = RuleDraftKind(newRuleKindCombo_->currentData().toInt());
        const auto action = Proxy::RuleAction(newRuleActionCombo_->currentData().toInt());
        const Proxy::TrafficRule rule{ruleDraftMatcher(kind, newRuleValueEdit_->text()), action};
        const auto rendered = rule.toMihomoRule();
        if (rendered.isErr()) {
            emit statusMessageRequested(UiText::ruleInvalidPrefix().arg(rendered.error()));
            return;
        }
        rulesOptions_.rules.push_back(rule);
        newRuleValueEdit_->clear();
        refreshRulesTable();
    });
    newRuleRow->addWidget(addRuleBtn_);

    recRulesBtn_ = new QPushButton(QStringLiteral("推荐分流"), formInner);
    recRulesBtn_->setObjectName(QStringLiteral("RecommendedRulesButton"));
    recRulesBtn_->setToolTip(QStringLiteral("覆盖为国内直连、广告拒绝、其余代理"));
    connect(recRulesBtn_, &QPushButton::clicked, this, &ProxyPage::confirmRecommendedRules);
    newRuleRow->addWidget(recRulesBtn_);

    optRulesBtn_ = new QPushButton(QStringLiteral("自动优化"), formInner);
    optRulesBtn_->setObjectName(QStringLiteral("OptimizeRulesButton"));
    optRulesBtn_->setToolTip(QStringLiteral("移除重复规则并保持当前顺序"));
    connect(optRulesBtn_, &QPushButton::clicked, this, &ProxyPage::autoOptimizeRules);
    newRuleRow->addWidget(optRulesBtn_);

    formBox->addLayout(newRuleRow);

    processNameWarnLabel_ = new QLabel(UiText::processNameNeedsTun(), formInner);
    processNameWarnLabel_->setObjectName(QStringLiteral("Warning"));
    processNameWarnLabel_->setWordWrap(true);
    Gui::Theme::setWidgetCss(processNameWarnLabel_, QStringLiteral("font-size: 11px; color: {warning};"));
    processNameWarnLabel_->setVisible(false);
    formBox->addWidget(processNameWarnLabel_);

    newRuleHintLabel_ = new QLabel(QStringLiteral("推荐规则：局域网与国内直连，广告和 QUIC 拒绝，其余代理"), formInner);
    newRuleHintLabel_->setObjectName(QStringLiteral("Muted"));
    Gui::Theme::setWidgetCss(newRuleHintLabel_, QStringLiteral("font-size: 11px; color: {muted};"));
    formBox->addWidget(newRuleHintLabel_);

    customBox->addWidget(formInner);

    auto *saveRulesLine = new QHBoxLayout();
    saveRulesBtn_ = new QPushButton(QStringLiteral("保存规则"), customCard);
    saveRulesBtn_->setObjectName(QStringLiteral("Primary"));
    saveRulesBtn_->setProperty("testName", QStringLiteral("SaveRulesButton"));
    connect(saveRulesBtn_, &QPushButton::clicked, this, &ProxyPage::saveRules);
    saveRulesLine->addWidget(saveRulesBtn_);
    saveRulesLine->addStretch();
    customBox->addLayout(saveRulesLine);

    injectedNoticeLabel_ = new QLabel(customCard);
    injectedNoticeLabel_->setObjectName(QStringLiteral("Muted"));
    injectedNoticeLabel_->setWordWrap(true);
    Gui::Theme::setWidgetCss(injectedNoticeLabel_, QStringLiteral("background-color: {panelHover}; border: 1px solid {borderSubtle}; border-radius: 8px; padding: 8px 12px; font-size: 11px; color: {textSecondary};"));
    customBox->addWidget(injectedNoticeLabel_);

    rulesBusyNoticeLabel_ = new QLabel(UiText::busyProcessingRules(), customCard);
    rulesBusyNoticeLabel_->setObjectName(QStringLiteral("Subtitle"));
    Gui::Theme::setWidgetCss(rulesBusyNoticeLabel_, QStringLiteral("font-size: 12px; color: {muted};"));
    rulesBusyNoticeLabel_->setVisible(false);
    customBox->addWidget(rulesBusyNoticeLabel_);

    customSectionBox->addWidget(customCard);

    rulesEditableWidgets_ << presetInner << customCard;

    applyRoutingModeView();

    scroll->setWidget(panel);
    return scroll;
}

void ProxyPage::applyRoutingModeView()
{
    const Proxy::RoutingMode mode = rulesOptions_.routingMode;
    if (routingModeSubtitleLabel_ != nullptr) {
        routingModeSubtitleLabel_->setText(routingModeDescription(mode));
    }
    if (routingModeDetailLabel_ != nullptr) {
        routingModeDetailLabel_->setText(routingModeDetail(mode));
    }
    // Rust: let show_custom_rules = proxy_config.routing_mode == RoutingMode::Rule;
    if (customRulesSection_ != nullptr) {
        customRulesSection_->setVisible(mode == Proxy::RoutingMode::Rule);
    }
    applyRuleDraftKindView();
    applyInjectionNotice();
}

void ProxyPage::applyRuleDraftKindView()
{
    if (newRuleKindCombo_ == nullptr) {
        return;
    }
    const auto kind = RuleDraftKind(newRuleKindCombo_->currentData().toInt());
    if (newRuleValueEdit_ != nullptr) {
        newRuleValueEdit_->setVisible(ruleDraftNeedsValue(kind));
    }
    // PROCESS-NAME 只有 TUN 模式拿得到进程名，系统代理模式下选了就得当场提醒。
    if (processNameWarnLabel_ != nullptr) {
        const bool warn = kind == RuleDraftKind::ProcessName
            && rulesOptions_.mode != Proxy::ProxyMode::Tun;
        processNameWarnLabel_->setVisible(warn);
    }
}

void ProxyPage::applyInjectionNotice()
{
    if (injectedNoticeLabel_ == nullptr) {
        return;
    }
    QStringList rendered;
    for (const auto &rule : Proxy::mihomoDnsPoisonOverrides()) {
        const auto text = rule.toMihomoRule();
        if (text.isOk()) {
            rendered.append(text.value());
        }
    }
    if (rendered.isEmpty()) {
        injectedNoticeLabel_->clear();
        injectedNoticeLabel_->setVisible(false);
        return;
    }
    injectedNoticeLabel_->setText(
        UiText::injectionLine().arg(rendered.join(UiText::injectionJoinSep())));
    injectedNoticeLabel_->setVisible(true);
}

void ProxyPage::setRulesEditable(bool editable)
{
    for (QWidget *widget : rulesEditableWidgets_) {
        if (widget != nullptr) {
            widget->setEnabled(editable);
        }
    }
    if (configBusyNoticeLabel_ != nullptr) {
        configBusyNoticeLabel_->setVisible(!editable);
    }
    if (rulesBusyNoticeLabel_ != nullptr) {
        rulesBusyNoticeLabel_->setVisible(!editable);
    }
}

void ProxyPage::setRulesBusy(bool busy)
{
    if (rulesBusy_ == busy) {
        return;
    }
    rulesBusy_ = busy;
    setRulesEditable(!busy);
    // Rust 的 proxy_busy 是全应用一个标志，DNS/TUN 那栏的 editable 也在同一帧重算。
    applyNetworkEditable();
    applyNetworkEditable();
    // Rust 的 proxy_busy 是全应用一个标志，DNS/TUN 那栏的 editable 也在同一帧重算。
    applyNetworkEditable();
}

QString ProxyPage::routingModeSubtitleText() const
{
    return routingModeSubtitleLabel_ == nullptr ? QString() : routingModeSubtitleLabel_->text();
}

QString ProxyPage::routingModeDetailText() const
{
    return routingModeDetailLabel_ == nullptr ? QString() : routingModeDetailLabel_->text();
}

bool ProxyPage::customRulesVisible() const
{
    // 离屏页面从不 show()，isVisible() 恒假；isHidden() 只看显式的 hide()。
    return customRulesSection_ != nullptr && !customRulesSection_->isHidden();
}

QString ProxyPage::configBusyNoticeText() const
{
    if (configBusyNoticeLabel_ == nullptr || configBusyNoticeLabel_->isHidden()) {
        return QString();
    }
    return configBusyNoticeLabel_->text();
}

QString ProxyPage::rulesBusyNoticeText() const
{
    if (rulesBusyNoticeLabel_ == nullptr || rulesBusyNoticeLabel_->isHidden()) {
        return QString();
    }
    return rulesBusyNoticeLabel_->text();
}

QString ProxyPage::injectedNoticeText() const
{
    if (injectedNoticeLabel_ == nullptr || injectedNoticeLabel_->isHidden()) {
        return QString();
    }
    return injectedNoticeLabel_->text();
}

QString ProxyPage::processNameWarningText() const
{
    if (processNameWarnLabel_ == nullptr || processNameWarnLabel_->isHidden()) {
        return QString();
    }
    return processNameWarnLabel_->text();
}

QString ProxyPage::ruleRowMatcherText(int row) const
{
    if (rulesTable_ == nullptr || row < 0 || row >= rulesTable_->rowCount()) {
        return QString();
    }
    const auto *item = rulesTable_->item(row, 0);
    return item == nullptr ? QString() : item->text();
}

QString ProxyPage::ruleRowActionText(int row) const
{
    if (rulesTable_ == nullptr || row < 0 || row >= rulesTable_->rowCount()) {
        return QString();
    }
    const auto *item = rulesTable_->item(row, 1);
    return item == nullptr ? QString() : item->text();
}

void ProxyPage::syncRulesWidgets()
{
    if (routingModeGroup_ != nullptr) {
        auto *button = routingModeGroup_->button(int(rulesOptions_.routingMode));
        if (button != nullptr) {
            button->setChecked(true);
        }
    }
    applyRoutingModeView();
    refreshRulesTable();
}

void ProxyPage::reloadRules()
{
    // Rust 的 proxy_config 是「工作副本」：只在装载和保存之后重读。每轮轮询都拿内核
    // 配置覆盖回来的话，用户刚点下的模式会被悄悄打回去 —— 而「预设 + 规则清单」整段
    // 的可见性正是跟着这个模式走的。
    if (!rulesLoaded_) {
        rulesOptions_ = Proxy::configForUi(context_.proxy().config());
        rulesLoaded_ = true;
        syncRulesWidgets();
    }
    setRulesEditable(!rulesBusy_);
    reloadRuleHits();
    updateRuleHitsView();
}

void ProxyPage::setRulesConfigForTest(const Proxy::MihomoConfigOptions &options)
{
    rulesOptions_ = options;
    rulesLoaded_ = true;
    syncRulesWidgets();
}

void ProxyPage::recordConnectionHits()
{
    QVector<ConnectionHitInput> inputs;
    inputs.reserve(connections_.size());
    for (const auto &conn : connections_) {
        inputs.push_back({conn.id, conn.rule, conn.target});
    }
    accumulateRuleHits(ruleSeen_, ruleTally_, inputs);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastHitFlushMs_ == 0) {
        // Rust 的 last_hit_flush 是启动那一刻的 Instant，第一次刷要等满 30 秒。
        lastHitFlushMs_ = now;
        return;
    }
    if (!ruleTally_.isEmpty() && now - lastHitFlushMs_ >= 30000) {
        flushRuleHits();
    }
}

void ProxyPage::feedConnectionsForTest(const QVector<ConnectionHitInput> &connections)
{
    accumulateRuleHits(ruleSeen_, ruleTally_, connections);
}

void ProxyPage::flushRuleHits()
{
    lastHitFlushMs_ = QDateTime::currentMSecsSinceEpoch();
    const QVector<DbTelemetry::RuleHit> records
        = drainRuleHits(ruleTally_, TimeUtil::OptionalUtc::now().toDbText());
    if (records.isEmpty()) {
        return;
    }
    QPointer<ProxyPage> guard(this);
    auto box = std::make_shared<QVector<DbTelemetry::RuleHitRow>>();
    context_.runAsync(
        [ctx = &context_, records, box]() {
            DbTelemetry::recordRuleHits(ctx->database(), records);
            const auto today = DbTelemetry::ruleHitsToday(ctx->database());
            if (today.isOk()) {
                *box = today.value();
            }
        },
        [guard, box]() {
            if (guard.isNull()) {
                return;
            }
            guard->ruleHits_ = *box;
            guard->updateRuleHitsView();
        });
}

void ProxyPage::reloadRuleHits()
{
    if (ruleHitsRequested_) {
        return;
    }
    ruleHitsRequested_ = true;
    QPointer<ProxyPage> guard(this);
    auto box = std::make_shared<QVector<DbTelemetry::RuleHitRow>>();
    context_.runAsync(
        [ctx = &context_, box]() {
            const auto today = DbTelemetry::ruleHitsToday(ctx->database());
            if (today.isOk()) {
                *box = today.value();
            }
        },
        [guard, box]() {
            if (guard.isNull()) {
                return;
            }
            guard->ruleHitsRequested_ = false;
            guard->ruleHits_ = *box;
            guard->updateRuleHitsView();
        });
}

void ProxyPage::setRuleHitsForTest(const QVector<DbTelemetry::RuleHitRow> &hits)
{
    ruleHits_ = hits;
    lastRuleRowsSignature_.clear();
    updateRuleHitsView();
}

bool ProxyPage::ruleHitsCardVisible() const
{
    return hitsCard_ != nullptr && !hitsCard_->isHidden();
}

QString ProxyPage::ruleHitsSummaryText() const
{
    if (rulesHitSummaryLabel_ == nullptr || !ruleHitsCardVisible()) {
        return QString();
    }
    return rulesHitSummaryLabel_->text();
}

QVector<RuleHitRowView> ProxyPage::ruleHitsRows() const
{
    return buildRuleHitCard(ruleHits_).rows;
}

QString ProxyPage::ruleHitsRowsSignature(const RuleHitCard &card)
{
    QStringList parts;
    for (const auto &row : card.rows) {
        parts << QStringLiteral("%1|%2|%3").arg(row.rule, row.target, QString::number(row.hits));
    }
    return parts.join(QStringLiteral(";"));
}

void ProxyPage::updateRuleHitsView()
{
    const RuleHitCard card = buildRuleHitCard(ruleHits_);
    if (hitsCard_ != nullptr) {
        hitsCard_->setVisible(card.visible);
    }
    if (!card.visible || rulesHitSummaryLabel_ == nullptr) {
        return;
    }
    rulesHitSummaryLabel_->setText(card.summary);

    // 轮询每秒都跑到这儿：行没变就一张都不重排（和节点栅格同一套跳过逻辑）。
    const QString signature = ruleHitsRowsSignature(card);
    if (signature == lastRuleRowsSignature_ || rulesHitsListBox_ == nullptr
        || rulesHitsListWidget_ == nullptr) {
        lastRuleRowsSignature_ = signature;
        return;
    }
    lastRuleRowsSignature_ = signature;

    QLayoutItem *child = nullptr;
    while ((child = rulesHitsListBox_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    for (const auto &row : card.rows) {
        auto *rowWidget = new QWidget(rulesHitsListWidget_);
        auto *rowBox = new QHBoxLayout(rowWidget);
        rowBox->setContentsMargins(4, 2, 4, 2);
        rowBox->setSpacing(8);

        auto *ruleLabel = new QLabel(row.rule, rowWidget);
        Gui::Theme::setWidgetCss(ruleLabel, QStringLiteral("font-size: 12px; color: {accent};"));

        auto *targetLabel = new QLabel(UiText::hitsArrow().arg(row.target), rowWidget);
        Gui::Theme::setWidgetCss(targetLabel, QStringLiteral("font-size: 12px; color: {textSecondary};"));

        auto *countLabel = new QLabel(UiText::hitsCount().arg(row.hits), rowWidget);
        Gui::Theme::setWidgetCss(countLabel, QStringLiteral("font-size: 12px; font-weight: 700; color: {textPrimary};"));

        rowBox->addWidget(ruleLabel);
        rowBox->addWidget(targetLabel);
        rowBox->addStretch();
        rowBox->addWidget(countLabel);
        rulesHitsListBox_->addWidget(rowWidget);
    }
}

void ProxyPage::refreshRulesTable()
{
    if (rulesTable_ == nullptr) {
        return;
    }
    rulesTable_->setUpdatesEnabled(false);
    syncingRulesTable_ = true;
    rulesTable_->clearContents();
    rulesTable_->setRowCount(rulesOptions_.rules.size());
    for (int i = 0; i < rulesOptions_.rules.size(); ++i) {
        const auto &rule = rulesOptions_.rules.at(i);
        rulesTable_->setItem(i, 0, new QTableWidgetItem(ruleMatcherText(rule.matcher)));

        // 动作只写成单元格文本 + UserRole；编辑由 RuleActionDelegate 在被点开时现造下拉框。
        auto *actionItem = new QTableWidgetItem(ruleActionText(rule.action));
        actionItem->setData(Qt::UserRole, int(rule.action));
        actionItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        rulesTable_->setItem(i, 1, actionItem);

        auto *moveWidget = new QWidget(rulesTable_);
        auto *moveBox = new QHBoxLayout(moveWidget);
        moveBox->setContentsMargins(0, 0, 0, 0);
        moveBox->setSpacing(2);

        auto *upBtn = new QPushButton(UiText::ruleMoveUpGlyph(), moveWidget);
        upBtn->setObjectName(QStringLiteral("Small"));
        upBtn->setToolTip(UiText::ruleMoveUp());
        upBtn->setEnabled(i > 0);
        connect(upBtn, &QPushButton::clicked, this, [this, i]() {
            if (i > 0) {
                std::swap(rulesOptions_.rules[i], rulesOptions_.rules[i - 1]);
                refreshRulesTable();
            }
        });
        moveBox->addWidget(upBtn);

        auto *downBtn = new QPushButton(UiText::ruleMoveDownGlyph(), moveWidget);
        downBtn->setObjectName(QStringLiteral("Small"));
        downBtn->setToolTip(UiText::ruleMoveDown());
        downBtn->setEnabled(i + 1 < rulesOptions_.rules.size());
        connect(downBtn, &QPushButton::clicked, this, [this, i]() {
            if (i + 1 < rulesOptions_.rules.size()) {
                std::swap(rulesOptions_.rules[i], rulesOptions_.rules[i + 1]);
                refreshRulesTable();
            }
        });
        moveBox->addWidget(downBtn);
        rulesTable_->setCellWidget(i, 2, moveWidget);

        auto *delBtn = new QPushButton(UiText::ruleRemoveGlyph(), rulesTable_);
        delBtn->setObjectName(QStringLiteral("Small"));
        delBtn->setToolTip(UiText::ruleRemove());
        connect(delBtn, &QPushButton::clicked, this, [this, i]() {
            if (i < rulesOptions_.rules.size()) {
                rulesOptions_.rules.remove(i);
                refreshRulesTable();
            }
        });
        rulesTable_->setCellWidget(i, 3, delBtn);
    }
    rulesTable_->setUpdatesEnabled(true);
    syncingRulesTable_ = false;
}

void ProxyPage::saveRules()
{
    const auto opts = rulesOptions_;
    setRulesBusy(true);
    emit statusMessageRequested(UiText::savingConfig());
    QPointer<ProxyPage> guard(this);
    auto failure = std::make_shared<QString>();
    context_.runAsync(
        [ctx = &context_, opts, failure]() {
            const auto applied = ctx->proxy().reloadConfig(opts);
            if (applied.isErr()) {
                *failure = applied.error();
                return;
            }
            const auto json = opts.toSavedJsonText();
            if (json.isOk()) {
                ctx->database().setProxyConfigJsonText(json.value());
            }
        },
        [guard, failure]() {
            if (guard.isNull()) {
                return;
            }
            guard->setRulesBusy(false);
            guard->rulesLoaded_ = false;
            emit guard->statusMessageRequested(failure->isEmpty()
                                                  ? UiText::configSaved()
                                                  : UiText::configSaveFailed().arg(*failure));
        });
}

void ProxyPage::saveRoutingMode()
{
    if (routingModeGroup_ != nullptr) {
        rulesOptions_.routingMode = Proxy::RoutingMode(routingModeGroup_->checkedId());
    }
    saveRules();
}

void ProxyPage::updateGeoData()
{
    if (geoUpdating_) {
        return;
    }
    geoUpdating_ = true;
    if (geoUpdateBtn_ != nullptr) geoUpdateBtn_->setEnabled(false);
    if (geoStatusLabel_ != nullptr) geoStatusLabel_->setText(QStringLiteral("正在更新 Geo 规则..."));

    // Rust: running 判据是 state == Running；没在跑就直接下库，不碰接口。
    const auto status = context_.proxy().status();
    const bool running = status.state == Proxy::ProxyState::Running;
    const quint16 port = status.controllerPort;
    const QString secret = status.controllerSecret;
    emit statusMessageRequested(running ? UiText::geoRequesting() : UiText::geoOfflineBusy());

    struct Outcome {
        QString message;
        QString error;
    };
    auto box = std::make_shared<Outcome>();
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [running, port, secret, box]() {
            if (running) {
                // 这个调用是同步的：内核要自己下完两个库（11MB + 16MB）才回话，
                // 30 秒会把还在正常下载的情况误判成失败。
                Net::Request request;
                request.url = QStringLiteral("http://127.0.0.1:%1/configs/geo").arg(port);
                request.method = QByteArrayLiteral("POST");
                request.timeoutMs = 180000;
                if (!secret.isEmpty()) {
                    request.headers.append({QStringLiteral("Authorization"),
                                            QStringLiteral("Bearer %1").arg(secret)});
                }
                const auto res = Net::send(request);
                if (res.isErr()) {
                    box->error = UiText::geoRequestFailed().arg(res.error());
                    return;
                }
                const Net::Response response = res.value();
                if (response.isSuccess()) {
                    box->message = UiText::geoUpdatedOk();
                    return;
                }
                // 内核会把失败原因写在响应体里（哪家镜像挂了、超时还是半截），必须带出来。
                const QString detail = QString::fromUtf8(response.body).trimmed();
                const QString code = QString::number(response.status);
                box->error = detail.isEmpty()
                    ? UiText::geoBadStatus().arg(code)
                    : UiText::geoKernelDownloadFailed().arg(code, detail);
                return;
            }
            const auto provisioned = Geo::provision(Geo::mihomoDir(), QString(), true);
            if (provisioned.isErr()) {
                box->error = provisioned.error();
                return;
            }
            box->message = UiText::geoOfflineDone().arg(Geo::mihomoDir(), provisioned.value());
        },
        [guard, box]() {
            if (guard.isNull()) {
                return;
            }
            guard->geoUpdating_ = false;
            if (guard->geoUpdateBtn_ != nullptr) {
                guard->geoUpdateBtn_->setEnabled(true);
            }
            if (guard->geoStatusLabel_ != nullptr) {
                guard->geoStatusLabel_->clear();
            }
            guard->emit statusMessageRequested(
                box->error.isEmpty() ? box->message : UiText::geoUpdateFailed().arg(box->error));
        });
}

void ProxyPage::confirmRecommendedRules()
{
    if (recommendedConfirmDialog_ != nullptr) {
        return;
    }
    // 用 show() 而不是 exec()：嵌套事件循环会把离屏测试和外壳轮询一起卡住，
    // 而 Rust 的 egui::Modal 本来也是画在当帧里、不阻塞的。
    auto *dialog = new QDialog(this);
    recommendedConfirmDialog_ = dialog;
    dialog->setObjectName(QStringLiteral("RecommendedRulesConfirm"));
    dialog->setModal(true);

    auto *box = new QVBoxLayout(dialog);
    box->setContentsMargins(18, 14, 18, 14);
    box->setSpacing(10);

    auto *title = new QLabel(UiText::overrideRulesTitle(), dialog);
    title->setObjectName(QStringLiteral("Title"));
    box->addWidget(title);

    auto *body = new QLabel(UiText::overrideRulesBody(), dialog);
    body->setObjectName(QStringLiteral("Subtitle"));
    body->setWordWrap(true);
    box->addWidget(body);

    auto *line = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(UiText::confirmCancel(), dialog);
    auto *okBtn = new QPushButton(UiText::confirmOverride(), dialog);
    okBtn->setObjectName(QStringLiteral("Primary"));
    line->addStretch();
    line->addWidget(cancelBtn);
    line->addWidget(okBtn);
    box->addLayout(line);

    connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(dialog, &QDialog::finished, this, [this, dialog](int code) {
        recommendedConfirmDialog_ = nullptr;
        dialog->deleteLater();
        if (code != QDialog::Accepted) {
            return;
        }
        rulesOptions_.rules = Proxy::recommendedTrafficRules();
        refreshRulesTable();
        saveRules();
    });
    dialog->show();
}

void ProxyPage::autoOptimizeRules()
{
    const int before = rulesOptions_.rules.size();
    rulesOptions_.rules = Proxy::optimizeTrafficRules(rulesOptions_.rules);
    const int removed = before - rulesOptions_.rules.size();
    refreshRulesTable();
    emit statusMessageRequested(removed == 0 ? UiText::optimizeAlreadyBest()
                                             : UiText::optimizeRemovedN().arg(removed));
}

void ProxyPage::prependPresetRules(const QVector<Proxy::TrafficRule> &preset)
{
    for (const auto &r : preset) {
        if (!rulesOptions_.rules.contains(r)) {
            rulesOptions_.rules.prepend(r);
        }
    }
    rulesOptions_.rules = Proxy::optimizeTrafficRules(rulesOptions_.rules);
    refreshRulesTable();
    saveRules();
}

QWidget *ProxyPage::buildNetworkPanel()
{
    auto *scroll = new QScrollArea(stack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    auto *panel = new QWidget(scroll);
    auto *box = new QVBoxLayout(panel);
    // 左右必须是 0：状态卡直接挂在页面根布局（18px 页边距）上，这里再加 12px 就会让
    // 本分栏的卡片比顶卡缩进一圈。其余四个分栏（可用节点/活跃连接/内核管理/运行日志）
    // 都是 0，只有这两处曾经不一样。
    box->setContentsMargins(0, 10, 0, 16);
    box->setSpacing(16);

    // Rust 的这一栏是「卡外一个标题 + 卡内三条粗体小标题」，全栏只有一张卡；
    // 标题的副标题是固定文案（不是随状态变的那类）。
    makeSectionTitle(QStringLiteral("DNS / TUN"), UiText::netSectionHint(), panel);

    auto *card = new QFrame(panel);
    card->setProperty("testName", QStringLiteral("NetworkCard"));
    Theme::decorateCard(card);
    auto *cardBox = new QVBoxLayout(card);
    cardBox->setContentsMargins(18, 14, 18, 14);
    cardBox->setSpacing(12);

    // Rust 用 ui.add_enabled_ui(editable, ...) 把整片内容（含保存按钮）一起禁用，
    // 而"停止代理后可修改运行配置"那行在禁用片之外 —— 所以这里也分两层。
    auto *host = new QWidget(card);
    host->setObjectName(QStringLiteral("NetworkEditableHost"));
    auto *hostBox = new QVBoxLayout(host);
    hostBox->setContentsMargins(0, 0, 0, 0);
    hostBox->setSpacing(12);
    netEditableHost_ = host;

    // 每一块区域都要有自己的底：卡片里再分"运行配置 / TUN 参数 / DNS"三块 InnerBlock
    // （panelHover 底 + borderSubtle 描边），不然一整栏就是一张白卡上摊着一堆控件，
    // 用户看到的"块和块分不开"就是这个。Rust 侧内框同样是 fill=panel_hover
    // （`views/proxy.rs:849`）。
    netRunBlock_ = new QFrame(host);
    Theme::decorateInnerBlock(netRunBlock_);
    netRunBlock_->setProperty("testName", QStringLiteral("NetworkRunBlock"));
    auto *runBox = new QVBoxLayout(netRunBlock_);
    runBox->setContentsMargins(14, 10, 14, 10);
    runBox->setSpacing(10);
    hostBox->addWidget(netRunBlock_);

    runBox->addWidget(makeStrongLabel(UiText::netModeStrong(), netRunBlock_));

    auto *modeLine = new QHBoxLayout();
    modeLine->setSpacing(14);
    netModeSystemRadio_ = new QRadioButton(UiText::netModeSystem(), netRunBlock_);
    netModeTunRadio_ = new QRadioButton(QStringLiteral("TUN"), netRunBlock_);
    netModeSystemRadio_->setChecked(true);
    modeLine->addWidget(netModeSystemRadio_);
    modeLine->addWidget(netModeTunRadio_);
    modeLine->addStretch();
    runBox->addLayout(modeLine);

    connect(netModeTunRadio_, &QRadioButton::toggled, this, [this](bool tun) {
        if (netTunGroup_ != nullptr) {
            netTunGroup_->setVisible(tun);
        }
    });

    auto *kernelLine = new QHBoxLayout();
    kernelLine->setSpacing(10);
    kernelLine->addWidget(new QLabel(UiText::netKernelLabel(), netRunBlock_));
    netKernelPrefCombo_ = makeComboBox(netRunBlock_);
    netKernelPrefCombo_->addItem(UiText::netKernelAuto(), int(Kernel::Preference::Auto));
    netKernelPrefCombo_->addItem(QStringLiteral("Mihomo"), int(Kernel::Preference::Mihomo));
    netKernelPrefCombo_->addItem(QStringLiteral("Sing-box"), int(Kernel::Preference::SingBox));
    netKernelPrefCombo_->addItem(QStringLiteral("Xray"), int(Kernel::Preference::Xray));
    kernelLine->addWidget(netKernelPrefCombo_);
    kernelLine->addStretch();
    runBox->addLayout(kernelLine);

    // 端口/局域网/终端代理各占一行（Rust 那边是 horizontal_wrapped，窄屏会自己换行；
    // Qt 的 QHBoxLayout 不会换行，挤在一行里就是他看到的"控制端口/允许局域网/终端代理
    // 全都黏在 mixed 端口后面"）。旋钮给一个固定宽度，否则独占一行时会被拉到整行宽。
    auto addFieldRow = [this, runBox](QLabel *caption, QWidget *field) {
        auto *line = new QHBoxLayout();
        line->setSpacing(10);
        line->addWidget(caption);
        line->addWidget(field);
        line->addStretch();
        runBox->addLayout(line);
    };

    netMixedPortSpin_ = new QSpinBox(netRunBlock_);
    netMixedPortSpin_->setRange(1, 65535);
    netMixedPortSpin_->setValue(9980);
    netMixedPortSpin_->setFixedWidth(120);
    addFieldRow(new QLabel(UiText::netPortMixed(), netRunBlock_), netMixedPortSpin_);

    netControllerPortSpin_ = new QSpinBox(netRunBlock_);
    netControllerPortSpin_->setRange(1, 65535);
    netControllerPortSpin_->setValue(9090);
    netControllerPortSpin_->setFixedWidth(120);
    addFieldRow(new QLabel(UiText::netPortController(), netRunBlock_), netControllerPortSpin_);

    netAllowLanCheck_ = new QCheckBox(UiText::netAllowLan(), netRunBlock_);
    runBox->addWidget(netAllowLanCheck_);

    netTerminalProxyCheck_ = new QCheckBox(UiText::netTerminalProxy(), netRunBlock_);
    netTerminalProxyCheck_->setToolTip(UiText::netTerminalProxyHover());
    runBox->addWidget(netTerminalProxyCheck_);

    // TUN 参数：Rust 是 `if self.proxy_config.mode == ProxyMode::Tun { ... }`，
    // 整段（含小标题）只在 TUN 下出现，没有额外提示行。
    netTunGroup_ = new QFrame(host);
    Theme::decorateInnerBlock(static_cast<QFrame *>(netTunGroup_));
    netTunGroup_->setProperty("testName", QStringLiteral("TunGroup"));
    auto *tunBox = new QVBoxLayout(netTunGroup_);
    tunBox->setContentsMargins(0, 0, 0, 0);
    tunBox->setSpacing(8);
    tunBox->addWidget(makeStrongLabel(UiText::netTunStrong(), netTunGroup_));

    auto *tunLine = new QHBoxLayout();
    tunLine->setSpacing(12);
    tunLine->addWidget(new QLabel(UiText::netStackLabel(), netTunGroup_));
    netTunStackCombo_ = makeComboBox(netTunGroup_);
    netTunStackCombo_->addItem(QStringLiteral("Mixed"), int(Proxy::TunStack::Mixed));
    netTunStackCombo_->addItem(QStringLiteral("System"), int(Proxy::TunStack::System));
    netTunStackCombo_->addItem(QStringLiteral("gVisor"), int(Proxy::TunStack::Gvisor));
    tunLine->addWidget(netTunStackCombo_);

    netTunAutoRouteCheck_ = new QCheckBox(UiText::netAutoRoute(), netTunGroup_);
    tunLine->addWidget(netTunAutoRouteCheck_);
    netTunAutoDetectCheck_ = new QCheckBox(UiText::netAutoDetect(), netTunGroup_);
    tunLine->addWidget(netTunAutoDetectCheck_);
    tunLine->addStretch();
    tunBox->addLayout(tunLine);
    netTunGroup_->setVisible(false);
    hostBox->addWidget(netTunGroup_);

    netDnsBlock_ = new QFrame(host);
    Theme::decorateInnerBlock(netDnsBlock_);
    netDnsBlock_->setProperty("testName", QStringLiteral("NetworkDnsBlock"));
    auto *dnsBox = new QVBoxLayout(netDnsBlock_);
    dnsBox->setContentsMargins(14, 10, 14, 10);
    dnsBox->setSpacing(10);
    hostBox->addWidget(netDnsBlock_);

    dnsBox->addWidget(makeStrongLabel(QStringLiteral("DNS"), netDnsBlock_));

    auto *dnsLine = new QHBoxLayout();
    dnsLine->setSpacing(12);
    netDnsEnabledCheck_ = new QCheckBox(UiText::netDnsEnable(), netDnsBlock_);
    dnsLine->addWidget(netDnsEnabledCheck_);
    netDnsIpv6Check_ = new QCheckBox(QStringLiteral("IPv6"), netDnsBlock_);
    dnsLine->addWidget(netDnsIpv6Check_);
    netDnsEnhancedCombo_ = makeComboBox(netDnsBlock_);
    netDnsEnhancedCombo_->addItem(QStringLiteral("Fake-IP"), int(Proxy::DnsEnhancedMode::FakeIp));
    netDnsEnhancedCombo_->addItem(QStringLiteral("Redir-Host"), int(Proxy::DnsEnhancedMode::RedirHost));
    dnsLine->addWidget(netDnsEnhancedCombo_);
    dnsLine->addStretch();
    hostBox->addLayout(dnsLine);

    // 上游 DNS：Rust 按 dns.nameservers 逐条画一行（"DNS 1"、"DNS 2"...），
    // 条数是动态的，不是固定"主/备"两行。
    dnsRowsHost_ = new QWidget(netDnsBlock_);
    dnsRowsHost_->setObjectName(QStringLiteral("DnsRows"));
    dnsRowsBox_ = new QVBoxLayout(dnsRowsHost_);
    dnsRowsBox_->setContentsMargins(0, 0, 0, 0);
    dnsRowsBox_->setSpacing(6);
    dnsBox->addLayout(dnsLine);
    dnsBox->addWidget(dnsRowsHost_);

    saveNetworkBtn_ = new QPushButton(UiText::netSaveButton(), host);
    saveNetworkBtn_->setObjectName(QStringLiteral("Primary"));    saveNetworkBtn_->setProperty("testName", QStringLiteral("SaveNetworkButton"));
    connect(saveNetworkBtn_, &QPushButton::clicked, this, &ProxyPage::saveNetworkConfig);
    hostBox->addWidget(saveNetworkBtn_);

    cardBox->addWidget(host);

    netEditableNoticeLabel_ = new QLabel(card);
    netEditableNoticeLabel_->setObjectName(QStringLiteral("Subtitle"));
    Gui::Theme::setWidgetCss(netEditableNoticeLabel_, QStringLiteral("font-size: 12px; color: {muted};"));
    netEditableNoticeLabel_->setVisible(false);
    cardBox->addWidget(netEditableNoticeLabel_);

    box->addWidget(card);

    scroll->setWidget(panel);
    return scroll;
}

void ProxyPage::reloadNetwork()
{
    networkOptions_ = Proxy::configForUi(context_.proxy().config());
    syncNetworkWidgets();
}

void ProxyPage::setNetworkConfigForTest(const Proxy::MihomoConfigOptions &options)
{
    networkOptions_ = options;
    syncNetworkWidgets();
}

void ProxyPage::syncNetworkWidgets()
{

    if (netModeSystemRadio_ != nullptr && netModeTunRadio_ != nullptr) {
        if (networkOptions_.mode == Proxy::ProxyMode::Tun) {
            netModeTunRadio_->setChecked(true);
        } else {
            netModeSystemRadio_->setChecked(true);
        }
    }

    if (netKernelPrefCombo_ != nullptr) {
        netKernelPrefCombo_->setCurrentIndex(int(networkOptions_.kernelPreference));
    }
    if (netMixedPortSpin_ != nullptr) {
        netMixedPortSpin_->setValue(networkOptions_.mixedPort);
    }
    if (netControllerPortSpin_ != nullptr) {
        netControllerPortSpin_->setValue(networkOptions_.controllerPort);
    }
    if (netAllowLanCheck_ != nullptr) {
        netAllowLanCheck_->setChecked(networkOptions_.allowLan);
    }
    if (netTerminalProxyCheck_ != nullptr) {
        netTerminalProxyCheck_->setChecked(networkOptions_.terminalProxy);
    }

    // 段内控件先按配置摆好；可见性也直接跟配置走，不依赖 radio 的 toggled 时机。
    if (netTunGroup_ != nullptr) {
        netTunGroup_->setVisible(networkOptions_.mode == Proxy::ProxyMode::Tun);
    }
    if (netTunStackCombo_ != nullptr) {
        netTunStackCombo_->setCurrentIndex(int(networkOptions_.tun.stack));
    }
    if (netTunAutoRouteCheck_ != nullptr) {
        netTunAutoRouteCheck_->setChecked(networkOptions_.tun.autoRoute);
    }
    if (netTunAutoDetectCheck_ != nullptr) {
        netTunAutoDetectCheck_->setChecked(networkOptions_.tun.autoDetectInterface);
    }

    if (netDnsEnabledCheck_ != nullptr) {
        netDnsEnabledCheck_->setChecked(networkOptions_.dns.enabled);
    }
    if (netDnsIpv6Check_ != nullptr) {
        netDnsIpv6Check_->setChecked(networkOptions_.dns.ipv6);
    }
    if (netDnsEnhancedCombo_ != nullptr) {
        netDnsEnhancedCombo_->setCurrentIndex(int(networkOptions_.dns.enhancedMode));
    }
    rebuildDnsRows();
    applyNetworkEditable();
}

void ProxyPage::applyNetworkEditable()
{
    // Rust: let editable = !self.proxy_busy && self.proxy_status.config_editable();
    // 之前这里用的是 localProxyActive()，Starting/Stopping 两个状态下判据是错的。
    const bool editable = !rulesBusy_ && context_.proxy().status().configEditable();
    if (netEditableHost_ != nullptr) {
        netEditableHost_->setEnabled(editable);
    }
    if (netEditableNoticeLabel_ != nullptr) {
        netEditableNoticeLabel_->setText(UiText::netBusyNotice());
        netEditableNoticeLabel_->setVisible(!editable);
    }
}

void ProxyPage::rebuildDnsRows()
{
    if (dnsRowsBox_ == nullptr) {
        return;
    }
    // Rust 渲染时的副作用：整列表空着就先塞一条默认主 DNS（只塞一条）。
    if (networkOptions_.dns.nameservers.isEmpty()) {
        networkOptions_.dns.nameservers.append(QLatin1String(Proxy::kDefaultDnsPrimary));
    }

    QLayoutItem *item = nullptr;
    while ((item = dnsRowsBox_->takeAt(0)) != nullptr) {
        if (item->widget() != nullptr) {
            delete item->widget();
        }
        delete item;
    }
    dnsEdits_.clear();

    for (int index = 0; index < networkOptions_.dns.nameservers.size(); ++index) {
        auto *row = new QWidget(dnsRowsHost_);
        auto *rowBox = new QHBoxLayout(row);
        rowBox->setContentsMargins(0, 0, 0, 0);
        rowBox->setSpacing(10);

        // Rust 的行标签是 format!("DNS {}", index + 1)：ASCII 名 + 一个空格 + 1 起的序号。
        rowBox->addWidget(new QLabel(QStringLiteral("DNS %1").arg(index + 1), row));

        auto *edit = new QLineEdit(row);
        edit->setPlaceholderText(QString::fromLatin1(index == 0 ? Proxy::kDefaultDnsPrimary
                                                                : Proxy::kDefaultDnsSecondary));
        edit->setText(networkOptions_.dns.nameservers.at(index));
        rowBox->addWidget(edit, 1);
        dnsRowsBox_->addWidget(row);
        dnsEdits_.push_back(edit);
    }
}

void ProxyPage::saveNetworkConfig()
{
    if (netModeTunRadio_ != nullptr) {
        networkOptions_.mode = netModeTunRadio_->isChecked() ? Proxy::ProxyMode::Tun : Proxy::ProxyMode::System;
    }
    if (netKernelPrefCombo_ != nullptr) {
        networkOptions_.kernelPreference = Kernel::Preference(netKernelPrefCombo_->currentData().toInt());
    }
    if (netMixedPortSpin_ != nullptr) {
        networkOptions_.mixedPort = static_cast<quint16>(netMixedPortSpin_->value());
    }
    if (netControllerPortSpin_ != nullptr) {
        networkOptions_.controllerPort = static_cast<quint16>(netControllerPortSpin_->value());
    }
    if (netAllowLanCheck_ != nullptr) {
        networkOptions_.allowLan = netAllowLanCheck_->isChecked();
    }
    if (netTerminalProxyCheck_ != nullptr) {
        networkOptions_.terminalProxy = netTerminalProxyCheck_->isChecked();
    }

    if (netTunStackCombo_ != nullptr) {
        networkOptions_.tun.stack = Proxy::TunStack(netTunStackCombo_->currentData().toInt());
    }
    if (netTunAutoRouteCheck_ != nullptr) {
        networkOptions_.tun.autoRoute = netTunAutoRouteCheck_->isChecked();
    }
    if (netTunAutoDetectCheck_ != nullptr) {
        networkOptions_.tun.autoDetectInterface = netTunAutoDetectCheck_->isChecked();
    }

    if (netDnsEnabledCheck_ != nullptr) {
        networkOptions_.dns.enabled = netDnsEnabledCheck_->isChecked();
    }
    if (netDnsIpv6Check_ != nullptr) {
        networkOptions_.dns.ipv6 = netDnsIpv6Check_->isChecked();
    }
    if (netDnsEnhancedCombo_ != nullptr) {
        networkOptions_.dns.enhancedMode = Proxy::DnsEnhancedMode(netDnsEnhancedCombo_->currentData().toInt());
    }

    QStringList ns;
    for (QLineEdit *edit : dnsEdits_) {
        if (edit != nullptr) {
            ns.append(edit->text());
        }
    }
    if (!ns.isEmpty()) {
        networkOptions_.dns.nameservers = ns;
    }

    // Rust 的 save_proxy_config：先占 busy + 报"正在保存"，回来才报保存成功/失败。
    const auto opts = networkOptions_;
    setRulesBusy(true);
    emit statusMessageRequested(UiText::savingConfig());
    QPointer<ProxyPage> guard(this);
    auto failure = std::make_shared<QString>();
    context_.runAsync(
        [ctx = &context_, opts, failure]() {
            const auto applied = ctx->proxy().updateConfig(opts);
            if (applied.isErr()) {
                *failure = applied.error();
                return;
            }
            const auto json = opts.toSavedJsonText();
            if (json.isOk()) {
                ctx->database().setProxyConfigJsonText(json.value());
            }
        },
        [guard, failure]() {
            if (guard.isNull()) {
                return;
            }
            guard->setRulesBusy(false);
            emit guard->statusMessageRequested(failure->isEmpty()
                                                  ? UiText::configSaved()
                                                  : UiText::configSaveFailed().arg(*failure));
        });
}

QString ProxyPage::networkNoticeText() const
{
    if (netEditableNoticeLabel_ == nullptr || netEditableNoticeLabel_->isHidden()) {
        return QString();
    }
    return netEditableNoticeLabel_->text();
}

bool ProxyPage::networkControlsEnabled() const
{
    return netEditableHost_ != nullptr && netEditableHost_->isEnabled();
}

bool ProxyPage::networkTunGroupVisible() const
{
    return netTunGroup_ != nullptr && !netTunGroup_->isHidden();
}

int ProxyPage::networkDnsRowCount() const
{
    return dnsEdits_.size();
}

QString ProxyPage::networkDnsRowLabel(int index) const
{
    if (index < 0 || index >= dnsEdits_.size() || dnsEdits_.at(index) == nullptr) {
        return QString();
    }
    auto *row = qobject_cast<QWidget *>(dnsEdits_.at(index)->parentWidget());
    if (row != nullptr) {
        for (QLabel *label : row->findChildren<QLabel *>()) {
            return label->text();
        }
    }
    return QString();
}

QWidget *ProxyPage::buildKernelsPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("KernelsPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(8);

    makeSectionTitle(UiText::kernelSectionTitle(), UiText::kernelSectionHint(), panel);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    auto *dirHint = new QLabel(UiText::kernelStorageHint(), panel);
    dirHint->setObjectName(QStringLiteral("Muted"));
    actions->addWidget(dirHint);
    actions->addStretch();

    kernelCheckButton_ = new QPushButton(UiText::kernelCheckUpdates(), panel);
    connect(kernelCheckButton_, &QPushButton::clicked, this, [this]() {
        emit statusMessageRequested(UiText::kernelCheckingStatus());
        reloadKernels(/*fetchRemote=*/true);
    });
    actions->addWidget(kernelCheckButton_);

    kernelCancelButton_ = new QPushButton(UiText::kernelCancelDownload(), panel);
    kernelCancelButton_->setObjectName(QStringLiteral("Danger"));
    kernelCancelButton_->setVisible(false);
    connect(kernelCancelButton_, &QPushButton::clicked, this, &ProxyPage::cancelKernelDownload);
    actions->addWidget(kernelCancelButton_);
    box->addLayout(actions);

    kernelProgressLabel_ = new QLabel(panel);
    kernelProgressLabel_->setObjectName(QStringLiteral("Accent"));
    kernelProgressLabel_->setVisible(false);
    box->addWidget(kernelProgressLabel_);

    auto *card = new QFrame(panel);
    Theme::decorateCard(card);
    kernelTable_ = card;
    kernelGrid_ = new QGridLayout(card);
    kernelGrid_->setContentsMargins(Theme::kPadCard, Theme::kPadCard, Theme::kPadCard, Theme::kPadCard);
    kernelGrid_->setSpacing(8);
    layoutKernelColumns();
    box->addWidget(card);
    box->addStretch();

    return panel;
}

/// 首次打开自动补齐三颗内核（vcc 独有：Rust 是用户在内核面板手工点"下载"）。
/// 一条后台任务顺序下三颗，不阻塞界面；进度走内核面板原有的 `setKernelProgress`，
/// 所以"正在下哪一颗、下到哪儿"在界面上是看得见的；下完再 `reloadKernels(false)`
/// 让面板显示真实版本（不在状态条里编成功文案）。
/// 新装包第一次打开控制台时替他把三件事做完：拉远端内核版本、补上缺的内核、补上缺的 geo
/// 库。判据是"缺不缺"而不是"是不是第一次" —— 三样都齐的老安装里这个函数直接返回，于是
/// 不会每次开机都去敲 GitHub API（未认证只有 60 次/小时；Rust 的 reload_all 同一口径，
/// 启动只做本地探测）。
void ProxyPage::autoPrepareOnStartup()
{
    // 一个进程只试一次：MainWindow 和测试/探针会构造好几个 ProxyPage，每次都开下载就是
    // 重复拉 170 MB。
    static bool alreadyTried = false;
    if (alreadyTried) {
        return;
    }
    alreadyTried = true;

    // 离屏（单元测试 / 内存探针）里绝不联网下载：那是夹具在跑，不是用户打开了软件。
    if (qEnvironmentVariable("QT_QPA_PLATFORM").compare(QLatin1String("offscreen"),
                                                        Qt::CaseInsensitive) == 0) {
        return;
    }

    QVector<Kernel::Kind> missing;
    for (Kernel::Kind kind : {Kernel::Kind::Mihomo, Kernel::Kind::SingBox, Kernel::Kind::Xray}) {
        if (!Kernel::kernelIsAvailable(kind)) {
            missing.append(kind);
        }
    }
    const bool geoMissing = !Geo::geoReady(Geo::mihomoDir());
    if (missing.isEmpty() && !geoMissing) {
        return;
    }

    // ① 远端版本：走内核管理栏「检查更新」那同一条路，只是这里替他先点一次。
    reloadKernels(true);

    auto reportBox = std::make_shared<QStringList>();
    auto geoReport = std::make_shared<QString>();
    const QString root = Kernel::managedKernelRoot();
    const Proxy::ProxyRuntimeStatus proxyStatus = context_.proxy().status();
    const QString appProxy = proxyStatus.localProxyActive() && proxyStatus.mixedPort > 0
        ? QStringLiteral("http://127.0.0.1:%1").arg(proxyStatus.mixedPort)
        : QString();
    QPointer<ProxyPage> guard(this);
    setKernelBusy(true);

    context_.runAsync(
        [missing, root, appProxy, reportBox, geoReport, guard, geoMissing]() {
            // ② geo 排在内核前面：geo 只是两条几十 MB 的 HTTP，而内核还要解压落盘；
            //    先把 geo 补上，他第一次点「启动代理」就不会卡在缺库的启动预算上。
            if (geoMissing) {
                const auto provisioned = Geo::provision(Geo::mihomoDir(), QString(), false);
                if (provisioned.isOk()) {
                    *geoReport = provisioned.value();
                    reportBox->append(QStringLiteral("geo ok: %1").arg(provisioned.value()));
                } else {
                    reportBox->append(QStringLiteral("geo error: %1").arg(provisioned.error()));
                }
            }
            if (missing.isEmpty()) {
                return;
            }
            const Updater::KernelUpdater updater = Updater::KernelUpdater::create(root, appProxy);
            const Updater::CancelToken cancel;
            for (Kernel::Kind kind : missing) {
                const Result<Kernel::UpdateResult> done = updater.update(
                    kind, Kernel::Source::GhProxy, QString(), cancel,
                    [guard](const Kernel::UpdateProgress &progress) {
                        if (guard.isNull()) {
                            return;
                        }
                        QMetaObject::invokeMethod(guard, [guard, progress]() {
                            if (!guard.isNull()) {
                                guard->setKernelProgress(progress);
                            }
                        }, Qt::QueuedConnection);
                    });
                const QString name = Kernel::kindDisplayName(kind);
                reportBox->append(done.isOk()
                                      ? QStringLiteral("%1 %2").arg(name, done.value().version)
                                      : QStringLiteral("%1 error: %2").arg(name, done.error()));
            }
        },
        [guard, reportBox, geoReport]() {
            if (guard.isNull()) {
                return;
            }
            guard->setKernelBusy(false);
            guard->setKernelProgress(std::nullopt);
            for (const QString &line : *reportBox) {
                // 只用 ASCII 前缀：这条是运行日志，不该往"中文文案必须与 Rust 逐字一致"
                // 的账上添新债（界面真相走内核面板的版本/忙态，不在这里编成功文案）。
                Log::info(QStringLiteral("auto prepare: %1").arg(line));
            }
            if (!geoReport->isEmpty()) {
                emit guard->statusMessageRequested(
                    UiText::geoOfflineDone().arg(Geo::mihomoDir(), *geoReport));
            }
            guard->reloadKernels(false);
        });
}

void ProxyPage::reloadKernels(bool fetchRemote)
{
    // 本地"扫一下磁盘"并不便宜：detectAll() 为每颗内核跑一次 `--version` 子进程，这台
    // 机器实测 318 ms 才回。压在 GUI 线程上就是"点进内核管理冻一下"。Rust 那侧是
    // tokio::join! 并发探测，从来不在主线程做，所以这里整块挪进 runAsync：先让面板留着
    // 上一次的行（首次进栏是空表），探测回来再填。
    if (kernelDetecting_) {
        return;
    }
    const bool alsoRemote = fetchRemote && !kernelLoading_;
    kernelDetecting_ = true;
    kernelLoading_ = alsoRemote;
    if (kernelCheckButton_ != nullptr) {
        kernelCheckButton_->setEnabled(!alsoRemote);
    }

    auto detectBox = std::make_shared<QList<Kernel::Info>>();
    auto releaseBox = std::make_shared<QList<Kernel::ReleaseInfo>>();
    const auto proxyStatus = context_.proxy().status();
    const QString appProxy = proxyStatus.localProxyActive() && proxyStatus.mixedPort > 0
        ? QStringLiteral("http://127.0.0.1:%1").arg(proxyStatus.mixedPort)
        : QString();
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [detectBox, releaseBox, appProxy, alsoRemote]() {
            Kernel::Manager detector;
            *detectBox = detector.detectAll();
            if (!alsoRemote) {
                return;
            }
            // 远端版本检查接在同一条后台任务后面：它要用刚探到的本地信息，而且更慢（走网络）。
            const QString root = QStringLiteral("data/kernels");
            const Updater::KernelUpdater updater = Updater::KernelUpdater::create(root, appProxy);
            for (const Kernel::Info &info : *detectBox) {
                const Result<Kernel::ReleaseInfo> checked = updater.check(info);
                releaseBox->push_back(Kernel::releaseInfoFromCheck(checked, info));
            }
        },
        [guard, detectBox, releaseBox, alsoRemote]() {
            if (guard.isNull()) {
                return;
            }
            guard->kernelDetecting_ = false;
            guard->kernelInfos_ = *detectBox;
            if (alsoRemote) {
                guard->kernelLoading_ = false;
                if (guard->kernelCheckButton_ != nullptr) {
                    guard->kernelCheckButton_->setEnabled(true);
                }
                guard->kernelReleases_ = *releaseBox;
                QStringList parts;
                for (const Kernel::ReleaseInfo &release : *releaseBox) {
                    // Rust: format!("{} {}", kind.display_name(), latest.unwrap_or("未取到最新版本"))
                    parts.push_back(QStringLiteral("%1 %2")
                                        .arg(Kernel::kindDisplayName(release.kind),
                                             release.latestVersion.isEmpty()
                                                 ? UiText::kernelNoLatest()
                                                 : release.latestVersion));
                }
                emit guard->statusMessageRequested(
                    UiText::kernelCheckDone().arg(parts.join(UiText::middleDotJoin())));
            }
            guard->refreshKernelRows();
        });
}

void ProxyPage::refreshKernelRows()
{
    kernelRows_ = buildKernelRows(kernelInfos_, kernelReleases_, kernelBusy_,
                                 context_.proxy().status());
    layoutKernelColumns();
}

void ProxyPage::layoutKernelColumns()
{
    if (kernelGrid_ == nullptr) {
        return;
    }
    kernelGrid_->setHorizontalSpacing(16);
    kernelGrid_->setVerticalSpacing(12);

    while (QLayoutItem *item = kernelGrid_->takeAt(0)) {
        if (item->widget() != nullptr) {
            delete item->widget();
        }
        delete item;
    }

    const QVector<QString> headers = {
        UiText::kernelColumnKind(),    UiText::kernelColumnStatus(),  UiText::kernelColumnVersion(),
        UiText::kernelColumnLatest(),  UiText::kernelColumnPath(),    UiText::kernelColumnAction()};

    for (int col = 0; col < headers.size(); ++col) {
        auto *label = new QLabel(headers.at(col), kernelTable_);
        label->setObjectName(QStringLiteral("Subtitle"));
        Gui::Theme::setWidgetCss(label, QStringLiteral("font-weight: 700; font-size: 13px; color: {muted}; padding-bottom: 4px;"));
        kernelGrid_->addWidget(label, 0, col);
    }

    auto *headerSep = new QFrame(kernelTable_);
    headerSep->setFrameShape(QFrame::HLine);
    Gui::Theme::setWidgetCss(headerSep, QStringLiteral("color: {borderSubtle}; max-height: 1px;"));
    kernelGrid_->addWidget(headerSep, 1, 0, 1, headers.size());

    for (int rowIdx = 0; rowIdx < kernelRows_.size(); ++rowIdx) {
        const KernelRow &row = kernelRows_.at(rowIdx);
        const int gridRow = rowIdx + 2;

        auto *name = new QLabel(row.name, kernelTable_);
        name->setObjectName(QStringLiteral("Title"));
        name->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
        name->setMinimumWidth(80);
        kernelGrid_->addWidget(name, gridRow, 0);

        auto *status = new QLabel(row.statusText, kernelTable_);
        status->setObjectName(row.statusOk ? QStringLiteral("Success") : QStringLiteral("Danger"));
        status->setMinimumWidth(80);
        kernelGrid_->addWidget(status, gridRow, 1);

        auto *version = new QLabel(row.version, kernelTable_);
        version->setToolTip(row.version);
        version->setMinimumWidth(100);
        kernelGrid_->addWidget(version, gridRow, 2);

        auto *latest = new QLabel(row.latest, kernelTable_);
        latest->setObjectName(row.latestIsWarning ? QStringLiteral("Warning") : QStringLiteral("Muted"));
        latest->setToolTip(row.latest);
        latest->setWordWrap(true);
        latest->setMinimumWidth(120);
        kernelGrid_->addWidget(latest, gridRow, 3);

        auto *path = new QLabel(row.path, kernelTable_);
        path->setToolTip(row.path);
        path->setObjectName(QStringLiteral("Muted"));
        path->setStyleSheet(QStringLiteral("font-size: 11px;"));
        kernelGrid_->addWidget(path, gridRow, 4);

        auto *btn = new QPushButton(row.actionText, kernelTable_);
        btn->setEnabled(row.actionEnabled);
        btn->setToolTip(row.hover);
        btn->setFixedWidth(90);
        btn->setFixedHeight(30);
        btn->setObjectName(QStringLiteral("Small"));
        const Kernel::Kind kind = row.kind;
        connect(btn, &QPushButton::clicked, this, [this, kind]() {
            openKernelDownloadModal(kind);
        });
        kernelGrid_->addWidget(btn, gridRow, 5);
    }

    kernelGrid_->setColumnStretch(0, 0);
    kernelGrid_->setColumnStretch(1, 0);
    kernelGrid_->setColumnStretch(2, 0);
    kernelGrid_->setColumnStretch(3, 1);
    kernelGrid_->setColumnStretch(4, 2);
    kernelGrid_->setColumnStretch(5, 0);
}

void ProxyPage::openKernelDownloadModal(Kernel::Kind kind)
{
    const auto proxyStatus = context_.proxy().status();
    if (proxyStatus.kernelInUse(kind)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("停止代理后才能更新 %1").arg(Kernel::kindDisplayName(kind)));
        return;
    }
    if (kernelBusy_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("正在下载或更新内核，请稍候"));
        return;
    }

    const QString currentSourceId = context_.settings().kernelSource;
    const Kernel::Source defaultSource = Kernel::sourceFromId(currentSourceId);

    KernelDownloadDialog dlg(kind, defaultSource, proxyStatus.localProxyActive(), this);
    if (dlg.exec() == QDialog::Accepted) {
        const Kernel::Source source = dlg.selectedSource();
        const QString localFile = dlg.localFilePath();

        Models::UpdateSettings patch;
        patch.kernelSource = Kernel::sourceId(source);
        context_.saveSettings(patch);

        emit kernelUpdateRequested(kind);
        startKernelUpdate(kind, source, localFile);
    }
}

void ProxyPage::startKernelUpdate(Kernel::Kind kind, Kernel::Source source, const QString &localFile)
{
    if (kernelBusy_) {
        return;
    }
    const auto proxyStatus = context_.proxy().status();
    if (proxyStatus.kernelInUse(kind)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("停止代理后才能更新 %1").arg(Kernel::kindDisplayName(kind)));
        return;
    }

    setKernelBusy(true);
    setKernelProgress(std::nullopt);

    Updater::CancelToken cancelToken;
    cancelToken_ = cancelToken;

    const QString appProxy = proxyStatus.localProxyActive()
        ? QStringLiteral("http://127.0.0.1:%1").arg(proxyStatus.mixedPort)
        : QString();

    const QString root = QStringLiteral("data/kernels");

    auto resultBox = std::make_shared<Result<Kernel::UpdateResult>>(
        Result<Kernel::UpdateResult>::withError(QStringLiteral("未执行")));

    QPointer<ProxyPage> guard(this);

    emit statusMessageRequested(QStringLiteral("正在下载并校验 %1（%2）...")
                                    .arg(Kernel::kindDisplayName(kind), Kernel::sourceLabel(source)));

    context_.runAsync(
        [kind, source, localFile, cancelToken, appProxy, root, resultBox, guard]() {
            const Updater::KernelUpdater updater = Updater::KernelUpdater::create(root, appProxy);
            *resultBox = updater.update(
                kind, source, localFile, cancelToken,
                [guard](const Kernel::UpdateProgress &progress) {
                    if (guard.isNull()) return;
                    QMetaObject::invokeMethod(guard, [guard, progress]() {
                        if (!guard.isNull()) {
                            guard->setKernelProgress(progress);
                        }
                    }, Qt::QueuedConnection);
                });
        },
        [guard, kind, resultBox]() {
            if (guard.isNull()) {
                return;
            }
            guard->setKernelBusy(false);
            guard->setKernelProgress(std::nullopt);
            guard->cancelToken_.reset();

            if (resultBox->isOk()) {
                const Kernel::UpdateResult &res = resultBox->value();
                // Rust 直接把作业返回的整句话写进状态条（"X 已更新至 Y"，兼容模式还会补后缀）。
                const QString message = res.message.isEmpty()
                    ? QStringLiteral("%1 %2").arg(Kernel::kindDisplayName(kind), res.version)
                    : res.message;
                guard->emit statusMessageRequested(message);
                QMessageBox::information(guard, QStringLiteral("下载完成"), message);
            } else {
                const QString message = UiText::kernelUpdateFailed().arg(resultBox->error());
                guard->emit statusMessageRequested(message);
                QMessageBox::warning(guard, QStringLiteral("下载失败"), message);
            }
            guard->reloadKernels();
        });
}

void ProxyPage::cancelKernelDownload()
{
    if (cancelToken_.has_value()) {
        cancelToken_->cancel();
    }
    emit kernelCancelRequested();
    emit statusMessageRequested(UiText::kernelCancellingStatus());
}

QWidget *ProxyPage::buildLogsPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("LogsPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);

    // Rust 的标题是固定的"运行日志"，内核名在副标题里（"{kernel} 最近输出"）。
    makeSectionTitle(UiText::logsTitle(),
                     UiText::logsSubtitle().arg(UiText::logsKernelFallback()),
                     panel, &logSubtitleLabel_);

    auto *headerLine = new QHBoxLayout();
    headerLine->setSpacing(10);
    headerLine->addStretch();

    logAutoScrollCheck_ = new QCheckBox(QStringLiteral("自动滚到底部"), panel);
    logAutoScrollCheck_->setChecked(true);
    headerLine->addWidget(logAutoScrollCheck_);

    logCopyBtn_ = new QPushButton(QStringLiteral("复制全部"), panel);
    connect(logCopyBtn_, &QPushButton::clicked, this, &ProxyPage::copyLogs);
    headerLine->addWidget(logCopyBtn_);

    logClearBtn_ = new QPushButton(QStringLiteral("清空"), panel);
    connect(logClearBtn_, &QPushButton::clicked, this, &ProxyPage::clearLogs);
    headerLine->addWidget(logClearBtn_);

    box->addLayout(headerLine);

    // Rust 把日志装在同一张卡里；空日志是卡内一条 muted 文案，不是输入框占位符。
    auto *logCard = new QFrame(panel);
    Theme::decorateCard(logCard);
    auto *logCardBox = new QVBoxLayout(logCard);
    logCardBox->setContentsMargins(14, 12, 14, 12);
    logCardBox->setSpacing(8);

    logEmptyLabel_ = new QLabel(UiText::logsEmpty(), logCard);
    logEmptyLabel_->setObjectName(QStringLiteral("Muted"));
    logEmptyLabel_->setAlignment(Qt::AlignCenter);
    logEmptyLabel_->setVisible(false);
    logCardBox->addWidget(logEmptyLabel_);

    logView_ = new QPlainTextEdit(logCard);
    logView_->setObjectName(QStringLiteral("LogView"));
    logView_->setReadOnly(true);
    logCardBox->addWidget(logView_, 1);

    box->addWidget(logCard, 1);

    return panel;
}

void ProxyPage::reloadLogs()
{
    const auto status = context_.proxy().status();
    if (logSubtitleLabel_ != nullptr) {
        const QString kernelName = status.kernel.has_value()
            ? Kernel::kindDisplayName(*status.kernel)
            : UiText::logsKernelFallback();
        logSubtitleLabel_->setText(UiText::logsSubtitle().arg(kernelName));
    }

    // Rust 的读取上限是 12_000 字符，出错时状态条报"读取代理日志失败: {error}"。
    struct Read {
        QString text;
        QString error;
    };
    auto logBox = std::make_shared<Read>();
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [ctx = &context_, logBox]() {
            const auto res = ctx->proxy().recentLog(12000);
            if (res.isOk()) {
                logBox->text = res.value();
            } else {
                logBox->error = res.error();
            }
        },
        [guard, logBox]() {
            if (guard.isNull()) {
                return;
            }
            if (!logBox->error.isEmpty()) {
                emit guard->statusMessageRequested(UiText::logReadFailed().arg(logBox->error));
            }
            if (guard->logView_ == nullptr) {
                return;
            }
            const bool empty = logBox->text.isEmpty();
            if (guard->logEmptyLabel_ != nullptr) {
                guard->logEmptyLabel_->setVisible(empty);
            }
            guard->logView_->setVisible(!empty);
            const QString current = guard->logView_->toPlainText();
            if (current != logBox->text) {
                guard->logView_->setPlainText(logBox->text);
                if (guard->logAutoScrollCheck_ != nullptr && guard->logAutoScrollCheck_->isChecked()) {
                    guard->logView_->verticalScrollBar()->setValue(
                        guard->logView_->verticalScrollBar()->maximum());
                }
            }
        });
}

void ProxyPage::clearLogs()
{
    if (logView_ != nullptr) {
        logView_->clear();
    }
}

void ProxyPage::copyLogs()
{
    if (logView_ != nullptr) {
        QApplication::clipboard()->setText(logView_->toPlainText());
        emit statusMessageRequested(QStringLiteral("已复制运行日志到剪贴板"));
    }
}

void ProxyPage::onPanelSwitch()
{
    // 取证：页内 tab 切换的同步代价（Rules/Network 读代理配置也要拿 runtimeMutex_）。
    QElapsedTimer clock;
    clock.start();
    refresh();
    const qint64 ms = clock.elapsed();
    if (ms > 30) {
        Log::warn(QStringLiteral("控制台页内切换 panel=%1 耗时 %2 ms")
                      .arg(int(currentPanel())).arg(ms));
    }
}

void ProxyPage::onPollTimer()
{
    QElapsedTimer clock;
    clock.start();
    const auto status = context_.proxy().status();
    refreshStatusCard();

    if (currentPanel() == Panel::Connections && status.localProxyActive()) {
        reloadConnections();
    } else if (currentPanel() == Panel::Logs && status.localProxyActive()) {
        reloadLogs();
    } else if (status.localProxyActive()) {
        reloadConnections();
    }
    // 取证：每 1.5 秒一次的轮询如果耗时异常，说明 status() 在等 runtimeMutex_
    // （代理启动/切换期间 startWithPath 全程持锁）。
    const qint64 ms = clock.elapsed();
    if (ms > 50) {
        Log::warn(QStringLiteral("onPollTimer 耗时 %1 ms").arg(ms));
    }
}

void ProxyPage::refresh()
{
    refreshStatusCard();

    switch (currentPanel()) {
    case Panel::Nodes:
        if (allNodes_.isEmpty()) {
            reloadNodes();
        } else {
            applyView();
        }
        break;
    case Panel::Connections:
        reloadConnections();
        break;
    case Panel::Rules:
        reloadRules();
        break;
    case Panel::Network:
        reloadNetwork();
        break;
    case Panel::Kernels:
        if (kernelInfos_.isEmpty() && !kernelLoading_) {
            reloadKernels();
        } else {
            refreshKernelRows();
        }
        break;
    case Panel::Logs:
        reloadLogs();
        break;
    }
}

void ProxyPage::reloadNodes()
{
    if (nodesRequested_) {
        return;
    }
    nodesRequested_ = true;
    auto nodesBox = std::make_shared<QVector<Models::NodeSummary>>();
    auto errorBox = std::make_shared<QString>();
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [ctx = &context_, nodesBox, errorBox]() {
            const auto res = ctx->database().loadAvailableNodeSummaries(QString());
            if (res.isOk()) {
                *nodesBox = res.value();
            } else {
                *errorBox = res.error();
            }
        },
        [guard, nodesBox, errorBox]() {
            if (guard.isNull()) {
                return;
            }
            guard->nodesRequested_ = false;
            if (!errorBox->isEmpty()) {
                // 一个分库读失败以前会被静默当成"没有可用节点"（列表整片空白，日志里
                // 一个字都没有）。这里认账：留空缓存，下一次 refresh() 会再查一遍。
                Log::warn(QStringLiteral("console node pool load failed: %1").arg(*errorBox));
                emit guard->statusMessageRequested(
                    QStringLiteral("可用节点读取失败，稍后自动重试: %1").arg(*errorBox));
                return;
            }
            guard->allNodes_ = *nodesBox;
            guard->applyView();
        });
}

quint64 ProxyPage::viewSignature(const Proxy::ProxyRuntimeStatus &status) const
{
    // FNV-1a 滚动混合：顺序也参与，所以"同样的行换个顺序"是要重画的。
    quint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](quint64 value) { hash = (hash ^ value) * 1099511628211ULL; };
    mix(dataRev_);
    // 主题进签名：切主题后卡片皮肤/徽章/延迟分档色都要重取一遍。不加这一条，
    // applyView() 会因为"内容没变"直接跳过，卡片维持上一个主题的自绘配色。
    mix(qHash(Gui::Theme::currentId()));
    mix(qHash(regionFilter_));
    mix(qHash(search_));
    mix(quint64(sortBy_));
    // 页码进签名：只变页码时网格必须重画（否则调用方得手动把签名清零）。
    mix(quint64(nodePage_));
    mix(quint64(gridColumns()));
    mix(quint64(status.state));
    mix(status.localProxyActive() ? 1ULL : 0ULL);
    mix(qHash(status.nodeId));
    mix(qHash(selectedNodeId_));
    for (const ProxyViewRow &row : view_.rows) {
        mix(qHash(row.node.id));
        mix(quint64(row.node.latencyMs.value_or(-1)));
        mix(quint64(row.node.downloadKbps.value_or(-1.0) * 8.0));
    }
    return hash;
}

void ProxyPage::applyView()
{
    view_ = buildProxyView(allNodes_, regionFilter_, search_, sortBy_, dataRev_);
    if (countLabel_ != nullptr) {
        countLabel_->setText(countText());
    }
    if (grid_ == nullptr) {
        return;
    }

    // 整网格只取一次代理状态：每张卡都去抢 runtimeMutex_ 的话，代理启动/切换期间
    // （锁被秒级持有）会把 GUI 线程直接冻住。
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();

    // 内容、筛选、列数、选中项、代理状态都没变 —— 一张卡都不动。
    const quint64 signature = viewSignature(status);
    if (signature == lastViewSignature_) {
        return;
    }
    lastViewSignature_ = signature;

    rebuildNodeGrid(status);
}

void ProxyPage::rebuildNodeGrid(const Proxy::ProxyRuntimeStatus &status)
{
    ++gridRebuilds_;
    const int cols = gridColumns();
    // 自动选路开关整轮只读一次：autoRoutingEnabled() 走 proxy().config()，那把
    // runtimeMutex_ 在代理启动/切换时会被秒级持有 —— 每张卡都去抢一次，GUI 线程
    // 就跟着冻住（这是这个仓库反复踩过的那条，见 NodesPage::applyView 的同款注释）。
    const bool autoRouting = autoRoutingEnabled();
    // 只画当前页：以前 245 个节点就是 245 张常驻卡片，切一次地区还要整套重画。
    nodePage_ = std::clamp(nodePage_, 1, nodePageCount());
    const int first = (nodePage_ - 1) * nodePageSize_;
    const int wanted = std::min(nodePageSize_, static_cast<int>(view_.rows.size()) - first);

    // 上一轮的"垫高行"要按旧行数清掉：筛选后行数变少，旧的行拉伸因子还留在格子里，
    // 卡片就会被拉得比内容高（"只有一个节点时卡片特别大"就是这个）。
    for (int row = 0; row <= lastGridRows_; ++row) {
        grid_->setRowStretch(row, 0);
    }
    gridHost_->setUpdatesEnabled(false);
    while (QLayoutItem *item = grid_->takeAt(0)) {
        delete item;               // 只摘布局项：卡片本体留在池里复用
    }

    // 卡片按下标复用。以前切一次地区要新建 245 张卡（1470 个子控件），那才是
    // "筛选地区特别卡"的大头。多出来的卡片只藏不删：连着点几个筛选时，
    // deleteLater 的卡片要等事件循环才回收，实测 32 次点击能攒出 129MB。
    const int highWater = static_cast<int>(nodeCards_.size());
    if (wanted < highWater) {
        for (int index = wanted; index < highWater; ++index) {
            nodeCards_.at(index)->hide();
        }
    }
    while (nodeCards_.size() < wanted) {
        auto *card = new NodeCardFrame(gridHost_);
        card->setObjectName(QStringLiteral("NodeCard"));
        card->setCursor(Qt::PointingHandCursor);
        card->installEventFilter(this);
        // 右键菜单不写出来没人知道它存在（Rust 的 hover 提示就是干这个的）。
        card->setToolTip(autoRouting ? UiText::cardHoverAutoRouting() : UiText::cardHoverPlain());
        nodeCards_.push_back(card);
    }
    for (int index = 0; index < wanted; ++index) {
        NodeCardFrame *card = nodeCards_.at(index);
        card->bind(view_.rows.at(first + index), status, selectedNodeId_);
        grid_->addWidget(card, index / cols, index % cols);
        // 池里的卡是"只藏不删"的：上一轮筛空过就全部 hide() 了，而 addWidget 不会把隐藏
        // 的控件显示出来 —— 于是出现"计数有数、一张卡都没有"。复用必须连着 show。
        card->show();
    }
    // 列拉伸与节点池同一套（每列等分、卡片横向 Expanding）：不设的话同样 3 颗节点，
    // 控制台摊满整行、节点池只占左边一块，两张页面的卡就"不一样大"。
    // 列数变小时必须把多出来的那几列的拉伸清掉，否则它们还占宽度。
    // 按"格子里真的有几列"来等分，而不是按 cols：cols 是重建那一刻的视口宽度算的，
    // 面板晚布局/懒建时它会比实际用掉的列数小，于是最后一列拿不到拉伸 ——
    // 实测同一行里出现 405 / 404 / 231 三种宽度（他说的"不一样"有一半是这个）。
    const int usedColumns = grid_->columnCount();
    gridColumnHighWater_ = std::max(gridColumnHighWater_, usedColumns);
    for (int col = 0; col < gridColumnHighWater_; ++col) {
        grid_->setColumnStretch(col, col < usedColumns ? 1 : 0);
        // 最小宽度也必须清零：QGridLayout 的列宽是"该列内容的最大最小宽 + 拉伸份额"，
        // 第一列名字最长时它就比其他列宽（他 2026-10-12 报的"第一列比其他列宽"）。
        grid_->setColumnMinimumWidth(col, 0);
    }
    updateNodePager();
    lastGridRows_ = (wanted + cols - 1) / cols;
    // 富余高度全部交给最后一行之后的空行，卡片保持内容本身的高度。
    grid_->setRowStretch(lastGridRows_, 1);
    gridHost_->setUpdatesEnabled(true);
}

int ProxyPage::visibleCardCount() const
{
    int count = 0;
    for (NodeCardFrame *card : nodeCards_) {
        if (card->isVisible()) {
            ++count;
        }
    }
    return count;
}

int ProxyPage::nodePageCardCount() const
{
    // 数网格里的真卡片，不数池子：池按历史峰值留着（切回大筛选不重建），
    // 拿池大小当"本页卡片数"会在末页少一屏时报错。
    if (grid_ == nullptr) {
        return 0;
    }
    int count = 0;
    for (int index = 0; index < grid_->count(); ++index) {
        if (grid_->itemAt(index)->widget() != nullptr) {
            ++count;
        }
    }
    return count;
}

int ProxyPage::nodePageCount() const
{
    const int rows = static_cast<int>(view_.rows.size());
    return rows > 0 ? (rows + nodePageSize_ - 1) / nodePageSize_ : 1;
}

void ProxyPage::updateNodePager()
{
    if (nodePageLabel_ != nullptr) {
        nodePageLabel_->setText(UiText::pagerInfo()
                                    .arg(nodePage_)
                                    .arg(nodePageCount())
                                    .arg(static_cast<int>(view_.rows.size())));
    }
    if (nodePrevPageBtn_ != nullptr) {
        nodePrevPageBtn_->setEnabled(nodePage_ > 1);
    }
    if (nodeNextPageBtn_ != nullptr) {
        nodeNextPageBtn_->setEnabled(nodePage_ < nodePageCount());
    }
}

int ProxyPage::gridColumns() const
{
    const int available = nodeScroll_ != nullptr ? nodeScroll_->viewport()->width() : width();
    if (available >= 920) return 4;
    if (available >= 660) return 3;
    return 2;
}

bool ProxyPage::eventFilter(QObject *watched, QEvent *event)
{
    auto *card = qobject_cast<NodeCardFrame *>(watched);
    if (card == nullptr) {
        return Page::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ContextMenu) {
        showNodeCardMenu(card, static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return Page::eventFilter(watched, event);
        }
        const QString nodeId = card->property("nodeId").toString();
        const QString nodeName = card->property("nodeName").toString();
        if (nodeId.isEmpty()) {
            return Page::eventFilter(watched, event);
        }
        // 自动选路中左键不改出口：要固定得右键显式锁定（Rust proxy.rs:695-697）。
        if (autoRoutingEnabled() && card->state() == NodeCardState::Normal) {
            emit statusMessageRequested(UiText::autoRoutingClickIgnored());
            return true;
        }
        setSelectedNode(nodeId, nodeName);
        emit nodeSwitchRequested(nodeId, nodeName);
        return true;
    }
    return Page::eventFilter(watched, event);
}

bool ProxyPage::autoRoutingEnabled() const
{
    return Proxy::configForUi(context_.proxy().config()).urlTest;
}

QMenu *ProxyPage::buildNodeCardMenu(NodeCardFrame *card)
{
    const QString nodeId = card->property("nodeId").toString();
    const QString nodeName = card->property("nodeName").toString();
    if (nodeId.isEmpty()) {
        return nullptr;
    }
    auto *menu = new QMenu(this);
    // 与控制台之外的那张卡共用同一个菜单构造器；顺序按 Rust views/proxy.rs：
    // 详情 / 测速 / IP 检测 /（自动选路开着且不是当前出口时）── 锁定为出口。
    const bool lockEgress = autoRoutingEnabled() && card->state() == NodeCardState::Normal;
    const NodeCardMenu actions =
        addNodeCardMenu(menu, false, !context_.fullSpeedtestRunning(), false, lockEgress);
    connect(actions.details, &QAction::triggered, this,
            [this, nodeId]() { emit nodeDetailsRequested(nodeId); });
    connect(actions.speedtest, &QAction::triggered, this,
            [this, nodeId]() { emit nodeSpeedtestRequested(nodeId); });
    connect(actions.ipRisk, &QAction::triggered, this,
            [this, nodeId]() { emit nodeIpRiskRequested(nodeId); });
    if (actions.lockEgress != nullptr) {
        connect(actions.lockEgress, &QAction::triggered, this,
                [this, nodeId, nodeName]() { emit nodeEgressLockRequested(nodeId, nodeName); });
    }
    return menu;
}

void ProxyPage::showNodeCardMenu(NodeCardFrame *card, const QPoint &globalPos)
{
    QMenu *menu = buildNodeCardMenu(card);
    if (menu == nullptr) {
        return;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->exec(globalPos);
}

void ProxyPage::resizeEvent(QResizeEvent *event)
{
    Page::resizeEvent(event);
    if (currentPanel() == Panel::Nodes) {
        applyView();
    }
}

QStringList ProxyPage::regionPillCodes() const
{
    return {QStringLiteral("all"), QStringLiteral("HK"), QStringLiteral("JP"),
            QStringLiteral("US"), QStringLiteral("SG"), QStringLiteral("TW"),
            QStringLiteral("KR"), QStringLiteral("OTHER")};
}

QString ProxyPage::countText() const
{
    return SysProxy::fillTemplate(UiText::nodeCountTotal(), {}, {QString::number(allNodes_.size())});
}

QString ProxyPage::kernelRowText(int index) const
{
    if (index < 0 || index >= kernelRows_.size()) {
        return QString();
    }
    const KernelRow &row = kernelRows_.at(index);
    return QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(row.name, row.statusText, row.version, row.latest, row.path, row.actionText);
}

QString ProxyPage::kernelProgressText() const
{
    if (!kernelProgress_.has_value()) {
        return QString();
    }
    return Gui::kernelProgressText(*kernelProgress_);
}

void ProxyPage::setKernelProgress(const std::optional<Kernel::UpdateProgress> &progress)
{
    kernelProgress_ = progress;
    if (kernelProgressLabel_ != nullptr) {
        if (progress.has_value()) {
            kernelProgressLabel_->setText(kernelProgressText());
            kernelProgressLabel_->setVisible(true);
        } else {
            kernelProgressLabel_->setVisible(false);
        }
    }
}

void ProxyPage::setKernelBusy(bool busy)
{
    kernelBusy_ = busy;
    if (kernelCheckButton_ != nullptr) kernelCheckButton_->setEnabled(!busy);
    if (kernelCancelButton_ != nullptr) kernelCancelButton_->setVisible(busy);
    refreshKernelRows();
}

QString ProxyPage::stateBadgeText() const
{
    return stateBadge_ != nullptr ? stateBadge_->text() : QString();
}

QString ProxyPage::stateMessageText() const
{
    return stateMessage_ != nullptr ? stateMessage_->text() : QString();
}

QString ProxyPage::kernelInfoText() const
{
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();
    QString text;
    if (status.kernel.has_value()) {
        text += Kernel::kindDisplayName(*status.kernel) + QStringLiteral(" · ");
    }
    return text + QStringLiteral("mixed %1 · controller %2")
                       .arg(status.mixedPort)
                       .arg(status.controllerPort);
}

ProxyPage::Panel ProxyPage::currentPanel() const
{
    return Panel(panels_->currentIndex());
}

void ProxyPage::selectPanel(Panel panel)
{
    panels_->setCurrentIndex(int(panel));
}

QWidget *createProxyPage(App::AppContext &context, QWidget *parent)
{
    return new ProxyPage(context, parent);
}

} // namespace Gui
