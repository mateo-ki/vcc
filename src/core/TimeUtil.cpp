#include "core/TimeUtil.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <chrono>
#include <cmath>

namespace {

constexpr qint64 kNanosPerSec = 1000000000LL;
constexpr qint64 kJulianDayUnixEpoch = 2440588; // 1970-01-01

void breakDownSeconds(qint64 seconds, qint64 &days, qint64 &hour, qint64 &minute, qint64 &second)
{
    qint64 normalized = seconds;
    days = normalized / 86400;
    qint64 rem = normalized % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    hour = rem / 3600;
    rem %= 3600;
    minute = rem / 60;
    second = rem % 60;
}

qint64 clockNanos()
{
#ifdef Q_OS_WIN
    FILETIME fileTime{};
    GetSystemTimePreciseAsFileTime(&fileTime);
    const qint64 raw = (qint64(fileTime.dwHighDateTime) << 32) | qint64(fileTime.dwLowDateTime);
    // FILETIME 起点 1601-01-01，单位 100ns。
    return (raw - 116444736000000000LL) * 100LL;
#else
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
#endif
}

}

namespace TimeUtil {

qint64 nowUtcNanos()
{
    return clockNanos();
}

QString formatRfc3339(qint64 epochNanos, bool useZ)
{
    qint64 seconds = epochNanos >= 0 ? epochNanos / kNanosPerSec
                                     : -((-epochNanos + kNanosPerSec - 1) / kNanosPerSec);
    qint64 nano = epochNanos - seconds * kNanosPerSec;
    if (nano < 0) {
        seconds -= 1;
        nano += kNanosPerSec;
    }
    // chrono 在写库前处理了 nano >= 1e9 的进位，这里等价。
    if (nano >= kNanosPerSec) {
        seconds += 1;
        nano -= kNanosPerSec;
    }

    qint64 days = 0;
    qint64 hour = 0;
    qint64 minute = 0;
    qint64 second = 0;
    breakDownSeconds(seconds, days, hour, minute, second);

    const QDate date = QDate::fromJulianDay(kJulianDayUnixEpoch + int(days));
    QString out = QStringLiteral("%1-%2-%3T%4:%5:%6")
                      .arg(date.year(), 4, 10, QLatin1Char('0'))
                      .arg(date.month(), 2, 10, QLatin1Char('0'))
                      .arg(date.day(), 2, 10, QLatin1Char('0'))
                      .arg(hour, 2, 10, QLatin1Char('0'))
                      .arg(minute, 2, 10, QLatin1Char('0'))
                      .arg(second, 2, 10, QLatin1Char('0'));

    if (nano != 0) {
        if (nano % 1000000LL == 0) {
            out += QStringLiteral(".%1").arg(nano / 1000000LL, 3, 10, QLatin1Char('0'));
        } else if (nano % 1000LL == 0) {
            out += QStringLiteral(".%1").arg(nano / 1000LL, 6, 10, QLatin1Char('0'));
        } else {
            out += QStringLiteral(".%1").arg(nano, 9, 10, QLatin1Char('0'));
        }
    }

    out += useZ ? QStringLiteral("Z") : QStringLiteral("+00:00");
    return out;
}

QString nowRfc3339()
{
    return formatRfc3339(clockNanos(), false);
}

QString nowRfc3339Json()
{
    return formatRfc3339(clockNanos(), true);
}

Result<qint64> parseRfc3339(const QString &text)
{
    // 形如 2026-08-10T11:42:51.624230300+00:00 / ...Z
    const QString trimmed = text.trimmed();
    static const QRegularExpression pattern(QStringLiteral(
        "^(\\d{4})-(\\d{2})-(\\d{2})[Tt ](\\d{2}):(\\d{2}):(\\d{2})(?:\\.(\\d{1,9}))?"
        "(Z|z|[+-]\\d{2}:?\\d{2})$"));
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch()) {
        return Result<qint64>::withError(QStringLiteral("不是合法的 RFC3339 时间: %1").arg(text));
    }

    const QDate date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
    const QTime time(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt());
    if (!date.isValid() || !time.isValid()) {
        return Result<qint64>::withError(QStringLiteral("时间字段越界: %1").arg(text));
    }

    qint64 nano = 0;
    const QString fraction = match.captured(7);
    if (!fraction.isEmpty()) {
        QString padded = fraction;
        while (padded.size() < 9) {
            padded += QLatin1Char('0');
        }
        nano = padded.toLongLong();
    }

    qint64 offsetSeconds = 0;
    const QString zone = match.captured(8);
    if (zone.size() > 1) {
        const QString digits = zone.mid(1).remove(QLatin1Char(':'));
        offsetSeconds = qint64(digits.left(2).toInt()) * 3600 + qint64(digits.mid(2, 2).toInt()) * 60;
        if (zone.at(0) == QLatin1Char('-')) {
            offsetSeconds = -offsetSeconds;
        }
    }

    const QDateTime utc(date, time, QTimeZone::UTC);
    if (!utc.isValid()) {
        return Result<qint64>::withError(QStringLiteral("无法构造 UTC 时间: %1").arg(text));
    }
    const qint64 epochSeconds = utc.toSecsSinceEpoch() - offsetSeconds;
    return Result<qint64>::withValue(epochSeconds * kNanosPerSec + nano);
}

QString utcDay(qint64 epochNanos)
{
    qint64 days = 0;
    qint64 hour = 0;
    qint64 minute = 0;
    qint64 second = 0;
    breakDownSeconds(epochNanos / kNanosPerSec, days, hour, minute, second);
    const QDate date = QDate::fromJulianDay(kJulianDayUnixEpoch + int(days));
    return QStringLiteral("%1-%2-%3")
        .arg(date.year(), 4, 10, QLatin1Char('0'))
        .arg(date.month(), 2, 10, QLatin1Char('0'))
        .arg(date.day(), 2, 10, QLatin1Char('0'));
}

QString localDay(qint64 epochNanos)
{
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(epochNanos / 1000000LL,
                                                           QTimeZone::systemTimeZone());
    return local.date().toString(QStringLiteral("yyyy-MM-dd"));
}

qint64 localDayStartNanos(const QString &day)
{
    const QDate date = QDate::fromString(day, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid()) {
        return 0;
    }
    const QDateTime start(date, QTime(0, 0, 0), QTimeZone::systemTimeZone());
    return start.isValid() ? start.toMSecsSinceEpoch() * 1000000LL : 0;
}

qint64 secsToNanos(qint64 seconds)
{
    return seconds * kNanosPerSec;
}

QString formatLocalReadable(qint64 epochNanos)
{
    return QDateTime::fromMSecsSinceEpoch(epochNanos / 1000000LL, QTimeZone::systemTimeZone())
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString formatUtcMillis(qint64 epochNanos)
{
    // 对应 chrono::Local::now().format("%Y-%m-%d %H:%M:%S%.3f")
    return QDateTime::fromMSecsSinceEpoch(epochNanos / 1000000LL, QTimeZone::systemTimeZone())
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

}
