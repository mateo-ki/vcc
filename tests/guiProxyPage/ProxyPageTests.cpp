#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "gui/Prepare.h"
#include "gui/ProxyPage.h"
#include "gui/Widgets.h"
#include "proxy/MihomoConfig.h"
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

/// 按控件挂的样式表必须①没有没填上的语义槽 ②切主题时真的跟着走。
/// Why：页面里以前散着 #ef4444/#10b981/#f59e0b 这类写死的颜色（Rust 的 theme.rs 早就
/// 定了"不许出现红和黄"），改成 `{槽}` 模板之后，这两条是模板机制自己的守卫：
/// 槽名打错 = 留下一个没人填的 `{accent}`；忘挂模板 = 切到"青绿"后聚焦边框还是钴蓝。
TEST(ProxyPageTests, widgetCssTemplatesResolveAndFollowThemeSwitch)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->resize(1100, 800);
    page->show();
    // 分栏是懒建的：逐个打开，否则这一轮只审到"节点"那一栏的控件。
    for (int index = 0; index <= int(ProxyPage::Panel::Logs); ++index) {
        page->selectPanel(ProxyPage::Panel(index));
        QCoreApplication::processEvents();
    }

    static const QRegularExpression slotPattern(QStringLiteral("\\{[A-Za-z]+\\}"));
    int templated = 0;
    QStringList unresolved;
    const QList<QWidget *> widgets = page->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const QString pattern = widget->property("paletteCss").toString();
        if (pattern.isEmpty()) {
            continue;
        }
        ++templated;
        if (slotPattern.match(widget->styleSheet()).hasMatch()) {
            unresolved.push_back(QStringLiteral("%1 %2")
                                     .arg(QString::fromLatin1(widget->metaObject()->className()),
                                          widget->styleSheet().left(60)));
        }
    }
    EXPECT_GE(templated, 20) << "带模板的控件太少，这一轮等于什么都没审";
    EXPECT_TRUE(unresolved.isEmpty())
        << "有没填上的语义槽（槽名写错就会这样留在样式表里）: "
        << unresolved.join(QStringLiteral(" ; ")).toStdString();

    // 拿一颗"模板里带强调色"的控件验证跟随：预设按钮那份样式表有 {accent}/{accentBg}。
    // （下拉框不算：它的外观已经整体收回应用级 QSS，控件不再自带一份，
    //  见 ComboTheme.appStyleSheetOnlyUsesPaletteColors。）
    QPushButton *preset = page->findChild<QPushButton *>(QStringLiteral("PresetAiButton"));
    ASSERT_TRUE(preset != nullptr);
    const QString cobalt = Theme::palette(Theme::kDefaultThemeId).accent.name();
    ASSERT_TRUE(preset->styleSheet().contains(cobalt))
        << "默认主题下预设按钮的描边就该是钴蓝: " << preset->styleSheet().left(90).toStdString();

    Theme::apply(QStringLiteral("soft-teal"));
    QCoreApplication::processEvents();
    const QString teal = Theme::current().accent.name();
    ASSERT_NE(teal, cobalt) << "两套主题的强调色一样，这条断言就什么都没测";
    EXPECT_TRUE(preset->styleSheet().contains(teal))
        << "切完主题，按控件那份 CSS 还留着上一个主题的强调色";
    EXPECT_FALSE(preset->styleSheet().contains(cobalt));
    // 应用级那张表也要跟着换（下拉框的外观全靠它）。
    EXPECT_TRUE(qApp->styleSheet().contains(teal)) << "应用级 QSS 没跟着主题走";

    Theme::apply(Theme::kDefaultThemeId);
    QCoreApplication::processEvents();
    EXPECT_TRUE(preset->styleSheet().contains(cobalt)) << "切回来也要一致";
    delete page;
}

/// 首启用默认主题时，应用级 QSS 必须真的挂上。
/// 这条钉的是一个只有"新库/新装"才会踩到的 bug：apply() 里那句"主题 id 没变就直接 return"
/// 比的是静态初值，而初值本身就等于默认主题 id —— 于是应用级样式表一次都没挂，
/// 卡片间距和页面底色全丢，只剩每块区域自带的那份底（Release 截图就是这个样子）。
TEST(ThemeApply, installingTheAlreadyCurrentThemeStillPutsUpTheAppStyleSheet)
{
    const QString id = Gui::Theme::currentId();
    ASSERT_FALSE(id.isEmpty());

    qApp->setStyleSheet(QString());
    ASSERT_TRUE(qApp->styleSheet().isEmpty());

    Gui::Theme::apply(id);

    const QString sheet = qApp->styleSheet();
    EXPECT_FALSE(sheet.isEmpty()) << "主题 id 没变就不挂样式表 = 首启用默认主题时界面没有应用级 QSS";
    EXPECT_TRUE(sheet.contains(QStringLiteral("#Card"))) << sheet.left(120).toStdString();
    EXPECT_TRUE(sheet.contains(QStringLiteral("background"))) << sheet.left(120).toStdString();
}

/// TUN 要提权、系统代理不要（Rust mod.rs:4699-4701 的三条同名断言）。
TEST(TunElevation, onlyTunModeNeedsIt)
{
    EXPECT_TRUE(Gui::requiresTunElevation(Proxy::ProxyMode::Tun, false));
    EXPECT_FALSE(Gui::requiresTunElevation(Proxy::ProxyMode::Tun, true));
    EXPECT_FALSE(Gui::requiresTunElevation(Proxy::ProxyMode::System, false));
}

/// 提权交接的配置编码：两端是同一个进程的两份实例，但字符表必须和 Rust 的
/// URL_SAFE_NO_PAD 一致 —— 值可能以 '-' 开头，父进程发的和子进程解的必须对得上。
TEST(TunElevation, handoffConfigRoundTripsAsUrlSafeNoPad)
{
    Proxy::MihomoConfigOptions options;
    options.mode = Proxy::ProxyMode::Tun;
    options.mixedPort = 9980;
    options.controllerPort = 9090;
    options.terminalProxy = true;
    options.allowLan = true;

    const auto encoded = Proxy::MihomoConfigOptions::encodeHandoffConfig(options);
    ASSERT_TRUE(encoded.isOk()) << encoded.error().toStdString();
    const QString text = encoded.value();
    EXPECT_FALSE(text.contains(QLatin1Char('+'))) << text.toStdString();
    EXPECT_FALSE(text.contains(QLatin1Char('/'))) << text.toStdString();
    EXPECT_FALSE(text.contains(QLatin1Char('='))) << text.toStdString();

    const auto decoded = Proxy::MihomoConfigOptions::decodeHandoffConfig(text);
    ASSERT_TRUE(decoded.isOk()) << decoded.error().toStdString();
    EXPECT_EQ(decoded.value().mode, Proxy::ProxyMode::Tun);
    EXPECT_EQ(decoded.value().mixedPort, options.mixedPort);
    EXPECT_EQ(decoded.value().controllerPort, options.controllerPort);
    EXPECT_EQ(decoded.value().terminalProxy, options.terminalProxy);
    EXPECT_EQ(decoded.value().allowLan, options.allowLan);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
