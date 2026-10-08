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
    return QDir(base).filePath(QStringLiteral("vpn-parser-startup.log"));
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

}

namespace Log {

void initialize()
{
    QMutexLocker locker(&g_logMutex);
    if (g_initialized) {
        return;
    }
    g_initialized = true;
    const QString candidate =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("startup.log"));
    g_logPath = appendLine(candidate, QString()) ? candidate : tempFallbackPath();
}

QString logFilePath()
{
    QMutexLocker locker(&g_logMutex);
    if (g_logPath.isEmpty()) {
        initialize();
    }
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
    if (!g_initialized) {
        initialize();
    }
    if (!appendLine(g_logPath, line)) {
        const QString fallback = tempFallbackPath();
        if (fallback != g_logPath) {
            g_logPath = fallback;
            appendLine(g_logPath, line);
        }
    }
}

}
