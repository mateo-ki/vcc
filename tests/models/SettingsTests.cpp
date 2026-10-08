#include <gtest/gtest.h>

#include "models/Settings.h"

// 逐条搬 Rust src/models/mod.rs 的 name_excluded 测试，行为必须一致。
TEST(NameExcluded, MatchesCaseInsensitivelyOnSubstrings)
{
    const QString keywords = QString::fromUtf8("到期，限速 ; ×3, 未验证");
    EXPECT_TRUE(Models::nameExcluded(QString::fromUtf8("HK-到期-01"), keywords));
    EXPECT_TRUE(Models::nameExcluded(QString::fromUtf8("【限速】JP 节点"), keywords));
    EXPECT_TRUE(Models::nameExcluded(QString::fromUtf8("未验证节点"), keywords));
    EXPECT_FALSE(Models::nameExcluded(QString::fromUtf8("HK 普通节点 01"), keywords));
    // 空关键词列表不排除任何东西
    EXPECT_FALSE(Models::nameExcluded(QString::fromUtf8("任意节点"), QString::fromUtf8("")));
    EXPECT_FALSE(Models::nameExcluded(QString::fromUtf8("任意节点"),
                                      QString::fromUtf8(" , ； ")));
    // 大小写不敏感
    EXPECT_TRUE(Models::nameExcluded(QStringLiteral("US-TEST-01"), QStringLiteral("test")));
    EXPECT_TRUE(Models::nameExcluded(QStringLiteral("us-test-01"), QStringLiteral("TEST")));
}

TEST(AppSettings, DefaultsMatchRustStruct)
{
    const Models::AppSettings settings;
    EXPECT_FALSE(settings.autoFetchEnabled);
    EXPECT_EQ(settings.autoFetchIntervalMin, 60);
    EXPECT_FALSE(settings.autoSpeedtestEnabled);
    EXPECT_EQ(settings.fetchConcurrency, 4);
    EXPECT_EQ(settings.fetchTimeoutSecs, 30);
    EXPECT_EQ(settings.speedtestConcurrency, 128);
    EXPECT_EQ(settings.speedtestTimeoutMs, 1500);
    EXPECT_FALSE(settings.speedtestDownload);
    EXPECT_EQ(settings.downloadTestMs, 800);
    EXPECT_TRUE(settings.httpServerEnabled);
    EXPECT_TRUE(settings.closeToTray);
    EXPECT_EQ(settings.failoverLatencyMs, 1500);
    EXPECT_FALSE(settings.speedtestParallelKernels);
    EXPECT_FALSE(settings.speedtestPersistentKernel);
    EXPECT_EQ(settings.speedtestReuseMinutes, 0);
    EXPECT_FALSE(settings.speedtestConditionalIsolation);
    EXPECT_EQ(settings.speedtestDownloadTopN, 0);
    EXPECT_FALSE(settings.bandwidthGuard);
    EXPECT_EQ(settings.bandwidthMinKbps, 0);
    EXPECT_FALSE(settings.firewallAutoAllow);
    EXPECT_FALSE(settings.autoFailover);
    EXPECT_FALSE(settings.speedtestAutoRefill);
    // 这两条"软件自己动"的开关默认开。
    EXPECT_TRUE(settings.autoPrepareKernel);
    EXPECT_TRUE(settings.speedtestAddedSource);
    EXPECT_EQ(settings.uiTheme.toStdString(), std::string(Models::kDefaultUiTheme));
    EXPECT_EQ(settings.kernelSource.toStdString(), std::string(Models::kDefaultKernelSource));
}

TEST(AppSettings, JsonKeysAreSerdeSnakeCaseAndNullForAbsentTimes)
{
    const Models::AppSettings settings;
    const Json value = settings.toJson();
    EXPECT_TRUE(value.contains("auto_fetch_interval_min"));
    EXPECT_TRUE(value.contains("speedtest_download_top_n"));
    EXPECT_TRUE(value.contains("last_auto_fetch_at"));
    EXPECT_TRUE(value["last_auto_fetch_at"].is_null());
    // 非 Option 的字符串字段输出 ""，不是 null。
    EXPECT_EQ(value["exclude_keywords"].is_string(), true);
    EXPECT_TRUE(value["exclude_keywords"].is_null() == false);
}

TEST(UpdateSettings, OnlyPresentKeysAreApplied)
{
    const Json patch = Json::parse(R"({"fetch_concurrency": 12, "auto_failover": true})");
    const Models::UpdateSettings update = Models::UpdateSettings::fromJson(patch);
    Models::AppSettings settings;
    update.applyTo(settings);
    EXPECT_EQ(settings.fetchConcurrency, 12);
    EXPECT_TRUE(settings.autoFailover);
    // 未出现的键保持默认，不被顺带改写。
    EXPECT_EQ(settings.speedtestConcurrency, 128);
    EXPECT_TRUE(settings.autoPrepareKernel);
}
