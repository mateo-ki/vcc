#include "proxy/ProxyManager.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <algorithm>

#include "core/Ids.h"
#include "core/JsonUtil.h"
#include "core/Log.h"
#include "export/ClashProxy.h"
#include "export/ClashYamlExport.h"
#include "geo/Geo.h"
#include "kernel/KernelManager.h"
#include "net/HttpClient.h"
#include "platform/ProcessControl.h"
#include "proxy/Diagnostics.h"
#include "proxy/SystemProxySession.h"
#include "proxy_probe/TestUrls.h"
#include "sysproxy/TerminalProxy.h"

// sing-box / xray 的配置生成由并行的移植任务提供。
namespace Proxy {
Result<Json> buildSingBoxConfig(const Models::Node &node, MihomoConfigOptions options);
Result<Json> buildXrayConfig(const Models::Node &node, MihomoConfigOptions options);
}

namespace Proxy {

namespace {

constexpr qint64 kNanosPerMilli = 1000000LL;

QString kernelName(const std::optional<Kernel::Kind> &kernel)
{
    return kernel.has_value() ? Kernel::kindDisplayName(*kernel)
                              : QStringLiteral("代理内核");
}

void sleepMs(int milliseconds)
{
    QThread::msleep(static_cast<unsigned long>(milliseconds));
}

bool connectToLocalPort(quint16 port, int timeoutMs)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(timeoutMs);
}

bool childRunning(QProcess *child)
{
    return child && child->state() != QProcess::NotRunning;
}

/// 对应 Rust 的 `child.try_wait()`：返回"已经退出"这一事实。
bool childHasExited(QProcess *child, QString *exitText)
{
    if (!child) {
        return false;
    }
    if (child->state() == QProcess::Running) {
        return false;
    }
    if (child->exitStatus() == QProcess::NormalExit) {
        *exitText = QStringLiteral("exit status: %1").arg(child->exitCode());
    } else {
        *exitText = QStringLiteral("exit code: 0x%1 (崩溃)")
                        .arg(child->exitCode(), 8, 16, QLatin1Char('0'));
    }
    return true;
}

Status killChild(QProcess *child)
{
    if (!child) {
        return Status::succeeded();
    }
    if (child->state() == QProcess::Running) {
        child->kill();
    }
    if (!child->waitForFinished(3000)) {
        return Status::failed(QStringLiteral("等待退出失败"));
    }
    return Status::succeeded();
}

/// 内核日志全文（诊断只取最后一行或 msg= 片段，不需要截断）。
QString readKernelLog(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}


/// 等端口就绪；内核中途退出就算失败。
bool waitForPort(QProcess *child, quint16 port, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        QString exitText;
        if (childHasExited(child, &exitText)) {
            return false;
        }
        const qint64 remaining = timeoutMs - clock.elapsed();
        const int connectTimeout =
            static_cast<int>(std::min<qint64>(remaining, kStartupPortConnectTimeoutMs));
        if (connectToLocalPort(port, connectTimeout)) {
            return true;
        }
        sleepMs(20);
    }
    return false;
}


}

int kernelBatchSize(Kernel::Kind kind)
{
    return kind == Kernel::Kind::Mihomo ? 256 : 64;
}

quint16 kernelReadinessPort(Kernel::Kind kind, const MihomoConfigOptions &options)
{
    return kind == Kernel::Kind::Mihomo ? options.controllerPort : options.mixedPort;
}

QVector<quint16> kernelRequiredPorts(Kernel::Kind kind, const MihomoConfigOptions &options)
{
    if (kind == Kernel::Kind::Mihomo) {
        return {options.mixedPort, options.controllerPort};
    }
    return {options.mixedPort};
}

Result<Json> buildKernelConfig(Kernel::Kind kind, const Models::Node &node,
                              MihomoConfigOptions options)
{
    switch (kind) {
    case Kernel::Kind::Mihomo:
        return buildMihomoConfig(node, options);
    case Kernel::Kind::SingBox:
        return buildSingBoxConfig(node, options);
    case Kernel::Kind::Xray:
        return buildXrayConfig(node, options);
    }
    return Result<Json>::withError(QStringLiteral("未知内核"));
}

Result<QString> serializeKernelConfig(Kernel::Kind kind, const Json &config)
{
    // mihomo 吃 YAML，sing-box/xray 吃缩进 JSON（与 serde_json::to_string_pretty 同形）。
    if (kind == Kernel::Kind::Mihomo) {
        return Result<QString>::withValue(Export::jsonToYaml(config));
    }
    return Result<QString>::withValue(QString::fromStdString(config.dump(2)));
}

void ProxyManager::Runtime::applyOptions(MihomoConfigOptions incoming)
{
    // 一次性控制器密钥和 url-test 候选都不持久化，界面传进来的副本里它们是空的：
    // 直接覆盖会让鉴权失效、自动选路丢候选，所以空值沿用当前运行时的。
    if (incoming.controllerSecret.isEmpty()) {
        incoming.controllerSecret = options.controllerSecret;
    }
    if (incoming.urlTestNodes.isEmpty()) {
        incoming.urlTestNodes = options.urlTestNodes;
    }
    options = std::move(incoming);
}

ProxyManager::ProxyManager(MihomoConfigOptions options)
{
    // 控制端口默认无鉴权：同机任意进程都能 PUT /configs 热改规则/DNS/节点。
    // 每次进程启动生成一次性密钥，GUI 侧的控制器调用统一带 Bearer。
    if (options.controllerSecret.isEmpty()) {
        options.controllerSecret = Ids::newUuidV4Simple();
    }
    runtime_.options = options;
    runtime_.status = ProxyRuntimeStatus::stopped(options);
}

bool ProxyManager::isRunning() const
{
    QMutexLocker locker(&runtimeMutex_);
    return runtime_.status.state == ProxyState::Running
        || runtime_.status.state == ProxyState::Degraded;
}

ProxyRuntimeStatus ProxyManager::status()
{
    QElapsedTimer waitClock;
    waitClock.start();
    std::optional<ProxyWorkDir> cleanup;
    ProxyRuntimeStatus snapshot;
    {
        QMutexLocker locker(&runtimeMutex_);
        // 取证：GUI 线程卡顿时，这里等锁的毫秒数就是元凶。startWithPath 目前全程
        // 持有 runtimeMutex_（内含 waitForPort/verifyLocalProxy 等秒级阻塞），
        // 代理启动/切换期间每一次 status() 都会被它冻住。
        const qint64 waitedMs = waitClock.elapsed();
        if (waitedMs > 50) {
            Log::warn(QStringLiteral("status() 等待 runtimeMutex_ %1 ms（代理正在启动/切换？）")
                          .arg(waitedMs));
        }
        QString exitText;
        if (runtime_.child && childHasExited(runtime_.child.get(), &exitText)) {
            runtime_.child.reset();
            const QString message = kernelName(runtime_.status.kernel)
                + QStringLiteral(" 已退出: ") + exitText;
            runtime_.status.state =
                runtime_.systemProxySnapshot.has_value() ? ProxyState::Degraded
                                                        : ProxyState::Failed;
            runtime_.status.localProxyReady = false;
            runtime_.status.systemProxyEnabled = runtime_.systemProxySnapshot.has_value();
            runtime_.status.lastError = message;
            runtime_.status.message = message;
            cleanup = std::move(runtime_.workDir);
        }
        snapshot = runtime_.status;
    }
    if (cleanup && cleanup->isValid()) {
        cleanup->cleanup();
    }
    return snapshot;
}

MihomoConfigOptions ProxyManager::config()
{
    QMutexLocker locker(&runtimeMutex_);
    return runtime_.options;
}

int ProxyManager::setUrlTestCandidates(const QVector<Models::Node> &nodes)
{
    const QVector<Json> proxies = Export::nodesToClashProxies(nodes);
    QMutexLocker locker(&runtimeMutex_);
    runtime_.options.urlTestNodes = proxies;
    return proxies.size();
}

void ProxyManager::markFailed(Runtime &runtime, const QString &message)
{
    runtime.status.state = ProxyState::Failed;
    runtime.status.localProxyReady = false;
    runtime.status.lastError = message;
    runtime.status.message = message;
}

void ProxyManager::setFailed(const QString &message)
{
    QMutexLocker locker(&runtimeMutex_);
    runtime_.status.state = ProxyState::Failed;
    runtime_.status.message = message;
}

Status ProxyManager::updateConfig(const MihomoConfigOptions &options)
{
    const Status valid = options.validate();
    if (valid.isErr()) {
        return valid;
    }
    QMutexLocker operation(&operationMutex_);
    QMutexLocker locker(&runtimeMutex_);
    if (!runtime_.status.configEditable()) {
        return Status::failed(QStringLiteral("请先停止代理再修改运行配置"));
    }
    runtime_.applyOptions(options);
    runtime_.status.mode = options.mode;
    runtime_.status.mixedPort = options.mixedPort;
    runtime_.status.controllerPort = options.controllerPort;
    runtime_.status.controllerSecret = options.controllerSecret;
    return Status::succeeded();
}

Result<bool> ProxyManager::putConfigs(const QString &configPath, const QString &configText,
                                     quint16 controllerPort, const QString &controllerSecret,
                                     QString *failure)
{
    Net::Request request;
    request.url = QStringLiteral("http://127.0.0.1:%1/configs?force=true").arg(controllerPort);
    request.method = QByteArrayLiteral("PUT");
    request.timeoutMs = 1500;
    request.userAgent = QStringLiteral("vpn-parser/1.0");
    if (!controllerSecret.isEmpty()) {
        request.headers.append({QStringLiteral("Authorization"),
                                QStringLiteral("Bearer %1").arg(controllerSecret)});
    }
    Json payload;
    payload["path"] = configPath.toStdString();
    payload["payload"] = configText.toStdString();
    request.body = QByteArray::fromStdString(payload.dump());
    request.contentType = QStringLiteral("application/json");

    const Result<Net::Response> response = Net::send(request);
    if (response.isErr()) {
        if (failure) {
            *failure = response.error();
        }
        return Result<bool>::withValue(false);
    }
    if (!response.value().isSuccess()) {
        if (failure) {
            *failure = QStringLiteral("API 返回 HTTP %1").arg(response.value().status);
        }
        return Result<bool>::withValue(false);
    }
    return Result<bool>::withValue(true);
}

Result<bool> ProxyManager::reloadConfig(const MihomoConfigOptions &options)
{
    const Status valid = options.validate();
    if (valid.isErr()) {
        return Result<bool>::withError(valid.error());
    }
    QMutexLocker operation(&operationMutex_);

    QString configPath;
    quint16 controllerPort = 0;
    Models::Node node;
    QString secret;
    {
        QMutexLocker locker(&runtimeMutex_);
        const ProxyState state = runtime_.status.state;
        if (state != ProxyState::Running && state != ProxyState::Degraded) {
            runtime_.applyOptions(options);
            runtime_.status.mode = options.mode;
            runtime_.status.mixedPort = options.mixedPort;
            runtime_.status.controllerPort = options.controllerPort;
            runtime_.status.controllerSecret = options.controllerSecret;
            return Result<bool>::withValue(true);
        }
        if (runtime_.status.kernel != Kernel::Kind::Mihomo) {
            runtime_.applyOptions(options);
            return Result<bool>::withValue(false);
        }
        if (!childRunning(runtime_.child.get())) {
            runtime_.applyOptions(options);
            return Result<bool>::withValue(false);
        }
        if (!runtime_.workDir || !runtime_.currentNode) {
            runtime_.applyOptions(options);
            return Result<bool>::withValue(false);
        }
        configPath = runtime_.workDir->configPath();
        controllerPort = runtime_.options.controllerPort;
        node = *runtime_.currentNode;
        secret = runtime_.options.controllerSecret;
    }

    QElapsedTimer clock;
    clock.start();
    const Result<Json> config = buildKernelConfig(Kernel::Kind::Mihomo, node, options);
    if (config.isErr()) {
        Log::warn(QStringLiteral("构建热重载分流规则配置失败: %1").arg(config.error()));
        return Result<bool>::withValue(false);
    }
    const Result<QString> text = serializeKernelConfig(Kernel::Kind::Mihomo, config.value());
    if (text.isErr()) {
        Log::warn(QStringLiteral("序列化热重载分流规则配置失败: %1").arg(text.error()));
        return Result<bool>::withValue(false);
    }
    {
        QFile file(configPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Log::warn(QStringLiteral("写入热重载分流规则配置文件失败: %1")
                          .arg(file.errorString()));
            return Result<bool>::withValue(false);
        }
        file.write(text.value().toUtf8());
        file.close();
    }

    QString failure;
    const Result<bool> pushed =
        putConfigs(configPath, text.value(), controllerPort, secret, &failure);
    if (pushed.isErr()) {
        Log::warn(QStringLiteral("请求 Mihomo 热重载 API 失败: %1").arg(pushed.error()));
        return Result<bool>::withValue(false);
    }
    if (!pushed.value()) {
        Log::warn(QStringLiteral("Mihomo 热重载 API 返回失败: %1").arg(failure));
        return Result<bool>::withValue(false);
    }
    const quint64 elapsedMs = clock.elapsed();
    {
        QMutexLocker locker(&runtimeMutex_);
        runtime_.applyOptions(options);
        runtime_.status.message = QStringLiteral("Mihomo 运行中（分流规则已热重载生效，耗时 %1 ms）")
                                     .arg(elapsedMs);
    }
    Log::info(QStringLiteral("Mihomo 分流规则热重载生效成功，耗时 %1 ms").arg(elapsedMs));
    return Result<bool>::withValue(true);
}

Status ProxyManager::start(const Models::Node &node)
{
    return connect(node, std::nullopt);
}

Status ProxyManager::connect(const Models::Node &node,
                             std::optional<Kernel::Preference> kernelOverride)
{
    QMutexLocker operation(&operationMutex_);
    bool running = false;
    {
        QMutexLocker locker(&runtimeMutex_);
        const ProxyState state = runtime_.status.state;
        running = state == ProxyState::Running || state == ProxyState::Degraded;
    }
    if (running) {
        return restart(node, kernelOverride);
    }
    return startWithPreference(node, std::nullopt, kernelOverride);
}

/// 端口占用检查：4 次尝试，中间按 Rust 的节奏退避并在第二次清残留内核。
Status ensurePortAvailable(quint16 port)
{
    QString lastError;
    for (int attempt = 0; attempt < 4; ++attempt) {
        QTcpServer server;
        if (server.listen(QHostAddress::LocalHost, port)) {
            server.close();
            return Status::succeeded();
        }
        lastError = server.errorString();
        if (attempt == 0) {
            sleepMs(60);
        } else if (attempt == 1) {
            const int cleaned = Platform::killOrphanProxyCores();
            if (cleaned > 0) {
                Log::info(QStringLiteral("清理了 %1 个残留代理内核进程以释放端口 %2")
                              .arg(cleaned)
                              .arg(port));
            }
            sleepMs(120);
        } else if (attempt == 2) {
            sleepMs(100);
        }
    }
    return Status::failed(
        QStringLiteral("代理端口 %1 已被占用: %2").arg(port).arg(
            lastError.isEmpty() ? QStringLiteral("未知错误") : lastError));
}

/// 经本地 mixed 端口真取一次 generate_204：必须是 204 才算通。
Status verifyLocalProxy(quint16 port, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    QStringList lastErrors;
    while (clock.elapsed() < timeoutMs) {
        const qint64 remaining = timeoutMs - clock.elapsed();
        if (remaining <= 0) {
            break;
        }
        const int attemptTimeout = static_cast<int>(std::min<qint64>(remaining, 900));
        Net::Request request;
        request.url = ProxyProbe::kProxyConnectivityTestUrl;
        request.method = QByteArrayLiteral("GET");
        request.timeoutMs = attemptTimeout;
        request.connectTimeoutMs = 600;
        request.proxyUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        request.userAgent = QStringLiteral("vpn-parser/1.0");
        const Result<Net::Response> response = Net::send(request);
        if (response.isOk() && response.value().status == 204) {
            return Status::succeeded();
        }
        if (response.isOk()) {
            lastErrors = {QStringLiteral("%1 返回 HTTP %2，预期 204")
                              .arg(request.url)
                              .arg(response.value().status)};
        } else {
            lastErrors = {response.error()};
        }
        const qint64 left = timeoutMs - clock.elapsed();
        if (left <= 0) {
            break;
        }
        sleepMs(static_cast<int>(std::min<qint64>(left, 40)));
    }
    if (lastErrors.isEmpty()) {
        lastErrors.append(QStringLiteral("验证超过 %1 ms").arg(timeoutMs));
    }
    return Status::failed(QStringLiteral("本地代理无法访问测试地址: %1")
                              .arg(lastErrors.join(QStringLiteral("；"))));
}

Status ProxyManager::startWithKernelPath(const Models::Node &node, Kernel::Kind kernel,
                                        const QString &executablePath)
{
    return startWithPath(node, kernel, executablePath, std::nullopt);
}

Status ProxyManager::startWithPreference(const Models::Node &node,
                                         std::optional<SysProxy::Snapshot> preservedSnapshot,
                                         std::optional<Kernel::Preference> kernelOverride)
{
    MihomoConfigOptions options;
    {
        QMutexLocker locker(&runtimeMutex_);
        options = runtime_.options;
    }
    const Kernel::Preference preference =
        kernelOverride.has_value() ? *kernelOverride : options.kernelPreference;
    const Result<Kernel::Kind> selected = Kernel::selectProxyKernelPreference(
        node.protocol, preference, options.mode == ProxyMode::System);
    if (selected.isErr()) {
        setFailed(selected.error());
        return Status::failed(selected.error());
    }
    const std::optional<QString> path = Kernel::findKernelExecutable(selected.value());
    if (!path.has_value()) {
        const QString message =
            QStringLiteral("未找到 %1 可执行文件").arg(Kernel::kindDisplayName(selected.value()));
        setFailed(message);
        return Status::failed(message);
    }
    return startWithPath(node, selected.value(), *path, preservedSnapshot);
}

Status ProxyManager::startWithPath(const Models::Node &node, Kernel::Kind kernel,
                                  const QString &executablePath,
                                  std::optional<SysProxy::Snapshot> preservedSnapshot)
{
    QElapsedTimer startupClock;
    startupClock.start();

    QMutexLocker locker(&runtimeMutex_);
    const ProxyState state = runtime_.status.state;
    if (state == ProxyState::Starting || state == ProxyState::Running
        || state == ProxyState::Stopping) {
        return Status::failed(QStringLiteral("代理正在运行或切换状态"));
    }
    if (runtime_.options.mode == ProxyMode::Tun && !Platform::isElevated()) {
        return Status::failed(QStringLiteral("TUN 模式需要管理员权限"));
    }

    // 分流数据库没备好就别急着起内核：半截库会让内核直接拒绝启动，而且报出来的
    // 是一句要人复制才能看懂的长错误。这里缺就补（镜像优先，其次系统代理）。
    if (kernel == Kernel::Kind::Mihomo) {
        bool usesGeo = false;
        for (const TrafficRule &rule : runtime_.options.rules) {
            if (rule.usesGeo()) {
                usesGeo = true;
                break;
            }
        }
        if (usesGeo) {
            const QString dir = Geo::mihomoDir();
            if (!Geo::geoReady(dir)) {
                const QString systemProxy = SysProxy::currentSystemProxyUrl();
                const Result<QString> provisioned =
                    Geo::provision(dir, systemProxy.isNull() ? QString() : systemProxy, false);
                if (provisioned.isErr()) {
                    return Status::failed(provisioned.error());
                }
            }
        }
    }

    runtime_.status.state = ProxyState::Starting;
    runtime_.status.kernel = kernel;
    runtime_.status.nodeId = node.id;
    runtime_.currentNode = node;
    runtime_.status.message =
        QStringLiteral("正在启动 %1").arg(Kernel::kindDisplayName(kernel));
    runtime_.status.localProxyReady = false;
    runtime_.status.lastError = QString();

    const QVector<quint16> ports = kernelRequiredPorts(kernel, runtime_.options);
    for (const quint16 port : ports) {
        const Status available = ensurePortAvailable(port);
        if (available.isErr()) {
            markFailed(runtime_, available.error());
            return available;
        }
    }

    const Result<Json> config = buildKernelConfig(kernel, node, runtime_.options);
    if (config.isErr()) {
        runtime_.status.state = ProxyState::Failed;
        runtime_.status.message = config.error();
        return Status::failed(config.error());
    }
    const Result<QString> configText = serializeKernelConfig(kernel, config.value());
    if (configText.isErr()) {
        const QString message = QStringLiteral("%1 生成 %2 配置失败")
                                    .arg(Kernel::kindDisplayName(kernel), configText.error());
        markFailed(runtime_, message);
        return Status::failed(message);
    }
    Result<ProxyWorkDir> workDir = ProxyWorkDir::create(kernel);
    if (workDir.isErr()) {
        const QString message = QStringLiteral("创建 %1 工作目录失败: %2")
                                    .arg(Kernel::kindDisplayName(kernel), workDir.error());
        markFailed(runtime_, message);
        return Status::failed(message);
    }
    const QString path = workDir.value().configPath();
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            workDir->cleanup();
            const QString message =
                QStringLiteral("写入 %1 配置失败: %2").arg(Kernel::kindDisplayName(kernel),
                                                          file.errorString());
            markFailed(runtime_, message);
            return Status::failed(message);
        }
        file.write(configText.value().toUtf8());
        file.close();
    }

    // 这里刻意不跑 dry-run 校验：起一个重进程要花 300-800ms。配置真有问题时
    // 内核会立刻退出，wait_for_port 会把诊断日志读出来。
    auto child = std::make_unique<QProcess>();
    child->setProgram(executablePath);
    QStringList arguments;
    const QString dataRoot = workDir.value().dataRoot();
    const QString logPath = workDir.value().logPath();
    switch (kernel) {
    case Kernel::Kind::Mihomo:
        arguments << QStringLiteral("-d") << dataRoot << QStringLiteral("-f") << path;
        break;
    case Kernel::Kind::SingBox:
        arguments << QStringLiteral("run") << QStringLiteral("-D") << workDir.value().root()
                  << QStringLiteral("-c") << path;
        break;
    case Kernel::Kind::Xray: {
        const QString assetDir =
            QDir(Kernel::managedKernelRoot()).filePath(Kernel::kindDirectoryName(kernel));
        arguments << QStringLiteral("run") << QStringLiteral("-c") << path;
        child->setWorkingDirectory(assetDir);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XRAY_LOCATION_ASSET"), assetDir);
        child->setProcessEnvironment(environment);
        break;
    }
    }
    child->setArguments(arguments);
    child->setProcessChannelMode(QProcess::MergedChannels);
    child->setStandardOutputFile(logPath, QIODevice::Append);
    child->start();
    if (!child->waitForStarted(5000)) {
        workDir->cleanup();
        const QString message = QStringLiteral("启动 %1 失败: %2")
                                    .arg(Kernel::kindDisplayName(kernel), child->errorString());
        runtime_.status.state = ProxyState::Failed;
        runtime_.status.message = message;
        return Status::failed(message);
    }
    Platform::assignChildProcessId(static_cast<quint32>(child->processId()));
    Log::info(QStringLiteral("代理启动阶段完成: process_spawned, kernel=%1, elapsed_ms=%2")
                  .arg(Kernel::kindDisplayName(kernel))
                  .arg(startupClock.elapsed()));
    runtime_.status.localProxyReady = true;

    for (const quint16 port : ports) {
        const qint64 elapsed = startupClock.elapsed();
        const int remaining =
            static_cast<int>(std::max<qint64>(0, kStartupTotalTimeoutMs - elapsed));
        if (!waitForPort(child.get(), port, remaining)) {
            killChild(child.get());
            const QString detail = readKernelLog(logPath);
            workDir->cleanup();
            const QString message =
                detail.trimmed().isEmpty()
                ? QStringLiteral("%1 启动后端口 %2 未就绪").arg(Kernel::kindDisplayName(kernel))
                        .arg(port)
                : QStringLiteral("%1 启动失败: %2").arg(Kernel::kindDisplayName(kernel),
                                                        lastNonemptyLine(detail));
            runtime_.status.state = ProxyState::Failed;
            runtime_.status.message = message;
            return Status::failed(message);
        }
    }
    Log::info(QStringLiteral("代理启动阶段完成: port_ready, kernel=%1, elapsed_ms=%2")
                  .arg(Kernel::kindDisplayName(kernel))
                  .arg(startupClock.elapsed()));

    if (kVerifyConnectivityOnStart) {
        const qint64 remaining =
            std::max<qint64>(0, kStartupTotalTimeoutMs - startupClock.elapsed());
        const qint64 reserve = runtime_.options.mode == ProxyMode::System
                && !preservedSnapshot.has_value()
            ? kStartupSystemProxyReserveMs
            : 0;
        const int verifyTimeout =
            static_cast<int>(std::min<qint64>(std::max<qint64>(0, remaining - reserve),
                                             kStartupVerifyTimeoutMs));
        if (verifyTimeout <= 0) {
            killChild(child.get());
            workDir->cleanup();
            const QString message = QStringLiteral("%1 启动超过 %2 秒，节点验证未执行")
                                        .arg(Kernel::kindDisplayName(kernel))
                                        .arg(kStartupTotalTimeoutMs / 1000);
            markFailed(runtime_, message);
            return Status::failed(message);
        }
        const Status verified = verifyLocalProxy(runtime_.options.mixedPort, verifyTimeout);
        if (verified.isErr()) {
            const QString detail = readKernelLog(logPath);
            const QString diagnostic = formatKernelDiagnostic(detail);
            if (diagnostic.contains(QStringLiteral("规则加载中"))
                || diagnostic.contains(QStringLiteral("自动建立连接"))
                || diagnostic.contains(QStringLiteral("start initial configuration"))) {
                if (runtime_.options.mode == ProxyMode::System) {
                    if (preservedSnapshot) {
                        runtime_.systemProxySnapshot = preservedSnapshot;
                    } else if (!runtime_.systemProxySnapshot) {
                        const QString server =
                            QStringLiteral("127.0.0.1:%1").arg(runtime_.options.mixedPort);
                        std::optional<quint16> terminalPort;
                        if (runtime_.options.terminalProxy) {
                            terminalPort = runtime_.options.mixedPort;
                        }
                        const Result<SysProxy::Snapshot> activated =
                            activateSystemProxy(server, terminalPort);
                        if (activated.isErr()) {
                            return Status::failed(activated.error());
                        }
                        runtime_.systemProxySnapshot = activated.value();
                    }
                }
                runtime_.status.state = ProxyState::Running;
                runtime_.status.localProxyReady = true;
                runtime_.status.systemProxyEnabled = runtime_.systemProxySnapshot.has_value();
                runtime_.status.lastError = QString();
                runtime_.status.message =
                    QStringLiteral("%1 运行中（初次连接自动建立中）")
                        .arg(Kernel::kindDisplayName(kernel));
                runtime_.child = std::move(child);
                runtime_.workDir = std::move(workDir.value());
                return Status::succeeded();
            }
            const QString message = detail.trimmed().isEmpty()
                ? QStringLiteral("节点连通性验证失败: %1").arg(verified.error())
                : QStringLiteral("节点连通性验证失败: %1; %2: %3")
                      .arg(verified.error(), Kernel::kindDisplayName(kernel), diagnostic);
            if (runtime_.options.mode == ProxyMode::System) {
                if (preservedSnapshot) {
                    runtime_.systemProxySnapshot = preservedSnapshot;
                } else if (!runtime_.systemProxySnapshot) {
                    const QString server =
                        QStringLiteral("127.0.0.1:%1").arg(runtime_.options.mixedPort);
                    std::optional<quint16> terminalPort;
                    if (runtime_.options.terminalProxy) {
                        terminalPort = runtime_.options.mixedPort;
                    }
                    const Result<SysProxy::Snapshot> activated =
                        activateSystemProxy(server, terminalPort);
                    if (activated.isErr()) {
                        return Status::failed(activated.error());
                    }
                    runtime_.systemProxySnapshot = activated.value();
                }
            }
            // 本地端口已就绪但外部不通：内核继续跑，进入降级状态，系统代理不还原。
            runtime_.status.state = ProxyState::Degraded;
            runtime_.status.systemProxyEnabled = runtime_.systemProxySnapshot.has_value();
            runtime_.status.lastError = message;
            runtime_.status.message =
                QStringLiteral("%1 降级运行: %2").arg(Kernel::kindDisplayName(kernel), message);
            runtime_.child = std::move(child);
            runtime_.workDir = std::move(workDir.value());
            return Status::succeeded();
        }
        Log::info(QStringLiteral("代理启动阶段完成: connectivity_verified, kernel=%1, elapsed_ms=%2")
                      .arg(Kernel::kindDisplayName(kernel))
                      .arg(startupClock.elapsed()));
    }

    if (runtime_.options.mode == ProxyMode::System) {
        if (startupClock.elapsed() >= kStartupTotalTimeoutMs) {
            killChild(child.get());
            workDir->cleanup();
            const QString message = QStringLiteral("%1 启动超过 %2 秒，系统代理未启用")
                                        .arg(Kernel::kindDisplayName(kernel))
                                        .arg(kStartupTotalTimeoutMs / 1000);
            markFailed(runtime_, message);
            return Status::failed(message);
        }
        if (preservedSnapshot) {
            runtime_.systemProxySnapshot = preservedSnapshot;
        } else {
            const QString server = QStringLiteral("127.0.0.1:%1").arg(runtime_.options.mixedPort);
            std::optional<quint16> terminalPort;
            if (runtime_.options.terminalProxy) {
                terminalPort = runtime_.options.mixedPort;
            }
            const Result<SysProxy::Snapshot> activated =
                activateSystemProxy(server, terminalPort);
            if (activated.isErr()) {
                killChild(child.get());
                workDir->cleanup();
                const QString message =
                    QStringLiteral("启用系统代理失败: %1").arg(activated.error());
                markFailed(runtime_, message);
                return Status::failed(message);
            }
            runtime_.systemProxySnapshot = activated.value();
        }
        Log::info(QStringLiteral("代理启动阶段完成: system_proxy_enabled, kernel=%1, elapsed_ms=%2")
                      .arg(Kernel::kindDisplayName(kernel))
                      .arg(startupClock.elapsed()));
    }

    const quint64 elapsedMs = startupClock.elapsed();
    runtime_.status.state = ProxyState::Running;
    runtime_.status.localProxyReady = true;
    runtime_.status.systemProxyEnabled = runtime_.systemProxySnapshot.has_value();
    runtime_.status.lastError = QString();
    runtime_.status.message = QStringLiteral("%1 运行中（启动耗时 %2 ms）")
                                  .arg(Kernel::kindDisplayName(kernel))
                                  .arg(elapsedMs);
    runtime_.child = std::move(child);
    runtime_.workDir = std::move(workDir.value());
    return Status::succeeded();
}

Result<std::optional<SysProxy::Snapshot>> ProxyManager::stopWithSystemProxyRestore(
    bool restoreProxy)
{
    std::unique_ptr<QProcess> child;
    std::optional<ProxyWorkDir> workDir;
    std::optional<SysProxy::Snapshot> snapshot;
    MihomoConfigOptions options;
    std::optional<Kernel::Kind> kernel;
    {
        QMutexLocker locker(&runtimeMutex_);
        kernel = runtime_.status.kernel;
        runtime_.status.state = ProxyState::Stopping;
        runtime_.status.message =
            QStringLiteral("正在停止 %1").arg(kernelName(kernel));
        child = std::move(runtime_.child);
        workDir = std::move(runtime_.workDir);
        snapshot = runtime_.systemProxySnapshot;
        runtime_.systemProxySnapshot.reset();
        options = runtime_.options;
    }
    const QString name = kernelName(kernel);

    QStringList errors;
    if (child) {
        if (childRunning(child.get())) {
            if (killChild(child.get()).isErr()) {
                errors << QStringLiteral("停止 %1 失败").arg(name);
            }
        } else if (child->exitStatus() != QProcess::NormalExit) {
            errors << QStringLiteral("读取 %1 状态失败").arg(name);
        }
    }
    if (workDir && workDir->isValid()) {
        const Status cleaned = workDir->cleanup();
        if (cleaned.isErr()) {
            errors << QStringLiteral("清理 %1 工作目录失败: %2").arg(name, cleaned.error());
        }
    }

    std::optional<SysProxy::Snapshot> preserved;
    if (restoreProxy) {
        if (snapshot) {
            const Status restored = restoreSystemProxy(*snapshot, options.terminalProxy);
            if (restored.isErr()) {
                errors << QStringLiteral("恢复系统代理失败: %1").arg(restored.error());
            }
        }
    } else {
        preserved = snapshot;
    }

    if (!errors.isEmpty()) {
        if (preserved) {
            const Status restored = restoreSystemProxy(*preserved, options.terminalProxy);
            if (restored.isErr()) {
                errors << QStringLiteral("停止失败后恢复系统代理失败: %1").arg(restored.error());
            }
        }
        const QString message = errors.join(QStringLiteral("；"));
        QMutexLocker locker(&runtimeMutex_);
        markFailed(runtime_, message);
        return Result<std::optional<SysProxy::Snapshot>>::withError(message);
    }

    QMutexLocker locker(&runtimeMutex_);
    runtime_.status = ProxyRuntimeStatus::stopped(options);
    runtime_.currentNode = std::nullopt;
    return Result<std::optional<SysProxy::Snapshot>>::withValue(preserved);
}

Status ProxyManager::stop()
{
    QMutexLocker operation(&operationMutex_);
    const Result<std::optional<SysProxy::Snapshot>> stopped = stopWithSystemProxyRestore(true);
    return stopped.isOk() ? Status::succeeded() : Status::failed(stopped.error());
}

Result<bool> ProxyManager::tryHotReloadMihomo(const Models::Node &node)
{
    QString configPath;
    quint16 controllerPort = 0;
    QString secret;
    MihomoConfigOptions options;
    {
        QMutexLocker locker(&runtimeMutex_);
        const ProxyState state = runtime_.status.state;
        if (state != ProxyState::Running && state != ProxyState::Degraded) {
            return Result<bool>::withValue(false);
        }
        if (runtime_.status.kernel != Kernel::Kind::Mihomo) {
            return Result<bool>::withValue(false);
        }
        if (!childRunning(runtime_.child.get()) || !runtime_.workDir) {
            return Result<bool>::withValue(false);
        }
        configPath = runtime_.workDir->configPath();
        controllerPort = runtime_.options.controllerPort;
        secret = runtime_.options.controllerSecret;
        options = runtime_.options;
    }

    QElapsedTimer clock;
    clock.start();
    const Result<Json> config = buildKernelConfig(Kernel::Kind::Mihomo, node, options);
    if (config.isErr()) {
        Log::warn(QStringLiteral("构建热重载配置失败: %1").arg(config.error()));
        return Result<bool>::withValue(false);
    }
    const Result<QString> text = serializeKernelConfig(Kernel::Kind::Mihomo, config.value());
    if (text.isErr()) {
        Log::warn(QStringLiteral("序列化热重载配置失败: %1").arg(text.error()));
        return Result<bool>::withValue(false);
    }
    {
        QFile file(configPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Log::warn(QStringLiteral("写入热重载配置文件失败: %1").arg(file.errorString()));
            return Result<bool>::withValue(false);
        }
        file.write(text.value().toUtf8());
        file.close();
    }

    QString failure;
    const Result<bool> pushed = putConfigs(configPath, text.value(), controllerPort, secret,
                                           &failure);
    if (pushed.isErr() || !pushed.value()) {
        Log::warn(QStringLiteral("Mihomo 热重载 API 失败，回退进程重启: %1").arg(failure));
        return Result<bool>::withValue(false);
    }
    const quint64 elapsedMs = clock.elapsed();
    QMutexLocker locker(&runtimeMutex_);
    runtime_.status.nodeId = node.id;
    runtime_.currentNode = node;
    runtime_.status.state = ProxyState::Running;
    runtime_.status.localProxyReady = true;
    runtime_.status.lastError = QString();
    runtime_.status.message = QStringLiteral("Mihomo 运行中（平滑切换至「%1」，耗时 %2 ms）")
                                  .arg(node.name)
                                  .arg(elapsedMs);
    Log::info(QStringLiteral("Mihomo 热重载平滑切换节点成功: %1, %2 ms")
                  .arg(node.id)
                  .arg(elapsedMs));
    return Result<bool>::withValue(true);
}

Status ProxyManager::switchNode(const Models::Node &node)
{
    QMutexLocker operation(&operationMutex_);
    const Result<bool> reloaded = tryHotReloadMihomo(node);
    if (reloaded.isErr()) {
        return Status::failed(reloaded.error());
    }
    if (reloaded.value()) {
        return Status::succeeded();
    }
    return restart(node, std::nullopt);
}

Status ProxyManager::restart(const Models::Node &node,
                             std::optional<Kernel::Preference> kernelOverride)
{
    QElapsedTimer clock;
    clock.start();
    const Result<std::optional<SysProxy::Snapshot>> stopped = stopWithSystemProxyRestore(false);
    if (stopped.isErr()) {
        return Status::failed(stopped.error());
    }
    std::optional<SysProxy::Snapshot> snapshot = stopped.value();
    Log::info(QStringLiteral("代理重启阶段完成: old_kernel_stopped, elapsed_ms=%1")
                  .arg(clock.elapsed()));

    const Status started = startWithPreference(node, snapshot, kernelOverride);
    if (started.isOk()) {
        const quint64 elapsedMs = clock.elapsed();
        QMutexLocker locker(&runtimeMutex_);
        if (runtime_.status.kernel.has_value()) {
            runtime_.status.message = QStringLiteral("%1 运行中（切换耗时 %2 ms）")
                                          .arg(Kernel::kindDisplayName(*runtime_.status.kernel))
                                          .arg(elapsedMs);
        }
        return Status::succeeded();
    }
    if (snapshot) {
        // 切换失败时系统代理继续开着：否则浏览器直接断网，而用户以为是自己点错了。
        QMutexLocker locker(&runtimeMutex_);
        runtime_.systemProxySnapshot = snapshot;
        runtime_.status.state = ProxyState::Degraded;
        runtime_.status.localProxyReady = false;
        runtime_.status.systemProxyEnabled = true;
        runtime_.status.lastError = started.error();
        runtime_.status.message =
            QStringLiteral("切换失败，系统代理保持开启: %1").arg(started.error());
    }
    return started;
}

Result<QString> ProxyManager::recentLog(int maxChars)
{
    QString path;
    {
        QMutexLocker locker(&runtimeMutex_);
        if (runtime_.workDir) {
            path = runtime_.workDir->logPath();
        }
    }
    if (path.isEmpty()) {
        return Result<QString>::withValue(QString());
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QString>::withError(file.errorString());
    }
    const QString text = QString::fromUtf8(file.readAll());
    if (text.length() <= maxChars) {
        return Result<QString>::withValue(text);
    }
    return Result<QString>::withValue(text.right(maxChars));
}

std::pair<bool, quint64> ProxyManager::probeHealth(int timeoutMs)
{
    quint16 port = 0;
    {
        QMutexLocker locker(&runtimeMutex_);
        port = runtime_.options.mixedPort;
    }
    QElapsedTimer clock;
    clock.start();
    Net::Request request;
    request.url = ProxyProbe::kProxyConnectivityTestUrl;
    request.timeoutMs = timeoutMs;
    request.connectTimeoutMs = std::min(timeoutMs, 1500);
    request.proxyUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    request.userAgent = QStringLiteral("vpn-parser/1.0 (+https://localhost)");
    const Result<Net::Response> response = Net::send(request);
    const bool ok = response.isOk() && response.value().isSuccess();
    return {ok, static_cast<quint64>(clock.elapsed())};
}

}
