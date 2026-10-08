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

/// serde_yaml 走的是 YAML core schema：plain scalar 会先解析成 null/bool/int/float，
/// 解析不了才是 string。yaml-cpp 的 Node 不保留引号风格，所以被引号包住却长得像数字的
/// 标量在这里会被当成数字（Rust 会当成字符串）；只影响 config 里该字段的 JSON 类型。
Json scalarToValue(const YAML::Node& node)
{
    const std::string text = node.Scalar();
    const QString value = QString::fromStdString(text);

    if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") {
        return Json();
    }
    if (text == "true" || text == "True" || text == "TRUE") {
        return Json(true);
    }
    if (text == "false" || text == "False" || text == "FALSE") {
        return Json(false);
    }

    // 整数：[-+]?[0-9]+
    bool isInteger = !value.isEmpty();
    qsizetype digitStart = 0;
    if (isInteger && (value.at(0) == QLatin1Char('-') || value.at(0) == QLatin1Char('+'))) {
        digitStart = 1;
    }
    bool hasDigit = false;
    for (qsizetype i = digitStart; i < value.size() && isInteger; ++i) {
        const ushort u = value.at(i).unicode();
        if (u >= u'0' && u <= u'9') {
            hasDigit = true;
        } else {
            isInteger = false;
        }
    }
    if (isInteger && hasDigit) {
        bool ok = false;
        if (value.at(0) != QLatin1Char('-')) {
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

    // 浮点：只在明显是小数/指数形式时转换。
    if (value.contains(QLatin1Char('.')) || value.contains(QLatin1Char('e'))
        || value.contains(QLatin1Char('E'))) {
        bool ok = false;
        const double parsed = value.toDouble(&ok);
        QChar head = value.at(0);
        const bool numericLead = (head.unicode() >= u'0' && head.unicode() <= u'9')
            || head == QLatin1Char('-') || head == QLatin1Char('+') || head == QLatin1Char('.');
        if (ok && numericLead) {
            return Json(parsed);
        }
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
        for (const auto& pair : node.as<YAML::ConstMapNode>()) {
            object[QString::fromStdString(pair.first.as<std::string>()).toStdString()] =
                yamlToValue(pair.second);
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
    bool needsQuote = false;
    const QByteArray utf8 = text.toUtf8();
    for (const char ch : utf8) {
        const unsigned char byte = unsigned(ch);
        if (byte < 0x20 || byte == 0x7F || ch == ':' || ch == '#' || ch == '\'' || ch == '"'
            || ch == '\\' || ch == '\n') {
            needsQuote = true;
            break;
        }
    }
    // 会被重新解析成别的类型的字符串必须引号化。
    static const QStringList kReserved = {QStringLiteral("null"), QStringLiteral("~"),
                                          QStringLiteral("true"), QStringLiteral("false"),
                                          QStringLiteral("True"), QStringLiteral("False"),
                                          QStringLiteral("yes"),  QStringLiteral("no")};
    if (kReserved.contains(text)) {
        needsQuote = true;
    }
    bool allDigits = true;
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if (!((u >= u'0' && u <= u'9') || (u == u'.' && allDigits) || (u == u'-' && text.at(0) == ch)
              || (u == u'+' && text.at(0) == ch))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        needsQuote = true;
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

/// `obj.get("port").and_then(|v| as_u64→u16 or as_str→parse)`，取不到就是 0。
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
        if (QStringView(*text).size() > 0) {
            bool ok = false;
            const quint64 parsed = text->toULongLong(&ok);
            if (ok && parsed <= quint64(65535) && !text->contains(QLatin1Char('-'))) {
                port = int(parsed);
                // Rust 的 u16 parse 不允许空格/加号之外的字符。
                for (const QChar ch : *text) {
                    const ushort u = ch.unicode();
                    if (!(u >= u'0' && u <= u'9')) {
                        return 0;
                    }
                }
                return port;
            }
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
