#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <memory>

#include "app/AppContext.h"
#include "core/Result.h"
#include "core/Version.h"
#include "gui/AboutDialog.h"

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

QString findButtonText(Gui::AboutDialog &dialog, const QString &text)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == text) {
            return text;
        }
    }
    return QString();
}

bool hasLabelWith(Gui::AboutDialog &dialog, const QString &fragment)
{
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        if (label->text().contains(fragment)) {
            return true;
        }
    }
    return false;
}

QPushButton *buttonByLabel(Gui::AboutDialog &dialog, const QString &label)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == label) {
            return button;
        }
    }
    return nullptr;
}

} // namespace

TEST(AboutDialog, ShowsTheBuiltInVersionAndTheRepository)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutDialog dialog(*context);
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("关于 VCC"));
    EXPECT_TRUE(hasLabelWith(dialog, Core::appVersion()))
        << "关于框里看不到当前版本，检查更新就没有对照";
    EXPECT_TRUE(hasLabelWith(dialog, Gui::AboutDialog::repositoryUrl()));
    EXPECT_TRUE(hasLabelWith(dialog, QStringLiteral("Apache-2.0")));
    EXPECT_FALSE(findButtonText(dialog, QStringLiteral("检查更新")).isEmpty());
    EXPECT_FALSE(findButtonText(dialog, QStringLiteral("打开发布页")).isEmpty());
}

TEST(AboutDialog, DownloadStaysDarkUntilThereIsSomethingToGrab)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutDialog dialog(*context);
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("打开发布页")) {
            EXPECT_FALSE(button->isEnabled()) << "还没检查过就给了一个指向未知的按钮";
        }
    }
    Core::Release release;
    release.version = QStringLiteral("1.2.0");
    release.url = QStringLiteral("https://github.com/mateo-ki/vcc/releases/tag/v1.2.0");
    release.assetName = QStringLiteral("VCC-1.2.0-win64.exe");
    release.assetUrl = QStringLiteral("https://objects.example/VCC-1.2.0-win64.exe");
    release.assetSize = 4096;
    dialog.showUpdateResult(QStringLiteral("已是最新版 1.2.0"), release);
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("打开发布页")) {
            EXPECT_TRUE(button->isEnabled());
        }
    }
}

TEST(AboutDialog, StatusTextTellsTheThreeCasesApart)
{
    EXPECT_EQ(Gui::AboutDialog::updateStatusText(QStringLiteral("v1.3.0"), QStringLiteral("1.2.0")),
              QStringLiteral("发现新版本 1.3.0（当前 1.2.0）"));
    EXPECT_EQ(Gui::AboutDialog::updateStatusText(QStringLiteral("v1.2.0"), QStringLiteral("1.2.0")),
              QStringLiteral("已是最新版 1.2.0"));
    // 开发构建比发布版新：既不是"有更新"，也不能谎称"已是最新"。
    EXPECT_EQ(Gui::AboutDialog::updateStatusText(QStringLiteral("v1.1.1"), QStringLiteral("1.2.0")),
              QStringLiteral("当前 1.2.0 比最新发布版 1.1.1 更新"));
    // 一个发布都还没建时，latest 接口回的就是 404：这不是"检查失败"。
    EXPECT_EQ(Gui::AboutDialog::failureStatusText(QStringLiteral("HTTP 404")),
              QStringLiteral("还没有已发布的版本"));
    EXPECT_EQ(Gui::AboutDialog::failureStatusText(QStringLiteral("HTTP 502")),
              QStringLiteral("检查失败：HTTP 502"));
    EXPECT_EQ(Gui::AboutDialog::failureStatusText(QString()), QStringLiteral("检查失败：未知原因"));
}

TEST(AboutDialog, PointsAtTheRepoAndItsReleasesPage)
{
    EXPECT_EQ(Gui::AboutDialog::repositoryUrl(), QStringLiteral("https://github.com/mateo-ki/vcc"));
    EXPECT_EQ(Gui::AboutDialog::releasesUrl(),
              QStringLiteral("https://github.com/mateo-ki/vcc/releases"));
    EXPECT_EQ(Gui::AboutDialog::latestReleaseApiUrl(),
              QStringLiteral("https://api.github.com/repos/mateo-ki/vcc/releases/latest"));
}

/// 离屏摆一次：四行信息 + 一行状态 + 三颗按钮都得在场、都得看得见，尺寸不许挤成一团。
TEST(AboutDialog, LaysOutEveryRowWhenShown)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutDialog dialog(*context);
    dialog.show();
    QCoreApplication::processEvents();

    EXPECT_GE(dialog.width(), 380) << dialog.size().width();
    EXPECT_GE(dialog.height(), 220) << dialog.size().height();
    int visibleLabels = 0;
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        if (label->isVisible() && !label->text().isEmpty()) {
            ++visibleLabels;
        }
    }
    EXPECT_GE(visibleLabels, 6) << "版本/仓库/许可证/社区/状态这些行至少都得渲染出来";
    int visibleButtons = 0;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->isVisible() && button->isEnabled()) {
            ++visibleButtons;
        }
    }
    EXPECT_GE(visibleButtons, 2) << "检查更新与关闭至少得是能点的";
    dialog.hide();
}

/// 下载那颗只在"确有新版、且这条发布真挂了安装包"时亮；缺一个条件都不该亮。
TEST(AboutDialog, EnablesDownloadOnlyWhenANewerReleaseCarriesAnInstaller)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutDialog dialog(*context);
    QPushButton *download = buttonByLabel(dialog, QStringLiteral("下载新版本"));
    ASSERT_TRUE(download != nullptr);

    Core::Release same;
    same.version = Core::appVersion();
    same.assetUrl = QStringLiteral("https://objects.example/VCC-installer.exe");
    dialog.showUpdateResult(QStringLiteral("已是最新版"), same);
    EXPECT_FALSE(download->isEnabled()) << "本机已经是最新，不该给一颗能点的下载";

    Core::Release newerWithoutAsset;
    newerWithoutAsset.version = QStringLiteral("999.0.0");
    dialog.showUpdateResult(QStringLiteral("发现新版本 999.0.0"), newerWithoutAsset);
    EXPECT_FALSE(download->isEnabled()) << "这条发布根本没有安装包";

    Core::Release newer;
    newer.version = QStringLiteral("999.0.0");
    newer.assetName = QStringLiteral("VCC-999.0.0-win64.exe");
    newer.assetUrl = QStringLiteral("https://objects.example/VCC-999.0.0-win64.exe");
    dialog.showUpdateResult(QStringLiteral("发现新版本 999.0.0"), newer);
    EXPECT_TRUE(download->isEnabled());
}

/// 目标文件名来自远端的资产名，不能让它把文件写到 updates/ 以外去。
TEST(AboutDialog, KeepsTheDownloadedFileNameInsideTheTargetDirectory)
{
    EXPECT_EQ(Gui::AboutDialog::downloadTarget(QStringLiteral("updates"),
                                               QStringLiteral("VCC-1.2.0-win64.exe"),
                                               QStringLiteral("1.2.0")),
              QStringLiteral("updates/VCC-1.2.0-win64.exe"));
    const QString escaped = Gui::AboutDialog::downloadTarget(
        QStringLiteral("updates"), QStringLiteral("../../../windows/system32/evil.exe"),
        QStringLiteral("1.2.0"));
    EXPECT_FALSE(escaped.contains(QStringLiteral(".."))) << escaped.toStdString();
    EXPECT_TRUE(escaped.endsWith(QStringLiteral(".exe"))) << escaped.toStdString();
    EXPECT_TRUE(escaped.startsWith(QStringLiteral("updates/"))) << escaped.toStdString();
}
