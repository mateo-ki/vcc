#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/Result.h"
#include "geo/GeoConstants.h"

namespace Geo {

/// 真库是 11MB / 16MB；小于这个数一定是半截货（历史上那次截断是 130,209 字节）。
constexpr quint64 kMinGeoBytes = 1000000ULL;

extern const QVector<QString> kGeoFiles;

/// mihomo 的 -d 工作目录，geo 文件就放这里。
QString mihomoDir();

/// 对应 Rust `geo_state` 的一项：文件名 + "可用大小"。
struct FileState {
    QString name;
    std::optional<quint64> usableBytes;
};

QVector<FileState> geoState(const QString &dir);

bool geoReady(const QString &dir);

/// 缺什么补什么；返回一句给人看的报告。force=true 时两个库全部重下。
/// 阻塞式下载，只能在 worker 线程调用。
Result<QString> provision(const QString &dir, const QString &proxyUrl, bool force);

/// 校验响应体：够大、且不是镜像挂了返回的 HTML。返回字节数。
Result<quint64> validateGeoBody(const QByteArray &bytes);

}
