#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "gui/Prepare.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "kernel/KernelManager.h"
#include "proxy/ProxyManager.h"

using namespace Gui;

namespace {

std::shared_ptr<App::AppContext> makeContext(const QString &directory)
{
    App::AppContext::Options options;
    options.dbPath = QDir(directory).filePath(QStringLiteral("vpn-parser.db"));
    const Result<std::shared_ptr<App::AppContext>> created = App::AppContext::create(options);
    if (created.isErr()) {
        ADD_FAILURE() << created.error().toStdString();
        return nullptr;
    }
    return created.value();
}

ProxyPage *makePage(std::shared_ptr<App::AppContext> &context)
{
    auto *page = new ProxyPage(*context);
    page->resize(900, 600);
    return page;
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

}

TEST(ProxyPageTests, tabsAreTheSixPanelsInRustOrder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    auto *tabs = page->findChild<QTabBar *>();
    ASSERT_TRUE(tabs != nullptr);
    ASSERT_EQ(tabs->count(), 6);
    const QStringList expected = {UiText::panelNodes(), UiText::panelConnections(),
                                  UiText::panelRules(), UiText::panelNetwork(),
                                  UiText::panelKernels(), UiText::panelLogs()};
    QStringList actual;
    for (int index = 0; index < tabs->count(); ++index) {
        actual.push_back(tabs->tabText(index));
    }
    EXPECT_EQ(actual, expected);
    delete page;
}

TEST(ProxyPageTests, selectingAPanelMovesTheStack)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    // 默认落在"可用节点"，和 Rust 的 ProxyPanel 默认值一致。
    EXPECT_EQ(page->currentPanel(), ProxyPage::Panel::Nodes);
    page->selectPanel(ProxyPage::Panel::Logs);
    EXPECT_EQ(page->currentPanel(), ProxyPage::Panel::Logs);
    auto *stack = page->findChild<QStackedWidget *>();
    ASSERT_TRUE(stack != nullptr);
    EXPECT_EQ(stack->currentIndex(), int(ProxyPage::Panel::Logs));
    // 日志栏是这一层唯一填实的分栏：只读的多行文本。
    auto *logView = stack->widget(int(ProxyPage::Panel::Logs))->findChild<QPlainTextEdit *>();
    ASSERT_TRUE(logView != nullptr);
    EXPECT_TRUE(logView->isReadOnly());
    delete page;
}

TEST(ProxyPageTests, kernelsPanelBehaviorAndProgress)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    page->selectPanel(ProxyPage::Panel::Kernels);
    EXPECT_EQ(page->currentPanel(), ProxyPage::Panel::Kernels);
    auto *stack = page->findChild<QStackedWidget *>();
    ASSERT_TRUE(stack != nullptr);
    EXPECT_EQ(stack->currentIndex(), int(ProxyPage::Panel::Kernels));

    EXPECT_FALSE(page->kernelBusy());
    page->setKernelBusy(true);
    EXPECT_TRUE(page->kernelBusy());
    page->setKernelBusy(false);
    EXPECT_FALSE(page->kernelBusy());

    Kernel::UpdateProgress progress;
    progress.kind = Kernel::Kind::Mihomo;
    progress.stage = Kernel::UpdateStage::Downloading;
    progress.downloaded = 1000;
    progress.total = 2000;
    progress.bytesPerSecond = 500;
    page->setKernelProgress(progress);
    EXPECT_FALSE(page->kernelProgressText().isEmpty());

    page->setKernelProgress(std::nullopt);
    EXPECT_TRUE(page->kernelProgressText().isEmpty());

    delete page;
}

TEST(ProxyPageTests, badgeRuleCoversBothPreparedAndStoppedBranches)
{
    using Proxy::ProxyState;
    // 一个内核都没装、代理没在跑 —— 这才是"预备"，说"已停止"是废话。
    EXPECT_EQ(statusBadgeFor(ProxyState::Stopped, false, false).text, UiText::prepared());
    EXPECT_EQ(statusBadgeFor(ProxyState::Stopped, false, false).role.toStdString(),
              std::string("Accent"));
    // 内核装好了就不是预备：同一个 Stopped 换成"已停止"。
    EXPECT_EQ(statusBadgeFor(ProxyState::Stopped, true, false).text, UiText::stateStopped());
    EXPECT_EQ(statusBadgeFor(ProxyState::Stopped, true, false).role.toStdString(),
              std::string("Muted"));
    // 代理在跑时哪怕一个内核都"看不见可用"，也不许报预备。
    EXPECT_EQ(statusBadgeFor(ProxyState::Running, false, true).text, UiText::stateRunning());

    EXPECT_EQ(statusBadgeFor(ProxyState::Starting, true, false).role.toStdString(),
              std::string("Warning"));
    EXPECT_EQ(statusBadgeFor(ProxyState::Stopping, true, false).role.toStdString(),
              std::string("Warning"));
    EXPECT_EQ(statusBadgeFor(ProxyState::Degraded, true, false).text, UiText::stateDegraded());
    EXPECT_EQ(statusBadgeFor(ProxyState::Failed, true, false).role.toStdString(),
              std::string("Danger"));
}

TEST(ProxyPageTests, pageUsesThatSameBadgeRule)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    const Proxy::ProxyRuntimeStatus status = context->proxy().status();
    EXPECT_EQ(page->stateBadgeText(),
              statusBadgeFor(status.state, anyKernelInstalled(),
                             Proxy::stateHasLocalProxy(status.state))
                  .text);
    EXPECT_FALSE(page->stateMessageText().isEmpty());
    delete page;
}

TEST(ProxyPageTests, cardLayeringAndPortInfoComeFromRuntimeStatus)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    auto *card = page->findChild<QFrame *>(QStringLiteral("Card"));
    ASSERT_TRUE(card != nullptr);
    EXPECT_EQ(card->objectName().toStdString(), std::string("Card"));
    // 分层靠样式表 QFrame#Card 的描边 + 页面底色差；不再挂 QGraphicsDropShadowEffect
    // （每卡一个图形效果会让整棵子树离屏合成，几十张卡 + 轮询重绘 = 界面卡死）。
    EXPECT_TRUE(card->graphicsEffect() == nullptr);

    const Proxy::ProxyRuntimeStatus status = context->proxy().status();
    EXPECT_TRUE(page->kernelInfoText().contains(QString::number(status.mixedPort)));
    EXPECT_TRUE(page->kernelInfoText().contains(QString::number(status.controllerPort)));
    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
