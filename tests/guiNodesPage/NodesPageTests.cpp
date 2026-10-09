#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/NodesPage.h"
#include "gui/Theme.h"
#include "models/Node.h"
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

Models::Node seedNode(const QString &id, const QString &name, const QString &server,
                      const QString &country, std::optional<qint64> ms)
{
    Models::Node node;
    node.id = id;
    node.protocol = QStringLiteral("ss");
    node.name = name;
    node.server = server;
    node.port = 443;
    node.country = country;
    node.raw = QStringLiteral("ss://a@%1:443").arg(server);
    node.config = Json::object();
    node.latencyOk = std::optional<bool>(true);
    node.latencyMs = ms;
    node.latencyTestKind = QStringLiteral("google_proxy");
    node.createdAt = TimeUtil::OptionalUtc::now();
    node.updatedAt = TimeUtil::OptionalUtc::now();
    return node;
}

void seedSampleNodes(App::AppContext &context)
{
    Models::CreateSource input;
    input.name = QStringLiteral("test-src");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const Result<Models::Source> created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk());
    const QString sourceId = created.value().id;

    QVector<Models::Node> nodes;
    nodes << seedNode(QStringLiteral("n1"), QStringLiteral("HK Node 1"), QStringLiteral("1.1.1.1"), QStringLiteral("HK"), 100)
          << seedNode(QStringLiteral("n2"), QStringLiteral("US Node 2"), QStringLiteral("2.2.2.2"), QStringLiteral("US"), 150)
          << seedNode(QStringLiteral("n3"), QStringLiteral("JP Node 3"), QStringLiteral("3.3.3.3"), QStringLiteral("JP"), 80);
    for (auto &n : nodes) {
        n.sourceId = sourceId;
    }
    const Result<int> written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk());
}

}

TEST(NodesPageTests, initialEmptyState)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    EXPECT_EQ(page->currentPage(), 1);
    EXPECT_EQ(page->loadedNodeCount(), 0);
    EXPECT_EQ(page->totalNodeCount(), 0);
    delete page;
}

TEST(NodesPageTests, loadsSeededNodes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    seedSampleNodes(*context);

    auto direct = context->database().listNodes(std::nullopt, QString(), std::nullopt, std::nullopt, std::nullopt, 1, 50, QStringLiteral("latency"));
    ASSERT_TRUE(direct.isOk()) << direct.error().toStdString();
    ASSERT_EQ(direct.value().items.size(), 3) << "Direct listNodes items count mismatch";

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    page->show();

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000 && page->loadedNodeCount() != 3) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();

    EXPECT_EQ(page->loadedNodeCount(), 3);
    EXPECT_EQ(page->totalNodeCount(), 3);
    EXPECT_EQ(page->currentPage(), 1);

    delete page;
}

TEST(NodesPageTests, filteringByKeywordNarrowsResults)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    seedSampleNodes(*context);

    auto *page = new NodesPage(*context);
    page->resize(900, 600);
    page->show();

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000 && page->loadedNodeCount() != 3) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    auto lineEdits = page->findChildren<QLineEdit *>();
    ASSERT_GE(lineEdits.size(), 2);
    QLineEdit *keyEdit = lineEdits.at(1);
    keyEdit->setText(QStringLiteral("HK"));

    auto buttons = page->findChildren<QPushButton *>();
    for (auto *btn : buttons) {
        if (btn->text() == QStringLiteral("查询")) {
            btn->click();
            break;
        }
    }

    clock.restart();
    while (clock.elapsed() < 4000 && page->loadedNodeCount() != 1) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    EXPECT_EQ(page->loadedNodeCount(), 1);
    EXPECT_EQ(page->totalNodeCount(), 1);

    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

