#include "gui/ProxyPage.h"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>

#include "app/AppContext.h"
#include "gui/Prepare.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "kernel/KernelManager.h"
#include "proxy/ProxyManager.h"

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
    // 先把六个格子摆出来：Nodes/Rules/Network/Kernels/Connections 由后续层填实，
    // Logs 这一层已经能用。
    for (int index = 0; index < 6; ++index) {
        auto *panel = new QWidget(stack_);
        panel->setObjectName(QStringLiteral("Panel%1").arg(index));
        stack_->addWidget(panel);
    }
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
    stateBadge_->adjustSize();
    stateMessage_->setText(status.message);
    kernelInfo_->setText(kernelInfoText());
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
