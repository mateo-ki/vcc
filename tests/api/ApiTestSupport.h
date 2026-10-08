#pragma once

#include <QTemporaryDir>
#include <QVector>

#include <memory>
#include <optional>

#include "api/ApiDepends.h"
#include "api/ApiHandlers.h"
#include "api/ApiRouter.h"
#include "api/HttpMessage.h"
#include "db/Database.h"
#include "tasks/TaskTracker.h"

// API 层的测试夹具：全部 handler 都用假网关注入，不碰 socket、不碰网络、不起进程。
namespace ApiTest {

class FakeCollector : public Api::CollectorGateway {
public:
    Api::Result<Models::FetchResult> fetchOne(const QString &sourceId) override
    {
        ++fetchOneCalls;
        lastFetchId = sourceId;
        if (fetchOneFails) {
            return Api::Result<Models::FetchResult>::withError(fetchOneError);
        }
        Models::FetchResult result;
        result.sourceId = sourceId;
        result.sourceName = QStringLiteral("fake");
        result.success = true;
        result.message = QStringLiteral("ok");
        result.nodeCount = 3;
        return Api::Result<Models::FetchResult>::withValue(result);
    }

    Api::Result<QVector<Models::FetchResult>> fetchAllEnabled() override
    {
        ++fetchAllCalls;
        if (fetchAllFails) {
            return Api::Result<QVector<Models::FetchResult>>::withError(fetchAllError);
        }
        Models::FetchResult result;
        result.sourceId = QStringLiteral("s-1");
        result.success = true;
        return Api::Result<QVector<Models::FetchResult>>::withValue({result});
    }

    void discoverGistsInBackground(const QString &query, int maxPages) override
    {
        ++discoverCalls;
        lastQuery = query;
        lastMaxPages = maxPages;
    }

    int fetchOneCalls = 0;
    int fetchAllCalls = 0;
    int discoverCalls = 0;
    QString lastFetchId;
    QString lastQuery;
    int lastMaxPages = 0;
    bool fetchOneFails = false;
    bool fetchAllFails = false;
    QString fetchOneError = QStringLiteral("source not found");
    QString fetchAllError = QStringLiteral("boom");
};

class FakeSpeedtest : public Api::SpeedtestGateway {
public:
    Api::Result<Models::LatencyResult> testOne(const QString &nodeId,
                                               const Api::SpeedtestOptions &options) override
    {
        ++oneCalls;
        lastConcurrency = options.concurrency;
        lastTimeoutMs = options.timeoutMs;
        if (fails) {
            return Api::Result<Models::LatencyResult>::withError(error);
        }
        Models::LatencyResult result;
        result.id = nodeId;
        result.ok = true;
        result.latencyMs = 42;
        return Api::Result<Models::LatencyResult>::withValue(result);
    }

    Api::Result<Models::SpeedTestSummary> testMany(
        const std::optional<QVector<QString>> &nodeIds,
        const Api::SpeedtestOptions &options) override
    {
        ++manyCalls;
        lastIds = nodeIds;
        lastBatchConcurrency = options.concurrency;
        Models::SpeedTestSummary summary;
        summary.total = nodeIds.has_value() ? nodeIds->size() : 7;
        summary.ok = summary.total;
        return Api::Result<Models::SpeedTestSummary>::withValue(summary);
    }

    int oneCalls = 0;
    int manyCalls = 0;
    quint32 lastConcurrency = 0;
    quint32 lastBatchConcurrency = 0;
    quint32 lastTimeoutMs = 0;
    std::optional<QVector<QString>> lastIds;
    bool fails = false;
    QString error = QStringLiteral("node not found");
};

class FakeKernel : public Api::KernelGateway {
public:
    QList<Kernel::Info> detectAll() override
    {
        ++detectCalls;
        QList<Kernel::Info> infos;
        Kernel::Info mihomo;
        mihomo.kind = Kernel::Kind::Mihomo;
        mihomo.available = true;
        mihomo.version = QStringLiteral("v1.18.0");
        mihomo.message = QStringLiteral("ok");
        Kernel::Info xray;
        xray.kind = Kernel::Kind::Xray;
        xray.available = false;
        xray.message = QStringLiteral("missing");
        infos << mihomo << xray;
        return infos;
    }

    Api::Result<QList<Kernel::ReleaseInfo>> checkUpdatesWithProxy(
        const std::optional<QString> &proxyUrl) override
    {
        ++releaseCalls;
        lastProxyUrl = proxyUrl;
        if (releasesFail) {
            return Api::Result<QList<Kernel::ReleaseInfo>>::withError(releasesError);
        }
        Kernel::ReleaseInfo info;
        info.kind = Kernel::Kind::Mihomo;
        info.latestVersion = QStringLiteral("v1.19.0");
        info.updateAvailable = true;
        return Api::Result<QList<Kernel::ReleaseInfo>>::withValue({info});
    }

    Api::Result<Kernel::UpdateResult> updateWithOptions(
        Kernel::Kind kind, const std::optional<QString> &proxyUrl, Kernel::Source source) override
    {
        ++updateCalls;
        lastKind = kind;
        lastSource = source;
        lastUpdateProxyUrl = proxyUrl;
        if (updateFail) {
            return Api::Result<Kernel::UpdateResult>::withError(updateError);
        }
        Kernel::UpdateResult result;
        result.kind = kind;
        result.version = QStringLiteral("v1.19.0");
        result.message = QStringLiteral("installed");
        return Api::Result<Kernel::UpdateResult>::withValue(result);
    }

    int detectCalls = 0;
    int releaseCalls = 0;
    int updateCalls = 0;
    std::optional<QString> lastProxyUrl;
    std::optional<QString> lastUpdateProxyUrl;
    std::optional<Kernel::Kind> lastKind;
    std::optional<Kernel::Source> lastSource;
    bool releasesFail = false;
    bool updateFail = false;
    QString releasesError = QStringLiteral("rate limited");
    QString updateError = QStringLiteral("download failed");
};

class FakeProxy : public Api::ProxyGateway {
public:
    Proxy::ProxyRuntimeStatus status() override
    {
        ++statusCalls;
        return currentStatus;
    }

    Proxy::MihomoConfigOptions config() override
    {
        ++configCalls;
        return currentConfig;
    }

    Api::Status updateConfig(const Proxy::MihomoConfigOptions &options) override
    {
        ++updateConfigCalls;
        lastAppliedConfig = options;
        if (updateConfigFails) {
            return Api::Status::failed(updateConfigError);
        }
        currentConfig = options;
        return Api::Status::succeeded();
    }

    Api::Result<QString> recentLog(int maxChars) override
    {
        ++logCalls;
        lastMaxChars = maxChars;
        if (logFails) {
            return Api::Result<QString>::withError(logError);
        }
        return Api::Result<QString>::withValue(QStringLiteral("log tail"));
    }

    Api::Status connect(const Models::Node &node, std::optional<Kernel::Preference> kernel) override
    {
        ++connectCalls;
        lastConnectedNodeId = node.id;
        lastRequestedKernel = kernel;
        if (connectFails) {
            return Api::Status::failed(connectError);
        }
        currentStatus.state = Proxy::ProxyState::Running;
        currentStatus.localProxyReady = true;
        return Api::Status::succeeded();
    }

    Api::Status stop() override
    {
        ++stopCalls;
        currentStatus.state = Proxy::ProxyState::Stopped;
        currentStatus.localProxyReady = false;
        return stopFails ? Api::Status::failed(stopError) : Api::Status::succeeded();
    }

    int statusCalls = 0;
    int configCalls = 0;
    int updateConfigCalls = 0;
    int logCalls = 0;
    int connectCalls = 0;
    int stopCalls = 0;
    int lastMaxChars = 0;
    QString lastConnectedNodeId;
    std::optional<Kernel::Preference> lastRequestedKernel;
    Proxy::MihomoConfigOptions lastAppliedConfig;
    Proxy::ProxyRuntimeStatus currentStatus;
    Proxy::MihomoConfigOptions currentConfig;
    bool updateConfigFails = false;
    QString updateConfigError = QStringLiteral("端口被占用");
    bool logFails = false;
    QString logError = QStringLiteral("no log file");
    bool connectFails = false;
    QString connectError = QStringLiteral("connection refused");
    bool stopFails = false;
    QString stopError = QStringLiteral("stop failed");
};

/// 一个完整的假 AppState：数据库是 QTemporaryDir 里的真 SQLite（不开 socket）。
struct Fixture {
    explicit Fixture(const QString &name)
    {
        tempDir = std::make_shared<QTemporaryDir>();
        if (!tempDir->isValid()) {
            return;
        }
        Result<std::shared_ptr<Database>> opened = Database::connect(
            tempDir->path() + QStringLiteral("/%1.db").arg(name));
        if (opened.isOk()) {
            db = opened.takeValue();
        }
        state.db = db;
        state.tasks = std::make_shared<Tasks::TaskTracker>();
        collector = std::make_shared<FakeCollector>();
        speedtest = std::make_shared<FakeSpeedtest>();
        kernel = std::make_shared<FakeKernel>();
        proxy = std::make_shared<FakeProxy>();
        state.collector = collector;
        state.speedtest = speedtest;
        state.kernel = kernel;
        state.proxy = proxy;
    }

    std::shared_ptr<QTemporaryDir> tempDir;
    std::shared_ptr<Database> db;
    std::shared_ptr<FakeCollector> collector;
    std::shared_ptr<FakeSpeedtest> speedtest;
    std::shared_ptr<FakeKernel> kernel;
    std::shared_ptr<FakeProxy> proxy;
    Api::AppState state;
};

inline Api::Request makeRequest(const QString &method, const QString &target,
                                const QByteArray &body = QByteArray(),
                                const QString &contentType = QString())
{
    Api::Request request;
    request.method = method;
    request.version = QStringLiteral("HTTP/1.1");
    request.target = target;
    const int queryMark = target.indexOf(QLatin1Char('?'));
    request.rawPath = queryMark < 0 ? target : target.left(queryMark);
    request.rawQuery = queryMark < 0 ? QString() : target.mid(queryMark + 1);
    request.path = request.rawPath;
    if (!request.rawQuery.isEmpty()) {
        for (const QString &pair : request.rawQuery.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
            const int separator = pair.indexOf(QLatin1Char('='));
            if (separator < 0) {
                request.queryPairs.append(qMakePair(pair, QString()));
            } else {
                request.queryPairs.append(
                    qMakePair(pair.left(separator), pair.mid(separator + 1)));
            }
        }
    }
    request.body = body;
    if (!contentType.isEmpty()) {
        request.headers.insert(QStringLiteral("content-type"), contentType);
    }
    return request;
}

inline QString bodyText(const Api::Response &response)
{
    return QString::fromUtf8(response.body);
}

inline Json bodyJson(const Api::Response &response)
{
    const Result<Json> parsed = JsonUtil::parse(bodyText(response));
    return parsed.isOk() ? parsed.value() : Json();
}

/// 拆信封：{ok, message, data}。
inline Json envelopeData(const Api::Response &response)
{
    const Json envelope = bodyJson(response);
    return envelope.contains("data") ? envelope.at("data") : Json();
}

/// proxy/mod.rs 的 is_node_connectivity_failure 认的那段标记文本（"节点连通性验证失败"），
/// 用码点拼出来，测试里绝不手打中文。
inline QString connectivityFailureMarker()
{
    static const char32_t points[] = {0x8282, 0x70B9, 0x8FDE, 0x901A, 0x6027,
                                      0x9A8C, 0x8BC1, 0x5931, 0x8D25};
    return QString::fromUcs4(points, 9);
}

/// 建一个源（直接走 Database，避免依赖 HTTP 层）。
inline QString createSource(const std::shared_ptr<Database> &db, const QString &name,
                           const QString &url)
{
    Models::CreateSource input;
    input.name = name;
    input.urlTemplate = url;
    input.enabled = true;
    const Result<Models::Source> created = db->createSource(input);
    return created.isOk() ? created.value().id : QString();
}

inline Models::Node makeNode(const QString &id, const QString &sourceId, const QString &protocol,
                            const QString &server, int port)
{
    Models::Node node;
    node.id = id;
    node.sourceId = sourceId;
    node.protocol = protocol;
    node.name = QStringLiteral("node-%1").arg(id);
    node.server = server;
    node.port = port;
    node.raw = QStringLiteral("%1://%2:%3").arg(protocol, server).arg(port);
    node.config = Json::object();
    node.config["type"] = protocol.toStdString();
    node.config["name"] = node.name.toStdString();
    node.config["server"] = server.toStdString();
    node.config["port"] = port;
    node.createdAt = TimeUtil::OptionalUtc::now();
    node.updatedAt = TimeUtil::OptionalUtc::now();
    return node;
}

} // namespace ApiTest
