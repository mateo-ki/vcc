#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QProgressBar>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <memory>

#include "app/AppContext.h"
#include "core/Result.h"
#include "core/Version.h"
#include "gui/AboutPage.h"
#include "kernel/KernelSource.h"

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

QString findButtonText(Gui::AboutPage &dialog, const QString &text)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == text) {
            return text;
        }
    }
    return QString();
}

bool hasLabelWith(Gui::AboutPage &dialog, const QString &fragment)
{
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        if (label->text().contains(fragment)) {
            return true;
        }
    }
    return false;
}

QPushButton *buttonByLabel(Gui::AboutPage &dialog, const QString &label)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == label) {
            return button;
        }
    }
    return nullptr;
}

} // namespace

TEST(AboutPage, ShowsTheBuiltInVersionAndTheRepository)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutPage dialog(*context);
    // 它是一页，不是对话框：没有标题栏可断言，页面自己有 objectName。
    EXPECT_EQ(dialog.objectName(), QStringLiteral("AboutPage"));
    EXPECT_TRUE(hasLabelWith(dialog, Core::appVersion()))
        << "关于框里看不到当前版本，检查更新就没有对照";
    EXPECT_TRUE(hasLabelWith(dialog, Gui::AboutPage::repositoryUrl()));
    EXPECT_TRUE(hasLabelWith(dialog, QStringLiteral("Apache-2.0")));
    EXPECT_FALSE(findButtonText(dialog, QStringLiteral("检查更新")).isEmpty());
    EXPECT_FALSE(findButtonText(dialog, QStringLiteral("打开发布页")).isEmpty());
}

TEST(AboutPage, DownloadStaysDarkUntilThereIsSomethingToGrab)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutPage dialog(*context);
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

TEST(AboutPage, StatusTextTellsTheThreeCasesApart)
{
    EXPECT_EQ(Gui::AboutPage::updateStatusText(QStringLiteral("v1.3.0"), QStringLiteral("1.2.0")),
              QStringLiteral("发现新版本 1.3.0（当前 1.2.0）"));
    EXPECT_EQ(Gui::AboutPage::updateStatusText(QStringLiteral("v1.2.0"), QStringLiteral("1.2.0")),
              QStringLiteral("已是最新版 1.2.0"));
    // 开发构建比发布版新：既不是"有更新"，也不能谎称"已是最新"。
    EXPECT_EQ(Gui::AboutPage::updateStatusText(QStringLiteral("v1.1.1"), QStringLiteral("1.2.0")),
              QStringLiteral("当前 1.2.0 比最新发布版 1.1.1 更新"));
    // 一个发布都还没建时，latest 接口回的就是 404：这不是"检查失败"。
    EXPECT_EQ(Gui::AboutPage::failureStatusText(QStringLiteral("HTTP 404")),
              QStringLiteral("还没有已发布的版本"));
    EXPECT_EQ(Gui::AboutPage::failureStatusText(QStringLiteral("HTTP 502")),
              QStringLiteral("检查失败：HTTP 502"));
    EXPECT_EQ(Gui::AboutPage::failureStatusText(QString()), QStringLiteral("检查失败：未知原因"));
}

TEST(AboutPage, PointsAtTheRepoAndItsReleasesPage)
{
    EXPECT_EQ(Gui::AboutPage::repositoryUrl(), QStringLiteral("https://github.com/mateo-ki/vcc"));
    EXPECT_EQ(Gui::AboutPage::releasesUrl(),
              QStringLiteral("https://github.com/mateo-ki/vcc/releases"));
    EXPECT_EQ(Gui::AboutPage::latestReleaseApiUrl(),
              QStringLiteral("https://api.github.com/repos/mateo-ki/vcc/releases/latest"));
}

/// 离屏摆一次：四行信息 + 一行状态 + 三颗按钮都得在场、都得看得见，尺寸不许挤成一团。
TEST(AboutPage, LaysOutEveryRowWhenShown)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutPage dialog(*context);
    // 它是一页：真实尺寸来自内容栈（1330x800 减掉侧栏），不是自己 resize 出来的，
    // 所以这里按一页该有的尺寸量，而不是拿对话框的默认大小当基线。
    dialog.resize(1000, 700);
    dialog.show();
    QCoreApplication::processEvents();

    EXPECT_EQ(dialog.width(), 1000);
    EXPECT_EQ(dialog.height(), 700);
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
    // 页上没有"关闭"这颗了（它不是弹框）：开页时只有「检查更新」该是能点的，
    // 下载/打开发布页/打开所在文件夹都要等检查结果出来。
    EXPECT_EQ(visibleButtons, 1) << "没检查过就该只有一颗能点：检查更新";
    EXPECT_GE(dialog.findChildren<QPushButton *>().size(), 4) << "四颗动作按钮都得在场";
    dialog.hide();
}

/// 下载那颗只在"确有新版、且这条发布真挂了安装包"时亮；缺一个条件都不该亮。
TEST(AboutPage, EnablesDownloadOnlyWhenANewerReleaseCarriesAnInstaller)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutPage dialog(*context);
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
TEST(AboutPage, KeepsTheDownloadedFileNameInsideTheTargetDirectory)
{
    EXPECT_EQ(Gui::AboutPage::downloadTarget(QStringLiteral("updates"),
                                               QStringLiteral("VCC-1.2.0-win64.exe"),
                                               QStringLiteral("1.2.0")),
              QStringLiteral("updates/VCC-1.2.0-win64.exe"));
    const QString escaped = Gui::AboutPage::downloadTarget(
        QStringLiteral("updates"), QStringLiteral("../../../windows/system32/evil.exe"),
        QStringLiteral("1.2.0"));
    EXPECT_FALSE(escaped.contains(QStringLiteral(".."))) << escaped.toStdString();
    EXPECT_TRUE(escaped.endsWith(QStringLiteral(".exe"))) << escaped.toStdString();
    EXPECT_TRUE(escaped.startsWith(QStringLiteral("updates/"))) << escaped.toStdString();
}

/// 进度那行字：知道总长就给百分比，不知道就只报已收 —— 不编一个假的百分比。
TEST(AboutPage, ProgressTextOnlyInventsAPercentageWhenTheSizeIsKnown)
{
    const qint64 meg = 1024 * 1024;
    EXPECT_EQ(Gui::AboutPage::progressText(5 * meg, 20 * meg),
              QStringLiteral("正在下载 5.0 MB / 20.0 MB（25%）"));
    EXPECT_EQ(Gui::AboutPage::progressText(0, 0), QStringLiteral("正在下载 0.0 MB…"));
    EXPECT_FALSE(Gui::AboutPage::progressText(3 * meg, 0).contains(QChar('%')));
}

/// 关于页顶部要有品牌行：那枚 44px 的 logo 必须真渲染出来，不能只有两行字。
TEST(AboutPage, ShowsTheAppIconAndItsLogoRow)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutPage dialog(*context);
    QLabel *logo = dialog.findChild<QLabel *>(QStringLiteral("AboutLogo"));
    ASSERT_TRUE(logo != nullptr);
    EXPECT_FALSE(logo->pixmap().isNull()) << "品牌行的 logo 没渲染出来";
    EXPECT_EQ(logo->pixmap().width(), 44) << "品牌行的 logo 就是侧栏那一枚的尺寸";
}

/// 进度条平时不占位，只有下载中才出现（检查更新/关闭那两行不该被它撑开）。
TEST(AboutPage, ProgressBarStaysHiddenUntilADownloadStarts)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    Gui::AboutPage dialog(*context);
    QProgressBar *bar = dialog.findChild<QProgressBar *>();
    ASSERT_TRUE(bar != nullptr);
    EXPECT_FALSE(bar->isVisible());
    dialog.show();
    QCoreApplication::processEvents();
    EXPECT_FALSE(bar->isVisible()) << "没在下载就该藏着";
    dialog.hide();
}

/// 出口优先级：自己的内核 mixed 端口 > Windows 系统代理 > 直连。
TEST(AboutPage, FallsBackToTheSystemProxyWhenNoKernelIsRunning)
{
    EXPECT_EQ(Gui::AboutPage::pickProxyUrl(QStringLiteral("http://127.0.0.1:10808"),
                                             QStringLiteral("http://127.0.0.1:9980")),
              QStringLiteral("http://127.0.0.1:10808"));
    EXPECT_EQ(Gui::AboutPage::pickProxyUrl(QString(), QStringLiteral("http://127.0.0.1:9980")),
              QStringLiteral("http://127.0.0.1:9980"));
    EXPECT_TRUE(Gui::AboutPage::pickProxyUrl(QString(), QString()).isEmpty());
}

/// 三条通道：镜像 / 本地代理 / 直连。没有可用代理时那一行不该出现。
TEST(AboutPage, ListsThreeChannelsAndHidesProxyWhenThereIsNone)
{
    const QList<Kernel::Source> withProxy = Gui::AboutPage::downloadChannels(true);
    ASSERT_EQ(withProxy.size(), 3);
    EXPECT_EQ(withProxy.at(0), Kernel::Source::GhProxy);
    EXPECT_EQ(withProxy.at(1), Kernel::Source::Proxy);
    EXPECT_EQ(withProxy.at(2), Kernel::Source::Official);

    const QList<Kernel::Source> withoutProxy = Gui::AboutPage::downloadChannels(false);
    ASSERT_EQ(withoutProxy.size(), 2);
    EXPECT_FALSE(withoutProxy.contains(Kernel::Source::Proxy))
        << "列一条点不动的通道等于骗人";
}

/// 每条通道翻成"下哪个 URL、走不走代理"。
TEST(AboutPage, PlansEachChannelDifferently)
{
    const QString asset = QStringLiteral("https://github.com/mateo-ki/vcc/releases/download/v9/VCC-9.exe");
    const QString kernel = QStringLiteral("http://127.0.0.1:10808");
    const QString system = QStringLiteral("http://127.0.0.1:9980");

    const Gui::AboutPage::DownloadPlan mirror
        = Gui::AboutPage::planForDownload(Kernel::Source::GhProxy, asset, kernel, system);
    EXPECT_TRUE(mirror.url.startsWith(QStringLiteral("https://gh-proxy.com/"))) << mirror.url.toStdString();
    EXPECT_TRUE(mirror.url.endsWith(asset)) << mirror.url.toStdString();
    EXPECT_TRUE(mirror.proxyUrl.isEmpty()) << "镜像站是公网地址，不该再套一层自己的代理";

    const Gui::AboutPage::DownloadPlan proxy
        = Gui::AboutPage::planForDownload(Kernel::Source::Proxy, asset, kernel, system);
    EXPECT_EQ(proxy.url, asset);
    EXPECT_EQ(proxy.proxyUrl, kernel) << "自己的内核 mixed 优先";

    const Gui::AboutPage::DownloadPlan proxyWithoutKernel
        = Gui::AboutPage::planForDownload(Kernel::Source::Proxy, asset, QString(), system);
    EXPECT_EQ(proxyWithoutKernel.proxyUrl, system);

    const Gui::AboutPage::DownloadPlan direct
        = Gui::AboutPage::planForDownload(Kernel::Source::Official, asset, kernel, system);
    EXPECT_EQ(direct.url, asset);
    EXPECT_TRUE(direct.proxyUrl.isEmpty()) << "选了直连就不能偷偷走代理";
}

/// 校验值只认 `sha256:<hex>` 这一种形状，比对用小写十六进制。
TEST(AboutPage, ReadsOnlyASha256Digest)
{
    EXPECT_EQ(Gui::AboutPage::digestHex(QStringLiteral("sha256:5D585EB8")),
              QStringLiteral("5d585eb8"));
    EXPECT_TRUE(Gui::AboutPage::digestHex(QStringLiteral("md5:abcdef")).isEmpty());
    EXPECT_TRUE(Gui::AboutPage::digestHex(QString()).isEmpty());
}
