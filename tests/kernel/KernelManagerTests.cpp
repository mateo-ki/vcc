#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <optional>

#include "core/Ids.h"
#include "core/JsonUtil.h"
#include "kernel/KernelManager.h"
#include "kernel/KernelMessages.h"

using namespace Kernel;

namespace {

/// 用户可见文本的期望值一律写成 UTF-8 十六进制：这些字节由 node 直接从 Rust 字面量
/// 推导（tools/gen-kernel-cjk-codepoints.mjs 是同一份来源的第二遍独立推导）。
/// 手打中文在这个环境里会被静默丢掉 astral 平面字符，所以测试里一个字都不写。
QString fromHex(const char *hex)
{
    return QString::fromUtf8(QByteArray::fromHex(QByteArray(hex)));
}

std::optional<int> code(int value)
{
    return std::optional<int>(value);
}

QString tempRoot(const QString &tag)
{
    const QString path = QDir(QDir::tempPath())
                             .absoluteFilePath(QStringLiteral("vcc-kernel-%1-%2")
                                                   .arg(tag, Ids::newUuidV4()));
    QDir().mkpath(path);
    return path;
}

void writeStubFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
    file.close();
}

const QList<Kind> kNone {};
const QList<Kind> kMihomoOnly {Kind::Mihomo};
const QList<Kind> kSingBoxOnly {Kind::SingBox};
const QList<Kind> kXrayOnly {Kind::Xray};

/// 对应 Rust 测试里的 `missing_with`。
std::optional<Kind> missingWith(const QString &protocol, Preference preference, bool systemMode,
                                const QList<Kind> &installed)
{
    return missingKernelFor(protocol, preference, systemMode,
                            [&installed](Kind kind) { return installed.contains(kind); });
}

Availability installedAre(const QList<Kind> &installed)
{
    const QList<Kind> copy = installed;
    return [copy](Kind kind) { return copy.contains(kind); };
}

QString native(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

/// 在 PATH 里找一个无害的桩可执行文件（测试只拿它验证探测管线，绝不碰真内核）。
QString findProgramInPath(const QString &name)
{
    const QStringList directories = qEnvironmentVariable("PATH").split(QLatin1Char(';'));
    for (const QString &directory : directories) {
        if (directory.isEmpty()) {
            continue;
        }
        const QString full = QDir(directory).absoluteFilePath(name);
        if (QFileInfo(full).isFile()) {
            return native(full);
        }
    }
    return QString();
}

/// `node -v` 打印一行版本号（就是 mihomo 的 `-v` 形状）后立刻退出，退出码 0。
QString harmlessVersionStub()
{
    return findProgramInPath(QStringLiteral("node.exe"));
}

}

// ================= Rust src/kernel/mod.rs 的内联测试，逐条搬过来 =================

TEST(KernelManager, version_probe_reports_why_the_kernel_stayed_silent)
{
    // 本机实测的正常输出（mihomo -v，go1.26.8 构建）。
    const QString good = QStringLiteral(
        "Mihomo Meta v1.19.32 windows amd64 with go1.26.8 Wed Sep 30 17:00:13 UTC 2026\n"
        "Use tags: with_gvisor\n");
    const Result<QString> ok = classifyVersionProbe(true, code(0), good);
    ASSERT_TRUE(ok.isOk());
    EXPECT_EQ(ok.value().toStdString(),
              "Mihomo Meta v1.19.32 windows amd64 with go1.26.8 Wed Sep 30 17:00:13 UTC 2026");

    // 崩掉：退出码 -1073741795 就是 0xC000001D，输出为空 —— 要能指名道姓。
    const Result<QString> crashed = classifyVersionProbe(false, code(-1073741795), QString());
    ASSERT_TRUE(crashed.isErr());
    EXPECT_NE(crashed.error().indexOf(QStringLiteral("0xC000001D")), -1);
    EXPECT_NE(crashed.error().indexOf(QStringLiteral("STATUS_ILLEGAL_INSTRUCTION")), -1);
    EXPECT_TRUE(crashed.error().contains(Messages::probeIllegalInstruction()));

    // 退出码不是已知那几种也得照实报数。
    const Result<QString> other = classifyVersionProbe(false, code(-1073741819), QString());
    ASSERT_TRUE(other.isErr());
    EXPECT_NE(other.error().indexOf(QStringLiteral("0xC0000005")), -1);
    EXPECT_TRUE(other.error().contains(Messages::probeAccessViolation()));

    const Result<QString> silent = classifyVersionProbe(true, code(0), QStringLiteral("\n  \n"));
    ASSERT_TRUE(silent.isErr());
    EXPECT_TRUE(silent.error().contains(Messages::probeSucceededWithoutOutput()));
    // 这句在 Rust 里就是整条消息，没有别的成分。
    EXPECT_EQ(silent.error(), Messages::probeSucceededWithoutOutput());

    // 0xC00000FD：三种已知码里的最后一种。
    const Result<QString> stack = classifyVersionProbe(false, code(-1073741571), QString());
    ASSERT_TRUE(stack.isErr());
    EXPECT_NE(stack.error().indexOf(QStringLiteral("STATUS_STACK_OVERFLOW")), -1);
    // 未知码不许带上任何说明文字。
    const Result<QString> unknownCode = classifyVersionProbe(false, code(1), QString());
    ASSERT_TRUE(unknownCode.isErr());
    EXPECT_NE(unknownCode.error().indexOf(QStringLiteral("0x00000001")), -1);
    EXPECT_FALSE(unknownCode.error().contains(Messages::probeStackOverflow()));
}

TEST(KernelManager, preflight_stays_out_of_the_way_when_a_usable_kernel_exists)
{
    EXPECT_FALSE(missingWith(QStringLiteral("ss"), Preference::Auto, true, kMihomoOnly)
                     .has_value());
    EXPECT_FALSE(missingWith(QStringLiteral("hysteria2"), Preference::Auto, true, kSingBoxOnly)
                     .has_value());
}

TEST(KernelManager, preflight_points_at_the_protocol_default_when_nothing_is_installed)
{
    const std::optional<Kind> ss = missingWith(QStringLiteral("ss"), Preference::Auto, true, kNone);
    ASSERT_TRUE(ss.has_value());
    EXPECT_EQ(*ss, Kind::Mihomo);

    const std::optional<Kind> hy2 =
        missingWith(QStringLiteral("hysteria2"), Preference::Auto, true, kNone);
    ASSERT_TRUE(hy2.has_value());
    EXPECT_EQ(*hy2, Kind::SingBox);
}

TEST(KernelManager, preflight_offers_the_named_kernel_even_if_another_one_would_work)
{
    // 用户点名 sing-box，机器上只有 mihomo：不能拿"能跑"糊过去，那不是我选的引擎。
    const std::optional<Kind> named =
        missingWith(QStringLiteral("ss"), Preference::SingBox, true, kMihomoOnly);
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(*named, Kind::SingBox);

    EXPECT_FALSE(
        missingWith(QStringLiteral("ss"), Preference::Mihomo, true, kMihomoOnly).has_value());
}

TEST(KernelManager, preflight_does_not_confuse_unsupported_with_not_installed)
{
    // Xray 在 TUN 下是"不支持"，不是"没装"：报这个就行，弹下载框反而误导。
    EXPECT_FALSE(missingWith(QStringLiteral("ss"), Preference::Xray, false, kNone).has_value());
    // 不认识的协议同理：该走"暂不支持协议"那条错。
    EXPECT_FALSE(
        missingWith(QStringLiteral("quantum-tunnel"), Preference::Auto, true, kNone).has_value());
    // 系统代理模式下点名 Xray 且没装，才该提示去下载。
    const std::optional<Kind> xrayMissing =
        missingWith(QStringLiteral("ss"), Preference::Xray, true, kNone);
    ASSERT_TRUE(xrayMissing.has_value());
    EXPECT_EQ(*xrayMissing, Kind::Xray);
}

TEST(KernelManager, kernel_kind_has_stable_display_names)
{
    EXPECT_EQ(kindDisplayName(Kind::Mihomo).toStdString(), "Mihomo");
    EXPECT_EQ(kindDisplayName(Kind::Xray).toStdString(), "Xray");
    EXPECT_EQ(kindDisplayName(Kind::SingBox).toStdString(), "Sing-box");
}

TEST(KernelManager, kernel_candidates_only_include_managed_paths)
{
    const QString executable = QStringLiteral(R"(C:\Tools\vpn-parser\vpn-parser.exe)");
    const QString workingDir = QStringLiteral(R"(D:\vpn-parser)");
    const QStringList candidates = kernelCandidates(Kind::Mihomo, executable, workingDir);

    ASSERT_EQ(candidates.size(), 2);
    EXPECT_TRUE(candidates.contains(QStringLiteral(R"(D:\vpn-parser\data\kernels\mihomo\mihomo.exe)")));
    // 顺序即优先级：工作目录那条在前。
    EXPECT_EQ(candidates.at(0).toStdString(),
              "D:\\vpn-parser\\data\\kernels\\mihomo\\mihomo.exe");
    EXPECT_EQ(candidates.at(1).toStdString(),
              "C:\\Tools\\vpn-parser\\data\\kernels\\mihomo\\mihomo.exe");
    for (const QString &candidate : candidates) {
        EXPECT_EQ(candidate.indexOf(QStringLiteral("v2rayN")), -1);
    }
    // Rust 的两个 Option 输入：任一个都可能缺席，缺席的那条就不生成。
    EXPECT_EQ(kernelCandidates(Kind::Mihomo, executable, QString()).size(), 1);
    EXPECT_EQ(kernelCandidates(Kind::Mihomo, QString(), workingDir).size(), 1);
    EXPECT_TRUE(kernelCandidates(Kind::Mihomo, QString(), QString()).isEmpty());
}

TEST(KernelManager, find_first_existing_candidate_returns_existing_file)
{
    const QString root = tempRoot(QStringLiteral("candidates"));
    const QString existing = QDir(root).absoluteFilePath(QStringLiteral("mihomo.exe"));
    writeStubFile(existing, QByteArray("test"));

    const std::optional<QString> result = findFirstExistingCandidate(
        {QDir(root).absoluteFilePath(QStringLiteral("missing.exe")), existing});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, existing);
    // 目录不是文件：Rust 的 is_file() 同样不接受目录。
    EXPECT_FALSE(findFirstExistingCandidate({root}).has_value());
    EXPECT_FALSE(findFirstExistingCandidate(QStringList()).has_value());

    QDir(root).removeRecursively();
}

TEST(KernelManager, parse_version_from_mihomo_output_keeps_first_nonempty_line)
{
    const QString output =
        QStringLiteral("\nMihomo Meta v1.19.13 windows amd64\nBuildTime: 2026-01-01");
    const std::optional<QString> version = parseVersionOutput(output);
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(version->toStdString(), "Mihomo Meta v1.19.13 windows amd64");
    EXPECT_FALSE(parseVersionOutput(QStringLiteral("\r\n  \r\n")).has_value());
    EXPECT_FALSE(parseVersionOutput(QString()).has_value());
    // xray / sing-box 的 `version` 输出形状。
    EXPECT_EQ(parseVersionOutput(QStringLiteral("Xray 26.3.27 (Xray, Penetrationscence unlocked)\n"
                                                "Window-x64\n"))
                  .value_or(QString())
                  .toStdString(),
              "Xray 26.3.27 (Xray, Penetrationscence unlocked)");
}

TEST(KernelManager, kernel_info_reports_missing_path)
{
    const Manager manager;
    const Info info = manager.infoForMissing(Kind::Mihomo);

    EXPECT_EQ(info.kind, Kind::Mihomo);
    EXPECT_FALSE(info.available);
    EXPECT_TRUE(info.path.isNull());
    EXPECT_TRUE(info.version.isNull());
    // "{} 未找到" 填上显示名后的字节必须和 Rust 一字不差。
    EXPECT_EQ(info.message, fromHex("4D69686F6D6F20E69CAAE689BEE588B0"));
    EXPECT_EQ(manager.infoForMissing(Kind::SingBox).message,
              fromHex("53696E672D626F7820E69CAAE689BEE588B0"));
}

TEST(KernelManager, each_kernel_has_version_command_arguments)
{
    EXPECT_EQ(kindVersionArgs(Kind::Mihomo), (QStringList {QStringLiteral("-v")}));
    EXPECT_EQ(kindVersionArgs(Kind::Xray), (QStringList {QStringLiteral("version")}));
    EXPECT_EQ(kindVersionArgs(Kind::SingBox), (QStringList {QStringLiteral("version")}));
}

TEST(KernelManager, recognizes_official_archive_executable_names)
{
    EXPECT_TRUE(kindMatchesArchiveExecutable(Kind::Mihomo, QStringLiteral("mihomo-windows-amd64.exe")));
    EXPECT_TRUE(kindMatchesArchiveExecutable(Kind::Xray, QStringLiteral("xray.exe")));
    EXPECT_TRUE(kindMatchesArchiveExecutable(Kind::SingBox, QStringLiteral("sing-box.exe")));
    EXPECT_FALSE(kindMatchesArchiveExecutable(Kind::Xray, QStringLiteral("wv2ray.exe")));
}

TEST(KernelManager, proxy_kernel_preferences_are_protocol_specific)
{
    for (const char *protocol : {"ssr", "hysteria", "vless", "vmess", "trojan", "ss"}) {
        const std::optional<Kind> preferred = preferredProxyKernel(QString::fromLatin1(protocol));
        ASSERT_TRUE(preferred.has_value()) << protocol;
        EXPECT_EQ(*preferred, Kind::Mihomo) << protocol;
    }
    for (const char *protocol : {"hysteria2", "hy2", "tuic", "anytls", "wireguard", "wg"}) {
        const std::optional<Kind> preferred = preferredProxyKernel(QString::fromLatin1(protocol));
        ASSERT_TRUE(preferred.has_value()) << protocol;
        EXPECT_EQ(*preferred, Kind::SingBox) << protocol;
    }
    EXPECT_FALSE(preferredProxyKernel(QStringLiteral("mieru")).has_value());
    // 大小写不敏感（Rust 走 to_ascii_lowercase）。
    EXPECT_EQ(preferredProxyKernel(QStringLiteral("VLess")).value_or(Kind::Xray), Kind::Mihomo);
}

TEST(KernelManager, proxy_kernel_selection_falls_back_only_to_compatible_installed_kernel)
{
    const Result<Kind> vless =
        selectProxyKernelWithAvailability(QStringLiteral("vless"), installedAre(kSingBoxOnly));
    ASSERT_TRUE(vless.isOk());
    EXPECT_EQ(vless.value(), Kind::SingBox);

    EXPECT_TRUE(
        selectProxyKernelWithAvailability(QStringLiteral("ssr"), installedAre(kSingBoxOnly)).isErr());

    const Result<Kind> hy2 =
        selectProxyKernelWithAvailability(QStringLiteral("hysteria2"), installedAre(kMihomoOnly));
    ASSERT_TRUE(hy2.isOk());
    // 首选 sing-box 不在，退到仍支持该协议的 mihomo。
    EXPECT_EQ(hy2.value(), Kind::Mihomo);

    EXPECT_TRUE(selectProxyKernelWithAvailability(QStringLiteral("mieru"), installedAre(kNone))
                    .isErr());
    EXPECT_TRUE(
        selectProxyKernelWithAvailability(QStringLiteral("mieru"), [](Kind) { return true; })
            .isErr());

    // wireguard 只有 sing-box 认：装了 xray 也不许退让它。
    EXPECT_TRUE(selectProxyKernelWithAvailability(QStringLiteral("wireguard"),
                                                 installedAre(kXrayOnly))
                    .isErr());
    const Result<Kind> wg = selectProxyKernelWithAvailability(QStringLiteral("wireguard"),
                                                              installedAre(kSingBoxOnly));
    ASSERT_TRUE(wg.isOk());
    EXPECT_EQ(wg.value(), Kind::SingBox);

    // 错误消息与 Rust 逐字一致。
    const Result<Kind> unknown =
        selectProxyKernelWithAvailability(QStringLiteral("mieru"), [](Kind) { return true; });
    ASSERT_TRUE(unknown.isErr());
    EXPECT_EQ(unknown.error(), fromHex("E69A82E4B88DE694AFE68C81E58D8FE8AEAE3A206D69657275"));
    const Result<Kind> notInstalled =
        selectProxyKernelWithAvailability(QStringLiteral("ssr"), installedAre(kSingBoxOnly));
    ASSERT_TRUE(notInstalled.isErr());
    EXPECT_EQ(notInstalled.error(),
              fromHex("E6B2A1E69C89E5B7B2E5AE89E8A385E4B894E694AFE68C812073737220E79A84E4BBA3E79086"
                      "E58685E6A0B8"));
}

TEST(KernelManager, xray_support_is_limited_to_system_proxy_protocols)
{
    for (const char *protocol : {"ss", "vmess", "vless", "trojan", "http", "socks5"}) {
        EXPECT_TRUE(kindSupportsProxyProtocol(Kind::Xray, QString::fromLatin1(protocol)))
            << protocol;
    }
    for (const char *protocol : {"hysteria2", "tuic", "anytls", "wireguard"}) {
        EXPECT_FALSE(kindSupportsProxyProtocol(Kind::Xray, QString::fromLatin1(protocol)))
            << protocol;
    }
}

#ifdef Q_OS_WIN
TEST(KernelManager, managed_kernel_paths_use_native_separators)
{
    const QString text = managedKernelRoot();
    EXPECT_EQ(text.indexOf(QStringLiteral("data/kernels")), -1) << text.toStdString();
    EXPECT_TRUE(text.endsWith(QStringLiteral("data\\kernels"))) << text.toStdString();
}
#endif

// ================= 发现顺序：find_kernel_executable 的三条分支 =================

TEST(KernelManager, environment_override_wins_over_managed_directories)
{
    const QString root = tempRoot(QStringLiteral("discovery"));
    const QString override = QDir(root).absoluteFilePath(QStringLiteral("custom/mihomo.exe"));
    const QString workDir = QDir(root).absoluteFilePath(QStringLiteral("work"));
    const QString managed =
        QDir(workDir).absoluteFilePath(QStringLiteral("data/kernels/mihomo/mihomo.exe"));
    const QString exe = QDir(root).absoluteFilePath(QStringLiteral("bin/vpn-parser.exe"));
    writeStubFile(override, QByteArray("override"));
    writeStubFile(managed, QByteArray("managed"));
    writeStubFile(exe, QByteArray("app"));

    const std::optional<QString> found =
        findKernelExecutableWith(override, exe, workDir, Kind::Mihomo);
    ASSERT_TRUE(found.has_value());
    // 环境变量那条原样返回，不做任何规范化（Rust 是 var_os → PathBuf::from）。
    EXPECT_EQ(*found, override);

    QDir(root).removeRecursively();
}

TEST(KernelManager, working_directory_candidate_wins_over_executable_directory)
{
    const QString root = tempRoot(QStringLiteral("discovery-order"));
    const QString workDir = QDir(root).absoluteFilePath(QStringLiteral("work"));
    const QString fromCwd =
        QDir(workDir).absoluteFilePath(QStringLiteral("data/kernels/sing-box/sing-box.exe"));
    const QString exe = QDir(root).absoluteFilePath(QStringLiteral("app/vpn-parser.exe"));
    const QString fromExeDir =
        QDir(QFileInfo(exe).path())
            .absoluteFilePath(QStringLiteral("data/kernels/sing-box/sing-box.exe"));
    writeStubFile(fromCwd, QByteArray("cwd"));
    writeStubFile(exe, QByteArray("app"));
    writeStubFile(fromExeDir, QByteArray("exe"));

    const std::optional<QString> found =
        findKernelExecutableWith(QString(), exe, workDir, Kind::SingBox);
    ASSERT_TRUE(found.has_value());
    // 候选是按分量拼出来的原生分隔符路径（Rust 的 PathBuf::join 同样如此）。
    EXPECT_EQ(*found, native(fromCwd));

    // 工作目录那份不存在时，才轮到 exe 目录那条。
    const std::optional<QString> onlyExeDir = findKernelExecutableWith(
        QString(), exe, QDir(root).absoluteFilePath(QStringLiteral("nowhere")), Kind::SingBox);
    ASSERT_TRUE(onlyExeDir.has_value());
    EXPECT_EQ(*onlyExeDir, native(fromExeDir));

    QDir(root).removeRecursively();
}

TEST(KernelManager, unusable_environment_override_falls_back_to_managed_paths)
{
    const QString root = tempRoot(QStringLiteral("discovery-env"));
    const QString workDir = QDir(root).absoluteFilePath(QStringLiteral("work"));
    const QString managed =
        QDir(workDir).absoluteFilePath(QStringLiteral("data/kernels/xray/xray.exe"));
    writeStubFile(managed, QByteArray("managed"));

    const std::optional<QString> missingOverride = findKernelExecutableWith(
        QDir(root).absoluteFilePath(QStringLiteral("nowhere/xray.exe")), QString(), workDir,
        Kind::Xray);
    ASSERT_TRUE(missingOverride.has_value());
    EXPECT_EQ(*missingOverride, native(managed));

    // 环境变量指向目录也不接受（Rust 用 is_file()）。
    EXPECT_FALSE(findKernelExecutableWith(workDir, QString(),
                                          QDir(root).absoluteFilePath(QStringLiteral("nowhere")),
                                          Kind::Xray)
                     .has_value());

    QDir(root).removeRecursively();
}

TEST(KernelManager, no_candidate_resolves_when_nothing_is_installed)
{
    const QString root = tempRoot(QStringLiteral("discovery-empty"));
    EXPECT_FALSE(findKernelExecutableWith(QString(),
                                          QDir(root).absoluteFilePath(QStringLiteral("vpn-parser.exe")),
                                          root, Kind::Mihomo)
                     .has_value());
    EXPECT_FALSE(
        findKernelExecutableWith(QString(), QString(), QString(), Kind::Mihomo).has_value());
    QDir(root).removeRecursively();
}

TEST(KernelManager, environment_override_reads_the_kernel_specific_variable)
{
    const QString root = tempRoot(QStringLiteral("discovery-live-env"));
    const QString override = QDir(root).absoluteFilePath(QStringLiteral("env/mihomo.exe"));
    writeStubFile(override, QByteArray("override"));

    qputenv("MIHOMO_PATH", override.toUtf8());
    const std::optional<QString> found = findKernelExecutable(Kind::Mihomo);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, override);
    // 每个内核读自己那个变量：XRAY_PATH 没设置时不受影响。
    EXPECT_TRUE(environmentOverride(Kind::Xray).isNull());
    EXPECT_EQ(kindEnvironmentVariable(Kind::Mihomo).toStdString(), "MIHOMO_PATH");
    EXPECT_EQ(kindEnvironmentVariable(Kind::Xray).toStdString(), "XRAY_PATH");
    EXPECT_EQ(kindEnvironmentVariable(Kind::SingBox).toStdString(), "SING_BOX_PATH");

    qputenv("MIHOMO_PATH", QByteArray());
    QDir(root).removeRecursively();
}

#ifdef Q_OS_WIN
TEST(KernelManager, installed_kernel_layout_resolves_under_the_app_data_directory)
{
    // 只读校验装机目录：绝不执行那里任何二进制。
    const QString appDir = QStringLiteral(R"(D:\software\VPN Parser)");
    const QString installed = QDir(appDir)
                                  .absoluteFilePath(QStringLiteral("data/kernels/mihomo/mihomo.exe"));
    if (!QFileInfo(installed).isFile()) {
        GTEST_SKIP() << "这台机器上没有装机目录";
    }
    const QString exe = QDir(appDir).absoluteFilePath(QStringLiteral("vpn-parser.exe"));
    const QStringList candidates = kernelCandidates(Kind::Mihomo, exe, appDir);
    ASSERT_EQ(candidates.size(), 2);
    for (const QString &candidate : candidates) {
        EXPECT_EQ(candidate, QDir::toNativeSeparators(installed));
        EXPECT_TRUE(QFileInfo(candidate).isFile());
    }
    // sing-box / xray 没装：候选路径形状一致，但文件不在。
    const QStringList singBox = kernelCandidates(Kind::SingBox, exe, appDir);
    ASSERT_FALSE(singBox.isEmpty());
    EXPECT_TRUE(singBox.at(0).endsWith(QStringLiteral("sing-box\\sing-box.exe")));
    EXPECT_FALSE(QFileInfo(singBox.at(0)).isFile());
}
#endif

// ================= 版本探测：指向无害的桩，绝不跑真内核 =================

TEST(KernelManager, version_probe_fails_with_the_execute_error_wording)
{
    const QString root = tempRoot(QStringLiteral("probe-not-pe"));
    const QString fake = QDir(root).absoluteFilePath(QStringLiteral("mihomo.exe"));
    writeStubFile(fake, QByteArray("not a windows executable"));

    const Result<QString> result = readKernelVersion(Kind::Mihomo, fake);
    ASSERT_TRUE(result.isErr());
    // Rust：`.with_context(|| format!("执行 {} 失败", path.display()))`，anyhow 的 Display
    // 只打印最外层那一句，所以这里就是「执行 <路径> 失败」。
    EXPECT_EQ(result.error(), fromHex("E689A7E8A18C20") + fake + fromHex("20E5A4B1E8B4A5"));

    const QString missing = QStringLiteral("Z:\\nope\\xray.exe");
    const Result<QString> absent = readKernelVersion(Kind::Xray, missing);
    ASSERT_TRUE(absent.isErr());
    EXPECT_EQ(absent.error(), fromHex("E689A7E8A18C20") + missing + fromHex("20E5A4B1E8B4A5"));

    QDir(root).removeRecursively();
}

TEST(KernelManager, version_probe_returns_the_first_line_of_a_stub)
{
    const QString stub = harmlessVersionStub();
    if (stub.isEmpty()) {
        GTEST_SKIP() << "PATH 里找不到 node.exe，跳过桩探测";
    }
    // `node -v` 与 mihomo 的 `-v` 同形：打印一行版本就退出。
    const Result<QString> result = readKernelVersion(Kind::Mihomo, stub);
    ASSERT_TRUE(result.isOk()) << result.error().toStdString();
    EXPECT_FALSE(result.value().isEmpty());
    // 取的就是第一行非空输出（已 trim）。
    EXPECT_EQ(result.value(), result.value().trimmed());
    EXPECT_EQ(parseVersionOutput(QStringLiteral("v24.0.0\n")).value_or(QString()).toStdString(),
              "v24.0.0");

    // GODEBUG 注入不影响结果形状：本机实测 cpu.all=off 被接受且输出逐字不变。
    const Result<QString> withEnv = readKernelVersionWithEnv(Kind::Mihomo, stub, godebugCompat());
    ASSERT_TRUE(withEnv.isOk()) << withEnv.error().toStdString();
    EXPECT_EQ(withEnv.value(), result.value());
}

TEST(KernelManager, detect_at_path_covers_missing_and_broken_kernels)
{
    const Manager manager;

    const Info missing = manager.detectAtPath(Kind::SingBox, std::nullopt);
    EXPECT_EQ(missing.kind, Kind::SingBox);
    EXPECT_FALSE(missing.available);
    EXPECT_TRUE(missing.path.isNull());
    EXPECT_TRUE(missing.version.isNull());

    const QString root = tempRoot(QStringLiteral("detect"));
    const QString fake = QDir(root).absoluteFilePath(QStringLiteral("mihomo.exe"));
    writeStubFile(fake, QByteArray("not a windows executable"));

    // 文件在盘上但跑不起来：available 必须是 false（Rust 特意纠正过这一点）。
    const Info broken = manager.detectAtPath(Kind::Mihomo, std::optional<QString>(fake));
    EXPECT_EQ(broken.kind, Kind::Mihomo);
    EXPECT_FALSE(broken.available);
    EXPECT_EQ(broken.path, fake);
    EXPECT_TRUE(broken.version.isNull());
    EXPECT_TRUE(broken.message.startsWith(fromHex(
        "4D69686F6D6F20E8A385E4BA86E4BD86E8B791E4B88DE8B5B7E69DA53A20")));
    EXPECT_TRUE(broken.message.contains(
        fromHex("E689A7E8A18C20") + fake + fromHex("20E5A4B1E8B4A5")));

    QDir(root).removeRecursively();

    const QString stub = harmlessVersionStub();
    if (!stub.isEmpty()) {
        const Info running = manager.detectAtPath(Kind::Mihomo, std::optional<QString>(stub));
        EXPECT_TRUE(running.available) << running.message.toStdString();
        EXPECT_EQ(running.path, stub);
        EXPECT_EQ(running.message, running.version);
    }
}

TEST(KernelManager, select_proxy_kernel_preference_errors_keep_the_rust_wording)
{
    // Xray 点名 + TUN：直接拒，不看装没装。
    const Result<Kind> tun = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::Xray, false, installedAre(kNone));
    ASSERT_TRUE(tun.isErr());
    EXPECT_EQ(tun.error(), fromHex("5872617920E69A82E4B88DE694AFE68C812054554E20E6A8A1E5BC8F"));

    // 支持协议但点名内核不认这个协议。
    const Result<Kind> unsupported = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("hysteria2"), Preference::Xray, true, installedAre(kXrayOnly));
    ASSERT_TRUE(unsupported.isErr());
    EXPECT_EQ(unsupported.error(),
              fromHex("5872617920E4B88DE694AFE68C81E58D8FE8AEAE3A20687973746572696132"));

    // 支持但没装。
    const Result<Kind> absent = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::Mihomo, true, installedAre(kNone));
    ASSERT_TRUE(absent.isErr());
    EXPECT_EQ(absent.error(), fromHex("E69CAAE689BEE588B0204D69686F6D6F20E58FAFE689A7E8A18CE69687E4BBB6"));

    // 装了就点名返回它，Auto 的退让不许插进来。
    const Result<Kind> ok = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::Mihomo, true, installedAre(kMihomoOnly));
    ASSERT_TRUE(ok.isOk());
    EXPECT_EQ(ok.value(), Kind::Mihomo);

    const Result<Kind> namedSingBox = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::SingBox, true, installedAre(kSingBoxOnly));
    ASSERT_TRUE(namedSingBox.isOk());
    EXPECT_EQ(namedSingBox.value(), Kind::SingBox);

    // Auto：先按协议挑，挑不到才考虑 Xray（且必须允许系统代理模式）。
    const Result<Kind> autoFallback = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::Auto, true, installedAre(kXrayOnly));
    ASSERT_TRUE(autoFallback.isOk());
    EXPECT_EQ(autoFallback.value(), Kind::Xray);

    // Rust 的实际行为：`select_proxy_kernel` 的候选表本身就是 [首选, 退让, Xray]，
    // Xray 支持 ss 且装着时，TUN 模式也会挑中它 —— `allow_xray` 只挡住"点名 Xray"和
    // "select 失败后的补一轮"这两条路。这里照搬，不做改进。
    const Result<Kind> autoTunOff = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::Auto, false, installedAre(kXrayOnly));
    ASSERT_TRUE(autoTunOff.isOk()) << autoTunOff.error().toStdString();
    EXPECT_EQ(autoTunOff.value(), Kind::Xray);

    // 一个都没装才是「没有已安装且支持 …」那条错。
    const Result<Kind> nothingInstalled = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("ss"), Preference::Auto, true, installedAre(kNone));
    ASSERT_TRUE(nothingInstalled.isErr());
    EXPECT_EQ(nothingInstalled.error(),
              fromHex("E6B2A1E69C89E5B7B2E5AE89E8A385E4B894E694AFE68C8120737320E79A84E4BBA3E79086E5"
                      "8685E6A0B8"));

    // 不认识的协议 + Auto：Rust 把 `select_proxy_kernel` 的「暂不支持协议」丢掉，
    // 只报最后那条「没有已安装且支持 …」——「暂不支持协议」只有 select_*_with 才会传出去
    // （见 proxy_kernel_selection_falls_back_only_to_compatible_installed_kernel）。
    const Result<Kind> unknown = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("mieru"), Preference::Auto, true, installedAre(kNone));
    ASSERT_TRUE(unknown.isErr());
    EXPECT_EQ(unknown.error(),
              fromHex("E6B2A1E69C89E5B7B2E5AE89E8A385E4B894E694AFE68C81206D6965727520E79A84E4BBA3"
                      "E79086E58685E6A0B8"));

    // 点名内核 + 不认识的协议：那条错由点名分支给出，带显示名。
    const Result<Kind> namedUnknown = selectProxyKernelPreferenceWithAvailability(
        QStringLiteral("mieru"), Preference::Mihomo, true, installedAre(kMihomoOnly));
    ASSERT_TRUE(namedUnknown.isErr());
    EXPECT_EQ(namedUnknown.error(),
              fromHex("4D69686F6D6F20E4B88DE694AFE68C81E58D8FE8AEAE3A206D69657275"));
}

TEST(KernelManager, kernel_info_json_uses_the_serde_field_names)
{
    Info info;
    info.kind = Kind::Mihomo;
    info.message = fromHex("4D69686F6D6F20E69CAAE689BEE588B0");
    const QString expected =
        QStringLiteral("{\"available\":false,\"kind\":\"mihomo\",\"message\":\"")
        + info.message + QStringLiteral("\",\"path\":null,\"version\":null}");
    EXPECT_EQ(JsonUtil::dump(info.toJson()).toStdString(), expected.toStdString());

    info.path = QStringLiteral("/tmp/mihomo.exe");
    info.version = QStringLiteral("v1.19.32");
    info.available = true;
    const Info back = Info::fromJson(JsonUtil::parseOrEmptyObject(JsonUtil::dump(info.toJson())));
    EXPECT_EQ(back.kind, Kind::Mihomo);
    EXPECT_EQ(back.path.toStdString(), "/tmp/mihomo.exe");
    EXPECT_EQ(back.version.toStdString(), "v1.19.32");
    EXPECT_TRUE(back.available);
    EXPECT_EQ(back.message, info.message);

    // sing_box 的 snake_case 取值 + null QString 写成 JSON null。
    Info singBox;
    singBox.kind = Kind::SingBox;
    EXPECT_TRUE(JsonUtil::dump(singBox.toJson()).contains("sing_box"));
    EXPECT_TRUE(JsonUtil::dump(singBox.toJson()).contains("\"path\":null"));
}

TEST(KernelManager, check_update_fallback_keeps_the_current_version)
{
    Info info;
    info.kind = Kind::SingBox;
    info.version = QStringLiteral("1.13.13");

    const ReleaseInfo fallback =
        releaseInfoFromCheck(Result<ReleaseInfo>::withError(QStringLiteral("timeout")), info);
    EXPECT_EQ(fallback.kind, Kind::SingBox);
    EXPECT_EQ(fallback.currentVersion.toStdString(), "1.13.13");
    EXPECT_TRUE(fallback.latestVersion.isNull());
    EXPECT_TRUE(fallback.assetName.isNull());
    EXPECT_FALSE(fallback.updateAvailable);
    EXPECT_EQ(fallback.message,
              fromHex("E6A380E69FA5E69BB4E696B0E8B685E697B6E68896E58F97E999903A2074696D656F7574"));

    ReleaseInfo good;
    good.kind = Kind::Xray;
    good.message = QStringLiteral("ok");
    const ReleaseInfo passed = releaseInfoFromCheck(Result<ReleaseInfo>::withValue(good), info);
    EXPECT_EQ(passed.kind, Kind::Xray);
    EXPECT_EQ(passed.message.toStdString(), "ok");
}

TEST(KernelManager, godebug_compat_is_the_go_cpu_feature_switch)
{
    ASSERT_EQ(godebugCompat().size(), 1);
    EXPECT_EQ(godebugCompat().at(0).first.toStdString(), "GODEBUG");
    EXPECT_EQ(godebugCompat().at(0).second.toStdString(), "cpu.all=off");
}

// ================= KernelSource（mod.rs 里 pub use 出去的下载方式）=================

TEST(KernelSource, source_ids_round_trip_and_legacy_values_fall_back)
{
    ASSERT_EQ(sourceOptions().size(), 5);
    for (const SourceOption &option : sourceOptions()) {
        EXPECT_EQ(sourceFromId(option.id), option.source);
        EXPECT_EQ(sourceId(sourceFromId(option.id)).toStdString(), option.id.toStdString());
    }
    // 库里可能还留着旧值 "race"（三家并发抢跑那一版）：落到实测最快的那一家 gh-proxy，
    // 既不是"什么都不下"，也不会让弹框因为对不上 id 而画不出选中态。
    EXPECT_EQ(sourceFromId(QStringLiteral("race")), Source::GhProxy);
    EXPECT_EQ(sourceFromId(QStringLiteral("no-such-source")), Source::GhProxy);
    EXPECT_EQ(sourceId(sourceFromId(QString())).toStdString(), "gh-proxy");
    // settings.kernel_source 的默认值和 schema 里的 DEFAULT 'gh-proxy' 必须对得上。
    EXPECT_EQ(sourceId(Source::GhProxy).toStdString(), "gh-proxy");

    const QStringList ids {QStringLiteral("gh-proxy"), QStringLiteral("ghfast"),
                           QStringLiteral("official"), QStringLiteral("proxy"),
                           QStringLiteral("local")};
    QStringList actual;
    for (const SourceOption &option : sourceOptions()) {
        actual.push_back(option.id);
    }
    EXPECT_EQ(actual, ids);
}

TEST(KernelSource, each_download_method_resolves_to_exactly_one_url)
{
    const QString direct = QStringLiteral(
        "https://github.com/MetaCubeX/mihomo/releases/download/v1.19.29/"
        "mihomo-windows-amd64-v3-v1.19.29.zip");
    EXPECT_EQ(assetUrlFor(Source::GhProxy, direct).toStdString(),
              "https://gh-proxy.com/" + direct.toStdString());
    EXPECT_EQ(assetUrlFor(Source::GhFast, direct).toStdString(),
              "https://ghfast.top/" + direct.toStdString());
    for (Source source : {Source::Official, Source::Proxy, Source::Local}) {
        EXPECT_EQ(assetUrlFor(source, direct), direct);
    }
    EXPECT_EQ(sourceMirrorPrefix(Source::Official), std::nullopt);
    EXPECT_EQ(sourceMirrorPrefix(Source::GhProxy).value_or(QString()).toStdString(),
              "https://gh-proxy.com/");
    EXPECT_EQ(assetMirrorPrefixes().size(), 2);
}

TEST(KernelSource, one_channel_at_a_time_gates_proxy_versus_direct)
{
    const QString direct = Messages::directTransport();
    const QString proxied = QStringLiteral("http://127.0.0.1:9980");
    // 镜像站本身就在国内：两个镜像方式和官方直连都只允许不经代理那一条。
    for (Source source : {Source::Official, Source::GhProxy, Source::GhFast}) {
        EXPECT_TRUE(sourceAllowsTransport(source, direct));
        EXPECT_FALSE(sourceAllowsTransport(source, proxied));
    }
    // 「走代理」不许退回直连。
    EXPECT_FALSE(sourceAllowsTransport(Source::Proxy, direct));
    EXPECT_TRUE(sourceAllowsTransport(Source::Proxy, proxied));
    // 本地导入不联网，通道无所谓。
    EXPECT_TRUE(sourceAllowsTransport(Source::Local, direct));
    EXPECT_TRUE(sourceAllowsTransport(Source::Local, proxied));
}

TEST(KernelSource, labels_and_stage_names_come_from_the_rust_table)
{
    ASSERT_EQ(sourceOptions().size(), 5);
    EXPECT_EQ(sourceOptions().at(0).label, fromHex("67682D70726F787920E9959CE5838F"));
    EXPECT_EQ(sourceOptions().at(1).label, fromHex("67686661737420E9959CE5838F"));
    EXPECT_EQ(sourceOptions().at(2).label, fromHex("E5AE98E696B9E79BB4E8BF9E"));
    EXPECT_EQ(sourceOptions().at(3).label, fromHex("E8B5B0E4BBA3E79086"));
    EXPECT_EQ(sourceOptions().at(4).label, fromHex("E69CACE59CB0E69687E4BBB6E5AFBCE585A5"));
    EXPECT_EQ(sourceLabel(Source::GhFast), fromHex("67686661737420E9959CE5838F"));
    EXPECT_EQ(Messages::sourceLabelFallback(), fromHex("67682D70726F787920E9959CE5838F"));
    // hint 不为空（下载弹框要显示实测速度）。
    for (const SourceOption &option : sourceOptions()) {
        EXPECT_FALSE(option.hint.isEmpty()) << option.id.toStdString();
    }
    // 阶段标签（下载弹框那五个词）。
    EXPECT_EQ(updateStageLabel(UpdateStage::Checking), fromHex("E6A380E69FA5E78988E69CAC"));
    EXPECT_EQ(updateStageLabel(UpdateStage::Downloading), fromHex("E4B88BE8BDBD"));
    EXPECT_EQ(updateStageLabel(UpdateStage::Verifying), fromHex("E6A0A1E9AA8C"));
    EXPECT_EQ(updateStageLabel(UpdateStage::Extracting), fromHex("E8A7A3E58E8B"));
    EXPECT_EQ(updateStageLabel(UpdateStage::Installing), fromHex("E5AE89E8A385"));
    EXPECT_EQ(Messages::directTransport(), fromHex("E79BB4E8BF9E"));
}

// ================= 更新元数据类型 =================

TEST(KernelRelease, matches_release_tag_in_version_output)
{
    EXPECT_TRUE(versionMatches(QStringLiteral("Mihomo Meta v1.19.29 windows amd64"),
                               QStringLiteral("v1.19.29")));
    EXPECT_TRUE(versionMatches(QStringLiteral("Xray 26.3.27 (Xray)"), QStringLiteral("v26.3.27")));
    EXPECT_FALSE(versionMatches(QStringLiteral("sing-box version 1.13.13"),
                                 QStringLiteral("v1.13.14")));
}

TEST(KernelRelease, json_field_names_match_serde)
{
    ReleaseInfo release;
    release.kind = Kind::Xray;
    release.updateAvailable = true;
    const Json value = release.toJson();
    for (const char *key : {"kind", "current_version", "latest_version", "update_available",
                            "asset_name", "message"}) {
        EXPECT_TRUE(value.contains(key)) << key;
    }
    EXPECT_EQ(JsonUtil::dump(value).toStdString(),
              "{\"asset_name\":null,\"current_version\":null,\"kind\":\"xray\","
              "\"latest_version\":null,\"message\":\"\",\"update_available\":true}");
    const ReleaseInfo back = ReleaseInfo::fromJson(value);
    EXPECT_EQ(back.kind, Kind::Xray);
    EXPECT_TRUE(back.updateAvailable);

    UpdateResult result;
    result.kind = Kind::SingBox;
    result.version = QStringLiteral("1.13.13");
    result.path = QStringLiteral("C:/kernels/sing-box.exe");
    const Json resultJson = result.toJson();
    for (const char *key : {"kind", "version", "path", "backup_path", "message"}) {
        EXPECT_TRUE(resultJson.contains(key)) << key;
    }
    const UpdateResult resultBack = UpdateResult::fromJson(resultJson);
    EXPECT_EQ(resultBack.kind, Kind::SingBox);
    EXPECT_EQ(resultBack.version.toStdString(), "1.13.13");
    EXPECT_EQ(resultBack.path.toStdString(), "C:/kernels/sing-box.exe");
    EXPECT_TRUE(resultBack.backupPath.isNull());

    UpdateProgress progress;
    progress.kind = Kind::Mihomo;
    progress.stage = UpdateStage::Downloading;
    progress.downloaded = 30;
    progress.total = 120;
    EXPECT_FLOAT_EQ(progress.fraction(), 0.25F);
    progress.total = 0;
    EXPECT_FLOAT_EQ(progress.fraction(), 0.0F);
    progress.downloaded = 200;
    progress.total = 100;
    EXPECT_FLOAT_EQ(progress.fraction(), 1.0F);
    const Json progressJson = progress.toJson();
    EXPECT_TRUE(progressJson.contains("bytes_per_second"));
    EXPECT_EQ(JsonUtil::textOrEmpty(progressJson, "stage").toStdString(), "downloading");
    EXPECT_EQ(UpdateProgress::fromJson(progressJson).stage, UpdateStage::Downloading);
    EXPECT_EQ(updateStageToText(UpdateStage::Extracting).toStdString(), "extracting");
}
