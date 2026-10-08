#include "api/ApiServerController.h"

#include <QThread>

#include <algorithm>
#include <mutex>
#include <vector>

#include "api/HttpServer.h"
#include "core/Log.h"

namespace Api {

namespace {

/// 一个服务器线程：run() 里跑 serveWithShutdown，等价 Rust 的
/// `tokio::spawn(serve_with_shutdown(state, listen, token, rx))`。
class ServerThread : public QThread {
public:
    ServerThread(AppState state, QString listen, std::optional<QString> apiToken,
                 std::shared_ptr<std::atomic_bool> shutdownFlag,
                 std::shared_ptr<std::atomic_bool> runningFlag)
        : state_(std::move(state)), listen_(std::move(listen)), apiToken_(std::move(apiToken)),
          shutdownFlag_(std::move(shutdownFlag)), runningFlag_(std::move(runningFlag))
    {
    }

    void requestShutdown()
    {
        shutdownFlag_->store(true, std::memory_order_release);
    }

protected:
    void run() override
    {
        const Result<Status> result =
            serveWithShutdown(state_, listen_, apiToken_, *shutdownFlag_);
        // 对应 Rust：任务结束时 running_flag.store(false, SeqCst)，绑定/监听失败只写日志。
        runningFlag_->store(false, std::memory_order_seq_cst);
        if (result.isErr()) {
            Log::error(QStringLiteral("HTTP API server error: %1").arg(result.error()));
        }
    }

private:
    AppState state_;
    QString listen_;
    std::optional<QString> apiToken_;
    std::shared_ptr<std::atomic_bool> shutdownFlag_;
    std::shared_ptr<std::atomic_bool> runningFlag_;
};

} // namespace

struct ApiServerController::Inner {
    AppState state;
    QString listen;
    std::optional<QString> apiToken;
    std::shared_ptr<std::atomic_bool> running = std::make_shared<std::atomic_bool>(false);
    std::mutex mutex;
    std::shared_ptr<std::atomic_bool> shutdownFlag;
    ServerThread *thread = nullptr;
    /// 旧线程要在它真的退出后才销毁；停不掉就先搁这里（对应 Rust 让 task 自然结束）。
    std::vector<std::unique_ptr<ServerThread>> retired;

    ~Inner()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (shutdownFlag) {
            shutdownFlag->store(true, std::memory_order_release);
        }
        if (thread != nullptr) {
            thread->requestShutdown();
            thread->wait(3000);
        }
        running->store(false, std::memory_order_seq_cst);
    }
};

ApiServerController::ApiServerController(AppState state, QString listen,
                                         std::optional<QString> apiToken)
    : inner_(std::make_shared<Inner>())
{
    inner_->state = std::move(state);
    inner_->listen = std::move(listen);
    inner_->apiToken = std::move(apiToken);
}

bool ApiServerController::isRunning() const
{
    return inner_->running->load(std::memory_order_relaxed);
}

QString ApiServerController::listenAddr() const
{
    return inner_->listen;
}

std::optional<QString> ApiServerController::token() const
{
    return inner_->apiToken;
}

Status ApiServerController::start()
{
    std::lock_guard<std::mutex> lock(inner_->mutex);
    if (inner_->running->load(std::memory_order_relaxed)) {
        return Status::succeeded();
    }
    // 回收已经结束的旧线程（QThread 必须在停止后才能销毁）。
    inner_->retired.erase(
        std::remove_if(inner_->retired.begin(), inner_->retired.end(),
                       [](const std::unique_ptr<ServerThread> &thread) {
                           if (thread->isFinished()) {
                               return true;
                           }
                           return false;
                       }),
        inner_->retired.end());

    auto shutdownFlag = std::make_shared<std::atomic_bool>(false);
    inner_->shutdownFlag = shutdownFlag;
    inner_->running->store(true, std::memory_order_seq_cst);
    ServerThread *thread = new ServerThread(inner_->state, inner_->listen, inner_->apiToken,
                                           shutdownFlag, inner_->running);
    inner_->thread = thread;
    thread->start();
    return Status::succeeded();
}

Status ApiServerController::stop()
{
    std::lock_guard<std::mutex> lock(inner_->mutex);
    ServerThread *thread = inner_->thread;
    inner_->thread = nullptr;
    if (thread != nullptr) {
        thread->requestShutdown();
        if (thread->isFinished()) {
            delete thread;
        } else {
            inner_->retired.push_back(std::unique_ptr<ServerThread>(thread));
        }
    }
    if (inner_->shutdownFlag) {
        inner_->shutdownFlag->store(true, std::memory_order_release);
    }
    inner_->running->store(false, std::memory_order_seq_cst);
    return Status::succeeded();
}

} // namespace Api
