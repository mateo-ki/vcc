#include "app/ExitCleanup.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace App {

ExitStopOutcome stopProxyOnExit(const std::function<Status()> &stop, int timeoutMs)
{
    ExitStopOutcome outcome;
    // 顺序很重要：结果先写、标志后置位（release/acquire 配对），等待方只在 completed 为真时
    // 才读结果，超时那条路一次都不碰它。
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<Status>(Status::succeeded());
    std::thread([stop, finished, result]() {
        *result = stop();
        finished->store(true);
    }).detach();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!finished->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    outcome.completed = finished->load();
    if (outcome.completed && result->isErr()) {
        outcome.error = result->error();
    }
    return outcome;
}

} // namespace App
