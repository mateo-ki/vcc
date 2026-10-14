#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QKeyEvent>
#include <QTextEdit>
#include <QStyle>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <optional>

#include "app/AppContext.h"
#include "api/ApiMessages.h"
#include "core/Log.h"
#include "db/Database.h"
#include "gui/AboutDialog.h"
#include "gui/IpRiskDialog.h"
#include "gui/Icons.h"
#include "gui/NodesPage.h"
#include "gui/Page.h"
#include "gui/Pages.h"
#include "gui/ProxyPage.h"
#include "gui/SourcesPage.h"
#include "gui/CommandPalette.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "proxy/ProxyManager.h"
#include "speedtest/SpeedTester.h"

namespace {

constexpr int kSidebarWidth = 220;

struct NavEntry {
    Gui::Tab tab;
    QString label;
    /// Rust 的 `nav_item(ui, target, Tab::Proxy, Icon::Dashboard, "控制台")` 一一对应。
    Gui::Icons::Kind icon;
};

} // namespace

namespace Gui {

bool MainWindow::shouldAutoSpeedtest(bool autoSpeedtestEnabled, int nodeCount)
{
    return autoSpeedtestEnabled && nodeCount > 0;
}

MainWindow::MainWindow(App::AppContext &context, QWidget *parent)
    : QMainWindow(parent), context_(context)
{
    setWindowTitle(QStringLiteral("VCC"));
    setWindowIcon(QIcon(QStringLiteral(":/app-icon.png")));
    resize(1330, 800);
    setMinimumSize(960, 600);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("CentralWidget"));
    central->setAttribute(Qt::WA_StyledBackground, true);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildSidebar());

    auto *right = new QWidget(central);
    right->setObjectName(QStringLiteral("ContentArea"));
    right->setAttribute(Qt::WA_StyledBackground, true);
    auto *rightBox = new QVBoxLayout(right);
    rightBox->setContentsMargins(0, 0, 0, 0);
    rightBox->setSpacing(0);

    pages_ = new QStackedWidget(right);
    pages_->setObjectName(QStringLiteral("PagesStack"));
    pages_->setAttribute(Qt::WA_StyledBackground, true);

    auto *proxyWidget = createProxyPage(context_, pages_);
    proxyPage_ = qobject_cast<ProxyPage *>(proxyWidget);
    pages_->addWidget(proxyWidget);

    auto *nodesWidget = createNodesPage(context_, pages_);
    nodesPage_ = qobject_cast<NodesPage *>(nodesWidget);
    pages_->addWidget(nodesWidget);

    auto *sourcesWidget = createSourcesPage(context_, pages_);
    sourcesPage_ = qobject_cast<SourcesPage *>(sourcesWidget);
    pages_->addWidget(sourcesWidget);
    pages_->addWidget(createSettingsPage(context_, pages_));
    pages_->addWidget(createApiPage(context_, pages_));

    // Ctrl+V 在**任何一页**都能导入：订阅链接、节点分享链接、JSON 源列表，三路识别都住在
    // SourcesPage::importFromClipboard 里。原来这条挂在订阅源页上，切到控制台/节点池粘贴
    // 就没反应。这里刻意用应用级事件过滤器而不是 QShortcut —— 快捷键一旦命中就把按键吃了，
    // 输入框里的 Ctrl+V 会粘不进文字（PasteShortcutTests 记着这个故障）。
    qApp->installEventFilter(this);

    rightBox->addWidget(pages_, 1);
    rightBox->addWidget(buildStatusStrip());

    root->addWidget(right, 1);
    setCentralWidget(central);

    // 换出口只有一条路：手动点卡片与自动切换共用 switchProxyNode，差别只在 reason。
    const auto manualSwitch = [this](const QString &nodeId, const QString &nodeName) {
        switchProxyNode(nodeId, nodeName, Gui::UiText::switchReasonManual());
    };

    if (proxyPage_ != nullptr) {
        connect(proxyPage_, &ProxyPage::nodeSwitchRequested, this, manualSwitch);
        connect(proxyPage_, &ProxyPage::statusMessageRequested, this, [this](const QString &msg) {
            statusLabel_->setText(msg);
        });
        // 右键菜单的四个动作全部转给节点池那三份现成实现（详情弹窗 / 单节点测速 / IP 风控），
        // 不在外壳再写一份 —— 那三处的 busy 处理与文案已经跟着 Rust 了。
        connect(proxyPage_, &ProxyPage::nodeDetailsRequested, this,
                [this](const QString &nodeId) { nodesPage_->showNodeDetails(nodeId); });
        connect(proxyPage_, &ProxyPage::nodeSpeedtestRequested, this,
                [this](const QString &nodeId) { nodesPage_->speedtestNode(nodeId); });
        connect(proxyPage_, &ProxyPage::nodeIpRiskRequested, this,
                [this](const QString &nodeId) { nodesPage_->checkIpRisk(nodeId); });
        connect(proxyPage_, &ProxyPage::nodeEgressLockRequested, this,
                &MainWindow::lockEgress);
    }

    if (nodesPage_ != nullptr) {
        connect(nodesPage_, &NodesPage::nodeSwitchRequested, this, manualSwitch);
        connect(nodesPage_, &NodesPage::statusMessageRequested, this, [this](const QString &msg) {
            statusLabel_->setText(msg);
        });
    }

    if (sourcesPage_ != nullptr) {
        // 拉取（含粘贴导入）真解析出节点之后，按设置里那颗"自动测速"开关决定要不要
        // 立刻测这一源。测速走 NodesPage::speedtestSource 这一条现成的路（进度就地更新），
        // 不另起第二套测速。
        connect(sourcesPage_, &SourcesPage::nodesFetchedForSource, this,
                [this](const QString &sourceId, int nodeCount) {
                    if (!shouldAutoSpeedtest(context_.settings().autoSpeedtestEnabled, nodeCount)) {
                        return;
                    }
                    if (nodesPage_ != nullptr) {
                        nodesPage_->speedtestSource(sourceId);
                    }
                });
        connect(sourcesPage_, &SourcesPage::statusMessageRequested, this, [this](const QString &msg) {
            statusLabel_->setText(msg);
        });
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
    initTray();
}

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(kSidebarWidth);

    auto *box = new QVBoxLayout(sidebar);
    box->setContentsMargins(12, 18, 12, 12);
    // 导航条目之间的上下间距：Rust 是 4-6px 的 add_space，用户看着太挤，这里放到 10px。
    // （有意偏离 1:1：尺寸见下面 BrandLogo 的注释。）
    box->setSpacing(9);

    auto *brand = new QHBoxLayout();
    brand->setSpacing(8);
    // Rust 的品牌行是 horizontal 里先 add_space(8) 再摆 logo：少了这 8px，
    // 图标就贴着侧栏左边缘。
    brand->addSpacing(8);
    auto *logo = new QLabel(sidebar);
    logo->setObjectName(QStringLiteral("BrandLogo"));
    // Rust 的 logo 是 32×32（`fit_to_exact_size(Vec2::splat(32.0))`）；用户要"图标大一点"，
    // 这里放到 44×44。改这一处就够了：pixmap 也按同尺寸重画，不然只是把 32 的图拉糊。
    constexpr int kBrandLogoSize = 44;
    logo->setFixedSize(kBrandLogoSize, kBrandLogoSize);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/app-icon.png")).scaled(
        kBrandLogoSize, kBrandLogoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand->addWidget(logo);

    auto *brandText = new QVBoxLayout();
    brandText->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("VCC"), sidebar);
    title->setObjectName(QStringLiteral("BrandTitle"));
    auto *subtitle = new QLabel(UiText::brandSubtitle(), sidebar);
    subtitle->setObjectName(QStringLiteral("BrandSubtitle"));
    brandText->addWidget(title);
    brandText->addWidget(subtitle);
    brand->addLayout(brandText);
    brand->addStretch();
    box->addLayout(brand);

    // Rust 在品牌行之后画一条分隔线（add_space(14) -> hline -> add_space(10)），
    // 第一条导航项离图标只剩 4px 就是这里漏的。
    auto *brandRule = new QFrame(sidebar);
    brandRule->setObjectName(QStringLiteral("SidebarRule"));
    brandRule->setFrameShape(QFrame::HLine);
    brandRule->setFrameShadow(QFrame::Plain);
    box->addSpacing(18);
    box->addWidget(brandRule);
    box->addSpacing(14);

    const QVector<NavEntry> entries = {
        {Tab::Proxy, UiText::navConsole(), Gui::Icons::Kind::Dashboard},
        {Tab::Nodes, UiText::navNodePool(), Gui::Icons::Kind::Lightning},
        {Tab::Sources, UiText::navSubscriptions(), Gui::Icons::Kind::Subscription},
        {Tab::Settings, UiText::navSystemSettings(), Gui::Icons::Kind::Gear},
        {Tab::Api, UiText::navExternalApi(), Gui::Icons::Kind::Plug},
    };
    for (const NavEntry &entry : entries) {
        const Tab tab = entry.tab;
        auto *button = new QPushButton(entry.label, sidebar);
        button->setObjectName(QStringLiteral("Nav"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("tab", int(tab));
        // 图标颜色分选中/未选中两档（Rust nav_item 的 accent 与 sidebar_muted），所以
        // 选中态一变化就要重画一次，不能只在建按钮时画一遍。
        Gui::Icons::setNavIcon(button, entry.icon);
        connect(button, &QPushButton::toggled, button,
                [button](bool) { Gui::Icons::refreshNavIcon(button); });
        connect(button, &QPushButton::clicked, this, [this, tab]() { selectTab(tab); });
        navButtons_.push_back(button);
        box->addWidget(button);
    }
    box->addStretch();

    // 侧栏最下面那颗「关于」。它不参与 tab 切换，所以既不进 navButtons_，也不能借用
    // "Nav" 这个 objectName —— 有两条测试按它数"侧栏有五个页签"，借了就变成六个。
    // 外观走语义槽模板，切主题时由 Theme::apply 重填。
    auto *aboutButton = new QPushButton(QStringLiteral("关于"), sidebar);
    aboutButton->setObjectName(QStringLiteral("AboutNav"));
    aboutButton->setCursor(Qt::PointingHandCursor);
    Gui::Theme::setWidgetCss(aboutButton,
        QStringLiteral("QPushButton { background: transparent; color: {sidebarMuted}; border: none;"
                       " text-align: left; padding: 7px 12px; }"
                       " QPushButton:hover { color: {accent}; }"));
    connect(aboutButton, &QPushButton::clicked, this, [this]() { showAboutDialog(); });
    box->addWidget(aboutButton);
    return sidebar;
}

QWidget *MainWindow::buildStatusStrip()
{
    auto *strip = new QFrame(this);
    strip->setObjectName(QStringLiteral("StatusStrip"));
    strip->setAttribute(Qt::WA_StyledBackground, true);
    strip->setMinimumHeight(44);

    auto *box = new QHBoxLayout(strip);
    box->setContentsMargins(16, 8, 16, 8);
    box->setSpacing(12);

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
    const auto config = Proxy::configForUi(context_.proxy().config());

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
    updateTray();
}

void MainWindow::toggleProxy()
{
    const auto status = context_.proxy().status();
    if (status.localProxyActive()) {
        statusLabel_->setText(QStringLiteral("正在停止代理..."));
        context_.runAsync(
            [ctx = &context_]() { ctx->proxy().stop(); },
            [this]() {
                statusLabel_->setText(QStringLiteral("代理已停止"));
                updateStatusControls();
                if (proxyPage_ != nullptr) {
                    proxyPage_->refresh();
                }
            }, this);
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
        if (requestTunElevationHandoff(selectedNodeId_)) {
            return;   // 提权实例接管，本进程马上退
        }
        statusLabel_->setText(QStringLiteral("正在启动代理..."));
        const auto node = nodeRes.value();
        auto outcome = std::make_shared<Status>();
        context_.runAsync(
            [ctx = &context_, node, outcome]() { *outcome = ctx->proxy().start(node); },
            [this, node, outcome]() {
                const auto curStatus = context_.proxy().status();
                if (outcome->isErr()) {
                    const QString message = outcome->error();
                    if (Proxy::isNodeConnectivityFailure(message)) {
                        markNodeUnavailable(node.id);
                        statusLabel_->setText(message + Api::Messages::markedUnavailableSuffix());
                    } else {
                        statusLabel_->setText(message);
                    }
                } else if (curStatus.state == Proxy::ProxyState::Degraded) {
                    // 内核进程在、本地端口也就绪，但外部连通性没验证通过 —— Rust 在这种
                    // 情况下当场把这颗节点标成不可用并说实话；vcc 之前报成"运行中"，等于
                    // 骗界面，而且坏节点会一直留在可用池里被反复选中。
                    markNodeUnavailable(node.id);
                    statusLabel_->setText(Gui::UiText::nodeDegradedMarked().arg(curStatus.message));
                } else if (curStatus.mode == Proxy::ProxyMode::Tun) {
                    statusLabel_->setText(Gui::UiText::proxyStartedTun());
                } else {
                    statusLabel_->setText(Gui::UiText::proxyStartedSystem());
                }
                updateStatusControls();
                if (proxyPage_ != nullptr) {
                    proxyPage_->refresh();
                }
            }, this);
    }
}

void MainWindow::markNodeUnavailable(const QString &nodeId)
{
    // 与 Rust 的 update_node_speed(id, false, None, None, kernel, GOOGLE_PROXY_TEST_KIND) 对应。
    QString kernelText; // 空 QString = Rust 的 None
    const std::optional<Kernel::Kind> used = context_.proxy().status().kernel;
    if (used.has_value()) {
        kernelText = Kernel::kindToText(*used);
    }
    const Status updated =
        context_.database().updateNodeSpeed(nodeId, false, std::nullopt, std::nullopt, kernelText,
                                            QString::fromLatin1(Models::kGoogleProxyTestKind));
    if (updated.isErr()) {
        Log::warn(Api::Messages::markFailurePrefix().replace(QStringLiteral("{update_error}"),
                                                             updated.error()));
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

void MainWindow::showAboutDialog()
{
    // 非模态：它要在后台问一次 GitHub，用户不该被一颗转圈等的框按住。
    auto *dialog = new Gui::AboutDialog(context_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::toggleProxyMode()
{
    const auto status = context_.proxy().status();
    if (status.localProxyActive()) {
        statusLabel_->setText(QStringLiteral("请先停止代理后再切换工作模式"));
        return;
    }
    auto config = Proxy::configForUi(context_.proxy().config());
    config.mode = (config.mode == Proxy::ProxyMode::System) ? Proxy::ProxyMode::Tun : Proxy::ProxyMode::System;
    context_.proxy().updateConfig(config);
    const auto json = config.toSavedJsonText();
    if (json.isOk()) {
        context_.database().setProxyConfigJsonText(json.value());
    }
    updateStatusControls();
    statusLabel_->setText(QStringLiteral("已切换为: %1").arg(config.mode == Proxy::ProxyMode::Tun ? QStringLiteral("TUN 模式") : QStringLiteral("系统代理")));
}

void MainWindow::lockEgress(const QString &nodeId, const QString &nodeName)
{
    // 两个开关不该同时表达"我要怎么选路"：显式锁定就把自动选路关掉，
    // 想回到全自动再自己勾回来（Rust proxy.rs:736-741）。
    Proxy::MihomoConfigOptions options = Proxy::configForUi(context_.proxy().config());
    options.urlTest = false;
    context_.proxy().updateConfig(options);
    statusLabel_->setText(Gui::UiText::egressLockedStatus2().arg(nodeName));
    switchProxyNode(nodeId, nodeName, Gui::UiText::switchReasonManual());
}

Speedtest::SpeedTestOptions MainWindow::speedtestAllOptions() const
{
    // 节点池那一份参数装配（强度下拉框 + 「系统设置 → 测速」那一组开关）。
    // 页面还没建出来时退回默认档，不拿空指针去装配参数。
    if (nodesPage_ != nullptr) {
        return nodesPage_->speedOptions();
    }
    return Speedtest::speedOptionsFor(context_.settings(), Speedtest::defaultIntensity());
}

void MainWindow::speedtestAll()
{
    if (!context_.tryBeginFullSpeedtest()) {
        // 已经有一趟在跑了（另一个入口起的）：静默不重复起，进度文案归那一趟说。
        return;
    }
    // 文案与参数都走 Rust 那一条路（speedtest_all_sources + speed_opts）：
    // 之前这里用 defaultOptions()，等于把「系统设置 → 测速」的并发/超时/下载/常驻内核/
    // 并行内核/复用分钟/条件隔离整组忽略，状态栏那套中文也是自己编的。
    statusLabel_->setText(Gui::UiText::speedAllInitial());
    if (btnSpeedtestAll_ != nullptr) {
        btnSpeedtestAll_->setEnabled(false);
    }
    // Rust 只有一个测速强度旋钮（`speedtest_intensity`），界面上每一条测速入口都从
    // `speed_opts()` 取它 —— 底部这颗不能自己钉 Standard，否则同一句「测速全部」在
    // 两个入口会用两个超时（节点池那侧读下拉框）。
    const Speedtest::SpeedTestOptions options = speedtestAllOptions();
    QPointer<MainWindow> guard(this);
    auto outcome = std::make_shared<Result<Models::SpeedTestSummary>>();
    context_.runAsync(
        [ctx = &context_, guard, options, outcome]() {
            Speedtest::SpeedTester tester(ctx->databasePtr(), *ctx->tasksPtr());
            *outcome = tester.testAllSourcesWithProgress(options, [guard](size_t done, size_t total) {
                if (guard.isNull()) return;
                QMetaObject::invokeMethod(guard.data(), [guard, done, total]() {
                    if (guard.isNull() || guard->statusLabel_ == nullptr) return;
                    guard->statusLabel_->setText(Gui::UiText::speedAllProgress()
                                                     .arg(qint64(done))
                                                     .arg(qint64(total)));
                }, Qt::QueuedConnection);
            });
        },
        [this, outcome]() {
            context_.endFullSpeedtest();
            if (btnSpeedtestAll_ != nullptr) {
                btnSpeedtestAll_->setEnabled(true);
            }
            statusLabel_->setText(outcome->isOk() ? Gui::speedDoneText(outcome->value())
                                                  : Gui::UiText::speedDoneFailed().arg(outcome->error()));
            if (proxyPage_ != nullptr) {
                // 全量测速重写了几千行的 latency_test_kind，控制台那份缓存必须作废，
                // 否则它挂的还是开屏那一刻的"可用节点"列表。
                proxyPage_->requestFullReload();
                proxyPage_->refresh();
            }
            if (nodesPage_ != nullptr) {
                nodesPage_->refresh();
            }
        }, this);
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

void MainWindow::initTray()
{
    if (trayIcon_ != nullptr) {
        return;
    }
    trayIcon_ = new QSystemTrayIcon(this);
    trayIcon_->setIcon(QIcon(QStringLiteral(":/app-icon.png")));
    trayIcon_->setToolTip(QStringLiteral("VCC - 网络代理控制台"));

    trayMenu_ = new QMenu(this);
    actShow_ = trayMenu_->addAction(QStringLiteral("显示主界面"), this, [this]() {
        showNormal();
        activateWindow();
        raise();
    });

    actToggleProxy_ = trayMenu_->addAction(QStringLiteral("启动代理"), this, [this]() {
        toggleProxy();
    });

    trayMenu_->addAction(QStringLiteral("关于 VCC"), this, [this]() {
        showAboutDialog();
    });

    trayMenu_->addSeparator();

    actQuit_ = trayMenu_->addAction(QStringLiteral("退出程序"), this, [this]() {
        forceQuit_ = true;
        close();
        qApp->quit();
    });

    trayIcon_->setContextMenu(trayMenu_);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible() && !isMinimized()) {
                hide();
            } else {
                showNormal();
                activateWindow();
                raise();
            }
        }
    });

    trayIcon_->show();
    updateTray();
}

void MainWindow::updateTray()
{
    if (trayIcon_ == nullptr || actToggleProxy_ == nullptr) {
        return;
    }
    const auto status = context_.proxy().status();
    const bool running = status.localProxyActive();
    actToggleProxy_->setText(running ? QStringLiteral("停止代理") : QStringLiteral("启动代理"));
    if (running) {
        trayIcon_->setToolTip(QStringLiteral("VCC - 代理运行中 (端口 %1)").arg(status.mixedPort));
    } else {
        trayIcon_->setToolTip(QStringLiteral("VCC - 网络代理控制台"));
    }

    // 故障自动切换的心跳。Rust 那边是"每帧调 check_failover、闸门在函数里"，这里 5 秒
    // 看一眼的效果一样，而且不用跟着重绘转。
    failoverTimer_ = new QTimer(this);
    connect(failoverTimer_, &QTimer::timeout, this, &MainWindow::checkFailover);
    failoverTimer_->start(5000);
}

MainWindow::~MainWindow()
{
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeEventFilter(this);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (!key->isAutoRepeat() && (key->modifiers() & Qt::ControlModifier) &&
            (key->key() == Qt::Key_K || key->key() == Qt::Key_P)) {
            showCommandPalette();
            return true;
        }
        if (!key->isAutoRepeat() && key->matches(QKeySequence::Paste) && sourcesPage_ != nullptr) {
            auto *widget = qobject_cast<QWidget *>(watched);
            // 只管本窗口里的粘贴：模态对话框是另一扇顶层窗，它自己的 Ctrl+V 不碰。
            if (widget != nullptr && widget->window() == this
                && qobject_cast<QLineEdit *>(widget) == nullptr
                && qobject_cast<QTextEdit *>(widget) == nullptr
                && qobject_cast<QPlainTextEdit *>(widget) == nullptr) {
                sourcesPage_->importFromClipboard();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showCommandPalette()
{
    CommandPaletteHost host;
    host.selectTab = [this](int tabIndex) { selectTab(static_cast<Tab>(tabIndex)); };
    host.startNode = [this](const QString &nodeId) { startHandoffNode(nodeId); };
    auto *palette = new CommandPalette(context_, std::move(host), this);
    const QPoint center = geometry().center();
    palette->move(center.x() - palette->width() / 2, qMax(geometry().top() + 80, center.y() - palette->height() / 2));
    palette->show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!forceQuit_ && context_.settings().closeToTray && trayIcon_ != nullptr && trayIcon_->isVisible()) {
        event->ignore();
        hide();
        trayIcon_->showMessage(QStringLiteral("VCC"), QStringLiteral("已最小化到系统托盘，后台保持运行"),
                               QSystemTrayIcon::Information, 1200);
    } else {
        event->accept();
    }
}

} // namespace Gui
