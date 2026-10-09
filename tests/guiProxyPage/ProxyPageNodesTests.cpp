#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/FlowLayout.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "models/Source.h"
#include "proxy/ProxyManager.h"
#include "sysproxy/Format.h"

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

/// 灌一个源 + 四个"已测通"的节点。可用节点那条查询要求 latency_ok = 1 且
/// latency_test_kind = 'google_proxy'，缺任何一个这一页就一条都取不到。
void seedNodes(App::AppContext &context)
{
    Models::CreateSource input;
    input.name = QStringLiteral("seed");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const Result<Models::Source> created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    const QString sourceId = created.value().id;
    QVector<Models::Node> nodes;
    nodes << seedNode(QStringLiteral("n-hk1"), QStringLiteral("HK-1"), QStringLiteral("1.1.1.1"),
                      QStringLiteral("HK"), qint64(90))
          << seedNode(QStringLiteral("n-hk2"), QStringLiteral("HK-2"), QStringLiteral("1.1.1.2"),
                      QStringLiteral("HK"), qint64(20))
          << seedNode(QStringLiteral("n-jp"), QStringLiteral("JP-1"), QStringLiteral("2.2.2.2"),
                      QStringLiteral("JP"), qint64(50))
          << seedNode(QStringLiteral("n-other"), QStringLiteral("VN-1"), QStringLiteral("3.3.3.3"),
                      QStringLiteral("VN"), qint64(60));
    for (Models::Node &node : nodes) {
        node.sourceId = sourceId;
    }
    const Result<int> written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk()) << written.error().toStdString();
}

ProxyPage *shownPage(std::shared_ptr<App::AppContext> &context, int rows)
{
    auto *page = new ProxyPage(*context);
    page->resize(900, 600);
    page->show();
    QElapsedTimer clock;
    clock.start();
    // 取数走 AppContext::runAsync：worker 读库、结果排队回 GUI 线程，
    // 所以要转事件循环等到行数到位，不能假设它是同步的。
    while (clock.elapsed() < 4000 && page->rowCount() != rows) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(page->rowCount(), rows) << "栅格没等到预期行数：可用节点查询没接上";
    return page;
}

QVector<QPushButton *> regionPills(ProxyPage &page)
{
    QVector<QPushButton *> out;
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String("Pill") && button->property("region").isValid()) {
            out.push_back(button);
        }
    }
    return out;
}

QPushButton *pillFor(ProxyPage &page, const QString &code)
{
    for (QPushButton *button : regionPills(page)) {
        if (button->property("region").toString() == code) {
            return button;
        }
    }
    return nullptr;
}

QPushButton *sortPill(ProxyPage &page, const QString &label)
{
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String("Pill") && !button->property("region").isValid()
            && button->text() == label) {
            return button;
        }
    }
    return nullptr;
}

}

TEST(ProxyPageNodes, loadsVerifiedNodesAndShowsEightRegionPillsInOrder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    const QVector<QPushButton *> pills = regionPills(*page);
    ASSERT_EQ(pills.size(), 8);
    const QStringList codes = page->regionPillCodes();
    const QStringList labels = {UiText::regionAll(), UiText::regionHK(),  UiText::regionJP(),
                                UiText::regionUS(),  UiText::regionSG(),  UiText::regionTW(),
                                UiText::regionKR(),  UiText::regionOther()};
    for (int index = 0; index < codes.size(); ++index) {
        EXPECT_EQ(pills.at(index)->property("region").toString(), codes.at(index));
        EXPECT_EQ(pills.at(index)->text(), labels.at(index));
    }
    EXPECT_TRUE(pills.at(0)->isChecked());
    EXPECT_EQ(page->regionFilter().toStdString(), std::string("all"));
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, pillAndSearchNarrowTheGrid)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    pillFor(*page, QStringLiteral("HK"))->click();
    EXPECT_EQ(page->rowCount(), 2);
    EXPECT_EQ(page->regionFilter().toStdString(), std::string("HK"));
    // 选中态只有一个是"地区"药丸亮着。
    int checked = 0;
    for (QPushButton *pill : regionPills(*page)) {
        checked += pill->isChecked() ? 1 : 0;
    }
    EXPECT_EQ(checked, 1);

    auto *search = page->findChild<QLineEdit *>();
    ASSERT_TRUE(search != nullptr);
    search->setText(QStringLiteral("hk-2"));
    QCoreApplication::processEvents();
    EXPECT_EQ(page->rowCount(), 1);
    search->clear();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->rowCount(), 2);
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, sortPillsToggleTheSortKey)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    EXPECT_EQ(page->sortBy(), ProxySortBy::Latency);
    sortPill(*page, UiText::sortByName())->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->sortBy(), ProxySortBy::Name);
    sortPill(*page, UiText::sortByLatency())->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page->sortBy(), ProxySortBy::Latency);
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, countLabelReportsTheLoadedTotalNotTheFilteredRows)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    const QString four = SysProxy::fillTemplate(UiText::nodeCountTotal(), {}, {QStringLiteral("4")});
    EXPECT_EQ(page->countText(), four);
    pillFor(*page, QStringLiteral("JP"))->click();
    EXPECT_EQ(page->rowCount(), 1);
    // "共 N 个节点"跟着的是手里这批，不是过滤后剩几行。
    EXPECT_EQ(page->countText(), four);
    page->hide();
    delete page;
}

TEST(ProxyPageNodes, regionRowWrapsInsteadOfBeingClipped)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context);
    ProxyPage *page = shownPage(context, 4);

    QWidget *regionRow = regionPills(*page).first()->parentWidget();
    auto *layout = dynamic_cast<FlowLayout *>(regionRow->layout());
    ASSERT_TRUE(layout != nullptr);
    const int wide = layout->heightForWidth(1400);
    const int narrow = layout->heightForWidth(240);
    // Rust 用 horizontal_wrapped 的理由：窄了不换行就是"被裁掉就等于没有"。
    EXPECT_GT(narrow, wide);

    page->resize(260, 500);
    QCoreApplication::processEvents();
    for (QPushButton *pill : regionPills(*page)) {
        EXPECT_LE(pill->geometry().right(), regionRow->width() + 2)
            << pill->text().toStdString();
    }
    page->hide();
    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
