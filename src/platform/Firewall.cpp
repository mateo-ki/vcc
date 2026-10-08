#include "platform/Firewall.h"

#include <QFile>
#include <QFileInfo>

#include "core/Log.h"
#include "platform/Format.h"
#include "platform/Messages.h"
#include "sysproxy/RegRunner.h"

namespace Platform::Firewall {

namespace {

/// Rust 的 `Path::display()`：这边直接是路径原文（Windows 用反斜杠）。
QString displayPath(const QString &path)
{
    return path;
}

/// Rust 的 `{:?}` 打一个 Path/OsStr：带引号，反斜杠与引号都要转义。
QString debugQuoted(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"')) {
            out += QLatin1Char('\\');
        }
        out += ch;
    }
    out += QLatin1Char('"');
    return out;
}

/// 取路径的文件名主干，对应 Rust `Path::file_stem()`；拿不到时 Rust 用 "program" 兜底。
QString fileStem(const QString &path)
{
    const QFileInfo info(path);
    const QString fileName = info.fileName();
    if (fileName.isEmpty()) {
        return QStringLiteral("program");
    }
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    // Rust 的 file_stem：最后一个点之前的部分；点在开头（".gitignore"）时整个名字就是主干。
    if (dot <= 0) {
        return fileName;
    }
    return fileName.left(dot);
}

/// 对应 Rust `Path::canonicalize()`：绝对化、去冗余；不存在就失败。
Result<QString> canonicalizePath(const QString &path)
{
    const QFileInfo info(path);
    const QString full = info.canonicalFilePath();
    if (full.isEmpty()) {
        return Result<QString>::withError(
            Messages::canonicalizeFailed()
                .replace(QStringLiteral("{:?}"), debugQuoted(path))
                .replace(QStringLiteral("{error}"),
                         QStringLiteral("The system cannot find the file path (os error 2)")));
    }
    return Result<QString>::withValue(full);
}

} // namespace

QString rulePrefix()
{
    return QStringLiteral("VPN Parser");
}

QString ruleName(const QString &programPath)
{
    // 名字只由"前缀 + 文件主干"决定，与目录无关：路径不变、exe 变了（每次重编译）
    // 规则名也不变，这正是"按路径放行"的意义（Rust 那条测试守的就是这一点）。
    return fillTemplate(Messages::ruleNameTemplate(),
                        { { QStringLiteral("{RULE_PREFIX}"), rulePrefix() },
                          { QStringLiteral("{stem}"), fileStem(programPath) } });
}

QStringList showRuleArguments(const QString &name)
{
    return { QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("show"),
             QStringLiteral("rule"), QStringLiteral("name=%1").arg(name) };
}

QStringList addRuleArguments(const QString &name, const QString &programPath)
{
    // `program=<路径>` 必须是**一个**参数：Rust 用 Command::args 传，带空格的路径
    // 由 std 的命令行拼装负责加引号，这边交给 QProcess（SysProxy::runHidden）同样负责。
    return { QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("add"),
             QStringLiteral("rule"), QStringLiteral("name=%1").arg(name),
             QStringLiteral("program=%1").arg(displayPath(programPath)), QStringLiteral("dir=in"),
             QStringLiteral("action=allow"), QStringLiteral("enable=yes"),
             QStringLiteral("profile=any") };
}

bool ruleExistsFromOutput(const QByteArray &stdOut, const QString &name)
{
    // 找不到时 netsh 仍返回 0，只能看输出里有没有这条规则的名字。
    // Rust 用 String::from_utf8_lossy 解码，这里同样用 fromUtf8（非法字节变成 U+FFFD）：
    // 中文系统上 netsh 的输出是 GBK，规则名里那个 U+00B7 永远对不上，
    // 于是"已存在就跳过"实际上会退化成"每次都重加"。这是 Rust 的真实行为，照抄不改。
    const QString text = QString::fromUtf8(stdOut);
    return text.contains(name);
}

bool ruleExists(const QString &programPath)
{
#ifdef Q_OS_WIN
    const QString name = ruleName(programPath);
    const SysProxy::ProcessResult output = SysProxy::runHidden(QStringLiteral("netsh"),
                                                              showRuleArguments(name));
    if (!output.ok) {
        // Rust 那边是 `output.status.success()`：非 0 退出或没能启动都算"不存在"。
        return false;
    }
    return ruleExistsFromOutput(output.stdOut, name);
#else
    Q_UNUSED(programPath);
    return false;
#endif
}

bool shouldSkipMissing(const QString &programPath)
{
    // Rust: `if !program.exists() { continue; }` —— 路径不存在不算失败，直接跳过。
    return !QFile::exists(programPath);
}

QString failureEntry(const QString &programPath, const QString &error)
{
    // Rust: `format!("{:?}: {error}", program.file_name().unwrap_or_default())`
    const QString fileName = QFileInfo(programPath).fileName();
    return QStringLiteral("%1: %2").arg(debugQuoted(fileName), error);
}

Result<QString> ensureAllowed(const QString &programPath)
{
#ifdef Q_OS_WIN
    const Result<QString> full = canonicalizePath(programPath);
    if (full.isErr()) {
        return full;
    }
    const QString path = full.value();
    if (ruleExists(path)) {
        return Result<QString>::withValue(
            fillTemplate(Messages::alreadyAllowed(), {}, { displayPath(path) }));
    }
    const QString name = ruleName(path);
    const SysProxy::ProcessResult output = SysProxy::runHidden(QStringLiteral("netsh"),
                                                               addRuleArguments(name, path));
    if (!output.started) {
        // 对应 Rust `command.output()` 返回 io::Error 那条分支（netsh 根本没能跑起来）。
        return Result<QString>::withError(fillTemplate(Messages::netshFailed(),
                                                      { { QStringLiteral("{error}"),
                                                        QStringLiteral("CreateProcess failed") } }));
    }
    if (!output.ok) {
        // 添加失败：取 stderr 的第一行并 trim，空的话用"未知错误"（Rust 的 unwrap_or）。
        const QString errorText = QString::fromUtf8(output.stdErr);
        QString firstLine;
        for (const QString &line : errorText.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                firstLine = trimmed;
                break;
            }
        }
        if (firstLine.isEmpty()) {
            firstLine = Messages::unknownError();
        }
        return Result<QString>::withError(
            fillTemplate(Messages::addRuleFailed(), {}, { firstLine }));
    }
    return Result<QString>::withValue(
        fillTemplate(Messages::addedToAllowList(), {}, { displayPath(path) }));
#else
    Q_UNUSED(programPath);
    return Result<QString>::withError(
        fillTemplate(Messages::windowsOnlySingle(), {}, { displayPath(programPath) }));
#endif
}

Result<QString> ensureAll(const QStringList &programPaths)
{
#ifdef Q_OS_WIN
    QStringList failures;
    int added = 0;
    for (const QString &program : programPaths) {
        if (shouldSkipMissing(program)) {
            continue;
        }
        const Result<QString> report = ensureAllowed(program);
        if (report.isOk()) {
            added += 1;
        } else {
            failures << failureEntry(program, report.error());
        }
    }
    if (failures.isEmpty()) {
        return Result<QString>::withValue(
            fillTemplate(Messages::allReady(), {}, { QString::number(added) }));
    }
    return Result<QString>::withError(fillTemplate(Messages::partialFailure(),
                                                  {},
                                                  { QString::number(added),
                                                    QString::number(failures.size()),
                                                    failures.join(Messages::failureJoiner()) }));
#else
    // Rust 的非 Windows 分支不遍历，直接报"仅 Windows 支持…（N 个程序）"。
    return Result<QString>::withError(
        fillTemplate(Messages::windowsOnlyMany(), {}, { QString::number(programPaths.size()) }));
#endif
}

}
