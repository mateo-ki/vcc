#include "app/AppContext.h"

#include <QApplication>
#include <QMetaType>
#include <QPointer>
#include <QTimer>

#include <QUuid>

#include "api/ApiGateways.h"
#include "api/ApiServerController.h"
#include "core/Log.h"
#include "db/Database.h"
#include "proxy/MihomoConfig.h"
#include "proxy/ProxyManager.h"

namespace App {

namespace {
/// 比两条 idle_timeout（30s/60s）都密，才能在一分钟内把空闲连接收掉；单次回收只是
/// 关掉句柄，成本可以忽略。
constexpr int kPoolReapIntervalMs = 15 * 1000;
}

AppContext::AppContext()
    : tasks_(std::make_shared<Tasks::TaskTracker>())
{
}

AppContext::~AppContext()
{
    // 等待正在执行的后台 worker（如 runAsync 中读写数据库）安全结束，
    // 防止析构后后台线程仍在访问已释放的 ConnectionPool 造成越界或崩溃。
    // 只在 GUI 线程上等：runAsync 现在让 work 也持一份 shared_ptr，最后一个引用很可能
    // 是池线程里那个 lambda 放掉的 —— 在池线程上 waitForDone 等于让池线程等自己，
    // 最多白等满 1.5s，而那种情况下也没人需要这次等待。
    if (qApp != nullptr && QThread::currentThread() == qApp->thread()) {
        QThreadPool::globalInstance()->waitForDone(1500);
    }
}

void AppContext::runAsync(Continuation work, Continuation onUi, QObject *uiOwner)
{
    // Use QPointer so that if this AppContext is destroyed before the async
    // work completes, the onUi callback is safely skipped.
    // The completion is posted to qApp (which always outlives any AppContext
    // created in tests or production) rather than to `this` directly, which
    // prevents the UB of invokeMethod on an already-destroyed QObject.
    //
    // self 是给 work 用的保命符：以前只靠 ~AppContext 里那一次 waitForDone(1500)，
    // 而这里派发的活儿有 30s 的订阅拉取、8s 的关代理、5s 的测速 —— 1.5s 一到对象就没了，
    // work 里捕获的 &context_ / database_ / proxy_ 全变悬垂。有了这份引用，
    // "活儿没干完对象就不会没"，代价只是最坏情况多活几十秒。
    std::shared_ptr<AppContext> self = shared_from_this();
    QPointer<AppContext> guard(this);
    QPointer<QObject> ownerGuard(uiOwner);
    const bool hasOwner = (uiOwner != nullptr);
    QtConcurrent::run([self = std::move(self), guard, ownerGuard, hasOwner, work, onUi]() {
        work();
        // 排队回 GUI 线程：等价于 Rust 那边往 BgMsg 通道塞一条消息，由界面轮询取出。
        // 投递到 qApp（永远存活），再在 GUI 线程上二次判空：上下文可能已经没人引用了，
        // 而发起这件事的 widget 更可能已经随窗口一起销毁（没传 owner 就没这一道判空）。
        QMetaObject::invokeMethod(qApp, [guard, ownerGuard, hasOwner, onUi]() {
            if (guard == nullptr) {
                return;
            }
            if (hasOwner && ownerGuard == nullptr) {
                return;
            }
            onUi();
        }, Qt::QueuedConnection);
    });
}

Result<std::shared_ptr<AppContext>> AppContext::create(const Options &options)
{
    Result<std::shared_ptr<Database>> opened = Database::connect(options.dbPath);
    if (opened.isErr()) {
        return Result<std::shared_ptr<AppContext>>::withError(opened.error());
    }
    auto context = std::shared_ptr<AppContext>(new AppContext());
    context->database_ = opened.takeValue();

    const Status migrated = context->database_->migrate();
    if (migrated.isErr()) {
        return Result<std::shared_ptr<AppContext>>::withError(migrated.error());
    }

    // 内核选项存在库里（一整个 JSON），解析失败时 fromSavedJsonText 自己回默认值。
    Proxy::MihomoConfigOptions configOptions;
    const Result<QString> saved = context->database_->getProxyConfigJsonText();
    if (saved.isOk()) {
        const Result<Proxy::MihomoConfigOptions> parsed =
            Proxy::MihomoConfigOptions::fromSavedJsonText(saved.value());
        if (parsed.isOk()) {
            configOptions = parsed.value();
        }
    }
    // 规则自动最优（vcc 独有）：库里的规则表如果没有中国大陆直连那一段，国内站会整站落到
    // `MATCH,PROXY` 绕海外（实测 B 站因此慢约 3 倍、吞吐被限在节点那 3.5 MB/s）。空库直接给
    // 推荐 15 条；非空只把缺的兜底条追加到末尾，用户自己的规则和顺序一条不动。
    const QVector<Proxy::TrafficRule> autoRules =
        Proxy::withMissingFallbackRules(configOptions.rules);
    const bool rulesAutoFixed = autoRules.size() != configOptions.rules.size();
    configOptions.rules = autoRules;
    context->proxy_ = std::make_shared<Proxy::ProxyManager>(configOptions);

    const Status loaded = context->reloadSettings();
    if (loaded.isErr()) {
        return Result<std::shared_ptr<AppContext>>::withError(loaded.error());
    }

    // 补出来的规则要落盘，否则界面（它读的是运行时 options）和库会不一致，下次启动又算一遍。
    if (rulesAutoFixed) {
        const Result<QString> savedText = configOptions.toSavedJsonText();
        if (savedText.isOk()) {
            const Status saved = context->database_->setProxyConfigJsonText(savedText.value());
            if (saved.isErr()) {
                Log::warn(QStringLiteral("auto rules persist failed: %1").arg(saved.error()));
            }
        }
    }

    // 连接池空闲回收。Rust 那边是 sqlx 池自带的后台维护任务（idle_timeout 60s/30s），
    // C++ 这边没人顺手做，所以挂一条定时器代它：不回收的话 几十个订阅源 × 2 条 SQLite
    // 连接（每条 ~2MB 页缓存）会一直压在那儿。定时器是 AppContext 的子对象，随它一起没，
    // 所以回调里抓裸 Database* 是安全的；没有事件循环的场景（单元测试/探针）它不触发。
    Database *reapTarget = context->database_.get();
    auto *poolReaper = new QTimer(context.get());
    QObject::connect(poolReaper, &QTimer::timeout, context.get(), [reapTarget]() {
        reapTarget->reapIdlePools();
    });
    poolReaper->start(kPoolReapIntervalMs);

    Api::AppState state;
    state.db = context->database_;
    state.tasks = context->tasks_;
    state.proxy = Api::makeProxyGateway(context->proxy_);
    state.kernel = Api::makeKernelGateway();

    QString token;
    if (options.apiToken.has_value() && !options.apiToken->trimmed().isEmpty()) {
        token = options.apiToken->trimmed();
    } else {
        QString envToken = qEnvironmentVariable("VCC_API_TOKEN").trimmed();
        if (envToken.isEmpty()) {
            envToken = qEnvironmentVariable("VPN_PARSER_API_TOKEN").trimmed();
        }
        if (!envToken.isEmpty()) {
            token = envToken;
        } else {
            token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }

    const QString listen = options.listenAddr.trimmed().isEmpty()
                               ? QStringLiteral("127.0.0.1:8080")
                               : options.listenAddr.trimmed();

    context->apiServer_ = std::make_shared<Api::ApiServerController>(
        state, listen, token);

    return Result<std::shared_ptr<AppContext>>::withValue(context);
}

Database &AppContext::database()
{
    return *database_;
}

std::shared_ptr<Database> AppContext::databasePtr() const
{
    return database_;
}

Proxy::ProxyManager &AppContext::proxy()
{
    return *proxy_;
}

std::shared_ptr<Proxy::ProxyManager> AppContext::proxyPtr() const
{
    return proxy_;
}

Tasks::TaskTracker &AppContext::tasks()
{
    return *tasks_;
}

std::shared_ptr<Tasks::TaskTracker> AppContext::tasksPtr() const
{
    return tasks_;
}

Api::ApiServerController &AppContext::apiServer()
{
    return *apiServer_;
}

const Api::ApiServerController &AppContext::apiServer() const
{
    return *apiServer_;
}

std::shared_ptr<Api::ApiServerController> AppContext::apiServerPtr() const
{
    return apiServer_;
}

const Models::AppSettings &AppContext::settings() const
{
    return settings_;
}

Status AppContext::reloadSettings()
{
    const Result<Models::AppSettings> loaded = database_->getSettings();
    if (loaded.isErr()) {
        return Status::failed(loaded.error());
    }
    settings_ = loaded.value();
    return Status::succeeded();
}

Result<Models::AppSettings> AppContext::saveSettings(const Models::UpdateSettings &patch)
{
    const Result<Models::AppSettings> saved = database_->updateSettings(patch);
    if (saved.isErr()) {
        return Result<Models::AppSettings>::withError(saved.error());
    }
    settings_ = saved.value();
    emit settingsChanged();
    return saved;
}

}
