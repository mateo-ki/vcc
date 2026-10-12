#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/SourcesPage.h"
#include "gui/Theme.h"
#include "models/Source.h"
#include "parser/Common.h"

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

/// 操作列（一个 QWidget 容器套若干按钮）里有没有某个文案的按钮。
bool hasButton(QWidget *cell, const QString &text)
{
    if (cell == nullptr) return false;
    for (auto *button : cell->findChildren<QPushButton *>()) {
        if (button->text() == text) return true;
    }
    return false;
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

/// 带日期占位符的订阅模板（`{Y}/{mm}/2-{Ymd}`）也要能起出像样的建议名：
/// 原串直接喂 QUrl 解析不出 host（实测为空），必须先按拉取时同一套规则展开。
TEST(SourcesPageTests, suggestedNameHandlesDatePlaceholders)
{
    EXPECT_EQ(Gui::SourcesPage::hostForSuggestedName(
                  QStringLiteral("https://sub.example.com/{Y}/{mm}/2-{Ymd}?token=x"))
                  .toStdString(),
              std::string("sub.example.com"));
    EXPECT_EQ(Gui::SourcesPage::hostForSuggestedName(QStringLiteral("https://www.x.example/a"))
                  .toStdString(),
              std::string("x.example"));
    EXPECT_EQ(Gui::SourcesPage::hostForSuggestedName(QStringLiteral("not a url at all")).toStdString(),
              std::string());
}

/// "URL 模板 ?" 那段提示必须和展开器真的认的写法严格一致：提示里写了而展开器不认，用户
/// 照着填就是静默 404；展开器认而提示没写，等于白少一个入口。两个方向都算不一致。
TEST(SourcesPageTests, urlTemplateHelpListsExactlyTheSupportedPlaceholders)
{
    QSet<QString> listed;
    auto matches = QRegularExpression(QStringLiteral("\\{([A-Za-z]+)\\}"))
                       .globalMatch(SourcesPage::urlTemplateHelp());
    while (matches.hasNext()) {
        listed.insert(matches.next().captured(1));
    }

    const QStringList candidates{QStringLiteral("Ymd"), QStringLiteral("ymd"),
                                 QStringLiteral("Y"),  QStringLiteral("YYYY"),
                                 QStringLiteral("y"),  QStringLiteral("yyyy"),
                                 QStringLiteral("m"),  QStringLiteral("M"),
                                 QStringLiteral("mm"), QStringLiteral("MM"),
                                 QStringLiteral("d"),  QStringLiteral("D"),
                                 QStringLiteral("dd"), QStringLiteral("DD")};
    QSet<QString> supported;
    for (const QString &token : candidates) {
        const QString template_ = QStringLiteral("https://host/{%1}/sub").arg(token);
        if (Parser::Common::expandUrlTemplate(template_) != template_) {
            supported.insert(token);
        }
    }
    ASSERT_EQ(supported.size(), candidates.size())
        << "展开器自己少认了占位符，这条守卫会跟着一起瞎";

    QStringList onlyInHelp;
    for (const QString &token : listed - supported) {
        onlyInHelp.push_back(token);
    }
    QStringList onlyInExpander;
    for (const QString &token : supported - listed) {
        onlyInExpander.push_back(token);
    }
    EXPECT_TRUE(onlyInHelp.isEmpty())
        << "提示里写了但不展开（会原样留在 URL 里）: " << onlyInHelp.join(QChar(',')).toStdString();
    EXPECT_TRUE(onlyInExpander.isEmpty())
        << "能展开但提示没写: " << onlyInExpander.join(QChar(',')).toStdString();
}

/// 拉取单源之后只重写这一行（#34）。守卫看对象指针：其它行的 item 与 cellWidget 必须还是
/// 同一批对象（== 整表没重建），而这一行的 item 必须换掉、显示内容跟着库走。
/// 反向验证：把 refreshSourceRow 换回 reloadSources，下面三条 EXPECT_EQ 指针必须变红。
TEST(SourcesPageTests, refreshSourceRowRebuildsOnlyItsOwnRow)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    const QStringList names{QStringLiteral("Alpha Source"), QStringLiteral("Beta Source"),
                            QStringLiteral("Gamma Source")};
    QString idA;
    for (int i = 0; i < names.size(); ++i) {
        Models::CreateSource input;
        input.name = names.at(i);
        input.urlTemplate = QStringLiteral("https://example.com/%1.txt").arg(i);
        input.enabled = true;
        auto created = context->database().createSource(input);
        ASSERT_TRUE(created.isOk());
        if (i == 0) {
            idA = created.value().id;
        }
    }

    auto *page = new SourcesPage(*context);
    page->refresh();

    auto *table = page->findChild<QTableWidget *>(QStringLiteral("SourcesTable"));
    ASSERT_TRUE(table != nullptr);
    ASSERT_EQ(table->rowCount(), names.size());

    int rowA = -1;
    int rowB = -1;
    for (int r = 0; r < table->rowCount(); ++r) {
        if (table->item(r, 0) == nullptr) continue;
        if (table->item(r, 0)->text() == names.at(0)) rowA = r;
        if (table->item(r, 0)->text() == names.at(1)) rowB = r;
    }
    ASSERT_GE(rowA, 0);
    ASSERT_GE(rowB, 0);
    ASSERT_NE(rowA, rowB);

    // 记下"别的行"的三个对象指针，以及 A 行当前的 item。
    QTableWidgetItem *otherName = table->item(rowB, 0);
    QWidget *otherStatus = table->cellWidget(rowB, 2);
    QWidget *otherActions = table->cellWidget(rowB, 5);
    QTableWidgetItem *ownName = table->item(rowA, 0);
    ASSERT_TRUE(otherName != nullptr);
    ASSERT_TRUE(otherStatus != nullptr);
    ASSERT_TRUE(otherActions != nullptr);
    ASSERT_TRUE(ownName != nullptr);
    // A 行此刻还是「拉取」按钮（lastStatus 为空）。
    EXPECT_FALSE(hasButton(table->cellWidget(rowA, 5), QStringLiteral("重试")));

    const auto marked =
        context->database().markSourceFetched(idA, QStringLiteral("拉取失败: 超时"), 12);
    ASSERT_TRUE(marked.isOk());

    page->refreshSourceRow(idA);

    // 其它行：对象没被换过 => 整表没有重建。
    EXPECT_EQ(table->item(rowB, 0), otherName);
    EXPECT_EQ(table->cellWidget(rowB, 2), otherStatus);
    EXPECT_EQ(table->cellWidget(rowB, 5), otherActions);

    // A 行：item 被替换，容器控件也是新的，内容跟着库走。
    EXPECT_NE(table->item(rowA, 0), ownName);
    auto *statusCell = table->cellWidget(rowA, 2);
    auto *healthCell = table->cellWidget(rowA, 4);
    ASSERT_TRUE(statusCell != nullptr);
    ASSERT_TRUE(healthCell != nullptr);
    bool sawStatus = false;
    for (auto *label : statusCell->findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("拉取失败"))) sawStatus = true;
    }
    EXPECT_TRUE(sawStatus);
    bool sawHealth = false;
    for (auto *label : healthCell->findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("测通 0/12")) sawHealth = true;
    }
    EXPECT_TRUE(sawHealth);
    EXPECT_TRUE(hasButton(table->cellWidget(rowA, 5), QStringLiteral("重试")));

    // 库里查不到的源（比如刚被删）退回整表刷新：不许崩，也不许把别的行刷错位。
    page->refreshSourceRow(QStringLiteral("no-such-source"));
    EXPECT_EQ(table->rowCount(), names.size());

    delete page;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
