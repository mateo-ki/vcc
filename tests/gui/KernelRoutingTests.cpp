#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include "gui/KernelRouting.h"

using namespace Gui;

namespace {

// 一份按 mihomo /proxies 真实形状写的响应：PROXY 交给「自动选择」，
// 「自动选择」选中 香港01；FINAL 走 DIRECT；另有一个 socks 节点带 history。
const char *kBody = R"JSON({
  "proxies": {
    "PROXY": {"type":"select","now":"自动选择","all":["自动选择","香港01"]},
    "自动选择": {"type":"URL-Test","now":"香港01","all":["香港01","东京02"],
      "history":[{"delay":210},{"delay":188}]},
    "FINAL": {"type":"select","now":"DIRECT","all":["DIRECT","REJECT"]},
    "香港01": {"type":"ss","alive":true,"history":[{"delay":120},{"delay":97}]},
    "东京02": {"type":"socks5","alive":false},
    "无历史": {"type":"vless"}
  }
})JSON";

QVector<KernelGroup> groupsOf(const QByteArray &body) { return parseKernelProxies(body).groups; }

} // namespace

TEST(KernelRouting, keepsOnlySelectableGroupsAndSortsThem)
{
    const KernelProxies parsed = parseKernelProxies(QByteArray(kBody));
    QStringList names;
    for (const KernelGroup &group : parsed.groups) {
        names.push_back(group.name);
    }
    // 单个节点（ss/socks5/vless）不许进列表；组按名字排序（Rust mod.rs:2716）。
    EXPECT_EQ(names.join(QLatin1Char(',')).toStdString(),
              std::string("FINAL,PROXY,自动选择"));
    EXPECT_EQ(parsed.groups.size(), 3);
    // type 要小写归一（上游给的是 "URL-Test"）。
    bool sawUrlTest = false;
    for (const KernelGroup &group : parsed.groups) {
        if (group.name == QString::fromUtf8("自动选择")) {
            sawUrlTest = group.kind == QLatin1String("url-test");
            EXPECT_EQ(group.members.size(), 2);
        }
    }
    EXPECT_TRUE(sawUrlTest) << "URL-Test 没归一化成 url-test";
}

TEST(KernelRouting, latenciesTakeTheNewestHistoryEntryOnly)
{
    const KernelProxies parsed = parseKernelProxies(QByteArray(kBody));
    ASSERT_EQ(parsed.latencies.size(), 2) << "只有带 history 的条目才产生延迟读数（这里是 自动选择 与 香港01）";
    int hk = -1;
    int auto_ = -1;
    for (const KernelLatency &latency : parsed.latencies) {
        if (latency.name == QString::fromUtf8("香港01")) {
            hk = latency.delayMs;
            EXPECT_TRUE(latency.alive);
        }
        if (latency.name == QString::fromUtf8("自动选择")) {
            auto_ = latency.delayMs;
        }
    }
    // history 是从旧到新，取最后一条：120 而不是 97 之外的旧值。
    EXPECT_EQ(hk, 97);
    EXPECT_EQ(auto_, 188);
    // alive:false 的节点没有 history，所以不会出现在上面；这里单独验 alive 的读法。
    const KernelProxies withDead = parseKernelProxies(QByteArray(
        R"JSON({"proxies":{"x":{"type":"ss","alive":false,"history":[{"delay":0}]}}})JSON"));
    ASSERT_EQ(withDead.latencies.size(), 1);
    EXPECT_FALSE(withDead.latencies.first().alive);
}

TEST(KernelRouting, egressFollowsIntoTheAutoGroup)
{
    const QVector<KernelGroup> groups = groupsOf(QByteArray(kBody));
    EXPECT_EQ(proxyGroupNow(groups).toStdString(), std::string("自动选择"));
    // PROXY.now 指向「自动选择」→ 真出口是那个组选中的成员。
    EXPECT_EQ(kernelEgressName(groups).toStdString(), std::string("香港01"));
    EXPECT_EQ(finalGroupNow(groups).toStdString(), std::string("DIRECT"));
}

TEST(KernelRouting, egressIsTheProxyGroupMemberWhenNotHandedToAuto)
{
    const QVector<KernelGroup> groups = groupsOf(QByteArray(
        R"JSON({"proxies":{"PROXY":{"type":"select","now":"东京03","all":["东京03"]}}})JSON"));
    EXPECT_EQ(kernelEgressName(groups).toStdString(), std::string("东京03"));
}

TEST(KernelRouting, missingNowMeansNoEgress)
{
    // PROXY 没报 now（内核刚起来）→ 空串，界面要落到「等待内核上报...」。
    const QVector<KernelGroup> noNow = groupsOf(QByteArray(
        R"JSON({"proxies":{"PROXY":{"type":"select","all":["自动选择"]}}})JSON"));
    EXPECT_TRUE(proxyGroupNow(noNow).isEmpty());
    EXPECT_TRUE(kernelEgressName(noNow).isEmpty());

    // PROXY.now 指向「自动选择」但那个组本身不存在 → 也只能是空。
    const QVector<KernelGroup> dangling = groupsOf(QByteArray(
        R"JSON({"proxies":{"PROXY":{"type":"select","now":"自动选择"}}})JSON"));
    EXPECT_EQ(proxyGroupNow(dangling).toStdString(), std::string("自动选择"));
    EXPECT_TRUE(kernelEgressName(dangling).isEmpty());
}

TEST(KernelRouting, garbageBodiesParseToEmpty)
{
    // 这一路只喂状态卡，读不出来就当没数据，绝不能抛、也不能崩。
    EXPECT_TRUE(parseKernelProxies(QByteArray("not json at all")).groups.isEmpty());
    EXPECT_TRUE(parseKernelProxies(QByteArray("")).groups.isEmpty());
    EXPECT_TRUE(parseKernelProxies(QByteArray("{\"proxies\":[]}")).groups.isEmpty());
    EXPECT_TRUE(parseKernelProxies(QByteArray("{\"proxies\":{\"a\":5}}")).groups.isEmpty());
}
