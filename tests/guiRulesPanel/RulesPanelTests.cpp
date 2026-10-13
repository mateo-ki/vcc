// 分流规则栏的 1:1 断言。每条对应"内容还不完整"清单里的一项（1–8、10、11），
// 全部离屏跑：不 show() 窗口，只看数据面与可见性面（isHidden），所以不抢桌面。
//
// 中文文案一律和 UiText（tools/gen-cjk-codepoints.mjs 从 Rust 源码抠出来的码点）比，
// 测试里不手打中文 —— 手打既可能打错，又只会把实现抄一遍，证明不了任何事。
#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QPushButton>
#include <QRadioButton>
#include <QLineEdit>
#include <QTimer>
#include <QSet>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "gui/ProxyPage.h"
#include "gui/RulesPanel.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "proxy/ConfigTypes.h"

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
    page->resize(980, 720);
    page->selectPanel(ProxyPage::Panel::Rules);
    // 页面里的 1.5s 轮询抓的是 AppContext 的引用：测试结束 context 先析构、页面还活着，
    // 下一跳就是野指针（单跑绿、整套跑才 SEH 0xc0000005 就是这个原因）。
    // context 一没就先把这些定时器停下，页面本身留给进程回收。
    QObject::connect(context.get(), &QObject::destroyed, page, [page]() {
        for (QTimer *timer : page->findChildren<QTimer *>()) {
            timer->stop();
        }
    });
    return page;
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

QVector<QRadioButton *> routingModeRadios(QWidget *root)
{
    QVector<QRadioButton *> out;
    for (auto *radio : root->findChildren<QRadioButton *>()) {
        if (radio->property("routingMode").isValid()) {
            out.push_back(radio);
        }
    }
    return out;
}

QRadioButton *routingModeRadio(QWidget *root, Proxy::RoutingMode mode)
{
    for (auto *radio : routingModeRadios(root)) {
        if (radio->property("routingMode").toInt() == int(mode)) {
            return radio;
        }
    }
    return nullptr;
}

Proxy::TrafficRule rule(Proxy::RuleMatcher::Kind kind, const QString &value,
                        Proxy::RuleAction action)
{
    return Proxy::makeRule(kind, value, action);
}

Proxy::MihomoConfigOptions seeded(QVector<Proxy::TrafficRule> rules,
                                  Proxy::RoutingMode mode = Proxy::RoutingMode::Rule)
{
    Proxy::MihomoConfigOptions options;
    options.routingMode = mode;
    options.rules = std::move(rules);
    return options;
}

/// runAsync 的回调要靠事件循环落地，等到页面不再忙为止。
void waitForRulesIdle(ProxyPage &page, qint64 timeoutMs = 8000)
{
    QElapsedTimer clock;
    clock.start();
    while (page.rulesBusy() && clock.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
    }
}

}

TEST(RulesPanelTests, matcherKindLabelsAreTheRustTemplates)
{
    // 第 4 项：rule_matcher_label 的九个变体，逐个和生成出来的 Rust 模板比。
    const QString value = QStringLiteral("example.com");
    struct Case {
        Proxy::RuleMatcher matcher;
        QString expected;
    };
    using K = Proxy::RuleMatcher::Kind;
    const QVector<Case> cases = {
        {Proxy::RuleMatcher::simple(K::Domain, value), UiText::matcherLabelDomain().arg(value)},
        {Proxy::RuleMatcher::simple(K::DomainSuffix, value),
         UiText::matcherLabelDomainSuffix().arg(value)},
        {Proxy::RuleMatcher::simple(K::GeoSite, value), UiText::matcherLabelGeoSite().arg(value)},
        {Proxy::RuleMatcher::simple(K::GeoIp, value), UiText::matcherLabelGeoIp().arg(value)},
        {Proxy::RuleMatcher::simple(K::IpCidr, value), UiText::matcherLabelIpCidr().arg(value)},
        {Proxy::RuleMatcher::simple(K::DestinationPort, value),
         UiText::matcherLabelDestinationPort().arg(value)},
        {Proxy::RuleMatcher::simple(K::ProcessName, value),
         UiText::matcherLabelProcessName().arg(value)},
        {Proxy::RuleMatcher::makeNetwork(Proxy::NetworkProtocol::Tcp),
         UiText::matcherLabelNetworkTcp()},
        {Proxy::RuleMatcher::makeNetwork(Proxy::NetworkProtocol::Udp),
         UiText::matcherLabelNetworkUdp()},
        {Proxy::RuleMatcher::makeQuic(), UiText::matcherLabelQuic()},
    };
    ASSERT_EQ(cases.size(), 10);
    QSet<QString> distinct;
    for (const auto &item : cases) {
        EXPECT_EQ(ruleMatcherText(item.matcher).toStdString(), item.expected.toStdString())
            << "matcher kind=" << int(item.matcher.kind);
        distinct.insert(ruleMatcherText(item.matcher));
    }
    // 十个变体十种文案：只要有一条是写死的，这里就会掉到 9。
    EXPECT_EQ(distinct.size(), 10);

    // QUIC 是无值变体：文案里不能残留 %1 这种没喂过的占位符。
    EXPECT_EQ(ruleMatcherText(Proxy::RuleMatcher::makeQuic()).contains(QStringLiteral("%1")), false);
}

TEST(RulesPanelTests, actionLabelsFollowRustRuleActionLabel)
{
    EXPECT_EQ(ruleActionText(Proxy::RuleAction::Direct).toStdString(),
              UiText::ruleActionDirect().toStdString());
    EXPECT_EQ(ruleActionText(Proxy::RuleAction::Proxy).toStdString(),
              UiText::ruleActionProxy().toStdString());
    EXPECT_EQ(ruleActionText(Proxy::RuleAction::Reject).toStdString(),
              UiText::ruleActionReject().toStdString());
}

TEST(RulesPanelTests, draftKindComboMatchesRustRuleDraftKindAll)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    QComboBox *combo = page->findChild<QComboBox *>(QStringLiteral("NewRuleKindCombo"));
    ASSERT_TRUE(combo != nullptr) << "新增规则的下拉没按 RuleDraftKind::ALL 摆十项";
    ASSERT_EQ(combo->count(), ruleDraftAll().size());
    QStringList actual;
    for (int i = 0; i < combo->count(); ++i) {
        actual.push_back(combo->itemText(i));
    }
    QStringList expected;
    for (RuleDraftKind kind : ruleDraftAll()) {
        expected.push_back(ruleDraftLabel(kind));
    }
    EXPECT_EQ(actual, expected);

    // 默认选中 Rust 的 DomainSuffix，动作默认是「代理」。
    EXPECT_EQ(combo->currentData().toInt(), int(RuleDraftKind::DomainSuffix));
    QComboBox *actionCombo = page->findChild<QComboBox *>(QStringLiteral("NewRuleActionCombo"));
    ASSERT_TRUE(actionCombo != nullptr);
    ASSERT_EQ(actionCombo->count(), 3);
    EXPECT_EQ(actionCombo->currentData().toInt(), int(Proxy::RuleAction::Proxy));

    delete page;
}

TEST(RulesPanelTests, routingModeDrivesSubtitleDetailAndSectionVisibility)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    // 第 1 项 + 第 2 项：副标题 = routing_mode_description，详情 = routing_mode_detail，
    // 且非「自定义规则」时预设卡片与规则清单整段收起。
    struct Case {
        Proxy::RoutingMode mode;
        QString subtitle;
        QString detail;
        bool customVisible;
    };
    const QVector<Case> cases = {
        {Proxy::RoutingMode::Rule, UiText::routingModeDescRule(), UiText::routingModeDetailRule(),
         true},
        {Proxy::RoutingMode::ChinaDirect, UiText::routingModeDescChinaDirect(),
         UiText::routingModeDetailChinaDirect(), false},
        {Proxy::RoutingMode::GfwList, UiText::routingModeDescGfwList(),
         UiText::routingModeDetailGfwList(), false},
        {Proxy::RoutingMode::Global, UiText::routingModeDescGlobal(),
         UiText::routingModeDetailGlobal(), false},
        {Proxy::RoutingMode::Direct, UiText::routingModeDescDirect(),
         UiText::routingModeDetailDirect(), false},
    };

    QSet<QString> subtitles;
    QSet<QString> details;
    for (const auto &item : cases) {
        QRadioButton *radio = routingModeRadio(page, item.mode);
        ASSERT_TRUE(radio != nullptr) << "模式单选缺了一个";
        radio->click();
        QCoreApplication::processEvents();

        EXPECT_EQ(page->routingModeSubtitleText().toStdString(), item.subtitle.toStdString())
            << "mode=" << int(item.mode);
        EXPECT_EQ(page->routingModeDetailText().toStdString(), item.detail.toStdString())
            << "mode=" << int(item.mode);
        EXPECT_EQ(page->customRulesVisible(), item.customVisible) << "mode=" << int(item.mode);
        subtitles.insert(page->routingModeSubtitleText());
        details.insert(page->routingModeDetailText());
    }
    // 五条各不相同：只要有一条是写死的，这里就会掉到 4。
    EXPECT_EQ(subtitles.size(), 5);
    EXPECT_EQ(details.size(), 5);

    // 收起的是整段：预设按钮和规则表都必须住在那个容器里，不然藏不到它们。
    routingModeRadio(page, Proxy::RoutingMode::Global)->click();
    QCoreApplication::processEvents();
    QWidget *section = page->findChild<QWidget *>(QStringLiteral("CustomRulesSection"));
    ASSERT_TRUE(section != nullptr);
    for (const char *name : {"PresetAiButton", "SaveRulesButton"}) {
        QPushButton *button = findButton(page, QString::fromLatin1(name));
        ASSERT_TRUE(button != nullptr) << name;
        bool inside = false;
        for (QWidget *w = button; w != nullptr; w = w->parentWidget()) {
            if (w == section) {
                inside = true;
            }
        }
        EXPECT_TRUE(inside) << name << " 不在收起的那一段里";
    }

    routingModeRadio(page, Proxy::RoutingMode::Rule)->click();
    QCoreApplication::processEvents();
    EXPECT_TRUE(page->customRulesVisible());

    delete page;
}

TEST(RulesPanelTests, busyNoticesShowAndDisableTheConfigWidgets)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    // 第 3 项：Rust 的 if !editable 两支文案，卡片内容同时禁用。
    EXPECT_TRUE(page->configBusyNoticeText().isEmpty());
    EXPECT_TRUE(page->rulesBusyNoticeText().isEmpty());

    page->setRulesBusy(true);
    EXPECT_EQ(page->configBusyNoticeText().toStdString(), UiText::busyProcessingConfig().toStdString());
    EXPECT_EQ(page->rulesBusyNoticeText().toStdString(), UiText::busyProcessingRules().toStdString());
    QPushButton *saveMode = findButton(page, QStringLiteral("SaveModeButton"));
    QPushButton *preset = findButton(page, QStringLiteral("PresetAiButton"));
    ASSERT_TRUE(saveMode != nullptr);
    ASSERT_TRUE(preset != nullptr);
    EXPECT_FALSE(saveMode->isEnabled());
    EXPECT_FALSE(preset->isEnabled());

    page->setRulesBusy(false);
    EXPECT_TRUE(page->configBusyNoticeText().isEmpty());
    EXPECT_TRUE(page->rulesBusyNoticeText().isEmpty());
    EXPECT_TRUE(saveMode->isEnabled());
    EXPECT_TRUE(preset->isEnabled());

    delete page;
}

TEST(RulesPanelTests, rulesTableShowsMatcherActionAndMoveDeleteAffordances)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    const QString domain = QStringLiteral("example.com");
    page->setRulesConfigForTest(seeded({
        rule(Proxy::RuleMatcher::Kind::DomainSuffix, domain, Proxy::RuleAction::Proxy),
        rule(Proxy::RuleMatcher::Kind::GeoIp, QStringLiteral("CN"), Proxy::RuleAction::Direct),
    }));
    ASSERT_EQ(page->ruleRowCount(), 2);

    // 第 4 项：第一列是 rule_matcher_label，第二列是 rule_action_label。
    EXPECT_EQ(page->ruleRowMatcherText(0).toStdString(),
              UiText::matcherLabelDomainSuffix().arg(domain).toStdString());
    EXPECT_EQ(page->ruleRowMatcherText(1).toStdString(),
              UiText::matcherLabelGeoIp().arg(QStringLiteral("CN")).toStdString());
    EXPECT_EQ(page->ruleRowActionText(0).toStdString(),
              UiText::ruleActionProxy().toStdString());
    EXPECT_EQ(page->ruleRowActionText(1).toStdString(),
              UiText::ruleActionDirect().toStdString());

    // ↑ / ↓ / × 的 hover 文案与可用边界（首行不能上移，末行不能下移）。
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("RulesTable"));
    ASSERT_TRUE(table != nullptr);
    auto rowButtons = [table](int row) {
        QVector<QPushButton *> out;
        if (auto *moves = table->cellWidget(row, 2)) {
            out << moves->findChildren<QPushButton *>();
        }
        if (auto *del = qobject_cast<QPushButton *>(table->cellWidget(row, 3))) {
            out.push_back(del);
        }
        return out;
    };
    for (int row = 0; row < 2; ++row) {
        const QVector<QPushButton *> buttons = rowButtons(row);
        ASSERT_EQ(buttons.size(), 3) << "row=" << row;
        QPushButton *up = buttons[0];
        QPushButton *down = buttons[1];
        QPushButton *remove = buttons[2];
        EXPECT_EQ(up->toolTip().toStdString(), UiText::ruleMoveUp().toStdString());
        EXPECT_EQ(down->toolTip().toStdString(), UiText::ruleMoveDown().toStdString());
        EXPECT_EQ(remove->toolTip().toStdString(), UiText::ruleRemove().toStdString());
        EXPECT_EQ(up->isEnabled(), row > 0);
        EXPECT_EQ(down->isEnabled(), row + 1 < 2);
        // 按钮文字是 ↑ ↓ × 三个符号，直接从生成的码点比。
        EXPECT_EQ(up->text(), UiText::ruleMoveUpGlyph());
        EXPECT_EQ(down->text(), UiText::ruleMoveDownGlyph());
        EXPECT_EQ(remove->text(), UiText::ruleRemoveGlyph());
    }

    // 行内改动作要真的写回工作副本（Rust 的 selectable_value 直接改 rules[index].action）。
    // 动作格现在是"单元格 + 委托"：常驻的是文本，编辑器只在点开时才现造（见
    // RuleActionDelegate），所以这里按委托的编辑链路走一遍，而不摸行里的控件。
    const QModelIndex actionIndex = table->model()->index(0, 1);
    ASSERT_TRUE(actionIndex.isValid());
    ASSERT_TRUE(actionIndex.flags().testFlag(Qt::ItemIsEditable)) << "动作格必须可编辑";
    auto *delegate = table->itemDelegateForColumn(1);
    ASSERT_TRUE(delegate != nullptr);
    QWidget *editor = delegate->createEditor(table, QStyleOptionViewItem(), actionIndex);
    ASSERT_TRUE(editor != nullptr);
    auto *editorCombo = qobject_cast<QComboBox *>(editor);
    ASSERT_TRUE(editorCombo != nullptr);
    // 三项和文案要和 Rust 的 DIRECT/PROXY/REJECT 一致，且初值取自这一行。
    ASSERT_EQ(editorCombo->count(), 3);
    EXPECT_EQ(editorCombo->itemText(int(Proxy::RuleAction::Direct)).toStdString(),
              UiText::ruleActionDirect().toStdString());
    delegate->setEditorData(editor, actionIndex);
    editorCombo->setCurrentIndex(int(Proxy::RuleAction::Reject));
    delegate->setModelData(editor, table->model(), actionIndex);
    EXPECT_EQ(page->ruleRowActionText(0).toStdString(),
              UiText::ruleActionReject().toStdString());
    delete editor;

    delete page;
}

TEST(RulesPanelTests, processNameRuleWarnsOutsideTunMode)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);

    // 第 5 项：PROCESS-NAME 只有 TUN 拿得到进程名，系统代理下当场提醒。
    Proxy::MihomoConfigOptions options = seeded({});
    options.mode = Proxy::ProxyMode::System;
    page->setRulesConfigForTest(options);

    QComboBox *kindCombo = page->findChild<QComboBox *>(QStringLiteral("NewRuleKindCombo"));
    ASSERT_TRUE(kindCombo != nullptr);

    const int processIndex = ruleDraftAll().indexOf(RuleDraftKind::ProcessName);
    const int domainIndex = ruleDraftAll().indexOf(RuleDraftKind::Domain);
    kindCombo->setCurrentIndex(processIndex);
    QCoreApplication::processEvents();
    EXPECT_EQ(page->processNameWarningText().toStdString(), UiText::processNameNeedsTun().toStdString());

    // TCP 没有匹配值：输入框得跟着收起（Rust 的 needs_value）。
    const int tcpIndex = ruleDraftAll().indexOf(RuleDraftKind::Tcp);
    kindCombo->setCurrentIndex(tcpIndex);
    QCoreApplication::processEvents();
    EXPECT_TRUE(page->processNameWarningText().isEmpty());
    // TCP 没有匹配值：输入框要跟着收起（Rust 的 needs_value）。
    QLineEdit *valueEdit = page->findChild<QLineEdit *>(QStringLiteral("NewRuleValueEdit"));
    ASSERT_TRUE(valueEdit != nullptr);
    EXPECT_TRUE(valueEdit->isHidden());
    kindCombo->setCurrentIndex(domainIndex);
    QCoreApplication::processEvents();
    EXPECT_FALSE(valueEdit->isHidden());

    kindCombo->setCurrentIndex(domainIndex);
    QCoreApplication::processEvents();
    EXPECT_TRUE(page->processNameWarningText().isEmpty());

    // 切到 TUN 模式后，同一个 PROCESS-NAME 就不该再提示。
    options.mode = Proxy::ProxyMode::Tun;
    page->setRulesConfigForTest(options);
    kindCombo->setCurrentIndex(processIndex);
    QCoreApplication::processEvents();
    EXPECT_TRUE(page->processNameWarningText().isEmpty());

    delete page;
}

TEST(RulesPanelTests, invalidNewRuleReportsRuleInvalidPrefix)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->setRulesConfigForTest(seeded({}));

    // 第 6 项：值交给 toMihomoRule 判，失败时状态条是 "规则无效: {错误}"，规则不入列。
    QStringList messages;
    QObject::connect(page, &ProxyPage::statusMessageRequested, page,
                     [&messages](const QString &text) { messages.push_back(text); });

    QComboBox *kindCombo = page->findChild<QComboBox *>(QStringLiteral("NewRuleKindCombo"));
    ASSERT_TRUE(kindCombo != nullptr);
    kindCombo->setCurrentIndex(ruleDraftAll().indexOf(RuleDraftKind::Domain));

    QLineEdit *valueEdit = page->findChild<QLineEdit *>(QStringLiteral("NewRuleValueEdit"));
    ASSERT_TRUE(valueEdit != nullptr);
    valueEdit->clear();

    QPushButton *addButton = findButton(page, QStringLiteral("AddRuleButton"));
    ASSERT_TRUE(addButton != nullptr);
    addButton->click();
    QCoreApplication::processEvents();

    const Proxy::TrafficRule empty{
        Proxy::RuleMatcher::simple(Proxy::RuleMatcher::Kind::Domain, QString()),
        Proxy::RuleAction::Proxy};
    const QString expected = UiText::ruleInvalidPrefix().arg(empty.toMihomoRule().error());
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().toStdString(), expected.toStdString());
    EXPECT_EQ(page->ruleRowCount(), 0) << "无效规则不能进列表";

    // 补上合法值同一条规则就该收下。
    valueEdit->setText(QStringLiteral("example.com"));
    addButton->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->ruleRowCount(), 1);
    EXPECT_EQ(page->ruleRowMatcherText(0).toStdString(),
              UiText::matcherLabelDomain().arg(QStringLiteral("example.com")).toStdString());

    delete page;
}

TEST(RulesPanelTests, recommendedRulesGoThroughTheConfirmModal)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->setRulesConfigForTest(seeded({
        rule(Proxy::RuleMatcher::Kind::Domain, QStringLiteral("keep.example"),
             Proxy::RuleAction::Direct),
    }));
    const int before = page->ruleRowCount();
    ASSERT_EQ(before, 1);

    // 第 7 项：推荐分流是确认弹窗，取消不动规则，确认才覆盖并立刻保存。
    QPushButton *recommended = findButton(page, QStringLiteral("RecommendedRulesButton"));
    ASSERT_TRUE(recommended != nullptr);
    recommended->click();
    QCoreApplication::processEvents();

    QDialog *dialog = page->findChild<QDialog *>(QStringLiteral("RecommendedRulesConfirm"));
    ASSERT_TRUE(dialog != nullptr) << "点推荐分流没弹确认框";
    QStringList dialogTexts;
    for (auto *label : dialog->findChildren<QLabel *>()) {
        dialogTexts.push_back(label->text());
    }
    EXPECT_TRUE(dialogTexts.contains(UiText::overrideRulesTitle()));
    EXPECT_TRUE(dialogTexts.contains(UiText::overrideRulesBody()));
    QPushButton *cancel = nullptr;
    QPushButton *confirm = nullptr;
    for (auto *button : dialog->findChildren<QPushButton *>()) {
        if (button->text() == UiText::confirmCancel()) {
            cancel = button;
        } else if (button->text() == UiText::confirmOverride()) {
            confirm = button;
        }
    }
    ASSERT_TRUE(cancel != nullptr);
    ASSERT_TRUE(confirm != nullptr);

    cancel->click();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(page->findChild<QDialog *>(QStringLiteral("RecommendedRulesConfirm")) == nullptr);
    EXPECT_EQ(page->ruleRowCount(), before) << "取消不能改规则";

    recommended->click();
    QCoreApplication::processEvents();
    dialog = page->findChild<QDialog *>(QStringLiteral("RecommendedRulesConfirm"));
    ASSERT_TRUE(dialog != nullptr);
    for (auto *button : dialog->findChildren<QPushButton *>()) {
        if (button->text() == UiText::confirmOverride()) {
            button->click();
        }
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(page->ruleRowCount(), Proxy::recommendedTrafficRules().size()) << "确认后要换成推荐规则";
    waitForRulesIdle(*page);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_FALSE(page->rulesBusy()) << "确认后的保存在飞太久";

    delete page;
}

TEST(RulesPanelTests, autoOptimizeReportsRustWordings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    const QString suffix = QStringLiteral("example.com");
    page->setRulesConfigForTest(seeded({
        rule(Proxy::RuleMatcher::Kind::DomainSuffix, suffix, Proxy::RuleAction::Proxy),
        rule(Proxy::RuleMatcher::Kind::DomainSuffix, suffix, Proxy::RuleAction::Proxy),
        rule(Proxy::RuleMatcher::Kind::Domain, QStringLiteral("solo.example"),
             Proxy::RuleAction::Direct),
    }));
    const int before = page->ruleRowCount();
    ASSERT_EQ(before, 3);

    // 第 8 项：两条状态文案照 Rust 原文，条数按实际移除量填。
    QStringList messages;
    QObject::connect(page, &ProxyPage::statusMessageRequested, page,
                     [&messages](const QString &text) { messages.push_back(text); });

    QPushButton *optimize = findButton(page, QStringLiteral("OptimizeRulesButton"));
    ASSERT_TRUE(optimize != nullptr);
    optimize->click();
    QCoreApplication::processEvents();

    const int after = page->ruleRowCount();
    ASSERT_LT(after, before);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().toStdString(),
              UiText::optimizeRemovedN().arg(before - after).toStdString());

    messages.clear();
    optimize->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->ruleRowCount(), after);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().toStdString(), UiText::optimizeAlreadyBest().toStdString());

    delete page;
}

TEST(RulesPanelTests, injectedOverrideNoticeIsComputedNotTyped)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->setRulesConfigForTest(seeded({}));

    // 第 10 项：内置注入那行必须由 mihomoDnsPoisonOverrides() 现算，不是手写的示例。
    QStringList rendered;
    for (const auto &item : Proxy::mihomoDnsPoisonOverrides()) {
        const auto text = item.toMihomoRule();
        if (text.isOk()) {
            rendered.append(text.value());
        }
    }
    ASSERT_FALSE(rendered.isEmpty());
    const QString expected = UiText::injectionLine().arg(rendered.join(UiText::injectionJoinSep()));
    EXPECT_EQ(page->injectedNoticeText().toStdString(), expected.toStdString());
    // 生成的旧文案里那句 "YouTube · Google · Telegram · Twitter" 是编的，不能再出现。
    EXPECT_EQ(page->injectedNoticeText().contains(QStringLiteral("Twitter")), false);

    delete page;
}

TEST(RulesPanelTests, presetButtonsCarryTheSixRustTooltips)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    ProxyPage *page = makePage(context);
    page->setRulesConfigForTest(seeded({}));

    // 第 11 项：六个预设按钮各带一条 hover 说明。
    const QVector<QPair<QString, QString>> cases = {
        {QStringLiteral("PresetAiButton"), UiText::presetAiHover()},
        {QStringLiteral("PresetMediaButton"), UiText::presetMediaHover()},
        {QStringLiteral("PresetSocialButton"), UiText::presetSocialHover()},
        {QStringLiteral("PresetDevButton"), UiText::presetDevHover()},
        {QStringLiteral("PresetGamesButton"), UiText::presetGameHover()},
        {QStringLiteral("PresetAdButton"), UiText::presetAdHover()},
    };
    for (const auto &item : cases) {
        QPushButton *button = findButton(page, item.first);
        ASSERT_TRUE(button != nullptr) << item.first.toLatin1().data();
        EXPECT_FALSE(item.second.isEmpty());
        EXPECT_EQ(button->toolTip().toStdString(), item.second.toStdString())
            << item.first.toLatin1().data();
    }

    // 点一个预设要真的并进列表（Rust 是去重后插到最前，然后立刻保存）。
    const int before = page->ruleRowCount();
    QPushButton *ai = findButton(page, QStringLiteral("PresetAiButton"));
    ASSERT_TRUE(ai != nullptr);
    ai->click();
    QCoreApplication::processEvents();
    EXPECT_GT(page->ruleRowCount(), before) << "点了预设却没加规则";
    waitForRulesIdle(*page);

    delete page;
}

int main(int argc, char *argv[])
{
    // 下拉与卡片的外观都来自应用级样式表；页面本身只认数据面，但离屏跑还是要有
    // QApplication，并且主题得先落一次，免得量到"根本没有 QSS"的状态。
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
