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
