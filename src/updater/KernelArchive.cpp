#include "updater/KernelArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <archive.h>
#include <archive_entry.h>

#include "kernel/KernelMessages.h"
#include "updater/UpdaterFormat.h"

namespace Updater {

namespace {

/// ZIP 条目名的分隔符：Rust 用 `Path::components()`，Windows 上 `/` 与 `\` 都算分隔符。
bool isSeparator(QChar character)
{
    return character == QLatin1Char('/') || character == QLatin1Char('\\');
}

/// `Component::Prefix`：形如 `C:` / `\\host\share` 的开头分量，Rust 那边直接 return None。
bool hasWindowsPrefix(const QString &name)
{
    if (name.size() >= 2 && name.at(0).isLetter() && name.at(1) == QLatin1Char(':')) {
        return true;
    }
    return false;
}

/// 条目数据的安全上限：整包已经被上层卡在 150 MB 以内，但 zip 里的单个条目可以
/// 解压成远大于原文件的东西（zip bomb）。Rust 直接 `read_to_end` 不封顶，
/// 这里封顶并把失败当错误返回，避免把机器内存打穿。
constexpr qint64 kMaxEntryBytes = qint64(150) * 1024 * 1024;

/// 读完整条数据（对应 Rust 的 `entry.read_to_end(&mut bytes)`）。
Result<QByteArray> readEntryData(struct archive *reader)
{
    QByteArray body;
    char buffer[65536];
    for (;;) {
        const la_ssize_t read = archive_read_data(reader, buffer, sizeof(buffer));
        if (read == 0) {
            return Result<QByteArray>::withValue(std::move(body));
        }
        if (read < 0) {
            const char *detail = archive_error_string(reader);
            return Result<QByteArray>::withError(
                QStringLiteral("archive_read_data failed: %1")
                    .arg(QString::fromUtf8(detail ? detail : "unknown")));
        }
        body.append(buffer, int(read));
        if (body.size() > kMaxEntryBytes) {
            return Result<QByteArray>::withError(QStringLiteral("archive entry too large"));
        }
    }
}

/// 等价于 Rust 的 `std::fs::write(path, bytes)`：存在就覆盖（QFile 在 Windows 上走
/// 宽字符路径，非 ASCII 目录名不会被当成 ANSI 处理）。
Status writeFileBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Status::failed(QStringLiteral("write failed: %1 (%2)")
                                  .arg(path, file.errorString()));
    }
    const qint64 written = file.write(bytes);
    file.close();
    if (written != bytes.size()) {
        return Status::failed(QStringLiteral("write truncated: %1").arg(path));
    }
    return Status::succeeded();
}

QString lowerAscii(const QString &text)
{
    QString out = text;
    for (QChar &character : out) {
        if (character.unicode() >= 'A' && character.unicode() <= 'Z') {
            character = QChar(character.unicode() + 32);
        }
    }
    return out;
}

}

std::optional<QStringList> enclosedNameComponents(const QString &entryName)
{
    // Rust：Path::components() 里 Prefix/RootDir/ParentDir 都是"这条不要"，
    // CurDir 跳过，Normal 收下；全空的（例如 "."）也当没有名字。
    if (entryName.isEmpty()) {
        return std::nullopt;
    }
    if (hasWindowsPrefix(entryName)) {
        return std::nullopt;
    }
    const bool uncLike = entryName.startsWith(QLatin1String("\\\\"));
    if (entryName.at(0) == QLatin1Char('/') || entryName.at(0) == QLatin1Char('\\') || uncLike) {
        return std::nullopt;
    }
    QStringList components;
    QString segment;
    for (const QChar &character : entryName) {
        if (isSeparator(character)) {
            if (!segment.isEmpty()) {
                if (segment == QLatin1String("..")) {
                    return std::nullopt;
                }
                if (segment != QLatin1String(".")) {
                    components.push_back(segment);
                }
            }
            segment.clear();
            continue;
        }
        segment.append(character);
    }
    if (!segment.isEmpty()) {
        if (segment == QLatin1String("..")) {
            return std::nullopt;
        }
        if (segment != QLatin1String(".")) {
            components.push_back(segment);
        }
    }
    if (components.isEmpty()) {
        return std::nullopt;
    }
    return components;
}

Status extractExecutable(const QByteArray &archive, Kernel::Kind kind, const QString &target)
{
    struct archive *reader = archive_read_new();
    if (reader == nullptr) {
        return Status::failed(Kernel::Messages::zipOpenFailed());
    }
    // Rust 那边只吃 ZIP（`zip::ZipArchive::new`），所以只挂 zip 格式；filter 全开是
    // 为了让"外层还套了一层 gzip/bz2 的包"也能读出来（libarchive 的 raw 处理）。
    archive_read_support_filter_all(reader);
    archive_read_support_format_zip(reader);
    // 条目名字符集按格式喂给 libarchive：zip crate 对没有 UTF-8 标志位的条目名用
    // CP437 解码，这里对齐（认不出这个选项就退回 libarchive 的默认转换，内核 ZIP 里
    // 那几个名字本来就是纯 ASCII，两种解法结果一致）。
    archive_read_set_options(reader, "hdrcharset=CP437");

    const Status outcome = [&]() {
        if (archive_read_open_memory(reader, archive.constData(), size_t(archive.size()))
            != ARCHIVE_OK) {
            return Status::failed(Kernel::Messages::zipOpenFailed());
        }
        const QString targetDirectory = QFileInfo(target).path();
        bool executableFound = false;
        for (;;) {
            struct archive_entry *entry = nullptr;
            const int code = archive_read_next_header(reader, &entry);
            if (code == ARCHIVE_EOF) {
                break;
            }
            if (code != ARCHIVE_OK) {
                const char *detail = archive_error_string(reader);
                return Status::failed(QStringLiteral("archive_read_next_header failed: %1")
                                          .arg(QString::fromUtf8(detail ? detail : "unknown")));
            }
            const char *utf8Name = archive_entry_pathname_utf8(entry);
            const char *rawName = utf8Name ? utf8Name : archive_entry_pathname(entry);
            const QString entryName =
                rawName ? QString::fromUtf8(rawName) : QString();
            const std::optional<QStringList> components = enclosedNameComponents(entryName);
            if (!components.has_value()) {
                // Rust：enclosed_name() 不给路径就 continue，但条目数据要跳过才能继续迭代。
                archive_read_data_skip(reader);
                continue;
            }
            const QString fileName = components->isEmpty() ? QString() : components->back();
            if (fileName.isEmpty()) {
                archive_read_data_skip(reader);
                continue;
            }
            const bool isExecutable = Kernel::kindMatchesArchiveExecutable(kind, fileName);
            const QString lower = lowerAscii(fileName);
            const bool isXrayData = kind == Kernel::Kind::Xray
                && (lower == QLatin1String("geoip.dat") || lower == QLatin1String("geosite.dat"));
            if (!isExecutable && !isXrayData) {
                archive_read_data_skip(reader);
                continue;
            }
            const Result<QByteArray> body = readEntryData(reader);
            if (body.isErr()) {
                return Status::failed(body.error());
            }
            if (isExecutable) {
                const Status written = writeFileBytes(target, body.value());
                if (written.isErr()) {
                    return written;
                }
                executableFound = true;
            } else if (!targetDirectory.isEmpty()) {
                const Status written =
                    writeFileBytes(QDir(targetDirectory).filePath(fileName), body.value());
                if (written.isErr()) {
                    return written;
                }
            }
        }
        if (executableFound) {
            return Status::succeeded();
        }
        return Status::failed(fillTemplate(Kernel::Messages::zipMissingExecutable(),
                                           {},
                                           {Kernel::kindExecutableName(kind)}));
    }();

    archive_read_close(reader);
    archive_read_free(reader);
    return outcome;
}

}
