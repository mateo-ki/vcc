#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>

#include "api/ApiServerController.h"
#include "app/AppContext.h"
#include "app/MainWindow.h"
#include "core/Log.h"
#include "gui/Theme.h"
#include "platform/ProcessControl.h"
#include "platform/SingleInstance.h"
#include "platform/Tray.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("vpn-parser"));
    app.setOrganizationName(QStringLiteral("vpn-parser"));

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
                                      QStringLiteral("data/vpn-parser.db"));
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

    const Result<std::optional<Platform::SingleInstanceGuard>> acquired =
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

    if (parser.isSet(headlessOption) || context->settings().httpServerEnabled) {
        context->apiServer().start();
    }

    if (parser.isSet(headlessOption)) {
        Log::startup(QStringLiteral("Running in headless mode."));
        if (!parser.isSet(tokenOption) && qEnvironmentVariable("VPN_PARSER_API_TOKEN").isEmpty()) {
            if (auto t = context->apiServer().token()) {
                Log::startup(QStringLiteral("HTTP API 访问令牌（本次启动随机生成）: %1").arg(*t));
            }
        }
        return app.exec();
    }

    Gui::Theme::apply(context->settings().uiTheme);

    Gui::MainWindow window(*context);
    window.show();

    return app.exec();
}
