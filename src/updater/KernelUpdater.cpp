#include "updater/KernelUpdater.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <chrono>

#include "core/Crypto.h"
#include "core/Log.h"
#include "kernel/KernelMessages.h"
#include "net/HttpClient.h"
#include "sysproxy/SystemProxy.h"
#include "updater/KernelArchive.h"
#include "updater/UpdaterFormat.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
// windows.h 的 max/min 宏会把 std::max(...) 拆成语法垃圾，这里只用 std 的那套。
#define NOMINMAX
#include <windows.h>
#endif

namespace Updater {

using Kernel::Messages::compatModeSuffix;
using Kernel::Messages::directTransport;
using Kernel::UpdateStage;

const QString kUserAgent = QStringLiteral("vcc/0.1 kernel-updater");

namespace {

/// Rust 的 `PathBuf::join` + 原生分隔符（和 kernel/KernelManager.cpp 里那套一致，
/// 这样 startup.log 里出现的路径与 Rust 版逐字相同）。
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

bool isRegularFile(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile();
}

/// Rust `failures.join("；")`。
QString joinFailures(const QStringList &parts)
{
    QString out;
    for (qsizetype index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            out += Kernel::Messages::failureJoiner();
        }
        out += parts.at(index);
    }
    return out;
}

/// 具名占位符写起来太长，这里给两个常用的一眼能认出来的小助手。
std::pair<QString, QString> named(const char *marker, const QString &value)
{
    return {QString::fromLatin1(marker), value};
}

void report(const ProgressCallback &progress,
            Kernel::Kind kind,
            UpdateStage stage,
            quint64 downloaded,
            quint64 total,
            double bytesPerSecond,
            const QString &message)
{
    if (!progress) {
        return;
    }
    Kernel::UpdateProgress updateProgress;
    updateProgress.kind = kind;
    updateProgress.stage = stage;
    updateProgress.downloaded = downloaded;
    updateProgress.total = total;
    updateProgress.bytesPerSecond = bytesPerSecond;
    updateProgress.message = message;
    progress(updateProgress);
}

/// 对应 Rust 的 `tokio::fs::rename`：Windows 上是带 REPLACE_EXISTING 的 MoveFileEx，
/// 目标已存在也要能换掉（QFile::rename 不换，会让"替换正式内核"这一步莫名失败）。
/// 走宽字符 API，中文用户名/路径才不会先被 ANSI 化一遍。
Status renameReplace(const QString &source, const QString &target)
{
#ifdef Q_OS_WIN
    if (MoveFileExW(reinterpret_cast<const wchar_t *>(source.utf16()),
                    reinterpret_cast<const wchar_t *>(target.utf16()),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Status::succeeded();
    }
    return Status::failed(QStringLiteral("MoveFileExW failed (error %1): %2 -> %3")
                              .arg(GetLastError())
                              .arg(source, target));
#else
    if (QFile::rename(source, target)) {
        return Status::succeeded();
    }
    QFile::remove(target);
    if (QFile::rename(source, target)) {
        return Status::succeeded();
    }
    return Status::failed(QStringLiteral("rename failed: %1 -> %2").arg(source, target));
#endif
}

/// 把 OS 级失败细节留在日志里：返回给上层的文本必须与 Rust 的 anyhow 顶层消息一致，
/// 所以真正的 errno/Win32 code 只能进 startup.log。
void logIoDetail(const QString &stage, const Status &status)
{
    Log::startup(QStringLiteral("[kernel-io] %1: %2").arg(stage, status.error()));
}

QByteArray readFileFully(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    *ok = true;
    return bytes;
}

/// 对应 Rust `fetch_json` 里那次 GET 的公共部分。
Net::Request makeGetRequest(const Transport &transport, const QString &url, int timeoutMs)
{
    Net::Request request;
    request.url = url;
    request.method = QByteArrayLiteral("GET");
    request.userAgent = kUserAgent;
    request.timeoutMs = timeoutMs;
    request.connectTimeoutMs = kConnectTimeoutMs;
    request.proxyUrl = transport.proxyUrl;
    return request;
}

}

// ---------------------------------------------------------------------------
// Release / Asset 的 JSON 形态（serde 字段名逐字一致：本地缓存要和 Rust 写的缓存互通）
// ---------------------------------------------------------------------------

Json Asset::toJson() const
{
    Json value;
    value["name"] = name.toStdString();
    value["browser_download_url"] = browserDownloadUrl.toStdString();
    value["size"] = size;
    value["digest"] = JsonUtil::optText(digest);
    return value;
}

std::optional<Asset> Asset::fromJson(const Json &value)
{
    const std::optional<QString> nameValue = JsonUtil::optTextValue(value, "name");
    const std::optional<QString> urlValue =
        JsonUtil::optTextValue(value, "browser_download_url");
    const std::optional<qint64> sizeValue = JsonUtil::optInt(value, "size");
    if (!nameValue.has_value() || !urlValue.has_value() || !sizeValue.has_value()
        || *sizeValue < 0) {
        return std::nullopt;
    }
    Asset asset;
    asset.name = *nameValue;
    asset.browserDownloadUrl = *urlValue;
    asset.size = quint64(*sizeValue);
    // digest 是 Option<String>：缺键/null 都算 None，非字符串就当解析失败。
    if (value.is_object()) {
        const auto found = value.find("digest");
        if (found != value.end() && !found->is_null() && !found->is_string()) {
            return std::nullopt;
        }
    }
    asset.digest = JsonUtil::nullableText(value, "digest");
    return asset;
}

Json Release::toJson() const
{
    Json value;
    value["tag_name"] = tagName.toStdString();
    Json assetsJson = Json::array();
    for (const Asset &asset : assets) {
        assetsJson.push_back(asset.toJson());
    }
    value["assets"] = assetsJson;
    value["fallback"] = fallback;
    return value;
}

std::optional<Release> Release::fromJson(const Json &value)
{
    const std::optional<QString> tag = JsonUtil::optTextValue(value, "tag_name");
    if (!tag.has_value() || !value.is_object() || !value.contains("assets")
        || !value["assets"].is_array()) {
        return std::nullopt;
    }
    Release release;
    release.tagName = *tag;
    // #[serde(default)]
    release.fallback = JsonUtil::boolOr(value, "fallback", false);
    for (const Json &entry : value["assets"]) {
        const std::optional<Asset> asset = Asset::fromJson(entry);
        if (!asset.has_value()) {
            // serde 对 Vec<GithubAsset> 是"有一条坏的就整个失败"，缓存/响应都按这个来。
            return std::nullopt;
        }
        release.assets.push_back(*asset);
    }
    return release;
}

// ---------------------------------------------------------------------------
// 模块级纯函数（对应 Rust updater.rs 里那几个不挂在 KernelUpdater 上的）
// ---------------------------------------------------------------------------

QVector<std::optional<QString>> proxyCandidates(const QString &appProxy, const QString &systemProxy)
{
    QVector<std::optional<QString>> chain;
    if (!appProxy.isNull()) {
        const QString trimmed = appProxy.trimmed();
        if (!trimmed.isEmpty()) {
            chain.push_back(std::optional<QString>(trimmed));
        }
    }
    if (!systemProxy.isNull()) {
        const std::optional<QString> entry(systemProxy);
        if (!chain.contains(entry)) {
            chain.push_back(entry);
        }
    }
    if (!chain.contains(std::optional<QString>())) {
        chain.push_back(std::optional<QString>());
    }
    return chain;
}

QVector<std::optional<QString>> proxyCandidatesWithSystemProxy(const QString &appProxy)
{
    return proxyCandidates(appProxy, SysProxy::currentSystemProxyUrl());
}

Result<QString> requireSha256(const Asset &asset)
{
    if (asset.digest.isNull()) {
        return Result<QString>::withError(Kernel::Messages::assetDigestMissing());
    }
    const QString prefix = QStringLiteral("sha256:");
    if (!asset.digest.startsWith(prefix)) {
        return Result<QString>::withError(Kernel::Messages::assetDigestInvalid());
    }
    const QString value = asset.digest.mid(prefix.size());
    if (value.size() != 64) {
        return Result<QString>::withError(Kernel::Messages::assetDigestInvalid());
    }
    for (const QChar &character : value) {
        if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
              || (character >= QLatin1Char('a') && character <= QLatin1Char('f'))
              || (character >= QLatin1Char('A') && character <= QLatin1Char('F')))) {
            return Result<QString>::withError(Kernel::Messages::assetDigestInvalid());
        }
    }
    return Result<QString>::withValue(value);
}

Status verifySha256(const QByteArray &bytes, const QString &expectedHex)
{
    const QString actual = Crypto::sha256Hex(bytes);
    if (actual.compare(expectedHex, Qt::CaseInsensitive) != 0) {
        return Status::failed(Kernel::Messages::sha256Mismatch());
    }
    return Status::succeeded();
}

QString stagingName(Kernel::Kind kind)
{
    // Rust：`format!("{}.staging.exe", executable_name().trim_end_matches(".exe"))`
    // —— trim_end_matches 会把结尾所有 ".exe" 都剥掉。
    QString stem = Kernel::kindExecutableName(kind);
    while (stem.endsWith(QLatin1String(".exe"))) {
        stem.chop(4);
    }
    return stem + QStringLiteral(".staging.exe");
}

Status ensureNotCancelled(const CancelToken &cancelled)
{
    if (cancelled.cancelled()) {
        return Status::failed(Kernel::Messages::kernelDownloadCancelled());
    }
    return Status::succeeded();
}

std::optional<Release> fallbackRelease(Kernel::Kind kind)
{
    // 内置兜底 Release：只在"查不到最新 Release + 本地也没有缓存"时用。
    const QString mihomoTag = QStringLiteral("v1.19.32");
    const QString mihomoSha256 =
        QStringLiteral("sha256:1ac84e795b5b915446e20139677fb6cc013a0778e876e8d4c96f994750b4c4a3");
    constexpr quint64 mihomoSize = 22492402ULL;
    if (kind != Kernel::Kind::Mihomo) {
        return std::nullopt;
    }
    const QString name = Kernel::kindAssetName(kind, mihomoTag);
    Asset asset;
    asset.name = name;
    asset.browserDownloadUrl =
        QStringLiteral("https://github.com/MetaCubeX/mihomo/releases/download/%1/%2")
            .arg(mihomoTag, name);
    asset.size = mihomoSize;
    asset.digest = mihomoSha256;
    Release release;
    release.tagName = mihomoTag;
    release.assets.push_back(asset);
    release.fallback = true;
    return release;
}

// ---------------------------------------------------------------------------
// KernelUpdater
// ---------------------------------------------------------------------------

KernelUpdater KernelUpdater::create(const QString &root, const QString &appProxy)
{
    return createWithCandidates(root, appProxy, proxyCandidatesWithSystemProxy(appProxy));
}

KernelUpdater KernelUpdater::createWithCandidates(const QString &root,
                                                 const QString &appProxy,
                                                 const QVector<std::optional<QString>> &candidates)
{
    QVector<Transport> transports;
    for (const std::optional<QString> &candidate : candidates) {
        Transport transport;
        if (candidate.has_value()) {
            const QString url = *candidate;
            transport.proxyUrl = url;
            // Rust：`app_proxy.is_some() && Some(url) == app_proxy` —— 比的是**没 trim 的**原值，
            // 所以带空格的配置会被标成"系统代理"，这里照样原样比。
            transport.label = !appProxy.isNull() && url == appProxy
                ? fillTemplate(Kernel::Messages::transportAppProxyLabel(), {named("{url}", url)})
                : fillTemplate(Kernel::Messages::transportSystemProxyLabel(), {named("{url}", url)});
        } else {
            transport.label = directTransport();
        }
        transports.push_back(transport);
    }
    return KernelUpdater(root, transports);
}

Result<Asset> KernelUpdater::selectAsset(const Release &release, const QString &assetName)
{
    for (const Asset &asset : release.assets) {
        if (asset.name == assetName) {
            return Result<Asset>::withValue(asset);
        }
    }
    return Result<Asset>::withError(
        fillTemplate(Kernel::Messages::releaseAssetMissing(), {named("{asset_name}", assetName)}));
}

Result<Kernel::ReleaseInfo> KernelUpdater::releaseInfoFromRelease(const Release &release,
                                                                  const Kernel::Info &info)
{
    const QString assetName = Kernel::kindAssetName(info.kind, release.tagName);
    const Result<Asset> selected = selectAsset(release, assetName);
    if (selected.isErr()) {
        return Result<Kernel::ReleaseInfo>::withError(selected.error());
    }
    // check() 只要求"有能核对的 SHA-256"，摘要本身用不上（下载时再校验一次）。
    const Result<QString> digest = requireSha256(selected.value());
    if (digest.isErr()) {
        return Result<Kernel::ReleaseInfo>::withError(digest.error());
    }

    Kernel::ReleaseInfo releaseInfo;
    releaseInfo.kind = info.kind;
    releaseInfo.currentVersion = info.version;
    releaseInfo.latestVersion = release.tagName;
    releaseInfo.updateAvailable =
        info.version.isNull() ? true : !Kernel::versionMatches(info.version, release.tagName);
    releaseInfo.assetName = selected.value().name;
    releaseInfo.message = releaseInfo.updateAvailable
        ? fillTemplate(Kernel::Messages::releaseUpdateAvailable(),
                       {},
                       {Kernel::kindDisplayName(info.kind), release.tagName})
        : fillTemplate(Kernel::Messages::releaseUpToDate(), {}, {Kernel::kindDisplayName(info.kind)});
    return Result<Kernel::ReleaseInfo>::withValue(releaseInfo);
}

Result<Kernel::ReleaseInfo> KernelUpdater::check(const Kernel::Info &info) const
{
    const Result<Release> release = fetchRelease(info.kind);
    if (release.isErr()) {
        return Result<Kernel::ReleaseInfo>::withError(release.error());
    }
    return releaseInfoFromRelease(release.value(), info);
}

Result<Kernel::UpdateResult> KernelUpdater::update(Kernel::Kind kind,
                                                   Kernel::Source source,
                                                   const QString &localFile,
                                                   const CancelToken &cancelled,
                                                   const ProgressCallback &progress) const
{
    if (source == Kernel::Source::Local) {
        return installLocal(kind, localFile, cancelled, progress);
    }
    return updateFromNetwork(kind, source, cancelled, progress);
}

Result<Kernel::UpdateResult> KernelUpdater::installLocal(Kernel::Kind kind,
                                                         const QString &localFile,
                                                         const CancelToken &cancelled,
                                                         const ProgressCallback &progress) const
{
    if (localFile.isNull()) {
        return Result<Kernel::UpdateResult>::withError(Kernel::Messages::localZipRequired());
    }
    const Status notCancelled = ensureNotCancelled(cancelled);
    if (notCancelled.isErr()) {
        return Result<Kernel::UpdateResult>::withError(notCancelled.error());
    }
    report(progress, kind,
           UpdateStage::Checking,
           0,
           0,
           0.0,
           fillTemplate(Kernel::Messages::localFileReading(), {}, {localFile}));

    // Rust 是 `tokio::fs::read(path)` 整包读进内存再看 len()；超限的包读进来也只是把内存打穿，
    // 所以先用声明大小拦一道，报的是同一句话、同一个数字。
    const qint64 declaredSize = QFileInfo(localFile).size();
    if (declaredSize > qint64(kMaxArchiveSize)) {
        return Result<Kernel::UpdateResult>::withError(
            fillTemplate(Kernel::Messages::localArchiveSizeInvalid(),
                         {},
                         {QString::number(declaredSize)}));
    }
    bool readOk = false;
    const QByteArray bytes = readFileFully(localFile, &readOk);
    if (!readOk) {
        return Result<Kernel::UpdateResult>::withError(
            fillTemplate(Kernel::Messages::localFileReadFailed(), {}, {localFile}));
    }
    if (bytes.isEmpty() || quint64(bytes.size()) > kMaxArchiveSize) {
        return Result<Kernel::UpdateResult>::withError(
            fillTemplate(Kernel::Messages::localArchiveSizeInvalid(),
                         {},
                         {QString::number(bytes.size())}));
    }

    // 能对上官方资产名就顺手校验一次，对不上就明确告诉他没校验。
    std::optional<std::pair<QString, QString>> expected;
    const Result<Release> release = fetchRelease(kind);
    if (release.isOk()) {
        const QString fileName = QFileInfo(localFile).fileName();
        for (const Asset &asset : release.value().assets) {
            if (asset.name == fileName) {
                const Result<QString> digest = requireSha256(asset);
                if (digest.isOk()) {
                    expected = std::make_pair(digest.value(), release.value().tagName);
                }
                break;
            }
        }
    }

    const quint64 size = quint64(bytes.size());
    if (expected.has_value()) {
        report(progress, kind, UpdateStage::Verifying, size, size, 0.0, Kernel::Messages::localDigestVerifying());
        const Status verified = verifySha256(bytes, expected->first);
        if (verified.isErr()) {
            return Result<Kernel::UpdateResult>::withError(verified.error());
        }
    } else {
        report(progress, kind, UpdateStage::Verifying, size, size, 0.0, Kernel::Messages::localDigestSkipped());
    }

    const QString expectedTag = expected.has_value() ? expected->second : QString();
    return installBytes(kind, bytes, expectedTag, cancelled, progress);
}

Result<Kernel::UpdateResult> KernelUpdater::updateFromNetwork(Kernel::Kind kind,
                                                              Kernel::Source source,
                                                              const CancelToken &cancelled,
                                                              const ProgressCallback &progress) const
{
    const Status notCancelled = ensureNotCancelled(cancelled);
    if (notCancelled.isErr()) {
        return Result<Kernel::UpdateResult>::withError(notCancelled.error());
    }
    const QString displayName = Kernel::kindDisplayName(kind);
    report(progress, kind,
           UpdateStage::Checking,
           0,
           0,
           0.0,
           fillTemplate(Kernel::Messages::checkingOfficialRelease(), {}, {displayName}));

    const Result<std::pair<Release, int>> fetched = fetchReleaseWithTransport(kind);
    if (fetched.isErr()) {
        return Result<Kernel::UpdateResult>::withError(fetched.error());
    }
    const Release release = fetched.value().first;
    const int transportIndex = fetched.value().second;
    if (release.fallback) {
        report(progress, kind,
               UpdateStage::Checking,
               0,
               0,
               0.0,
               fillTemplate(Kernel::Messages::fallbackReleaseNotice(),
                            {},
                            {displayName, release.tagName}));
    }
    Log::startup(fillTemplate(Kernel::Messages::logReleaseSource(),
                              {named("{transport_index}", QString::number(transportIndex))},
                              {displayName,
                               release.fallback ? Kernel::Messages::logSourceBuiltinFallback()
                                                : Kernel::Messages::logSourceApiOrCache(),
                               release.tagName}));

    const QString assetName = Kernel::kindAssetName(kind, release.tagName);
    const Result<Asset> selected = selectAsset(release, assetName);
    if (selected.isErr()) {
        return Result<Kernel::UpdateResult>::withError(selected.error());
    }
    const Asset asset = selected.value();
    const Result<QString> digest = requireSha256(asset);
    if (digest.isErr()) {
        return Result<Kernel::UpdateResult>::withError(digest.error());
    }
    const QString expectedHash = digest.value();
    Log::startup(fillTemplate(Kernel::Messages::logTargetAsset(),
                              {},
                              {displayName,
                               asset.name,
                               QString::number(asset.size),
                               expectedHash.left(12),
                               Kernel::sourceLabel(source)}));
    if (asset.size == 0 || asset.size > kMaxArchiveSize) {
        return Result<Kernel::UpdateResult>::withError(
            fillTemplate(Kernel::Messages::archiveSizeInvalid(), {}, {QString::number(asset.size)}));
    }

    const Result<QByteArray> downloaded =
        downloadAsset(kind, asset, source, transportIndex, cancelled, progress);
    if (downloaded.isErr()) {
        return Result<Kernel::UpdateResult>::withError(downloaded.error());
    }
    const QByteArray bytes = downloaded.value();
    if (quint64(bytes.size()) != asset.size) {
        Log::startup(fillTemplate(Kernel::Messages::logDownloadSizeMismatch(),
                                  {},
                                  {displayName,
                                   QString::number(asset.size),
                                   QString::number(bytes.size())}));
        return Result<Kernel::UpdateResult>::withError(
            fillTemplate(Kernel::Messages::downloadSizeMismatch(),
                         {},
                         {QString::number(asset.size), QString::number(bytes.size())}));
    }
    Log::startup(fillTemplate(Kernel::Messages::logDownloadComplete(),
                             {},
                             {displayName, QString::number(bytes.size())}));
    report(progress, kind, UpdateStage::Verifying, asset.size, asset.size, 0.0, Kernel::Messages::verifyingSha256());
    const Status stillRunning = ensureNotCancelled(cancelled);
    if (stillRunning.isErr()) {
        return Result<Kernel::UpdateResult>::withError(stillRunning.error());
    }
    const Status verified = verifySha256(bytes, expectedHash);
    if (verified.isErr()) {
        Log::startup(fillTemplate(Kernel::Messages::logVerifyError(),
                                  {named("{error}", verified.error())},
                                  {displayName}));
        return Result<Kernel::UpdateResult>::withError(verified.error());
    }
    Log::startup(fillTemplate(Kernel::Messages::logSha256Passed(), {}, {displayName}));
    return installBytes(kind, bytes, release.tagName, cancelled, progress);
}

Result<Kernel::UpdateResult> KernelUpdater::installBytes(Kernel::Kind kind,
                                                         const QByteArray &bytes,
                                                         const QString &expectedTag,
                                                         const CancelToken &cancelled,
                                                         const ProgressCallback &progress) const
{
    const QString displayName = Kernel::kindDisplayName(kind);
    const quint64 total = quint64(bytes.size());
    const QString kindDir = joinComponents(root_, {Kernel::kindDirectoryName(kind)});
    const QString target = joinComponents(kindDir, {Kernel::kindExecutableName(kind)});
    const QString staging = joinComponents(kindDir, {stagingName(kind)});
    // 上一版暂存名的形状（点开头 + 不以 .exe 结尾）是排障元凶之一，读到就顺手清掉。
    const QString legacyStaging =
        joinComponents(kindDir,
                       {QStringLiteral(".%1.staging").arg(Kernel::kindExecutableName(kind))});
    const QString backup =
        joinComponents(kindDir, {QStringLiteral("%1.backup").arg(Kernel::kindExecutableName(kind))});

    auto fail = [](const QString &error) {
        return Result<Kernel::UpdateResult>::withError(error);
    };

    if (!QDir().mkpath(kindDir)) {
        return fail(QStringLiteral("create_dir_all failed: %1").arg(kindDir));
    }
    // 上一次尝试留下的残渣先清掉：残留文件会让"这次到底跑没跑起来"看不出来。
    QFile::remove(staging);
    QFile::remove(legacyStaging);

    report(progress, kind, UpdateStage::Extracting, total, total, 0.0, Kernel::Messages::extractingStaging());
    const Status notCancelled = ensureNotCancelled(cancelled);
    if (notCancelled.isErr()) {
        return fail(notCancelled.error());
    }
    Log::startup(fillTemplate(Kernel::Messages::logExtractToStaging(), {}, {displayName, staging}));
    const Status extracted = extractExecutable(bytes, kind, staging);
    if (extracted.isErr()) {
        return fail(extracted.error());
    }

    // 这一步是整个"预备"最容易被外因（虚拟机 CPU / 杀软）打掉的地方，
    // 所以三种结果都写进 startup.log —— VM 上没有控制台，文件是唯一看得见的证据。
    Result<QString> staged = Kernel::readKernelVersion(kind, staging);
    bool stagedRuns = staged.isOk() && !staged.value().isNull();
    if (staged.isOk() && !staged.value().isNull()) {
        Log::startup(fillTemplate(Kernel::Messages::logStagedVersion(),
                                  {named("{line}", staged.value())}));
    } else if (staged.isOk()) {
        Log::startup(Kernel::Messages::logStagedNoOutput());
    } else {
        Log::startup(fillTemplate(Kernel::Messages::logStagedProbeFailed(),
                                  {named("{error}", staged.error())}));
    }
    // 第一枪没响就用 Go 的 CPU 特性总开关再打一枪：能分清"虚拟机 CPUID 撒谎"
    // 和"杀软挂钩 / 环境问题"这两种完全不同的下场。
    bool compatOnly = false;
    if (!stagedRuns) {
        const Result<QString> second =
            Kernel::readKernelVersionWithEnv(kind, staging, Kernel::godebugCompat());
        if (second.isOk() && !second.value().isNull()) {
            compatOnly = true;
            Log::startup(fillTemplate(Kernel::Messages::logCompatOnly(), {}, {displayName}));
            staged = second;
        } else {
            // 文本里不放反斜杠路径：日志里真正有用的是下面那行完整路径本身。
            Log::startup(Kernel::Messages::logCompatStillFails());
            Log::startup(fillTemplate(Kernel::Messages::logStagedFile(), {}, {staging}));
        }
    }
    if (staged.isErr()) {
        return fail(staged.error());
    }
    if (staged.value().isNull()) {
        return fail(fillTemplate(Kernel::Messages::stagedVersionMissing(), {}, {displayName}));
    }
    const QString stagedVersion = staged.value();
    if (!expectedTag.isNull() && !Kernel::versionMatches(stagedVersion, expectedTag)) {
        Log::startup(fillTemplate(Kernel::Messages::logStagedVersionMismatch(),
                                  {named("{staged_version}", stagedVersion),
                                   named("{tag}", expectedTag)},
                                  {displayName}));
        QFile::remove(staging);
        return fail(fillTemplate(Kernel::Messages::stagedVersionMismatch(),
                                 {named("{staged_version}", stagedVersion)}));
    }

    const Status stillRunning = ensureNotCancelled(cancelled);
    if (stillRunning.isErr()) {
        return fail(stillRunning.error());
    }
    Log::startup(fillTemplate(Kernel::Messages::logPrepareReplace(),
                              {},
                              {displayName, target, backup}));
    report(progress, kind,
           UpdateStage::Installing,
           total,
           total,
           0.0,
           Kernel::Messages::installingBackupAndReplace());

    QString backupPath;
    if (isRegularFile(target)) {
        QFile::remove(backup);
        const Status backedUp = renameReplace(target, backup);
        if (backedUp.isErr()) {
            logIoDetail(QStringLiteral("backup %1").arg(target), backedUp);
            return fail(Kernel::Messages::backupOldKernelFailed());
        }
        backupPath = backup;
    }
    const Status replaced = renameReplace(staging, target);
    if (replaced.isErr()) {
        logIoDetail(QStringLiteral("replace %1 -> %2").arg(staging, target), replaced);
        if (isRegularFile(backup)) {
            const Status rolledBack = renameReplace(backup, target);
            if (rolledBack.isErr()) {
                logIoDetail(QStringLiteral("rollback %1 -> %2").arg(backup, target), rolledBack);
            }
        }
        return fail(Kernel::Messages::replaceFailedRolledBack());
    }
    const Result<QString> postInstall = Kernel::readKernelVersion(kind, target);
    if (postInstall.isErr()) {
        Log::startup(fillTemplate(Kernel::Messages::logReplacedStillFails(),
                                  {named("{error}", postInstall.error())},
                                  {displayName}));
        QFile::remove(target);
        if (isRegularFile(backup)) {
            const Status rolledBack = renameReplace(backup, target);
            if (rolledBack.isErr()) {
                logIoDetail(QStringLiteral("rollback %1 -> %2").arg(backup, target), rolledBack);
            }
        }
        return fail(Kernel::Messages::newKernelVerifyFailedRolledBack());
    }

    Log::startup(fillTemplate(Kernel::Messages::logInstallComplete(),
                              {},
                              {displayName,
                               target,
                               stagedVersion,
                               backupPath.isNull() ? Kernel::Messages::noOldKernelToBackup()
                                                   : backupPath}));
    QString message =
        fillTemplate(Kernel::Messages::kernelUpdatedTo(), {}, {displayName, stagedVersion});
    if (compatOnly) {
        // 界面上不能只说"成功"：这台机器的内核要靠 CPU 兼容开关才跑得起来。
        message += compatModeSuffix();
    }
    Kernel::UpdateResult result;
    result.kind = kind;
    result.version = stagedVersion;
    result.path = target;
    result.backupPath = backupPath;
    result.message = message;
    return Result<Kernel::UpdateResult>::withValue(std::move(result));
}

QString KernelUpdater::releaseCachePath(Kernel::Kind kind) const
{
    return joinComponents(root_,
                          {QStringLiteral(".release-cache"),
                           QStringLiteral("%1.json").arg(Kernel::kindDirectoryName(kind))});
}

Result<QVector<int>> KernelUpdater::transportOrder(Kernel::Source source) const
{
    QVector<int> order;
    for (qsizetype index = 0; index < transports_.size(); ++index) {
        if (Kernel::sourceAllowsTransport(source, transports_.at(index).label)) {
            order.push_back(int(index));
        }
    }
    if (order.isEmpty()) {
        return Result<QVector<int>>::withError(
            fillTemplate(Kernel::Messages::noChannelForSource(), {}, {Kernel::sourceLabel(source)}));
    }
    return Result<QVector<int>>::withValue(order);
}

Result<Release> KernelUpdater::fetchReleaseJson(const Transport &transport, const QString &url) const
{
    Net::Request request = makeGetRequest(transport, url, kReleaseTimeoutMs);
    const Result<Net::Response> sent = Net::send(request);
    if (sent.isErr()) {
        // Rust：`.map_err(|error| anyhow!("{error:#}"))` —— 传输层错误原样带上原因。
        return Result<Release>::withError(sent.error());
    }
    const Net::Response response = sent.value();
    if (!response.isSuccess()) {
        // Rust 的 `.error_for_status()?`：非 2xx 一律算失败。
        return Result<Release>::withError(
            QStringLiteral("error decoding response body: HTTP %1").arg(response.status));
    }
    const Result<Json> parsed = JsonUtil::parse(QString::fromUtf8(response.body));
    if (parsed.isErr()) {
        return Result<Release>::withError(parsed.error());
    }
    const std::optional<Release> release = Release::fromJson(parsed.value());
    if (!release.has_value()) {
        return Result<Release>::withError(QStringLiteral("error decoding response body"));
    }
    return Result<Release>::withValue(*release);
}

Result<Release> KernelUpdater::fetchRelease(Kernel::Kind kind) const
{
    const Result<std::pair<Release, int>> fetched = fetchReleaseWithTransport(kind);
    if (fetched.isErr()) {
        return Result<Release>::withError(fetched.error());
    }
    return Result<Release>::withValue(fetched.value().first);
}

Result<std::pair<Release, int>> KernelUpdater::fetchReleaseWithTransport(Kernel::Kind kind) const
{
    const QString url =
        QStringLiteral("https://api.github.com/repos/%1/releases/latest")
            .arg(Kernel::kindGithubRepository(kind));
    QStringList failures;
    for (qsizetype index = 0; index < transports_.size(); ++index) {
        const Result<Release> fetched = fetchReleaseJson(transports_.at(index), url);
        if (fetched.isOk()) {
            // 上一次成功查到的 Release 会写在这儿，全失败时退到它。
            const QString cachePath = releaseCachePath(kind);
            QDir().mkpath(QFileInfo(cachePath).path());
            QSaveFile file(cachePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(JsonUtil::dump(fetched.value().toJson()).toUtf8());
                file.commit();
            }
            return Result<std::pair<Release, int>>::withValue({fetched.value(), int(index)});
        }
        failures.push_back(
            QStringLiteral("%1: %2").arg(transports_.at(index).label, fetched.error()));
    }
    const QString remoteError = joinFailures(failures);

    bool readOk = false;
    const QByteArray cached = readFileFully(releaseCachePath(kind), &readOk);
    if (readOk) {
        const Result<Json> parsed = JsonUtil::parse(QString::fromUtf8(cached));
        if (parsed.isOk()) {
            const std::optional<Release> release = Release::fromJson(parsed.value());
            if (release.has_value()) {
                return Result<std::pair<Release, int>>::withValue({*release, 0});
            }
        }
    }
    // 新装的机器上缓存也不存在，而 api.github.com 在这边是间歇被掐的（镜像站又不代理它）。
    // 这时用内置兜底版本，别让"预备状态"卡在查不到版本号上。
    const std::optional<Release> fallback = fallbackRelease(kind);
    if (fallback.has_value()) {
        return Result<std::pair<Release, int>>::withValue({*fallback, 0});
    }
    return Result<std::pair<Release, int>>::withError(
        fillTemplate(Kernel::Messages::releaseFetchFailed(),
                     {named("{remote_error}", remoteError)},
                     {Kernel::kindDisplayName(kind)}));
}

Result<QByteArray> KernelUpdater::downloadAsset(Kernel::Kind kind,
                                                const Asset &asset,
                                                Kernel::Source source,
                                                int preferred,
                                                const CancelToken &cancelled,
                                                const ProgressCallback &progress) const
{
    const Status notCancelled = ensureNotCancelled(cancelled);
    if (notCancelled.isErr()) {
        return Result<QByteArray>::withError(notCancelled.error());
    }
    const Result<QVector<int>> orderResult = transportOrder(source);
    if (orderResult.isErr()) {
        return Result<QByteArray>::withError(orderResult.error());
    }
    QVector<int> order = orderResult.value();
    // 查版本先赢的那条通道排最前（只是排序提示；std::stable_sort 对应 Rust 的稳定 sort_by_key）。
    std::stable_sort(order.begin(), order.end(), [preferred](int left, int right) {
        const int leftKey = left == preferred ? 0 : 1;
        const int rightKey = right == preferred ? 0 : 1;
        return leftKey < rightKey;
    });
    const QString url = Kernel::assetUrlFor(source, asset.browserDownloadUrl);
    QStringList failures;
    // 一次只有一条连接在跑：这条通道失败才换下一条，所以进度天然单调递增。
    for (const int index : order) {
        const Transport &transport = transports_.at(index);
        const QString label = fillTemplate(Kernel::Messages::transportSourceJoin(),
                                           {},
                                           {transport.label, Kernel::sourceLabel(source)});
        const Result<QByteArray> streamed =
            streamAsset(kind, transport, url, label, asset.name, asset.size, cancelled, progress);
        if (streamed.isOk()) {
            return streamed;
        }
        failures.push_back(QStringLiteral("%1: %2").arg(label, streamed.error()));
        report(progress, kind,
               UpdateStage::Downloading,
               0,
               asset.size,
               0.0,
               fillTemplate(Kernel::Messages::channelFailedNextTransport(),
                            {named("{label}", label), named("{error}", streamed.error())}));
    }
    return Result<QByteArray>::withError(
        fillTemplate(Kernel::Messages::downloadAllFailed(), {}, {asset.name, joinFailures(failures)}));
}

Result<QByteArray> KernelUpdater::streamAsset(Kernel::Kind kind,
                                              const Transport &transport,
                                              const QString &url,
                                              const QString &label,
                                              const QString &name,
                                              quint64 expected,
                                              const CancelToken &cancelled,
                                              const ProgressCallback &progress) const
{
    if (cancelled.cancelled()) {
        return Result<QByteArray>::withError(Kernel::Messages::cancelledMarker());
    }
    Net::Request request = makeGetRequest(transport, url, kAssetTimeoutMs);
    QElapsedTimer clock;
    clock.start();
    const Result<Net::Response> sent = Net::send(request);
    if (sent.isErr()) {
        // Rust：`Err(error) => bail!("{error:#}")`。
        return Result<QByteArray>::withError(sent.error());
    }
    const Net::Response response = sent.value();
    if (!response.isSuccess()) {
        // Rust：`response.error_for_status()` 失败 → `bail!("HTTP 失败: {error}")`。
        return Result<QByteArray>::withError(
            fillTemplate(Kernel::Messages::httpFailed(),
                         {named("{error}", QStringLiteral("HTTP %1").arg(response.status))}));
    }
    const QByteArray bytes = response.body;
    if (quint64(bytes.size()) > kMaxArchiveSize) {
        return Result<QByteArray>::withError(Kernel::Messages::downloadExceededLimit());
    }
    // 一次一条连接，进度只在这条连接结束时报一次；速度按本条连接自己的字节数/耗时算
    // （对应 Rust 每个 chunk 用 started 累计的那套）。
    const double rawElapsed = double(clock.elapsed()) / 1000.0;
    // Rust：`started.elapsed().as_secs_f64().max(0.001)`。
    const double elapsed = rawElapsed < 0.001 ? 0.001 : rawElapsed;
    const quint64 own = quint64(bytes.size());
    report(progress, kind,
           UpdateStage::Downloading,
           own,
           expected,
           double(own) / elapsed,
           fillTemplate(Kernel::Messages::downloadingAsset(),
                        {named("{name}", name), named("{label}", label)}));
    if (own < expected) {
        // 长度不够就判失败——镜像偶尔回半截或回 HTML，不能让它把坏数据交给上层校验。
        return Result<QByteArray>::withError(
            fillTemplate(Kernel::Messages::downloadTruncated(),
                         {},
                         {QString::number(own), QString::number(expected)}));
    }
    return Result<QByteArray>::withValue(bytes);
}

}
