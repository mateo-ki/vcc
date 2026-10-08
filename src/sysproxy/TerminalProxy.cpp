#include "sysproxy/TerminalProxy.h"

#include <QRegularExpression>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "sysproxy/Format.h"
#include "sysproxy/Messages.h"
#include "sysproxy/RegRunner.h"

namespace TerminalProxy {

namespace Messages = SysProxy::Messages;

using SysProxy::fillTemplate;
using SysProxy::ProcessResult;
using SysProxy::runHidden;

const QStringList kProxyVars = {QStringLiteral("HTTP_PROXY"), QStringLiteral("HTTPS_PROXY"),
                                QStringLiteral("ALL_PROXY")};
const QString kNoProxyVar = QStringLiteral("NO_PROXY");

namespace {

constexpr const char *kEnvironmentKey = "HKCU\\Environment";

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

QString lossyText(const QByteArray &bytes)
{
    // 这里保留 Rust 的原样：terminal.rs 用的是 from_utf8_lossy，没有过滤替换字符。
    return QString::fromUtf8(bytes);
}

Status writeEnvValue(const QString &name, const QString &value)
{
    const ProcessResult output = runHidden(
        QStringLiteral("reg.exe"),
        {QStringLiteral("add"), QLatin1String(kEnvironmentKey), QStringLiteral("/v"), name,
         QStringLiteral("/t"), QStringLiteral("REG_SZ"), QStringLiteral("/d"), value,
         QStringLiteral("/f")});
    if (output.ok) {
        return Status::succeeded();
    }
    return Status::failed(fillTemplate(Messages::writeFailed(),
                                       {{QStringLiteral("{name}"), name},
                                        {QStringLiteral("{}"), lossyText(output.stdOut)}}));
}

Status deleteEnvValue(const QString &name)
{
    const ProcessResult output =
        runHidden(QStringLiteral("reg.exe"),
                  {QStringLiteral("delete"), QLatin1String(kEnvironmentKey),
                   QStringLiteral("/v"), name, QStringLiteral("/f")});
    // 值不存在时 reg.exe 的中文提示是 GBK，退出码也不统一，所以先查存在性。
    if (output.ok || readEnvValue(name).isNull()) {
        return Status::succeeded();
    }
    return Status::failed(fillTemplate(Messages::removeFailed(),
                                       {{QStringLiteral("{name}"), name},
                                        {QStringLiteral("{}"), lossyText(output.stdOut)}}));
}

/// 让已经开着的终端也能拿到新值（explorer 会转发 WM_SETTINGCHANGE）。
void broadcastEnvironmentChange()
{
#ifdef Q_OS_WIN
    static const wchar_t *kEnvironment = L"Environment";
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(kEnvironment), SMTO_ABORTIFHUNG, 2000, &result);
#endif
}

}

QString noProxyValue()
{
    return Messages::noProxyValue();
}

bool isOurs(const QString &value)
{
    return value.trimmed().startsWith(QLatin1String("http://127.0.0.1:"));
}

QString readEnvValue(const QString &name)
{
#ifdef Q_OS_WIN
    const ProcessResult output =
        runHidden(QStringLiteral("reg.exe"),
                  {QStringLiteral("query"), QLatin1String(kEnvironmentKey), QStringLiteral("/v"),
                   name});
    if (!output.ok) {
        return QString();
    }
    const QString text = lossyText(output.stdOut);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]")));
    for (const QString &line : lines) {
        const QStringList fields = splitWhitespace(line);
        if (fields.size() < 3) {
            continue;
        }
        if (fields.at(1).toUpper() != QLatin1String("REG_SZ")) {
            continue;
        }
        if (fields.at(0).toUpper() == name.toUpper()) {
            return fields.at(2);
        }
    }
    return QString();
#else
    Q_UNUSED(name);
    return QString();
#endif
}

QString Report::summary() const
{
    QStringList parts;
    if (!written.isEmpty()) {
        parts.append(fillTemplate(Messages::writtenPrefix(), {}, {written.join(QLatin1Char('/'))}));
    }
    if (!removed.isEmpty()) {
        parts.append(fillTemplate(Messages::removedPrefix(), {}, {removed.join(QLatin1Char('/'))}));
    }
    if (!untouched.isEmpty()) {
        parts.append(
            fillTemplate(Messages::untouchedPrefix(), {}, {untouched.join(QLatin1Char('/'))}));
    }
    if (parts.isEmpty()) {
        return Messages::noChange();
    }
    return parts.join(Messages::joiner());
}

Result<Report> enable(quint16 port)
{
#ifdef Q_OS_WIN
    const QString server = QStringLiteral("http://127.0.0.1:%1").arg(port);
    Report report;
    // 先整体检查：只要有一个变量是用户自己设的（不是我们的），就一个都不动。
    for (const QString &name : kProxyVars) {
        const QString existing = readEnvValue(name);
        if (!existing.isNull() && !isOurs(existing)) {
            return Result<Report>::withError(fillTemplate(
                Messages::alreadyYours(),
                {{QStringLiteral("{name}"), name}, {QStringLiteral("{existing}"), existing}}));
        }
    }
    for (const QString &name : kProxyVars) {
        const Status written = writeEnvValue(name, server);
        if (written.isErr()) {
            return Result<Report>::withError(written.error());
        }
        report.written.append(name);
    }
    const QString existingNoProxy = readEnvValue(kNoProxyVar);
    if (!existingNoProxy.isNull() && !existingNoProxy.trimmed().isEmpty()) {
        report.untouched.append(kNoProxyVar);
    } else {
        const Status written = writeEnvValue(kNoProxyVar, noProxyValue());
        if (written.isErr()) {
            return Result<Report>::withError(written.error());
        }
        report.written.append(kNoProxyVar);
    }
    broadcastEnvironmentChange();
    return Result<Report>::withValue(report);
#else
    Q_UNUSED(port);
    return Result<Report>::withError(Messages::windowsOnly());
#endif
}

Result<Report> disable()
{
#ifdef Q_OS_WIN
    Report report;
    for (const QString &name : kProxyVars) {
        const QString existing = readEnvValue(name);
        if (existing.isNull()) {
            continue;
        }
        if (isOurs(existing)) {
            const Status removed = deleteEnvValue(name);
            if (removed.isErr()) {
                return Result<Report>::withError(removed.error());
            }
            report.removed.append(name);
        } else {
            report.untouched.append(name);
        }
    }
    // NO_PROXY 只有在没有代理变量残留时才顺手清掉，避免误删用户自己的例外列表。
    if (report.untouched.isEmpty()) {
        const QString existing = readEnvValue(kNoProxyVar);
        if (!existing.isNull() && existing == noProxyValue()) {
            const Status removed = deleteEnvValue(kNoProxyVar);
            if (removed.isErr()) {
                return Result<Report>::withError(removed.error());
            }
            report.removed.append(kNoProxyVar);
        }
    }
    broadcastEnvironmentChange();
    return Result<Report>::withValue(report);
#else
    return Result<Report>::withError(Messages::windowsOnly());
#endif
}

}
