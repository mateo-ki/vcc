#include <gtest/gtest.h>

#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>

#include <iostream>


#include "core/Encoding.h"
#include "core/JsonUtil.h"
#include "models/Node.h"
#include "parser/Common.h"
#include "parser/Fingerprint.h"
#include "parser/Clash.h"
#include "parser/Subscription.h"
#include "parser/Uri.h"

// 对应 vpn-parser/src/parser/mod.rs 的 #[cfg(test)] mod tests。
// 这四个 Rust 文件里只有 mod.rs 带测试模块（common.rs / uri.rs / clash.rs 没有 #[cfg(test)]），
// 所以这里逐条移植 mod.rs 的 9 个 #[test]，名字保持 Rust 原样。
namespace {

using Parser::Common::ucs4;

/// 测试里的 CJK / emoji 全部按码点构造，避免字面量在转录时丢码位。
QString hk()
{
    return ucs4({0x9999, 0x6E2F}); // 香港
}

QString us()
{
    return ucs4({0x7F8E, 0x56FD}); // 美国
}

QString jp()
{
    return ucs4({0x65E5, 0x672C}); // 日本
}

QString dedicatedLine()
{
    return ucs4({0x4E13, 0x7EBF}); // 专线
}

TEST(Parser, parse_ss_uri)
{
    const QString uri =
        QStringLiteral("ss://YWVzLTI1Ni1nY206cGFzc3dvcmQ@1.2.3.4:8388#test");
    const auto node = Parser::Uri::parseUri(uri, QString());
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->server.toStdString(), std::string("1.2.3.4"));
    EXPECT_EQ(node->port, 8388);
    EXPECT_EQ(node->protocol.toStdString(), std::string("shadowsocks"));
}

TEST(Parser, clean_node_name_removes_ads_and_normalizes)
{
    // "[TG@vpn_share] 🇭🇰 香港 01 - 剩余流量: 999G"
    const QString input1 = ucs4({0x005B, 0x0054, 0x0047, 0x0040, 0x0076, 0x0070, 0x006E, 0x005F,
                                 0x0073, 0x0068, 0x0061, 0x0072, 0x0065, 0x005D, 0x0020, 0x1F1ED,
                                 0x1F1F0, 0x0020, 0x9999, 0x6E2F, 0x0020, 0x0030, 0x0031, 0x0020,
                                 0x002D, 0x0020, 0x5269, 0x4F59, 0x6D41, 0x91CF, 0x003A, 0x0020,
                                 0x0039, 0x0039, 0x0039, 0x0047});
    // "香港 01"
    const QString expect1 = ucs4({0x9999, 0x6E2F, 0x0020, 0x0030, 0x0031});
    EXPECT_EQ(Parser::cleanNodeName(input1, QStringLiteral("HK"), QStringLiteral("1.1.1.1"), 443),
              expect1);

    // "【推荐关注公众号:xxx】US-02"
    const QString input2 = ucs4({0x3010, 0x63A8, 0x8350, 0x5173, 0x6CE8, 0x516C, 0x4F17, 0x53F7,
                                 0x003A, 0x0078, 0x0078, 0x0078, 0x3011, 0x0055, 0x0053, 0x002D,
                                 0x0030, 0x0032});
    // "美国 02"
    const QString expect2 = ucs4({0x7F8E, 0x56FD, 0x0020, 0x0030, 0x0032});
    EXPECT_EQ(Parser::cleanNodeName(input2, QString(), QStringLiteral("1.1.1.1"), 443), expect2);

    // "⭐官网: https://freevpn.cc - 🇯🇵 日本专线"
    const QString input3 = ucs4({0x2B50, 0x5B98, 0x7F51, 0x003A, 0x0020, 0x0068, 0x0074, 0x0074,
                                 0x0070, 0x0073, 0x003A, 0x002F, 0x002F, 0x0066, 0x0072, 0x0065,
                                 0x0065, 0x0076, 0x0070, 0x006E, 0x002E, 0x0063, 0x0063, 0x0020,
                                 0x002D, 0x0020, 0x1F1EF, 0x1F1F5, 0x0020, 0x65E5, 0x672C, 0x4E13,
                                 0x7EBF});
    // "日本 专线"
    const QString expect3 = jp() + QStringLiteral(" ") + dedicatedLine();
    EXPECT_EQ(Parser::cleanNodeName(input3, QString(), QStringLiteral("1.1.1.1"), 443), expect3);
}

TEST(Parser, expand_date_template)
{
    const QString url =
        Parser::expandUrlTemplate(QStringLiteral("https://example.com/{Ymd}-clash.yaml"));
    EXPECT_TRUE(url.contains(QStringLiteral("-clash.yaml")));
    EXPECT_FALSE(url.contains(QStringLiteral("{Ymd}")));
}

TEST(Parser, fingerprint_ignores_name)
{
    Json configA = Json::object();
    configA["type"] = std::string("vless");
    configA["uuid"] = std::string("11111111-1111-1111-1111-111111111111");
    configA["server"] = std::string("1.2.3.4");
    configA["port"] = 443;
    Json configB = configA;

    const Models::Node a = Parser::makeNode(QStringLiteral("vless"),
                                            QStringLiteral("name-a"), QStringLiteral("1.2.3.4"), 443,
                                            QStringLiteral("vless://u@1.2.3.4:443"), configA,
                                            QString());
    const Models::Node b = Parser::makeNode(
        QStringLiteral("vless"), QStringLiteral("name-b-totally-different"),
        QStringLiteral("1.2.3.4"), 443, QStringLiteral("vless://u@1.2.3.4:443#other"), configB,
        QString());
    EXPECT_EQ(Parser::nodeFingerprint(a), Parser::nodeFingerprint(b));
    const QVector<Models::Node> out = Parser::dedupNodes({a, b});
    EXPECT_EQ(out.size(), 1);
}

TEST(Parser, fingerprint_differs_by_uuid)
{
    Json configA = Json::object();
    configA["uuid"] = std::string("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    Json configB = Json::object();
    configB["uuid"] = std::string("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");

    const Models::Node a = Parser::makeNode(QStringLiteral("vless"), QStringLiteral("n"),
                                            QStringLiteral("1.2.3.4"), 443, QStringLiteral("raw"),
                                            configA, QString());
    const Models::Node b = Parser::makeNode(QStringLiteral("vless"), QStringLiteral("n"),
                                            QStringLiteral("1.2.3.4"), 443, QStringLiteral("raw"),
                                            configB, QString());
    EXPECT_NE(Parser::nodeFingerprint(a), Parser::nodeFingerprint(b));
}

TEST(Parser, parse_ss_with_query_and_amp)
{
    const QString uri = QStringLiteral(
        "ss://YWVzLTI1Ni1nY206cGFzcw@1.2.3.4:8388?plugin=obfs&amp;foo=1#name");
    const auto node = Parser::Uri::parseUri(uri, QString());
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->server.toStdString(), std::string("1.2.3.4"));
    EXPECT_EQ(node->port, 8388);
}

TEST(Parser, parse_mislabeled_ss_as_vless)
{
    const QString uri = QStringLiteral(
        "ss://03fcc618-b93d-6796-6aed-8a38c975d581@186.190.211.188:443?security=tls&amp;"
        "encryption=none&amp;type=tcp&amp;sni=example.com#test");
    const auto node = Parser::Uri::parseUri(uri, QString());
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->protocol.toStdString(), std::string("vless"));
    EXPECT_EQ(node->server.toStdString(), std::string("186.190.211.188"));
    EXPECT_EQ(node->port, 443);
}

TEST(Parser, parse_vless_with_emoji_fragment)
{
    const QString uri = QStringLiteral(
        "vless://a18f7c2d-9e45-4b8a-af3c-1d5e7f9c8b2a@83.168.71.216:443?security=reality"
        "&encryption=none&type=tcp&sni=ads.x5.ru#Join-Telegram");
    const auto node = Parser::Uri::parseUri(uri, QString());
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->protocol.toStdString(), std::string("vless"));
    EXPECT_EQ(node->server.toStdString(), std::string("83.168.71.216"));
}

/// 对应 Rust 的 `std::fs::read_to_string("data/sample-solispirit.txt")`：
/// 路径按 Rust 一样相对当前工作目录，取不到就跳过（Rust 里也是 eprintln 后直接 return）。
/// 想跑到真实断言，需要在 vpn-parser 仓库根目录下执行本 exe。
TEST(Parser, parse_solispirit_sample)
{
    QFile file(QStringLiteral("data/sample-solispirit.txt"));
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "sample missing, skip" << std::endl;
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList uris = Parser::extractUris(content);
    qsizetype parsed = 0;
    qsizetype failSs = 0;
    qsizetype failVmess = 0;
    qsizetype failVless = 0;
    qsizetype failTrojan = 0;
    qsizetype failOther = 0;
    QStringList failSamples;
    for (const QString& uri : uris) {
        if (Parser::Uri::parseUri(uri, QString()).has_value()) {
            ++parsed;
            continue;
        }
        const QString low = Parser::Common::toAsciiLower(uri);
        if (low.startsWith(QStringLiteral("ss://"))) {
            ++failSs;
        } else if (low.startsWith(QStringLiteral("vmess://"))) {
            ++failVmess;
        } else if (low.startsWith(QStringLiteral("vless://"))) {
            ++failVless;
        } else if (low.startsWith(QStringLiteral("trojan://"))) {
            ++failTrojan;
        } else {
            ++failOther;
        }
        if (failSamples.size() < 8) {
            failSamples.append(uri.left(140));
        }
    }
    const QVector<Models::Node> nodes = Parser::parseSubscription(content, QStringLiteral("test"));
    std::cerr << "uris=" << int(uris.size()) << " parsed_raw=" << int(parsed)
              << " deduped=" << int(nodes.size()) << " fail_ss=" << int(failSs)
              << " fail_vmess=" << int(failVmess) << " fail_vless=" << int(failVless)
              << " fail_trojan=" << int(failTrojan) << " fail_other=" << int(failOther)
              << std::endl;
    for (const QString& sample : failSamples) {
        std::cerr << "FAIL " << sample.toStdString() << std::endl;
    }
    EXPECT_GT(parsed, 7000) << "raw parse too low: " << int(parsed);
    EXPECT_GT(nodes.size(), 3500) << "deduped too low: " << int(nodes.size());
}


} // namespace
