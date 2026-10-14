#include <gtest/gtest.h>

#include "core/Version.h"

TEST(VersionRelease, StripsOnlyTheLeadingVFromATag)
{
    EXPECT_EQ(Core::releaseVersion(QStringLiteral("v1.2.0")), QStringLiteral("1.2.0"));
    EXPECT_EQ(Core::releaseVersion(QStringLiteral("  v1.2.0  ")), QStringLiteral("1.2.0"));
    EXPECT_EQ(Core::releaseVersion(QStringLiteral("1.2.0")), QStringLiteral("1.2.0"));
    // 版本号中间出现的 v 不是前缀，不能误砍。
    EXPECT_EQ(Core::releaseVersion(QStringLiteral("v1.2.0-rc1")), QStringLiteral("1.2.0-rc1"));
}

TEST(VersionCompare, ComparesSegmentWiseNotLexically)
{
    EXPECT_EQ(Core::compareVersions(QStringLiteral("1.2.0"), QStringLiteral("1.2.0")), 0);
    EXPECT_EQ(Core::compareVersions(QStringLiteral("1.9.9"), QStringLiteral("1.10.0")), -1);
    EXPECT_EQ(Core::compareVersions(QStringLiteral("2.0.0"), QStringLiteral("1.99.99")), 1);
    // 段数不同按 0 补齐：开发时常见的"只写两段"要和"写全三段"相等。
    EXPECT_EQ(Core::compareVersions(QStringLiteral("1.2"), QStringLiteral("1.2.0")), 0);
    EXPECT_EQ(Core::compareVersions(QStringLiteral("1.2.1"), QStringLiteral("1.2")), 1);
    // 尾巴（预发布/构建名）不参与排序，主版本号说了算；这不是完整 semver，是有意的。
    EXPECT_EQ(Core::compareVersions(QStringLiteral("1.2.0-rc1"), QStringLiteral("1.2.0")), 0);
    EXPECT_EQ(Core::compareVersions(QStringLiteral("abc"), QStringLiteral("0.0.0")), 0);
}

TEST(VersionUpdate, OnlyANewerLatestCountsAsAnUpdate)
{
    EXPECT_TRUE(Core::isUpdateAvailable(QStringLiteral("v1.3.0"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(Core::isUpdateAvailable(QStringLiteral("v1.2.0"), QStringLiteral("1.2.0")));
    // 本机比发布版新（开发构建）不算"有更新"，但也不能被说成"已是最新"。
    EXPECT_FALSE(Core::isUpdateAvailable(QStringLiteral("v1.1.1"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(Core::isUpdateAvailable(QString(), QStringLiteral("1.2.0")));
}

TEST(VersionReleaseJson, ReadsTagAndPageFromTheLatestReleaseShape)
{
    const QString body = QStringLiteral(
        R"({"tag_name":"v1.3.0","name":"VCC 1.3.0","html_url":"https://github.com/mateo-ki/vcc/releases/tag/v1.3.0"})");
    const Result<Core::Release> parsed = Core::releaseFromJsonText(body);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_EQ(parsed.value().version, QStringLiteral("1.3.0"));
    EXPECT_EQ(parsed.value().url,
              QStringLiteral("https://github.com/mateo-ki/vcc/releases/tag/v1.3.0"));
}

TEST(VersionReleaseJson, ReportsGitHubsMessageWhenThereIsNoReleaseYet)
{
    const QString body =
        QStringLiteral(R"({"message":"Not Found","documentation_url":"https://docs.github.com"})");
    const Result<Core::Release> parsed = Core::releaseFromJsonText(body);
    ASSERT_TRUE(parsed.isErr());
    EXPECT_EQ(parsed.error(), QStringLiteral("Not Found"));
}

TEST(VersionReleaseJson, RejectsSomethingThatIsNotJsonAtAll)
{
    const Result<Core::Release> parsed = Core::releaseFromJsonText(QStringLiteral("<html>502</html>"));
    ASSERT_TRUE(parsed.isErr());
}

TEST(VersionReleaseJson, PrefersOurOwnInstallerAsset)
{
    const QString body = QStringLiteral(R"({"tag_name":"v2.0.0","html_url":"https://x/y","assets":[
        {"name":"source.zip","browser_download_url":"https://x/zip","size":999999},
        {"name":"VCC-2.0.0-win64.exe","browser_download_url":"https://x/ours","size":4000},
        {"name":"other-2.0.0.exe","browser_download_url":"https://x/bigger","size":9000}]})");
    const Result<Core::Release> parsed = Core::releaseFromJsonText(body);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_EQ(parsed.value().assetName, QStringLiteral("VCC-2.0.0-win64.exe"));
    EXPECT_EQ(parsed.value().assetUrl, QStringLiteral("https://x/ours"));
    EXPECT_EQ(parsed.value().assetSize, quint64(4000));
}

TEST(VersionReleaseJson, FallsBackToTheBiggestExeWhenNothingLooksOurs)
{
    const QString body = QStringLiteral(R"({"tag_name":"v2.0.0","assets":[
        {"name":"small.exe","browser_download_url":"https://x/small","size":10},
        {"name":"big.exe","browser_download_url":"https://x/big","size":9000},
        {"name":"notes.txt","browser_download_url":"https://x/notes","size":100000}]})");
    const Result<Core::Release> parsed = Core::releaseFromJsonText(body);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_EQ(parsed.value().assetUrl, QStringLiteral("https://x/big"));
}

TEST(VersionReleaseJson, LeavesTheAssetEmptyWhenTheReleaseHasNoInstaller)
{
    const QString body = QStringLiteral(R"({"tag_name":"v2.0.0","assets":[]})");
    const Result<Core::Release> parsed = Core::releaseFromJsonText(body);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_TRUE(parsed.value().assetUrl.isEmpty());
}

TEST(VersionFileName, KeepsOnlyPlainAsciiExeNamesAndRejectsPaths)
{
    EXPECT_EQ(Core::safeInstallerFileName(QStringLiteral("VCC-1.2.0-win64.exe"),
                                          QStringLiteral("1.2.0")),
              QStringLiteral("VCC-1.2.0-win64.exe"));
    // 远端给的名字是外部输入：带路径分隔符、带 ..、不是 .exe、空的，一律换掉。
    EXPECT_EQ(Core::safeInstallerFileName(QStringLiteral("../../windows/a.exe"),
                                          QStringLiteral("1.2.0")),
              QStringLiteral("VCC-1.2.0-win64.exe"));
    EXPECT_EQ(Core::safeInstallerFileName(QStringLiteral("a\\b.exe"), QStringLiteral("1.2.0")),
              QStringLiteral("VCC-1.2.0-win64.exe"));
    EXPECT_EQ(Core::safeInstallerFileName(QStringLiteral("installer.zip"), QStringLiteral("1.2.0")),
              QStringLiteral("VCC-1.2.0-win64.exe"));
    EXPECT_EQ(Core::safeInstallerFileName(QString(), QStringLiteral("1.2.0")),
              QStringLiteral("VCC-1.2.0-win64.exe"));
    // 版本号本身也可能被塞进怪字符，拼出来的文件名只留数字点和字母。
    EXPECT_EQ(Core::safeInstallerFileName(QString(), QStringLiteral("1.2.0-rc/1")),
              QStringLiteral("VCC-1.2.0-rc-1-win64.exe"));
}
