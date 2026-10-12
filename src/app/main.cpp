#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QThread>

#include "api/ApiServerController.h"
#include "app/AppContext.h"
#include "app/ExitCleanup.h"
#include "app/MainWindow.h"
#include "core/Log.h"
#include "gui/Theme.h"
#include "platform/ProcessControl.h"
#include "platform/SingleInstance.h"
#include "platform/Tray.h"
#include "proxy/MihomoConfig.h"
#include "proxy/ProxyManager.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("vcc"));
    app.setOrganizationName(QStringLiteral("vcc"));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon.png")));

    Log::initialize();
    Log::startup(QStringLiteral("=== Application starting ==="));

    // Rust 在写日志后立刻把当前目录切到 exe 目录：data/、bin/ 内核、startup.log 全按
    // 相对路径找，从快捷方式/服务启动时工作目录是 system32。
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (QDir::setCurrent(exeDir)) {
        Log::startup(QStringLiteral("set_current_dir: %1").arg(exeDir));
    }

    // 对应 clap 的 Cli。两个 hide=true 的参数是提权交接用的，值里可能以 '-' 开头
    // （base64url 字母表含 '-'），不照 Rust 放行就会在写日志之前 exit(2)。
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("VPN/proxy subscription collector with desktop GUI and HTTP API"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption listenOption(
        QStringLiteral("listen"), QStringLiteral("HTTP API listen address"),
        QStringLiteral("listen"), QStringLiteral("127.0.0.1:8080"));
    const QCommandLineOption dbOption(QStringLiteral("db"),
                                      QStringLiteral("SQLite database path"),
                                      QStringLiteral("db"),
                                      QStringLiteral("data/vcc.db"));
    const QCommandLineOption headlessOption(
        QStringLiteral("headless"), QStringLiteral("Headless mode: only run HTTP API (no GUI)"));
    const QCommandLineOption tokenOption(QStringLiteral("api-token"),
                                         QStringLiteral("Bearer token required by the HTTP API"),
                                         QStringLiteral("api-token"));
    QCommandLineOption elevatedConfigOption(
        QStringLiteral("elevated-proxy-config"),
        QStringLiteral("Internal: proxy config handed over by the elevated instance"),
        QStringLiteral("config"));
    elevatedConfigOption.setFlags(QCommandLineOption::HiddenFromHelp);
    QCommandLineOption startNodeOption(
        QStringLiteral("start-proxy-node"),
        QStringLiteral("Internal: node id the elevated instance should start"),
        QStringLiteral("node"));
    startNodeOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions(
        {listenOption, dbOption, headlessOption, tokenOption, elevatedConfigOption, startNodeOption});
    // base64url 的值可能以 '-' 开头，QCommandLineParser 默认会把这种值当未知选项拒掉。
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
    parser.process(app);

    Log::startup(QStringLiteral("CLI parsed: headless=%1, db=%2")
                     .arg(parser.isSet(headlessOption) ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                          parser.value(dbOption)));

    // 作业对象先建，之后所有内核子进程都挂进这个作业：主进程被杀时内核跟着死，
    // 不会留下"界面没了但代理端口还在监听"的孤儿。
    Platform::ensureJobInitialized();
    const int cleanedCores = Platform::killOrphanProxyCores();
    if (cleanedCores > 0) {
        Log::startup(
            QStringLiteral("Cleaned up %1 orphan proxy core processes.").arg(cleanedCores));
    }

    // 提权交接：父进程（普通权限那个）正在退出并释放单实例锁，子进程最多轮询 3 秒接管。
    // 不等的话它会看见"锁还被占着"然后自己退出，表现就是"点了 TUN 启动，UAC 过了，什么都没发生"。
    const bool elevatedHandoff = parser.isSet(elevatedConfigOption);
    std::optional<Platform::SingleInstanceGuard> instance;
    if (elevatedHandoff) {
        Result<std::optional<Platform::SingleInstanceGuard>> handed =
            Platform::acquireSingleInstanceForHandoff();
        if (handed.isErr()) {
            Log::startup(QStringLiteral("single instance mutex failed: %1").arg(handed.error()));
            return 1;
        }
        if (!handed.value().has_value()) {
            Log::startup(QStringLiteral("elevated handoff gave up waiting for the single instance lock"));
            return 0;
        }
        std::optional<Platform::SingleInstanceGuard> taken = handed.takeValue();
        instance = std::move(*taken);
    } else {
        Result<std::optional<Platform::SingleInstanceGuard>> acquired =
            Platform::acquireSingleInstance();
        if (acquired.isErr()) {
            Log::startup(QStringLiteral("single instance mutex failed: %1").arg(acquired.error()));
            return 1;
        }
        if (!acquired.value().has_value()) {
            Log::startup(QStringLiteral("another instance already holds the lock; waking it"));
            Platform::notifyExistingInstance();
            return 0;
        }
        std::optional<Platform::SingleInstanceGuard> taken = acquired.takeValue();
        instance = std::move(*taken);
    }

    App::AppContext::Options options;
    options.dbPath = parser.value(dbOption);
    options.listenAddr = parser.value(listenOption);
    if (parser.isSet(tokenOption)) {
        options.apiToken = parser.value(tokenOption);
    }
    const Result<std::shared_ptr<App::AppContext>> created = App::AppContext::create(options);
    if (created.isErr()) {
        Log::startup(QStringLiteral("startup failed: %1").arg(created.error()));
        return 1;
    }
    std::shared_ptr<App::AppContext> context = created.value();
    Log::startup(QStringLiteral("database ready at %1").arg(options.dbPath));

    // 对应 Rust 的退出清理：GUI 走 `on_exit`（gui/mod.rs:4635-4669），headless 走
    // `main.rs:322-331` 的 ctrl_c 分支，两条都是"进程走人之前必须把代理停一遍"。少这一条
    // 的代价今天实测到了：vcc 被结束掉之后 HKCU\Internet Settings 里 ProxyEnable 还是 1、
    // ProxyServer 还是 127.0.0.1:9980，而 9980 上已经没人监听 —— 浏览器从此连不上网，
    // 直到下一个抢到该端口的程序出现。
    // stop() 可能长时间占着 operationMutex（后台测速/启动在跑），Rust 给它 8s 上限就为这个；
    // 工作线程抓一份 shared_ptr，所以超时后放任它跑也不会碰到已析构的对象，残留内核交给
    // 下次启动的 killOrphanProxyCores —— Rust 那句超时日志说的就是这套。
    const bool headlessExit = parser.isSet(headlessOption);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [context, headlessExit]() {
        const App::ExitStopOutcome stopped =
            App::stopProxyOnExit([context]() { return context->proxy().stop(); });
        if (!stopped.completed) {
            Log::error(QStringLiteral(
                "proxy stop on GUI exit timed out after %1s; orphan core is cleaned up on next start")
                .arg(App::kExitProxyStopTimeoutMs / 1000));
        } else if (!stopped.error.isEmpty()) {
            Log::error(QStringLiteral("%1: %2")
                           .arg(headlessExit ? QStringLiteral("restore system proxy on headless exit failed")
                                             : QStringLiteral("restore system proxy on GUI exit failed"),
                                stopped.error));
        }
    });

    // 提权子进程只做代理，不接管 HTTP API：否则等于把（带鉴权令牌的）接口以管理员身份
    // 再监听一次（Rust main.rs:311 同一判断）。
    if ((parser.isSet(headlessOption) || context->settings().httpServerEnabled) && !elevatedHandoff) {
        context->apiServer().start();
    }

    if (parser.isSet(headlessOption)) {
        Log::startup(QStringLiteral("Running in headless mode."));
        if (!parser.isSet(tokenOption) && qEnvironmentVariable("VCC_API_TOKEN").isEmpty() && qEnvironmentVariable("VPN_PARSER_API_TOKEN").isEmpty()) {
            if (auto t = context->apiServer().token()) {
                Log::startup(QStringLiteral("HTTP API 访问令牌（本次启动随机生成）: %1").arg(*t));
            }
        }
        return app.exec();
    }

    Gui::Theme::apply(context->settings().uiTheme);

    Gui::MainWindow window(*context);
    if (elevatedHandoff) {
        // 父进程递过来的配置：解码 + 校验，失败就只记 ASCII 日志（这是内部交接，
        // 不是给用户看的文案，别往"中文必须与 Rust 逐字一致"那本账上添条目）。
        const auto handed = Proxy::MihomoConfigOptions::decodeHandoffConfig(
            parser.value(elevatedConfigOption));
        if (handed.isErr()) {
            Log::startup(QStringLiteral("elevated handoff config rejected: %1").arg(handed.error()));
        } else {
            const Status applied = context->proxy().updateConfig(handed.value());
            if (applied.isErr()) {
                Log::startup(QStringLiteral("elevated handoff config apply failed: %1").arg(applied.error()));
            }
        }
    }
    window.show();
    if (parser.isSet(startNodeOption)) {
        window.startHandoffNode(parser.value(startNodeOption));
    }

    return app.exec();
}
