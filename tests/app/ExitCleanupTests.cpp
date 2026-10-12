#include <gtest/gtest.h>

#include "app/AppContext.h"
#include <QDir>
#include <QTemporaryDir>

#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <thread>

#include "app/ExitCleanup.h"
#include "core/Result.h"

// 退出时必须把代理关掉：少这一步，系统代理里就留着 127.0.0.1:<mixed 端口> 这个死端口，
// 浏览器连不上网（2026-10-10 实测踩过）。Rust 的做法是 on_exit 里同步 stop()、最多等 8s
// （gui/mod.rs:4657-4669），这里的三条断言对应它的三个分支。
namespace {

TEST(ExitCleanupTests, theExitBudgetIsRustsEightSeconds)
{
    ASSERT_EQ(App::kExitProxyStopTimeoutMs, 8000);
}

TEST(ExitCleanupTests, aCleanStopReportsNoError)
{
    std::atomic<int> calls{0};
    const App::ExitStopOutcome outcome = App::stopProxyOnExit([&calls]() {
        calls.fetch_add(1);
        return Status::succeeded();
    });
    ASSERT_TRUE(outcome.completed);
    ASSERT_TRUE(outcome.error.isEmpty()) << outcome.error.toStdString();
    ASSERT_EQ(calls.load(), 1);
}

TEST(ExitCleanupTests, stopFailuresAreHandedBackWithTheirReason)
{
    const App::ExitStopOutcome outcome =
        App::stopProxyOnExit([]() { return Status::failed(QStringLiteral("kernel stop failed")); });
    ASSERT_TRUE(outcome.completed);
    ASSERT_EQ(outcome.error.toStdString(), "kernel stop failed");
}

/// 后台测速/启动占着 operationMutex 时，退出不能无限期挂住（Rust 用 timeout 包 stop 就是
/// 为这个）；但那条 stop 不能因此被丢掉 —— 放弃等待之后它必须继续跑完。
TEST(ExitCleanupTests, aStopOutlivingTheBudgetLetsGoButStillRunsToCompletion)
{
    auto finished = std::make_shared<std::atomic<bool>>(false);
    QElapsedTimer started;
    started.start();
    const App::ExitStopOutcome outcome = App::stopProxyOnExit(
        [finished]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            finished->store(true);
            return Status::succeeded();
        },
        100);
    ASSERT_FALSE(outcome.completed) << "到点就该放手，不该跟着 stop 一起挂死";
    ASSERT_TRUE(outcome.error.isEmpty());
    ASSERT_LT(started.elapsed(), 350);
    for (int spin = 0; spin < 100 && !finished->load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(finished->load()) << "放弃等待不等于放弃关闭：残留代理下次开机还在";
}


/// 全量测速那道门：两个入口只能有一趟在跑，跑完必须开回来。
TEST(FullSpeedtestGate, singleFlightAndRelease)
{
    App::AppContext::Options options;
    const QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    options.dbPath = QDir(directory.path()).filePath(QStringLiteral("gate.db"));
    const auto created = App::AppContext::create(options);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    App::AppContext *context = created.value().get();

    EXPECT_FALSE(context->fullSpeedtestRunning());
    EXPECT_TRUE(context->tryBeginFullSpeedtest());
    EXPECT_FALSE(context->tryBeginFullSpeedtest()) << "第二趟必须被挡在外面";
    EXPECT_TRUE(context->fullSpeedtestRunning());
    context->endFullSpeedtest();
    EXPECT_FALSE(context->fullSpeedtestRunning());
    EXPECT_TRUE(context->tryBeginFullSpeedtest()) << "跑完之后还能再起下一趟";
}

} // namespace
