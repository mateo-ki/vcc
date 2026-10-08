#pragma once

#include <QtGlobal>

#include "models/Settings.h"
#include "speedtest/SpeedtestTypes.h"

/// src/scheduler/mod.rs 的纯规则部分（Rust 里的私有自由函数）。
/// 单独成文件是因为它们不碰数据库/网络，判定和副作用分开，这条规则才测得动。
namespace Scheduler {

/// 测通的节点低于这个数，就认为「故障转移没有替补可挑」了 —— 自动切换找候选就是从
/// 这批里捞（top_verified_nodes 的条件是 latency_ok = 1）。
inline constexpr qint64 kSparseVerifiedFloor = 20;
/// 两轮自动补测之间至少隔这么久，共用 last_auto_speedtest_at 这个时间戳。
inline constexpr qint64 kRefillCooldownMin = 30;
/// 自动补测的并发上限：这是后台活，不许把用户的带宽抢走（手动测速仍按设置里那个数）。
inline constexpr int kRefillConcurrency = 8;

/// 该不该补一轮全量测速。四个条件都成立才跑：开关开着、库里真的有节点可测、
/// 测通数低于地板、距上次自动测速（或从来没测过）够久。
/// nowEpochSecs 是 Unix 秒，与 Rust 的 `chrono::DateTime::timestamp()` 同单位。
bool refillNeeded(const Models::AppSettings &settings, qint64 total, qint64 verified,
                  qint64 nowEpochSecs);

/// 补测用的选项：并发压低、不测下载吞吐（那才是真吃带宽的一段），其余跟随设置。
Speedtest::SpeedTestOptions refillOpts(const Models::AppSettings &settings);

}
