#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <optional>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

#include "core/Crypto.h"
#include "core/JsonUtil.h"
#include "kernel/KernelManager.h"
#include "kernel/KernelMessages.h"
#include "kernel/KernelRelease.h"
#include "kernel/KernelSource.h"
#include "kernel/KernelTypes.h"
#include "updater/KernelArchive.h"
#include "updater/KernelUpdater.h"
#include "updater/UpdaterFormat.h"

using namespace Updater;
using Kernel::Kind;
using Kernel::Messages::compatModeSuffix;
using Kernel::Messages::directTransport;
using Kernel::Source;
using Kernel::UpdateProgress;
using Kernel::UpdateStage;

namespace {

/// 用户可见文本的期望值一律由 `Kernel::Messages::*()`（tools/gen-kernel-cjk-codepoints.mjs
/// 的产物）现取现拼，测试里一个中文都不写——手打中文在这个环境里会被静默丢掉字符。
QString joinNative(const QString &base, const QStringList &components)
{
    QString out = QDir::toNativeSeparators(base);
    for (const QString &component : components) {
        out = QDir(out).absoluteFilePath(component);
    }
    return QDir::toNativeSeparators(out);
}

QByteArray readWholeFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

bool writeStubFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(bytes);
    file.close();
    return true;
}

QString findProgramInPath(const QString &program)
{
    const QStringList directories = QString(qEnvironmentVariable("PATH")).split(QLatin1Char(';'));
    for (const QString &directory : directories) {
        if (directory.isEmpty()) {
            continue;
        }
        const QFileInfo info(QDir(directory).absoluteFilePath(program));
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

/// "拷到别处还能跑、给 -v 会打印一行东西"的无害桩：装内核那条链要真的把暂存文件跑一次才
/// 走得完，但绝不能用这台机器上的真内核（有一份生产安装正跑着）。System32 的小工具最可靠
/// （不依赖自己的安装目录），git/node 只当补充，node 体积最大所以排在最后。
QString harmlessVersionStub()
{
    static QString cached;
    if (!cached.isEmpty()) {
        return cached;
    }
    const QString systemRoot = qEnvironmentVariable("SystemRoot");
    QStringList candidates;
    if (!systemRoot.isEmpty()) {
        candidates.push_back(QDir(systemRoot).absoluteFilePath(QStringLiteral("System32/where.exe")));
        candidates.push_back(QDir(systemRoot).absoluteFilePath(QStringLiteral("System32/ping.exe")));
    }
    candidates.push_back(findProgramInPath(QStringLiteral("git.exe")));
    candidates.push_back(findProgramInPath(QStringLiteral("node.exe")));

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (candidate.isEmpty() || !info.isFile()
            || info.size() > qint64(kMaxArchiveSize)) {
            continue;
        }
        const QTemporaryDir probeDir;
        if (!probeDir.isValid()) {
            continue;
        }
        const QString copy =
            QDir(probeDir.path()).absoluteFilePath(QStringLiteral("mihomo.staging.exe"));
        if (!QFile::copy(candidate, copy)) {
            continue;
        }
        const Result<QString> probed = Kernel::readKernelVersion(Kind::Mihomo, copy);
        if (probed.isOk() && !probed.value().isNull() && !probed.value().isEmpty()) {
            cached = candidate;
            return cached;
        }
    }
    return QString();
}

la_ssize_t zipWriteCallback(struct archive *writer, void *client, const void *buffer, size_t length)
{
    auto *out = static_cast<QByteArray *>(client);
    out->append(static_cast<const char *>(buffer), int(length));
    return la_ssize_t(length);
}

int zipOpenCallback(struct archive *, void *)
{
    return ARCHIVE_OK;
}

int zipCloseCallback(struct archive *, void *)
{
    return ARCHIVE_OK;
}

/// 在内存里造一个 ZIP（对应 Rust 测试里的 `zip::ZipWriter`）。
QByteArray makeZip(const QVector<QPair<QString, QByteArray>> &entries)
{
    QByteArray out;
    struct archive *writer = archive_write_new();
    if (writer == nullptr) {
        return out;
    }
    archive_write_add_filter_none(writer);
    archive_write_set_format_zip(writer);
    if (archive_write_open(writer, &out, zipOpenCallback, zipWriteCallback, zipCloseCallback)
        != ARCHIVE_OK) {
        archive_write_free(writer);
        return QByteArray();
    }
    for (const auto &entry : entries) {
        struct archive_entry *item = archive_entry_new();
        archive_entry_set_pathname(item, entry.first.toUtf8().constData());
        archive_entry_set_filetype(item, AE_IFREG);
        archive_entry_set_perm(item, 0644);
        archive_entry_set_size(item, entry.second.size());
        if (archive_write_header(writer, item) == ARCHIVE_OK && !entry.second.isEmpty()) {
            archive_write_data(writer, entry.second.constData(), size_t(entry.second.size()));
        }
        archive_entry_free(item);
    }
    archive_write_close(writer);
    archive_write_free(writer);
    return out;
}

struct Recorded
{
    QList<UpdateStage> stages;
    QList<UpdateProgress> updates;
};

void record(Recorded *into, const UpdateProgress &progress)
{
    into->stages.push_back(progress.stage);
    into->updates.push_back(progress);
}

/// 只带"直连"一条通道的更新器：联网前的判定因此能不碰网络地测。
KernelUpdater directOnlyUpdater(const QString &root)
{
    Transport direct;
    direct.label = directTransport();
    return KernelUpdater(root, {direct});
}

Asset makeAsset(const QString &name, quint64 size, const QString &digest)
{
    Asset asset;
    asset.name = name;
    asset.browserDownloadUrl =
        QStringLiteral("https://github.com/MetaCubeX/mihomo/releases/download/v1.19.29/%1").arg(name);
    asset.size = size;
    asset.digest = digest;
    return asset;
}

const char *kGoodDigest = "sha256:1ac84e795b5b915446e20139677fb6cc013a0778e876e8d4c96f994750b4c4a3";

}

// ================= Rust `mod tests`（updater.rs），逐条搬过来 =================

TEST(UpdaterKernel, matches_release_tag_in_version_output)
{
    EXPECT_TRUE(Kernel::versionMatches(QStringLiteral("Mihomo Meta v1.19.29 windows amd64"),
                                       QStringLiteral("v1.19.29")));
    EXPECT_TRUE(
        Kernel::versionMatches(QStringLiteral("Xray 26.3.27 (Xray)"), QStringLiteral("v26.3.27")));
    EXPECT_FALSE(Kernel::versionMatches(QStringLiteral("sing-box version 1.13.13"),
                                        QStringLiteral("v1.13.14")));
}

TEST(UpdaterKernel, fallback_release_is_self_consistent)
{
    // 兜底 Release 必须是"照原样下得回来、哈希对得上"的那种：资产名符合内核自己的命名规则，
    // 摘要要能过 require_sha256（否则下载后校验会立刻失败）。
    const std::optional<Release> release = fallbackRelease(Kind::Mihomo);
    ASSERT_TRUE(release.has_value());
    EXPECT_TRUE(release->fallback);
    ASSERT_EQ(release->assets.size(), 1);
    const Asset &asset = release->assets.at(0);
    EXPECT_EQ(asset.name, Kernel::kindAssetName(Kind::Mihomo, release->tagName));
    EXPECT_EQ(asset.size, Q_UINT64_C(22492402));
    EXPECT_EQ(asset.browserDownloadUrl.toStdString(),
              "https://github.com/MetaCubeX/mihomo/releases/download/" + release->tagName.toStdString()
                  + "/" + asset.name.toStdString());
    const Result<QString> digest = requireSha256(asset);
    ASSERT_TRUE(digest.isOk()) << digest.error().toStdString();
    EXPECT_EQ(digest.value().size(), 64);
    // 只兜底 mihomo：别的内核没实测过哈希，不许编一个版本号。
    EXPECT_FALSE(fallbackRelease(Kind::Xray).has_value());
    EXPECT_FALSE(fallbackRelease(Kind::SingBox).has_value());
}

TEST(UpdaterKernel, verifies_sha256_digest)
{
    const QByteArray payload("kernel");
    const QString expected = Crypto::sha256Hex(payload);
    ASSERT_EQ(expected.size(), 64);
    EXPECT_TRUE(verifySha256(payload, expected).isOk());
    // 大写十六进制也认（Rust 用 eq_ignore_ascii_case）。
    EXPECT_TRUE(verifySha256(payload, expected.toUpper()).isOk());

    const Status changed = verifySha256(QByteArray("changed"), expected);
    ASSERT_TRUE(changed.isErr());
    EXPECT_EQ(changed.error(), Kernel::Messages::sha256Mismatch());
}

TEST(UpdaterKernel, cancellation_stops_before_installation)
{
    CancelToken cancelled;
    cancelled.cancel();
    const Status status = ensureNotCancelled(cancelled);
    ASSERT_TRUE(status.isErr());
    // Rust 断言 `contains("已取消")`：那两个字从生成的消息里取，不手打。
    EXPECT_TRUE(status.error().contains(Kernel::Messages::cancelledMarker()));
    EXPECT_EQ(status.error(), Kernel::Messages::kernelDownloadCancelled());

    const CancelToken fresh;
    EXPECT_TRUE(ensureNotCancelled(fresh).isOk());
    // 拷贝共享同一份状态（对应 Arc<AtomicBool>）。
    CancelToken shared = fresh;
    shared.cancel();
    EXPECT_TRUE(fresh.cancelled());
}

TEST(UpdaterKernel, staging_files_keep_an_executable_name)
{
    // 暂存名的形状是排障关键：以 .exe 结尾、不前导点、不撞正式名。
    for (const Kind kind : {Kind::Mihomo, Kind::Xray, Kind::SingBox}) {
        const QString name = stagingName(kind);
        EXPECT_TRUE(name.endsWith(QLatin1String(".exe"))) << name.toStdString();
        EXPECT_FALSE(name.startsWith(QLatin1Char('.'))) << name.toStdString();
        EXPECT_NE(name, Kernel::kindExecutableName(kind));
    }
    EXPECT_EQ(stagingName(Kind::Mihomo).toStdString(), "mihomo.staging.exe");
    EXPECT_EQ(stagingName(Kind::SingBox).toStdString(), "sing-box.staging.exe");
    EXPECT_EQ(stagingName(Kind::Xray).toStdString(), "xray.staging.exe");
}

TEST(UpdaterKernel, a_kernel_that_cannot_run_is_never_installed)
{
    // 对应 Rust `a_kernel_that_cannot_run_is_never_installed`：下得来、哈希也对，但跑不起来的
    // 内核不许进正式路径（进了就是"看起来装好了，启动代理才炸"）。
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QByteArray archive =
        makeZip({{QStringLiteral("mihomo.exe"), QByteArray("not a windows executable")}});
    ASSERT_FALSE(archive.isEmpty());

    const KernelUpdater updater(directory.path(), QVector<Transport>());
    const CancelToken cancelled;
    Recorded recorded;
    const Result<Kernel::UpdateResult> result =
        updater.installBytes(Kind::Mihomo,
                            archive,
                            QString(),
                            cancelled,
                            [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(result.isErr());
    // 错误要指出是探测这一步：Rust 断言 contains("执行") || contains("未返回版本")，
    // 这两段都从生成的消息里现取（"执行 {} 失败" 的头两个字 / "{} 暂存内核未返回版本" 的尾巴）。
    const QString probeFragment = Kernel::Messages::executeKernelFailed().left(2);
    const QString noVersionFragment = Kernel::Messages::stagedVersionMissing().right(5);
    EXPECT_TRUE(result.error().contains(probeFragment) || result.error().contains(noVersionFragment))
        << result.error().toStdString();
    // 正式路径必须还是干净的。
    const QString target = joinNative(directory.path(), {QStringLiteral("mihomo"),
                                                         QStringLiteral("mihomo.exe")});
    EXPECT_FALSE(QFileInfo(target).exists());
    EXPECT_EQ(recorded.stages, QList<UpdateStage> {UpdateStage::Extracting});
}

TEST(UpdaterKernel, xray_archive_extracts_executable_and_geo_data)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QByteArray archive = makeZip({
        {QStringLiteral("xray.exe"), QByteArray("exe")},
        {QStringLiteral("geoip.dat"), QByteArray("ip")},
        {QStringLiteral("geosite.dat"), QByteArray("site")},
        {QStringLiteral("LICENSE"), QByteArray("junk")},
    });
    ASSERT_FALSE(archive.isEmpty());

    const QString target = QDir(directory.path()).absoluteFilePath(stagingName(Kind::Xray));
    const Status extracted = extractExecutable(archive, Kind::Xray, target);
    ASSERT_TRUE(extracted.isOk()) << extracted.error().toStdString();

    EXPECT_EQ(readWholeFile(target), QByteArray("exe"));
    EXPECT_EQ(readWholeFile(QDir(directory.path()).absoluteFilePath(QStringLiteral("geoip.dat"))),
              QByteArray("ip"));
    EXPECT_EQ(readWholeFile(QDir(directory.path()).absoluteFilePath(QStringLiteral("geosite.dat"))),
              QByteArray("site"));
    // 别的条目一个都不落地。
    EXPECT_FALSE(
        QFileInfo(QDir(directory.path()).absoluteFilePath(QStringLiteral("LICENSE"))).exists());
}

TEST(UpdaterKernel, extract_uses_the_basename_of_nested_entries)
{
    // 官方 ZIP 里 mihomo 的可执行文件在包根，Xray 也一样；这一条钉住"只按 basename 判定、
    // 落盘也只落到 target"（Rust：path.file_name()）。
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QByteArray archive = makeZip({
        {QStringLiteral("mihomo-windows/mihomo.exe"), QByteArray("nested kernel")},
        {QStringLiteral("mihomo-windows/README.md"), QByteArray("doc")},
    });
    ASSERT_FALSE(archive.isEmpty());
    const QString target = QDir(directory.path()).absoluteFilePath(stagingName(Kind::Mihomo));
    const Status extracted = extractExecutable(archive, Kind::Mihomo, target);
    ASSERT_TRUE(extracted.isOk()) << extracted.error().toStdString();
    EXPECT_EQ(readWholeFile(target), QByteArray("nested kernel"));
    EXPECT_FALSE(
        QFileInfo(QDir(directory.path()).absoluteFilePath(QStringLiteral("README.md"))).exists());
}

TEST(UpdaterKernel, transport_order_gates_proxy_versus_direct)
{
    // 对应 Rust `transport_order_gates_proxy_versus_direct`（代理地址是桩，不联网）。
    const KernelUpdater updater = KernelUpdater::createWithCandidates(
        QStringLiteral("unused"),
        QStringLiteral("http://127.0.0.1:9"),
        {std::optional<QString>(QStringLiteral("http://127.0.0.1:9")), std::optional<QString>()});
    ASSERT_EQ(updater.transports().size(), 2);
    EXPECT_TRUE(updater.transports().at(0).label
                    .contains(Kernel::Messages::transportAppProxyLabel().left(5)));
    EXPECT_TRUE(updater.transports().at(0).label.contains(QStringLiteral("127.0.0.1:9")));
    EXPECT_EQ(updater.transports().at(1).label, directTransport());

    EXPECT_TRUE(updater.transportOrder(Source::Proxy).isOk());
    // 官方直连和两个镜像方式都只允许不经代理那一条（镜像站本身就在国内）。
    for (const Source source : {Source::Official, Source::GhProxy, Source::GhFast}) {
        const Result<QVector<int>> only = updater.transportOrder(source);
        ASSERT_TRUE(only.isOk()) << only.error().toStdString();
        EXPECT_EQ(only.value(), QVector<int> {1});
        for (const int index : only.value()) {
            EXPECT_EQ(updater.transports().at(index).label, directTransport());
        }
    }
    // 「走代理」不许退回直连。
    const Result<QVector<int>> proxied = updater.transportOrder(Source::Proxy);
    ASSERT_TRUE(proxied.isOk());
    EXPECT_EQ(proxied.value(), QVector<int> {0});
    // 本地导入根本不联网：通道判定全放行。
    EXPECT_TRUE(updater.transportOrder(Source::Local).isOk());

    // 标签是"系统代理 …"而不是"应用内代理 …"：候选来自注册表那条时，app_proxy 对不上。
    const KernelUpdater systemLabeled = KernelUpdater::createWithCandidates(
        QStringLiteral("unused"),
        QString(),
        {std::optional<QString>(QStringLiteral("http://127.0.0.1:10808")), std::optional<QString>()});
    EXPECT_TRUE(systemLabeled.transports().at(0).label
                    .contains(Kernel::Messages::transportSystemProxyLabel().left(4)));
}

// ================= 与下载 URL / 代理候选有关的那几条 =================

TEST(UpdaterKernel, candidate_chain_always_ends_with_direct_and_no_duplicates)
{
    // 对应 Rust `candidate_chain_always_ends_with_direct_and_no_duplicates`。
    // 差别：Rust 自己去读系统代理，这里把它做成入参（测试不许碰注册表）。
    const QString app = QStringLiteral("http://127.0.0.1:9980");
    const QVector<std::optional<QString>> chain = proxyCandidates(app, QString());
    ASSERT_EQ(chain.size(), 2);
    ASSERT_TRUE(chain.first().has_value());
    EXPECT_EQ(chain.first().value_or(QString()), app);
    EXPECT_FALSE(chain.last().has_value());

    // 候选不能重复（系统代理和应用内代理常常是同一个）。
    EXPECT_EQ(proxyCandidates(app, app).size(), 2);
    const QVector<std::optional<QString>> three =
        proxyCandidates(app, QStringLiteral("http://127.0.0.1:1"));
    ASSERT_EQ(three.size(), 3);
    ASSERT_TRUE(three.at(1).has_value());
    EXPECT_EQ(three.at(1).value_or(QString()), QStringLiteral("http://127.0.0.1:1"));
    EXPECT_FALSE(three.last().has_value());

    const QVector<std::optional<QString>> systemOnly =
        proxyCandidates(QString(), QStringLiteral("http://127.0.0.1:10808"));
    ASSERT_EQ(systemOnly.size(), 2);
    EXPECT_EQ(systemOnly.at(0).value_or(QString()), QStringLiteral("http://127.0.0.1:10808"));

    const QVector<std::optional<QString>> direct = proxyCandidates(QString(), QString());
    ASSERT_EQ(direct.size(), 1);
    EXPECT_FALSE(direct.first().has_value());
    // 空白代理地址不能当成一条通道。
    EXPECT_EQ(proxyCandidates(QStringLiteral("   "), QString()), direct);
    // trim 只作用在应用内代理那一条上（与 Rust 的 `map(str::trim)` 一致）。
    const QVector<std::optional<QString>> trimmed =
        proxyCandidates(QStringLiteral("  http://127.0.0.1:9980  "), QString());
    ASSERT_TRUE(trimmed.first().has_value());
    EXPECT_EQ(trimmed.first().value_or(QString()), app);
}

TEST(UpdaterKernel, each_download_method_resolves_to_exactly_one_url)
{
    const QString direct = QStringLiteral(
        "https://github.com/MetaCubeX/mihomo/releases/download/v1.19.29/"
        "mihomo-windows-amd64-v3-v1.19.29.zip");
    EXPECT_EQ(Kernel::assetUrlFor(Source::GhProxy, direct).toStdString(),
              "https://gh-proxy.com/" + direct.toStdString());
    EXPECT_EQ(Kernel::assetUrlFor(Source::GhFast, direct).toStdString(),
              "https://ghfast.top/" + direct.toStdString());
    for (const Source source : {Source::Official, Source::Proxy, Source::Local}) {
        EXPECT_EQ(Kernel::assetUrlFor(source, direct), direct);
    }
}

// ================= 本层自己的纯逻辑：选资产 / 摘要 / Release JSON / 目录布局 =================

TEST(UpdaterKernel, select_asset_pins_the_asset_name_matching_rule)
{
    const QString tag = QStringLiteral("v1.19.29");
    const QString expectedName = Kernel::kindAssetName(Kind::Mihomo, tag);
    Release release;
    release.tagName = tag;
    release.assets.push_back(makeAsset(QStringLiteral("mihomo-windows-amd64-v3-v1.19.29.zip"),
                                       Q_UINT64_C(22492402),
                                       QString::fromLatin1(kGoodDigest)));
    release.assets.push_back(
        makeAsset(expectedName, Q_UINT64_C(22492402), QString::fromLatin1(kGoodDigest)));

    // 取的是"名字全等"的那一条，不是前缀像的那一条（v3 构建排在前面）。
    const Result<Asset> selected = KernelUpdater::selectAsset(release, expectedName);
    ASSERT_TRUE(selected.isOk()) << selected.error().toStdString();
    EXPECT_EQ(selected.value().name, expectedName);

    const QString absent = QStringLiteral("mihomo-windows-arm64-v1.19.29.zip");
    const Result<Asset> missing = KernelUpdater::selectAsset(release, absent);
    ASSERT_TRUE(missing.isErr());
    EXPECT_EQ(missing.error(),
              fillTemplate(Kernel::Messages::releaseAssetMissing(),
                           {{QStringLiteral("{asset_name}"), absent}}));
}

TEST(UpdaterKernel, require_sha256_rejects_missing_and_malformed_digests)
{
    EXPECT_EQ(requireSha256(
                  makeAsset(QStringLiteral("a.zip"), Q_UINT64_C(1), QString::fromLatin1(kGoodDigest)))
                  .value()
                  .size(),
              64);

    const Result<QString> absent =
        requireSha256(makeAsset(QStringLiteral("a.zip"), Q_UINT64_C(1), QString()));
    ASSERT_TRUE(absent.isErr());
    EXPECT_EQ(absent.error(), Kernel::Messages::assetDigestMissing());

    const QStringList malformed = {
        // 没有 sha256: 前缀
        QStringLiteral("1ac84e795b5b915446e20139677fb6cc013a0778e876e8d4c96f994750b4c4a3"),
        // 少一位
        QStringLiteral("sha256:1ac84e795b5b915446e20139677fb6cc013a0778e876e8d4c96f994750b4c4a"),
        // 不是十六进制
        QStringLiteral("sha256:zzc84e795b5b915446e20139677fb6cc013a0778e876e8d4c96f994750b4c4a3"),
    };
    for (const QString &digest : malformed) {
        const Result<QString> rejected =
            requireSha256(makeAsset(QStringLiteral("a.zip"), Q_UINT64_C(1), digest));
        ASSERT_TRUE(rejected.isErr()) << digest.toStdString();
        EXPECT_EQ(rejected.error(), Kernel::Messages::assetDigestInvalid());
    }
}

TEST(UpdaterKernel, release_json_uses_the_serde_field_names)
{
    const Result<Json> parsed = JsonUtil::parse(QStringLiteral(
        "{\"tag_name\":\"v1.19.29\",\"assets\":["
        "{\"name\":\"mihomo-windows-amd64-v1.19.29.zip\","
        "\"browser_download_url\":\"https://github.com/MetaCubeX/mihomo/releases/download/"
        "v1.19.29/mihomo-windows-amd64-v1.19.29.zip\",\"size\":22492402,"
        "\"digest\":\"sha256:1ac84e795b5b915446e20139677fb6cc013a0778e876e8d4c96f994750b4c4a3\"}]}"));
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    const std::optional<Release> release = Release::fromJson(parsed.value());
    ASSERT_TRUE(release.has_value());
    EXPECT_EQ(release->tagName.toStdString(), "v1.19.29");
    // fallback 是 #[serde(default)]：响应里没这个键就是 false。
    EXPECT_FALSE(release->fallback);
    ASSERT_EQ(release->assets.size(), 1);
    EXPECT_EQ(release->assets.at(0).size, Q_UINT64_C(22492402));
    EXPECT_EQ(release->assets.at(0).digest.left(7).toStdString(), "sha256:");

    // 本地缓存 = 这份 JSON 再读回来：字段名必须和 serde 写出去的完全一致。
    const std::optional<Release> roundTrip = Release::fromJson(release->toJson());
    ASSERT_TRUE(roundTrip.has_value());
    EXPECT_EQ(roundTrip->tagName, release->tagName);
    EXPECT_EQ(roundTrip->assets.size(), release->assets.size());
    EXPECT_EQ(roundTrip->assets.at(0).browserDownloadUrl, release->assets.at(0).browserDownloadUrl);
    EXPECT_EQ(roundTrip->assets.at(0).digest, release->assets.at(0).digest);
    EXPECT_EQ(roundTrip->fallback, release->fallback);

    // digest 允许 null（Option<String>）；缺 name/size 就整条解析失败。
    const Result<Json> nullDigest = JsonUtil::parse(
        QStringLiteral("{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a\","
                       "\"browser_download_url\":\"u\",\"size\":1,\"digest\":null}]}"));
    ASSERT_TRUE(nullDigest.isOk());
    const std::optional<Release> digestless = Release::fromJson(nullDigest.value());
    ASSERT_TRUE(digestless.has_value());
    EXPECT_TRUE(digestless->assets.at(0).digest.isNull());

    EXPECT_FALSE(Release::fromJson(Json::array()).has_value());
    const Result<Json> broken = JsonUtil::parse(
        QStringLiteral("{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a\",\"size\":1}]}"));
    ASSERT_TRUE(broken.isOk());
    EXPECT_FALSE(Release::fromJson(broken.value()).has_value());
    const Result<Json> noAssets =
        JsonUtil::parse(QStringLiteral("{\"tag_name\":\"v1\"}"));
    ASSERT_TRUE(noAssets.isOk());
    EXPECT_FALSE(Release::fromJson(noAssets.value()).has_value());
}

TEST(UpdaterKernel, release_cache_path_lays_out_under_the_root)
{
    const QString root = joinNative(QStringLiteral("C:/App"),
                                    {QStringLiteral("data"), QStringLiteral("kernels")});
    const KernelUpdater updater(root, QVector<Transport>());
    EXPECT_EQ(updater.releaseCachePath(Kind::Mihomo).toStdString(),
              "C:\\App\\data\\kernels\\.release-cache\\mihomo.json");
    EXPECT_EQ(updater.releaseCachePath(Kind::Xray).toStdString(),
              "C:\\App\\data\\kernels\\.release-cache\\xray.json");
    EXPECT_EQ(updater.releaseCachePath(Kind::SingBox).toStdString(),
              "C:\\App\\data\\kernels\\.release-cache\\sing-box.json");
}

TEST(UpdaterKernel, check_rules_pick_the_asset_and_decide_whether_an_update_exists)
{
    const QString tag = QStringLiteral("v1.19.29");
    Release release;
    release.tagName = tag;
    release.assets.push_back(makeAsset(Kernel::kindAssetName(Kind::Mihomo, tag),
                                       Q_UINT64_C(22492402),
                                       QString::fromLatin1(kGoodDigest)));

    Kernel::Info installed;
    installed.kind = Kind::Mihomo;
    installed.version = QStringLiteral("Mihomo Meta v1.19.29 windows amd64");
    const Result<Kernel::ReleaseInfo> upToDate =
        KernelUpdater::releaseInfoFromRelease(release, installed);
    ASSERT_TRUE(upToDate.isOk()) << upToDate.error().toStdString();
    EXPECT_FALSE(upToDate.value().updateAvailable);
    EXPECT_EQ(upToDate.value().latestVersion, tag);
    EXPECT_EQ(upToDate.value().currentVersion, installed.version);
    EXPECT_EQ(upToDate.value().assetName, Kernel::kindAssetName(Kind::Mihomo, tag));
    EXPECT_EQ(upToDate.value().message,
              fillTemplate(Kernel::Messages::releaseUpToDate(),
                           {},
                           {Kernel::kindDisplayName(Kind::Mihomo)}));

    installed.version = QStringLiteral("Mihomo Meta v1.19.28 windows amd64");
    const Result<Kernel::ReleaseInfo> outdated =
        KernelUpdater::releaseInfoFromRelease(release, installed);
    ASSERT_TRUE(outdated.isOk());
    EXPECT_TRUE(outdated.value().updateAvailable);
    EXPECT_EQ(outdated.value().message,
              fillTemplate(Kernel::Messages::releaseUpdateAvailable(),
                           {},
                           {Kernel::kindDisplayName(Kind::Mihomo), tag}));

    // 没装过（version 是 None）也算"可更新"。
    Kernel::Info fresh;
    fresh.kind = Kind::Mihomo;
    const Result<Kernel::ReleaseInfo> neverInstalled =
        KernelUpdater::releaseInfoFromRelease(release, fresh);
    ASSERT_TRUE(neverInstalled.isOk());
    EXPECT_TRUE(neverInstalled.value().updateAvailable);
    EXPECT_TRUE(neverInstalled.value().currentVersion.isNull());

    Release wrongAsset;
    wrongAsset.tagName = tag;
    wrongAsset.assets.push_back(makeAsset(QStringLiteral("mihomo-linux-amd64-v1.19.29.zip"),
                                          Q_UINT64_C(1),
                                          QString::fromLatin1(kGoodDigest)));
    const Result<Kernel::ReleaseInfo> unmatched =
        KernelUpdater::releaseInfoFromRelease(wrongAsset, fresh);
    ASSERT_TRUE(unmatched.isErr());
    EXPECT_EQ(unmatched.error(),
              fillTemplate(Kernel::Messages::releaseAssetMissing(),
                           {{QStringLiteral("{asset_name}"),
                             Kernel::kindAssetName(Kind::Mihomo, tag)}}));

    Release noDigest;
    noDigest.tagName = tag;
    noDigest.assets.push_back(
        makeAsset(Kernel::kindAssetName(Kind::Mihomo, tag), Q_UINT64_C(1), QString()));
    const Result<Kernel::ReleaseInfo> digestless =
        KernelUpdater::releaseInfoFromRelease(noDigest, fresh);
    ASSERT_TRUE(digestless.isErr());
    EXPECT_EQ(digestless.error(), Kernel::Messages::assetDigestMissing());
}

TEST(UpdaterKernel, archive_entry_names_block_path_traversal)
{
    // 对应 zip crate 的 `entry.enclosed_name()`：绝对路径和 `..` 一律不要。
    const QStringList rejected = {
        QStringLiteral("../evil.exe"),
        QStringLiteral("a/../../evil.exe"),
        QStringLiteral("/etc/passwd"),
        QStringLiteral("C:/Windows/x.exe"),
        QStringLiteral("\\\\host\\share\\x.exe"),
        QStringLiteral("."),
        QStringLiteral("./"),
        QString(),
    };
    for (const QString &name : rejected) {
        EXPECT_FALSE(enclosedNameComponents(name).has_value()) << name.toStdString();
    }

    const std::optional<QStringList> nested =
        enclosedNameComponents(QStringLiteral("windows/./mihomo.exe"));
    ASSERT_TRUE(nested.has_value());
    EXPECT_EQ(*nested, QStringList({QStringLiteral("windows"), QStringLiteral("mihomo.exe")}));
    const std::optional<QStringList> backslash = enclosedNameComponents(QStringLiteral("sub\\xray.exe"));
    ASSERT_TRUE(backslash.has_value());
    EXPECT_EQ(backslash->last(), QStringLiteral("xray.exe"));
}

TEST(UpdaterKernel, extract_reports_the_missing_executable_by_name)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QByteArray archive = makeZip({{QStringLiteral("README.md"), QByteArray("hi")}});
    ASSERT_FALSE(archive.isEmpty());

    const QString target = QDir(directory.path()).absoluteFilePath(stagingName(Kind::Mihomo));
    const Status extracted = extractExecutable(archive, Kind::Mihomo, target);
    ASSERT_TRUE(extracted.isErr());
    // Rust：`bail!("ZIP 中未找到 {}", kind.executable_name())`。
    EXPECT_EQ(extracted.error(),
              fillTemplate(Kernel::Messages::zipMissingExecutable(),
                           {},
                           {Kernel::kindExecutableName(Kind::Mihomo)}));
    EXPECT_FALSE(QFileInfo(target).exists());

    // 不是 ZIP 的东西报的是那句"打开内核 ZIP 失败"。
    const Status garbage =
        extractExecutable(QByteArray("this is not a zip file"), Kind::Mihomo, target);
    ASSERT_TRUE(garbage.isErr());
    EXPECT_EQ(garbage.error(), Kernel::Messages::zipOpenFailed());
}

TEST(UpdaterKernel, update_refuses_the_proxy_method_before_touching_the_network)
{
    // 「走代理」只有一条直连通道时，必须在建连接之前就报错，而不是悄悄退回直连。
    const KernelUpdater updater = directOnlyUpdater(QStringLiteral("unused"));
    const CancelToken cancelled;
    Recorded recorded;
    const Asset asset = makeAsset(QStringLiteral("mihomo-windows-amd64-v1.19.29.zip"),
                                  Q_UINT64_C(22492402),
                                  QString::fromLatin1(kGoodDigest));
    const Result<QByteArray> downloaded =
        updater.downloadAsset(Kind::Mihomo,
                              asset,
                              Source::Proxy,
                              0,
                              cancelled,
                              [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(downloaded.isErr());
    EXPECT_EQ(downloaded.error(),
              fillTemplate(Kernel::Messages::noChannelForSource(),
                           {},
                           {Kernel::sourceLabel(Source::Proxy)}));
    EXPECT_TRUE(recorded.stages.isEmpty());

    // 已经取消时，连通道判定都轮不到（Rust 第一件事就是 ensure_not_cancelled）。
    CancelToken cancelledAgain;
    cancelledAgain.cancel();
    const Result<QByteArray> aborted =
        updater.downloadAsset(Kind::Mihomo,
                              asset,
                              Source::GhProxy,
                              0,
                              cancelledAgain,
                              [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(aborted.isErr());
    EXPECT_EQ(aborted.error(), Kernel::Messages::kernelDownloadCancelled());
}

TEST(UpdaterKernel, install_backs_up_the_old_kernel_then_swaps_the_new_one_in)
{
    // 这条钉住 install_bytes 的落地顺序：解压到暂存 → 跑一次拿版本 → 备份旧内核 → 替换 →
    // 再跑一次；全程不联网，跑的是无害桩。
    const QString stub = harmlessVersionStub();
    if (stub.isEmpty()) {
        GTEST_SKIP() << "找不到可拷贝运行的版本桩，跳过安装链路";
    }
    const QByteArray stubBytes = readWholeFile(stub);
    ASSERT_FALSE(stubBytes.isEmpty());
    const QByteArray archive = makeZip({{QStringLiteral("mihomo.exe"), stubBytes}});
    ASSERT_FALSE(archive.isEmpty());

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    // 安装链路上的路径都是 Rust 的 `PathBuf::display()` 形态（原生分隔符），期望值也这么拼。
    const QString kindDir = joinNative(directory.path(), {QStringLiteral("mihomo")});
    const QString target = joinNative(kindDir, {QStringLiteral("mihomo.exe")});
    const QString backup = joinNative(kindDir, {QStringLiteral("mihomo.exe.backup")});
    const QString staging = joinNative(kindDir, {stagingName(Kind::Mihomo)});
    ASSERT_TRUE(writeStubFile(target, QByteArray("the old kernel")));

    const KernelUpdater updater(directory.path(), QVector<Transport>());
    const CancelToken cancelled;
    Recorded recorded;
    // expectedTag 传 None：正是"本地导入且对不上官方资产"时 Rust 走的那一支。
    const Result<Kernel::UpdateResult> result =
        updater.installBytes(Kind::Mihomo,
                            archive,
                            QString(),
                            cancelled,
                            [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(result.isOk()) << result.error().toStdString();

    const Result<QString> probed = Kernel::readKernelVersion(Kind::Mihomo, stub);
    ASSERT_TRUE(probed.isOk());
    EXPECT_EQ(result.value().version, probed.value());
    EXPECT_EQ(result.value().kind, Kind::Mihomo);
    EXPECT_EQ(result.value().path, target);
    EXPECT_EQ(result.value().backupPath, backup);
    EXPECT_EQ(result.value().message,
              fillTemplate(Kernel::Messages::kernelUpdatedTo(),
                           {},
                           {Kernel::kindDisplayName(Kind::Mihomo), result.value().version}));
    // 旧内核挪去了 .backup，正式路径现在是解出来的那一份。
    EXPECT_EQ(readWholeFile(backup), QByteArray("the old kernel"));
    EXPECT_EQ(readWholeFile(target), stubBytes);
    EXPECT_FALSE(QFileInfo(staging).exists());
    EXPECT_EQ(recorded.stages,
              QList<UpdateStage>({UpdateStage::Extracting, UpdateStage::Installing}));
    // 没有旧内核可备份时，backup_path 是 None（界面据此不显示"可回滚"）。
    ASSERT_TRUE(QFile::remove(target));
    ASSERT_TRUE(QFile::remove(backup));
    const Result<Kernel::UpdateResult> firstTime =
        updater.installBytes(Kind::Mihomo,
                            archive,
                            QString(),
                            cancelled,
                            [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(firstTime.isOk()) << firstTime.error().toStdString();
    EXPECT_TRUE(firstTime.value().backupPath.isNull());
}

TEST(UpdaterKernel, staged_version_mismatch_drops_the_staging_file_and_keeps_the_old_kernel)
{
    const QString stub = harmlessVersionStub();
    if (stub.isEmpty()) {
        GTEST_SKIP() << "找不到可拷贝运行的版本桩，跳过版本不匹配这条路";
    }
    const QByteArray archive = makeZip({{QStringLiteral("mihomo.exe"), readWholeFile(stub)}});
    ASSERT_FALSE(archive.isEmpty());

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    // 安装链路上的路径都是 Rust 的 `PathBuf::display()` 形态（原生分隔符），期望值也这么拼。
    const QString kindDir = joinNative(directory.path(), {QStringLiteral("mihomo")});
    const QString target = joinNative(kindDir, {QStringLiteral("mihomo.exe")});
    const QString backup = joinNative(kindDir, {QStringLiteral("mihomo.exe.backup")});
    const QString staging = joinNative(kindDir, {stagingName(Kind::Mihomo)});
    ASSERT_TRUE(writeStubFile(target, QByteArray("the old kernel")));

    const KernelUpdater updater(directory.path(), QVector<Transport>());
    const CancelToken cancelled;
    Recorded recorded;
    const QString expectedTag = QStringLiteral("v0.0.0-definitely-not-this-build");
    const Result<Kernel::UpdateResult> result =
        updater.installBytes(Kind::Mihomo,
                            archive,
                            expectedTag,
                            cancelled,
                            [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(result.isErr());
    const Result<QString> probed = Kernel::readKernelVersion(Kind::Mihomo, stub);
    ASSERT_TRUE(probed.isOk());
    // Rust：`bail!("暂存内核版本不匹配: {staged_version}")`，并且日志里写明丢弃暂存文件。
    EXPECT_EQ(result.error(),
              fillTemplate(Kernel::Messages::stagedVersionMismatch(),
                           {{QStringLiteral("{staged_version}"), probed.value()}}));
    EXPECT_FALSE(QFileInfo(staging).exists());
    // 版本没对上就还轮不到备份：正式路径与 .backup 都不许动。
    EXPECT_EQ(readWholeFile(target), QByteArray("the old kernel"));
    EXPECT_FALSE(QFileInfo(backup).exists());
    EXPECT_EQ(recorded.stages, QList<UpdateStage> {UpdateStage::Extracting});

    // 对上了就一路走到底（同一条 expectedTag 换成桩自己报的版本号）。
    const Result<Kernel::UpdateResult> matched =
        updater.installBytes(Kind::Mihomo,
                            archive,
                            QStringLiteral("v") + probed.value(),
                            cancelled,
                            [&](const UpdateProgress &progress) { record(&recorded, progress); });
    if (Kernel::versionMatches(probed.value(), QStringLiteral("v") + probed.value())) {
        ASSERT_TRUE(matched.isOk()) << matched.error().toStdString();
        EXPECT_TRUE(QFileInfo(backup).exists());
    } else {
        // 桩的第一行不是"版本号形状"时（where.exe 那种提示语），这一支必然仍判不匹配。
        EXPECT_TRUE(matched.isErr());
    }
}

TEST(UpdaterKernel, cancelled_token_stops_install_before_the_extraction_runs)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QByteArray archive = makeZip({{QStringLiteral("mihomo.exe"), QByteArray("junk")}});
    ASSERT_FALSE(archive.isEmpty());

    const KernelUpdater updater(directory.path(), QVector<Transport>());
    CancelToken cancelled;
    cancelled.cancel();
    Recorded recorded;
    const Result<Kernel::UpdateResult> result =
        updater.installBytes(Kind::Mihomo,
                            archive,
                            QString(),
                            cancelled,
                            [&](const UpdateProgress &progress) { record(&recorded, progress); });
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), Kernel::Messages::kernelDownloadCancelled());
    // 解压这一步的进度先报出去（与 Rust 的顺序一致），但文件一个都没落地。
    EXPECT_EQ(recorded.stages, QList<UpdateStage> {UpdateStage::Extracting});
    EXPECT_FALSE(QFileInfo(QDir(directory.path())
                               .absoluteFilePath(QStringLiteral("mihomo/mihomo.exe")))
                     .exists());
    EXPECT_FALSE(QFileInfo(QDir(directory.path())
                               .absoluteFilePath(QStringLiteral("mihomo/mihomo.staging.exe")))
                     .exists());
}

TEST(UpdaterKernel, local_install_without_a_file_says_pick_one_first)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const KernelUpdater updater(directory.path(), QVector<Transport>());
    const CancelToken cancelled;
    const Result<Kernel::UpdateResult> result =
        updater.update(Kind::Mihomo, Source::Local, QString(), cancelled, ProgressCallback());
    ASSERT_TRUE(result.isErr());
    // Rust：`local_file.ok_or_else(|| anyhow!("请先选择要导入的内核 zip"))?`
    EXPECT_EQ(result.error(), Kernel::Messages::localZipRequired());

    // 已经取消时先报取消，再说"没选文件"（Rust 顺序：ok_or_else 在 ensure_not_cancelled 之前）。
    CancelToken cancelled2;
    cancelled2.cancel();
    const Result<Kernel::UpdateResult> cancelledFirst =
        updater.update(Kind::Mihomo, Source::Local, QString(), cancelled2, ProgressCallback());
    ASSERT_TRUE(cancelledFirst.isErr());
    EXPECT_EQ(cancelledFirst.error(), Kernel::Messages::localZipRequired());
}

TEST(UpdaterKernel, progress_update_json_uses_the_serde_shape)
{
    // 进度对象要发给界面/API：阶段名是 snake_case，fraction 在 total=0 时给 0。
    UpdateProgress progress;
    progress.kind = Kind::SingBox;
    progress.stage = UpdateStage::Downloading;
    progress.downloaded = Q_UINT64_C(50);
    progress.total = Q_UINT64_C(200);
    EXPECT_FLOAT_EQ(progress.fraction(), 0.25F);

    const Json value = progress.toJson();
    EXPECT_EQ(value["stage"].get<std::string>(), "downloading");
    EXPECT_EQ(value["kind"].get<std::string>(), "sing_box");
    EXPECT_EQ(value["downloaded"].get<qint64>(), 50);
    EXPECT_EQ(UpdateProgress::fromJson(value).stage, UpdateStage::Downloading);
    EXPECT_EQ(UpdateProgress::fromJson(value).kind, Kind::SingBox);

    // total=0 给 0；超过 total 夹到 1（对应 Rust 的 clamp）。
    progress.total = 0;
    EXPECT_FLOAT_EQ(progress.fraction(), 0.0F);
    progress.total = Q_UINT64_C(10);
    progress.downloaded = Q_UINT64_C(40);
    EXPECT_FLOAT_EQ(progress.fraction(), 1.0F);

    // 阶段中文标签来自 updater.rs 的 KernelUpdateStage::label。
    EXPECT_EQ(Kernel::updateStageLabel(UpdateStage::Extracting), Kernel::Messages::stageExtracting());
    EXPECT_EQ(Kernel::updateStageLabel(UpdateStage::Checking), Kernel::Messages::stageChecking());
    EXPECT_EQ(Kernel::updateStageLabel(UpdateStage::Installing), Kernel::Messages::stageInstalling());
    EXPECT_EQ(Kernel::updateStageToText(UpdateStage::Verifying).toStdString(), "verifying");
}

TEST(UpdaterKernel, update_result_json_uses_the_serde_shape)
{
    Kernel::UpdateResult result;
    result.kind = Kind::Xray;
    result.version = QStringLiteral("Xray 26.3.27 (Xray)");
    result.path = QStringLiteral("C:\\data\\kernels\\xray\\xray.exe");
    const Json value = result.toJson();
    EXPECT_EQ(value["kind"].get<std::string>(), "xray");
    EXPECT_EQ(value["path"].get<std::string>(), "C:\\data\\kernels\\xray\\xray.exe");
    // backup_path 是 Option<PathBuf>：没备份就是 null，不是空串。
    ASSERT_TRUE(value.contains("backup_path"));
    EXPECT_TRUE(value["backup_path"].is_null());
    EXPECT_TRUE(Kernel::UpdateResult::fromJson(value).backupPath.isNull());
}

TEST(UpdaterKernel, compat_only_suffix_and_log_lines_come_from_the_generated_messages)
{
    // 这两句只在那台机器需要 CPU 兼容模式时出现，文案必须接在成功消息后面、且与 Rust 逐字一致。
    const QString base = fillTemplate(Kernel::Messages::kernelUpdatedTo(),
                                      {},
                                      {QStringLiteral("Mihomo"), QStringLiteral("v1.19.32")});
    const QString withSuffix = base + compatModeSuffix();
    EXPECT_TRUE(withSuffix.endsWith(compatModeSuffix()));
    EXPECT_TRUE(Kernel::Messages::logCompatOnly().contains(QStringLiteral("GODEBUG=cpu.all=off")));
    EXPECT_TRUE(Kernel::Messages::logCompatStillFails().contains(QStringLiteral("x.exe -v")));
    // 兜底那条日志里的两个来源标签也是生成的（长度取自 tools/gen-kernel-cjk-codepoints.mjs
    // 的报告：内置兜底 4 个码点、api.github.com/本地缓存 19 个码点）。
    EXPECT_EQ(Kernel::Messages::logSourceBuiltinFallback().size(), 4);
    EXPECT_EQ(Kernel::Messages::logSourceApiOrCache().size(), 19);
    EXPECT_TRUE(Kernel::Messages::logSourceApiOrCache()
                    .startsWith(QStringLiteral("api.github.com/")));
}
