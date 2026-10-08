#include "core/Crypto.h"

#include <QFile>
#include <QCryptographicHash>

namespace Crypto {

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

Result<QString> sha256HexOfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QString>::withError(
            QStringLiteral("打不开文件 %1: %2").arg(path, file.errorString()));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // 1 MiB 缓冲，和 Rust 侧分块读取的内存峰值同量级。
    static constexpr qint64 kChunkBytes = 1LL << 20;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunkBytes);
        if (chunk.isEmpty()) {
            if (file.error() != QFileDevice::NoError) {
                return Result<QString>::withError(
                    QStringLiteral("读取 %1 失败: %2").arg(path, file.errorString()));
            }
            break;
        }
        hash.addData(chunk);
    }
    return Result<QString>::withValue(QString::fromLatin1(hash.result().toHex()));
}

}
