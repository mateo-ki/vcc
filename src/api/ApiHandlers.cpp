#include "api/ApiHandlers.h"

#include <QStringList>
#include <QVector>

#include <algorithm>

#include "api/ApiMessages.h"
#include "db/Database.h"
#include "db/DatabaseExport.h"
#include "export/ClashYamlExport.h"
#include "export/QualityExport.h"
#include "export/UriExport.h"
#include "kernel/KernelTypes.h"
#include "models/Settings.h"
#include "proxy/MihomoConfig.h"
#include "tasks/TaskTracker.h"

namespace Api {

namespace {

/// axum-core `define_rejection!` 里 Json/Query 提取失败时的固定文案前缀。
const QString kExpectedJsonContentType =
    QStringLiteral("Expected request with `Content-Type: application/json`");
const QString kJsonParseFailure = QStringLiteral("Failed to parse the request body as JSON: ");
const QString kJsonTypeFailure =
    QStringLiteral("Failed to deserialize the JSON body into the target type: ");
const QString kQueryFailure = QStringLiteral("Failed to deserialize query string: ");

/// serde_urlencoded 的 bool 解析走 `s.parse::<bool>()`，错误文本就是这个。
QString invalidBoolDetail()
{
    return QStringLiteral("provided string was not `true` or `false`");
}

/// Rust `ParseIntError` 的两种 Display。
QString invalidIntegerDetail()
{
    return QStringLiteral("invalid digit found in string");
}

QString integerOverflowDetail()
{
    return QStringLiteral("number too large to fit in target type");
}

/// Rust `ParseFloatError` 的 Display。
QString invalidFloatDetail()
{
    return QStringLiteral("invalid float literal");
}

/// serde_json 对空 body 的报错。
QString emptyBodyDetail()
{
    return QStringLiteral("EOF while parsing a value at line 1 column 0");
}

/// 对应 `struct FailedToDeserializeQueryString`（400 + 文本）。
Response queryRejected(const QString &detail)
{
    return Http::rejection(400, kQueryFailure + detail);
}

/// `str::parse::<T>()` 的严格程度：整串必须是数字，允许前导 '-'（signed 才允许），
/// 不允许空格、'+'、下划线。溢出单独报，和 Rust 的 ParseIntError 一致。
bool parseStrictInteger(const QString &text, bool allowSign, qint64 &out)
{
    if (text.isEmpty()) {
        return false;
    }
    int start = 0;
    if (text.at(0) == QLatin1Char('-')) {
        if (!allowSign) {
            return false;
        }
        start = 1;
    }
    if (start >= text.size()) {
        return false;
    }
    qint64 accumulator = 0;
    bool overflow = false;
    for (int index = start; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (!ch.isDigit()) {
            return false;
        }
        const qint64 digit = ch.unicode() - u'0';
        if (accumulator > (std::numeric_limits<qint64>::max() - digit) / 10) {
            overflow = true;
        }
        accumulator = accumulator * 10 + digit;
    }
    if (overflow) {
        out = allowSign ? std::numeric_limits<qint64>::max() : std::numeric_limits<qint64>::max();
        return false;
    }
    out = allowSign && text.at(0) == QLatin1Char('-') ? -accumulator : accumulator;
    return true;
}

struct UnsignedParse {
    bool parsed = false;
    bool overflow = false;
    quint32 value = 0;
};

UnsignedParse parseStrictUnsigned(const QString &text)
{
    UnsignedParse result;
    qint64 signedValue = 0;
    if (!parseStrictInteger(text, false, signedValue)) {
        return result;
    }
    if (signedValue > qint64(std::numeric_limits<quint32>::max())) {
        result.overflow = true;
        return result;
    }
    result.parsed = true;
    result.value = static_cast<quint32>(signedValue);
    return result;
}

/// serde 描述 JSON 值类型的名字（`invalid type: sequence, expected struct X`）。
QString serdeTypeName(const Json &value)
{
    if (value.is_null()) {
        return QStringLiteral("null");
    }
    if (value.is_boolean()) {
        return QStringLiteral("boolean");
    }
    if (value.is_string()) {
        return QStringLiteral("string");
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return QStringLiteral("integer");
    }
    if (value.is_number_float()) {
        return QStringLiteral("floating point number");
    }
    if (value.is_array()) {
        return QStringLiteral("sequence");
    }
    if (value.is_object()) {
        return QStringLiteral("map");
    }
    return QStringLiteral("value");
}

/// 提取并解析 JSON body，把 axum 的 415 / 400 / 422 三种拒绝原样搬过来。
struct JsonBody {
    Json value = Json::object();
    std::optional<Response> rejection;

    bool ok() const { return !rejection.has_value(); }
};

JsonBody extractJsonBody(const Request &request, const QString &structName)
{
    const QString contentType = request.header(QStringLiteral("content-type"));
    if (!Http::isJsonContentType(contentType)) {
        return {Json(), Http::rejection(415, kExpectedJsonContentType)};
    }
    if (request.body.isEmpty()) {
        // axum: serde_json 在空输入上报 Category::Eof → JsonSyntaxError → 422。
        return {Json(), Http::rejection(422, kJsonParseFailure + emptyBodyDetail())};
    }
    const Result<Json> parsed = JsonUtil::parse(QString::fromUtf8(request.body));
    if (parsed.isErr()) {
        // detail 来自 nlohmann 的异常文本，措辞与 serde_json 不同（状态码一致）。
        return {Json(), Http::rejection(422, kJsonParseFailure + parsed.error())};
    }
    if (!parsed.value().is_object()) {
        return {Json(),
                Http::rejection(422, kJsonTypeFailure + QStringLiteral("invalid type: %1, expected struct %2")
                                             .arg(serdeTypeName(parsed.value()), structName))};
    }
    return {parsed.value(), std::nullopt};
}

Response invalidTypeRejection(const Json &value, const QString &expected)
{
    return Http::rejection(422,
                           kJsonTypeFailure
                               + QStringLiteral("invalid type: %1, expected %2")
                                     .arg(serdeTypeName(value), expected));
}

/// 必填字符串字段：缺键 → `missing field`，类型不对 → invalid type。
bool requireString(const Json &body, const char *key, QString &out, Response &rejection)
{
    const auto found = body.find(key);
    if (found == body.end() || found->is_null()) {
        rejection = Http::rejection(
            422, kJsonTypeFailure + QStringLiteral("missing field `%1`")
                                         .arg(QString::fromLatin1(key)));
        return false;
    }
    if (!found->is_string()) {
        rejection = invalidTypeRejection(*found, QStringLiteral("a string"));
        return false;
    }
    out = QString::fromStdString(found->get<std::string>());
    return true;
}

/// KernelKind 的 serde 变体名，顺序照 Rust 的 enum 声明（`unknown variant` 提示要用）。
const QStringList &kernelKindVariants()
{
    static const QStringList variants = {QStringLiteral("mihomo"), QStringLiteral("xray"),
                                         QStringLiteral("sing_box")};
    return variants;
}

const QStringList &kernelPreferenceVariants()
{
    static const QStringList variants = {QStringLiteral("auto"), QStringLiteral("mihomo"),
                                         QStringLiteral("sing_box"), QStringLiteral("xray")};
    return variants;
}

QString unknownVariantDetail(const QString &value, const QStringList &variants)
{
    QStringList quoted;
    for (const QString &variant : variants) {
        quoted.append(QStringLiteral("`%1`").arg(variant));
    }
    return QStringLiteral("unknown variant `%1`, expected one of %2")
        .arg(value, quoted.join(QStringLiteral(", ")));
}

bool kernelKindFromJson(const Json &body, const char *key, Kernel::Kind &out, Response &rejection,
                        bool optional)
{
    const auto found = body.find(key);
    if (found == body.end() || found->is_null()) {
        if (optional) {
            return true;
        }
        rejection = Http::rejection(
            422, kJsonTypeFailure + QStringLiteral("missing field `%1`")
                                         .arg(QString::fromLatin1(key)));
        return false;
    }
    if (!found->is_string()) {
        rejection = invalidTypeRejection(*found, QStringLiteral("a string"));
        return false;
    }
    const QString text = QString::fromStdString(found->get<std::string>());
    if (!kernelKindVariants().contains(text)) {
        rejection = Http::rejection(422, kJsonTypeFailure
                                                 + unknownVariantDetail(text, kernelKindVariants()));
        return false;
    }
    out = Kernel::kindFromText(text);
    return true;
}

bool kernelPreferenceFromJson(const Json &body, const char *key,
                              std::optional<Kernel::Preference> &out, Response &rejection)
{
    const auto found = body.find(key);
    if (found == body.end() || found->is_null()) {
        return true;
    }
    if (!found->is_string()) {
        rejection = invalidTypeRejection(*found, QStringLiteral("a string"));
        return false;
    }
    const QString text = QString::fromStdString(found->get<std::string>());
    if (!kernelPreferenceVariants().contains(text)) {
        rejection =
            Http::rejection(422, kJsonTypeFailure + unknownVariantDetail(text, kernelPreferenceVariants()));
        return false;
    }
    out = Kernel::preferenceFromText(text);
    return true;
}

/// 查询串里的可选整数（usize / u32）。
bool takeQueryUnsigned(const Request &request, const QString &key, std::optional<quint32> &out,
                       QString &errorDetail)
{
    const std::optional<QString> raw = request.query(key);
    if (!raw.has_value()) {
        return true;
    }
    const UnsignedParse parsed = parseStrictUnsigned(*raw);
    if (parsed.overflow) {
        errorDetail = integerOverflowDetail();
        return false;
    }
    if (!parsed.parsed) {
        errorDetail = invalidIntegerDetail();
        return false;
    }
    out = parsed.value;
    return true;
}

/// 查询串里的可选有符号整数（i64）。
bool takeQuerySigned(const Request &request, const QString &key, std::optional<qint64> &out,
                     QString &errorDetail)
{
    const std::optional<QString> raw = request.query(key);
    if (!raw.has_value()) {
        return true;
    }
    qint64 value = 0;
    if (!parseStrictInteger(*raw, true, value)) {
        errorDetail = invalidIntegerDetail();
        return false;
    }
    out = value;
    return true;
}

bool takeQueryBool(const Request &request, const QString &key, std::optional<bool> &out,
                   QString &errorDetail)
{
    const std::optional<QString> raw = request.query(key);
    if (!raw.has_value()) {
        return true;
    }
    if (raw.value() == QLatin1String("true")) {
        out = true;
        return true;
    }
    if (raw.value() == QLatin1String("false")) {
        out = false;
        return true;
    }
    errorDetail = invalidBoolDetail();
    return false;
}

bool takeQueryDouble(const Request &request, const QString &key, std::optional<double> &out,
                     QString &errorDetail)
{
    const std::optional<QString> raw = request.query(key);
    if (!raw.has_value()) {
        return true;
    }
    // f64 的 FromStr 比整数宽（允许 "1e3"、".5"），这里只挡明显非法的空白与字母。
    const QString text = *raw;
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || text != text.trimmed()) {
        errorDetail = invalidFloatDetail();
        return false;
    }
    out = value;
    return true;
}

/// 只有出现在查询串里的键才写进 JSON：缺键要让 serde 当成 None。
Json queryAsJson(const Request &request, const QStringList &keys)
{
    Json value = Json::object();
    for (const QString &key : keys) {
        const std::optional<QString> found = request.query(key);
        if (found.has_value()) {
            value[key.toStdString()] = found->toStdString();
        }
    }
    return value;
}

Json fetchResultsToJson(const QVector<Models::FetchResult> &results)
{
    Json entries = Json::array();
    for (const Models::FetchResult &result : results) {
        entries.push_back(result.toJson());
    }
    return entries;
}

QVector<QString> jsonToStringVector(const Json &value)
{
    QVector<QString> out;
    for (const Json &item : value) {
        if (item.is_string()) {
            out.append(QString::fromStdString(item.get<std::string>()));
        }
    }
    return out;
}

/// UpdateSettings 的键 → serde 期望的 JSON 类型。Rust 侧类型不符是 422，
/// 而 Models::UpdateSettings::fromJson 是宽松读法，所以这里先做一遍严格校验。
enum class ValueKind {
    Bool,
    Integer,
    Text,
};

const QHash<QString, ValueKind> &updateSettingsSchema()
{
    static const QHash<QString, ValueKind> schema = {
        {QStringLiteral("auto_fetch_enabled"), ValueKind::Bool},
        {QStringLiteral("auto_fetch_interval_min"), ValueKind::Integer},
        {QStringLiteral("auto_speedtest_enabled"), ValueKind::Bool},
        {QStringLiteral("fetch_concurrency"), ValueKind::Integer},
        {QStringLiteral("fetch_timeout_secs"), ValueKind::Integer},
        {QStringLiteral("speedtest_concurrency"), ValueKind::Integer},
        {QStringLiteral("speedtest_timeout_ms"), ValueKind::Integer},
        {QStringLiteral("speedtest_download"), ValueKind::Bool},
        {QStringLiteral("download_test_ms"), ValueKind::Integer},
        {QStringLiteral("http_server_enabled"), ValueKind::Bool},
        {QStringLiteral("close_to_tray"), ValueKind::Bool},
        {QStringLiteral("exclude_keywords"), ValueKind::Text},
        {QStringLiteral("failover_latency_ms"), ValueKind::Integer},
        {QStringLiteral("speedtest_parallel_kernels"), ValueKind::Bool},
        {QStringLiteral("speedtest_persistent_kernel"), ValueKind::Bool},
        {QStringLiteral("speedtest_reuse_minutes"), ValueKind::Integer},
        {QStringLiteral("speedtest_conditional_isolation"), ValueKind::Bool},
        {QStringLiteral("speedtest_download_top_n"), ValueKind::Integer},
        {QStringLiteral("bandwidth_guard"), ValueKind::Bool},
        {QStringLiteral("bandwidth_min_kbps"), ValueKind::Integer},
        {QStringLiteral("publisher_api"), ValueKind::Text},
        {QStringLiteral("publisher_token"), ValueKind::Text},
        {QStringLiteral("ui_theme"), ValueKind::Text},
        {QStringLiteral("kernel_source"), ValueKind::Text},
        {QStringLiteral("firewall_auto_allow"), ValueKind::Bool},
        {QStringLiteral("auto_failover"), ValueKind::Bool},
        {QStringLiteral("speedtest_auto_refill"), ValueKind::Bool},
        {QStringLiteral("auto_prepare_kernel"), ValueKind::Bool},
        {QStringLiteral("speedtest_added_source"), ValueKind::Bool},
    };
    return schema;
}

bool typeMatches(const Json &value, ValueKind kind)
{
    switch (kind) {
    case ValueKind::Bool:
        return value.is_boolean();
    case ValueKind::Integer:
        return value.is_number_integer() || value.is_number_unsigned();
    case ValueKind::Text:
        return value.is_string();
    }
    return true;
}

QString expectedName(ValueKind kind)
{
    switch (kind) {
    case ValueKind::Bool:
        return QStringLiteral("a boolean");
    case ValueKind::Integer:
        return QStringLiteral("an integer");
    case ValueKind::Text:
        return QStringLiteral("a string");
    }
    return QString();
}

bool checkStringKeys(const Json &body, const QStringList &keys, Response &rejection)
{
    for (const QString &key : keys) {
        const auto found = body.find(key.toStdString());
        if (found == body.end() || found->is_null() || found->is_string()) {
            continue;
        }
        rejection = invalidTypeRejection(*found, QStringLiteral("a string"));
        return false;
    }
    return true;
}

bool checkBoolKeys(const Json &body, const QStringList &keys, Response &rejection)
{
    for (const QString &key : keys) {
        const auto found = body.find(key.toStdString());
        if (found == body.end() || found->is_null() || found->is_boolean()) {
            continue;
        }
        rejection = invalidTypeRejection(*found, QStringLiteral("a boolean"));
        return false;
    }
    return true;
}

bool checkUnsignedKeys(const Json &body, const QStringList &keys, Response &rejection)
{
    for (const QString &key : keys) {
        const auto found = body.find(key.toStdString());
        if (found == body.end() || found->is_null()) {
            continue;
        }
        if (!found->is_number_integer() && !found->is_number_unsigned()) {
            rejection = invalidTypeRejection(*found, QStringLiteral("usize"));
            return false;
        }
    }
    return true;
}

/// `{"deleted": n}` 家族（clear / delete-dead / delete-invalid / dedupe）。
Response deletedCountJson(const Result<qint64> &count)
{
    if (count.isErr()) {
        return Handlers::errorResponse(count.error());
    }
    Json data = Json::object();
    data["deleted"] = count.value();
    return Http::ok(data);
}

/// 对应 `fn proxy_start` / `fn proxy_restart`：Rust 里两个 handler 逐字相同。
Response connectProxyNode(const Handlers &handlers, const AppState &state, const Request &request)
{
    const JsonBody body = extractJsonBody(request, QStringLiteral("ProxyNodeBody"));
    if (!body.ok()) {
        return *body.rejection;
    }
    QString nodeId;
    Response rejection;
    if (!requireString(body.value, "node_id", nodeId, rejection)) {
        return rejection;
    }
    std::optional<Kernel::Preference> kernel;
    if (!kernelPreferenceFromJson(body.value, "kernel", kernel, rejection)) {
        return rejection;
    }
    if (!state.db || !state.proxy) {
        return Handlers::errorResponse(QStringLiteral("proxy layer is not wired"));
    }
    Result<Models::Node> loaded = state.db->getNode(nodeId);
    if (loaded.isErr()) {
        return Handlers::errorResponse(loaded.error());
    }
    if (loaded.value().id.isEmpty()) {
        return Http::error(404, QStringLiteral("node not found"));
    }
    const Models::Node node = loaded.takeValue();
    const Status connected = state.proxy->connect(node, kernel);
    if (connected.isErr()) {
        return handlers.proxyOperationError(node.id, connected.error());
    }
    return Http::ok(state.proxy->status().toJson());
}

} // namespace

SpeedtestOptions SpeedtestOptions::forSingleTest(const Models::AppSettings &settings)
{
    SpeedtestOptions options;
    options.concurrency = 1;
    options.timeoutMs = static_cast<quint32>(settings.speedtestTimeoutMs);
    options.measureDownload = settings.speedtestDownload;
    options.downloadTestMs = static_cast<quint32>(settings.downloadTestMs);
    options.parallelKernels = settings.speedtestParallelKernels;
    options.persistentKernel = settings.speedtestPersistentKernel;
    options.reuseVerifiedMinutes = static_cast<quint32>(settings.speedtestReuseMinutes);
    options.conditionalIsolation = settings.speedtestConditionalIsolation;
    options.downloadTopN = static_cast<quint32>(settings.speedtestDownloadTopN);
    return options;
}

SpeedtestOptions SpeedtestOptions::forBatchTest(const Models::AppSettings &settings)
{
    SpeedtestOptions options = forSingleTest(settings);
    options.concurrency = static_cast<quint32>(settings.speedtestConcurrency);
    return options;
}

std::optional<QString> localProxyUrl(const Proxy::ProxyRuntimeStatus &status)
{
    if (!status.localProxyActive()) {
        return std::nullopt;
    }
    return QStringLiteral("http://127.0.0.1:%1").arg(status.mixedPort);
}

Handlers::Handlers(AppState state) : state_(std::move(state)) {}

Response Handlers::errorResponse(const QString &message)
{
    return Http::error(500, message);
}

// ---------------------------------------------------------------------------
// GET /api/health
// ---------------------------------------------------------------------------
Response Handlers::health(const Request &, const PathParams &) const
{
    Json data = Json::object();
    data["status"] = "up";
    return Http::ok(data);
}

// ---------------------------------------------------------------------------
// GET /api/tasks?limit=
// ---------------------------------------------------------------------------
Response Handlers::listTasks(const Request &request, const PathParams &) const
{
    std::optional<quint32> limit;
    QString detail;
    if (!takeQueryUnsigned(request, QStringLiteral("limit"), limit, detail)) {
        return queryRejected(detail);
    }
    const size_t wanted = static_cast<size_t>(limit.value_or(10));
    Json entries = Json::array();
    if (state_.tasks) {
        for (const Tasks::Snapshot &snapshot : state_.tasks->snapshots(wanted)) {
            entries.push_back(snapshot.toJson());
        }
    }
    return Http::ok(entries);
}

// ---------------------------------------------------------------------------
// GET /api/kernels
// ---------------------------------------------------------------------------
Response Handlers::kernels(const Request &, const PathParams &) const
{
    if (!state_.kernel) {
        return errorResponse(QStringLiteral("kernel layer is not wired"));
    }
    Json entries = Json::array();
    for (const Kernel::Info &info : state_.kernel->detectAll()) {
        entries.push_back(info.toJson());
    }
    return Http::ok(entries);
}

// ---------------------------------------------------------------------------
// GET /api/kernels/releases
// ---------------------------------------------------------------------------
Response Handlers::kernelReleases(const Request &, const PathParams &) const
{
    if (!state_.kernel) {
        return errorResponse(QStringLiteral("kernel layer is not wired"));
    }
    std::optional<QString> proxyUrl;
    if (state_.proxy) {
        proxyUrl = localProxyUrl(state_.proxy->status());
    }
    const Result<QList<Kernel::ReleaseInfo>> releases =
        state_.kernel->checkUpdatesWithProxy(proxyUrl);
    if (releases.isErr()) {
        return errorResponse(releases.error());
    }
    Json entries = Json::array();
    for (const Kernel::ReleaseInfo &release : releases.value()) {
        entries.push_back(release.toJson());
    }
    return Http::ok(entries);
}

// ---------------------------------------------------------------------------
// POST /api/kernels/update  {kind}
// ---------------------------------------------------------------------------
Response Handlers::updateKernel(const Request &request, const PathParams &) const
{
    const JsonBody body = extractJsonBody(request, QStringLiteral("KernelUpdateBody"));
    if (!body.ok()) {
        return *body.rejection;
    }
    Kernel::Kind kind = Kernel::Kind::Mihomo;
    Response rejection;
    if (!kernelKindFromJson(body.value, "kind", kind, rejection, false)) {
        return rejection;
    }
    if (!state_.kernel) {
        return errorResponse(QStringLiteral("kernel layer is not wired"));
    }
    Proxy::ProxyRuntimeStatus status;
    if (state_.proxy) {
        status = state_.proxy->status();
    }
    if (status.kernelInUse(kind)) {
        const QString message = Messages::kernelUpdateBlocked().replace(
            QStringLiteral("{}"), Kernel::kindDisplayName(kind));
        return Http::error(409, message);
    }
    const std::optional<QString> proxyUrl = localProxyUrl(status);
    // 走哪种下载方式跟 GUI 弹框用同一份设置；读不到就用默认那家镜像
    // （Rust：get_settings().map(|s| s.kernel_source).unwrap_or_default()）。
    QString kernelSourceId;
    if (state_.db) {
        const Result<Models::AppSettings> settings = state_.db->getSettings();
        if (settings.isOk()) {
            kernelSourceId = settings.value().kernelSource;
        }
    }
    const Kernel::Source source = Kernel::sourceFromId(kernelSourceId);
    const Result<Kernel::UpdateResult> result =
        state_.kernel->updateWithOptions(kind, proxyUrl, source);
    if (result.isErr()) {
        return errorResponse(result.error());
    }
    return Http::ok(result.value().toJson());
}

// ---------------------------------------------------------------------------
// GET /api/proxy/status
// ---------------------------------------------------------------------------
Response Handlers::proxyStatus(const Request &, const PathParams &) const
{
    if (!state_.proxy) {
        return errorResponse(QStringLiteral("proxy layer is not wired"));
    }
    return Http::ok(state_.proxy->status().toJson());
}

// ---------------------------------------------------------------------------
// GET /api/proxy/config
// ---------------------------------------------------------------------------
Response Handlers::proxyConfig(const Request &, const PathParams &) const
{
    if (!state_.proxy) {
        return errorResponse(QStringLiteral("proxy layer is not wired"));
    }
    return Http::ok(state_.proxy->config().toJson());
}

// ---------------------------------------------------------------------------
// PUT /api/proxy/config
// ---------------------------------------------------------------------------
Response Handlers::updateProxyConfig(const Request &request, const PathParams &) const
{
    if (!state_.proxy || !state_.db) {
        return errorResponse(QStringLiteral("proxy layer is not wired"));
    }
    const JsonBody body = extractJsonBody(request, QStringLiteral("MihomoConfigOptions"));
    if (!body.ok()) {
        return *body.rejection;
    }
    // MihomoConfigOptions 的嵌套结构（tun/dns/rules）由已移植的 fromJson 宽松读取，
    // 类型错误不会像 serde 那样提前 422，而是落到下面的 validate() → 400。
    const Proxy::MihomoConfigOptions options = Proxy::MihomoConfigOptions::fromJson(body.value);
    const Proxy::MihomoConfigOptions previous = state_.proxy->config();
    const Status applied = state_.proxy->updateConfig(options);
    if (applied.isErr()) {
        return Http::error(400, applied.error());
    }
    const Result<QString> saved = options.toSavedJsonText();
    if (saved.isErr()) {
        state_.proxy->updateConfig(previous);
        return errorResponse(saved.error());
    }
    const Status written = state_.db->setProxyConfigJsonText(saved.value());
    if (written.isErr()) {
        state_.proxy->updateConfig(previous);
        return errorResponse(written.error());
    }
    return Http::ok(options.toJson());
}

// ---------------------------------------------------------------------------
// GET /api/proxy/log?max_chars=
// ---------------------------------------------------------------------------
Response Handlers::proxyLog(const Request &request, const PathParams &) const
{
    std::optional<quint32> maxChars;
    QString detail;
    if (!takeQueryUnsigned(request, QStringLiteral("max_chars"), maxChars, detail)) {
        return queryRejected(detail);
    }
    if (!state_.proxy) {
        return errorResponse(QStringLiteral("proxy layer is not wired"));
    }
    const int wanted = std::clamp<int>(static_cast<int>(maxChars.value_or(8000)), 100, 100000);
    const Result<QString> log = state_.proxy->recentLog(wanted);
    if (log.isErr()) {
        return errorResponse(log.error());
    }
    Json data = Json::object();
    data["log"] = log.value().toStdString();
    return Http::ok(data);
}

// ---------------------------------------------------------------------------
// POST /api/proxy/{start,restart,stop}
// ---------------------------------------------------------------------------
Response Handlers::proxyStart(const Request &request, const PathParams &) const
{
    return connectProxyNode(*this, state_, request);
}

Response Handlers::proxyRestart(const Request &request, const PathParams &) const
{
    return connectProxyNode(*this, state_, request);
}

Response Handlers::proxyStop(const Request &, const PathParams &) const
{
    if (!state_.proxy) {
        return errorResponse(QStringLiteral("proxy layer is not wired"));
    }
    const Status stopped = state_.proxy->stop();
    if (stopped.isErr()) {
        return errorResponse(stopped.error());
    }
    return Http::ok(state_.proxy->status().toJson());
}

Response Handlers::proxyOperationError(const QString &nodeId, const QString &message) const
{
    if (!Proxy::isNodeConnectivityFailure(message)) {
        return errorResponse(message);
    }
    QString kernelText; // null QString = Rust 的 None
    if (state_.proxy) {
        const std::optional<Kernel::Kind> used = state_.proxy->status().kernel;
        if (used.has_value()) {
            kernelText = Kernel::kindToText(*used);
        }
    }
    if (!state_.db) {
        return errorResponse(message);
    }
    const Status updated = state_.db->updateNodeSpeed(nodeId, false, std::nullopt, std::nullopt,
                                                      kernelText,
                                                      QString::fromLatin1(Models::kGoogleProxyTestKind));
    QString suffix = Messages::markedUnavailableSuffix();
    if (updated.isErr()) {
        suffix = Messages::markFailurePrefix().replace(QStringLiteral("{update_error}"),
                                                       updated.error());
    }
    return errorResponse(message + suffix);
}

// ---------------------------------------------------------------------------
// GET /api/stats
// ---------------------------------------------------------------------------
Response Handlers::stats(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Result<Models::Stats> value = state_.db->stats();
    if (value.isErr()) {
        return errorResponse(value.error());
    }
    return Http::ok(value.value().toJson());
}

// ---------------------------------------------------------------------------
// GET/PUT /api/settings
// ---------------------------------------------------------------------------
Response Handlers::getSettings(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Result<Models::AppSettings> settings = state_.db->getSettings();
    if (settings.isErr()) {
        return errorResponse(settings.error());
    }
    return Http::ok(settings.value().toJson());
}

Response Handlers::updateSettings(const Request &request, const PathParams &) const
{
    const JsonBody body = extractJsonBody(request, QStringLiteral("UpdateSettings"));
    if (!body.ok()) {
        return *body.rejection;
    }
    for (const auto &entry : body.value.items()) {
        const auto found = updateSettingsSchema().find(QString::fromLatin1(entry.key().c_str()));
        if (found == updateSettingsSchema().end() || entry.value().is_null()) {
            continue; // 未知字段 serde 忽略；null 是 Option 的合法值
        }
        if (!typeMatches(entry.value(), *found)) {
            return Http::rejection(422, kJsonTypeFailure
                                                + QStringLiteral("invalid type: %1, expected %2")
                                                      .arg(serdeTypeName(entry.value()),
                                                           expectedName(*found)));
        }
    }
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Models::UpdateSettings patch = Models::UpdateSettings::fromJson(body.value);
    const Result<Models::AppSettings> updated = state_.db->updateSettings(patch);
    if (updated.isErr()) {
        return errorResponse(updated.error());
    }
    return Http::ok(updated.value().toJson());
}

// ---------------------------------------------------------------------------
// /api/sources*
// ---------------------------------------------------------------------------
Response Handlers::listSources(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Result<QVector<Models::Source>> sources = state_.db->listSources();
    if (sources.isErr()) {
        return errorResponse(sources.error());
    }
    Json entries = Json::array();
    for (const Models::Source &source : sources.value()) {
        entries.push_back(source.toJson());
    }
    return Http::ok(entries);
}

Response Handlers::getSource(const Request &, const PathParams &params) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const QString id = params.value(QStringLiteral("id"));
    const Result<Models::Source> source = state_.db->getSource(id);
    if (source.isErr()) {
        return errorResponse(source.error());
    }
    if (source.value().id.isEmpty()) {
        return Http::error(404, QStringLiteral("not found"));
    }
    return Http::ok(source.value().toJson());
}

Response Handlers::createSource(const Request &request, const PathParams &) const
{
    const JsonBody body = extractJsonBody(request, QStringLiteral("CreateSource"));
    if (!body.ok()) {
        return *body.rejection;
    }
    Response rejection;
    if (!checkStringKeys(body.value, {QStringLiteral("name"), QStringLiteral("url_template")},
                         rejection)
        || !checkBoolKeys(body.value, {QStringLiteral("enabled")}, rejection)) {
        return rejection;
    }
    QString name;
    QString urlTemplate;
    if (!requireString(body.value, "name", name, rejection)) {
        return rejection;
    }
    if (!requireString(body.value, "url_template", urlTemplate, rejection)) {
        return rejection;
    }
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Models::CreateSource input = Models::CreateSource::fromJson(body.value);
    if (input.name.trimmed().isEmpty() || input.urlTemplate.trimmed().isEmpty()) {
        return Http::error(400, QStringLiteral("name and url_template required"));
    }
    const Result<Models::Source> created = state_.db->createSource(input);
    if (created.isErr()) {
        return errorResponse(created.error());
    }
    return Http::okWithStatus(201, created.value().toJson());
}

Response Handlers::updateSource(const Request &request, const PathParams &params) const
{
    const JsonBody body = extractJsonBody(request, QStringLiteral("UpdateSource"));
    if (!body.ok()) {
        return *body.rejection;
    }
    Response rejection;
    if (!checkStringKeys(body.value,
                         {QStringLiteral("name"), QStringLiteral("url_template"),
                          QStringLiteral("remark")},
                         rejection)
        || !checkBoolKeys(body.value, {QStringLiteral("enabled"), QStringLiteral("push_disabled")},
                          rejection)) {
        return rejection;
    }
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const QString id = params.value(QStringLiteral("id"));
    const Models::UpdateSource patch = Models::UpdateSource::fromJson(body.value);
    const Result<Models::Source> updated = state_.db->updateSource(id, patch);
    if (updated.isErr()) {
        return errorResponse(updated.error());
    }
    return Http::ok(updated.value().toJson());
}

Response Handlers::deleteSource(const Request &, const PathParams &params) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const QString id = params.value(QStringLiteral("id"));
    const Result<bool> deleted = state_.db->deleteSource(id);
    if (deleted.isErr()) {
        return errorResponse(deleted.error());
    }
    if (!deleted.value()) {
        return Http::error(404, QStringLiteral("not found"));
    }
    Json data = Json::object();
    data["deleted"] = true;
    return Http::ok(data);
}

// ---------------------------------------------------------------------------
// /api/sources/{id}/fetch, /api/sources/fetch-all, /api/gist/discover
// ---------------------------------------------------------------------------
Response Handlers::fetchSource(const Request &, const PathParams &params) const
{
    if (!state_.collector) {
        return errorResponse(QStringLiteral("collector layer is not wired"));
    }
    const QString id = params.value(QStringLiteral("id"));
    const Result<Models::FetchResult> result = state_.collector->fetchOne(id);
    if (result.isErr()) {
        return errorResponse(result.error());
    }
    return Http::ok(result.value().toJson());
}

Response Handlers::fetchAll(const Request &, const PathParams &) const
{
    if (!state_.collector) {
        return errorResponse(QStringLiteral("collector layer is not wired"));
    }
    const Result<QVector<Models::FetchResult>> results = state_.collector->fetchAllEnabled();
    if (results.isErr()) {
        return errorResponse(results.error());
    }
    return Http::ok(fetchResultsToJson(results.value()));
}

Response Handlers::discoverGists(const Request &request, const PathParams &) const
{
    const JsonBody body = extractJsonBody(request, QStringLiteral("GistDiscoveryRequest"));
    if (!body.ok()) {
        return *body.rejection;
    }
    Response rejection;
    if (!checkStringKeys(body.value, {QStringLiteral("query")}, rejection)
        || !checkUnsignedKeys(body.value, {QStringLiteral("max_pages")}, rejection)) {
        return rejection;
    }
    if (!state_.collector) {
        return errorResponse(QStringLiteral("collector layer is not wired"));
    }
    // Option<String>：给了就用，空串也算给了（Rust 的 unwrap_or_else 只在缺省时回填）。
    QString query = QStringLiteral("vless//");
    const std::optional<QString> givenQuery = JsonUtil::optTextValue(body.value, "query");
    if (givenQuery.has_value()) {
        query = *givenQuery;
    }
    int pages = 100;
    const auto pagesFound = body.value.find("max_pages");
    if (pagesFound != body.value.end() && pagesFound->is_number()) {
        pages = static_cast<int>(pagesFound->get<qint64>());
    }
    state_.collector->discoverGistsInBackground(query, pages);

    Json data = Json::object();
    data["started"] = true;
    data["query"] = query.toStdString();
    data["max_pages"] = pages;
    data["message"] = Messages::gistStartedMessage().toStdString();
    return Http::ok(data);
}

// ---------------------------------------------------------------------------
// /api/nodes*
// ---------------------------------------------------------------------------
Response Handlers::listNodes(const Request &request, const PathParams &) const
{
    std::optional<quint32> page;
    std::optional<quint32> pageSize;
    QString detail;
    if (!takeQueryUnsigned(request, QStringLiteral("page"), page, detail)) {
        return queryRejected(detail);
    }
    if (!takeQueryUnsigned(request, QStringLiteral("page_size"), pageSize, detail)) {
        return queryRejected(detail);
    }
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Models::NodeQuery query = Models::NodeQuery::fromJson(queryAsJson(
        request,
        {QStringLiteral("protocol"), QStringLiteral("source_id"), QStringLiteral("keyword"),
         QStringLiteral("latency"), QStringLiteral("country"), QStringLiteral("sort")}));
    const QString sourceId = query.sourceId.has_value() ? *query.sourceId : QString();
    const Result<Models::Page<Models::NodeSummary>> listed = state_.db->listNodes(
        query.protocol, sourceId, query.keyword, query.latency, query.country,
        page.value_or(1), pageSize.value_or(50), query.sort);
    if (listed.isErr()) {
        return errorResponse(listed.error());
    }
    return Http::ok(listed.value().toJson());
}

Response Handlers::getNode(const Request &, const PathParams &params) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const QString id = params.value(QStringLiteral("id"));
    const Result<Models::Node> node = state_.db->getNode(id);
    if (node.isErr()) {
        return errorResponse(node.error());
    }
    if (node.value().id.isEmpty()) {
        return Http::error(404, QStringLiteral("not found"));
    }
    return Http::ok(node.value().toJson());
}

Response Handlers::deleteNode(const Request &, const PathParams &params) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const QString id = params.value(QStringLiteral("id"));
    const Result<bool> deleted = state_.db->deleteNode(id);
    if (deleted.isErr()) {
        return errorResponse(deleted.error());
    }
    if (!deleted.value()) {
        return Http::error(404, QStringLiteral("not found"));
    }
    Json data = Json::object();
    data["deleted"] = true;
    return Http::ok(data);
}

Response Handlers::clearNodes(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    return deletedCountJson(state_.db->clearNodes());
}

Response Handlers::deleteDeadNodes(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    return deletedCountJson(state_.db->deleteDeadNodes());
}

Response Handlers::deleteInvalidHosts(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    return deletedCountJson(state_.db->deleteInvalidHosts());
}

Response Handlers::dedupeNodes(const Request &, const PathParams &) const
{
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    return deletedCountJson(state_.db->dedupeNodes());
}

// ---------------------------------------------------------------------------
// /api/nodes/{id}/speedtest, /api/nodes/speedtest
// ---------------------------------------------------------------------------
Response Handlers::speedtestOne(const Request &, const PathParams &params) const
{
    if (!state_.speedtest) {
        return errorResponse(QStringLiteral("speedtest layer is not wired"));
    }
    SpeedtestOptions options; // 对应 Rust 的 `Err(_) => SpeedTestOptions::default()`
    if (state_.db) {
        const Result<Models::AppSettings> settings = state_.db->getSettings();
        if (settings.isOk()) {
            options = SpeedtestOptions::forSingleTest(settings.value());
        }
    }
    const QString id = params.value(QStringLiteral("id"));
    const Result<Models::LatencyResult> result = state_.speedtest->testOne(id, options);
    if (result.isErr()) {
        return errorResponse(result.error());
    }
    return Http::ok(result.value().toJson());
}

Response Handlers::speedtestAll(const Request &request, const PathParams &) const
{
    if (!state_.speedtest) {
        return errorResponse(QStringLiteral("speedtest layer is not wired"));
    }
    if (!state_.db) {
        return errorResponse(QStringLiteral("database is not wired"));
    }
    const Result<Models::AppSettings> settings = state_.db->getSettings();
    if (settings.isErr()) {
        return errorResponse(settings.error());
    }
    // 对应 `Option<Json<SpeedtestBody>>`：body 缺失、Content-Type 不对、JSON 非法，
    // 一律当成"没给"（axum 的 Option 提取器把拒绝吞掉）。
    std::optional<QVector<QString>> ids;
    const JsonBody body = extractJsonBody(request, QStringLiteral("SpeedtestBody"));
    if (body.ok()) {
        const auto found = body.value.find("ids");
        if (found != body.value.end() && found->is_array()) {
            ids = jsonToStringVector(*found);
        }
    }
    const Result<Models::SpeedTestSummary> summary =
        state_.speedtest->testMany(ids, SpeedtestOptions::forBatchTest(settings.value()));
    if (summary.isErr()) {
        return errorResponse(summary.error());
    }
    return Http::ok(summary.value().toJson());
}

// ---------------------------------------------------------------------------
// /api/export/*
// ---------------------------------------------------------------------------
Handlers::QualityParams Handlers::qualityParams(const ExportQuery &query)
{
    QualityParams params;
    params.qualityOnly = query.qualityOnly.value_or(true);
    params.maxLatencyMs = query.maxLatencyMs.value_or(500);
    // 默认 0：要求下载速度以前会挑出一堆假网站。
    params.minDownloadKbps = query.minDownloadKbps.value_or(0.0);
    params.limit = query.limit;
    return params;
}

Handlers::ExportQuery Handlers::parseExportQuery(const Request &request)
{
    ExportQuery query;
    QString detail;
    query.protocol = request.query(QStringLiteral("protocol"));
    query.sourceId = request.query(QStringLiteral("source_id"));
    if (!takeQueryBool(request, QStringLiteral("quality_only"), query.qualityOnly, detail)) {
        query.queryError = kQueryFailure + detail;
        return query;
    }
    if (!takeQuerySigned(request, QStringLiteral("max_latency_ms"), query.maxLatencyMs, detail)) {
        query.queryError = kQueryFailure + detail;
        return query;
    }
    if (!takeQueryDouble(request, QStringLiteral("min_download_kbps"), query.minDownloadKbps,
                         detail)) {
        query.queryError = kQueryFailure + detail;
        return query;
    }
    if (!takeQueryUnsigned(request, QStringLiteral("limit"), query.limit, detail)) {
        query.queryError = kQueryFailure + detail;
        return query;
    }
    if (!takeQueryBool(request, QStringLiteral("base64"), query.base64, detail)) {
        query.queryError = kQueryFailure + detail;
        return query;
    }
    return query;
}

Result<QVector<Models::Node>> Handlers::loadExportNodes(const ExportQuery &query) const
{
    if (!state_.db) {
        return Result<QVector<Models::Node>>::withError(QStringLiteral("database is not wired"));
    }
    const QualityParams params = qualityParams(query);
    std::optional<quint32> fetchLimit;
    if (params.qualityOnly) {
        fetchLimit = qMin<quint32>(500, qMax<quint32>(1, params.limit.value_or(50)));
    } else {
        fetchLimit = params.limit;
    }
    Result<QVector<Models::Node>> loaded = DbExport::exportNodesFiltered(
        *state_.db, query.protocol, query.sourceId, params.qualityOnly, params.maxLatencyMs,
        params.minDownloadKbps, fetchLimit);
    if (loaded.isErr()) {
        return Result<QVector<Models::Node>>::withError(loaded.error());
    }
    QVector<Models::Node> nodes = loaded.takeValue();
    if (params.qualityOnly) {
        // 按已存的测速结果打分排序（Rust 的 sort_by(export_score(b).cmp(export_score(a)))，稳定排序）。
        std::stable_sort(nodes.begin(), nodes.end(),
                         [](const Models::Node &left, const Models::Node &right) {
                             return ExportQuality::exportScore(right)
                                 < ExportQuality::exportScore(left);
                         });
        const int want = static_cast<int>(
            qMin<quint32>(500, qMax<quint32>(1, params.limit.value_or(50))));
        if (nodes.size() > want) {
            nodes.resize(want);
        }
    } else if (params.limit.has_value()) {
        const int wanted = static_cast<int>(qMax<quint32>(1, *params.limit));
        if (nodes.size() > wanted) {
            nodes.resize(wanted);
        }
    }
    return Result<QVector<Models::Node>>::withValue(nodes);
}

Response Handlers::exportJson(const Request &request, const PathParams &) const
{
    const ExportQuery query = parseExportQuery(request);
    if (!query.queryError.isEmpty()) {
        return Http::rejection(400, query.queryError);
    }
    const QualityParams params = qualityParams(query);
    const Result<QVector<Models::Node>> nodes = loadExportNodes(query);
    if (nodes.isErr()) {
        return errorResponse(nodes.error());
    }
    return Http::ok(Export::jsonExportDocument(nodes.value(), params.qualityOnly,
                                               params.maxLatencyMs, params.minDownloadKbps,
                                               params.limit));
}

Response Handlers::exportClash(const Request &request, const PathParams &) const
{
    const ExportQuery query = parseExportQuery(request);
    if (!query.queryError.isEmpty()) {
        return Http::rejection(400, query.queryError);
    }
    const Result<QVector<Models::Node>> nodes = loadExportNodes(query);
    if (nodes.isErr()) {
        return errorResponse(nodes.error());
    }
    const QString yaml = Export::clashProxiesYaml(nodes.value());
    return Response::withTextBody(200, QStringLiteral("text/yaml; charset=utf-8"), yaml);
}

Response Handlers::exportUri(const Request &request, const PathParams &) const
{
    const ExportQuery query = parseExportQuery(request);
    if (!query.queryError.isEmpty()) {
        return Http::rejection(400, query.queryError);
    }
    const Result<QVector<Models::Node>> nodes = loadExportNodes(query);
    if (nodes.isErr()) {
        return errorResponse(nodes.error());
    }
    // v2rayN 这类订阅客户端默认要 base64；base64=false 才给多行明文。
    const QString plain = Export::nodesToShareUris(nodes.value()).join(QStringLiteral("\n"));
    const QString bodyText = query.base64.value_or(true)
        ? QString::fromLatin1(plain.toUtf8().toBase64())
        : plain;
    return Response::withTextBody(200, QStringLiteral("text/plain; charset=utf-8"), bodyText);
}

} // namespace Api
