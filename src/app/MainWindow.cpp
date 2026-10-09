#include "app/MainWindow.h"

#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "app/AppContext.h"
#include "core/Log.h"
#include "db/Database.h"
#include "gui/IpRiskDialog.h"
#include "gui/NodesPage.h"
#include "gui/Page.h"
#include "gui/Pages.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "proxy/ProxyManager.h"
#include "speedtest/SpeedTester.h"

namespace {

constexpr int kSidebarWidth = 220;

struct NavEntry {
    Gui::Tab tab;
    QString label;
};

} // namespace

namespace Gui {

MainWindow::MainWindow(App::AppContext &context, QWidget *parent)
    : QMainWindow(parent), context_(context)
{
    setWindowTitle(QStringLiteral("VPN Parser"));
    resize(1330, 800);
    setMinimumSize(960, 600);

    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildSidebar());

    auto *right = new QWidget(central);
    auto *rightBox = new QVBoxLayout(right);
    rightBox->setContentsMargins(0, 0, 0, 0);
    rightBox->setSpacing(0);

    pages_ = new QStackedWidget(right);

    auto *proxyWidget = createProxyPage(context_, pages_);
    proxyPage_ = qobject_cast<ProxyPage *>(proxyWidget);
    pages_->addWidget(proxyWidget);

    auto *nodesWidget = createNodesPage(context_, pages_);
    nodesPage_ = qobject_cast<NodesPage *>(nodesWidget);
    pages_->addWidget(nodesWidget);

    pages_->addWidget(createSourcesPage(context_, pages_));
    pages_->addWidget(createSettingsPage(context_, pages_));
    pages_->addWidget(createApiPage(context_, pages_));

    rightBox->addWidget(pages_, 1);
    rightBox->addWidget(buildStatusStrip());

    root->addWidget(right, 1);
    setCentralWidget(central);

    // 连接节点切换信号
    auto handleNodeSwitch = [this](const QString &nodeId, const QString &nodeName) {
        selectedNodeId_ = nodeId;
        selectedNodeName_ = nodeName;

        const auto nodeRes = context_.database().getNode(nodeId);
        if (nodeRes.isOk()) {
            selectedNodeServer_ = nodeRes.value().server;
        }

        if (proxyPage_ != nullptr) {
            proxyPage_->setSelectedNode(nodeId, nodeName);
        }

        const auto status = context_.proxy().status();
        if (status.localProxyActive()) {
            if (nodeRes.isOk()) {
                const auto node = nodeRes.value();
                context_.runAsync(
                    [this, node]() { context_.proxy().switchNode(node); },
                    [this, nodeName]() {
                        statusLabel_->setText(QStringLiteral("已切换到节点: %1").arg(nodeName));
                        updateStatusControls();
                        if (proxyPage_ != nullptr) {
                            proxyPage_->refresh();
                        }
                    });
            }
        } else {
            statusLabel_->setText(QStringLiteral("已选择节点: %1，可点击「启动代理」运行").arg(nodeName));
        }
        updateStatusControls();
    };

    if (proxyPage_ != nullptr) {
        connect(proxyPage_, &ProxyPage::nodeSwitchRequested, this, handleNodeSwitch);
        connect(proxyPage_, &ProxyPage::statusMessageRequested, this, [this](const QString &msg) {
            statusLabel_->setText(msg);
        });
        connect(proxyPage_, &ProxyPage::nodeIpRiskRequested, this, [this](const QString &) {
            checkIpRisk();
        });
    }

    if (nodesPage_ != nullptr) {
        connect(nodesPage_, &NodesPage::nodeSwitchRequested, this, handleNodeSwitch);
    }

    connect(&context_, &App::AppContext::proxyStatusChanged, this, [this]() {
        updateStatusControls();
        if (proxyPage_ != nullptr) {
            proxyPage_->refresh();
        }
    });

    selectTab(Tab::Proxy);
    statusLabel_->setText(context_.proxy().status().message);
    updateStatusControls();
}

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(kSidebarWidth);

    auto *box = new QVBoxLayout(sidebar);
    box->setContentsMargins(12, 14, 12, 10);
    box->setSpacing(4);

    auto *brand = new QHBoxLayout();
    brand->setSpacing(6);
    auto *logo = new QLabel(sidebar);
    logo->setObjectName(QStringLiteral("BrandLogo"));
    logo->setFixedSize(32, 32);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/app-icon.png")).scaled(
        32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand->addWidget(logo);

    auto *brandText = new QVBoxLayout();
    brandText->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("VPN Parser"), sidebar);
    title->setObjectName(QStringLiteral("BrandTitle"));
    auto *subtitle = new QLabel(UiText::brandSubtitle(), sidebar);
    subtitle->setObjectName(QStringLiteral("BrandSubtitle"));
    brandText->addWidget(title);
    brandText->addWidget(subtitle);
    brand->addLayout(brandText);
    brand->addStretch();
    box->addLayout(brand);

    const QVector<NavEntry> entries = {
        {Tab::Proxy, UiText::navConsole()},
        {Tab::Nodes, UiText::navNodePool()},
        {Tab::Sources, UiText::navSubscriptions()},
        {Tab::Settings, UiText::navSystemSettings()},
        {Tab::Api, UiText::navExternalApi()},
    };
    for (const NavEntry &entry : entries) {
        const Tab tab = entry.tab;
        auto *button = new QPushButton(entry.label, sidebar);
        button->setObjectName(QStringLiteral("Nav"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("tab", int(tab));
        connect(button, &QPushButton::clicked, this, [this, tab]() { selectTab(tab); });
        navButtons_.push_back(button);
        box->addWidget(button);
    }
    box->addStretch();
    return sidebar;
}

QWidget *MainWindow::buildStatusStrip()
{
    auto *strip = new QWidget(this);
    strip->setObjectName(QStringLiteral("StatusStrip"));

    auto *box = new QHBoxLayout(strip);
    box->setContentsMargins(14, 6, 14, 6);
    box->setSpacing(10);

    statusLabel_ = new QLabel(strip);
    statusLabel_->setObjectName(QStringLiteral("StatusLabel"));
    box->addWidget(statusLabel_, 1);

    statusActions_ = new QWidget(strip);
    statusActions_->setObjectName(QStringLiteral("StatusActions"));
    setupStatusActions();
    box->addWidget(statusActions_);

    return strip;
}

void MainWindow::setupStatusActions()
{
    auto *actions = new QHBoxLayout(statusActions_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    // 测速全部
    btnSpeedtestAll_ = new QPushButton(QStringLiteral("测速全部"), statusActions_);
    btnSpeedtestAll_->setToolTip(QStringLiteral("对所有订阅节点跑一轮批量测速：先 TCP 预筛，再测真实延迟"));
    connect(btnSpeedtestAll_, &QPushButton::clicked, this, &MainWindow::speedtestAll);
    actions->addWidget(btnSpeedtestAll_);

    // 模式 chip
    btnModeChip_ = new QPushButton(QStringLiteral("系统代理"), statusActions_);
    btnModeChip_->setObjectName(QStringLiteral("Pill"));
    btnModeChip_->setToolTip(QStringLiteral("点击切换 系统代理 / TUN 模式"));
    connect(btnModeChip_, &QPushButton::clicked, this, &MainWindow::toggleProxyMode);
    actions->addWidget(btnModeChip_);

    // IP 检测
    btnIpRisk_ = new QPushButton(QStringLiteral("IP 检测"), statusActions_);
    btnIpRisk_->setToolTip(QStringLiteral("查节点服务器 IP 的归属（机房/住宅）与风控画像"));
    connect(btnIpRisk_, &QPushButton::clicked, this, &MainWindow::checkIpRisk);
    actions->addWidget(btnIpRisk_);

    // 启动 / 停止代理
    btnToggleProxy_ = new QPushButton(QStringLiteral("启动代理"), statusActions_);
    btnToggleProxy_->setObjectName(QStringLiteral("Primary"));
    connect(btnToggleProxy_, &QPushButton::clicked, this, &MainWindow::toggleProxy);
    actions->addWidget(btnToggleProxy_);
}

void MainWindow::updateStatusControls()
{
    const auto status = context_.proxy().status();
    const bool isRunning = status.localProxyActive();
    const auto config = context_.proxy().config();

    if (btnToggleProxy_ != nullptr) {
        const QString wantedName = isRunning ? QStringLiteral("Danger") : QStringLiteral("Primary");
        const QString wantedText = isRunning ? QStringLiteral("停止代理") : QStringLiteral("启动代理");
        if (btnToggleProxy_->objectName() != wantedName) {
            btnToggleProxy_->setObjectName(wantedName);
            // 换了 objectName 后只重解析这一颗按钮的样式。以前这里是
            // Theme::apply(currentId())——那等于 app->setStyleSheet(整张表)，
            // 会让全窗口每个控件重新解析一遍 QSS（Qt 里最贵的操作之一），
            // 点节点/切代理时"卡一下"就是这么来的。
            btnToggleProxy_->style()->unpolish(btnToggleProxy_);
            btnToggleProxy_->style()->polish(btnToggleProxy_);
        }
        btnToggleProxy_->setText(wantedText);
        btnToggleProxy_->setEnabled(true);
    }

    if (btnModeChip_ != nullptr) {
        btnModeChip_->setText(config.mode == Proxy::ProxyMode::Tun ? QStringLiteral("🛡️ TUN 模式") : QStringLiteral("🌐 系统代理"));
    }
}

void MainWindow::toggleProxy()
{
    const auto status = context_.proxy().status();
    if (status.localProxyActive()) {
        statusLabel_->setText(QStringLiteral("正在停止代理..."));
        context_.runAsync(
            [this]() { context_.proxy().stop(); },
            [this]() {
                statusLabel_->setText(QStringLiteral("代理已停止"));
                updateStatusControls();
                if (proxyPage_ != nullptr) {
                    proxyPage_->refresh();
                }
            });
    } else {
        if (selectedNodeId_.isEmpty()) {
            statusLabel_->setText(QStringLiteral("请先在控制台或节点池选择一个代理节点再启动"));
            return;
        }
        const auto nodeRes = context_.database().getNode(selectedNodeId_);
        if (!nodeRes.isOk()) {
            statusLabel_->setText(QStringLiteral("无法读取选中的节点信息"));
            return;
        }
        statusLabel_->setText(QStringLiteral("正在启动代理..."));
        const auto node = nodeRes.value();
        context_.runAsync(
            [this, node]() { context_.proxy().start(node); },
            [this]() {
                const auto curStatus = context_.proxy().status();
                if (curStatus.localProxyActive()) {
                    statusLabel_->setText(QStringLiteral("代理已启动并生效"));
                } else if (!curStatus.lastError.isEmpty()) {
                    statusLabel_->setText(QStringLiteral("代理启动失败: %1").arg(curStatus.lastError));
                }
                updateStatusControls();
                if (proxyPage_ != nullptr) {
                    proxyPage_->refresh();
                }
            });
    }
}

void MainWindow::checkIpRisk()
{
    QString targetId = selectedNodeId_;
    QString targetName = selectedNodeName_;
    QString targetServer = selectedNodeServer_;

    const auto status = context_.proxy().status();
    if (targetId.isEmpty() && status.localProxyActive() && !status.nodeId.isEmpty()) {
        targetId = status.nodeId;
        const auto nodeRes = context_.database().getNode(targetId);
        if (nodeRes.isOk()) {
            targetName = nodeRes.value().name;
            targetServer = nodeRes.value().server;
        }
    }

    if (targetServer.isEmpty()) {
        statusLabel_->setText(QStringLiteral("请先选择一个节点再进行 IP 检测"));
        return;
    }

    auto *dialog = new IpRiskDialog(context_, targetId, targetName, targetServer, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

void MainWindow::toggleProxyMode()
{
    const auto status = context_.proxy().status();
    if (status.localProxyActive()) {
        statusLabel_->setText(QStringLiteral("请先停止代理后再切换工作模式"));
        return;
    }
    auto config = context_.proxy().config();
    config.mode = (config.mode == Proxy::ProxyMode::System) ? Proxy::ProxyMode::Tun : Proxy::ProxyMode::System;
    context_.proxy().updateConfig(config);
    const auto json = config.toSavedJsonText();
    if (json.isOk()) {
        context_.database().setProxyConfigJsonText(json.value());
    }
    updateStatusControls();
    statusLabel_->setText(QStringLiteral("已切换为: %1").arg(config.mode == Proxy::ProxyMode::Tun ? QStringLiteral("TUN 模式") : QStringLiteral("系统代理")));
}

void MainWindow::speedtestAll()
{
    statusLabel_->setText(QStringLiteral("正在启动全量节点测速..."));
    if (btnSpeedtestAll_ != nullptr) {
        btnSpeedtestAll_->setEnabled(false);
    }
    context_.runAsync(
        [this]() {
            Speedtest::SpeedTester tester(context_.databasePtr(), *context_.tasksPtr());
            tester.testAllSources(Speedtest::defaultOptions());
        },
        [this]() {
            if (btnSpeedtestAll_ != nullptr) {
                btnSpeedtestAll_->setEnabled(true);
            }
            statusLabel_->setText(QStringLiteral("全量节点测速已完成"));
            if (proxyPage_ != nullptr) {
                proxyPage_->refresh();
            }
            if (nodesPage_ != nullptr) {
                nodesPage_->refresh();
            }
        });
}

void MainWindow::selectTab(Tab tab)
{
    pages_->setCurrentIndex(int(tab));
    for (QPushButton *button : navButtons_) {
        button->setChecked(button->property("tab").toInt() == int(tab));
    }
    auto *pageWidget = qobject_cast<Page *>(pages_->currentWidget());
    if (pageWidget != nullptr) {
        // 取证：切页卡顿到底花在哪。refresh() 是同步跑在 GUI 线程上的，
        // SourcesPage/NodesPage 里它直接查库、ProxyPage 里它调 status()（可能等 runtimeMutex_）。
        QElapsedTimer clock;
        clock.start();
        pageWidget->refresh();
        const qint64 ms = clock.elapsed();
        if (ms > 30) {
            Log::warn(QStringLiteral("切页 tab=%1 的 refresh() 在 GUI 线程耗时 %2 ms")
                          .arg(int(tab)).arg(ms));
        }
    }
}

Tab MainWindow::currentTab() const
{
    return Tab(pages_->currentIndex());
}

QWidget *MainWindow::statusActions() const
{
    return statusActions_;
}

} // namespace Gui
