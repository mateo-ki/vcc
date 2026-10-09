#include <gtest/gtest.h>

#include "gui/Prepare.h"

using namespace Gui::Prepare;

TEST(Prepare, preparedOnlyWhenNothingIsReadyYet)
{
    // 刚装完：没内核也没在跑。
    EXPECT_TRUE(isPrepared(false, false));
    // 内核装好了就不是预备。
    EXPECT_FALSE(isPrepared(true, false));
    // 代理在跑时别把状态改成预备。
    EXPECT_FALSE(isPrepared(false, true));
}

TEST(Prepare, kernelDownloadIsGatedBySwitchKernelJobAndBudget)
{
    const std::optional<qint64> none;
    EXPECT_TRUE(shouldDownloadKernel(true, false, false, false, 0, none));
    // 开关关着就不能自己动。
    EXPECT_FALSE(shouldDownloadKernel(false, false, false, false, 0, none));
    EXPECT_FALSE(shouldDownloadKernel(true, true, false, false, 0, none));
    EXPECT_FALSE(shouldDownloadKernel(true, false, true, false, 0, none));
    // 内核作业还在跑不许叠第二个。
    EXPECT_FALSE(shouldDownloadKernel(true, false, false, true, 0, none));

    // 隔得不够久不重试；够久了可以；用满三次就停。
    EXPECT_FALSE(shouldDownloadKernel(true, false, false, false, 1, qint64(10000)));
    EXPECT_TRUE(shouldDownloadKernel(true, false, false, false, 1, qint64(90000)));
    EXPECT_TRUE(shouldDownloadKernel(true, false, false, false, 2, qint64(3600000)));
    EXPECT_FALSE(shouldDownloadKernel(true, false, false, false, kMaxAutoAttempts,
                                      qint64(3600000)));
}

TEST(Prepare, addedSourceIsOnlyTestedAfterItYieldsNodes)
{
    EXPECT_TRUE(shouldTestAddedSource(true, true, true, 40));
    // 开关关着不自动测。
    EXPECT_FALSE(shouldTestAddedSource(false, true, true, 40));
    // 不是我刚加的那个源就别管。
    EXPECT_FALSE(shouldTestAddedSource(true, false, true, 40));
    // 拉取失败不测。
    EXPECT_FALSE(shouldTestAddedSource(true, true, false, 40));
    // 解析出 0 个节点不测。
    EXPECT_FALSE(shouldTestAddedSource(true, true, true, 0));
}

TEST(Prepare, spacingAndAttemptBudgetMatchRust)
{
    EXPECT_EQ(kMaxAutoAttempts, 3);
    EXPECT_EQ(kMinAttemptSpacingMs, 90000);
}
