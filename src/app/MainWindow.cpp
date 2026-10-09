#include "app/MainWindow.h"

#include <QHBoxLayout>
#include <QPixmap>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "app/AppContext.h"
#include "gui/Page.h"
#include "gui/Pages.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "proxy/ProxyManager.h"

namespace {

/// Rust 的 SidePanel::left("sidebar").exact_width(220).resizable(false)。
constexpr int kSidebarWidth = 220;

struct NavEntry {
    Gui::Tab tab;
    QString label;
};

}

namespace Gui {

MainWindow::MainWindow(App::AppContext &context, QWidget *parent)
    : QMainWindow(parent), context_(context)
{
    // eframe::NativeOptions：inner 1330x800，min 960x600，标题 VPN Parser。
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
    pages_->addWidget(createProxyPage(context_, pages_));
    pages_->addWidget(createNodesPage(context_, pages_));
    pages_->addWidget(createSourcesPage(context_, pages_));
    pages_->addWidget(createSettingsPage(context_, pages_));
    pages_->addWidget(createApiPage(context_, pages_));
    rightBox->addWidget(pages_, 1);
    rightBox->addWidget(buildStatusStrip());

    root->addWidget(right, 1);
    setCentralWidget(central);

    selectTab(Tab::Proxy);
    statusLabel_->setText(context_.proxy().status().message);
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
    // 状态条纵向内边距 5、横向 14（Rust 2026-10-07 把按钮压矮之后跟着收的）。
    box->setContentsMargins(14, 5, 14, 5);
    box->setSpacing(10);

    statusLabel_ = new QLabel(strip);
    statusLabel_->setObjectName(QStringLiteral("StatusLabel"));
    box->addWidget(statusLabel_);

    // 控制按钮（启动/停止代理、IP 检测、模式 chip、测速全部）由控制台层填进来：
    // Rust 侧它们是 bottom-up 布局里靠右的一组，先加的在最右。
    statusActions_ = new QWidget(strip);
    statusActions_->setObjectName(QStringLiteral("StatusActions"));
    auto *actions = new QHBoxLayout(statusActions_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    box->addStretch();
    box->addWidget(statusActions_);
    return strip;
}

void MainWindow::selectTab(Tab tab)
{
    pages_->setCurrentIndex(int(tab));
    for (QPushButton *button : navButtons_) {
        button->setChecked(button->property("tab").toInt() == int(tab));
    }
    auto *pageWidget = qobject_cast<Page *>(pages_->currentWidget());
    if (pageWidget != nullptr) {
        pageWidget->refresh();
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

}
