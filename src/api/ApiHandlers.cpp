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

/// Rust `{:?}` 对 &str 的转义（引号、反斜杠、常见控制符，其余 `\u{...}`）。
/// serde 的 `string {:?}` 就是它，所以拒绝体里的值必须这么写。
/// 反斜杠与引号一律用码点构造：这段代码是脚本生成的，字面量里的反斜杠容易在
/// 多层转义里变形。
QString rustDebugQuoted(const QString &text)
{
    const QChar backslash(char16_t(0x5C));
    const QChar quotation(char16_t(0x22));
    QString out(quotation);
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == 0x22) {
            out += backslash;
            out += quotation;
        } else if (code == 0x5C) {
            out += backslash;
            out += backslash;
        } else if (code == 0x0A) {
            out += backslash;
            out += QLatin1Char('n');
        } else if (code == 0x0D) {
            out += backslash;
            out += QLatin1Char('r');
        } else if (code == 0x09) {
            out += backslash;
            out += QLatin1Char('t');
        } else if (code < 0x20 || code == 0x7F) {
            out += backslash;
            out += QLatin1String("u{");
            out += QString::number(code, 16);
            out += QLatin1Char('}');
        } else {
            out += ch;
        }
    }
    return out + quotation;
}

/// Rust 的 `{}` 走最短可往返位数（`0.1` 就是 0.1，不是 0.10000000000000001），
/// 且从不用科学计数法；serde 的 WithDecimalPoint 再给整数值补上 `.0`。
QString rustFloatDisplay(double value)
{
    QString text;
    for (int precision = 0; precision <= 17; ++precision) {
        text = QString::number(value, 'f', precision);
        bool roundTrips = false;
        const double back = text.toDouble(&roundTrips);
        if (roundTrips && back == value) {
            break;
        }
    }
    if (!text.contains(QLatin1Char('.'))) {
        text += QStringLiteral(".0");
    }
    return text;
}

/// serde `Unexpected` 的 Display（本机 serde_core-1.0.229 `src/de/mod.rs:402` 起）：
/// 标量带值（`string "nope"` / boolean `true` / integer `5` / floating point `1.5`），
/// 容器与 null 是裸词（sequence / map / null）。
QString serdeTypeName(const Json &value)
{
    if (value.is_null()) {
        return QStringLiteral("null");
    }
    if (value.is_boolean()) {
        return QStringLiteral("boolean `%1`").arg(value.get<bool>() ? QStringLiteral("true")
                                                                   : QStringLiteral("false"));
    }
    if (value.is_string()) {
        return QStringLiteral("string ")
            + rustDebugQuoted(QString::fromStdString(value.get<std::string>()));
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return QStringLiteral("integer `%1`").arg(QString::number(value.get<long long>()));
    }
    if (value.is_number_float()) {
        return QStringLiteral("floating point `%1`").arg(rustFloatDisplay(value.get<double>()));
    }
    if (value.is_array()) {
        return QStringLiteral("sequence");
    }
    if (value.is_object()) {
        return QStringLiteral("map");
    }
    return QStringLiteral("value");
}

/// 一个 JSON 值在**原始字节流**里的形状与位置。serde 的列号是字节位，而 nlohmann 不
/// 保留原文位置，所以要在原始 body 上自己扫一遍。
enum class RawShape { String, Integer, Float, Literal, Array, Object, Missing };

struct RawValue {
    RawShape shape = RawShape::Missing;
    size_t start = 0;  ///< 1-based，首字节
    size_t end = 0;    ///< 1-based，末字节
};

/// serde 报的列号是**字节**位（实测：两个汉字的字符串值报在 byte 25，不是 char 20）。
/// 标量 = 该 token 最后一个字节；数组/对象 = 开括号**前一**个字节（serde_json 还没吃
/// 掉 `[` 就报错，空格也算在内：`" : [1]` 报在冒号和括号之间那个空格上）。
size_t serdeColumn(const RawValue &raw)
{
    switch (raw.shape) {
    case RawShape::String:
    case RawShape::Literal:
    case RawShape::Integer:
    case RawShape::Float:
        return raw.end;
    case RawShape::Array:
    case RawShape::Object:
        return raw.start > 0 ? raw.start - 1 : 0;
    case RawShape::Missing:
        break;
    }
    return 0;
}

/// 扫顶层对象的每个值 token；order 按文档顺序收键名（serde 取的是文档里的第一个错）。
QHash<QString, RawValue> scanTopLevelValues(const QByteArray &body, QVector<QString> *order)
{
    QHash<QString, RawValue> found;
    size_t index = 0;
    const auto skipSpace = [&]() {
        while (index < size_t(body.size())
               && (body.at(index) == ' ' || body.at(index) == '\t' || body.at(index) == '\n'
                   || body.at(index) == '\r')) {
            ++index;
        }
    };
    skipSpace();
    if (index >= size_t(body.size()) || body.at(index) != '{') {
        return found;
    }
    ++index;
    while (true) {
        skipSpace();
        if (index >= size_t(body.size()) || body.at(index) == '}' || body.at(index) != '"') {
            return found;
        }
        const size_t keyStart = ++index;
        while (index < size_t(body.size()) && body.at(index) != '"') {
            if (body.at(index) == '\\') {
                ++index;
            }
            ++index;
        }
        const QString key = QString::fromUtf8(body.mid(int(keyStart), int(index - keyStart)));
        ++index;
        skipSpace();
        if (index < size_t(body.size()) && body.at(index) == ':') {
            ++index;
        }
        skipSpace();
        if (index >= size_t(body.size())) {
            return found;
        }
        RawValue value;
        const char head = body.at(index);
        if (head == '"') {
            value.shape = RawShape::String;
            value.start = index + 1;
            ++index;
            while (index < size_t(body.size()) && body.at(index) != '"') {
                if (body.at(index) == '\\') {
                    ++index;
                }
                ++index;
            }
            value.end = index + 1;
            ++index;
        } else if (head == '[' || head == '{') {
            value.shape = head == '[' ? RawShape::Array : RawShape::Object;
            value.start = index + 1;
            value.end = index + 1;
            int depth = 0;
            bool inString = false;
            for (; index < size_t(body.size()); ++index) {
                const char ch = body.at(index);
                if (inString) {
                    if (ch == '\\') {
                        ++index;
                    } else if (ch == '"') {
                        inString = false;
                    }
                    continue;
                }
                if (ch == '"') {
                    inString = true;
                } else if (ch == '[' || ch == '{') {
                    ++depth;
                } else if (ch == ']' || ch == '}') {
                    --depth;
                    if (depth == 0) {
                        ++index;
                        break;
                    }
                }
            }
        } else if (head == 't' || head == 'f' || head == 'n') {
            value.shape = RawShape::Literal;
            value.start = index + 1;
            index += head == 'f' ? 5 : 4;
            value.end = index;
        } else if (head == '-' || head == '+' || head == '.' || (head >= '0' && head <= '9')) {
            value.start = index + 1;
            while (index < size_t(body.size())) {
                const char ch = body.at(index);
                const bool numeric = (ch >= '0' && ch <= '9') || ch == '-' || ch == '+'
                    || ch == '.' || ch == 'e' || ch == 'E';
                if (!numeric) {
                    break;
                }
                ++index;
            }
            value.end = index;
            const QByteArray token = body.mid(int(value.start - 1), int(value.end - value.start + 1));
            value.shape = (token.contains('.') || token.contains('e') || token.contains('E'))
                ? RawShape::Float
                : RawShape::Integer;
        } else {
            return found;
        }
        // 重复键：serde 取后出现的值，所以覆盖；顺序表里只留第一次出现的位置。
        if (!found.contains(key) && order != nullptr) {
            order->push_back(key);
        }
        found.insert(key, value);
        skipSpace();
        if (index < size_t(body.size()) && body.at(index) == ',') {
            ++index;
            continue;
        }
        return found;
    }
}

/// 提取并解析 JSON body，把 axum 的 415 / 422 拒绝原样搬过来。
struct JsonBody {
    Json value = Json::object();
    QByteArray raw;
    QHash<QString, RawValue> tokens;
    QVector<QString> keyOrder;
    std::optional<Response> rejection;

    bool ok() const { return !rejection.has_value(); }

    /// 该键值的 serde 列号；扫不到就回 0（调用方于是省略 " at line 1 column N" 后缀）。
    size_t column(const QString &key) const
    {
        const auto found = tokens.constFind(key);
        return found == tokens.constEnd() ? 0 : serdeColumn(*found);
    }

    /// 该键值 token 的列号（键名是 C 串，调用点多数写的是字面量）。
    size_t column(const char *key) const { return column(QString::fromLatin1(key)); }

    /// struct 反序列化结束时 serde_json 已经吃掉整段 body，所以缺字段的错报在
    /// 最后一字节（实测 `{}` 报 column 2、`{"zzz":1}` 报 column 9）。
    size_t endColumn() const { return size_t(raw.size()); }

    /// 值 token 的首字节位：枚举值不是字符串时 serde_json 是在 peek 阶段失败的，
    /// 报的是开括号/首字符本身；对象要先吃掉 `{` 才发现里面没有变体名，故 +1。
    size_t startColumn(const QString &key) const
    {
        const auto found = tokens.constFind(key);
        if (found == tokens.constEnd()) {
            return 0;
        }
        return found->shape == RawShape::Object ? found->start + 1 : found->start;
    }

    size_t startColumn(const char *key) const { return startColumn(QString::fromLatin1(key)); }
};

JsonBody extractJsonBody(const Request &request, const QString &structName)
{
    const QString contentType = request.header(QStringLiteral("content-type"));
    if (!Http::isJsonContentType(contentType)) {
        return {Json(), QByteArray(), {}, {}, Http::rejection(415, kExpectedJsonContentType)};
    }
    if (request.body.isEmpty()) {
        // axum: serde_json 在空输入上报 Category::Eof → JsonSyntaxError → 422。
        return {Json(), QByteArray(), {}, {},
                Http::rejection(400, kJsonParseFailure + emptyBodyDetail())};
    }
    const Result<Json> parsed = JsonUtil::parse(QString::fromUtf8(request.body));
    if (parsed.isErr()) {
        // detail 来自 nlohmann 的异常文本，措辞与 serde_json 不同（状态码一致）。
        return {Json(), request.body, {}, {},
                Http::rejection(400, kJsonParseFailure + parsed.error())};
    }
    if (!parsed.value().is_object()) {
        return {Json(), request.body, {}, {},
                Http::rejection(422, kJsonTypeFailure + QStringLiteral("invalid type: %1, expected struct %2")
                                             .arg(serdeTypeName(parsed.value()), structName))};
    }
    JsonBody extracted{parsed.value(), request.body, {}, {}, std::nullopt};
    extracted.tokens = scanTopLevelValues(request.body, &extracted.keyOrder);
    return extracted;
}

/// serde 的错按 axum 的样子拼：前缀 + `invalid type/value: …, expected …`
/// + 可选的 ` at line 1 column N`。
Response serdeStyleRejection(const QString &detail, size_t column)
{
    QString body = kJsonTypeFailure + detail;
    if (column > 0) {
        body += QStringLiteral(" at line 1 column %1").arg(column);
    }
    return Http::rejection(422, body);
}

/// 负数/越界打到一个更窄的整数类型上时，serde 说的是 invalid **value** 而不是 type。
Response invalidValueRejection(const Json &value, const QString &expected, size_t column = 0)
{
    return serdeStyleRejection(
        QStringLiteral("invalid value: %1, expected %2").arg(serdeTypeName(value), expected),
        column);
}

Response invalidTypeRejection(const Json &value, const QString &expected, size_t column = 0)
{
    return serdeStyleRejection(
        QStringLiteral("invalid type: %1, expected %2").arg(serdeTypeName(value), expected),
        column);
}

/// serde_derive 在 map 走到末尾才发现键不在，所以列号 = 已消费的字节数（= body 长度）。
Response missingFieldRejection(const char *key, size_t column)
{
    return serdeStyleRejection(
        QStringLiteral("missing field `%1`").arg(QString::fromLatin1(key)), column);
}

/// 枚举值不是字符串时 serde_json 在 peek 阶段就失败，属 Syntax 一类 → axum 给 400，
/// 文案是 "expected value"，而不是 invalid type（实测）。
Response expectedValueRejection(size_t column)
{
    return Http::rejection(
        400, kJsonParseFailure + QStringLiteral("expected value at line 1 column %1").arg(column));
}

/// 必填字符串字段：缺键 → `missing field`，类型不对 → invalid type。
bool requireString(const JsonBody &body, const char *key, QString &out, Response &rejection)
{
    const auto found = body.value.find(key);
    if (found == body.value.end() || found->is_null()) {
        rejection = missingFieldRejection(key, body.endColumn());
        return false;
    }
    if (!found->is_string()) {
        rejection = invalidTypeRejection(*found, QStringLiteral("a string"), body.column(key));
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
    U32,
    Usize,
    Text,
};

/// 键名 → 期望类型的一张小程序内 schema，供 checkTypedKeys 用。
using FieldSchema = QHash<QString, ValueKind>;

const QHash<QString, ValueKind> &updateSettingsSchema()
{
    static const QHash<QString, ValueKind> schema = {
        {QStringLiteral("auto_fetch_enabled"), ValueKind::Bool},
        {QStringLiteral("auto_fetch_interval_min"), ValueKind::U32},
        {QStringLiteral("auto_speedtest_enabled"), ValueKind::Bool},
        {QStringLiteral("fetch_concurrency"), ValueKind::U32},
        {QStringLiteral("fetch_timeout_secs"), ValueKind::U32},
        {QStringLiteral("speedtest_concurrency"), ValueKind::U32},
        {QStringLiteral("speedtest_timeout_ms"), ValueKind::U32},
        {QStringLiteral("speedtest_download"), ValueKind::Bool},
        {QStringLiteral("download_test_ms"), ValueKind::U32},
        {QStringLiteral("http_server_enabled"), ValueKind::Bool},
        {QStringLiteral("close_to_tray"), ValueKind::Bool},
        {QStringLiteral("exclude_keywords"), ValueKind::Text},
        {QStringLiteral("failover_latency_ms"), ValueKind::U32},
        {QStringLiteral("speedtest_parallel_kernels"), ValueKind::Bool},
        {QStringLiteral("speedtest_persistent_kernel"), ValueKind::Bool},
        {QStringLiteral("speedtest_reuse_minutes"), ValueKind::U32},
        {QStringLiteral("speedtest_conditional_isolation"), ValueKind::Bool},
        {QStringLiteral("speedtest_download_top_n"), ValueKind::U32},
        {QStringLiteral("bandwidth_guard"), ValueKind::Bool},
        {QStringLiteral("bandwidth_min_kbps"), ValueKind::U32},
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

/// "类型对不对"与"这个值进不进得了该类型"是两件事：负数打到 u32 上，
/// serde 报 invalid **value**，措辞与 invalid type 不同（实测）。
enum class FieldCheck { Ok, WrongType, OutOfRange };

FieldCheck checkField(const Json &value, ValueKind kind)
{
    if (kind == ValueKind::Bool) {
        return value.is_boolean() ? FieldCheck::Ok : FieldCheck::WrongType;
    }
    if (kind == ValueKind::Text) {
        return value.is_string() ? FieldCheck::Ok : FieldCheck::WrongType;
    }
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        return FieldCheck::WrongType;
    }
    const long long number = value.get<long long>();
    if (number < 0) {
        return FieldCheck::OutOfRange;
    }
    // usize 在本机是 64 位，正数基本进得去；u32 还有一道上界。
    if (kind == ValueKind::U32 && number > 4294967295LL) {
        return FieldCheck::OutOfRange;
    }
    return FieldCheck::Ok;
}

/// serde 对每种类型的叫法不统一（bool 是 "a boolean"，String 是 "a string"，
/// u32 就光秃秃一个 "u32"）—— 措辞全部按实测输出对齐。
QString expectedName(ValueKind kind)
{
    switch (kind) {
    case ValueKind::Bool:
        return QStringLiteral("a boolean");
    case ValueKind::U32:
        return QStringLiteral("u32");
    case ValueKind::Usize:
        return QStringLiteral("usize");
    case ValueKind::Text:
        return QStringLiteral("a string");
    }
    return QString();
}

/// 按 schema 逐字段核类型，返回 serde 会产生的第一条拒绝（没问题就是 nullopt）。
/// 必须走**文档顺序**：`{"a":bad,"b":bad}` serde 报的是 a，而不是 schema 里先写的 b。
/// `enums` 里的键按 serde enum 处理：值不是字符串就落在 peek 阶段（Syntax → 400
/// "expected value"），是字符串但不是变体名才是 422 的 unknown variant（实测）。
std::optional<Response> checkTypedKeys(const JsonBody &body, const FieldSchema &schema,
                                       const QHash<QString, QStringList> &enums = {})
{
    for (const QString &key : body.keyOrder) {
        const auto entry = body.value.find(key.toStdString());
        if (entry == body.value.end() || entry->is_null()) {
            continue; // null 是 Option 的合法值
        }
        const auto variants = enums.constFind(key);
        if (variants != enums.constEnd()) {
            if (!entry->is_string()) {
                return expectedValueRejection(body.startColumn(key));
            }
            const QString text = QString::fromStdString(entry->get<std::string>());
            if (!variants->contains(text)) {
                return serdeStyleRejection(unknownVariantDetail(text, *variants), body.column(key));
            }
            continue;
        }
        const auto wanted = schema.constFind(key);
        if (wanted == schema.constEnd()) {
            continue; // 未知字段 serde 忽略
        }
        const FieldCheck checked = checkField(*entry, *wanted);
        if (checked == FieldCheck::Ok) {
            continue;
        }
        const QString expected = expectedName(*wanted);
        if (checked == FieldCheck::OutOfRange) {
            return invalidValueRejection(*entry, expected, body.column(key));
        }
        return invalidTypeRejection(*entry, expected, body.column(key));
    }
    return std::nullopt;
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
    // 两个字段要在同一趟文档顺序遍历里比：Rust 那边 node_id 的类型错和 kernel 的
    // 枚举错谁在前面取决于 body 里谁先出现。
    if (const std::optional<Response> wrongType = checkTypedKeys(
            body, {{QStringLiteral("node_id"), ValueKind::Text}},
            {{QStringLiteral("kernel"), kernelPreferenceVariants()}})) {
        return *wrongType;
    }
    QString nodeId;
    Response rejection;
    if (!requireString(body, "node_id", nodeId, rejection)) {
        return rejection;
    }
    std::optional<Kernel::Preference> kernel;
    const std::optional<QString> kernelText = JsonUtil::optTextValue(body.value, "kernel");
    if (kernelText.has_value()) {
        kernel = Kernel::preferenceFromText(*kernelText);
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
    if (const std::optional<Response> wrongType
        = checkTypedKeys(body, {}, {{QStringLiteral("kind"), kernelKindVariants()}})) {
        return *wrongType;
    }
    QString kindText;
    Response rejection;
    if (!requireString(body, "kind", kindText, rejection)) {
        return rejection;
    }
    const Kernel::Kind kind = Kernel::kindFromText(kindText);
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
    if (const std::optional<Response> rejection = checkTypedKeys(body, updateSettingsSchema())) {
        return *rejection;
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
    if (const std::optional<Response> wrongType = checkTypedKeys(
            body, {{QStringLiteral("name"), ValueKind::Text},
                   {QStringLiteral("url_template"), ValueKind::Text},
                   {QStringLiteral("remark"), ValueKind::Text},
                   {QStringLiteral("enabled"), ValueKind::Bool}})) {
        return *wrongType;
    }
    QString name;
    QString urlTemplate;
    if (!requireString(body, "name", name, rejection)) {
        return rejection;
    }
    if (!requireString(body, "url_template", urlTemplate, rejection)) {
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
    if (const std::optional<Response> wrongType = checkTypedKeys(
            body, {{QStringLiteral("name"), ValueKind::Text},
                   {QStringLiteral("url_template"), ValueKind::Text},
                   {QStringLiteral("remark"), ValueKind::Text},
                   {QStringLiteral("enabled"), ValueKind::Bool},
                   {QStringLiteral("push_disabled"), ValueKind::Bool}})) {
        return *wrongType;
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
    if (const std::optional<Response> wrongType = checkTypedKeys(
            body, {{QStringLiteral("query"), ValueKind::Text},
                   {QStringLiteral("max_pages"), ValueKind::Usize}})) {
        return *wrongType;
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
