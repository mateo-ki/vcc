#include "sysproxy/SystemProxy.h"

#include <QRegularExpression>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#endif

#include "sysproxy/Format.h"
#include "sysproxy/Messages.h"
#include "sysproxy/RegRunner.h"

namespace SysProxy {

namespace {

/// 逐个空白切分，等价于 Rust 的 `split_whitespace`。
QStringList splitWhitespace(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QStringList tokens;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return tokens;
    }
    QRegularExpressionMatchIterator iterator = whitespace.globalMatch(trimmed);
    int previous = 0;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        tokens.append(trimmed.mid(previous, int(match.capturedStart()) - previous));
        previous = int(match.capturedEnd());
    }
    tokens.append(trimmed.mid(previous));
    return tokens;
}

}

QString commandOutputText(const QByteArray &bytes)
{
    // reg.exe 在中文 Windows 上输出的是 GBK 而非 UTF-8，lossy 解码会留下一串替换字符
    // （界面上那些乱码就是这么来的）。解不出来就别往界面上塞。
    QString decoded = QString::fromUtf8(bytes);
    // Qt 6.10 没有可用的"按 QChar 删除"重载，逐字符过滤最稳。
    static constexpr ushort kReplacementCharacter = 0xFFFD;
    QString text;
    text.reserve(decoded.size());
    for (const QChar character : decoded) {
        if (character.unicode() != kReplacementCharacter) {
            text.append(character);
        }
    }
    return text.trimmed();
}

QString regFailureDetail(const ProcessResult &output)
{
    const QString text = commandOutputText(output.stdOut) + commandOutputText(output.stdErr);
    if (text.isEmpty()) {
        // 对应 `format!("reg.exe 退出码 {:?}", output.status.code())`：Option 的 Debug
        // 形式是 Some(n) / None，不是裸数字。
        const QString code = output.finished
            ? QStringLiteral("Some(%1)").arg(output.exitCode)
            : QStringLiteral("None");
        return fillTemplate(Messages::regExitCode(), {}, {code});
    }
    return text;
}

std::optional<bool> parseDword(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QLatin1String("0x"))) {
        bool ok = false;
        const quint32 parsed = trimmed.mid(2).toUInt(&ok, 16);
        if (!ok) {
            return std::nullopt;
        }
        return parsed != 0;
    }
    bool ok = false;
    const quint32 parsed = trimmed.toUInt(&ok, 10);
    if (!ok) {
        return std::nullopt;
    }
    return parsed != 0;
}

QString parseRegValue(const QString &output, const QString &name)
{
    // reg.exe query 的一行形如 `    ProxyServer    REG_SZ    127.0.0.1:7890`。
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\r\n]")));
    for (const QString &line : lines) {
        const QStringList parts = splitWhitespace(line);
        if (parts.size() < 2 || parts.at(0) != name) {
            continue;
        }
        if (!parts.at(1).startsWith(QLatin1String("REG_"))) {
            continue;
        }
        const QString value = QStringList(parts.mid(2)).join(QLatin1Char(' '));
        return value.isEmpty() ? QString() : value;
    }
    return QString();
}

QString normalizeProxyServer(const QString &server)
{
    // ProxyServer 可能是 "127.0.0.1:10808"，也可能是 "http=...;https=...;ftp=..."。
    QString hostPort;
    if (server.contains(QLatin1Char('=')) || server.contains(QLatin1Char(';'))) {
        // 优先 https=（本程序拉的都是 https），找不到再退 http=。
        const QStringList parts = server.split(QLatin1Char(';'));
        for (const QString &part : parts) {
            const QString trimmed = part.trimmed();
            if (trimmed.startsWith(QLatin1String("https="))) {
                hostPort = trimmed.mid(6).trimmed();
                break;
            }
        }
        if (hostPort.isEmpty()) {
            for (const QString &part : parts) {
                const QString trimmed = part.trimmed();
                if (trimmed.startsWith(QLatin1String("http="))) {
                    hostPort = trimmed.mid(5).trimmed();
                    break;
                }
            }
        }
    } else {
        hostPort = server.trimmed();
    }
    if (hostPort.isEmpty()) {
        return QString();
    }
    return QStringLiteral("http://%1").arg(hostPort);
}

QString queryRegValue(const QString &name)
{
#ifdef Q_OS_WIN
    const ProcessResult output =
        runHidden(QStringLiteral("reg.exe"),
                  {QStringLiteral("query"), QLatin1String(kInternetSettingsKey),
                   QStringLiteral("/v"), name});
    if (!output.ok) {
        return QString();
    }
    return parseRegValue(QString::fromUtf8(output.stdOut), name);
#else
    Q_UNUSED(name);
    return QString();
#endif
}

Status setRegString(const QString &name, const QString &value)
{
#ifdef Q_OS_WIN
    const ProcessResult output = runHidden(
        QStringLiteral("reg.exe"),
        {QStringLiteral("add"), QLatin1String(kInternetSettingsKey), QStringLiteral("/v"), name,
         QStringLiteral("/t"), QStringLiteral("REG_SZ"), QStringLiteral("/d"), value,
         QStringLiteral("/f")});
    if (output.ok) {
        return Status::succeeded();
    }
    return Status::failed(
        fillTemplate(Messages::modifyFailed(), {{QStringLiteral("{detail}"),
                                                regFailureDetail(output)}}));
#else
    Q_UNUSED(name);
    Q_UNUSED(value);
    return Status::failed(Messages::platformUnsupported());
#endif
}

Status setRegDword(const QString &name, quint32 value)
{
#ifdef Q_OS_WIN
    const ProcessResult output = runHidden(
        QStringLiteral("reg.exe"),
        {QStringLiteral("add"), QLatin1String(kInternetSettingsKey), QStringLiteral("/v"), name,
         QStringLiteral("/t"), QStringLiteral("REG_DWORD"), QStringLiteral("/d"),
         QString::number(value), QStringLiteral("/f")});
    if (output.ok) {
        return Status::succeeded();
    }
    return Status::failed(
        fillTemplate(Messages::modifyFailed(), {{QStringLiteral("{detail}"),
                                                regFailureDetail(output)}}));
#else
    Q_UNUSED(name);
    Q_UNUSED(value);
    return Status::failed(Messages::platformUnsupported());
#endif
}

Status deleteRegValue(const QString &name)
{
#ifdef Q_OS_WIN
    // 值本来就不存在（上一次会话根本没设过 ProxyOverride）是恢复快照的正常情况。
    // 不能靠 reg.exe 的退出码或提示文字判断：实测这台机器"找不到值"返回 1，而它
    // 的中文提示是 GBK，转成 UTF-8 只剩乱码。先查一次存在性最可靠。
    if (queryRegValue(name).isNull()) {
        return Status::succeeded();
    }
    const ProcessResult output =
        runHidden(QStringLiteral("reg.exe"),
                  {QStringLiteral("delete"), QLatin1String(kInternetSettingsKey),
                   QStringLiteral("/v"), name, QStringLiteral("/f")});
    if (output.ok) {
        return Status::succeeded();
    }
    return Status::failed(fillTemplate(
        Messages::deleteValueFailed(),
        {{QStringLiteral("{name}"), name}, {QStringLiteral("{}"), regFailureDetail(output)}}));
#else
    Q_UNUSED(name);
    return Status::failed(Messages::platformUnsupported());
#endif
}

Status notifyProxyChange()
{
#ifdef Q_OS_WIN
    const BOOL changed = InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    const BOOL refreshed = InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
    if (changed == 0 || refreshed == 0) {
        return Status::failed(Messages::wininetNotifyFailed());
    }
    return Status::succeeded();
#else
    return Status::failed(Messages::platformUnsupported());
#endif
}

Result<Snapshot> capture()
{
#ifdef Q_OS_WIN
    Snapshot snapshot;
    const QString enabled = queryRegValue(QStringLiteral("ProxyEnable"));
    const std::optional<bool> parsed = enabled.isNull() ? std::nullopt : parseDword(enabled);
    snapshot.enabled = parsed.value_or(false);
    snapshot.server = queryRegValue(QStringLiteral("ProxyServer"));
    snapshot.overrideList = queryRegValue(QStringLiteral("ProxyOverride"));
    return Result<Snapshot>::withValue(snapshot);
#else
    return Result<Snapshot>::withError(Messages::platformUnsupported());
#endif
}

Status enable(const QString &server)
{
#ifdef Q_OS_WIN
    const Status wroteServer = setRegString(QStringLiteral("ProxyServer"), server);
    if (wroteServer.isErr()) {
        return wroteServer;
    }
    const Status wroteEnable = setRegDword(QStringLiteral("ProxyEnable"), 1);
    if (wroteEnable.isErr()) {
        return wroteEnable;
    }
    return notifyProxyChange();
#else
    Q_UNUSED(server);
    return Status::failed(Messages::platformUnsupported());
#endif
}

Status restore(const Snapshot &snapshot)
{
#ifdef Q_OS_WIN
    if (snapshot.server.isNull()) {
        const Status deleted = deleteRegValue(QStringLiteral("ProxyServer"));
        if (deleted.isErr()) {
            return deleted;
        }
    } else {
        const Status wrote = setRegString(QStringLiteral("ProxyServer"), snapshot.server);
        if (wrote.isErr()) {
            return wrote;
        }
    }
    if (snapshot.overrideList.isNull()) {
        const Status deleted = deleteRegValue(QStringLiteral("ProxyOverride"));
        if (deleted.isErr()) {
            return deleted;
        }
    } else {
        const Status wrote =
            setRegString(QStringLiteral("ProxyOverride"), snapshot.overrideList);
        if (wrote.isErr()) {
            return wrote;
        }
    }
    const Status wroteEnable =
        setRegDword(QStringLiteral("ProxyEnable"), snapshot.enabled ? 1u : 0u);
    if (wroteEnable.isErr()) {
        return wroteEnable;
    }
    return notifyProxyChange();
#else
    Q_UNUSED(snapshot);
    return Status::failed(Messages::platformUnsupported());
#endif
}

QString currentSystemProxyUrl()
{
    const Result<Snapshot> captured = capture();
    if (captured.isErr() || !captured.value().enabled) {
        return QString();
    }
    return normalizeProxyServer(captured.value().server);
}

Json Snapshot::toJson() const
{
    Json value;
    value["enabled"] = enabled;
    value["server"] = JsonUtil::optText(server);
    value["override_list"] = JsonUtil::optText(overrideList);
    return value;
}

}
