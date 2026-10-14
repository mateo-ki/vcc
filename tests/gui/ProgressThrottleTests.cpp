#include <gtest/gtest.h>

#include <QThread>

#include "gui/ProgressThrottle.h"

using Gui::ProgressThrottle;

/// 闸门的全部规则：第一次必投、80ms 内合并、最后一颗不许被合并掉。
TEST(ProgressThrottle, FirstUpdateAlwaysGoesThrough)
{
    ProgressThrottle throttle;
    EXPECT_TRUE(throttle.shouldPost(1, 256));
}

TEST(ProgressThrottle, RapidUpdatesCollapseIntoOne)
{
    ProgressThrottle throttle;
    ASSERT_TRUE(throttle.shouldPost(1, 100));
    int posted = 1;
    for (size_t done = 2; done <= 400; ++done) {
        if (throttle.shouldPost(done, 500)) {
            ++posted;
        }
    }
    // 这一整轮跑不到 80ms 的几毫秒里，最多只该投出第一次那一条。
    EXPECT_EQ(posted, 1) << "高频回调没有真的合并";
}

TEST(ProgressThrottle, TheFinalUpdateIsNeverSwallowed)
{
    ProgressThrottle throttle;
    ASSERT_TRUE(throttle.shouldPost(1, 50));
    // 紧跟第二次：还在 80ms 窗口内，且不是最后一颗 -> 合并
    EXPECT_FALSE(throttle.shouldPost(2, 50));
    // 最后一颗：无论多近都必须投出去，否则状态栏会永远停在"还差几颗"
    EXPECT_TRUE(throttle.shouldPost(50, 50));
    EXPECT_TRUE(throttle.shouldPost(49, 50));   // done+1>=total 也算最后一条
}

TEST(ProgressThrottle, SlowUpdatesStillPassThrough)
{
    ProgressThrottle throttle;
    ASSERT_TRUE(throttle.shouldPost(1, 10));
    QThread::msleep(static_cast<unsigned long>(ProgressThrottle::kMinIntervalMs + 40));
    EXPECT_TRUE(throttle.shouldPost(2, 10)) << "过了合并窗口还不投，进度就冻住了";
}
