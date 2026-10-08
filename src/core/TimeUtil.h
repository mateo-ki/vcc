#pragma once

#include <QtGlobal>

#include "core/Result.h"

// 时间戳必须与 Rust 侧 chrono 逐字节一致：库里存 `Utc::now().to_rfc3339()`
// （`+00:00` 结尾），JSON/API 走 serde（`Z` 结尾），小数位按尾零收缩成 0/3/6/9 位。
// 两个实现会读同一个库文件，字符串比较（24 小时窗口、按日分组）依赖这个格式。
namespace TimeUtil {

/// Unix 起点以来的纳秒。Windows 上取 100ns 精度时钟，和 chrono::Utc::now() 同源。
qint64 nowUtcNanos();

/// useZ=false → `...+00:00`（落库）；useZ=true → `...Z`（JSON）。
QString formatRfc3339(qint64 epochNanos, bool useZ);
QString nowRfc3339();
QString nowRfc3339Json();

/// 解析 RFC3339（容忍 0~9 位小数、Z 或 ±HH:MM 偏移）。
Result<qint64> parseRfc3339(const QString &text);

/// UTC 日历日 `YYYY-MM-DD`。
QString utcDay(qint64 epochNanos);
/// 本地日历日 `YYYY-MM-DD`：出口切换记录按本地日分组，跨零点的那几天不能算错。
QString localDay(qint64 epochNanos);
/// 本地时区当天 00:00 的 UTC 纳秒，用于按日窗口查询。
qint64 localDayStartNanos(const QString &day);

qint64 secsToNanos(qint64 seconds);
QString formatLocalReadable(qint64 epochNanos);
QString formatUtcMillis(qint64 epochNanos);

/// 对应 Rust 的 Option<DateTime<Utc>>：同一时刻在库里写 `+00:00`、在 JSON 里写 `Z`，
/// 所以结构体里存纳秒而不是字符串，两种文本各自在边界处生成。
struct OptionalUtc {
    qint64 nanos = 0;
    bool hasValue = false;

    static OptionalUtc none() { return OptionalUtc(); }
    static OptionalUtc of(qint64 epochNanos)
    {
        OptionalUtc value;
        value.nanos = epochNanos;
        value.hasValue = true;
        return value;
    }
    static OptionalUtc now() { return of(nowUtcNanos()); }

    bool isNull() const { return !hasValue; }

    /// 落库文本；无值时返回 null QString，调用方据此 bind_null。
    QString toDbText() const { return hasValue ? formatRfc3339(nanos, false) : QString(); }
    /// JSON 文本；无值时返回 null QString，调用方据此写 Json null。
    QString toJsonText() const { return hasValue ? formatRfc3339(nanos, true) : QString(); }

    /// 读库/读 JSON：null、空串和无法解析都当成无值（与 Rust 侧 unwrap_or 行为一致）。
    static OptionalUtc fromText(const QString &text)
    {
        if (text.isNull() || text.trimmed().isEmpty()) {
            return none();
        }
        const Result<qint64> parsed = parseRfc3339(text);
        return parsed.isOk() ? of(parsed.value()) : none();
    }
};

}
