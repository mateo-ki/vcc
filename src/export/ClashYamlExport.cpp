#include "export/ClashYamlExport.h"

#include <yaml-cpp/yaml.h>

#include <QString>
#include <QVector>

#include <cstdint>

#include "export/ClashProxy.h"

namespace Export {
namespace {

void emitValue(YAML::Emitter &emitter, const Json &value)
{
    switch (value.type()) {
    case Json::value_t::object: {
        const Json::object_t &obj = value.get_ref<const Json::object_t &>();
        if (obj.empty()) {
            // serde_yaml 对空映射写 {}，这里也保持 flow 样式，避免落成孤零零的一行。
            emitter << YAML::Flow << YAML::BeginMap << YAML::EndMap << YAML::Auto;
            return;
        }
        emitter << YAML::BeginMap;
        for (const auto &entry : obj) {
            emitter << YAML::Key << entry.first << YAML::Value;
            emitValue(emitter, entry.second);
        }
        emitter << YAML::EndMap;
        return;
    }
    case Json::value_t::array: {
        if (value.empty()) {
            emitter << YAML::Flow << YAML::BeginSeq << YAML::EndSeq << YAML::Auto;
            return;
        }
        emitter << YAML::BeginSeq;
        for (const Json &item : value) {
            emitValue(emitter, item);
        }
        emitter << YAML::EndSeq;
        return;
    }
    case Json::value_t::null:
        emitter << YAML::Null;
        return;
    case Json::value_t::boolean:
        emitter << value.get<bool>();
        return;
    case Json::value_t::string:
        emitter << value.get_ref<const std::string &>();
        return;
    case Json::value_t::number_unsigned:
        emitter << value.get<std::uint64_t>();
        return;
    case Json::value_t::number_integer:
        emitter << value.get<std::int64_t>();
        return;
    case Json::value_t::number_float:
        // serde_yaml 用 Rust 的最短浮点文本（1.0），yaml-cpp 走 ostream 默认格式，
        // 整数值浮点会被写成 1；代理配置里的数字基本都是整数，这里只做数字通道输出。
        emitter << value.get<double>();
        return;
    case Json::value_t::binary:
    case Json::value_t::discarded:
        break;
    }
    // nlohmann 不会有 binary/discarded 出现在这条链路上；真出现了就按 JSON 文本写。
    emitter << value.dump();
}

} // namespace

QString jsonToYaml(const Json &value)
{
    YAML::Emitter emitter;
    emitter.SetIndent(2);
    // serde_yaml 写小写 true/false 和 null，不带 --- 文档标记。
    emitter.SetBoolFormat(YAML::TrueFalseBool);
    emitter.SetNullFormat(YAML::LowerNull);
    emitValue(emitter, value);
    return QString::fromUtf8(emitter.c_str(), static_cast<qsizetype>(emitter.size()));
}

Json clashProxiesDocument(const QVector<Models::Node> &nodes)
{
    const QVector<Json> proxies = nodesToClashProxies(nodes);
    Json proxyArray = Json::array();
    Json names = Json::array();
    for (const Json &proxy : proxies) {
        proxyArray.push_back(proxy);
        const auto named = proxy.find(std::string("name"));
        if (named != proxy.end() && named->is_string()) {
            names.push_back(*named);
        }
    }

    Json group = Json::object();
    group["name"] = "PROXY";
    group["type"] = "select";
    group["proxies"] = names;

    Json groupList = Json::array();
    groupList.push_back(group);

    Json rules = Json::array();
    rules.push_back("MATCH,PROXY");

    Json doc = Json::object();
    doc["port"] = 9980;
    doc["socks-port"] = 7891;
    doc["allow-lan"] = false;
    doc["mode"] = "rule";
    doc["log-level"] = "info";
    doc["external-controller"] = "127.0.0.1:9090";
    doc["proxies"] = proxyArray;
    doc["proxy-groups"] = groupList;
    doc["rules"] = rules;
    return doc;
}

QString clashProxiesYaml(const QVector<Models::Node> &nodes)
{
    return jsonToYaml(clashProxiesDocument(nodes));
}

Json jsonExportDocument(const QVector<Models::Node> &nodes, bool qualityOnly, qint64 maxLatencyMs,
                        double minDownloadKbps, std::optional<quint32> limit)
{
    Json configs = Json::array();
    for (const Models::Node &node : nodes) {
        configs.push_back(node.config);
    }

    Json doc = Json::object();
    doc["count"] = static_cast<std::int64_t>(nodes.size());
    doc["quality_only"] = qualityOnly;
    doc["max_latency_ms"] = maxLatencyMs;
    doc["min_download_kbps"] = minDownloadKbps;
    doc["limit"] = limit.has_value() ? Json(static_cast<std::uint64_t>(*limit)) : Json();
    doc["note"] = "Export uses stored test results only and does not run network tests. limit is "
                  "the maximum number of nodes returned.";
    doc["nodes"] = configs;
    return doc;
}

} // namespace Export
