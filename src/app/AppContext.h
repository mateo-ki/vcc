#pragma once

#include <QObject>
#include <atomic>

#include <QtConcurrent>

#include <functional>
#include <memory>

#include "core/Result.h"
#include "models/Settings.h"
#include "tasks/TaskTracker.h"

class Database;

namespace Api {
class ApiServerController;
}

namespace Proxy {
class ProxyManager;
}

namespace App {

/// worker 线程跑完活儿之后回到 GUI 线程要执行的东西。
using Continuation = std::function<void()>;

/// 界面与本地 API 共用的那一份运行态：库、代理编排器、任务台账、设置缓存。
/// 对应 Rust 里 main.rs 建好之后 Arc 给 gui 与 api 的那组对象。
class AppContext : public QObject, public std::enable_shared_from_this<AppContext> {
    Q_OBJECT

public:

    /// 全量测速一次只许跑一趟。它有两个入口（底栏「测速全部」和节点池页那个），
    /// 各自只禁用了自己那颗按钮，实测能被同时起两份：14448 个节点 × 128 并发 × 2
    /// = 262 线程 / 684MB，机器直接卡成幻灯片。对应 Rust 的 self.busy 那道门。
    /// 抢到返回 true；作业结束时必须 endFullSpeedtest()，否则这道门永远关着。
    bool tryBeginFullSpeedtest()
    {
        bool expected = false;
        return fullSpeedtestRunning_.compare_exchange_strong(expected, true);
    }
    void endFullSpeedtest() { fullSpeedtestRunning_.store(false); }
    bool fullSpeedtestRunning() const { return fullSpeedtestRunning_.load(); }

    struct Options {
        QString dbPath;
        QString listenAddr = QStringLiteral("127.0.0.1:8080");
        std::optional<QString> apiToken;
    };

    static Result<std::shared_ptr<AppContext>> create(const Options &options);

    Database &database();
    std::shared_ptr<Database> databasePtr() const;
    Proxy::ProxyManager &proxy();
    std::shared_ptr<Proxy::ProxyManager> proxyPtr() const;
    Tasks::TaskTracker &tasks();
    std::shared_ptr<Tasks::TaskTracker> tasksPtr() const;
    Api::ApiServerController &apiServer();
    const Api::ApiServerController &apiServer() const;
    std::shared_ptr<Api::ApiServerController> apiServerPtr() const;

    /// 设置的工作副本：界面读它，写一律走 saveSettings（落库成功后才更新缓存）。
    const Models::AppSettings &settings() const;
    Status reloadSettings();
    Result<Models::AppSettings> saveSettings(const Models::UpdateSettings &patch);

    /// 阻塞活儿（库、网络、内核进程）丢到 worker 线程，做完回 GUI 线程执行 onUi。
    /// 对应 Rust 后台线程往 BgMsg 通道里塞一条消息；要有返回值就自己捕获一个
    /// shared_ptr 的结果盒子，work 写它、onUi 读它。
    /// uiOwner 传发起这件事的那个 widget：回调里碰它的指针时，控件可能已经先一步被
    /// 销毁（关窗时正好有 30s 的拉取在跑）。传了就由这里统一兜底，别在每个调用点各写
    /// 一份 QPointer。
    void runAsync(Continuation work, Continuation onUi, QObject *uiOwner = nullptr);

    /// shared_ptr 的删除器要在 std 的上下文里能析构，所以析构函数得公开；
    /// 实现留在 .cpp。
    ~AppContext() override;

signals:
    void settingsChanged();
    void proxyStatusChanged();

private:
    AppContext();

    std::atomic<bool> fullSpeedtestRunning_{false};

    std::shared_ptr<Database> database_;
    std::shared_ptr<Proxy::ProxyManager> proxy_;
    std::shared_ptr<Tasks::TaskTracker> tasks_;
    std::shared_ptr<Api::ApiServerController> apiServer_;
    Models::AppSettings settings_;
};

}
