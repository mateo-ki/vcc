#include <gtest/gtest.h>

#include "gui/CountryNames.h"
#include "gui/NodeText.h"
#include "gui/UiText.h"

using namespace Gui;

namespace {

Models::NodeSummary node(std::optional<bool> ok, std::optional<qint64> ms,
                         const QString &kind = QString(), const QString &kernel = QString())
{
    Models::NodeSummary summary;
    summary.latencyOk = ok;
    summary.latencyMs = ms;
    if (!kind.isEmpty()) {
        summary.latencyTestKind = kind;
    }
    if (!kernel.isEmpty()) {
        summary.latencyKernel = kernel;
    }
    return summary;
}

const QString googleProxy = QString::fromLatin1(Models::kGoogleProxyTestKind);

}

TEST(NodeLatencyText, googleProxyShowsMsAndKernelName)
{
    // 快 = 绿，其余只是"更深的墨"：慢不慢看数字，不用红黄分级。
    const LatencyDisplay fast = latencyText(node(true, qint64(90), googleProxy,
                                                 QStringLiteral("mihomo")));
    EXPECT_EQ(fast.text, QStringLiteral("90 ms Mihomo"));
    EXPECT_EQ(fast.role.toStdString(), std::string("Success"));

    const LatencyDisplay slow = latencyText(node(true, qint64(1500), googleProxy,
                                                 QStringLiteral("sing_box")));
    EXPECT_EQ(slow.text, QStringLiteral("1500 ms Sing-box"));
    EXPECT_EQ(slow.role.toStdString(), std::string("Title"));

    EXPECT_EQ(latencyText(node(true, qint64(400), googleProxy)).role.toStdString(),
              std::string("Subtitle"));
    // 没记内核时兜底显示"代理"（Rust 的 None => "代理"）。
    EXPECT_EQ(latencyText(node(true, qint64(400), googleProxy)).text,
              QStringLiteral("400 ms ") + UiText::kernelNameFallback());
    // 认不出的内核名原样透出（Rust 的 Some(other) => other）。
    EXPECT_EQ(latencyText(node(true, qint64(400), googleProxy, QStringLiteral("surge"))).text,
              QStringLiteral("400 ms surge"));
}

TEST(NodeLatencyText, failuresAndUntestedSayDifferentThings)
{
    const LatencyDisplay proxyFailed = latencyText(node(false, qint64(30), googleProxy));
    EXPECT_EQ(proxyFailed.text, UiText::latencyProxyFailed());
    EXPECT_EQ(proxyFailed.role.toStdString(), std::string("Danger"));

    // TCP 预筛的通过不等于"能翻墙"，所以另一种口径。
    EXPECT_EQ(latencyText(node(true, qint64(30), QStringLiteral("tcp"))).text,
              QStringLiteral("30 ms TCP"));
    EXPECT_EQ(latencyText(node(false, std::nullopt, QStringLiteral("tcp"))).text,
              UiText::latencyTcpFailed());
    EXPECT_EQ(latencyText(node(std::nullopt, std::nullopt)).text, UiText::latencyUntested());
}

TEST(CountryNames, namesMatchTheRustTable)
{
    // 大小写都吃进去（Rust 先 to_ascii_uppercase）。
    EXPECT_EQ(countryFlagAndName(QStringLiteral("HK")).name, UiText::regionHK());
    EXPECT_EQ(countryFlagAndName(QStringLiteral("jp")).name, UiText::regionJP());
    EXPECT_EQ(countryFlagAndName(QStringLiteral("US")).name, UiText::regionUS());
    EXPECT_EQ(countryFlagAndName(QStringLiteral("sg")).name, UiText::regionSG());
    EXPECT_EQ(countryFlagAndName(QStringLiteral("TW")).name, UiText::regionTW());
    EXPECT_EQ(countryFlagAndName(QStringLiteral("KR")).name, UiText::regionKR());
    // 表里的长名字（三个汉字那种）也要能查到，不会掉进兜底。
    EXPECT_NE(countryFlagAndName(QStringLiteral("VN")).name, UiText::regionOther());
    EXPECT_GT(countryFlagAndName(QStringLiteral("AU")).name.length(), 2);
}

TEST(CountryNames, flagsAreRegionalIndicatorPairsAndUnknownFallsBack)
{
    // 国旗是两个区域指示符码点（U+1F1E6..U+1F1FF）。QString::size() 数的是 UTF-16 单元，
    // 所以两面字母旗 = 2 个码点 = 4 个单元；少一个码点就会变成 3（转录丢字符的典型形状）。
    const CountryDisplay hk = countryFlagAndName(QStringLiteral("hk"));
    EXPECT_EQ(hk.flag.size(), 4);
    EXPECT_NE(hk.flag, countryFlagAndName(QStringLiteral("jp")).flag);
    // "GB" | "UK" 在 Rust 里共用一面旗。
    EXPECT_EQ(countryFlagAndName(QStringLiteral("gb")).flag,
              countryFlagAndName(QStringLiteral("UK")).flag);
    // 认不出来的统一兜底，🌐 是单个码点 = 2 个 UTF-16 单元。
    const CountryDisplay unknown = countryFlagAndName(QStringLiteral("zz"));
    EXPECT_EQ(unknown.flag.size(), 2);
    EXPECT_EQ(unknown.name, UiText::regionOther());
    EXPECT_EQ(countryFlagAndName(QStringLiteral("")).name, UiText::regionOther());
}
