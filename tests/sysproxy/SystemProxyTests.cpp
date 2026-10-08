#include <gtest/gtest.h>

#include "sysproxy/SystemProxy.h"
#include "sysproxy/TerminalProxy.h"

using namespace SysProxy;

// 搬 Rust src/system_proxy/mod.rs 的测试。
TEST(SystemProxy, ParsesRegistryDwordAndStringValues)
{
    const QString enabled = QStringLiteral(
        "\nHKEY_CURRENT_USER\\Software\\Microsoft\n    ProxyEnable    REG_DWORD    0x1");
    const QString server = QStringLiteral("    ProxyServer    REG_SZ    127.0.0.1:7890");

    EXPECT_EQ(parseRegValue(enabled, QStringLiteral("ProxyEnable")).toStdString(),
              std::string("0x1"));
    EXPECT_EQ(parseRegValue(server, QStringLiteral("ProxyServer")).toStdString(),
              std::string("127.0.0.1:7890"));
    ASSERT_TRUE(parseDword(QStringLiteral("0x1")).has_value());
    EXPECT_TRUE(*parseDword(QStringLiteral("0x1")));
    EXPECT_FALSE(*parseDword(QStringLiteral("0x0")));
    ASSERT_TRUE(parseDword(QStringLiteral("1")).has_value());
    EXPECT_TRUE(*parseDword(QStringLiteral("1")));
}

TEST(SystemProxy, IgnoresUnrelatedRegistryLines)
{
    const QString output = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\n    Other    REG_SZ    value");
    EXPECT_TRUE(parseRegValue(output, QStringLiteral("ProxyServer")).isNull());
}

TEST(SystemProxy, NormalizesProxyServerRegistryForms)
{
    // v2rayN 写的简单形式
    EXPECT_EQ(normalizeProxyServer(QStringLiteral("127.0.0.1:10808")).toStdString(),
              std::string("http://127.0.0.1:10808"));
    // 按协议分组的形式：优先取 https，其次 http
    EXPECT_EQ(normalizeProxyServer(
                  QStringLiteral("ftp=1.1.1.1:21;http=2.2.2.2:80;https=3.3.3.3:443"))
                  .toStdString(),
              std::string("http://3.3.3.3:443"));
    EXPECT_EQ(normalizeProxyServer(QStringLiteral("http=2.2.2.2:80;ftp=1.1.1.1:21")).toStdString(),
              std::string("http://2.2.2.2:80"));
    EXPECT_TRUE(normalizeProxyServer(QStringLiteral("  ")).isNull());
    EXPECT_TRUE(normalizeProxyServer(QStringLiteral("socks=5.5.5.5:1080")).isNull());
}

TEST(SystemProxy, UndecodableRegOutputNeverReachesTheUiAsMojibake)
{
    // 「错误：」的 GBK 字节，不是合法 UTF-8：以前会被 lossy 解码变成一串替换字符
    // 直接打到界面上，现在必须被过滤掉。
    const QByteArray gbk = QByteArray::fromRawData("\xb4\xed\xce\xf3\xa3\xba", 6);
    const QString text = commandOutputText(gbk);
    EXPECT_EQ(text.indexOf(QChar(0xFFFD)), -1);
}

TEST(SystemProxy, DeletingAnAbsentRegistryValueIsNotAnError)
{
    // 恢复系统代理时曾经因为"值不存在"整个启动流程被判失败，这里守住。
    // 这个用例只查一个必然不存在的值名，不会动到真实配置。
    const Status status = deleteRegValue(QStringLiteral("ProxyOverride-Definitely-Absent-9f3c1a"));
    EXPECT_TRUE(status.isOk()) << status.error().toStdString();
}

// 搬 Rust src/system_proxy/terminal.rs 的测试。
TEST(TerminalProxy, OnlyOurOwnValuesAreEverTouched)
{
    EXPECT_TRUE(TerminalProxy::isOurs(QStringLiteral("http://127.0.0.1:9980")));
    EXPECT_FALSE(TerminalProxy::isOurs(QStringLiteral("http://proxy.corp.example:3128")));
    EXPECT_FALSE(TerminalProxy::isOurs(QStringLiteral("socks5://127.0.0.1:1080")));
    EXPECT_FALSE(TerminalProxy::isOurs(QString::fromUtf8("")));
}

TEST(TerminalProxy, ReportSummaryReadsInChinese)
{
    TerminalProxy::Report report;
    report.written = {QStringLiteral("HTTP_PROXY")};
    report.untouched = {QStringLiteral("NO_PROXY")};
    const QString text = report.summary();
    // 断言用码点写，避免测试本身依赖中文转录。
    static const char32_t kWritten[] = {0x5DF2, 0x5199, 0x5165};
    static const char32_t kKept[] = {0x4FDD, 0x7559, 0x4F60, 0x539F, 0x6709, 0x7684};
    const QString written = QString::fromUcs4(kWritten, 3);
    const QString kept = QString::fromUcs4(kKept, 6);
    EXPECT_NE(text.indexOf(written + QStringLiteral(" HTTP_PROXY")), -1) << text.toStdString();
    EXPECT_NE(text.indexOf(kept + QStringLiteral(" NO_PROXY")), -1) << text.toStdString();
}

TEST(TerminalProxy, NoProxyListCoversLocalAndPrivateRanges)
{
    const QString value = TerminalProxy::noProxyValue();
    EXPECT_TRUE(value.startsWith(QStringLiteral("localhost;127.0.0.1;::1;<local>")));
    EXPECT_TRUE(value.contains(QStringLiteral("192.168.*")));
    EXPECT_TRUE(value.contains(QStringLiteral("172.31.*")));
    EXPECT_EQ(value.length(), 190);
}
