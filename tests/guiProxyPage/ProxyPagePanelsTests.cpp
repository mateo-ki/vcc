// 控制台其余分栏（DNS/TUN、活跃连接、运行日志、状态卡）的 1:1 断言。
// 期望值全部取 tools/gen-cjk-codepoints.mjs 生成的 UiText，测试里不手打中文。
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "app/AppContext.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "proxy/ConfigTypes.h"
#include "proxy/MihomoConfig.h"

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
    page->resize(980, 760);
    return page;
}

QWidget *findWidget(QWidget *root, const QString &name)
{
    for (auto *widget : root->findChildren<QWidget *>()) {
        if (widget->property("testName").toString() == name) {
            return widget;
        }
    }
    return nullptr;
}

QPushButton *findButton(QWidget *root, const QString &name)
{
    for (auto *button : root->findChildren<QPushButton *>()) {
        if (button->objectName() == name || button->property("testName").toString() == name) {
            return button;
        }
    }
    return nullptr;
}

QStringList labelTexts(QWidget *root)
{
    QStringList out;
    for (auto *label : root->findChildren<QLabel *>()) {
        out.push_back(label->text());
    }
    return out;
}

QStringList checkBoxTexts(QWidget *root)
{
    QStringList out;
    for (auto *box : root->findChildren<QCheckBox *>()) {
        out.push_back(box->text());
    }
    return out;
}

Proxy::MihomoConfigOptions networkWith(QStringList nameservers, Proxy::ProxyMode mode)
{
    Proxy::MihomoConfigOptions options;
    options.mode = mode;
    options.dns.nameservers = std::move(nameservers);
    return options;
}

void spinUntil(const std::function<bool()> &condition, qint64 timeoutMs = 8000)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
    }
}

}

TEST(ProxyPagePanelsTests, networkPanelIsOneCardWithRustLabels)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->selectPanel(ProxyPage::Panel::Network);
    QCoreApplication::processEvents();

    QWidget *card = findWidget(page, QStringLiteral("NetworkCard"));
    ASSERT_TRUE(card != nullptr) << "DNS/TUN 栏没按 Rust 收成一张卡";

    const QStringList labels = labelTexts(page);
    EXPECT_TRUE(labels.contains(UiText::netSectionHint()));
    EXPECT_TRUE(labels.contains(UiText::netModeStrong()));
    EXPECT_TRUE(labels.contains(UiText::netKernelLabel()));
    EXPECT_TRUE(labels.contains(UiText::netPortMixed()));
    EXPECT_TRUE(labels.contains(UiText::netPortController()));

    // Rust 的这些标签一个冒号都没有（"运行内核:" 那种是我们自己加的）。
    QStringList colons;
    for (const auto &text : labelTexts(card)) {
        if (text.endsWith(QLatin1Char(':'))) {
            colons.push_back(text);
        }
    }
    EXPECT_TRUE(colons.isEmpty()) << colons.join(QStringLiteral(" ; ")).toStdString();

    const QStringList boxes = checkBoxTexts(card);
    EXPECT_TRUE(boxes.contains(UiText::netTerminalProxy()));
    EXPECT_TRUE(boxes.contains(UiText::netAllowLan()));
    EXPECT_TRUE(boxes.contains(UiText::netDnsEnable())) << "Rust 的 DNS 开关文案是「启用」";
    for (auto *box : card->findChildren<QCheckBox *>()) {
        if (box->text() == UiText::netTerminalProxy()) {
            // 终端代理那条既有全文也有 hover（hover 里带一个真换行）。
            EXPECT_EQ(box->toolTip().toStdString(),
                      UiText::netTerminalProxyHover().toStdString());
            EXPECT_TRUE(box->toolTip().contains(QLatin1Char('\n')));
            EXPECT_TRUE(box->toolTip().contains(QStringLiteral("HKCU")));
        }
    }

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, networkPanelIsASingleCard)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->selectPanel(ProxyPage::Panel::Network);
    QCoreApplication::processEvents();

    // Rust 的 DNS/TUN 栏是一张卡里三条粗体小标题；我们曾经拆成四张卡并编了标题。
    // 只数这一栏子树里的卡（别的分栏也有卡）。
    QWidget *card = findWidget(page, QStringLiteral("NetworkCard"));
    ASSERT_TRUE(card != nullptr);
    QStackedWidget *stack = page->findChild<QStackedWidget *>();
    ASSERT_TRUE(stack != nullptr);
    QWidget *panel = card;
    while (panel->parentWidget() != nullptr && panel->parentWidget() != stack) {
        panel = panel->parentWidget();
    }
    int cards = 0;
    for (auto *frame : panel->findChildren<QFrame *>()) {
        if (frame->objectName() == QLatin1String("Card")) {
            ++cards;
        }
    }
    EXPECT_EQ(cards, 1);
    // 小标题走 strong，而不是再造一个 section_title。
    const QStringList labels = labelTexts(card);
    EXPECT_TRUE(labels.contains(UiText::netModeStrong()));
    EXPECT_FALSE(labels.contains(UiText::netSectionHint())) << "副标题在卡外，不在卡内";

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, networkTunGroupFollowsModeAndNoticeFollowsBusy)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->selectPanel(ProxyPage::Panel::Network);

    // Rust: TUN 参数整段只在 mode == Tun 时出现。
    page->setNetworkConfigForTest(networkWith({QStringLiteral("a.example")},
                                              Proxy::ProxyMode::System));
    EXPECT_FALSE(page->networkTunGroupVisible());
    EXPECT_TRUE(page->networkControlsEnabled()) << "停止状态下列表面板该可编辑";
    EXPECT_TRUE(page->networkNoticeText().isEmpty());

    page->setNetworkConfigForTest(networkWith({QStringLiteral("a.example")},
                                              Proxy::ProxyMode::Tun));
    EXPECT_TRUE(page->networkTunGroupVisible());

    QWidget *tunGroup = nullptr;
    for (QWidget *widget : page->findChildren<QWidget *>()) {
        if (widget->property("testName").toString() == QLatin1String("TunGroup")) {
            tunGroup = widget;
        }
    }
    ASSERT_TRUE(tunGroup != nullptr);
    EXPECT_TRUE(labelTexts(tunGroup).contains(UiText::netTunStrong()));
    EXPECT_TRUE(labelTexts(tunGroup).contains(UiText::netStackLabel()));
    EXPECT_TRUE(checkBoxTexts(tunGroup).contains(UiText::netAutoRoute()));
    EXPECT_TRUE(checkBoxTexts(tunGroup).contains(UiText::netAutoDetect()));

    // 配置作业在飞（Rust 的 proxy_busy）→ 整片禁用 + "停止代理后可修改运行配置"。
    page->setRulesBusy(true);
    EXPECT_EQ(page->networkNoticeText().toStdString(), UiText::netBusyNotice().toStdString());
    EXPECT_FALSE(page->networkControlsEnabled());
    page->setRulesBusy(false);
    EXPECT_TRUE(page->networkNoticeText().isEmpty());
    EXPECT_TRUE(page->networkControlsEnabled());

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, dnsRowsFollowTheNameserverList)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->selectPanel(ProxyPage::Panel::Network);

    page->setNetworkConfigForTest(networkWith({QStringLiteral("one.example")},
                                              Proxy::ProxyMode::System));
    ASSERT_EQ(page->networkDnsRowCount(), 1);
    EXPECT_EQ(page->networkDnsRowLabel(0).toStdString(), "DNS 1");

    page->setNetworkConfigForTest(networkWith({QStringLiteral("a"), QStringLiteral("b"),
                                               QStringLiteral("c")},
                                              Proxy::ProxyMode::System));
    ASSERT_EQ(page->networkDnsRowCount(), 3) << "Rust 是按列表逐条画行，不是固定主/备两行";
    EXPECT_EQ(page->networkDnsRowLabel(2).toStdString(), "DNS 3");

    // Rust 渲染时的副作用：列表空着就先塞一条默认主 DNS（只塞一条）。
    page->setNetworkConfigForTest(networkWith({}, Proxy::ProxyMode::System));
    EXPECT_EQ(page->networkDnsRowCount(), 1);

    QWidget *rows = page->findChild<QWidget *>(QStringLiteral("DnsRows"));
    ASSERT_TRUE(rows != nullptr);
    const auto edits = rows->findChildren<QLineEdit *>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.first()->placeholderText().toStdString(),
              QString::fromLatin1(Proxy::kDefaultDnsPrimary).toStdString());

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, connectionsEmptyStateAndClearButtonFollowRust)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->selectPanel(ProxyPage::Panel::Connections);
    QCoreApplication::processEvents();

    // 没运行 → Rust 的 "代理未在运行中，暂无活跃连接数据"；筛选标签不带冒号。
    const QStringList texts = labelTexts(page);
    EXPECT_TRUE(texts.contains(UiText::connEmptyStopped()));
    EXPECT_TRUE(texts.contains(UiText::connFilterLabel()));
    EXPECT_FALSE(texts.contains(UiText::connFilterLabel() + QLatin1Char(':')));

    QLineEdit *filter = page->findChild<QLineEdit *>("");
    filter = nullptr;
    for (auto *edit : page->findChildren<QLineEdit *>()) {
        if (edit->property("testName").toString() == QLatin1String("ConnFilterEdit")) {
            filter = edit;
        }
    }
    ASSERT_TRUE(filter != nullptr);
    QPushButton *clear = nullptr;
    for (auto *button : page->findChildren<QPushButton *>()) {
        if (button->property("testName").toString() == QLatin1String("ClearFilterButton")) {
            clear = button;
        }
    }
    ASSERT_TRUE(clear != nullptr);
    // 按 Rust：只在筛选框非空时才出现。
    EXPECT_TRUE(clear->isHidden());
    filter->setText(QStringLiteral("zoom"));
    QCoreApplication::processEvents();
    EXPECT_FALSE(clear->isHidden());
    filter->clear();
    QCoreApplication::processEvents();
    EXPECT_TRUE(clear->isHidden());

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, logsPanelKeepsRustTitleSubtitleAndEmptyLabel)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->selectPanel(ProxyPage::Panel::Logs);
    page->refresh();
    spinUntil([page]() { return labelTexts(page).contains(UiText::logsEmpty()); });

    const QStringList texts = labelTexts(page);
    EXPECT_TRUE(texts.contains(UiText::logsTitle())) << "Rust 的标题是固定的「运行日志」";
    // 副标题带内核名；这里没有内核在跑，应该是 Rust 的那条兜底名。
    EXPECT_TRUE(texts.contains(UiText::logsSubtitle().arg(UiText::logsKernelFallback())));
    // 空日志是卡内一条 muted 文案，不是输入框占位符。
    EXPECT_TRUE(texts.contains(UiText::logsEmpty()));

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, savingConfigReportsTheRustStatusPair)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    QStringList messages;
    QObject::connect(page, &ProxyPage::statusMessageRequested, page,
                     [&messages](const QString &text) { messages.push_back(text); });

    page->selectPanel(ProxyPage::Panel::Rules);
    Proxy::MihomoConfigOptions rules;
    rules.rules = Proxy::recommendedTrafficRules();
    page->setRulesConfigForTest(rules);
    messages.clear();

    QPushButton *saveRules = findButton(page, QStringLiteral("SaveRulesButton"));
    ASSERT_TRUE(saveRules != nullptr);
    saveRules->click();
    ASSERT_FALSE(messages.isEmpty()) << "保存一开始就该报\"正在保存…\"";
    EXPECT_EQ(messages.first().toStdString(), UiText::savingConfig().toStdString());
    spinUntil([page]() { return !page->rulesBusy(); });
    ASSERT_EQ(messages.size(), 2) << messages.join(QStringLiteral(" | ")).toStdString();
    EXPECT_EQ(messages.last().toStdString(), UiText::configSaved().toStdString());

    // 网络分栏走同一条保存路径（Rust 只有一处 save_proxy_config）。
    page->selectPanel(ProxyPage::Panel::Network);
    // 先验非法 DNS 走的是 Rust 的失败文案（validate() 会拒非 IP/非 HTTPS URL）。
    page->setNetworkConfigForTest(networkWith({QStringLiteral("a.example")},
                                              Proxy::ProxyMode::System));
    messages.clear();
    QPushButton *saveNetwork = findButton(page, QStringLiteral("SaveNetworkButton"));
    ASSERT_TRUE(saveNetwork != nullptr);
    EXPECT_EQ(saveNetwork->text().toStdString(), UiText::netSaveButton().toStdString());
    saveNetwork->click();
    ASSERT_FALSE(messages.isEmpty());
    EXPECT_EQ(messages.first().toStdString(), UiText::savingConfig().toStdString());
    spinUntil([page]() { return !page->rulesBusy(); });
    ASSERT_EQ(messages.size(), 2) << messages.join(QStringLiteral(" | ")).toStdString();
    EXPECT_TRUE(messages.last().startsWith(UiText::configSaveFailed()
                                               .arg(QString()).left(1)) != false);
    EXPECT_FALSE(messages.last().contains(UiText::configSaved()));

    // 合法配置才是"代理配置已保存"。
    page->setNetworkConfigForTest(
        networkWith({QString::fromLatin1(Proxy::kDefaultDnsPrimary)}, Proxy::ProxyMode::System));
    messages.clear();
    saveNetwork->click();
    spinUntil([page]() { return !page->rulesBusy(); });
    ASSERT_EQ(messages.size(), 2) << messages.join(QStringLiteral(" | ")).toStdString();
    EXPECT_EQ(messages.last().toStdString(), UiText::configSaved().toStdString());

    page->deleteLater();
    // deleteLater 不会被 processEvents() 冲掉（默认不含 DeferredDelete）：不手动结算的话，
    // 这一页会带着已经析构掉的 AppContext 继续活到下一个测试，它的 1.5 s 轮询定时器一响
    // 就是 use-after-free —— 堆被写坏之后要到下一个测试分配时才炸，看着像"后面的测试自己崩了"。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(ProxyPagePanelsTests, formatSpeedHasNoGigabyteTier)
{
    // Rust 的 format_speed 只有 MB/s（两位）/ KB/s（一位）/ B/s 三档，
    // 和 format_bytes（带 GB 档）不一样。
    EXPECT_EQ(formatSpeed(0).toStdString(), "0 B/s");
    EXPECT_EQ(formatSpeed(1023).toStdString(), "1023 B/s");
    EXPECT_EQ(formatSpeed(1024).toStdString(), "1.0 KB/s");
    EXPECT_EQ(formatSpeed(1536).toStdString(), "1.5 KB/s");
    EXPECT_EQ(formatSpeed(1024ULL * 1024ULL).toStdString(), "1.00 MB/s");
    EXPECT_EQ(formatSpeed(1024ULL * 1024ULL * 1024ULL).toStdString(), "1024.00 MB/s");
    EXPECT_EQ(formatBytes(1024ULL * 1024ULL * 1024ULL).toStdString(), "1.00 GB");
}

/// 守卫：分流规则 / DNS-TUN 两栏里，每一块区域（Card 与 InnerBlock）都必须自己带一份
/// 底色模板，而且模板解出来就是主题的那个色 —— 用户报的"块和块分不开"就是这类缺底。
/// 不逐个 grab：一张卡的子树里挂着表格和滚动区，整棵重画会挂住（这一版踩过一次）。
TEST(ProxyPageAudit, everyBlockInRulesAndNetworkCarriesItsOwnBackground)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    auto *page = new ProxyPage(*context);
    page->resize(1180, 900);
    page->show();
    QCoreApplication::processEvents();

    const Theme::Palette pal = Theme::current();
    int blocks = 0;
    for (int index : {int(ProxyPage::Panel::Rules), int(ProxyPage::Panel::Network)}) {
        page->selectPanel(ProxyPage::Panel(index));
        QCoreApplication::processEvents();
        // 选中哪一栏，看得见的那个滚动区就是它（页面上不止一个 QStackedWidget，
        // 按索引摸会摸到别处）。
        QWidget *panel = nullptr;
        const QList<QScrollArea *> scrolls = page->findChildren<QScrollArea *>();
        for (QScrollArea *scroll : scrolls) {
            if (scroll->isVisible() && scroll->widget() != nullptr) {
                panel = scroll->widget();
            }
        }
        ASSERT_TRUE(panel != nullptr) << "分栏 " << index << " 找不到内容面板";
        int checkedHere = 0;
        const QList<QFrame *> frames = panel->findChildren<QFrame *>();
        for (QFrame *frame : frames) {
            const QString name = frame->objectName();
            const bool isCard = name == QLatin1String("Card");
            if (!isCard && name != QLatin1String("InnerBlock")) {
                continue;
            }
            const QString pattern = frame->property("paletteCss").toString();
            EXPECT_FALSE(pattern.isEmpty())
                << "分栏 " << index << " 的 " << name.toStdString() << " 没带自己的底色模板";
            const QString resolved = Theme::cssWithPalette(pattern);
            EXPECT_TRUE(resolved.contains(isCard ? pal.panel.name(QColor::HexRgb)
                                                 : pal.panelHover.name(QColor::HexRgb)))
                << "分栏 " << index << " 的 " << name.toStdString() << " 解出来 "
                << resolved.left(60).toStdString();
            ++blocks;
            ++checkedHere;
        }
        // 规则栏 5 张卡 + 5 块内块；DNS-TUN 栏按 Rust 是一张卡，里面三块内块。
        EXPECT_GE(checkedHere, index == int(ProxyPage::Panel::Network) ? 4 : 8)
            << "分栏 " << index << " 参与审计的块太少";
    }
    EXPECT_GE(blocks, 12);
    RecordProperty("blocksCarryingBackground", blocks);
    delete page;
}

/// 全控制台的输入控件必须每一个都自带主题模板（下拉框黑块、输入框"很奇怪"、勾选/单选
/// 状态怪，根因都是"应用级 QSS 铺不进卡片里的控件"，所以只能逐个自带）。
TEST(ProxyPageAudit, everyInputControlCarriesItsOwnTemplate)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    auto *page = new ProxyPage(*context);
    page->resize(1180, 900);
    page->show();
    QCoreApplication::processEvents();

    int checked = 0;
    QStringList offenders;
    const QList<QWidget *> widgets = page->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const bool isField = qobject_cast<QComboBox *>(widget) != nullptr
            || qobject_cast<QLineEdit *>(widget) != nullptr
            || qobject_cast<QSpinBox *>(widget) != nullptr
            || qobject_cast<QCheckBox *>(widget) != nullptr
            || qobject_cast<QRadioButton *>(widget) != nullptr;
        if (!isField) {
            continue;
        }
        // 旋钮/下拉框内部那个 QLineEdit 是 Qt 自己造的子件，外观随父件走：它**必须没有**
        // 自己的模板，否则端口框会变成"外一层框 + 内一层框"（真出过这个 bug）。
        if (qobject_cast<QLineEdit *>(widget) != nullptr
            && (qobject_cast<QSpinBox *>(widget->parentWidget()) != nullptr
                || qobject_cast<QComboBox *>(widget->parentWidget()) != nullptr)) {
            EXPECT_TRUE(widget->property("paletteCss").toString().isEmpty())
                << "旋钮/下拉框的内嵌输入框被单独装饰了，会画成两层框";
            continue;
        }
        ++checked;
        if (widget->property("paletteCss").toString().isEmpty()) {
            offenders << QStringLiteral("%1(%2)")
                             .arg(QString::fromUtf8(widget->metaObject()->className()),
                                  widget->objectName());
        }
    }

    page->selectPanel(ProxyPage::Panel::Network);
    QCoreApplication::processEvents();
    std::cerr << "AUDIT 输入控件 " << checked << " 个，未自带模板 " << offenders.size() << " 个"
              << std::endl;
    EXPECT_GE(checked, 14);
    EXPECT_TRUE(offenders.isEmpty()) << offenders.join(QStringLiteral(" ; ")).toStdString();

    delete page;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
TEST(ProxyPageAudit, decoratedCardsActuallyPaintTheirBackground)
{
    QScrollArea scroll;
    auto *content = new QWidget;
    auto *contentBox = new QVBoxLayout(content);
    // 证据块：只挂 objectName + WA_StyledBackground、完全靠应用级 QFrame#Card 来铺底。
    // 实测它铺不出来（量到的是页面灰），这就是"每块区域自带一份底色"必须存在的原因，
    // 也是上一轮颜色清理把 12 份自带底色删掉之后整片区域变透的根因。只打印不断言：
    // 真要哪天应用级规则生效了，这块不该反过来把测试弄红。
    auto *bare = new QFrame(content);
    bare->setObjectName(QStringLiteral("Card"));
    bare->setAttribute(Qt::WA_StyledBackground, true);
    auto *bareBox = new QVBoxLayout(bare);
    bareBox->addWidget(new QLabel(QStringLiteral("app-level rule only"), bare));
    contentBox->addWidget(bare);

    auto *plain = new QFrame(content);
    Theme::decorateCard(plain);
    auto *plainBox = new QVBoxLayout(plain);
    plainBox->addWidget(new QLabel(QStringLiteral("only app-level rule"), plain));
    auto *owned = new QFrame(content);
    Theme::decorateCard(owned);
    owned->setStyleSheet(QStringLiteral("QFrame#Card { background-color: #ffffff; border: 1px solid"
                                        " #cbd5e1; border-radius: 12px; }"));
    auto *ownedBox = new QVBoxLayout(owned);
    ownedBox->addWidget(new QLabel(QStringLiteral("with own copy"), owned));
    contentBox->addWidget(plain);
    contentBox->addWidget(owned);
    scroll.setWidget(content);
    scroll.resize(420, 260);
    scroll.show();
    QCoreApplication::processEvents();

    const auto ownFill = [](QWidget *widget) {
        const QImage image = widget->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        // 顶部内边距那一行（y=3）只有这块自己的底。
        return QColor(image.pixel(image.width() / 2, 3)).name(QColor::HexRgb);
    };
    const QString plainColor = ownFill(plain);
    const QString ownedColor = ownFill(owned);
    std::cerr << "AUDIT bare-card-no-own-css " << ownFill(bare).toStdString()
              << "  decorated-card " << plainColor.toStdString() << "  hard-coded-card "
              << ownedColor.toStdString() << "  (后两个必须是 #ffffff，第一个不是)" << std::endl;
    EXPECT_EQ(plainColor.toStdString(), std::string("#ffffff"))
        << "卡片没铺上自己的底：只靠应用级 QFrame#Card 是不生效的";
    EXPECT_EQ(ownedColor.toStdString(), std::string("#ffffff"));

    // 内块同理：量它自己顶部内边距那一行。
    auto *inner = new QFrame(content);
    Theme::decorateInnerBlock(inner);
    auto *innerBox = new QVBoxLayout(inner);
    innerBox->addWidget(new QLabel(QStringLiteral("inner"), inner));
    contentBox->addWidget(inner);
    scroll.show();
    QCoreApplication::processEvents();
    const QString innerColor = ownFill(inner);
    EXPECT_EQ(innerColor.toStdString(), Theme::current().panelHover.name(QColor::HexRgb).toStdString())
        << "内块没铺上自己的底: " << innerColor.toStdString();
}

/// 反复建/销控制台页并 show：这一轮的守卫整轮跑时会崩在第 8 个页面上，先把它单独钉住（异步回调绑住页面而不是上下文的那类 UAF 就是这么暴露的）。
TEST(ProxyPageAudit, repeatedPageShowCycle)
{
    for (int round = 0; round < 12; ++round) {
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        auto context = makeContext(directory.path());
        ASSERT_TRUE(context != nullptr);
        auto *page = new ProxyPage(*context);
        page->resize(1180, 900);
        page->show();
        QCoreApplication::processEvents();
        page->selectPanel(ProxyPage::Panel::Network);
        QCoreApplication::processEvents();
        std::cerr << "ROUND " << round << " shown ok" << std::endl;
        delete page;
        QCoreApplication::processEvents();
    }
}

/// 六个分栏里的第一张卡，左右边缘必须和顶部状态卡对齐（#33）。
/// 规则：左边缘必须精确相同；卡片宽度不得超过顶卡（超过就说明分栏横向溢出，会冒出
/// 横向滚动条、把卡片右边缘顶到顶卡之外——分流规则栏曾经用三列 QGridLayout 排 6 个
/// 长文案预设按钮，最小宽度被撑到 1561px，而 Rust 那边是 `horizontal_wrapped`）；
/// 允许的唯一缩进是竖向滚动条的宽度（egui 的 ScrollArea 同样为滚动条留出 bar_width，
/// 见 scroll_area.rs 里 outer_rect = inner_rect.size() + current_bar_use）。
TEST(ProxyPagePanels, panelCardsAlignWithStatusCard)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new ProxyPage(*context);
    page->resize(1180, 900);
    page->show();
    QCoreApplication::processEvents();

    QWidget *statusCard = page->layout()->itemAt(0)->widget();
    ASSERT_TRUE(statusCard != nullptr);
    auto *stack = page->findChild<QStackedWidget *>();
    ASSERT_TRUE(stack != nullptr);
    const QPoint statusOrigin = statusCard->mapToGlobal(QPoint(0, 0));
    const int statusLeft = statusOrigin.x();
    const int statusRight = statusOrigin.x() + statusCard->width();

    for (int panel = 0; panel < stack->count(); ++panel) {
        QWidget *panelWidget = stack->widget(panel);
        ASSERT_TRUE(panelWidget != nullptr);
        // 堆叠控件里只有当前页是可见的，不切过去量不到。
        stack->setCurrentIndex(panel);
        QCoreApplication::processEvents();
        // 比的是"卡片"这个层级，不是分栏里的第一个子控件（有的分栏第一个是筛选条）。
        QFrame *firstCard = nullptr;
        const QList<QFrame *> frames = panelWidget->findChildren<QFrame *>();
        for (QFrame *frame : frames) {
            if (frame->objectName() == QLatin1String("Card") && frame->isVisible()) {
                firstCard = frame;
                break;
            }
        }
        if (firstCard == nullptr) {
            continue;   // 这个分栏当前没有卡（比如日志页），不参与对齐判定
        }
        const QPoint origin = firstCard->mapToGlobal(QPoint(0, 0));
        const int right = origin.x() + firstCard->width();

        QScrollArea *area = nullptr;
        for (QWidget *w = firstCard->parentWidget(); w != nullptr && area == nullptr;
             w = w->parentWidget()) {
            area = qobject_cast<QScrollArea *>(w);
        }
        const int inset = area != nullptr && area->verticalScrollBar()->isVisible()
            ? area->verticalScrollBar()->width() : 0;

        EXPECT_EQ(origin.x(), statusLeft) << "分栏 " << panel << " 左边缘";
        EXPECT_LE(firstCard->width(), statusCard->width())
            << "分栏 " << panel << " 卡片比顶卡宽 " << firstCard->width() - statusCard->width()
            << "px：分栏横向溢出，卡片右边缘冲出去了";
        EXPECT_GE(right, statusRight - inset)
            << "分栏 " << panel << " 卡片右边缘 " << right << " 比顶卡 " << statusRight
            << " 少 " << statusRight - right << "px（竖向滚动条占 " << inset << "px）";
    }
    delete page;
    QCoreApplication::processEvents();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
