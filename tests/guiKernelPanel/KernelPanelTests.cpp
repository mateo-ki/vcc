#include <gtest/gtest.h>

#include <QChar>
#include <QList>
#include <QString>

#include "gui/KernelPanel.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "kernel/KernelManager.h"
#include "kernel/KernelRelease.h"
#include "proxy/MihomoConfig.h"
#include "sysproxy/Format.h"

using namespace Gui;

namespace {

Kernel::Info makeInfo(Kernel::Kind kind, bool available, const QString &version = QString(),
                      const QString &path = QString())
{
    Kernel::Info info;
    info.kind = kind;
    info.available = available;
    info.version = version;
    info.path = path;
    return info;
}

Kernel::ReleaseInfo makeRelease(Kernel::Kind kind, const QString &latest, bool updateAvailable)
{
    Kernel::ReleaseInfo release;
    release.kind = kind;
    release.latestVersion = latest;
    release.updateAvailable = updateAvailable;
    return release;
}

TEST(KernelFormat, BytesUseThreeUnitTiers)
{
    EXPECT_EQ(formatBytes(0), QStringLiteral("0 B"));
    EXPECT_EQ(formatBytes(1023), QStringLiteral("1023 B"));
    EXPECT_EQ(formatBytes(1024), QStringLiteral("1.0 KB"));
    EXPECT_EQ(formatBytes(1536), QStringLiteral("1.5 KB"));
    EXPECT_EQ(formatBytes(1024ULL * 1024ULL), QStringLiteral("1.0 MB"));
    EXPECT_EQ(formatBytes(1024ULL * 1024ULL * 1024ULL), QStringLiteral("1.00 GB"));
    // 档位是 >= 判断，跨界那一格必须换单位。
    EXPECT_EQ(formatBytes(1024ULL * 1024ULL * 1024ULL - 1), QStringLiteral("1024.0 MB"));
}

TEST(KernelRows, MissingKernelShowsNotFoundAndDashPlaceholders)
{
    const Proxy::ProxyRuntimeStatus status;
    const QList<Kernel::Info> kernels = {makeInfo(Kernel::Kind::Xray, false)};
    const QList<KernelRow> rows = buildKernelRows(kernels, {}, false, status);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].statusText, UiText::kernelStateMissing());
    EXPECT_FALSE(rows[0].statusOk);
    EXPECT_EQ(rows[0].version, QStringLiteral("-"));
    EXPECT_EQ(rows[0].path, QStringLiteral("-"));
    EXPECT_EQ(rows[0].latest, UiText::kernelLatestChecking());
    EXPECT_EQ(rows[0].actionText, UiText::kernelActionDownload());
    EXPECT_FALSE(rows[0].actionEnabled);
    EXPECT_EQ(rows[0].hover, UiText::kernelHoverAsset());
}

TEST(KernelRows, UpdateButtonNeedsReleaseInfoIdleFlagAndFreeKernel)
{
    const QList<Kernel::Info> kernels = {
        makeInfo(Kernel::Kind::Mihomo, true, QStringLiteral("v1.19.0"),
                 QStringLiteral("data/kernels/mihomo.exe"))};
    const QList<Kernel::ReleaseInfo> releases = {
        makeRelease(Kernel::Kind::Mihomo, QStringLiteral("v1.19.3"), true)};

    Proxy::ProxyRuntimeStatus idle;
    QList<KernelRow> rows = buildKernelRows(kernels, releases, false, idle);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].version, QStringLiteral("v1.19.0"));
    EXPECT_EQ(rows[0].path, QStringLiteral("data/kernels/mihomo.exe"));
    EXPECT_EQ(rows[0].latest, QStringLiteral("v1.19.3"));
    EXPECT_TRUE(rows[0].latestIsWarning);
    EXPECT_EQ(rows[0].actionText, UiText::kernelActionUpdate());
    EXPECT_TRUE(rows[0].actionEnabled);

    // 下载中不许叠第二个作业。
    rows = buildKernelRows(kernels, releases, true, idle);
    EXPECT_FALSE(rows[0].actionEnabled);

    // 正在被代理用的那个内核要停下来才能更新，提示语也要换成这一条。
    // kernelInUse 认的是"在跑 + 就是这个内核"两件事（与 Rust kernel_in_use 同源），
    // 只填 kernel 不填 state 是测试夹具写错，不是实现错。
    Proxy::ProxyRuntimeStatus running;
    running.state = Proxy::ProxyState::Running;
    running.kernel = Kernel::Kind::Mihomo;
    rows = buildKernelRows(kernels, releases, false, running);
    EXPECT_FALSE(rows[0].actionEnabled);
    EXPECT_EQ(rows[0].hover, UiText::kernelHoverInUse());
}

TEST(KernelRows, ReleaseWithoutVersionStillReadsChecking)
{
    const Proxy::ProxyRuntimeStatus status;
    const QList<Kernel::Info> kernels = {makeInfo(Kernel::Kind::SingBox, true,
                                                  QStringLiteral("0.9.0"))};
    const QList<Kernel::ReleaseInfo> releases = {makeRelease(Kernel::Kind::SingBox, QString(),
                                                             false)};
    const QList<KernelRow> rows = buildKernelRows(kernels, releases, false, status);
    EXPECT_EQ(rows[0].latest, UiText::kernelLatestChecking());
    EXPECT_FALSE(rows[0].latestIsWarning);
    EXPECT_FALSE(rows[0].actionEnabled);
}

TEST(KernelProgress, DownloadStageShowsBytesAndRate)
{
    Kernel::UpdateProgress progress;
    progress.kind = Kernel::Kind::Mihomo;
    progress.stage = Kernel::UpdateStage::Downloading;
    progress.downloaded = 1024ULL * 1024ULL;
    progress.total = 1024ULL * 1024ULL * 2ULL;
    progress.bytesPerSecond = 512 * 1024;
    progress.message = QStringLiteral("ignored while downloading");

    const QString text = kernelProgressText(progress);
    const QString expected = SysProxy::fillTemplate(
        UiText::kernelProgressFormat(), {},
        {Kernel::kindDisplayName(Kernel::Kind::Mihomo),
         Kernel::updateStageLabel(Kernel::UpdateStage::Downloading),
         SysProxy::fillTemplate(UiText::kernelDownloadFormat(), {},
                                {QStringLiteral("1.0 MB"), QStringLiteral("2.0 MB"),
                                 QStringLiteral("512.0 KB")})});
    EXPECT_EQ(text, expected);
    // 单位与速率拼接的口径：B/KB/MB 走同一套，结尾的 /s 只在模板里出现一次。
    EXPECT_EQ(text.count(QChar(0x00B7)), 3);
    EXPECT_FALSE(text.contains(QStringLiteral("/s/s")));
    EXPECT_FALSE(text.contains(progress.message));
}

TEST(KernelProgress, OtherStagesPassTheMessageThrough)
{
    Kernel::UpdateProgress progress;
    progress.kind = Kernel::Kind::Xray;
    progress.stage = Kernel::UpdateStage::Verifying;
    progress.message = QStringLiteral("sha256 ok");
    const QString text = kernelProgressText(progress);
    EXPECT_TRUE(text.contains(progress.message));
    EXPECT_TRUE(text.contains(Kernel::kindDisplayName(Kernel::Kind::Xray)));
}

}
