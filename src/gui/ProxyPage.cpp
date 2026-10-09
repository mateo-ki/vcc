#include "gui/ProxyPage.h"

#include <QCursor>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/FlowLayout.h"
#include "gui/Prepare.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "kernel/KernelManager.h"
#include "proxy/ProxyManager.h"
#include "sysproxy/Format.h"

namespace {

/// 三个内核里有没有任何一个已经装上（Rust 侧是 kernel_installed_cached()）。
bool anyKernelInstalled()
{
    for (Kernel::Kind kind : {Kernel::Kind::Mihomo, Kernel::Kind::SingBox, Kernel::Kind::Xray}) {
        if (Kernel::kernelIsAvailable(kind)) {
            return true;
        }
    }
    return false;
}

QLabel *makeCardTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("Title"));
    return label;
}

}

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

ProxyPage::ProxyPage(App::AppContext &context, QWidget *parent) : Page(context, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    root->addWidget(buildStatusCard());

    panels_ = new QTabBar(this);
    panels_->setExpanding(false);
    panels_->addTab(UiText::panelNodes());
    panels_->addTab(UiText::panelConnections());
    panels_->addTab(UiText::panelRules());
    panels_->addTab(UiText::panelNetwork());
    panels_->addTab(UiText::panelKernels());
    panels_->addTab(UiText::panelLogs());
    root->addWidget(panels_);

    stack_ = new QStackedWidget(this);
    // 先把六个格子摆出来：Connections/Rules/Network/Kernels 由后续层填实，
    // Nodes 与 Logs 这一层已经能用。
    for (int index = 0; index < 6; ++index) {
        auto *panel = new QWidget(stack_);
        panel->setObjectName(QStringLiteral("Panel%1").arg(index));
        stack_->addWidget(panel);
    }
    stack_->insertWidget(int(Panel::Nodes), buildNodesPanel());
    stack_->insertWidget(int(Panel::Logs), buildLogsPanel());
    connect(panels_, &QTabBar::currentChanged, this, [this](int index) {
        stack_->setCurrentIndex(index);
        refresh();
    });
    root->addWidget(stack_, 1);

    refresh();
}

QWidget *ProxyPage::buildStatusCard()
{
    auto *card = new QFrame(this);
    Theme::decorateCard(card);
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(Theme::kPadCard, Theme::kPadCard, Theme::kPadCard, Theme::kPadCard);
    box->setSpacing(6);

    auto *line = new QHBoxLayout();
    line->setSpacing(10);
    stateBadge_ = new QLabel(card);
    line->addWidget(stateBadge_);
    stateMessage_ = new QLabel(card);
    stateMessage_->setObjectName(QStringLiteral("Subtitle"));
    line->addWidget(stateMessage_, 1);
    box->addLayout(line);

    kernelInfo_ = new QLabel(card);
    kernelInfo_->setObjectName(QStringLiteral("Muted"));
    box->addWidget(kernelInfo_);
    return card;
}

QStringList ProxyPage::regionPillCodes() const
{
    return {QStringLiteral("all"), QStringLiteral("HK"), QStringLiteral("JP"),
            QStringLiteral("US"), QStringLiteral("SG"), QStringLiteral("TW"),
            QStringLiteral("KR"), QStringLiteral("OTHER")};
}

QString ProxyPage::countText() const
{
    // Rust 的 format!("共 {} 个节点", self.proxy_nodes.len())：说的是手里有多少节点，
    // 不是过滤后剩几行 —— 过滤后的数量得从栅格本身看。
    return SysProxy::fillTemplate(UiText::nodeCountTotal(), {},
                                  {QString::number(allNodes_.size())});
}

QWidget *ProxyPage::buildNodesPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("NodesPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);

    auto *regionRow = new QWidget(panel);
    auto *regionFlow = new FlowLayout(regionRow, 0, 6, 4);
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

    auto *sortRow = new QWidget(panel);
    auto *sortFlow = new FlowLayout(sortRow, 0, 6, 4);
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
        sortName_->setChecked(true);
        sortLatency_->setChecked(false);
        applyView();
    });
    sortFlow->addWidget(sortLatency_);
    sortFlow->addWidget(sortName_);
    countLabel_ = new QLabel(countText(), sortRow);
    countLabel_->setObjectName(QStringLiteral("Muted"));
    sortFlow->addWidget(countLabel_);
    // 清空按钮常驻：Rust 那边把 X 写成"只在搜索框非空时才 add"，于是敲下第一个字符后
    // 这一行后面的控件 auto-id 整体位移、TextEdit 换了 id → 焦点被丢、输入法组合窗跟着
    // 消失，表现就是"输一个字就卡"。Qt 这边也在建框时就定死，不在 textChanged 里增删。
    searchEdit_ = new QLineEdit(sortRow);
    searchEdit_->setObjectName(QStringLiteral("NodeSearch"));
    searchEdit_->setPlaceholderText(UiText::searchHint());
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setFixedWidth(180);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        search_ = text.trimmed().toLower();
        applyView();
    });
    sortFlow->addWidget(searchEdit_);
    box->addWidget(sortRow);

    nodeScroll_ = new QScrollArea(panel);
    nodeScroll_->setObjectName(QStringLiteral("NodeScroll"));
    nodeScroll_->setWidgetResizable(true);
    nodeScroll_->setFrameShape(QFrame::NoFrame);
    nodeScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridHost_ = new QWidget(nodeScroll_);
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(8);
    nodeScroll_->setWidget(gridHost_);
    box->addWidget(nodeScroll_, 1);
    return panel;
}

QWidget *ProxyPage::makeNodeRow(const ProxyViewRow &row)
{
    auto *card = new QFrame(gridHost_);
    card->setObjectName(QStringLiteral("Card"));
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(2);

    auto *name = new QLabel(row.node.name, card);
    name->setObjectName(QStringLiteral("Title"));
    box->addWidget(name);

    QString detail = row.country;
    if (row.node.latencyOk.has_value() && *row.node.latencyOk && row.node.latencyMs.has_value()) {
        detail += QStringLiteral(" · %1 ms").arg(*row.node.latencyMs);
    } else {
        detail += QStringLiteral(" · -- ms");
    }
    auto *detailLabel = new QLabel(detail, card);
    detailLabel->setObjectName(QStringLiteral("Muted"));
    box->addWidget(detailLabel);

    const QString nodeId = row.node.id;
    const QString nodeName = row.node.name;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, nodeId, nodeName]() {
        QMenu menu;
        QAction *details = menu.addAction(UiText::menuDetails());
        QAction *speedtest = menu.addAction(UiText::menuSpeedtest());
        QAction *ipRisk = menu.addAction(UiText::menuIpRisk());
        QAction *lock = menu.addAction(UiText::menuLockEgress());
        QAction *picked = menu.exec(QCursor::pos());
        if (picked == nullptr) {
            return;
        }
        if (picked == details) {
            emit nodeDetailsRequested(nodeId);
        } else if (picked == speedtest) {
            emit nodeSpeedtestRequested(nodeId);
        } else if (picked == ipRisk) {
            emit nodeIpRiskRequested(nodeId);
        } else if (picked == lock) {
            emit nodeEgressLockRequested(nodeId, nodeName);
        }
    });
    return card;
}

int ProxyPage::gridColumns() const
{
    // Rust：可用宽度 >=920 四列，>=660 三列，否则两列。
    const int available = nodeScroll_ != nullptr ? nodeScroll_->viewport()->width() : width();
    if (available >= 920) {
        return 4;
    }
    if (available >= 660) {
        return 3;
    }
    return 2;
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
            delete item->widget();
        }
        delete item;
    }
    const int columns = gridColumns();
    for (int index = 0; index < view_.rows.size(); ++index) {
        grid_->addWidget(makeNodeRow(view_.rows.at(index)), index / columns, index % columns);
    }
    for (int column = 0; column < columns; ++column) {
        grid_->setColumnStretch(column, 1);
    }
}

void ProxyPage::reloadNodes()
{
    if (nodesRequested_) {
        return;
    }
    nodesRequested_ = true;
    auto result = std::make_shared<Result<QVector<Models::NodeSummary>>>(
        Result<QVector<Models::NodeSummary>>::withValue(QVector<Models::NodeSummary>()));
    QPointer<ProxyPage> guard(this);
    context_.runAsync(
        [this, result]() {
            // Rust 侧的 "all" 在库里对应 null scope（nodeScopes 里那层翻译）。
            *result = context_.database().loadAvailableNodeSummaries(QString());
        },
        [guard, result]() {
            if (guard.isNull()) {
                return;
            }
            if (result->isOk()) {
                guard->allNodes_ = result->value();
            } else {
                guard->allNodes_.clear();
                guard->stateMessage_->setText(result->error());
            }
            guard->dataRev_ += 1;
            guard->applyView();
        });
}

void ProxyPage::resizeEvent(QResizeEvent *event)
{
    Page::resizeEvent(event);
    // 列数跟着宽度走，所以只在落在栅格这一栏且真有行时重排。
    if (stack_ != nullptr && stack_->currentIndex() == int(Panel::Nodes) && !view_.rows.isEmpty()) {
        applyView();
    }
}

QWidget *ProxyPage::buildLogsPanel()
{
    auto *panel = new QWidget(stack_);
    panel->setObjectName(QStringLiteral("LogsPanel"));
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    box->addWidget(makeCardTitle(UiText::panelLogs(), panel));
    logView_ = new QPlainTextEdit(panel);
    logView_->setReadOnly(true);
    box->addWidget(logView_, 1);
    return panel;
}

void ProxyPage::refresh()
{
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();
    const StatusBadge badge = statusBadgeFor(status.state, anyKernelInstalled(),
                                             Proxy::stateHasLocalProxy(status.state));
    stateBadge_->setText(badge.text);
    stateBadge_->setObjectName(badge.role);
    stateMessage_->setText(status.message);
    kernelInfo_->setText(kernelInfoText());

    if (currentPanel() == Panel::Nodes && allNodes_.isEmpty()) {
        reloadNodes();
    }
    if (currentPanel() == Panel::Logs && logView_ != nullptr) {
        const Result<QString> log = context_.proxy().recentLog(100000);
        logView_->setPlainText(log.isOk() ? log.value() : log.error());
    }
}

QString ProxyPage::stateBadgeText() const
{
    return stateBadge_->text();
}

QString ProxyPage::stateMessageText() const
{
    return stateMessage_->text();
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

}
