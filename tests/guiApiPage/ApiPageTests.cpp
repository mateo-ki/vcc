#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

#include "api/ApiServerController.h"
#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/ApiPage.h"
#include "gui/Theme.h"
#include "models/Settings.h"

using namespace Gui;

namespace {

std::shared_ptr<App::AppContext> makeContext(const QString &directory)
{
    App::AppContext::Options options;
    options.dbPath = QDir(directory).filePath(QStringLiteral("vpn-parser.db"));
    options.listenAddr = QStringLiteral("127.0.0.1:18080");
    options.apiToken = QStringLiteral("test-token-xyz");
    const Result<std::shared_ptr<App::AppContext>> created = App::AppContext::create(options);
    if (created.isErr()) {
        ADD_FAILURE() << created.error().toStdString();
        return nullptr;
    }
    return created.value();
}

} // namespace

TEST(ApiPageTests, InitialStateAndEndpoints)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new ApiPage(*context);
    page->resize(900, 700);

    // Verify token matches context
    EXPECT_EQ(context->apiServer().listenAddr(), QStringLiteral("127.0.0.1:18080"));
    EXPECT_TRUE(context->apiServer().token().has_value());
    EXPECT_EQ(*context->apiServer().token(), QStringLiteral("test-token-xyz"));

    // Server should initially not be running
    EXPECT_FALSE(context->apiServer().isRunning());

    // Verify endpoint items exist
    auto items = page->findChildren<QFrame *>(QStringLiteral("EndpointItem"));
    EXPECT_EQ(items.size(), 26);

    delete page;
}

TEST(ApiPageTests, PublisherConfigSaveAndClear)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new ApiPage(*context);
    page->resize(900, 700);

    auto *addrEdit = page->findChild<QLineEdit *>(QStringLiteral("PublisherApiEdit"));
    ASSERT_TRUE(addrEdit != nullptr);
    auto *tokenEdit = page->findChild<QLineEdit *>(QStringLiteral("PublisherTokenEdit"));
    ASSERT_TRUE(tokenEdit != nullptr);

    addrEdit->setText(QStringLiteral("https://my-linux-server.local/"));
    tokenEdit->setText(QStringLiteral("bearer-secret-token"));

    page->savePublisherConfig();

    EXPECT_EQ(context->settings().publisherApi, QStringLiteral("https://my-linux-server.local"));
    EXPECT_EQ(context->settings().publisherToken, QStringLiteral("bearer-secret-token"));

    page->clearPublisherConfig();

    EXPECT_TRUE(context->settings().publisherApi.isEmpty());
    EXPECT_TRUE(context->settings().publisherToken.isEmpty());

    delete page;
}

TEST(ApiPageTests, StartAndStopServer)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new ApiPage(*context);
    page->resize(900, 700);

    EXPECT_FALSE(context->apiServer().isRunning());

    // Call start
    page->startApiServer();

    // Process events to let runAsync finish
    for (int i = 0; i < 50; ++i) {
        QApplication::processEvents();
        QThread::msleep(10);
    }

    EXPECT_TRUE(context->apiServer().isRunning());

    // Call stop
    page->stopApiServer();

    for (int i = 0; i < 50; ++i) {
        QApplication::processEvents();
        QThread::msleep(10);
    }

    EXPECT_FALSE(context->apiServer().isRunning());

    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
