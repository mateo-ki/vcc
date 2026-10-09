#pragma once

#include <QObject>

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
class AppContext : public QObject {
    Q_OBJECT

public:
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
    void runAsync(Continuation work, Continuation onUi);

    /// shared_ptr 的删除器要在 std 的上下文里能析构，所以析构函数得公开；
    /// 实现留在 .cpp。
    ~AppContext() override;

signals:
    void settingsChanged();
    void proxyStatusChanged();

private:
    AppContext();

    std::shared_ptr<Database> database_;
    std::shared_ptr<Proxy::ProxyManager> proxy_;
    std::shared_ptr<Tasks::TaskTracker> tasks_;
    std::shared_ptr<Api::ApiServerController> apiServer_;
    Models::AppSettings settings_;
};

}
