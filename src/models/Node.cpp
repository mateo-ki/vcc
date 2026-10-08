#include "models/Node.h"

namespace Models {

namespace {

using JsonUtil::boolOr;
using JsonUtil::doubleOr;
using JsonUtil::intOr;
using JsonUtil::nullableText;
using JsonUtil::optText;
using JsonUtil::textOrEmpty;

Json optLatencyMs(const std::optional<qint64> &value)
{
    return value.has_value() ? Json(*value) : Json();
}

Json optLatencyOk(const std::optional<bool> &value)
{
    return value.has_value() ? Json(*value) : Json();
}

Json optDownload(const std::optional<double> &value)
{
    return value.has_value() ? Json(*value) : Json();
}

std::optional<qint64> readOptionalInt(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null() || !found->is_number()) {
        return std::nullopt;
    }
    return found->get<qint64>();
}

std::optional<double> readOptionalDouble(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null() || !found->is_number()) {
        return std::nullopt;
    }
    return found->get<double>();
}

std::optional<bool> readOptionalBool(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null() || !found->is_boolean()) {
        return std::nullopt;
    }
    return found->get<bool>();
}

void fillSharedFields(Json &value, const QString &id, const QString &sourceId,
                      const QString &protocol, const QString &name, const QString &server,
                      int port, const QString &country, const std::optional<qint64> &latencyMs,
                      const std::optional<bool> &latencyOk, const std::optional<double> &download,
                      const QString &latencyKernel, const QString &latencyTestKind,
                      const TimeUtil::OptionalUtc &latencyTestedAt)
{
    value["id"] = id.toStdString();
    value["source_id"] = optText(sourceId);
    value["protocol"] = protocol.toStdString();
    value["name"] = name.toStdString();
    value["server"] = server.toStdString();
    value["port"] = port;
    value["country"] = optText(country);
    value["latency_ms"] = optLatencyMs(latencyMs);
    value["latency_ok"] = optLatencyOk(latencyOk);
    value["download_kbps"] = optDownload(download);
    value["latency_kernel"] = optText(latencyKernel);
    value["latency_test_kind"] = optText(latencyTestKind);
    value["latency_tested_at"] = optText(latencyTestedAt.toJsonText());
}

}

Json Node::toJson() const
{
    Json value;
    fillSharedFields(value, id, sourceId, protocol, name, server, port, country, latencyMs,
                     latencyOk, downloadKbps, latencyKernel, latencyTestKind, latencyTestedAt);
    value["raw"] = raw.toStdString();
    value["config"] = config;
    value["created_at"] = optText(createdAt.toJsonText());
    value["updated_at"] = optText(updatedAt.toJsonText());
    return value;
}

Node Node::fromJson(const Json &value)
{
    Node node;
    node.id = textOrEmpty(value, "id");
    node.sourceId = nullableText(value, "source_id");
    node.protocol = textOrEmpty(value, "protocol");
    node.name = textOrEmpty(value, "name");
    node.server = textOrEmpty(value, "server");
    node.port = intOr(value, "port", 0);
    node.raw = nullableText(value, "raw");
    if (value.is_object() && value.contains("config") && !value.at("config").is_null()) {
        node.config = value.at("config");
    }
    node.country = nullableText(value, "country");
    node.latencyMs = readOptionalInt(value, "latency_ms");
    node.latencyOk = readOptionalBool(value, "latency_ok");
    node.downloadKbps = readOptionalDouble(value, "download_kbps");
    node.latencyKernel = nullableText(value, "latency_kernel");
    node.latencyTestKind = nullableText(value, "latency_test_kind");
    node.latencyTestedAt = TimeUtil::OptionalUtc::fromText(nullableText(value, "latency_tested_at"));
    node.createdAt = TimeUtil::OptionalUtc::fromText(nullableText(value, "created_at"));
    node.updatedAt = TimeUtil::OptionalUtc::fromText(nullableText(value, "updated_at"));
    return node;
}

Json NodeSummary::toJson() const
{
    Json value;
    fillSharedFields(value, id, sourceId, protocol, name, server, port, country, latencyMs,
                     latencyOk, downloadKbps, latencyKernel, latencyTestKind, latencyTestedAt);
    value["updated_at"] = optText(updatedAt.toJsonText());
    return value;
}

Json NodeGroupSummary::toJson() const
{
    Json value;
    value["source_id"] = optText(sourceId);
    value["source_name"] = sourceName.toStdString();
    value["total"] = total;
    value["alive"] = alive;
    return value;
}

}
