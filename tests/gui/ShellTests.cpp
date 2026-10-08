#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QFrame>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "app/MainWindow.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "gui/Theme.h"
#include "gui/UiText.h"

namespace {

/// 离屏跑：这台机器上 mihomo 内核可能是别的实例在用的，而 main() 会先清孤儿内核，
/// 所以这里绕开 main()，只验证窗口结构与主题装配。
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

}

TEST(GuiShell, windowMatchesRustNativeOptions)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    EXPECT_EQ(window.windowTitle().toStdString(), std::string("VPN Parser"));
    // eframe NativeOptions：inner 1330x800，min 960x600。
    EXPECT_EQ(window.width(), 1330);
    EXPECT_EQ(window.height(), 800);
    EXPECT_EQ(window.minimumWidth(), 960);
    EXPECT_EQ(window.minimumHeight(), 600);
}

TEST(GuiShell, sidebarListsFiveTabsInRustOrder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    const QList<QPushButton *> nav = window.findChildren<QPushButton *>();
    QList<QPushButton *> items;
    for (QPushButton *button : nav) {
        if (button->objectName() == QLatin1String("Nav")) {
            items.push_back(button);
        }
    }
    ASSERT_EQ(items.size(), 5);
    // 顺序抄自 views/sidebar.rs：控制台、节点池、订阅源、系统设置、外部接口。
    const QStringList expected = {Gui::UiText::navConsole(),  Gui::UiText::navNodePool(),
                                  Gui::UiText::navSubscriptions(),
                                  Gui::UiText::navSystemSettings(), Gui::UiText::navExternalApi()};
    for (int index = 0; index < items.size(); ++index) {
        EXPECT_EQ(items.at(index)->text().toStdString(), expected.at(index).toStdString());
        EXPECT_EQ(items.at(index)->property("tab").toInt(), index);
        EXPECT_TRUE(items.at(index)->isCheckable());
        EXPECT_EQ(items.at(index)->isChecked(), index == 0);
    }
}

TEST(GuiShell, selectingTabSwitchesStackAndCheckedItem)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::MainWindow window(*context);
    const QList<QPushButton *> nav = window.findChildren<QPushButton *>();
    ASSERT_GE(nav.size(), Gui::kTabCount);

    window.selectTab(Gui::Tab::Api);
    EXPECT_EQ(window.currentTab(), Gui::Tab::Api);
    int checked = 0;
    for (QPushButton *button : nav) {
        if (button->objectName() == QLatin1String("Nav") && button->isChecked()) {
            checked += 1;
        }
    }
    EXPECT_EQ(checked, 1);
    // Rust 默认进控制台，其它页都在这之后按 nav 点击切换。
    window.selectTab(Gui::Tab::Proxy);
    EXPECT_EQ(window.currentTab(), Gui::Tab::Proxy);
}

TEST(GuiShell, themeDerivesOnePalettePerAccentAndFallsBackToDefault)
{
    using namespace Gui;
    ASSERT_EQ(Theme::themes().size(), 5);
    EXPECT_EQ(Theme::kDefaultThemeId.toStdString(), std::string(Theme::themes().first().id.toStdString()));

    // 未知 id（旧主题被删过）必须回到默认那套，而不是随机挑一套。
    for (const char *legacy : {"obsidian", "latte", "midnight", "mocha", "rosepine", "linear", ""}) {
        EXPECT_EQ(Theme::palette(QString::fromLatin1(legacy)).accent.rgb(),
                  Theme::palette(Theme::kDefaultThemeId).accent.rgb())
            << legacy;
    }

    const Theme::Palette base = Theme::palette(Theme::kDefaultThemeId);
    EXPECT_TRUE(base.light);
    for (const Theme::Definition &theme : Theme::themes()) {
        const Theme::Palette pal = Theme::palette(theme.id);
        // 结构共用一份：改一套配色只改强调色。
        EXPECT_EQ(pal.pageBg.rgb(), base.pageBg.rgb()) << theme.id.toStdString();
        EXPECT_EQ(pal.panel.rgb(), base.panel.rgb()) << theme.id.toStdString();
        EXPECT_EQ(pal.textPrimary.rgb(), base.textPrimary.rgb()) << theme.id.toStdString();
        EXPECT_EQ(pal.success.rgb(), base.success.rgb()) << theme.id.toStdString();
        EXPECT_EQ(Theme::contrast(pal.onPrimary, pal.primaryBtn) >= 4.5, true)
            << theme.id.toStdString();
        // 页面上不许有红和黄：严重度靠字重与措辞，不靠颜色。
        EXPECT_LE(pal.danger.red(), 90) << theme.id.toStdString();
        EXPECT_LE(pal.danger.green(), 110) << theme.id.toStdString();
        EXPECT_LE(pal.warning.red(), 130) << theme.id.toStdString();
        EXPECT_GE(Theme::contrast(pal.panel, pal.pageBg), 1.12) << theme.id.toStdString();
        for (std::pair<const char *, QColor> slot :
             {std::pair{"success_bg", pal.successBg}, {"warning_bg", pal.warningBg},
              {"danger_bg", pal.dangerBg}}) {
            EXPECT_GE(Theme::contrast(pal.textPrimary, slot.second), 3.0)
                << theme.id.toStdString() << " " << slot.first;
        }
    }
    // 五套强调色两两得分得出来。
    for (int index = 0; index < Theme::themes().size(); ++index) {
        for (int other = index + 1; other < Theme::themes().size(); ++other) {
            const QColor a = Theme::palette(Theme::themes().at(index).id).accent;
            const QColor b = Theme::palette(Theme::themes().at(other).id).accent;
            const int delta = qAbs(a.red() - b.red()) + qAbs(a.green() - b.green())
                            + qAbs(a.blue() - b.blue());
            EXPECT_GT(delta, 60) << Theme::themes().at(index).id.toStdString() << " vs "
                                 << Theme::themes().at(other).id.toStdString();
        }
    }
}

TEST(GuiShell, applyingThemeInstallsStylesheetForWholeApp)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::Theme::apply(QStringLiteral("soft-teal"));
    EXPECT_EQ(Gui::Theme::currentId().toStdString(), std::string("soft-teal"));
    auto *app = qobject_cast<QApplication *>(QApplication::instance());
    ASSERT_TRUE(app != nullptr);
    const QString css = app->styleSheet();
    EXPECT_FALSE(css.isEmpty());
    // 淡蓝底 + 压深后的字：这两条是 Rust 那边守了两个真 bug 之后的形状。
    EXPECT_NE(css.indexOf(QStringLiteral("QFrame#Card")), -1);
    EXPECT_NE(css.indexOf(QStringLiteral("QPushButton#Primary")), -1);

    QFrame card;
    Gui::Theme::decorateCard(&card);
    EXPECT_EQ(card.objectName().toStdString(), std::string("Card"));
    EXPECT_TRUE(card.graphicsEffect() != nullptr);
    // 深色那套（现在只剩浅色，这里是防御）：不描边不投影就分层不出来。
    Gui::Theme::apply(QStringLiteral("soft-cobalt"));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
