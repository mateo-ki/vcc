#pragma once

#include <QString>

// 日志落在程序目录的 startup.log，写不进去（装在 Program Files 又没提权）就退到
// %TEMP%\vpn-parser-startup.log：预备状态那些步骤要是因为权限看不见，就等于没打日志。
namespace Log {

enum class Level { Info, Warn, Error };

/// 打开日志文件（程序目录优先，失败退到临时目录）。可重复调用。
void initialize();
void write(Level level, const QString &message);
void startup(const QString &message);

inline void info(const QString &message) { write(Level::Info, message); }
inline void warn(const QString &message) { write(Level::Warn, message); }
inline void error(const QString &message) { write(Level::Error, message); }

/// 当前实际写入的日志文件路径，界面里要显示它。
QString logFilePath();

}
