#include "app/AppContext.h"

#include <QMetaType>

#include <QUuid>

#include "api/ApiGateways.h"
#include "api/ApiServerController.h"
#include "core/Log.h"
#include "db/Database.h"
#include "proxy/MihomoConfig.h"
#include "proxy/ProxyManager.h"

namespace App {

AppContext::AppContext()
    : tasks_(std::make_shared<Tasks::TaskTracker>())
{
}

AppContext::~AppContext() = default;

void AppContext::runAsync(Continuation work, Continuation onUi)
{
    QtConcurrent::run([this, work, onUi]() {
        work();
        // 排队回 GUI 线程：等价于 Rust 那边往 BgMsg 通道塞一条消息，由界面轮询取出。
        QMetaObject::invokeMethod(this, onUi, Qt::QueuedConnection);
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
    context->proxy_ = std::make_shared<Proxy::ProxyManager>(configOptions);

    const Status loaded = context->reloadSettings();
    if (loaded.isErr()) {
        return Result<std::shared_ptr<AppContext>>::withError(loaded.error());
    }

    Api::AppState state;
    state.db = context->database_;
    state.tasks = context->tasks_;
    state.proxy = Api::makeProxyGateway(context->proxy_);
    state.kernel = Api::makeKernelGateway();

    QString token;
    if (options.apiToken.has_value() && !options.apiToken->trimmed().isEmpty()) {
        token = options.apiToken->trimmed();
    } else {
        const QString envToken = qEnvironmentVariable("VPN_PARSER_API_TOKEN").trimmed();
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
