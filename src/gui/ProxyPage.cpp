#include "gui/ProxyPage.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
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
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "app/AppContext.h"
#include "db/Database.h"
#include "geo/Geo.h"
#include "gui/CountryNames.h"
#include "gui/FlowLayout.h"
#include "gui/IpRiskDialog.h"
#include "gui/KernelPanel.h"
#include "gui/NodeText.h"
#include "gui/Prepare.h"
#include "gui/Theme.h"
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

bool anyKernelInstalled()
{
    for (Kernel::Kind kind : {Kernel::Kind::Mihomo, Kernel::Kind::SingBox, Kernel::Kind::Xray}) {
        if (Kernel::kernelIsAvailable(kind)) {
            return true;
        }
    }
    return false;
}

QLabel *makeSectionTitle(const QString &title, const QString &subtitle, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *t = new QLabel(title, container);
    t->setObjectName(QStringLiteral("Title"));
    t->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700;"));
    layout->addWidget(t);

    if (!subtitle.isEmpty()) {
        auto *s = new QLabel(subtitle, container);
        s->setObjectName(QStringLiteral("Subtitle"));
        s->setStyleSheet(QStringLiteral("font-size: 12px;"));
        layout->addWidget(s);
    }
    return t;
}

QString ruleMatcherLabel(const Proxy::RuleMatcher &m)
{
    switch (m.kind) {
    case Proxy::RuleMatcher::Kind::Domain:
        return QStringLiteral("DOMAIN, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::DomainSuffix:
        return QStringLiteral("DOMAIN-SUFFIX, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::GeoSite:
        return QStringLiteral("GEOSITE, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::GeoIp:
        return QStringLiteral("GEOIP, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::IpCidr:
        return QStringLiteral("IP-CIDR, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::DestinationPort:
        return QStringLiteral("DST-PORT, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::ProcessName:
        return QStringLiteral("PROCESS-NAME, %1").arg(m.value);
    case Proxy::RuleMatcher::Kind::Network:
        return QStringLiteral("NETWORK, %1").arg(Proxy::networkProtocolToText(m.network).toUpper());
    case Proxy::RuleMatcher::Kind::Quic:
        return QStringLiteral("QUIC 拦截 (UDP 443)");
    }
    return m.value;
}

QString routingModeDesc(Proxy::RoutingMode mode)
{
    switch (mode) {
    case Proxy::RoutingMode::Rule:
        return QStringLiteral("按下方规则列表从上到下逐条匹配，未匹配流量默认走代理");
    case Proxy::RoutingMode::ChinaDirect:
        return QStringLiteral("中国大陆域名与 IP 直连，常见境外网站走代理");
    case Proxy::RoutingMode::GfwList:
        return QStringLiteral("仅被封锁的境外域名走代理，其余网站全部直连");
    case Proxy::RoutingMode::Global:
        return QStringLiteral("所有网络流量全部经过代理服务器转发");
    case Proxy::RoutingMode::Direct:
        return QStringLiteral("所有网络流量全部直接连接，不经过任何代理");
    }
    return QString();
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
    panels_->addTab(UiText::panelNodes());
    panels_->addTab(UiText::panelConnections());
    panels_->addTab(UiText::panelRules());
    panels_->addTab(UiText::panelNetwork());
    panels_->addTab(UiText::panelKernels());
    panels_->addTab(UiText::panelLogs());
    root->addWidget(panels_);

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
        refresh();
    });
    root->addWidget(stack_, 1);

    // 定时轮询：每 1.5 秒更新流量读数与连接状态
    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &ProxyPage::onPollTimer);
    pollTimer_->start(1500);

    refresh();
}

ProxyPage::~ProxyPage() = default;

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

    stateMessage_ = new QLabel(card);
    stateMessage_->setObjectName(QStringLiteral("Subtitle"));
    stateMessage_->setVisible(false);
    row1->addWidget(stateMessage_);

    box->addLayout(row1);

    // 分隔线
    auto *sep = new QFrame(card);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName(QStringLiteral("Muted"));
    sep->setStyleSheet(QStringLiteral("color: #d8e2ed; max-height: 1px;"));
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

    row2->addStretch();

    statusRates_ = new QLabel(QStringLiteral("↓ 0 B/s  ↑ 0 B/s"), card);
    statusRates_->setObjectName(QStringLiteral("Subtitle"));
    statusRates_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row2->addWidget(statusRates_);

    box->addLayout(row2);

    // 第三行：自动切换状态 · 会话总计
    auto *row3 = new QHBoxLayout();
    row3->setSpacing(12);

    statusFailover_ = new QLabel(QStringLiteral("自动切换: 本次运行已切换 0 次"), card);
    statusFailover_->setObjectName(QStringLiteral("Muted"));
    statusFailover_->setStyleSheet(QStringLiteral("font-size: 11px;"));
    row3->addWidget(statusFailover_);

    row3->addStretch();

    statusSessionTraffic_ = new QLabel(QStringLiteral("会话 ↑ 0 B  ↓ 0 B"), card);
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
        stateBadge_->setObjectName(badge.role);
        Theme::apply(Theme::currentId());
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

    if (statusHealth_ != nullptr) {
        if (!status.lastError.isEmpty()) {
            statusHealth_->setText(QStringLiteral("⚠️ %1").arg(status.lastError));
            statusHealth_->setObjectName(QStringLiteral("Danger"));
        } else if (isRunning) {
            statusHealth_->setText(QStringLiteral("✓ 代理运行中"));
            statusHealth_->setObjectName(QStringLiteral("Success"));
        } else {
            statusHealth_->setText(QStringLiteral("未运行"));
            statusHealth_->setObjectName(QStringLiteral("Muted"));
        }
    }

    if (statusConnCount_ != nullptr) {
        statusConnCount_->setText(QStringLiteral("活跃连接: %1").arg(connections_.size()));
    }

    if (statusRates_ != nullptr) {
        if (isRunning) {
            statusRates_->setText(QStringLiteral("↓ %1  ↑ %2")
                                     .arg(formatSpeed(downloadRate_), formatSpeed(uploadRate_)));
        } else {
            statusRates_->setText(QStringLiteral("↓ 0 B/s  ↑ 0 B/s"));
        }
    }

    if (statusSessionTraffic_ != nullptr) {
        statusSessionTraffic_->setText(QStringLiteral("会话 ↑ %1  ↓ %2")
                                           .arg(formatBytes(uploadTotal_), formatBytes(downloadTotal_)));
    }

    if (statusFailover_ != nullptr) {
        statusFailover_->setText(QStringLiteral("自动切换: 本次运行已切换 %1 次").arg(failoverSwitches_));
    }
}

void ProxyPage::setSelectedNode(const QString &id, const QString &name)
{
    selectedNodeId_ = id;
    selectedNodeName_ = name;
    refreshStatusCard();
}

QWidget *ProxyPage::buildNodesPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("NodesPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(8);

    // 地区筛选药丸
    auto *regionRow = new QWidget(panel);
    auto *regionFlow = new FlowLayout(regionRow, 0, 6, 6);
    auto *regionCaption = new QLabel(UiText::regionLabel(), regionRow);
    regionCaption->setObjectName(QStringLiteral("Subtitle"));
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
            regionFilter_ = code;
            for (QPushButton *other : regionPills_) {
                other->setChecked(other->property("region").toString() == code);
            }
            applyView();
        });
        regionPills_.push_back(pill);
        regionFlow->addWidget(pill);
    }
    box->addWidget(regionRow);

    // 排序与搜索栏
    auto *sortRow = new QWidget(panel);
    auto *sortFlow = new FlowLayout(sortRow, 0, 6, 6);
    auto *sortCaption = new QLabel(UiText::sortLabel(), sortRow);
    sortCaption->setObjectName(QStringLiteral("Muted"));
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
        sortBy_ = ProxySortBy::Latency;
        sortLatency_->setChecked(true);
        sortName_->setChecked(false);
        applyView();
    });
    connect(sortName_, &QPushButton::clicked, this, [this]() {
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
        search_ = text.trimmed().toLower();
        applyView();
    });
    sortFlow->addWidget(searchEdit_);

    box->addWidget(sortRow);

    // 节点网格滚动区
    nodeScroll_ = new QScrollArea(panel);
    nodeScroll_->setWidgetResizable(true);
    nodeScroll_->setFrameShape(QFrame::NoFrame);
    nodeScroll_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    gridHost_ = new QWidget(nodeScroll_);
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 4, 0, 8);
    grid_->setSpacing(10);
    nodeScroll_->setWidget(gridHost_);
    box->addWidget(nodeScroll_, 1);

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

    auto *filterLabel = new QLabel(QStringLiteral("筛选:"), panel);
    filterLabel->setObjectName(QStringLiteral("Subtitle"));
    bar->addWidget(filterLabel);

    connFilterEdit_ = new QLineEdit(panel);
    connFilterEdit_->setPlaceholderText(QStringLiteral("按进程、域名或 IP 过滤"));
    connFilterEdit_->setFixedWidth(200);
    connect(connFilterEdit_, &QLineEdit::textChanged, this, [this]() { updateConnectionsTable(); });
    bar->addWidget(connFilterEdit_);

    auto *clearFilterBtn = new QPushButton(QStringLiteral("清空"), panel);
    connect(clearFilterBtn, &QPushButton::clicked, this, [this]() {
        if (connFilterEdit_ != nullptr) {
            connFilterEdit_->clear();
        }
    });
    bar->addWidget(clearFilterBtn);

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
    connEmptyLabel_ = new QLabel(QStringLiteral("代理未在运行中，暂无活跃连接数据"), connEmptyCard_);
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
    connTable_->setColumnWidth(0, 60);
    connTable_->setColumnWidth(1, 140);
    connTable_->setColumnWidth(3, 140);
    connTable_->setColumnWidth(4, 90);
    connTable_->setColumnWidth(5, 90);
    connTable_->setColumnWidth(6, 80);
    box->addWidget(connTable_, 1);

    connTable_->setVisible(false);
    return panel;
}

void ProxyPage::reloadConnections()
{
    const auto status = context_.proxy().status();
    if (!status.localProxyActive()) {
        connections_.clear();
        updateConnectionsTable();
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

                        QString rule = JsonUtil::textOrEmpty(item, "rule");
                        const QString payload = JsonUtil::textOrEmpty(item, "rulePayload");
                        if (!payload.isEmpty()) {
                            rule += QStringLiteral("(%1)").arg(payload);
                        }
                        conn.rule = rule.isEmpty() ? QStringLiteral("-") : rule;

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

            updateConnectionsTable();
            refreshStatusCard();
        });
}

void ProxyPage::updateConnectionsTable()
{
    const bool isRunning = context_.proxy().status().localProxyActive();
    if (!isRunning) {
        if (connTable_ != nullptr) connTable_->setVisible(false);
        if (connEmptyCard_ != nullptr) connEmptyCard_->setVisible(true);
        if (connEmptyLabel_ != nullptr) connEmptyLabel_->setText(QStringLiteral("代理未在运行中，暂无活跃连接数据"));
        if (connCountLabel_ != nullptr) connCountLabel_->setText(QStringLiteral("当前连接总数: 0"));
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
            connEmptyLabel_->setText(connections_.isEmpty() ? QStringLiteral("当前暂无活动的网络连接")
                                                            : QStringLiteral("没有匹配筛选条件的网络连接"));
        }
        return;
    }

    if (connEmptyCard_ != nullptr) connEmptyCard_->setVisible(false);
    if (connTable_ != nullptr) {
        connTable_->setVisible(true);
        connTable_->setRowCount(filtered.size());
        for (int row = 0; row < filtered.size(); ++row) {
            const auto &c = filtered.at(row);
            connTable_->setItem(row, 0, new QTableWidgetItem(c.network.toUpper()));
            connTable_->setItem(row, 1, new QTableWidgetItem(c.process));
            const QString target = !c.host.isEmpty() ? QStringLiteral("%1 (%2)").arg(c.host, c.destination) : c.destination;
            connTable_->setItem(row, 2, new QTableWidgetItem(target));
            connTable_->setItem(row, 3, new QTableWidgetItem(c.rule));
            connTable_->setItem(row, 4, new QTableWidgetItem(formatBytes(c.upload)));
            connTable_->setItem(row, 5, new QTableWidgetItem(formatBytes(c.download)));

            auto *btn = new QPushButton(QStringLiteral("断开"), connTable_);
            btn->setObjectName(QStringLiteral("Small"));
            const QString id = c.id;
            connect(btn, &QPushButton::clicked, this, [this, id]() { closeConnection(id); });
            connTable_->setCellWidget(row, 6, btn);
        }
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
        });
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
        });
}

QWidget *ProxyPage::buildRulesPanel()
{
    auto *scroll = new QScrollArea(stack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    auto *panel = new QWidget(scroll);
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(12);

    // 1. 今日规则命中
    auto *hitsCard = new QFrame(panel);
    Theme::decorateCard(hitsCard);
    auto *hitsBox = new QVBoxLayout(hitsCard);
    hitsBox->setContentsMargins(18, 14, 18, 14);
    hitsBox->setSpacing(8);

    makeSectionTitle(QStringLiteral("今日规则命中"), QStringLiteral("从本进程启动开始统计"), hitsCard);
    rulesHitSummaryLabel_ = new QLabel(QStringLiteral("本进程启动以来 0 条连接，其中 0 条走兜底 MATCH（0%）"), hitsCard);
    rulesHitSummaryLabel_->setObjectName(QStringLiteral("Subtitle"));
    hitsBox->addWidget(rulesHitSummaryLabel_);

    rulesHitsListWidget_ = new QWidget(hitsCard);
    rulesHitsListBox_ = new QVBoxLayout(rulesHitsListWidget_);
    rulesHitsListBox_->setContentsMargins(0, 0, 0, 0);
    rulesHitsListBox_->setSpacing(4);
    hitsBox->addWidget(rulesHitsListWidget_);
    box->addWidget(hitsCard);

    // 2. 分流模式选择
    auto *modeCard = new QFrame(panel);
    Theme::decorateCard(modeCard);
    auto *modeBox = new QVBoxLayout(modeCard);
    modeBox->setContentsMargins(18, 14, 18, 14);
    modeBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("分流模式"), QStringLiteral("选择全局流量的路由分流策略"), modeCard);

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
    modeBox->addLayout(modeRadioBox);

    routingModeDetailLabel_ = new QLabel(routingModeDesc(Proxy::RoutingMode::Rule), modeCard);
    routingModeDetailLabel_->setObjectName(QStringLiteral("Subtitle"));
    routingModeDetailLabel_->setWordWrap(true);
    modeBox->addWidget(routingModeDetailLabel_);

    connect(routingModeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        rulesOptions_.routingMode = Proxy::RoutingMode(id);
        if (routingModeDetailLabel_ != nullptr) {
            routingModeDetailLabel_->setText(routingModeDesc(rulesOptions_.routingMode));
        }
    });

    auto *saveModeLine = new QHBoxLayout();
    saveModeBtn_ = new QPushButton(QStringLiteral("保存分流模式"), modeCard);
    saveModeBtn_->setObjectName(QStringLiteral("Primary"));
    connect(saveModeBtn_, &QPushButton::clicked, this, &ProxyPage::saveRoutingMode);
    saveModeLine->addWidget(saveModeBtn_);
    saveModeLine->addStretch();
    modeBox->addLayout(saveModeLine);

    box->addWidget(modeCard);

    // 3. Geo 规则数据库维护
    auto *geoCard = new QFrame(panel);
    Theme::decorateCard(geoCard);
    auto *geoBox = new QVBoxLayout(geoCard);
    geoBox->setContentsMargins(18, 14, 18, 14);
    geoBox->setSpacing(8);

    makeSectionTitle(QStringLiteral("Geo 规则数据库维护"),
                     QStringLiteral("在线更新 GeoIP (中国大陆IP库) 与 GeoSite (域名分流库) 规则文件"), geoCard);

    auto *geoLine = new QHBoxLayout();
    geoLine->setSpacing(10);
    geoUpdateBtn_ = new QPushButton(QStringLiteral("一键更新 Geo 规则"), geoCard);
    geoUpdateBtn_->setObjectName(QStringLiteral("Primary"));
    connect(geoUpdateBtn_, &QPushButton::clicked, this, &ProxyPage::updateGeoData);
    geoLine->addWidget(geoUpdateBtn_);

    geoStatusLabel_ = new QLabel(geoCard);
    geoStatusLabel_->setObjectName(QStringLiteral("Subtitle"));
    geoLine->addWidget(geoStatusLabel_);
    geoLine->addStretch();
    geoBox->addLayout(geoLine);

    box->addWidget(geoCard);

    // 4. 常用场景分流预设
    auto *presetCard = new QFrame(panel);
    Theme::decorateCard(presetCard);
    auto *presetBox = new QVBoxLayout(presetCard);
    presetBox->setContentsMargins(18, 14, 18, 14);
    presetBox->setSpacing(8);

    makeSectionTitle(QStringLiteral("常用场景分流预设（一键添加至规则列表）"),
                     QStringLiteral("点击对应场景可一键合并预设分流规则，并在保存后自动热重载生效"), presetCard);

    auto *presetGrid = new QGridLayout();
    presetGrid->setSpacing(8);

    auto *btnAi = new QPushButton(QStringLiteral("+ AI 服务分流 (ChatGPT / Claude / Gemini)"), presetCard);
    connect(btnAi, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::aiServiceRules()); });
    presetGrid->addWidget(btnAi, 0, 0);

    auto *btnMedia = new QPushButton(QStringLiteral("+ 海外主流流媒体 (YouTube / Netflix / Spotify)"), presetCard);
    connect(btnMedia, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::streamingMediaRules()); });
    presetGrid->addWidget(btnMedia, 0, 1);

    auto *btnSocial = new QPushButton(QStringLiteral("+ 即时通讯 (Telegram / Discord / X)"), presetCard);
    connect(btnSocial, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::socialTelegramRules()); });
    presetGrid->addWidget(btnSocial, 0, 2);

    auto *btnDev = new QPushButton(QStringLiteral("+ 国内开发镜像直连 (npm / 清华源 / 阿里源)"), presetCard);
    connect(btnDev, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::devMirrorsRules()); });
    presetGrid->addWidget(btnDev, 1, 0);

    auto *btnGames = new QPushButton(QStringLiteral("+ 游戏平台直连 (Steam / Epic)"), presetCard);
    connect(btnGames, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::gamePlatformsRules()); });
    presetGrid->addWidget(btnGames, 1, 1);

    auto *btnAd = new QPushButton(QStringLiteral("🛡️ 广告与 QUIC 拦截"), presetCard);
    connect(btnAd, &QPushButton::clicked, this, [this]() { prependPresetRules(Proxy::adblockQuicRules()); });
    presetGrid->addWidget(btnAd, 1, 2);

    presetBox->addLayout(presetGrid);
    box->addWidget(presetCard);

    // 5. 自定义规则列表与新增表单
    auto *customCard = new QFrame(panel);
    Theme::decorateCard(customCard);
    auto *customBox = new QVBoxLayout(customCard);
    customBox->setContentsMargins(18, 14, 18, 14);
    customBox->setSpacing(10);

    makeSectionTitle(QStringLiteral("自定义规则"), QStringLiteral("从上到下匹配，未命中流量默认走代理"), customCard);

    rulesTable_ = new QTableWidget(customCard);
    rulesTable_->setColumnCount(4);
    rulesTable_->setHorizontalHeaderLabels({QStringLiteral("匹配条件"), QStringLiteral("动作"),
                                            QStringLiteral("排序"), QStringLiteral("操作")});
    rulesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    rulesTable_->setColumnWidth(1, 110);
    rulesTable_->setColumnWidth(2, 90);
    rulesTable_->setColumnWidth(3, 70);
    rulesTable_->verticalHeader()->setVisible(false);
    rulesTable_->setShowGrid(false);
    rulesTable_->setAlternatingRowColors(true);
    rulesTable_->setMinimumHeight(240);
    customBox->addWidget(rulesTable_);

    // 新增规则行
    auto *newRuleRow = new QHBoxLayout();
    newRuleRow->setSpacing(8);

    newRuleKindCombo_ = new QComboBox(customCard);
    newRuleKindCombo_->addItem(QStringLiteral("DOMAIN-SUFFIX"), int(Proxy::RuleMatcher::Kind::DomainSuffix));
    newRuleKindCombo_->addItem(QStringLiteral("DOMAIN"), int(Proxy::RuleMatcher::Kind::Domain));
    newRuleKindCombo_->addItem(QStringLiteral("GEOIP"), int(Proxy::RuleMatcher::Kind::GeoIp));
    newRuleKindCombo_->addItem(QStringLiteral("GEOSITE"), int(Proxy::RuleMatcher::Kind::GeoSite));
    newRuleKindCombo_->addItem(QStringLiteral("IP-CIDR"), int(Proxy::RuleMatcher::Kind::IpCidr));
    newRuleKindCombo_->addItem(QStringLiteral("DST-PORT"), int(Proxy::RuleMatcher::Kind::DestinationPort));
    newRuleKindCombo_->addItem(QStringLiteral("PROCESS-NAME"), int(Proxy::RuleMatcher::Kind::ProcessName));
    newRuleKindCombo_->addItem(QStringLiteral("QUIC"), int(Proxy::RuleMatcher::Kind::Quic));
    newRuleRow->addWidget(newRuleKindCombo_);

    newRuleValueEdit_ = new QLineEdit(customCard);
    newRuleValueEdit_->setPlaceholderText(QStringLiteral("匹配值"));
    newRuleRow->addWidget(newRuleValueEdit_, 1);

    newRuleActionCombo_ = new QComboBox(customCard);
    newRuleActionCombo_->addItem(QStringLiteral("直连"), int(Proxy::RuleAction::Direct));
    newRuleActionCombo_->addItem(QStringLiteral("代理"), int(Proxy::RuleAction::Proxy));
    newRuleActionCombo_->addItem(QStringLiteral("拒绝"), int(Proxy::RuleAction::Reject));
    newRuleRow->addWidget(newRuleActionCombo_);

    addRuleBtn_ = new QPushButton(QStringLiteral("添加规则"), customCard);
    connect(addRuleBtn_, &QPushButton::clicked, this, [this]() {
        const auto kind = Proxy::RuleMatcher::Kind(newRuleKindCombo_->currentData().toInt());
        const QString value = newRuleValueEdit_->text().trimmed();
        if (kind != Proxy::RuleMatcher::Kind::Quic && value.isEmpty()) {
            emit statusMessageRequested(QStringLiteral("规则匹配值不能为空"));
            return;
        }
        const auto action = Proxy::RuleAction(newRuleActionCombo_->currentData().toInt());
        const auto rule = Proxy::makeRule(kind, value, action);
        rulesOptions_.rules.push_back(rule);
        newRuleValueEdit_->clear();
        refreshRulesTable();
    });
    newRuleRow->addWidget(addRuleBtn_);

    recRulesBtn_ = new QPushButton(QStringLiteral("推荐分流"), customCard);
    recRulesBtn_->setToolTip(QStringLiteral("覆盖为国内直连、广告拒绝、其余代理"));
    connect(recRulesBtn_, &QPushButton::clicked, this, &ProxyPage::applyRecommendedRules);
    newRuleRow->addWidget(recRulesBtn_);

    optRulesBtn_ = new QPushButton(QStringLiteral("自动优化"), customCard);
    optRulesBtn_->setToolTip(QStringLiteral("移除重复规则并保持当前顺序"));
    connect(optRulesBtn_, &QPushButton::clicked, this, &ProxyPage::autoOptimizeRules);
    newRuleRow->addWidget(optRulesBtn_);

    customBox->addLayout(newRuleRow);

    newRuleHintLabel_ = new QLabel(QStringLiteral("推荐规则：局域网与国内直连，广告和 QUIC 拒绝，其余代理"), customCard);
    newRuleHintLabel_->setObjectName(QStringLiteral("Muted"));
    newRuleHintLabel_->setStyleSheet(QStringLiteral("font-size: 11px;"));
    customBox->addWidget(newRuleHintLabel_);

    auto *saveRulesLine = new QHBoxLayout();
    saveRulesBtn_ = new QPushButton(QStringLiteral("保存规则"), customCard);
    saveRulesBtn_->setObjectName(QStringLiteral("Primary"));
    connect(saveRulesBtn_, &QPushButton::clicked, this, &ProxyPage::saveRules);
    saveRulesLine->addWidget(saveRulesBtn_);
    saveRulesLine->addStretch();
    customBox->addLayout(saveRulesLine);

    auto *injectedNotice = new QLabel(
        QStringLiteral("内置注入（不可编辑，仅 Mihomo 生效，插在 GEOIP,CN 之前）：YouTube · Google · Telegram · Twitter"), customCard);
    injectedNotice->setObjectName(QStringLiteral("Muted"));
    injectedNotice->setStyleSheet(QStringLiteral("font-size: 11px;"));
    customBox->addWidget(injectedNotice);

    box->addWidget(customCard);

    scroll->setWidget(panel);
    return scroll;
}

void ProxyPage::reloadRules()
{
    rulesOptions_ = context_.proxy().config();
    if (routingModeGroup_ != nullptr) {
        auto *btn = routingModeGroup_->button(int(rulesOptions_.routingMode));
        if (btn != nullptr) {
            btn->setChecked(true);
        }
    }
    if (routingModeDetailLabel_ != nullptr) {
        routingModeDetailLabel_->setText(routingModeDesc(rulesOptions_.routingMode));
    }
    refreshRulesTable();
}

void ProxyPage::refreshRulesTable()
{
    if (rulesTable_ == nullptr) {
        return;
    }
    rulesTable_->setRowCount(rulesOptions_.rules.size());
    for (int i = 0; i < rulesOptions_.rules.size(); ++i) {
        const auto &rule = rulesOptions_.rules.at(i);
        rulesTable_->setItem(i, 0, new QTableWidgetItem(ruleMatcherLabel(rule.matcher)));

        auto *actionCombo = new QComboBox(rulesTable_);
        actionCombo->addItem(QStringLiteral("直连"), int(Proxy::RuleAction::Direct));
        actionCombo->addItem(QStringLiteral("代理"), int(Proxy::RuleAction::Proxy));
        actionCombo->addItem(QStringLiteral("拒绝"), int(Proxy::RuleAction::Reject));
        actionCombo->setCurrentIndex(int(rule.action));
        connect(actionCombo, &QComboBox::currentIndexChanged, this, [this, i](int idx) {
            if (i < rulesOptions_.rules.size()) {
                rulesOptions_.rules[i].action = Proxy::RuleAction(idx);
            }
        });
        rulesTable_->setCellWidget(i, 1, actionCombo);

        auto *moveWidget = new QWidget(rulesTable_);
        auto *moveBox = new QHBoxLayout(moveWidget);
        moveBox->setContentsMargins(0, 0, 0, 0);
        moveBox->setSpacing(2);

        auto *upBtn = new QPushButton(QStringLiteral("↑"), moveWidget);
        upBtn->setObjectName(QStringLiteral("Small"));
        upBtn->setEnabled(i > 0);
        connect(upBtn, &QPushButton::clicked, this, [this, i]() {
            if (i > 0) {
                std::swap(rulesOptions_.rules[i], rulesOptions_.rules[i - 1]);
                refreshRulesTable();
            }
        });
        moveBox->addWidget(upBtn);

        auto *downBtn = new QPushButton(QStringLiteral("↓"), moveWidget);
        downBtn->setObjectName(QStringLiteral("Small"));
        downBtn->setEnabled(i + 1 < rulesOptions_.rules.size());
        connect(downBtn, &QPushButton::clicked, this, [this, i]() {
            if (i + 1 < rulesOptions_.rules.size()) {
                std::swap(rulesOptions_.rules[i], rulesOptions_.rules[i + 1]);
                refreshRulesTable();
            }
        });
        moveBox->addWidget(downBtn);
        rulesTable_->setCellWidget(i, 2, moveWidget);

        auto *delBtn = new QPushButton(QStringLiteral("×"), rulesTable_);
        delBtn->setObjectName(QStringLiteral("Small"));
        connect(delBtn, &QPushButton::clicked, this, [this, i]() {
            if (i < rulesOptions_.rules.size()) {
                rulesOptions_.rules.remove(i);
                refreshRulesTable();
            }
        });
        rulesTable_->setCellWidget(i, 3, delBtn);
    }
}

void ProxyPage::saveRules()
{
    const auto opts = rulesOptions_;
    context_.runAsync(
        [this, opts]() {
            context_.proxy().reloadConfig(opts);
            const auto json = opts.toSavedJsonText();
            if (json.isOk()) {
                context_.database().setProxyConfigJsonText(json.value());
            }
        },
        [this]() { emit statusMessageRequested(QStringLiteral("分流规则已保存并热重载生效")); });
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

    const auto status = context_.proxy().status();
    const bool isRunning = status.localProxyActive();
    const quint16 port = status.controllerPort;
    const QString secret = status.controllerSecret;

    context_.runAsync(
        [isRunning, port, secret]() {
            if (isRunning) {
                Net::Request request;
                request.url = QStringLiteral("http://127.0.0.1:%1/configs/geo").arg(port);
                request.method = QByteArrayLiteral("POST");
                request.timeoutMs = 60000;
                if (!secret.isEmpty()) {
                    request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(secret)});
                }
                Net::send(request);
            } else {
                Geo::provision(Geo::mihomoDir(), QString(), true);
            }
        },
        [this]() {
            geoUpdating_ = false;
            if (geoUpdateBtn_ != nullptr) geoUpdateBtn_->setEnabled(true);
            if (geoStatusLabel_ != nullptr) geoStatusLabel_->setText(QStringLiteral("Geo 规则已更新完成"));
            emit statusMessageRequested(QStringLiteral("Geo 规则数据库更新指令已成功执行"));
        });
}

void ProxyPage::applyRecommendedRules()
{
    rulesOptions_.rules = Proxy::recommendedTrafficRules();
    refreshRulesTable();
    emit statusMessageRequested(QStringLiteral("已重置为推荐分流规则，请点击「保存规则」生效"));
}

void ProxyPage::autoOptimizeRules()
{
    const int before = rulesOptions_.rules.size();
    rulesOptions_.rules = Proxy::optimizeTrafficRules(rulesOptions_.rules);
    const int removed = before - rulesOptions_.rules.size();
    refreshRulesTable();
    emit statusMessageRequested(
        removed == 0 ? QStringLiteral("规则已是最优顺序，没有发现重复项")
                     : QStringLiteral("已自动移除 %1 条重复/冗余规则，请保存规则").arg(removed));
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
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(12);

    makeSectionTitle(QStringLiteral("DNS / TUN"), QStringLiteral("配置接管模式、监听端口与 DNS 策略"), panel);

    auto *card = new QFrame(panel);
    Theme::decorateCard(card);
    auto *cardBox = new QVBoxLayout(card);
    cardBox->setContentsMargins(18, 14, 18, 14);
    cardBox->setSpacing(12);

    // 接管模式
    auto *modeSec = new QLabel(QStringLiteral("接管模式"), card);
    modeSec->setStyleSheet(QStringLiteral("font-weight: 700;"));
    cardBox->addWidget(modeSec);

    auto *modeLine = new QHBoxLayout();
    netModeSystemRadio_ = new QRadioButton(QStringLiteral("系统代理"), card);
    netModeTunRadio_ = new QRadioButton(QStringLiteral("TUN 模式"), card);
    netModeSystemRadio_->setChecked(true);
    modeLine->addWidget(netModeSystemRadio_);
    modeLine->addWidget(netModeTunRadio_);
    modeLine->addStretch();
    cardBox->addLayout(modeLine);

    connect(netModeTunRadio_, &QRadioButton::toggled, this, [this](bool tun) {
        if (netTunGroup_ != nullptr) {
            netTunGroup_->setVisible(tun);
        }
    });

    // 运行内核
    auto *kernelLine = new QHBoxLayout();
    kernelLine->addWidget(new QLabel(QStringLiteral("运行内核:"), card));
    netKernelPrefCombo_ = new QComboBox(card);
    netKernelPrefCombo_->addItem(QStringLiteral("自动选择"), int(Kernel::Preference::Auto));
    netKernelPrefCombo_->addItem(QStringLiteral("Mihomo"), int(Kernel::Preference::Mihomo));
    netKernelPrefCombo_->addItem(QStringLiteral("Sing-box"), int(Kernel::Preference::SingBox));
    netKernelPrefCombo_->addItem(QStringLiteral("Xray"), int(Kernel::Preference::Xray));
    kernelLine->addWidget(netKernelPrefCombo_);
    kernelLine->addStretch();
    cardBox->addLayout(kernelLine);

    // 端口与网络
    auto *portsLine = new QHBoxLayout();
    portsLine->addWidget(new QLabel(QStringLiteral("Mixed 端口:"), card));
    netMixedPortSpin_ = new QSpinBox(card);
    netMixedPortSpin_->setRange(1, 65535);
    netMixedPortSpin_->setValue(9980);
    portsLine->addWidget(netMixedPortSpin_);

    portsLine->addSpacing(12);
    portsLine->addWidget(new QLabel(QStringLiteral("控制端口:"), card));
    netControllerPortSpin_ = new QSpinBox(card);
    netControllerPortSpin_->setRange(1, 65535);
    netControllerPortSpin_->setValue(9090);
    portsLine->addWidget(netControllerPortSpin_);

    portsLine->addSpacing(16);
    netAllowLanCheck_ = new QCheckBox(QStringLiteral("允许局域网"), card);
    portsLine->addWidget(netAllowLanCheck_);

    portsLine->addSpacing(16);
    netTerminalProxyCheck_ = new QCheckBox(QStringLiteral("终端代理 (写入 HTTP_PROXY / HTTPS_PROXY)"), card);
    portsLine->addWidget(netTerminalProxyCheck_);
    portsLine->addStretch();
    cardBox->addLayout(portsLine);

    // TUN 参数
    netTunGroup_ = new QWidget(card);
    auto *tunBox = new QVBoxLayout(netTunGroup_);
    tunBox->setContentsMargins(0, 0, 0, 0);
    tunBox->setSpacing(8);

    auto *tunSec = new QLabel(QStringLiteral("TUN 参数"), netTunGroup_);
    tunSec->setStyleSheet(QStringLiteral("font-weight: 700;"));
    tunBox->addWidget(tunSec);

    auto *tunLine = new QHBoxLayout();
    tunLine->addWidget(new QLabel(QStringLiteral("协议栈:"), netTunGroup_));
    netTunStackCombo_ = new QComboBox(netTunGroup_);
    netTunStackCombo_->addItem(QStringLiteral("Mixed"), int(Proxy::TunStack::Mixed));
    netTunStackCombo_->addItem(QStringLiteral("System"), int(Proxy::TunStack::System));
    netTunStackCombo_->addItem(QStringLiteral("gVisor"), int(Proxy::TunStack::Gvisor));
    tunLine->addWidget(netTunStackCombo_);

    tunLine->addSpacing(16);
    netTunAutoRouteCheck_ = new QCheckBox(QStringLiteral("自动路由"), netTunGroup_);
    tunLine->addWidget(netTunAutoRouteCheck_);

    tunLine->addSpacing(16);
    netTunAutoDetectCheck_ = new QCheckBox(QStringLiteral("自动检测网卡"), netTunGroup_);
    tunLine->addWidget(netTunAutoDetectCheck_);
    tunLine->addStretch();
    tunBox->addLayout(tunLine);
    netTunGroup_->setVisible(false);
    cardBox->addWidget(netTunGroup_);

    // DNS 设置
    auto *dnsSec = new QLabel(QStringLiteral("DNS"), card);
    dnsSec->setStyleSheet(QStringLiteral("font-weight: 700;"));
    cardBox->addWidget(dnsSec);

    auto *dnsLine = new QHBoxLayout();
    netDnsEnabledCheck_ = new QCheckBox(QStringLiteral("启用 DNS"), card);
    dnsLine->addWidget(netDnsEnabledCheck_);

    dnsLine->addSpacing(16);
    netDnsIpv6Check_ = new QCheckBox(QStringLiteral("IPv6"), card);
    dnsLine->addWidget(netDnsIpv6Check_);

    dnsLine->addSpacing(16);
    dnsLine->addWidget(new QLabel(QStringLiteral("模式:"), card));
    netDnsEnhancedCombo_ = new QComboBox(card);
    netDnsEnhancedCombo_->addItem(QStringLiteral("Fake-IP"), int(Proxy::DnsEnhancedMode::FakeIp));
    netDnsEnhancedCombo_->addItem(QStringLiteral("Redir-Host"), int(Proxy::DnsEnhancedMode::RedirHost));
    dnsLine->addWidget(netDnsEnhancedCombo_);
    dnsLine->addStretch();
    cardBox->addLayout(dnsLine);

    auto *dns1Line = new QHBoxLayout();
    dns1Line->addWidget(new QLabel(QStringLiteral("主 DNS:"), card));
    netDnsPrimaryEdit_ = new QLineEdit(card);
    netDnsPrimaryEdit_->setPlaceholderText(QString::fromLatin1(Proxy::kDefaultDnsPrimary));
    dns1Line->addWidget(netDnsPrimaryEdit_, 1);
    cardBox->addLayout(dns1Line);

    auto *dns2Line = new QHBoxLayout();
    dns2Line->addWidget(new QLabel(QStringLiteral("备用 DNS:"), card));
    netDnsSecondaryEdit_ = new QLineEdit(card);
    netDnsSecondaryEdit_->setPlaceholderText(QString::fromLatin1(Proxy::kDefaultDnsSecondary));
    dns2Line->addWidget(netDnsSecondaryEdit_, 1);
    cardBox->addLayout(dns2Line);

    auto *saveNetLine = new QHBoxLayout();
    saveNetworkBtn_ = new QPushButton(QStringLiteral("保存网络配置"), card);
    saveNetworkBtn_->setObjectName(QStringLiteral("Primary"));
    connect(saveNetworkBtn_, &QPushButton::clicked, this, &ProxyPage::saveNetworkConfig);
    saveNetLine->addWidget(saveNetworkBtn_);

    netEditableNoticeLabel_ = new QLabel(card);
    netEditableNoticeLabel_->setObjectName(QStringLiteral("Muted"));
    saveNetLine->addWidget(netEditableNoticeLabel_);
    saveNetLine->addStretch();
    cardBox->addLayout(saveNetLine);

    box->addWidget(card);

    scroll->setWidget(panel);
    return scroll;
}

void ProxyPage::reloadNetwork()
{
    networkOptions_ = context_.proxy().config();
    const bool isRunning = context_.proxy().status().localProxyActive();

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
    if (netDnsPrimaryEdit_ != nullptr) {
        netDnsPrimaryEdit_->setText(networkOptions_.dns.nameservers.value(0, QString::fromLatin1(Proxy::kDefaultDnsPrimary)));
    }
    if (netDnsSecondaryEdit_ != nullptr) {
        netDnsSecondaryEdit_->setText(networkOptions_.dns.nameservers.value(1, QString::fromLatin1(Proxy::kDefaultDnsSecondary)));
    }

    if (saveNetworkBtn_ != nullptr) {
        saveNetworkBtn_->setEnabled(!isRunning);
    }
    if (netEditableNoticeLabel_ != nullptr) {
        netEditableNoticeLabel_->setText(isRunning ? QStringLiteral("代理运行中不可修改网络设置，请先停止代理") : QString());
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
    if (netDnsPrimaryEdit_ != nullptr && !netDnsPrimaryEdit_->text().trimmed().isEmpty()) {
        ns.append(netDnsPrimaryEdit_->text().trimmed());
    } else {
        ns.append(QString::fromLatin1(Proxy::kDefaultDnsPrimary));
    }
    if (netDnsSecondaryEdit_ != nullptr && !netDnsSecondaryEdit_->text().trimmed().isEmpty()) {
        ns.append(netDnsSecondaryEdit_->text().trimmed());
    } else {
        ns.append(QString::fromLatin1(Proxy::kDefaultDnsSecondary));
    }
    networkOptions_.dns.nameservers = ns;

    const auto opts = networkOptions_;
    context_.runAsync(
        [this, opts]() {
            context_.proxy().updateConfig(opts);
            const auto json = opts.toSavedJsonText();
            if (json.isOk()) {
                context_.database().setProxyConfigJsonText(json.value());
            }
        },
        [this]() { emit statusMessageRequested(QStringLiteral("网络配置已保存，下次启动代理时生效")); });
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
        reloadKernels();
    });
    actions->addWidget(kernelCheckButton_);

    kernelCancelButton_ = new QPushButton(UiText::kernelCancelDownload(), panel);
    kernelCancelButton_->setObjectName(QStringLiteral("Danger"));
    kernelCancelButton_->setVisible(false);
    connect(kernelCancelButton_, &QPushButton::clicked, this, [this]() {
        emit kernelCancelRequested();
        emit statusMessageRequested(UiText::kernelCancellingStatus());
    });
    actions->addWidget(kernelCancelButton_);
    box->addLayout(actions);

    kernelProgressLabel_ = new QLabel(panel);
    kernelProgressLabel_->setObjectName(QStringLiteral("Accent"));
    kernelProgressLabel_->setVisible(false);
    box->addWidget(kernelProgressLabel_);

    auto *card = new QFrame(panel);
    Theme::decorateCard(card);
    card->setObjectName(QStringLiteral("Card"));
    kernelTable_ = card;
    kernelGrid_ = new QGridLayout(card);
    kernelGrid_->setContentsMargins(Theme::kPadCard, Theme::kPadCard, Theme::kPadCard, Theme::kPadCard);
    kernelGrid_->setSpacing(8);
    layoutKernelColumns();
    box->addWidget(card);
    box->addStretch();

    return panel;
}

void ProxyPage::reloadKernels()
{
    if (kernelLoading_) {
        return;
    }
    kernelLoading_ = true;
    auto kernelBox = std::make_shared<QList<Kernel::Info>>();
    auto releaseBox = std::make_shared<QList<Kernel::ReleaseInfo>>();
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [kernelBox, releaseBox]() {
            Kernel::Manager detector;
            *kernelBox = detector.detectAll();
            const QString root = QStringLiteral("data/kernels");
            const Updater::KernelUpdater updater(root, {});
            for (const Kernel::Info &info : *kernelBox) {
                const Result<Kernel::ReleaseInfo> checked = updater.check(info);
                releaseBox->push_back(Kernel::releaseInfoFromCheck(checked, info));
            }
        },
        [guard, kernelBox, releaseBox]() {
            if (guard.isNull()) {
                return;
            }
            guard->kernelLoading_ = false;
            guard->kernelInfos_ = *kernelBox;
            guard->kernelReleases_ = *releaseBox;
            guard->kernelRows_ = buildKernelRows(guard->kernelInfos_, guard->kernelReleases_,
                                                 guard->kernelBusy_, guard->context_.proxy().status());
            guard->layoutKernelColumns();
        });
}

void ProxyPage::layoutKernelColumns()
{
    if (kernelGrid_ == nullptr) {
        return;
    }
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
        label->setObjectName(QStringLiteral("Muted"));
        kernelGrid_->addWidget(label, 0, col);
    }

    for (int rowIdx = 0; rowIdx < kernelRows_.size(); ++rowIdx) {
        const KernelRow &row = kernelRows_.at(rowIdx);
        const int gridRow = rowIdx + 1;

        auto *name = new QLabel(row.name, kernelTable_);
        name->setObjectName(QStringLiteral("Title"));
        kernelGrid_->addWidget(name, gridRow, 0);

        auto *status = new QLabel(row.statusText, kernelTable_);
        status->setObjectName(row.statusOk ? QStringLiteral("Success") : QStringLiteral("Danger"));
        kernelGrid_->addWidget(status, gridRow, 1);

        auto *version = new QLabel(row.version, kernelTable_);
        version->setToolTip(row.version);
        kernelGrid_->addWidget(version, gridRow, 2);

        auto *latest = new QLabel(row.latest, kernelTable_);
        latest->setObjectName(row.latestIsWarning ? QStringLiteral("Warning") : QStringLiteral("Muted"));
        kernelGrid_->addWidget(latest, gridRow, 3);

        auto *path = new QLabel(row.path, kernelTable_);
        path->setToolTip(row.path);
        kernelGrid_->addWidget(path, gridRow, 4);

        auto *btn = new QPushButton(row.actionText, kernelTable_);
        btn->setEnabled(row.actionEnabled);
        btn->setToolTip(row.hover);
        const Kernel::Kind kind = row.kind;
        connect(btn, &QPushButton::clicked, this, [this, kind]() {
            emit kernelUpdateRequested(kind);
        });
        kernelGrid_->addWidget(btn, gridRow, 5);
    }

    kernelGrid_->setColumnStretch(0, 0);
    kernelGrid_->setColumnStretch(1, 0);
    kernelGrid_->setColumnStretch(2, 1);
    kernelGrid_->setColumnStretch(3, 0);
    kernelGrid_->setColumnStretch(4, 2);
    kernelGrid_->setColumnStretch(5, 0);
}

QWidget *ProxyPage::buildLogsPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("LogsPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);

    auto *headerLine = new QHBoxLayout();
    headerLine->setSpacing(10);

    logKernelTitleLabel_ = new QLabel(QStringLiteral("代理内核 运行日志"), panel);
    logKernelTitleLabel_->setObjectName(QStringLiteral("Title"));
    logKernelTitleLabel_->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700;"));
    headerLine->addWidget(logKernelTitleLabel_);
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

    logView_ = new QPlainTextEdit(panel);
    logView_->setObjectName(QStringLiteral("LogView"));
    logView_->setReadOnly(true);
    logView_->setPlaceholderText(QStringLiteral("暂无运行日志输出..."));
    box->addWidget(logView_, 1);

    return panel;
}

void ProxyPage::reloadLogs()
{
    const auto status = context_.proxy().status();
    if (logKernelTitleLabel_ != nullptr) {
        const QString kernelName = status.kernel.has_value() ? Kernel::kindDisplayName(*status.kernel) : QStringLiteral("代理内核");
        logKernelTitleLabel_->setText(QStringLiteral("%1 运行日志").arg(kernelName));
    }

    auto logBox = std::make_shared<QString>();
    context_.runAsync(
        [this, logBox]() {
            const auto res = context_.proxy().recentLog(100000);
            if (res.isOk()) {
                *logBox = res.value();
            }
        },
        [this, logBox]() {
            if (logView_ != nullptr) {
                const QString current = logView_->toPlainText();
                if (current != *logBox) {
                    logView_->setPlainText(*logBox);
                    if (logAutoScrollCheck_ != nullptr && logAutoScrollCheck_->isChecked()) {
                        logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
                    }
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

void ProxyPage::onPollTimer()
{
    const auto status = context_.proxy().status();
    refreshStatusCard();

    if (currentPanel() == Panel::Connections && status.localProxyActive()) {
        reloadConnections();
    } else if (currentPanel() == Panel::Logs && status.localProxyActive()) {
        reloadLogs();
    } else if (status.localProxyActive()) {
        reloadConnections();
    }
}

void ProxyPage::refresh()
{
    refreshStatusCard();

    switch (currentPanel()) {
    case Panel::Nodes:
        if (allNodes_.isEmpty()) {
            reloadNodes();
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
            kernelRows_ = buildKernelRows(kernelInfos_, kernelReleases_, kernelBusy_, context_.proxy().status());
            layoutKernelColumns();
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
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [this, nodesBox]() {
            const auto res = context_.database().loadAvailableNodeSummaries(QString());
            if (res.isOk()) {
                *nodesBox = res.value();
            }
        },
        [guard, nodesBox]() {
            if (guard.isNull()) {
                return;
            }
            guard->nodesRequested_ = false;
            guard->allNodes_ = *nodesBox;
            guard->applyView();
        });
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
    while (QLayoutItem *item = grid_->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    const int cols = gridColumns();
    for (int i = 0; i < view_.rows.size(); ++i) {
        QWidget *card = makeNodeRow(view_.rows.at(i));
        grid_->addWidget(card, i / cols, i % cols);
    }
}

int ProxyPage::gridColumns() const
{
    const int available = nodeScroll_ != nullptr ? nodeScroll_->viewport()->width() : width();
    if (available >= 920) return 4;
    if (available >= 660) return 3;
    return 2;
}

QWidget *ProxyPage::makeNodeRow(const ProxyViewRow &row)
{
    auto *card = new QFrame(gridHost_);
    Theme::decorateCard(card);
    card->setProperty("nodeId", row.node.id);
    card->setProperty("nodeName", row.node.name);
    card->setProperty("server", row.node.server);
    card->setCursor(Qt::PointingHandCursor);
    card->installEventFilter(this);

    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(14, 12, 14, 12);
    box->setSpacing(6);

    auto *topLine = new QHBoxLayout();
    topLine->setSpacing(6);

    const CountryDisplay cd = countryFlagAndName(row.country);
    auto *flagLabel = new QLabel(QStringLiteral("%1 %2").arg(cd.flag, cd.name), card);
    flagLabel->setObjectName(QStringLiteral("Subtitle"));
    topLine->addWidget(flagLabel);
    topLine->addStretch();

    const auto status = context_.proxy().status();
    if (status.localProxyActive() && status.nodeId == row.node.id) {
        auto *activeBadge = new QLabel(QStringLiteral("运行中"), card);
        activeBadge->setObjectName(QStringLiteral("SuccessPill"));
        topLine->addWidget(activeBadge);
    }
    box->addLayout(topLine);

    auto *nameLabel = new QLabel(row.node.name, card);
    nameLabel->setObjectName(QStringLiteral("Title"));
    nameLabel->setWordWrap(true);
    box->addWidget(nameLabel);

    auto *serverLabel = new QLabel(QStringLiteral("%1:%2").arg(row.node.server).arg(row.node.port), card);
    serverLabel->setObjectName(QStringLiteral("Muted"));
    box->addWidget(serverLabel);

    auto *bottomLine = new QHBoxLayout();
    bottomLine->setSpacing(6);

    const QString proto = row.node.protocol.toUpper();
    auto *protoBadge = new QLabel(proto, card);
    protoBadge->setObjectName(QStringLiteral("Muted"));
    protoBadge->setStyleSheet(QStringLiteral("font-size: 11px;"));
    bottomLine->addWidget(protoBadge);
    bottomLine->addStretch();

    QString latText = QStringLiteral("-");
    QString latRole = QStringLiteral("Muted");
    if (row.node.latencyOk == true && row.node.latencyMs.has_value()) {
        latText = QStringLiteral("%1 ms").arg(*row.node.latencyMs);
        latRole = QStringLiteral("Success");
    }
    auto *latLabel = new QLabel(latText, card);
    latLabel->setObjectName(latRole);
    latLabel->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 12px;"));
    bottomLine->addWidget(latLabel);

    box->addLayout(bottomLine);
    return card;
}

bool ProxyPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        auto *card = qobject_cast<QWidget *>(watched);
        if (card != nullptr && mouse->button() == Qt::LeftButton) {
            const QString nodeId = card->property("nodeId").toString();
            const QString nodeName = card->property("nodeName").toString();
            if (!nodeId.isEmpty()) {
                setSelectedNode(nodeId, nodeName);
                emit nodeSwitchRequested(nodeId, nodeName);
                return true;
            }
        }
    }
    return Page::eventFilter(watched, event);
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
    kernelRows_ = buildKernelRows(kernelInfos_, kernelReleases_, kernelBusy_, context_.proxy().status());
    layoutKernelColumns();
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
