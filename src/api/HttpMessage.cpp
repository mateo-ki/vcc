#include "api/HttpMessage.h"

#include <QStringList>

#include "models/Types.h"

namespace Api {

QString Request::header(const QString &lowerName) const
{
    return headers.value(lowerName);
}

std::optional<QString> Request::headerOrNull(const QString &lowerName) const
{
    const auto found = headers.constFind(lowerName);
    if (found == headers.constEnd()) {
        return std::nullopt;
    }
    return found.value();
}

std::optional<QString> Request::query(const QString &key) const
{
    std::optional<QString> value;
    for (const auto &pair : queryPairs) {
        if (pair.first == key) {
            value = pair.second;
        }
    }
    return value;
}

std::optional<QString> Request::rawQueryValue(const QString &key) const
{
    // 对应 Rust 的 query.split('&').find_map(|pair| pair.split_once('=') 且 key=="token")：
    // 不做百分号解码、只取第一个命中的键、必须有 '='。
    if (rawQuery.isEmpty()) {
        return std::nullopt;
    }
    const QStringList pairs = rawQuery.split(QLatin1Char('&'));
    for (const QString &pair : pairs) {
        const int separator = pair.indexOf(QLatin1Char('='));
        if (separator < 0) {
            continue;
        }
        if (pair.left(separator) == key) {
            return pair.mid(separator + 1);
        }
    }
    return std::nullopt;
}

void Response::setHeader(const QString &name, const QString &value)
{
    for (auto &pair : headers) {
        if (pair.first.compare(name, Qt::CaseInsensitive) == 0) {
            pair.second = value;
            return;
        }
    }
    headers.append(qMakePair(name, value));
}

bool Response::hasHeader(const QString &lowerName) const
{
    for (const auto &pair : headers) {
        if (pair.first == lowerName) {
            return true;
        }
    }
    return false;
}

QString Response::header(const QString &lowerName) const
{
    for (const auto &pair : headers) {
        if (pair.first == lowerName) {
            return pair.second;
        }
    }
    return QString();
}

QByteArray Response::toBytes(bool includeBody) const
{
    QByteArray wire = "HTTP/1.1 " + QByteArray::number(status) + ' '
        + Http::reasonPhrase(status).toLatin1() + "\r\n";
    for (const auto &pair : headers) {
        wire += pair.first.toLatin1() + ": " + pair.second.toUtf8() + "\r\n";
    }
    wire += "\r\n";
    if (includeBody) {
        wire += body;
    }
    return wire;
}

Response Response::withJsonBody(int status, const Json &payload)
{
    Response response;
    response.status = status;
    response.body = QByteArray::fromStdString(payload.dump());
    response.setHeader(QStringLiteral("content-type"), QStringLiteral("application/json"));
    response.setHeader(QStringLiteral("content-length"), QString::number(response.body.size()));
    return response;
}

Response Response::withTextBody(int status, const QString &contentType, const QString &text)
{
    Response response;
    response.status = status;
    response.body = text.toUtf8();
    response.setHeader(QStringLiteral("content-type"), contentType);
    response.setHeader(QStringLiteral("content-length"), QString::number(response.body.size()));
    return response;
}

Response Response::empty(int status)
{
    Response response;
    response.status = status;
    response.setHeader(QStringLiteral("content-length"), QStringLiteral("0"));
    return response;
}

namespace Http {

Json okEnvelope(const Json &data)
{
    Json value = Json::object();
    value["ok"] = true;
    value["message"] = "ok";
    value["data"] = data;
    return value;
}

Json errorEnvelope(const QString &message)
{
    return Models::emptyApiResponse(false, message);
}

Response ok(const Json &data)
{
    return Response::withJsonBody(200, okEnvelope(data));
}

Response okWithStatus(int status, const Json &data)
{
    return Response::withJsonBody(status, okEnvelope(data));
}

Response error(int status, const QString &message)
{
    return Response::withJsonBody(status, errorEnvelope(message));
}

Response rejection(int status, const QString &detail)
{
    return Response::withTextBody(status, QStringLiteral("text/plain; charset=utf-8"), detail);
}

bool isJsonContentType(const QString &contentType)
{
    if (contentType.isEmpty()) {
        return false;
    }
    const QString base = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    // axum 只看 type=="application" && subtype=="json"，`+json` 后缀不认。
    return base == QLatin1String("application/json");
}

Result<std::optional<Request>> parseRequest(const QByteArray &buffer, int *consumed)
{
    using Parsed = std::optional<Request>;
    if (consumed != nullptr) {
        *consumed = 0;
    }
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > kMaxHeaderBlockBytes) {
            return Result<Parsed>::withError(QStringLiteral("request header block too large"));
        }
        return Result<Parsed>::withValue(std::nullopt);
    }

    const QByteArray head = buffer.left(headerEnd);
    const int bodyStart = headerEnd + 4;
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty() || lines.first().trimmed().isEmpty()) {
        return Result<Parsed>::withError(QStringLiteral("empty request line"));
    }

    Request request;
    const QList<QByteArray> parts = lines.first().trimmed().split(' ');
    if (parts.size() != 3) {
        return Result<Parsed>::withError(QStringLiteral("malformed request line"));
    }
    if (!parts.at(2).startsWith("HTTP/1.")) {
        return Result<Parsed>::withError(QStringLiteral("unsupported HTTP version"));
    }
    request.method = QString::fromLatin1(parts.at(0)).toUpper();
    request.version = QString::fromLatin1(parts.at(2)).toUpper();
    request.target = QString::fromUtf8(parts.at(1));

    for (int index = 1; index < lines.size(); ++index) {
        QByteArray line = lines.at(index);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            return Result<Parsed>::withError(QStringLiteral("malformed header line"));
        }
        const QByteArray name = line.left(colon).toLower().trimmed();
        QByteArray value = line.mid(colon + 1);
        while (!value.isEmpty() && (value.at(0) == ' ' || value.at(0) == '\t')) {
            value.remove(0, 1);
        }
        while (!value.isEmpty()
               && (value.at(value.size() - 1) == ' ' || value.at(value.size() - 1) == '\t')) {
            value.chop(1);
        }
        if (name.isEmpty()) {
            return Result<Parsed>::withError(QStringLiteral("malformed header name"));
        }
        const QString key = QString::fromLatin1(name);
        // 重复头：hyper 保留多个值，但 axum 的 headers.get(AUTHORIZATION) 只看第一个。
        if (!request.headers.contains(key)) {
            request.headers.insert(key, QString::fromUtf8(value));
        }
    }

    const int queryMark = request.target.indexOf(QLatin1Char('?'));
    request.rawPath = queryMark < 0 ? request.target : request.target.left(queryMark);
    request.rawQuery = queryMark < 0 ? QString() : request.target.mid(queryMark + 1);
    request.path = decodePathSegment(request.rawPath);

    if (!request.rawQuery.isEmpty()) {
        const QStringList pairs = request.rawQuery.split(QLatin1Char('&'), Qt::SkipEmptyParts);
        for (const QString &pair : pairs) {
            const int separator = pair.indexOf(QLatin1Char('='));
            if (separator < 0) {
                request.queryPairs.append(qMakePair(decodeFormValue(pair), QString()));
            } else {
                request.queryPairs.append(qMakePair(decodeFormValue(pair.left(separator)),
                                                    decodeFormValue(pair.mid(separator + 1))));
            }
        }
    }

    // 请求体只支持固定长度（Content-Length）；chunked 明确不做。
    qint64 declared = 0;
    const std::optional<QString> lengthHeader = request.headerOrNull(QStringLiteral("content-length"));
    if (lengthHeader.has_value()) {
        bool ok = false;
        declared = lengthHeader->toLongLong(&ok);
        if (!ok || declared < 0) {
            return Result<Parsed>::withError(QStringLiteral("invalid content-length"));
        }
    }
    if (request.headers.contains(QStringLiteral("transfer-encoding"))) {
        return Result<Parsed>::withError(QStringLiteral("chunked request bodies are not supported"));
    }
    if (declared > kDefaultBodyLimitBytes) {
        // 对应 axum 的 DefaultBodyLimit(2 MiB)：光看 Content-Length 就拒，
        // 不是等收全了再说 —— 收全早就把内存吃光了。
        return Result<Parsed>::withError(QStringLiteral("payload too large"));
    }
    const qint64 available = buffer.size() - bodyStart;
    if (available < declared) {
        return Result<Parsed>::withValue(std::nullopt);
    }
    request.body = buffer.mid(bodyStart, static_cast<int>(declared));
    if (consumed != nullptr) {
        *consumed = bodyStart + static_cast<int>(declared);
    }
    return Result<Parsed>::withValue(std::move(request));
}

QString decodeFormValue(const QString &raw)
{
    // form_urlencoded::parse：'+' 是空格，%XX 是字节，非法序列原样保留，UTF-8 失败按 lossy。
    const QByteArray source = raw.toUtf8();
    QByteArray bytes;
    bytes.reserve(source.size());
    for (int index = 0; index < source.size(); ++index) {
        const char ch = source.at(index);
        if (ch == '%' && index + 2 < source.size()) {
            bool ok = false;
            const int value = source.mid(index + 1, 2).toUInt(&ok, 16);
            if (ok) {
                bytes.append(static_cast<char>(value));
                index += 2;
                continue;
            }
        }
        if (ch == '+') {
            bytes.append(' ');
            continue;
        }
        bytes.append(ch);
    }
    return QString::fromUtf8(bytes);
}

QString decodePathSegment(const QString &raw)
{
    const QByteArray source = raw.toUtf8();
    QByteArray bytes;
    bytes.reserve(source.size());
    for (int index = 0; index < source.size(); ++index) {
        const char ch = source.at(index);
        if (ch == '%' && index + 2 < source.size()) {
            bool ok = false;
            const int value = source.mid(index + 1, 2).toUInt(&ok, 16);
            if (ok) {
                bytes.append(static_cast<char>(value));
                index += 2;
                continue;
            }
        }
        bytes.append(ch);
    }
    return QString::fromUtf8(bytes);
}

QString reasonPhrase(int status)
{
    switch (status) {
    case 200:
        return QStringLiteral("OK");
    case 201:
        return QStringLiteral("Created");
    case 204:
        return QStringLiteral("No Content");
    case 400:
        return QStringLiteral("Bad Request");
    case 401:
        return QStringLiteral("Unauthorized");
    case 403:
        return QStringLiteral("Forbidden");
    case 404:
        return QStringLiteral("Not Found");
    case 405:
        return QStringLiteral("Method Not Allowed");
    case 409:
        return QStringLiteral("Conflict");
    case 413:
        return QStringLiteral("Payload Too Large");
    case 415:
        return QStringLiteral("Unsupported Media Type");
    case 422:
        return QStringLiteral("Unprocessable Entity");
    case 500:
        return QStringLiteral("Internal Server Error");
    case 503:
        return QStringLiteral("Service Unavailable");
    default:
        return QStringLiteral("Unknown");
    }
}

} // namespace Http

} // namespace Api
