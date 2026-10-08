#include <gtest/gtest.h>

#include "core/Encoding.h"
#include "models/Node.h"
#include "models/Source.h"

namespace {

// emoji 字面量在转录时容易静默丢码位，这里用码点构造，不靠我打出来的字符。
QString chinaFlag()
{
    const char32_t codePoints[] = {0x1F1E8, 0x1F1F3};
    return QString::fromUcs4(codePoints, 2);
}

}

TEST(JsonCompat, DumpSortsKeysAndStaysCompactLikeSerdeJson)
{
    // serde_json 默认 BTreeMap 行为：按字节序输出键、无空格。
    const Json value = Json::parse(R"({"b":1,"a":{"y":1,"x":2},"Port":3})");
    ASSERT_TRUE(value.is_discarded() == false);
    EXPECT_EQ(JsonUtil::dump(value).toStdString(),
              std::string(R"({"Port":3,"a":{"x":2,"y":1},"b":1})"));
}

TEST(JsonCompat, RoundTripsProductionNodeConfig)
{
    // 取自生产库 data/subscriptions/*.db 里一条真实 vmess config 列。
    const QString real = QString::fromUtf8(
        "{\"alterId\":0,\"cipher\":\"aes-128-gcm\",\"name\":\"HH_flag_412\","
        "\"network\":\"ws\",\"port\":80,\"server\":\"183.240.39.196\","
        "\"skip-cert-verify\":true,\"tls\":false,\"type\":\"vmess\",\"udp\":true,"
        "\"uuid\":\"0744d6be-279a-4116-bda2-3c3a1f541410\","
        "\"ws-opts\":{\"headers\":{\"Host\":\"hndongmei.xyz\"},"
        "\"path\":\"/a3654a1a-292d-402a-a313-d42a812598db\"}}");
    const Result<Json> parsed = JsonUtil::parse(real);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_EQ(JsonUtil::dump(parsed.value()), real);
}

TEST(JsonCompat, WritesNonAsciiAsRawUtf8NotEscaped)
{
    Models::Node node;
    node.config = Json::object();
    node.config["name"] = (chinaFlag() + QStringLiteral("_412")).toStdString();
    const QString dumped = JsonUtil::dump(node.config);
    EXPECT_FALSE(dumped.contains(QStringLiteral("\\u")));
    EXPECT_TRUE(dumped.toUtf8().contains(chinaFlag().toUtf8()));
}

TEST(SourceNullability, DistinguishesMissingFromEmptyString)
{
    Models::Source source = Models::Source::scratch(QStringLiteral("id"),
                                                    QStringLiteral("name"),
                                                    QStringLiteral("https://example.com/sub"));
    Json value = source.toJson();
    EXPECT_TRUE(value["remark"].is_null());
    source.remark = QString::fromUtf8("");
    value = source.toJson();
    EXPECT_EQ(value["remark"], "");
    // scratch() 的两个时间戳即时生成，JSON 侧应写成 Z 结尾。
    EXPECT_TRUE(value["created_at"].is_string());
    EXPECT_TRUE(value["created_at"].get<std::string>().back() == 'Z');
}

TEST(SourcePatch, AbsentKeysStayUnset)
{
    const Json patch = Json::parse(R"({"name": "新名字", "push_disabled": true})");
    const Models::UpdateSource update = Models::UpdateSource::fromJson(patch);
    ASSERT_TRUE(update.name.has_value());
    EXPECT_EQ(update.name->toStdString(), std::string("新名字"));
    ASSERT_TRUE(update.pushDisabled.has_value());
    EXPECT_TRUE(*update.pushDisabled);
    EXPECT_FALSE(update.urlTemplate.has_value());
    EXPECT_FALSE(update.enabled.has_value());
    EXPECT_FALSE(update.remark.has_value());
}

TEST(Encoding, EnginesMatchBase64CrateSemantics)
{
    // 单字节向量最容易核对：标准引擎补 ==，URL_SAFE_NO_PAD 不补。
    EXPECT_EQ(Encoding::encode("a", Encoding::Engine::Standard).toStdString(), std::string("YQ=="));
    EXPECT_EQ(Encoding::encode("a", Encoding::Engine::UrlSafeNoPad).toStdString(),
              std::string("YQ"));
    EXPECT_EQ(Encoding::encode("select node info", Encoding::Engine::Standard).toStdString(),
              std::string("c2VsZWN0IG5vZGUgaW5mbw=="));

    // 两个引擎的关系是定义性的：字母表替换 + 去掉填充，不依赖我手算码位。
    const QByteArray tricky = QByteArray::fromRawData("\xfb\xff\xfe\xbf\xdf\xff", 6);
    const QByteArray standard = Encoding::encode(tricky, Encoding::Engine::Standard);
    QByteArray expected = standard;
    expected.replace('+', '-');
    expected.replace('/', '_');
    expected.replace(QByteArray("="), QByteArray());
    EXPECT_EQ(Encoding::encode(tricky, Encoding::Engine::UrlSafeNoPad), expected);

    // 全字节往返：解包结果必须和原文一致。
    for (int value = 0; value < 256; ++value) {
        const QByteArray single = QByteArray(1, char(value));
        const Result<QByteArray> viaStandard =
            Encoding::decode(Encoding::encode(single, Encoding::Engine::Standard),
                             Encoding::Engine::Standard);
        ASSERT_TRUE(viaStandard.isOk()) << value;
        EXPECT_EQ(viaStandard.value(), single) << value;
        const Result<QByteArray> viaUrlSafe =
            Encoding::decode(Encoding::encode(single, Encoding::Engine::UrlSafeNoPad),
                             Encoding::Engine::UrlSafeNoPad);
        ASSERT_TRUE(viaUrlSafe.isOk()) << value;
        EXPECT_EQ(viaUrlSafe.value(), single) << value;
    }

    const Result<QByteArray> strict =
        Encoding::decode(QByteArray("c2VsZWN0IG5vZGUgaW5mbw=="), Encoding::Engine::Standard);
    ASSERT_TRUE(strict.isOk()) << strict.error().toStdString();
    EXPECT_EQ(strict.value(), QByteArray("select node info"));

    // 缺填充在 Rust 的 STANDARD 引擎里是错误，不能当成 base64 正文。
    EXPECT_TRUE(Encoding::decode(QByteArray("c2VsZWN0IG5vZGUgaW5mbw"), Encoding::Engine::Standard)
                    .isErr());
    // 残余比特非零同样报错（base64 crate 默认拒绝）。
    EXPECT_TRUE(Encoding::decode(QByteArray("c2VsZWN0IG5vZGUgaW5mb2=="), Encoding::Engine::Standard)
                    .isErr());
    // 宽松路径把缺填充的也接住，用于"猜是不是 base64"。
    EXPECT_TRUE(Encoding::decodeLenient(QByteArray("c2VsZWN0IG5vZGUgaW5mbw")).isOk());
    // URL_SAFE_NO_PAD 拒绝任何填充。
    EXPECT_TRUE(Encoding::decode(QByteArray("c2VsZWN0IG5vZGUgaW5mbw"),
                                 Encoding::Engine::UrlSafeNoPad)
                    .isOk());
    EXPECT_TRUE(Encoding::decode(QByteArray("c2VsZWN0IG5vZGUgaW5mbw=="),
                                 Encoding::Engine::UrlSafeNoPad)
                    .isErr());
}
