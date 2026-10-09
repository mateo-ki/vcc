#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/SettingsPage.h"
#include "gui/Theme.h"
#include "models/Settings.h"

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

} // namespace

TEST(SettingsPageTests, initialSettingsLoaded)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SettingsPage(*context);
    page->resize(900, 600);
    std::cout << "theme: " << context->settings().uiTheme.toStdString() << std::endl;
    std::cout << "autoPrepareKernel: " << context->settings().autoPrepareKernel << std::endl;
    std::cout << "speedtestAddedSource: " << context->settings().speedtestAddedSource << std::endl;
    std::cout << "speedtestDownload: " << context->settings().speedtestDownload << std::endl;
    std::cout << "httpServerEnabled: " << context->settings().httpServerEnabled << std::endl;
    std::cout << "closeToTray: " << context->settings().closeToTray << std::endl;
    std::cout << "fetchConcurrency: " << context->settings().fetchConcurrency << std::endl;
    std::cout << "fetchTimeoutSecs: " << context->settings().fetchTimeoutSecs << std::endl;
    std::cout << "speedtestConcurrency: " << context->settings().speedtestConcurrency << std::endl;
    std::cout << "speedtestTimeoutMs: " << context->settings().speedtestTimeoutMs << std::endl;
    EXPECT_FALSE(page->isDirty());
    delete page;
}

TEST(SettingsPageTests, saveAndReloadSettings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SettingsPage(*context);
    page->resize(900, 600);

    // Patch settings directly and refresh
    Models::UpdateSettings patch;
    patch.autoFetchEnabled = true;
    patch.autoFetchIntervalMin = 45;
    auto res = context->saveSettings(patch);
    ASSERT_TRUE(res.isOk());

    page->refresh();
    EXPECT_FALSE(page->isDirty());
    EXPECT_TRUE(context->settings().autoFetchEnabled);
    EXPECT_EQ(context->settings().autoFetchIntervalMin, 45);

    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
