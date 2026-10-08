#include <gtest/gtest.h>

#include <QDateTime>

#include "core/TimeUtil.h"

#include <QString>

namespace {

// 这些字符串直接取自生产库 data/vpn-parser.db 的 sources.created_at / last_fetch_at，
// 是 Rust chrono 写出来的真实字节，不是构造的理想样本。
const char *kRealNano9 = "2026-08-10T11:42:51.624230300+00:00";
const char *kRealNano6 = "2026-08-10T11:42:51.624442+00:00";
const char *kRealNano9Other = "2026-09-28T15:23:25.022502100+00:00";

TEST(TimeUtil, RoundTripsRealProductionTimestampsByteForByte)
{
    for (const char *text : {kRealNano9, kRealNano6, kRealNano9Other}) {
        const QString input = QString::fromLatin1(text);
        const TimeUtil::OptionalUtc parsed = TimeUtil::OptionalUtc::fromText(input);
        ASSERT_TRUE(parsed.hasValue) << "解析失败: " << text;
        EXPECT_EQ(parsed.toDbText().toStdString(), input.toStdString()) << text;
    }
}

TEST(TimeUtil, JsonFormUsesZuluSuffix)
{
    const TimeUtil::OptionalUtc parsed = TimeUtil::OptionalUtc::fromText(
        QString::fromLatin1(kRealNano9));
    ASSERT_TRUE(parsed.hasValue);
    EXPECT_EQ(parsed.toJsonText().toStdString(),
              std::string("2026-08-10T11:42:51.624230300Z"));
}

TEST(TimeUtil, AutoSiCollapsesTrailingZeroGroups)
{
    struct Sample {
        const char *input;
        const char *expected;
    };
    // chrono 的 AutoSi：nano==0 不写小数；整毫秒写 3 位；整微秒写 6 位；否则 9 位。
    const Sample samples[] = {
        {"2026-01-01T00:00:00+00:00", "2026-01-01T00:00:00+00:00"},
        {"2026-01-01T00:00:00.500+00:00", "2026-01-01T00:00:00.500+00:00"},
        {"2026-01-01T00:00:00.624442000+00:00", "2026-01-01T00:00:00.624442+00:00"},
        {"2026-01-01T00:00:00.000001000+00:00", "2026-01-01T00:00:00.000001+00:00"},
        {"2026-01-01T00:00:00.000000001+00:00", "2026-01-01T00:00:00.000000001+00:00"},
        {"2026-01-01T00:00:00.001+00:00", "2026-01-01T00:00:00.001+00:00"},
    };
    for (const Sample &sample : samples) {
        const TimeUtil::OptionalUtc parsed =
            TimeUtil::OptionalUtc::fromText(QString::fromLatin1(sample.input));
        ASSERT_TRUE(parsed.hasValue) << sample.input;
        EXPECT_EQ(parsed.toDbText().toStdString(), std::string(sample.expected)) << sample.input;
    }
}

TEST(TimeUtil, NowHasSubSecondResolutionSoItNeverCollidesWithinTheSameMillisecond)
{
    // 测速结果按 updated_at 排序，若只有毫秒精度，同批写入会并列。
    const QString first = TimeUtil::nowRfc3339();
    const QString second = TimeUtil::nowRfc3339();
    EXPECT_NE(first, second);
    EXPECT_TRUE(first.contains(QLatin1Char('.')));
}

TEST(TimeUtil, AcceptsZuluAndNonUtcOffsets)
{
    const TimeUtil::OptionalUtc zulu =
        TimeUtil::OptionalUtc::fromText(QStringLiteral("2026-08-10T11:42:51.624230300Z"));
    const TimeUtil::OptionalUtc offset =
        TimeUtil::OptionalUtc::fromText(QStringLiteral("2026-08-10T19:42:51.624230300+08:00"));
    ASSERT_TRUE(zulu.hasValue);
    ASSERT_TRUE(offset.hasValue);
    EXPECT_EQ(zulu.nanos, offset.nanos);
}

TEST(TimeUtil, RejectsMalformedText)
{
    EXPECT_FALSE(TimeUtil::OptionalUtc::fromText(QStringLiteral("2026-08-10")).hasValue);
    EXPECT_FALSE(TimeUtil::OptionalUtc::fromText(QString()).hasValue);
    EXPECT_FALSE(TimeUtil::OptionalUtc::fromText(QStringLiteral("")).hasValue);
}

TEST(TimeUtil, DayStringsUseRequestedCalendar)
{
    // 2026-08-10T23:30:00Z 在北京时间是次日 07:30，本地日可能比 UTC 日晚一天。
    const TimeUtil::OptionalUtc utc =
        TimeUtil::OptionalUtc::fromText(QStringLiteral("2026-08-10T23:30:00+00:00"));
    ASSERT_TRUE(utc.hasValue);
    EXPECT_EQ(TimeUtil::utcDay(utc.nanos).toStdString(), std::string("2026-08-10"));
    // 独立参照：Qt 自己的本地时区换算路径。
    const QString expected = QDateTime::fromMSecsSinceEpoch(utc.nanos / 1000000LL)
                                 .date()
                                 .toString(QStringLiteral("yyyy-MM-dd"));
    EXPECT_EQ(TimeUtil::localDay(utc.nanos), expected);
}

}
