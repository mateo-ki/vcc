#include "core/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdlib>

#include "core/TimeUtil.h"

namespace {

QMutex g_logMutex;
QString g_logPath;
bool g_initialized = false;

QString tempFallbackPath()
{
    const QString temp = QString::fromLocal8Bit(std::getenv("TEMP"));
    const QString base = temp.isEmpty() ? QDir::tempPath() : temp;
    return QDir(base).filePath(QStringLiteral("vcc-startup.log"));
}

bool appendLine(const QString &path, const QString &line)
{
    QFile file(path);
    // 每次打开追加：进程被硬杀也不会丢已写内容，代价是可以接受。
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << line << '\n';
    return true;
}

/// QMutex 不可重入：所有公开入口统一在这里加一次锁，内部函数一律用 *Locked 后缀
/// 表示"调用方已持锁"。之前 initialize() 自己又加了一次锁，
/// 于是 startup() 里惰性初始化会自死锁（第二次 migrate 打日志时就是这个现象）。
void ensureInitializedLocked()
{
    if (g_initialized) {
        return;
    }
    g_initialized = true;
    const QString candidate =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("startup.log"));
    g_logPath = appendLine(candidate, QString()) ? candidate : tempFallbackPath();
}

}

namespace Log {

void initialize()
{
    QMutexLocker locker(&g_logMutex);
    ensureInitializedLocked();
}

QString logFilePath()
{
    QMutexLocker locker(&g_logMutex);
    ensureInitializedLocked();
    return g_logPath;
}

void write(Level level, const QString &message)
{
    static const char *kTags[] = {"INFO", "WARN", "ERROR"};
    const QString tag = QLatin1String(kTags[int(level)]);
    startup(QStringLiteral("%1 %2").arg(tag, message));
}

void startup(const QString &message)
{
    const QString line =
        QStringLiteral("[%1] %2").arg(TimeUtil::formatUtcMillis(TimeUtil::nowUtcNanos()), message);
    QMutexLocker locker(&g_logMutex);
    ensureInitializedLocked();
    if (!appendLine(g_logPath, line)) {
        const QString fallback = tempFallbackPath();
        if (fallback != g_logPath) {
            g_logPath = fallback;
            appendLine(g_logPath, line);
        }
    }
}

}
