#include "proxy_probe/ProbeKernel.h"

#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QMutex>
#include <QProcess>
#include <QStringConverter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <algorithm>

#include "core/Ids.h"
#include "net/HttpClient.h"
#include "platform/ProcessControl.h"
#include "proxy_probe/ProbeMessages.h"

#ifdef Q_OS_WIN
/// CREATE_NO_WINDOW（winbase.h 里的 0x0800_0000）。刻意不 include windows.h：
/// 它带来的 min/max 宏会把本文件里所有 std::max/std::min 变成语法错误。
constexpr unsigned long kCreateNoWindow = 0x08000000UL;
#endif

namespace ProxyProbe {
namespace {

/// Rust 的 `TcpStream::connect` 没有显式超时（走 OS 默认），这里给一个足够接近
/// "连不上就快点失败"的上限，避免 20ms 的轮询被一次挂死的 connect 拖住。
constexpr int kPortConnectProbeMs = 200;

QByteArray utf8Bytes(const QString &text)
{
    return text.toUtf8();
}

char hexUpper(unsigned char value)
{
    return value < 10 ? char('0' + value) : char('A' + value - 10);
}

bool isAlphanumeric(unsigned char byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9');
}

void appendPercentEncoded(QByteArray *out, unsigned char byte)
{
    out->append('%');
    out->append(hexUpper(byte >> 4));
    out->append(hexUpper(byte & 0x0f));
}

}

int clampDelayTimeoutMs(int timeoutMs)
{
    return std::clamp(timeoutMs, 1000, 15000);
}

int controllerStartupBudgetMs(qsizetype proxyCount)
{
    const qint64 scaled = 8000 + proxyCount * 5;
    return static_cast<int>(std::min<qint64>(scaled, 60000));
}

PortReservations::PortReservations() = default;

PortReservations::~PortReservations()
{
    release();
}

Result<quint16> PortReservations::reserve()
{
    auto server = std::make_unique<QTcpServer>();
    if (!server->listen(QHostAddress::LocalHost, 0)) {
        return Result<quint16>::withError(QStringLiteral("bind 127.0.0.1:0 failed: %1")
                                              .arg(server->errorString()));
    }
    const quint16 port = server->serverPort();
    ports_.append(port);
    listeners_.push_back(std::move(server));
    return Result<quint16>::withValue(port);
}

Status PortReservations::reserveMany(qsizetype count, QVector<quint16> *ports)
{
    QVector<quint16> allocated;
    allocated.reserve(static_cast<qsizetype>(count));
    for (qsizetype index = 0; index < count; ++index) {
        const Result<quint16> port = reserve();
        if (port.isErr()) {
            return Status::failed(port.error());
        }
        allocated.append(port.value());
    }
    if (ports != nullptr) {
        *ports = allocated;
    }
    return Status::succeeded();
}

void PortReservations::release()
{
    for (const auto &server : listeners_) {
        if (server && server->isListening()) {
            server->close();
        }
    }
    listeners_.clear();
}

Result<TempWorkDir> TempWorkDir::create(const QString &prefix)
{
    const QString root = QDir(QDir::tempPath())
                             .filePath(QStringLiteral("%1-%2").arg(prefix, Ids::newUuidV4()));
    if (!QDir().mkpath(root)) {
        return Result<TempWorkDir>::withError(
            QStringLiteral("create_dir_all failed: %1").arg(root));
    }
    return Result<TempWorkDir>::withValue(TempWorkDir(root));
}

TempWorkDir::~TempWorkDir()
{
    if (!root_.isEmpty()) {
        QDir(root_).removeRecursively();
    }
}

TempWorkDir::TempWorkDir(TempWorkDir &&other) noexcept : root_(std::move(other.root_))
{
    other.root_.clear();
}

TempWorkDir &TempWorkDir::operator=(TempWorkDir &&other) noexcept
{
    if (this != &other) {
        if (!root_.isEmpty()) {
            QDir(root_).removeRecursively();
        }
        root_ = std::move(other.root_);
        other.root_.clear();
    }
    return *this;
}

QString TempWorkDir::filePath(const QString &name) const
{
    return QDir(root_).filePath(name);
}

Status TempWorkDir::remove()
{
    if (root_.isEmpty()) {
        return Status::succeeded();
    }
    QDir dir(root_);
    const bool existed = dir.exists();
    const bool removed = !existed || dir.removeRecursively();
    root_.clear();
    if (!removed) {
        return Status::failed(QStringLiteral("remove_dir_all failed"));
    }
    return Status::succeeded();
}

Status writeTextFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Status::failed(QStringLiteral("write %1 failed: %2").arg(path, file.errorString()));
    }
    const QByteArray payload = text.toUtf8();
    if (file.write(payload) != payload.size()) {
        file.close();
        return Status::failed(QStringLiteral("write %1 failed: %2").arg(path, file.errorString()));
    }
    file.close();
    return Status::succeeded();
}

QString readLogFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    // 内核日志可能是 GBK/latin1 混排的 Windows 输出：按 UTF-8 解，解不出来的字节走
    // replacement，和 Rust String::from_utf8_lossy 一致。
    return QString::fromUtf8(file.readAll());
}

QString takeCodePoints(const QString &text, qsizetype count)
{
    if (text.isEmpty() || count <= 0) {
        return QString();
    }
    qsizetype index = 0;
    qsizetype taken = 0;
    while (index < text.size() && taken < count) {
        if (QChar::isHighSurrogate(text.at(index).unicode()) && index + 1 < text.size()
            && QChar::isLowSurrogate(text.at(index + 1).unicode())) {
            index += 2;
        } else {
            ++index;
        }
        ++taken;
    }
    return text.left(index);
}

QString compactDiagnostic(const QString &text)
{
    // Rust: diagnostic.lines().map(str::trim).filter(!empty).last().unwrap_or("未知配置错误")
    const QStringList lines = text.split(QChar('\n'));
    for (qsizetype index = lines.size() - 1; index >= 0; --index) {
        QString line = lines.at(index).trimmed();
        if (line.endsWith(QChar('\r'))) {
            line.chop(1);
        }
        if (!line.isEmpty()) {
            return takeCodePoints(line, 240);
        }
    }
    return takeCodePoints(Messages::mihomoUnknownConfigError(), 240);
}

QString percentEncodePath(const QString &text)
{
    // NON_ALPHANUMERIC：连 '-' 与 '.' 都要编掉，所以不能用 QUrl::toPercentEncoding。
    const QByteArray bytes = text.toUtf8();
    QByteArray out;
    out.reserve(bytes.size() * 3);
    for (const char raw : bytes) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (isAlphanumeric(byte)) {
            out.append(char(byte));
        } else {
            appendPercentEncoded(&out, byte);
        }
    }
    return QString::fromLatin1(out);
}

QString formEncodeQuery(const QString &text)
{
    // form_urlencoded：unreserved 加 *-._ 直出，空格写 '+'，其余 %XX。
    static const QByteArray kept = QByteArrayLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789*-._");
    const QByteArray bytes = text.toUtf8();
    QByteArray out;
    out.reserve(bytes.size() * 3);
    for (const char raw : bytes) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (kept.contains(char(byte))) {
            out.append(char(byte));
        } else if (byte == ' ') {
            out.append('+');
        } else {
            appendPercentEncoded(&out, byte);
        }
    }
    return QString::fromLatin1(out);
}

Result<std::unique_ptr<QProcess>> startKernelProcess(const QString &program,
                                                     const QStringList &arguments,
                                                     const QString &logPath)
{
    auto process = std::make_unique<QProcess>();
#ifdef Q_OS_WIN
    // Rust 侧的 creation_flags(0x0800_0000) = CREATE_NO_WINDOW：测速是后台动作，不能闪窗。
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *createArguments) {
        createArguments->flags |= kCreateNoWindow;
    });
#endif
    // stdout 与 stderr 都进同一个日志文件（Rust 用 try_clone 共享同一个文件偏移）：
    // 分开收会把启动失败的那半句丢掉，诊断就退化成"未知配置错误"。
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(logPath);
    process->setProgram(program);
    process->setArguments(arguments);
    process->start();
    if (!process->waitForStarted(10000)) {
        const QString message = process->errorString();
        return Result<std::unique_ptr<QProcess>>::withError(message.isEmpty()
                                                                ? QStringLiteral("spawn failed")
                                                                : message);
    }
    // 只有这个新建的 PID 会被登记进本程序的作业对象；机器上已在跑的 Rust 实例不碰。
    Platform::assignChildProcessId(static_cast<quint32>(process->processId()));
    return Result<std::unique_ptr<QProcess>>::withValue(std::move(process));
}

ProcessOutput runProcessToOutput(const QString &program, const QStringList &arguments,
                                 const QString &workingDirectory, int budgetMs)
{
    ProcessOutput output;
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *createArguments) {
        createArguments->flags |= kCreateNoWindow;
    });
#endif
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) {
        output.error = process.errorString();
        return output;
    }
    output.started = true;
    Platform::assignChildProcessId(static_cast<quint32>(process.processId()));
    if (!process.waitForFinished(budgetMs)) {
        // 超时：只杀我们自己刚起的这个进程。
        process.kill();
        process.waitForFinished(1000);
        output.error = QStringLiteral("timed out after %1 ms").arg(budgetMs);
        return output;
    }
    output.finished = process.exitStatus() == QProcess::NormalExit;
    output.exitCode = process.exitCode();
    output.ok = output.finished && output.exitCode == 0;
    output.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    output.stdErr = QString::fromUtf8(process.readAllStandardError());
    return output;
}

bool processExited(const QProcess *process, QString *exitText)
{
    if (process == nullptr) {
        return false;
    }
    if (process->state() == QProcess::Running) {
        return false;
    }
    if (exitText != nullptr) {
        if (process->exitStatus() == QProcess::NormalExit) {
            *exitText = QStringLiteral("exit status: %1").arg(process->exitCode());
        } else {
            *exitText = QStringLiteral("exit code: 0x%1").arg(quint32(process->exitCode()), 8, 16,
                                                               QLatin1Char('0'));
        }
    }
    return true;
}

bool processRunning(const QProcess *process)
{
    return process != nullptr && process->state() == QProcess::Running;
}

Status killProcess(QProcess *process)
{
    if (process == nullptr) {
        return Status::succeeded();
    }
    if (process->state() == QProcess::Running) {
        process->kill();
    }
    if (!process->waitForFinished(3000)) {
        return Status::failed(QStringLiteral("wait for exit failed"));
    }
    return Status::succeeded();
}

bool portAcceptsConnection(quint16 port, int connectTimeoutMs)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(connectTimeoutMs);
}

bool waitUntilPortReady(QProcess &process, quint16 port, int budgetMs, int pollMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < budgetMs) {
        QString exitText;
        if (processExited(&process, &exitText)) {
            return false;
        }
        if (portAcceptsConnection(port, kPortConnectProbeMs)) {
            return true;
        }
        QThread::msleep(static_cast<unsigned long>(std::max(pollMs, 1)));
    }
    return false;
}

void emitProgress(const ProgressFn &progress, qsizetype done, qsizetype ok, qsizetype total,
                  qint64 elapsedMs, std::optional<qsizetype> batchIndex,
                  std::optional<qsizetype> batchTotal)
{
    if (!progress) {
        return;
    }
    ProbeProgress state;
    state.done = size_t(done);
    state.total = size_t(total);
    state.ok = size_t(ok);
    state.fail = size_t(std::max<qsizetype>(0, done - ok));
    state.elapsedMs = elapsedMs;
    if (batchIndex.has_value()) {
        state.batchIndex = size_t(*batchIndex);
    }
    if (batchTotal.has_value()) {
        state.batchTotal = size_t(*batchTotal);
    }
    progress(state);
}

void reportProgress(const ProgressFn &progress,
                    const QVector<std::optional<ProbeResult>> &results, qsizetype total,
                    qint64 elapsedMs)
{
    if (!progress) {
        return;
    }
    qsizetype done = 0;
    qsizetype ok = 0;
    for (const auto &slot : results) {
        if (!slot.has_value()) {
            continue;
        }
        ++done;
        if (slot->ok) {
            ++ok;
        }
    }
    emitProgress(progress, done, ok, total, elapsedMs);
}

QVector<QVector<Models::Node>> chunkNodes(const QVector<Models::Node> &nodes, qsizetype batchSize)
{
    const qsizetype width = std::max<qsizetype>(1, batchSize);
    QVector<QVector<Models::Node>> chunks;
    for (qsizetype start = 0; start < nodes.size(); start += width) {
        chunks.append(nodes.mid(start, width));
    }
    return chunks;
}

ConnectivityCheck checkConnectivityThroughLocalProxy(quint16 port, const QString &targetUrl,
                                                     int timeoutMs)
{
    Net::Request request;
    request.url = targetUrl;
    request.method = QByteArrayLiteral("GET");
    request.timeoutMs = timeoutMs;
    request.connectTimeoutMs = timeoutMs;
    request.proxyUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    // reqwest 那侧没设 UA；ProxyManager 用的 vpn-parser/1.0 是常驻代理的路径，
    // 测速路径保持与 Rust 一致。
    ConnectivityCheck check;
    const Result<Net::Response> response = Net::send(request);
    if (response.isErr()) {
        check.error = response.error();
        return check;
    }
    // Rust 判的是 `response.status() == StatusCode::NO_CONTENT`：204 之外一律算不通。
    if (response.value().status != 204) {
        check.error = QStringLiteral("HTTP %1").arg(response.value().status);
        return check;
    }
    check.ok = true;
    return check;
}

qsizetype countedResults(const QVector<std::optional<ProbeResult>> &results)
{
    qsizetype count = 0;
    for (const auto &slot : results) {
        if (slot.has_value()) {
            ++count;
        }
    }
    return count;
}

qsizetype countPassed(const QVector<ProbeResult> &results)
{
    qsizetype ok = 0;
    for (const ProbeResult &result : results) {
        if (result.ok) {
            ++ok;
        }
    }
    return ok;
}

qint64 probeLatencyMs(qint64 elapsedMs)
{
    // Rust: `started.elapsed().as_millis().max(1)`：0 ms 会被读成"没测到"。
    return std::max<qint64>(1, elapsedMs);
}

}
