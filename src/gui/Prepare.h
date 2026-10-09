#pragma once

#include <QtGlobal>

#include <optional>

namespace Gui {
namespace Prepare {

/// 「预备」这个软件状态的判定，和两个自动动作的门槛。全部纯函数：
/// 副作用（起下载、起测速）留在外壳里，规则本身才测得动。

/// 内核都还没装、代理也不在跑/不在启动 —— 这时软件处于「预备」状态：
/// 它还没准备好能翻墙，但也没坏，正在等着把内核装上。
bool isPrepared(bool hasKernel, bool proxyActive);

/// 一次运行最多自动试几次、两次之间至少隔多久。
///
/// 只试一次是不够的：新装的机器没有代理，查版本要直打 `api.github.com`，
/// 那条路在我们这儿是**间歇被掐**的（实测过一半次数整 5.0 秒握手失败），
/// 一次抖动就把"自动准备"变成永远不会再动的死状态。也不许多试 —— 无脑重试
/// 就是每帧一个下载作业。
constexpr int kMaxAutoAttempts = 3;
constexpr qint64 kMinAttemptSpacingMs = 90000;

/// 该不该自动去下内核。`kernelBusy`（不是界面整体 busy —— 下载内核和拉取/测速
/// 不抢同一份资源，被别人的作业卡住就等于不动）+ 次数 + 间隔三道门。
/// sinceLastMs 是距上一次尝试的毫秒数，null = 这轮还没试过。
bool shouldDownloadKernel(bool enabled, bool hasKernel, bool proxyActive, bool kernelBusy,
                          int attempts, std::optional<qint64> sinceLastMs);

/// 新增订阅源之后要不要顺手测它。只认"创建成功 + 拉取成功 + 这个源真解析出节点了"，
/// 三者缺一条就什么都不做 —— 对一个空源或失败源发起测速，只会白占一次并发。
bool shouldTestAddedSource(bool enabled, bool pendingForThis, bool fetchSucceeded,
                           qint64 nodeCount);

}
}
