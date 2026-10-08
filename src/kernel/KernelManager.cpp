#include "kernel/KernelManager.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <initializer_list>
#include <iterator>

#include "core/Log.h"
#include "kernel/KernelMessages.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Kernel {

namespace {

/// Rust 的 `format!` 只解释模板一次，插进去的值不会被二次求值。这里同样一遍扫描，
/// 免得协议名里真出现 "{error}" 之类的字面量时被连环替换。
struct Placeholder {
    const char *marker;
    QString value;
};

QString fill(const QString &text, std::initializer_list<Placeholder> args)
{
    QString out;
    out.reserve(text.size() + 16);
    for (qsizetype index = 0; index < text.size();) {
        bool matched = false;
        if (text.at(index) == QLatin1Char('{')) {
            for (const Placeholder &arg : args) {
                const QString marker = QLatin1String(arg.marker);
                if (!marker.isEmpty() && text.mid(index, marker.size()) == marker) {
                    out += arg.value;
                    index += marker.size();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            out += text.at(index);
            ++index;
        }
    }
    return out;
}

bool isRegularFile(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile();
}

/// 按分量拼路径（Rust 的 `PathBuf::join`），并统一成原生分隔符：
/// mod.rs 里那条测试就是在钉这个（正反斜杠混排看起来像 bug）。
QString joinComponents(const QString &base, const QStringList &components)
{
    QString out = QDir::toNativeSeparators(base);
    const QChar separator = QDir::separator();
    for (const QString &component : components) {
        if (!out.isEmpty() && !out.endsWith(separator) && !out.endsWith(QLatin1Char('/'))) {
            out += separator;
        }
        out += component;
    }
    return out;
}

QString currentExecutablePath()
{
#ifdef Q_OS_WIN
    WCHAR buffer[4096];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    if (length == 0 || length >= DWORD(std::size(buffer))) {
        return QString();
    }
    return QString::fromWCharArray(buffer, int(length));
#else
    // 对应 Rust 的 `std::env::current_exe().ok()`：拿不到就当 None，不去猜。
    return QCoreApplication::instance() ? QCoreApplication::applicationFilePath() : QString();
#endif
}

/// Rust 的 `{unsigned:08X}`。
QString hexUpper8(quint32 value)
{
    return QString::number(qint64(value), 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

/// Rust 的 `{:?}` of `Option<i32>`，日志字段用它。
QString renderOptionalCode(const std::optional<int> &code)
{
    return code.has_value() ? QStringLiteral("Some(%1)").arg(*code) : QStringLiteral("None");
}

/// 一次内核探测的总预算，对应 Rust 的 `Duration::from_secs(5)`。
constexpr qint64 kProbeBudgetMs = 5000;

int remainingMs(const QElapsedTimer &timer)
{
    const qint64 left = kProbeBudgetMs - timer.elapsed();
    return left > 0 ? int(left) : 0;
}

}

const EnvAssignments &godebugCompat()
{
    static const EnvAssignments assignments = {
        {QStringLiteral("GODEBUG"), QStringLiteral("cpu.all=off")},
    };
    return assignments;
}

QString managedKernelRoot()
{
    // Rust：current_dir().unwrap_or(".")，然后按分量 join("data").join("kernels")。
    const QString currentDir = QDir::currentPath();
    return joinComponents(currentDir.isEmpty() ? QStringLiteral(".") : currentDir,
                          {QStringLiteral("data"), QStringLiteral("kernels")});
}

QStringList kernelCandidates(Kind kind, const QString &currentExe, const QString &currentDir)
{
    QStringList candidates;
    const QStringList tail {QStringLiteral("data"), QStringLiteral("kernels"),
                           kindDirectoryName(kind), kindExecutableName(kind)};
    // 顺序即优先级：工作目录在前，可执行文件目录在后。
    if (!currentDir.isNull()) {
        candidates.push_back(joinComponents(currentDir, tail));
    }
    if (!currentExe.isNull()) {
        const QString directory = QFileInfo(currentExe).path();
        candidates.push_back(joinComponents(directory, tail));
    }
    return candidates;
}

std::optional<QString> findFirstExistingCandidate(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (isRegularFile(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

QString environmentOverride(Kind kind)
{
    // 对应 std::env::var_os：没设置返回 null QString（设了但为空也照样走 is_file 判断）。
    return qEnvironmentVariable(kindEnvironmentVariable(kind).toLatin1().constData());
}

std::optional<QString> findKernelExecutableWith(const QString &envOverride, const QString &currentExe,
                                                const QString &currentDir, Kind kind)
{
    if (!envOverride.isNull() && isRegularFile(envOverride)) {
        return envOverride;
    }
    return findFirstExistingCandidate(kernelCandidates(kind, currentExe, currentDir));
}

std::optional<QString> findKernelExecutable(Kind kind)
{
    return findKernelExecutableWith(environmentOverride(kind), currentExecutablePath(),
                                    QDir::currentPath(), kind);
}

std::optional<QString> findMihomoExecutable()
{
    return findKernelExecutable(Kind::Mihomo);
}

bool kernelIsAvailable(Kind kind)
{
    return findKernelExecutable(kind).has_value();
}

std::optional<QString> parseVersionOutput(const QString &output)
{
    // Rust：lines() 先按 '\n' 切（顺带去掉行尾 '\r'），每行 trim，取第一行非空。
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return std::nullopt;
}

Result<QString> classifyVersionProbe(bool exitOk, const std::optional<int> &code, const QString &output)
{
    const std::optional<QString> line = parseVersionOutput(output);
    if (line.has_value()) {
        return Result<QString>::withValue(*line);
    }
    if (exitOk) {
        return Result<QString>::withError(Messages::probeSucceededWithoutOutput());
    }
    const int raw = code.value_or(-1);
    const quint32 unsignedCode = quint32(raw);
    QString known;
    switch (unsignedCode) {
    case 0xC000001D:
        known = Messages::probeIllegalInstruction();
        break;
    case 0xC0000005:
        known = Messages::probeAccessViolation();
        break;
    case 0xC00000FD:
        known = Messages::probeStackOverflow();
        break;
    default:
        break;
    }
    const QString tail =
        known.isEmpty() ? QString() : fill(Messages::probeKnownTail(), {{"{known}", known}});
    return Result<QString>::withError(fill(Messages::probeCannotExecute(),
                                           {{"{raw}", QString::number(raw)},
                                            {"{unsigned:08X}", hexUpper8(unsignedCode)},
                                            {"{tail}", tail}}));
}

Result<QString> readKernelVersionWithEnv(Kind kind, const QString &path, const EnvAssignments &env)
{
    QProcess process;
#ifdef Q_OS_WIN
    // 对应 Rust 的 `creation_flags(0x08000000)`：探测在启动路径上，不许弹控制台窗口。
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *createArguments) {
        createArguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    if (!env.isEmpty()) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QPair<QString, QString> &assignment : env) {
            environment.insert(assignment.first, assignment.second);
        }
        process.setProcessEnvironment(environment);
    }
    process.setProgram(path);
    process.setArguments(kindVersionArgs(kind));
    process.start();

    QElapsedTimer timer;
    timer.start();
    if (remainingMs(timer) == 0 || !process.waitForStarted(remainingMs(timer))) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
        return Result<QString>::withError(
            fill(Messages::executeKernelFailed(), {{"{}", path}}));
    }
    // 必须带超时：内核挂住就会让整条探测链（以及依赖它的更新检查）永远不返回。
    if (!process.waitForFinished(remainingMs(timer))) {
        process.kill();
        process.waitForFinished(1000);
        return Result<QString>::withError(
            fill(Messages::readVersionTimeout(), {{"{}", path}}));
    }

    const QString text = QString::fromUtf8(process.readAllStandardOutput())
        + QString::fromUtf8(process.readAllStandardError());
    const bool finished = process.exitStatus() == QProcess::NormalExit;
    const bool exitOk = finished && process.exitCode() == 0;
    const std::optional<int> code = finished ? std::optional<int>(process.exitCode()) : std::nullopt;

    const Result<QString> outcome = classifyVersionProbe(exitOk, code, text);
    if (outcome.isErr()) {
        // 探测失败是"预备状态"最常见的外因落点，日志里要留退出码和原始输出长度。
        Log::warn(QStringLiteral("kernel=%1 path=%2 exit_ok=%3 code=%4 bytes=%5 %6")
                      .arg(kindDisplayName(kind),
                           path,
                           exitOk ? QStringLiteral("true") : QStringLiteral("false"),
                           renderOptionalCode(code))
                      .arg(text.toUtf8().size())
                      .arg(outcome.error()));
    }
    return outcome;
}

Result<QString> readKernelVersion(Kind kind, const QString &path)
{
    return readKernelVersionWithEnv(kind, path, EnvAssignments());
}

Result<Kind> selectProxyKernelWithAvailability(const QString &protocol, const Availability &available)
{
    const std::optional<Kind> preferred = preferredProxyKernel(protocol);
    if (!preferred.has_value()) {
        return Result<Kind>::withError(
            fill(Messages::unsupportedProtocol(), {{"{protocol}", protocol}}));
    }
    const Kind fallback = *preferred == Kind::Mihomo
        ? Kind::SingBox
        : (*preferred == Kind::SingBox ? Kind::Mihomo : Kind::Xray);
    const Kind order[3] = {*preferred, fallback, Kind::Xray};
    for (const Kind candidate : order) {
        if (kindSupportsProxyProtocol(candidate, protocol) && available(candidate)) {
            return Result<Kind>::withValue(candidate);
        }
    }
    return Result<Kind>::withError(
        fill(Messages::noInstalledKernelSupporting(), {{"{protocol}", protocol}}));
}

Result<Kind> selectProxyKernel(const QString &protocol)
{
    return selectProxyKernelWithAvailability(protocol, kernelIsAvailable);
}

Result<Kind> selectProxyKernelPreferenceWithAvailability(const QString &protocol,
                                                         Preference preference,
                                                         bool systemModeAllowed,
                                                         const Availability &available)
{
    const std::optional<Kind> named = preferenceKernel(preference);
    if (named.has_value()) {
        const Kind kind = *named;
        if (kind == Kind::Xray && !systemModeAllowed) {
            return Result<Kind>::withError(Messages::xrayNoTunMode());
        }
        if (!kindSupportsProxyProtocol(kind, protocol)) {
            return Result<Kind>::withError(fill(Messages::kernelDoesNotSupportProtocol(),
                                                {{"{}", kindDisplayName(kind)},
                                                 {"{protocol}", protocol}}));
        }
        if (!available(kind)) {
            return Result<Kind>::withError(
                fill(Messages::kernelExecutableNotFound(), {{"{}", kindDisplayName(kind)}}));
        }
        return Result<Kind>::withValue(kind);
    }

    const Result<Kind> selected = selectProxyKernelWithAvailability(protocol, available);
    if (selected.isOk()) {
        return selected;
    }
    if (systemModeAllowed && kindSupportsProxyProtocol(Kind::Xray, protocol)
        && available(Kind::Xray)) {
        return Result<Kind>::withValue(Kind::Xray);
    }
    return Result<Kind>::withError(
        fill(Messages::noInstalledKernelSupporting(), {{"{protocol}", protocol}}));
}

Result<Kind> selectProxyKernelPreference(const QString &protocol, Preference preference,
                                        bool systemModeAllowed)
{
    return selectProxyKernelPreferenceWithAvailability(protocol, preference, systemModeAllowed,
                                                      kernelIsAvailable);
}

std::optional<Kind> missingKernelFor(const QString &protocol, Preference preference, bool systemMode,
                                     const Availability &available)
{
    const std::optional<Kind> named = preferenceKernel(preference);
    if (named.has_value()) {
        const Kind kind = *named;
        // Xray 在 TUN 下是"不支持"，不是"没装"，别拿下载框去挡。
        if (kind == Kind::Xray && !systemMode) {
            return std::nullopt;
        }
        return available(kind) ? std::nullopt : std::optional<Kind>(kind);
    }
    if (selectProxyKernelWithAvailability(protocol, available).isOk()) {
        return std::nullopt;
    }
    const std::optional<Kind> preferred = preferredProxyKernel(protocol);
    if (preferred.has_value() && kindSupportsProxyProtocol(*preferred, protocol)) {
        return preferred;
    }
    return std::nullopt;
}

Json Info::toJson() const
{
    Json value;
    value["kind"] = kindToText(kind).toStdString();
    value["path"] = JsonUtil::optText(path);
    value["version"] = JsonUtil::optText(version);
    value["available"] = available;
    value["message"] = message.toStdString();
    return value;
}

Info Info::fromJson(const Json &value)
{
    Info info;
    info.kind = kindFromText(JsonUtil::textOrEmpty(value, "kind"));
    info.path = JsonUtil::nullableText(value, "path");
    info.version = JsonUtil::nullableText(value, "version");
    info.available = JsonUtil::boolOr(value, "available", false);
    info.message = JsonUtil::textOrEmpty(value, "message");
    return info;
}

Info Manager::infoForMissing(Kind kind) const
{
    Info info;
    info.kind = kind;
    info.available = false;
    info.message =
        fill(Messages::kernelNotFoundMessage(), {{"{}", kindDisplayName(kind)}});
    return info;
}

Info Manager::detect(Kind kind) const
{
    return detectAtPath(kind, findKernelExecutable(kind));
}

Info Manager::detectAtPath(Kind kind, const std::optional<QString> &path) const
{
    if (!path.has_value()) {
        return infoForMissing(kind);
    }

    const Result<QString> version = readKernelVersion(kind, *path);
    Info info;
    info.kind = kind;
    info.path = *path;
    if (version.isOk()) {
        info.available = true;
        info.version = version.value();
        info.message = version.value().isNull()
            ? fill(Messages::availableWithoutVersion(), {{"{}", kindDisplayName(kind)}})
            : version.value();
    } else {
        // 跑不起来就不能叫"可用"：内核文件在不在盘上和它能不能运行是两件事。
        info.available = false;
        info.message = fill(Messages::installedButCannotRun(),
                            {{"{}", kindDisplayName(kind)}, {"{error}", version.error()}});
    }
    return info;
}

QList<Info> Manager::detectAll() const
{
    // Rust 用 tokio::join! 并发探测三个内核，结果顺序仍是 mihomo → xray → sing-box。
    return {detect(Kind::Mihomo), detect(Kind::Xray), detect(Kind::SingBox)};
}

ReleaseInfo releaseInfoFromCheck(const Result<ReleaseInfo> &checked, const Info &info)
{
    if (checked.isOk()) {
        return checked.value();
    }
    ReleaseInfo fallback;
    fallback.kind = info.kind;
    fallback.currentVersion = info.version;
    fallback.updateAvailable = false;
    fallback.message = fill(Messages::checkUpdateFailed(), {{"{err}", checked.error()}});
    return fallback;
}

}
