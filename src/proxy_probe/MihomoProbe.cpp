#include "proxy_probe/MihomoProbe.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSemaphore>
#include <QString>
#include <QThread>

#include <algorithm>
#include <utility>

#include "core/Ids.h"
#include "core/JsonUtil.h"
#include "export/ClashProxy.h"
#include "export/ClashYamlExport.h"
#include "kernel/KernelManager.h"
#include "net/HttpClient.h"
#include "proxy/KernelConfigMessages.h"
#include "proxy_probe/JoinWindow.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/ProbeMessages.h"
#include "proxy_probe/TestUrls.h"

namespace ProxyProbe {
namespace {

/// /delay 探针的测试目标名，Rust 传的是 &'static str "Google"。
const QString kDelayTargetName = QStringLiteral("Google");
/// wait_for_controller 的轮询间隔（Rust 20ms）。
constexpr int kControllerPollMs = 20;
/// `mihomo -t` 一次的预算。Rust 用 `command.output()` 无限等；这里必须有个上限，
/// 否则一个卡死的校验进程会把整轮测速的线程挂住（见 runProcessToOutput）。
constexpr int kValidateBudgetMs = 30000;

/// Rust 的 `results.into_iter().flatten().collect()`：丢掉没填的槽位（正常路径上没有）。
QVector<ProbeResult> flattenResults(const QVector<std::optional<ProbeResult>> &resultSlots)
{
    QVector<ProbeResult> results;
    results.reserve(resultSlots.size());
    for (const auto &slot : resultSlots) {
        if (slot.has_value()) {
            results.append(*slot);
        }
    }
    return results;
}

/// 一个探针任务的回传：(槽位下标, 结果)，对应 Rust 的 `(proxy.index, result)`。
struct ProbeJoin {
    qsizetype index = -1;
    ProbeResult result;
};

/// isolate_invalid_proxies 一层的回传：批次本身要跟着回来，因为"下沉一半"是
/// 消费端（按完成顺序 join 的那个循环）决定的。
struct ValidationJoin {
    QVector<PreparedProxy> batch;
    Result<std::optional<QString>> outcome;
};

ProbeResult makeUnsupportedResult(const QString &id, const QString &reason)
{
    ProbeResult result;
    result.id = id;
    result.ok = false;
    result.message = Proxy::Messages::fill(Messages::mihomoConfigUnsupported(),
                                           {{"{reason}", reason}});
    return result;
}

/// Rust 的 `Err(format!("{target_name}: {error}"))` 那一层：错误文本已经带上目标名。
Result<qint64> probeMihomoTarget(const QString &url, const QString &targetName,
                                 int requestTimeoutMs)
{
    Net::Request request;
    request.url = url;
    request.method = QByteArrayLiteral("GET");
    request.timeoutMs = requestTimeoutMs;
    request.connectTimeoutMs = kControllerConnectTimeoutMs;
    // 探针配置里 secret 是空串（Rust build_mihomo_config 写死），所以不发 Authorization。
    const Result<Net::Response> response = Net::send(request);
    if (response.isErr()) {
        return Result<qint64>::withError(
            QStringLiteral("%1: %2").arg(targetName, response.error()));
    }
    const Net::Response &body = response.value();
    if (!body.isSuccess()) {
        // Rust 打印的是 `HTTP {}` 的 StatusCode Display，会带原因短语；Qt 只给了状态码。
        return Result<qint64>::withError(
            QStringLiteral("%1: HTTP %2").arg(targetName).arg(body.status));
    }
    const Result<Json> parsed = JsonUtil::parse(QString::fromUtf8(body.body));
    if (parsed.isErr()) {
        return Result<qint64>::withError(
            QStringLiteral("%1: %2").arg(targetName, parsed.error()));
    }
    const Json &value = parsed.value();
    // Rust: body.get("delay").and_then(Value::as_i64) —— 非整数（浮点/字符串/缺失）都算没返回。
    if (!value.is_object() || !value.contains("delay") || !value.at("delay").is_number_integer()) {
        return Result<qint64>::withError(delayMissingValueMessage(targetName));
    }
    const qint64 delayMs = value.at("delay").get<qint64>();
    if (delayMs <= 0) {
        return Result<qint64>::withError(delayMissingValueMessage(targetName));
    }
    return Result<qint64>::withValue(delayMs);
}

/// Rust 的 `probe_mihomo_target` 调用点在任务里：拿额度 → 编码 URL → 打一次 → 拼消息。
ProbeJoin probePreparedProxy(const PreparedProxy &proxy, quint16 controllerPort,
                             const QString &delayTimeoutText, int requestTimeoutMs,
                             QSemaphore &networkLimit)
{
    ProbeJoin join;
    join.index = proxy.index;
    join.result.id = proxy.id;
    // 对应 `network_limit.acquire_owned().await`：阻塞等到额度为止，握住到本次探针结束。
    // QSemaphore 没有 close() 这一态，所以 Rust 的 "测速并发控制已关闭" 分支到不了。
    QSemaphoreReleaser releaser(&networkLimit, 1);
    const QString url =
        mihomoDelayUrl(proxy.name, controllerPort, kProxyConnectivityTestUrl, delayTimeoutText);
    const Result<qint64> delay = probeMihomoTarget(url, kDelayTargetName, requestTimeoutMs);
    if (delay.isOk()) {
        join.result.ok = true;
        join.result.latencyMs = delay.value();
        join.result.message = delaySuccessMessage(delay.value(), kDelayTargetName);
    } else {
        join.result.ok = false;
        join.result.message = delayFailureMessage(delay.error(), kDelayTargetName);
    }
    return join;
}

BatchValidator mihomoValidator(const QString &mihomoPath, const QString &workDir)
{
    return [mihomoPath, workDir](const QVector<PreparedProxy> &batch) {
        return validateProxyBatchWithMihomo(batch, mihomoPath, workDir);
    };
}

}

PreparedProxies prepareProxies(const QVector<Models::Node> &nodes)
{
    PreparedProxies prepared;
    prepared.results.resize(nodes.size());
    for (qsizetype index = 0; index < nodes.size(); ++index) {
        const Models::Node &node = nodes.at(index);
        const std::optional<Json> converted = Export::nodeToClashProxy(node);
        if (!converted.has_value()) {
            ProbeResult result;
            result.id = node.id;
            result.ok = false;
            result.message = Proxy::Messages::fill(Messages::mihomoProtocolUnsupported(),
                                                   {{"{}", node.protocol}});
            prepared.results[index] = result;
            continue;
        }
        Json config = *converted;
        PreparedProxy proxy;
        proxy.index = index;
        proxy.id = node.id;
        // 名字带上下标：控制器按名字认代理，节点名（甚至 id）都可能撞车。
        proxy.name = QStringLiteral("probe-%1-%2").arg(index).arg(node.id);
        if (config.is_object()) {
            config["name"] = proxy.name.toStdString();
        }
        proxy.config = std::move(config);
        prepared.prepared.append(proxy);
    }
    return prepared;
}

Json buildBatchProbeConfig(const QVector<PreparedProxy> &prepared, quint16 controllerPort)
{
    Json proxies = Json::array();
    Json names = Json::array();
    for (const PreparedProxy &proxy : prepared) {
        proxies.push_back(proxy.config);
        names.push_back(proxy.name.toStdString());
    }
    Json group;
    group["name"] = "PROXY";
    group["type"] = "select";
    group["proxies"] = names;

    Json config;
    config["external-controller"] =
        QStringLiteral("127.0.0.1:%1").arg(controllerPort).toStdString();
    config["secret"] = "";
    config["allow-lan"] = false;
    config["ipv6"] = false;
    config["mode"] = "rule";
    config["log-level"] = "warning";
    config["proxies"] = proxies;
    config["proxy-groups"] = Json::array({group});
    config["rules"] = Json::array({std::string("MATCH,PROXY")});
    return config;
}

void appendBatchOutcome(QVector<ProbeResult> *results, const MihomoBatchOutcome &outcome,
                        const QString &batchError)
{
    if (results == nullptr) {
        return;
    }
    if (outcome.results.isOk()) {
        for (const ProbeResult &result : outcome.results.value()) {
            results->append(result);
        }
        return;
    }
    // 一整批失败也要每批都交回一条结果：节点绝不能因为批次炸了就凭空消失。
    for (const QString &id : outcome.ids) {
        ProbeResult result;
        result.id = id;
        result.ok = false;
        result.message = batchError;
        results->append(result);
    }
}

QString mihomoDelayUrl(const QString &proxyName, quint16 controllerPort, const QString &targetUrl,
                       const QString &timeoutMs)
{
    return QStringLiteral("http://127.0.0.1:%1/proxies/%2/delay?url=%3&timeout=%4")
        .arg(controllerPort)
        .arg(percentEncodePath(proxyName), formEncodeQuery(targetUrl),
             formEncodeQuery(timeoutMs));
}

QString delaySuccessMessage(qint64 delayMs, const QString &targetName)
{
    return Proxy::Messages::fill(Messages::mihomoDelayOk(),
                                 {{"{delay}", QString::number(delayMs)},
                                  {"{target}", targetName}});
}

QString delayFailureMessage(const QString &error, const QString &targetName)
{
    Q_UNUSED(targetName);
    // 目标名已经在 error 里（Rust 的 `{target_name}: {error}` 由 probe_mihomo_target 拼好）。
    return Proxy::Messages::fill(Messages::mihomoDelayFailed(), {{"{error}", error}});
}

QString delayMissingValueMessage(const QString &targetName)
{
    return Proxy::Messages::fill(Messages::mihomoDelayNoValue(), {{"{target_name}", targetName}});
}

Result<IsolationOutcome> isolateInvalidProxies(const QVector<PreparedProxy> &prepared,
                                               const BatchValidator &validator)
{
    IsolationOutcome outcome;
    QVector<QVector<PreparedProxy>> level;
    level.append(prepared);

    // 同一层的所有批次并发校验，只有失败的批次才下沉到下一层：这比原来一次一个
    // `mihomo -t` 的串行下降好太多，层数只有 O(log n)。
    while (!level.isEmpty()) {
        JoinWindow<ValidationJoin> window(kValidateConcurrency);
        QVector<QVector<PreparedProxy>> next;
        qsizetype cursor = 0;
        for (;;) {
            while (cursor < level.size()) {
                const QVector<PreparedProxy> batch = level.at(cursor);
                const bool spawned = window.trySpawn([batch, validator]() {
                    ValidationJoin join;
                    join.batch = batch;
                    join.outcome = validator(batch);
                    return join;
                });
                if (!spawned) {
                    break;
                }
                ++cursor;
            }
            Joined<ValidationJoin> joined;
            if (!window.joinNext(&joined)) {
                break;
            }
            if (joined.failed) {
                return Result<IsolationOutcome>::withError(
                    Proxy::Messages::fill(Messages::mihomoValidateTaskFailed(),
                                          {{"{error}", joined.error}}));
            }
            const QVector<PreparedProxy> &batch = joined.value.batch;
            if (joined.value.outcome.isErr()) {
                return Result<IsolationOutcome>::withError(joined.value.outcome.error());
            }
            const std::optional<QString> reason = joined.value.outcome.value();
            if (!reason.has_value()) {
                for (const PreparedProxy &proxy : batch) {
                    outcome.valid.append(proxy);
                }
                continue;
            }
            if (batch.size() == 1) {
                outcome.rejected.emplace_back(batch.first(), *reason);
                continue;
            }
            // Rust: let right = batch.split_off(batch.len() / 2); next.push(batch); next.push(right);
            const qsizetype half = batch.size() / 2;
            next.append(batch.first(half));
            next.append(batch.mid(half));
        }
        level = next;
    }

    std::sort(outcome.valid.begin(), outcome.valid.end(),
              [](const PreparedProxy &left, const PreparedProxy &right) {
                  return left.index < right.index;
              });
    std::sort(outcome.rejected.begin(), outcome.rejected.end(),
              [](const std::pair<PreparedProxy, QString> &left,
                 const std::pair<PreparedProxy, QString> &right) {
                  return left.first.index < right.first.index;
              });
    return Result<IsolationOutcome>::withValue(outcome);
}

Result<std::optional<QString>> validateProxyBatchWithMihomo(const QVector<PreparedProxy> &batch,
                                                             const QString &mihomoPath,
                                                             const QString &workDir)
{
    const QString path = QDir(workDir).filePath(QStringLiteral("validate-%1.yaml").arg(Ids::newUuidV4()));
    const Status written = writeTextFile(path, Export::jsonToYaml(buildBatchProbeConfig(batch, 0)));
    if (written.isErr()) {
        return Result<std::optional<QString>>::withError(written.error());
    }
    const ProcessOutput output =
        runProcessToOutput(mihomoPath,
                           {QStringLiteral("-t"), QStringLiteral("-d"), workDir,
                            QStringLiteral("-f"), path},
                           QString(), kValidateBudgetMs);
    QFile::remove(path);
    if (!output.started) {
        return Result<std::optional<QString>>::withError(output.error);
    }
    if (output.ok) {
        return Result<std::optional<QString>>::withValue(std::nullopt);
    }
    // Rust 把 stderr 和 stdout 用空格拼起来再取最后一行，顺序照抄。
    const QString diagnostic = output.stdErr + QLatin1Char(' ') + output.stdOut;
    return Result<std::optional<QString>>::withValue(compactDiagnostic(diagnostic));
}

Result<QVector<ProbeResult>> verifyNodesWithMihomoPath(const QVector<Models::Node> &nodes,
                                                      int timeoutMs, const QString &mihomoPath,
                                                      const ProgressFn &progress,
                                                      QSemaphore &networkLimit,
                                                      bool conditionalIsolation)
{
    const qsizetype total = nodes.size();
    QElapsedTimer clock;
    clock.start();

    PreparedProxies prepared = prepareProxies(nodes);
    QVector<std::optional<ProbeResult>> results = prepared.results;
    reportProgress(progress, results, total, clock.elapsed());
    QVector<PreparedProxy> live = std::move(prepared.prepared);
    if (live.isEmpty()) {
        return Result<QVector<ProbeResult>>::withValue(flattenResults(results));
    }

    Result<TempWorkDir> created = TempWorkDir::create(QStringLiteral("vpn-parser-mihomo"));
    if (created.isErr()) {
        return Result<QVector<ProbeResult>>::withError(created.error());
    }
    TempWorkDir workDir = created.takeValue();
    const BatchValidator validator = mihomoValidator(mihomoPath, workDir.root());

    // 默认先对整批配置做 `mihomo -t` 二分隔离；开了「条件隔离」就先直接测，
    // 只把失败的节点送去二分隔离，省掉一整轮配置验证。
    if (!conditionalIsolation) {
        Result<IsolationOutcome> isolated = isolateInvalidProxies(live, validator);
        if (isolated.isErr()) {
            return Result<QVector<ProbeResult>>::withError(isolated.error());
        }
        live = isolated.value().valid;
        for (const std::pair<PreparedProxy, QString> &entry : isolated.value().rejected) {
            results[entry.first.index] = makeUnsupportedResult(entry.first.id, entry.second);
        }
    }
    reportProgress(progress, results, total, clock.elapsed());
    if (live.isEmpty()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withValue(flattenResults(results));
    }

    PortReservations reservations;
    Result<quint16> controllerPort = reservations.reserve();
    if (controllerPort.isErr()) {
        return Result<QVector<ProbeResult>>::withError(controllerPort.error());
    }
    reservations.release();
    const quint16 port = controllerPort.value();

    const QString configPath = workDir.filePath(QStringLiteral("config.yaml"));
    const Status configWritten =
        writeTextFile(configPath, Export::jsonToYaml(buildBatchProbeConfig(live, port)));
    if (configWritten.isErr()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(configWritten.error());
    }
    const QString startupLog = workDir.filePath(QStringLiteral("startup.log"));
    Result<std::unique_ptr<QProcess>> started =
        startKernelProcess(mihomoPath,
                           {QStringLiteral("-d"), workDir.root(), QStringLiteral("-f"), configPath},
                           startupLog);
    if (started.isErr()) {
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(
            Proxy::Messages::fill(Messages::mihomoSpawnFailed(), {{"{}", mihomoPath}})
            + QStringLiteral(": %1").arg(started.error()));
    }
    std::unique_ptr<QProcess> child = started.takeValue();

    const int budget = controllerStartupBudgetMs(live.size());
    if (!waitUntilPortReady(*child, port, budget, kControllerPollMs)) {
        killProcess(child.get());
        const QString detail = compactDiagnostic(readLogFile(startupLog));
        workDir.remove();
        return Result<QVector<ProbeResult>>::withError(
            Proxy::Messages::fill(Messages::mihomoStartupFailed(), {{"{detail}", detail}}));
    }

    const int delayTimeoutMs = clampDelayTimeoutMs(timeoutMs);
    const QString delayTimeoutText = QString::number(delayTimeoutMs);
    const int requestTimeoutMs = timeoutMs + kControllerRequestSlackMs;

    // 增量计数：每完成一个探针就全量重扫 results 的话，8192 的批次要迭代一千三百万次，
    // 进度回调节本身把测速拖平了。
    ProgressCounter counter;
    counter.done = countedResults(results);
    for (const auto &slot : results) {
        if (slot.has_value() && slot->ok) {
            ++counter.ok;
        }
    }

    JoinWindow<ProbeJoin> window(kProxyConcurrency);
    qsizetype cursor = 0;
    for (;;) {
        while (cursor < live.size()) {
            // 指针而不是副本：live 在这里之后不再增删（只读 results/counter 会变），
            // 而窗口析构会等所有任务结束，所以任务体引用它一定有效。省掉每个探针一次
            // Json 深拷贝（Rust 侧是 move，本来也不拷）。
            const PreparedProxy *proxy = &live.at(cursor);
            const bool spawned = window.trySpawn([proxy, port, delayTimeoutText, requestTimeoutMs,
                                                  &networkLimit]() {
                return probePreparedProxy(*proxy, port, delayTimeoutText, requestTimeoutMs,
                                          networkLimit);
            });
            if (!spawned) {
                break;
            }
            ++cursor;
        }
        Joined<ProbeJoin> joined;
        if (!window.joinNext(&joined)) {
            break;
        }
        if (joined.failed) {
            // Rust 这里 `if let Ok(..)`：任务本身炸掉就丢掉这个槽位，最后由
            // "结果数量不一致"那条检查兜住，不会静默少一个节点。
            continue;
        }
        const bool wasOk = joined.value.result.ok;
        results[joined.value.index] = joined.value.result;
        counter.markResult(wasOk);
        emitProgress(progress, counter.done, counter.ok, total, clock.elapsed());
    }

    killProcess(child.get());
    child.reset();

    if (conditionalIsolation) {
        QVector<PreparedProxy> failed;
        for (const PreparedProxy &proxy : live) {
            const std::optional<ProbeResult> &slot = results.at(proxy.index);
            if (slot.has_value() && !slot->ok) {
                failed.append(proxy);
            }
        }
        if (!failed.isEmpty()) {
            Result<IsolationOutcome> isolated = isolateInvalidProxies(failed, validator);
            if (isolated.isErr()) {
                workDir.remove();
                return Result<QVector<ProbeResult>>::withError(isolated.error());
            }
            for (const std::pair<PreparedProxy, QString> &entry : isolated.value().rejected) {
                results[entry.first.index] = makeUnsupportedResult(entry.first.id, entry.second);
            }
        }
    }
    workDir.remove();

    if (results.size() != total || std::any_of(results.begin(), results.end(),
                                               [](const std::optional<ProbeResult> &slot) {
                                                   return !slot.has_value();
                                               })) {
        return Result<QVector<ProbeResult>>::withError(Messages::mihomoResultCountMismatch());
    }
    return Result<QVector<ProbeResult>>::withValue(flattenResults(results));
}

Result<QVector<ProbeResult>> verifyNodesWithMihomo(const QVector<Models::Node> &nodes,
                                                   int timeoutMs, const ProgressFn &progress,
                                                   QSemaphore &networkLimit, size_t batchSize,
                                                   bool conditionalIsolation)
{
    if (nodes.isEmpty()) {
        return Result<QVector<ProbeResult>>::withValue(QVector<ProbeResult>());
    }
    const std::optional<QString> mihomo = Kernel::findMihomoExecutable();
    if (!mihomo.has_value()) {
        return Result<QVector<ProbeResult>>::withError(Messages::mihomoNotFound());
    }
    const QString executable = *mihomo;
    const qsizetype total = nodes.size();
    QElapsedTimer clock;
    clock.start();
    const QVector<QVector<Models::Node>> batches =
        chunkNodes(nodes, qsizetype(std::max<size_t>(1, batchSize)));
    const qsizetype batchTotal = batches.size();

    JoinWindow<MihomoBatchOutcome> window(kMihomoBatchConcurrency);
    QVector<ProbeResult> results;
    results.reserve(total);
    qsizetype cursor = 0;
    qsizetype completedBatches = 0;
    for (;;) {
        while (cursor < batches.size()) {
            const QVector<Models::Node> batch = batches.at(cursor);
            QVector<QString> ids;
            ids.reserve(batch.size());
            for (const Models::Node &node : batch) {
                ids.append(node.id);
            }
            const bool spawned = window.trySpawn([batch, ids, timeoutMs, executable, &networkLimit,
                                                  conditionalIsolation]() {
                MihomoBatchOutcome outcome;
                outcome.ids = ids;
                // 批次级进度才是这一层要报的：单节点进度在 Rust 这里也是 None。
                outcome.results = verifyNodesWithMihomoPath(batch, timeoutMs, executable,
                                                            ProgressFn(), networkLimit,
                                                            conditionalIsolation);
                return outcome;
            });
            if (!spawned) {
                break;
            }
            ++cursor;
        }
        Joined<MihomoBatchOutcome> joined;
        if (!window.joinNext(&joined)) {
            break;
        }
        if (joined.failed) {
            return Result<QVector<ProbeResult>>::withError(
                Proxy::Messages::fill(Messages::mihomoBatchFailed(), {{"{error}", joined.error}}));
        }
        appendBatchOutcome(
            &results, joined.value,
            Proxy::Messages::fill(Messages::mihomoBatchFailed(),
                                  {{"{error}", joined.value.results.error()}}));
        ++completedBatches;
        if (progress) {
            qsizetype ok = 0;
            for (const ProbeResult &result : results) {
                if (result.ok) {
                    ++ok;
                }
            }
            emitProgress(progress, results.size(), ok, total, clock.elapsed(), completedBatches,
                         batchTotal);
        }
    }
    return Result<QVector<ProbeResult>>::withValue(results);
}

Result<QVector<ProbeResult>> verifyNodesWithMihomoProgress(const QVector<Models::Node> &nodes,
                                                           int timeoutMs,
                                                           const ProgressFn &progress)
{
    QSemaphore networkLimit(kProxyConcurrency);
    return verifyNodesWithMihomo(nodes, timeoutMs, progress, networkLimit, kMihomoBatchSize, false);
}

Result<QVector<ProbeResult>> verifyNodesWithMihomo(const QVector<Models::Node> &nodes,
                                                   int timeoutMs)
{
    return verifyNodesWithMihomoProgress(nodes, timeoutMs, ProgressFn());
}

}
