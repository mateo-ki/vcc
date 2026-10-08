#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/Encoding.h"
#include "core/JsonUtil.h"
#include "core/TimeUtil.h"
#include "export/ClashProxy.h"
#include "export/ClashYamlExport.h"
#include "export/QualityExport.h"
#include "export/UriExport.h"

namespace {

using Models::Node;

/// 与 export_quality.rs 之外的 Rust 测试保持同一套 fixture 构造方式：
/// 配置一律先过 JsonUtil::parse，等价于 Rust 的 json! 字面量。
Json parseFixture(const char *text)
{
    return JsonUtil::parseOrEmptyObject(QString::fromUtf8(text));
}

/// config[key] 是字符串才算命中（Rust 的 get().and_then(as_str)）。
std::optional<QString> asString(const Json &cfg, const char *key)
{
    const auto *obj = cfg.get_ptr<const Json::object_t *>();
    if (obj == nullptr) {
        return std::nullopt;
    }
    const auto found = obj->find(std::string(key));
    if (found == obj->end() || !found->second.is_string()) {
        return std::nullopt;
    }
    return QString::fromStdString(found->second.get<std::string>());
}

/// 复刻 export.rs 测试里的 sample_node。
Node sampleNode(const QString &protocol, const Json &config, const QString &raw)
{
    Node node;
    node.id = QStringLiteral("1");
    node.sourceId = QString();
    node.protocol = protocol;
    node.name = asString(config, "name").value_or(QStringLiteral("n"));
    const auto server = asString(config, "server");
    node.server = server.has_value() ? server.value()
                                     : asString(config, "add").value_or(QStringLiteral("1.2.3.4"));
    node.port = 443;
    if (const auto *obj = config.get_ptr<const Json::object_t *>()) {
        const auto found = obj->find(std::string("port"));
        if (found != obj->end() && found->second.is_number_unsigned()) {
            const std::uint64_t raw64 = found->second.get<std::uint64_t>();
            if (raw64 <= 0xFFFFULL) {
                node.port = static_cast<int>(raw64);
            }
        }
    }
    node.raw = raw;
    node.config = config;
    const TimeUtil::OptionalUtc now = TimeUtil::OptionalUtc::now();
    node.createdAt = now;
    node.updatedAt = now;
    return node;
}

/// 复刻 export_quality.rs 测试里的 node()。
Node qualityNode(const QString &protocol, int port, qint64 ms, const Json &config)
{
    Node node;
    node.id = QStringLiteral("1");
    node.sourceId = QString();
    node.protocol = protocol;
    node.name = QStringLiteral("n");
    node.server = QStringLiteral("1.2.3.4");
    node.port = port;
    node.raw = QString();
    node.config = config;
    node.latencyMs = ms;
    node.latencyOk = true;
    const TimeUtil::OptionalUtc now = TimeUtil::OptionalUtc::now();
    node.createdAt = now;
    node.updatedAt = now;
    return node;
}

const Json &field(const Json &value, const char *key)
{
    static const Json kMissing = Json();
    const auto *obj = value.get_ptr<const Json::object_t *>();
    if (obj == nullptr) {
        return kMissing;
    }
    const auto found = obj->find(std::string(key));
    return found == obj->end() ? kMissing : found->second;
}

bool hasField(const Json &value, const char *key)
{
    const auto *obj = value.get_ptr<const Json::object_t *>();
    if (obj == nullptr) {
        return false;
    }
    return obj->find(std::string(key)) != obj->end();
}

} // namespace

// ---------------------------------------------------------------- export.rs

TEST(Export, clash_type_ss_not_shadowsocks)
{
    const Node node = sampleNode(
        QStringLiteral("shadowsocks"),
        parseFixture(R"({
            "type": "shadowsocks",
            "server": "1.2.3.4",
            "port": 8388,
            "cipher": "aes-256-gcm",
            "password": "pw",
            "name": "ss1"
        })"),
        QStringLiteral("cipher: aes-256-gcm\n"));

    const QVector<Json> proxies = Export::nodesToClashProxies({node});
    ASSERT_EQ(proxies.size(), 1);
    EXPECT_EQ(field(proxies[0], "type"), Json("ss"));
    EXPECT_EQ(field(proxies[0], "cipher"), Json("aes-256-gcm"));
}

TEST(Export, clash_vmess_from_uri_config)
{
    const Node node = sampleNode(
        QStringLiteral("vmess"),
        parseFixture(R"({
            "add": "8.8.8.8",
            "id": "04621bae-ab36-11ec-b909-0242ac120002",
            "name": "v1",
            "net": "tcp",
            "port": 22324,
            "ps": "v1",
            "scy": "auto",
            "server": "8.8.8.8",
            "type": "vmess",
            "v": "2"
        })"),
        QStringLiteral("vmess://xxx"));

    const QVector<Json> proxies = Export::nodesToClashProxies({node});
    ASSERT_EQ(proxies.size(), 1);
    EXPECT_EQ(field(proxies[0], "type"), Json("vmess"));
    EXPECT_EQ(field(proxies[0], "uuid"), Json("04621bae-ab36-11ec-b909-0242ac120002"));
    EXPECT_EQ(field(proxies[0], "server"), Json("8.8.8.8"));
    EXPECT_EQ(field(proxies[0], "network"), Json("tcp"));
    EXPECT_FALSE(hasField(proxies[0], "add"));
    EXPECT_FALSE(hasField(proxies[0], "id"));
}

TEST(Export, clash_vless_ws_tls)
{
    const Node node = sampleNode(
        QStringLiteral("vless"),
        parseFixture(R"({
            "type": "vless",
            "server": "1.1.1.1",
            "port": 443,
            "uuid": "f775978b-6e22-41a8-ae5e-5f9779e6745f",
            "name": "vl",
            "network": "ws",
            "security": "tls",
            "sni": "example.com",
            "host": "example.com",
            "path": "/ws",
            "encryption": "none"
        })"),
        QStringLiteral("vless://f775978b-6e22-41a8-ae5e-5f9779e6745f@1.1.1.1:443?type=ws"));

    const QVector<Json> proxies = Export::nodesToClashProxies({node});
    ASSERT_EQ(proxies.size(), 1);
    EXPECT_EQ(field(proxies[0], "type"), Json("vless"));
    EXPECT_EQ(field(proxies[0], "tls"), Json(true));
    EXPECT_EQ(field(proxies[0], "network"), Json("ws"));
    EXPECT_EQ(field(field(proxies[0], "ws-opts"), "path"), Json("/ws"));
    EXPECT_EQ(field(field(field(proxies[0], "ws-opts"), "headers"), "Host"),
              Json("example.com"));
    EXPECT_EQ(field(proxies[0], "servername"), Json("example.com"));
    EXPECT_FALSE(hasField(proxies[0], "security"));
}

TEST(Export, uri_rebuild_from_yaml_ss)
{
    const Node node = sampleNode(
        QStringLiteral("shadowsocks"),
        parseFixture(R"({
            "type": "shadowsocks",
            "server": "1.2.3.4",
            "port": 443,
            "cipher": "aes-256-cfb",
            "password": "pw",
            "name": "ss-yaml"
        })"),
        QStringLiteral("cipher: aes-256-cfb\nname: ss-yaml\n"));

    const QVector<QString> uris = Export::nodesToShareUris({node});
    ASSERT_EQ(uris.size(), 1);
    EXPECT_TRUE(uris[0].startsWith(QLatin1String("ss://")));
    EXPECT_TRUE(uris[0].contains(QLatin1String("@1.2.3.4:443")));
}

TEST(Export, unique_proxy_names)
{
    const Node n1 = sampleNode(
        QStringLiteral("trojan"),
        parseFixture(
            R"({"type":"trojan","server":"1.1.1.1","port":443,"password":"p","name":"same"})"),
        QStringLiteral("trojan://p@1.1.1.1:443#same"));
    const Node n2 = sampleNode(
        QStringLiteral("trojan"),
        parseFixture(
            R"({"type":"trojan","server":"2.2.2.2","port":443,"password":"p","name":"same"})"),
        QStringLiteral("trojan://p@2.2.2.2:443#same"));

    const QVector<Json> proxies = Export::nodesToClashProxies({n1, n2});
    ASSERT_EQ(proxies.size(), 2);
    EXPECT_EQ(field(proxies[0], "name"), Json("same"));
    EXPECT_EQ(field(proxies[1], "name"), Json("same-2"));
}

TEST(Export, rebuild_mislabeled_ss_raw_as_vless)
{
    // protocol/config 是 vless，但 raw 还写着 ss://
    const Node node = sampleNode(
        QStringLiteral("vless"),
        parseFixture(R"({
            "type": "vless",
            "server": "185.143.233.200",
            "port": 80,
            "uuid": "0b6705dc-157e-46a1-8872-2fa6923e8d83",
            "name": "fix-me",
            "network": "ws",
            "security": "none",
            "host": "final.idbedam.ir",
            "path": "/?ed=2080",
            "encryption": "none"
        })"),
        QStringLiteral(
            "ss://0b6705dc-157e-46a1-8872-2fa6923e8d83@185.143.233.200:80?path=%2F&security=none&type=ws#bad"));

    const QVector<QString> uris = Export::nodesToShareUris({node});
    ASSERT_EQ(uris.size(), 1);
    EXPECT_TRUE(uris[0].startsWith(QLatin1String("vless://"))) << uris[0].toStdString();
    EXPECT_TRUE(uris[0].contains(QLatin1String("type=ws")));
    EXPECT_TRUE(uris[0].contains(
        QLatin1String("0b6705dc-157e-46a1-8872-2fa6923e8d83@185.143.233.200:80")));
}

// -------------------------------------------------------- export_quality.rs

TEST(ExportQuality, prefers_reality_over_none)
{
    const Node good = qualityNode(QStringLiteral("vless"), 443, 200,
                                  parseFixture(R"({"uuid":"a","security":"reality","sni":"a.com"})"));
    const Node bad = qualityNode(QStringLiteral("vless"), 80, 10,
                                 parseFixture(R"({"uuid":"a","security":"none","network":"ws"})"));

    EXPECT_GT(ExportQuality::exportScore(good), ExportQuality::exportScore(bad));
    EXPECT_TRUE(ExportQuality::isStrictExportCandidate(good));
    EXPECT_FALSE(ExportQuality::isStrictExportCandidate(bad));
}

// ------------------------------------------------------- 交叉验证（额外用例）

/// vmess 分享链接是 base64(JSON)，其他客户端逐字比对这串文本：
/// 键集合必须与 serde_json 一致、按键字节序排列、整数不带小数点。
TEST(ExportCrossValidation, vmess_share_uri_is_byte_identical_to_serde_json)
{
    const Node node = sampleNode(
        QStringLiteral("vmess"),
        parseFixture(R"({
            "add": "8.8.8.8",
            "id": "04621bae-ab36-11ec-b909-0242ac120002",
            "name": "v1",
            "net": "tcp",
            "port": 22324,
            "ps": "v1",
            "scy": "auto",
            "server": "8.8.8.8",
            "type": "vmess",
            "v": "2"
        })"),
        QStringLiteral("vmess://xxx"));

    const QString uri = Export::nodeToShareUri(node);
    ASSERT_FALSE(uri.isNull());
    ASSERT_TRUE(uri.startsWith(QLatin1String("vmess://")));

    const Result<QByteArray> decoded =
        Encoding::decode(uri.mid(8).toLatin1(), Encoding::Engine::Standard);
    ASSERT_TRUE(decoded.isOk());
    const QString expectedJson = QStringLiteral(
        R"({"add":"8.8.8.8","aid":"0","alpn":"","host":"","id":"04621bae-ab36-11ec-b909-0242ac120002",)"
        R"("net":"tcp","path":"","port":"22324","ps":"v1","scy":"auto","sni":"","tls":"","type":"none","v":"2"})");
    EXPECT_EQ(QString::fromUtf8(decoded.value()), expectedJson);
}

/// ss:// 的 userinfo 是 base64("cipher:password")，中文备注要按 UTF-8 逐字节百分号编码。
TEST(ExportCrossValidation, ss_uri_userinfo_and_cjk_fragment)
{
    Node node = sampleNode(
        QStringLiteral("shadowsocks"),
        parseFixture(R"({
            "type": "shadowsocks",
            "server": "1.2.3.4",
            "port": 443,
            "cipher": "aes-256-cfb",
            "password": "pw",
            "name": "ss-yaml"
        })"),
        QString());
    EXPECT_EQ(Export::nodeToShareUri(node),
              QStringLiteral("ss://YWVzLTI1Ni1jZmI6cHc=@1.2.3.4:443#ss-yaml"));

    // "中文" 走 fromUcs4，避免源文件里手写 CJK。
    static const char32_t kCjk[2] = {0x4E2D, 0x6587};
    const QString cjk = QString::fromUcs4(kCjk, 2);
    node.name = cjk;
    node.config["name"] = cjk.toStdString();
    EXPECT_EQ(Export::nodeToShareUri(node),
              QStringLiteral("ss://YWVzLTI1Ni1jZmI6cHc=@1.2.3.4:443#%E4%B8%AD%E6%96%87"));
}

/// vless 的 query 顺序、编码和 '?' 都必须与 Rust 的 format! 一致。
TEST(ExportCrossValidation, vless_query_order_and_percent_encoding)
{
    const Node node = sampleNode(
        QStringLiteral("vless"),
        parseFixture(R"({
            "type": "vless",
            "server": "185.143.233.200",
            "port": 80,
            "uuid": "0b6705dc-157e-46a1-8872-2fa6923e8d83",
            "name": "fix-me",
            "network": "ws",
            "security": "none",
            "host": "final.idbedam.ir",
            "path": "/?ed=2080",
            "encryption": "none"
        })"),
        QString());
    EXPECT_EQ(Export::nodeToShareUri(node),
              QStringLiteral("vless://0b6705dc-157e-46a1-8872-2fa6923e8d83@185.143.233.200:80"
                             "?encryption=none&type=ws&path=%2F%3Fed%3D2080"
                             "&host=final.idbedam.ir#fix-me"));
}

/// serde_yaml 的文档键序来自 serde_json 的 BTreeMap（字节序），
/// Clash 档案的顶层键必须按这个顺序写出来。
TEST(ExportCrossValidation, clashYamlTopLevelKeysAreByteSorted)
{
    const QVector<Node> nodes{sampleNode(
        QStringLiteral("shadowsocks"),
        parseFixture(R"({"type":"shadowsocks","server":"1.2.3.4","port":8388,
                         "cipher":"aes-256-gcm","password":"pw","name":"ss1"})"),
        QStringLiteral("cipher: aes-256-gcm\n"))};

    const QString yaml = Export::clashProxiesYaml(nodes);
    QStringList topLevel;
    for (const QString &line : yaml.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('-'))) {
            continue;
        }
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            topLevel.append(line.left(colon));
        }
    }
    // gtest 宏会把花括号里的逗号当成参数分隔，所以先落成变量再比较。
    const QStringList expectedKeys{QStringLiteral("allow-lan"),
                                   QStringLiteral("external-controller"),
                                   QStringLiteral("log-level"), QStringLiteral("mode"),
                                   QStringLiteral("port"), QStringLiteral("proxies"),
                                   QStringLiteral("proxy-groups"), QStringLiteral("rules"),
                                   QStringLiteral("socks-port")};
    EXPECT_EQ(topLevel, expectedKeys);

    // 代理对象内部的键同样是字节序。
    EXPECT_TRUE(yaml.contains(QLatin1String("MATCH,PROXY")));
    EXPECT_TRUE(yaml.contains(QLatin1String("PROXY")));
}

/// JSON 导出文档：键序、类型（整数不带小数点）和 note 文本都要和 Rust 的 json! 对齐。
TEST(ExportCrossValidation, jsonExportDocumentMatchesSerdeShape)
{
    const QVector<Node> nodes{sampleNode(
        QStringLiteral("trojan"),
        parseFixture(R"({"type":"trojan","server":"1.1.1.1","port":443,"password":"p","name":"same"})"),
        QStringLiteral("trojan://p@1.1.1.1:443#same"))};

    const Json doc = Export::jsonExportDocument(nodes, true, 500, 0.0, std::nullopt);
    EXPECT_EQ(JsonUtil::dump(field(doc, "count")), QStringLiteral("1"));
    EXPECT_EQ(JsonUtil::dump(field(doc, "max_latency_ms")), QStringLiteral("500"));
    EXPECT_EQ(JsonUtil::dump(field(doc, "min_download_kbps")), QStringLiteral("0.0"));
    EXPECT_TRUE(field(doc, "limit").is_null());
    EXPECT_EQ(field(doc, "quality_only"), Json(true));
    EXPECT_EQ(field(doc, "note"),
              Json("Export uses stored test results only and does not run network tests. limit is "
                   "the maximum number of nodes returned."));
    ASSERT_EQ(field(doc, "nodes").size(), 1u);
    EXPECT_EQ(field(field(doc, "nodes").at(0), "name"), Json("same"));

    // 整段序列化文本对拍：键按字节序、整数写成 1 而不是 1.0、浮点保留 .0。
    const QString expected = QStringLiteral(
        R"({"count":1,"limit":null,"max_latency_ms":500,"min_download_kbps":0.0,)"
        R"("nodes":[{"name":"same","password":"p","port":443,"server":"1.1.1.1","type":"trojan"}],)"
        R"("note":"Export uses stored test results only and does not run network tests. limit is )"
        R"(the maximum number of nodes returned.","quality_only":true})");
    EXPECT_EQ(JsonUtil::dump(doc), expected);
}
