#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/SourcesPage.h"
#include "gui/Theme.h"
#include "models/Source.h"

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

TEST(SourcesPageTests, initialEmptyState)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new SourcesPage(*context);
    page->resize(900, 600);
    EXPECT_EQ(page->sourceCount(), 0);
    EXPECT_EQ(page->fetchResultCount(), 0);
    delete page;
}

TEST(SourcesPageTests, loadsSeededSources)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Models::CreateSource input;
    input.name = QStringLiteral("Sample Source");
    input.urlTemplate = QStringLiteral("https://example.com/sub.txt");
    input.enabled = true;
    input.remark = QStringLiteral("Test Remark");

    auto created = context->database().createSource(input);
    ASSERT_TRUE(created.isOk());

    auto *page = new SourcesPage(*context);
    page->refresh();
    EXPECT_EQ(page->sourceCount(), 1);
    delete page;
}

TEST(SourcesPageTests, toggleEnabledAndPush)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Models::CreateSource input;
    input.name = QStringLiteral("Toggle Source");
    input.urlTemplate = QStringLiteral("https://example.com/toggle.txt");
    input.enabled = true;

    auto created = context->database().createSource(input);
    ASSERT_TRUE(created.isOk());
    const QString id = created.value().id;

    auto *page = new SourcesPage(*context);
    page->refresh();
    EXPECT_EQ(page->sourceCount(), 1);

    page->toggleSourceEnabled(id, false);
    auto s1 = context->database().getSource(id);
    ASSERT_TRUE(s1.isOk());
    EXPECT_FALSE(s1.value().enabled);

    page->toggleSourcePush(id, true);
    auto s2 = context->database().getSource(id);
    ASSERT_TRUE(s2.isOk());
    EXPECT_TRUE(s2.value().pushDisabled);
    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
