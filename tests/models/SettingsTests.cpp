// 基准站点列表的切分口径也住在这里（纯函数，跟着设置一起测）。
#include "proxy/TestUrls.h"

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
    EXPECT_TRUE(settings.failoverProbeUrls.isEmpty()) << "基准站点默认空 = 用内置的 Google 204";
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

/// 基准站点：patch 里出现才改，没出现不许顺手动它；序列化要带得出去也收得回来。
TEST(FailoverProbeUrls, patchAndRoundTrip)
{
    const Json patch = Json::parse(R"({"failover_probe_urls": "https://linux.do/"})");
    Models::AppSettings settings;
    Models::UpdateSettings::fromJson(patch).applyTo(settings);
    EXPECT_EQ(settings.failoverProbeUrls.toStdString(), std::string("https://linux.do/"));
    EXPECT_EQ(Json::parse(settings.toJson().dump())["failover_probe_urls"].is_string(), true);

    Models::AppSettings again;
    Models::UpdateSettings::fromJson(Models::UpdateSettings::fromJson(patch).toJson())
        .applyTo(again);
    EXPECT_EQ(again.failoverProbeUrls.toStdString(), std::string("https://linux.do/"));

    Models::AppSettings untouched;
    Models::UpdateSettings::fromJson(Json::parse(R"({"fetch_concurrency": 9})")).applyTo(untouched);
    EXPECT_TRUE(untouched.failoverProbeUrls.isEmpty());
}

/// 设置里那段文本 -> 探测列表：去空白行、去首尾空格、按出现顺序去重；空 = 用内置那一条。
TEST(ProbeBaselineUrls, splitsTrimsAndDedupes)
{
    const QString text = QStringList{
        QStringLiteral("  https://a.test/  "),
        QString(),
        QStringLiteral("https://b.test/"),
        QStringLiteral("https://a.test/"),
        QStringLiteral("   "),
    }.join(QChar(10));
    const QStringList parsed = ProxyProbe::probeBaselineUrls(text);
    ASSERT_EQ(parsed.size(), 2);
    EXPECT_EQ(parsed.at(0).toStdString(), std::string("https://a.test/"));
    EXPECT_EQ(parsed.at(1).toStdString(), std::string("https://b.test/"));
    EXPECT_TRUE(ProxyProbe::probeBaselineUrls(QString()).isEmpty());
    EXPECT_TRUE(ProxyProbe::probeBaselineUrls(
        QStringList{QString(), QStringLiteral(" 	 ")}.join(QChar(10))).isEmpty());
}
