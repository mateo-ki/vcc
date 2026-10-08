#include <gtest/gtest.h>

#include "core/HostUtil.h"

// 搬 Rust src/hostutil.rs 的测试。
TEST(HostUtil, RejectsLoopbackAndPrivate)
{
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("127.0.0.1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("localhost")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("192.168.1.1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("10.0.0.2")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("172.16.0.5")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("::1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("0.0.0.0")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("100.64.0.1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("169.254.1.1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("fe80::1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("fc00::1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("224.0.0.1")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("host.lan")));
    EXPECT_FALSE(HostUtil::isPublicProxyHost(QStringLiteral("bare")));
}

TEST(HostUtil, AcceptsPublic)
{
    EXPECT_TRUE(HostUtil::isPublicProxyHost(QStringLiteral("1.1.1.1")));
    EXPECT_TRUE(HostUtil::isPublicProxyHost(QStringLiteral("cf.090227.xyz")));
    EXPECT_TRUE(HostUtil::isPublicProxyHost(QStringLiteral("example-node.com")));
    EXPECT_TRUE(HostUtil::isPublicProxyHost(QStringLiteral("[2001:db8::1]")));
}

TEST(HostUtil, RejectsDecoyWebsitesForQualityExport)
{
    EXPECT_TRUE(HostUtil::isDecoyWebsiteHost(QStringLiteral("themeforest.net")));
    EXPECT_TRUE(HostUtil::isDecoyWebsiteHost(QStringLiteral("www.fbi.gov")));
    EXPECT_TRUE(HostUtil::isDecoyWebsiteHost(QStringLiteral("blog.cloudflare.com")));
    EXPECT_TRUE(HostUtil::isDecoyWebsiteHost(QStringLiteral("trt.fastlynew.hosting-ip.com")));
    EXPECT_FALSE(HostUtil::isQualityExportHost(QStringLiteral("themeforest.net")));
    EXPECT_TRUE(HostUtil::isQualityExportHost(QStringLiteral("cf.090227.xyz")));
    // 1.1.1.1 是公网 IP，但它是 DNS 伪装站，质量导出不该出现。
    EXPECT_TRUE(HostUtil::isPublicProxyHost(QStringLiteral("1.1.1.1")));
    EXPECT_TRUE(HostUtil::isDecoyWebsiteHost(QStringLiteral("1.1.1.1")));
    EXPECT_FALSE(HostUtil::isQualityExportHost(QStringLiteral("1.1.1.1")));
    // 子域同样命中伪装表。
    EXPECT_TRUE(HostUtil::isDecoyWebsiteHost(QStringLiteral("a.b.youtube.com")));
    EXPECT_FALSE(HostUtil::isDecoyWebsiteHost(QStringLiteral("notyoutube.com")));
}
