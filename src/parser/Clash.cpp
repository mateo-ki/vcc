#include "parser/Clash.h"

#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

#include <yaml-cpp/yaml.h>

#include "core/JsonUtil.h"
#include "parser/Subscription.h"

namespace Parser {

namespace {

/// YAML core schema 的标量归类，与 serde_yaml 喂给 serde_json::Value 的结果一致：
/// plain scalar 先试 null/bool/int/float，都不像才是字符串。
enum class ScalarKind { Null, Bool, Int, Float, String };

ScalarKind classifyScalar(const QString& text)
{
    if (text.isEmpty() || text == QLatin1String("~") || text == QLatin1String("null")
        || text == QLatin1String("Null") || text == QLatin1String("NULL")) {
        return ScalarKind::Null;
    }
    if (text == QLatin1String("true") || text == QLatin1String("True")
        || text == QLatin1String("TRUE") || text == QLatin1String("false")
        || text == QLatin1String("False") || text == QLatin1String("FALSE")) {
        return ScalarKind::Bool;
    }
    // 整数：[-+]?[0-9]+
    {
        qsizetype i = 0;
        if (!text.isEmpty() && (text.at(0) == QLatin1Char('-') || text.at(0) == QLatin1Char('+'))) {
            i = 1;
        }
        bool digits = i < text.size();
        for (; i < text.size(); ++i) {
            const ushort u = text.at(i).unicode();
            if (u < u'0' || u > u'9') {
                digits = false;
                break;
            }
        }
        if (digits) {
            return ScalarKind::Int;
        }
    }
    // 浮点：[-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?
    {
        bool ok = false;
        text.toDouble(&ok);
        if (ok && (text.contains(QLatin1Char('.')) || text.contains(QLatin1Char('e'))
                   || text.contains(QLatin1Char('E')))) {
            return ScalarKind::Float;
        }
    }
    return ScalarKind::String;
}

/// yaml-cpp 的 Tag()：plain scalar 是 "?"（等着隐式解析），被引号包住或带非特定标签是 "!"。
/// serde_yaml 里"带引号的标量"永远是字符串，所以用 Tag 把这条信息找回来，
/// 避免 password: "123456" 被误判成数字。
ScalarKind kindOfScalarNode(const YAML::Node& node, const QString& text)
{
    const std::string tag = node.Tag();
    if (tag == "?" || tag.empty()) {
        return classifyScalar(text);
    }
    if (tag == "!!bool" || tag == "tag:yaml.org,2002:bool") {
        return ScalarKind::Bool;
    }
    if (tag == "!!int" || tag == "tag:yaml.org,2002:int") {
        return ScalarKind::Int;
    }
    if (tag == "!!float" || tag == "tag:yaml.org,2002:float") {
        return ScalarKind::Float;
    }
    if (tag == "!!null" || tag == "tag:yaml.org,2002:null") {
        return ScalarKind::Null;
    }
    // "!"、"!!str" 或其它显式标签：按字符串处理。
    return ScalarKind::String;
}

Json scalarToValue(const YAML::Node& node)
{
    const QString value = QString::fromStdString(node.Scalar());
    switch (kindOfScalarNode(node, value)) {
    case ScalarKind::Null:
        return Json();
    case ScalarKind::Bool:
        return Json(value == QLatin1String("true") || value == QLatin1String("True")
                    || value == QLatin1String("TRUE"));
    case ScalarKind::Int: {
        bool ok = false;
        if (!value.isEmpty() && value.at(0) != QLatin1Char('-')) {
            const quint64 parsed = value.toULongLong(&ok);
            if (ok) {
                return Json(parsed);
            }
        } else {
            const qint64 parsed = value.toLongLong(&ok);
            if (ok) {
                return Json(parsed);
            }
        }
        // 超出 64 位范围：Rust 侧会报错，这里保守地留成字符串。
        return Json(value.toStdString());
    }
    case ScalarKind::Float: {
        bool ok = false;
        const double parsed = value.toDouble(&ok);
        return ok ? Json(parsed) : Json(value.toStdString());
    }
    case ScalarKind::String:
        return Json(value.toStdString());
    }
    return Json(value.toStdString());
}

Json yamlToValue(const YAML::Node& node)
{
    switch (node.Type()) {
    case YAML::NodeType::Undefined:
    case YAML::NodeType::Null:
        return Json();
    case YAML::NodeType::Scalar:
        return scalarToValue(node);
    case YAML::NodeType::Sequence: {
        Json array = Json::array();
        for (const YAML::Node& item : node) {
            array.push_back(yamlToValue(item));
        }
        return array;
    }
    case YAML::NodeType::Map: {
        Json object = Json::object();
        for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
            object[QString::fromStdString(it->first.as<std::string>()).toStdString()] =
                yamlToValue(it->second);
        }
        return object;
    }
    }
    return Json();
}

/// serde_yaml 的标量文本：只在会被误读成别的类型时才加引号。
QString scalarToYaml(const Json& value)
{
    if (value.is_null()) {
        return QStringLiteral("null");
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return QString::fromStdString(value.dump());
    }
    if (value.is_number_float()) {
        return QString::fromStdString(value.dump());
    }
    const QString text = QString::fromStdString(value.get<std::string>());
    if (text.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    // 会被再读成 null/bool/int/float 的字符串必须加引号（与 classifyScalar 同源判断）。
    bool needsQuote = classifyScalar(text) != ScalarKind::String;
    const QByteArray utf8 = text.toUtf8();
    for (const char ch : utf8) {
        const unsigned char byte = unsigned(ch);
        if (byte < 0x20 || byte == 0x7F || ch == ':' || ch == '#' || ch == '\'' || ch == '"'
            || ch == '\\' || ch == '\n') {
            needsQuote = true;
            break;
        }
    }
    if (text.startsWith(QLatin1Char(' ')) || text.endsWith(QLatin1Char(' '))) {
        needsQuote = true;
    }
    if (!needsQuote) {
        return text;
    }
    QString quoted = QStringLiteral("\"");
    static const QHash<QChar, QString> kEscapes = {
        {QChar(u'"'), QStringLiteral("\\\"")},
        {QChar(u'\\'), QStringLiteral("\\\\")},
        {QChar(u'\n'), QStringLiteral("\\n")},
        {QChar(u'\r'), QStringLiteral("\\r")},
        {QChar(u'\t'), QStringLiteral("\\t")},
    };
    for (const QChar ch : text) {
        const auto found = kEscapes.find(ch);
        if (found != kEscapes.end()) {
            quoted += *found;
        } else if (ch.unicode() < 0x20) {
            quoted += QStringLiteral("\\x%1").arg(int(ch.unicode()), 2, 16, QLatin1Char('0'));
        } else {
            quoted += ch;
        }
    }
    quoted += QStringLiteral("\"");
    return quoted;
}

QString indentText(qsizetype indent)
{
    return QString(indent, QLatin1Char(' '));
}

/// 对应 Rust `serde_yaml::to_string(item)`。只覆盖 proxy 配置里会出现的形状（映射/数组/标量），
/// 引号策略取"必要才加"，与 serde_yaml 的默认风格一致；非 ASCII 保持明文。
QString dumpBlock(const Json& value, qsizetype indent);

void dumpMapping(const Json& value, qsizetype indent, QStringList& lines)
{
    for (auto it = value.begin(); it != value.end(); ++it) {
        const QString key = QString::fromStdString(it.key());
        const Json& child = it.value();
        if (child.is_object()) {
            if (child.empty()) {
                lines.append(indentText(indent) + key + QStringLiteral(": {}"));
            } else {
                lines.append(indentText(indent) + key + QStringLiteral(":"));
                lines.append(dumpBlock(child, indent + 2).split(QLatin1Char('\n')));
            }
        } else if (child.is_array()) {
            if (child.empty()) {
                lines.append(indentText(indent) + key + QStringLiteral(": []"));
            } else {
                // serde_yaml 的块序列与父键同缩进。
                lines.append(indentText(indent) + key + QStringLiteral(":"));
                lines.append(dumpBlock(child, indent).split(QLatin1Char('\n')));
            }
        } else {
            lines.append(indentText(indent) + key + QStringLiteral(": ") + scalarToYaml(child));
        }
    }
}

QString dumpBlock(const Json& value, qsizetype indent)
{
    QStringList lines;
    if (value.is_object()) {
        dumpMapping(value, indent, lines);
        return lines.join(QLatin1Char('\n'));
    }
    if (value.is_array()) {
        for (const Json& item : value) {
            if (item.is_object()) {
                if (item.empty()) {
                    lines.append(indentText(indent) + QStringLiteral("- {}"));
                    continue;
                }
                const QString nested = dumpBlock(item, indent + 2);
                QStringList nestedLines = nested.split(QLatin1Char('\n'));
                QString first = nestedLines.takeFirst();
                first = indentText(indent) + QStringLiteral("- ")
                    + first.mid(int(indent) + 2);
                lines.append(first);
                lines.append(nestedLines);
            } else if (item.is_array()) {
                if (item.empty()) {
                    lines.append(indentText(indent) + QStringLiteral("- []"));
                    continue;
                }
                const QString nested = dumpBlock(item, indent + 2);
                QStringList nestedLines = nested.split(QLatin1Char('\n'));
                QString first = nestedLines.takeFirst();
                first = indentText(indent) + QStringLiteral("- ")
                    + first.mid(int(indent) + 2);
                lines.append(first);
                lines.append(nestedLines);
            } else {
                lines.append(indentText(indent) + QStringLiteral("- ") + scalarToYaml(item));
            }
        }
        return lines.join(QLatin1Char('\n'));
    }
    return indentText(indent) + scalarToYaml(value);
}

QString dumpYaml(const Json& value)
{
    // serde_yaml 的输出总是以换行结尾。
    return dumpBlock(value, 0) + QStringLiteral("\n");
}

/// serde_json 的 `v.as_u64()`。
std::optional<quint64> asU64(const Json& value)
{
    if (!value.is_number_unsigned()) {
        return std::nullopt;
    }
    return value.get<quint64>();
}

std::optional<QString> asText(const Json& value)
{
    if (!value.is_string()) {
        return std::nullopt;
    }
    return QString::fromStdString(value.get<std::string>());
}

/// `obj.get("port").and_then(|v| as_u64→u16 or as_str→u16 parse)`，取不到就是 0（Rust 的 unwrap_or(0)）。
int portFromValue(const Json& item)
{
    const auto found = item.find("port");
    if (found == item.end()) {
        return 0;
    }
    if (const std::optional<quint64> number = asU64(*found)) {
        if (*number <= quint64(65535)) {
            return int(*number);
        }
        return 0;
    }
    if (const std::optional<QString> text = asText(*found)) {
        int port = 0;
        // Rust 的 `str::parse::<u16>()`：只允许可选前导 '+' 加纯数字。
        if (Common::parsePortNumber(*text, port)) {
            return port;
        }
    }
    return 0;
}

QString textFromObject(const Json& obj, const char* key)
{
    const auto found = obj.find(key);
    if (found == obj.end()) {
        return QString();
    }
    if (const std::optional<QString> text = asText(*found)) {
        return *text;
    }
    return QString();
}

std::optional<Models::Node> proxyToNode(const Json& item, const QString& sourceId)
{
    if (!item.is_object()) {
        return std::nullopt;
    }
    const QString type = Common::toAsciiLower(textFromObject(item, "type"));
    const QString name = textFromObject(item, "name");
    const QString server = textFromObject(item, "server");
    if (server.isEmpty()) {
        return std::nullopt;
    }
    const int port = portFromValue(item);
    if (port == 0) {
        return std::nullopt;
    }

    QString protocol = type;
    if (type == QLatin1String("ss") || type == QLatin1String("shadowsocks")) {
        protocol = QStringLiteral("shadowsocks");
    } else if (type == QLatin1String("ssr")) {
        protocol = QStringLiteral("shadowsocksr");
    } else if (type == QLatin1String("vmess")) {
        protocol = QStringLiteral("vmess");
    } else if (type == QLatin1String("vless")) {
        protocol = QStringLiteral("vless");
    } else if (type == QLatin1String("trojan")) {
        protocol = QStringLiteral("trojan");
    } else if (type == QLatin1String("hysteria2") || type == QLatin1String("hy2")) {
        protocol = QStringLiteral("hysteria2");
    } else if (type == QLatin1String("hysteria")) {
        protocol = QStringLiteral("hysteria");
    } else if (type == QLatin1String("wireguard") || type == QLatin1String("wg")) {
        protocol = QStringLiteral("wireguard");
    } else if (type == QLatin1String("http") || type == QLatin1String("https")) {
        protocol = QStringLiteral("http");
    } else if (type == QLatin1String("socks5") || type == QLatin1String("socks")) {
        protocol = QStringLiteral("socks5");
    } else if (type == QLatin1String("tuic")) {
        protocol = QStringLiteral("tuic");
    } else if (type.isEmpty()) {
        protocol = QStringLiteral("unknown");
    }

    // config 里保留 Clash 的短类型名，导出时才对得上下游客户端。
    QString clashType = protocol;
    if (protocol == QLatin1String("shadowsocks")) {
        clashType = QStringLiteral("ss");
    } else if (protocol == QLatin1String("shadowsocksr")) {
        clashType = QStringLiteral("ssr");
    } else if (protocol == QLatin1String("wireguard")) {
        clashType = QStringLiteral("wireguard");
    }

    Json config = item;
    if (config.is_object()) {
        config["type"] = clashType.toStdString();
        config["server"] = server.toStdString();
        config["port"] = port;
        config["name"] = name.toStdString();
    }

    // raw 是原始 item 的 YAML 文本（不含上面覆盖的四个字段），对应 serde_yaml::to_string(item)。
    const QString raw = dumpYaml(item);
    return makeNode(protocol, name, server, port, raw, config, sourceId);
}

} // namespace

Result<QVector<Models::Node>> parseClashYaml(const QString& content, const QString& sourceId)
{
    YAML::Node root;
    try {
        root = YAML::Load(content.toStdString());
    } catch (const YAML::Exception& error) {
        // yaml-cpp 只有会抛异常的解析入口（nlohmann 有 accept，所以 JsonUtil 之外只有这里
        // 需要 try/catch），异常必须就地转成 Result，不能跨过 API 边界。
        return Result<QVector<Models::Node>>::withError(QString::fromUtf8(error.what()));
    }
    if (root.Type() == YAML::NodeType::Undefined) {
        return Result<QVector<Models::Node>>::withError(
            QStringLiteral("empty document（对应 serde_yaml 的 EOF while parsing）"));
    }

    const Json value = yamlToValue(root);
    Json proxies = Json::array();
    if (value.is_object()) {
        const auto found = value.find("proxies");
        if (found != value.end()) {
            proxies = *found;
        }
    } else if (value.is_array()) {
        // 有些文件就是一个纯列表。
        proxies = value;
    }

    QVector<Models::Node> nodes;
    if (proxies.is_array()) {
        for (const Json& item : proxies) {
            if (const auto node = proxyToNode(item, sourceId)) {
                nodes.append(*node);
            }
        }
    }
    return Result<QVector<Models::Node>>::withValue(nodes);
}

} // namespace Parser
