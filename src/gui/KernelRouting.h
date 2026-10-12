#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// 内核控制器 GET /proxies 的解析与"当前出口"判定。
// 对应 Rust：轮询在 gui/mod.rs:2667-2719（poll_kernel_groups），出口判定在
// gui/views/proxy.rs:746-760（kernel_egress_name），渲染在 proxy.rs:590-623。
namespace Gui {

/// 一个可选路的策略组（select / url-test）。now 为空 = Rust 的 None。
struct KernelGroup {
    QString name;
    QString kind;
    QString now;
    QStringList members;
};

/// 内核自己测出来的一拍延迟（history 最后一条 = 最新）。
struct KernelLatency {
    QString name;
    int delayMs = 0;
    bool alive = true;
};

struct KernelProxies {
    QVector<KernelGroup> groups;
    QVector<KernelLatency> latencies;
};

/// Rust proxy/mod.rs:186 的 URL_TEST_GROUP / 内置的 PROXY、FINAL 组名。
inline constexpr const char *kUrlTestGroup = "自动选择";
inline constexpr const char *kProxyGroup = "PROXY";
inline constexpr const char *kFinalGroup = "FINAL";

/// 解析 /proxies 响应体。任何一步读不出来就返回空集合（Rust 那边同样是直接 return，
/// 不报错 —— 这一路只是给状态卡显示用的，不该把拉取流程带崩）。
KernelProxies parseKernelProxies(const QByteArray &body);

/// PROXY 组自己报的 now（可能就是「自动选择」）。没有 PROXY 或没报 now → 空。
QString proxyGroupNow(const QVector<KernelGroup> &groups);

/// FINAL 组（漏网之鱼走这里）当前指向谁。
QString finalGroupNow(const QVector<KernelGroup> &groups);

/// url-test 开着时的真实出口：PROXY.now 若指向「自动选择」，再取该组选中的成员。
QString kernelEgressName(const QVector<KernelGroup> &groups);

} // namespace Gui
