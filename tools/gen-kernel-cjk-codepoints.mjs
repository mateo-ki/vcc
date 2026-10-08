import fs from 'node:fs';

// 从 Rust 源里抽取内核模块的中文提示文本，生成纯 ASCII 的 C++ 码点表：
//   node tools/gen-kernel-cjk-codepoints.mjs
// 这些文本是用户可见的错误消息（以及下载弹框的文案），必须与 vpn-parser 逐字一致，
// 所以不允许手打——本环境会静默丢掉 astral 平面的字符。
//
// 与 gen-cjk-codepoints.mjs 的差别：这里用 ASCII 锚点 + **精确行偏移** 定位字面量，
// 找到 0 个、多于 1 个或非 ASCII 模式下的纯 ASCII 都直接报错退出，绝不猜。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

// [name, file, anchor, occurrence, offset, mode, pick]
// pick = 同一行里第几个非 ASCII 字面量；不给时要求该行恰好有 1 个。
const entries = [
  // ---- kernel/mod.rs：选择内核与版本探测的错误 ----
  ['unsupportedProtocol', 'kernel/mod.rs', 'ok_or_else(|| anyhow::anyhow!("', 0, 0],
  ['noInstalledKernelSupporting', 'kernel/mod.rs', 'Err(anyhow::anyhow!("', 0, 0],
  ['xrayNoTunMode', 'kernel/mod.rs', 'Err(anyhow::anyhow!("', 1, 0],
  ['kernelDoesNotSupportProtocol', 'kernel/mod.rs', 'if !kind.supports_proxy_protocol(protocol) {', 0, 2],
  ['kernelExecutableNotFound', 'kernel/mod.rs', 'Err(anyhow::anyhow!("', 2, 0],
  ['availableWithoutVersion', 'kernel/mod.rs', 'unwrap_or_else(|| format!(', 0, 0],
  ['kernelNotFoundMessage', 'kernel/mod.rs', 'available: false,', 0, 1],
  ['installedButCannotRun', 'kernel/mod.rs', 'available: false,', 1, 1],
  ['probeSucceededWithoutOutput', 'kernel/mod.rs', 'if exit_ok {', 0, 1],
  ['probeIllegalInstruction', 'kernel/mod.rs', '0xC000_001D =>', 0, 0],
  ['probeAccessViolation', 'kernel/mod.rs', '0xC000_0005 =>', 0, 0],
  ['probeStackOverflow', 'kernel/mod.rs', '0xC000_00FD =>', 0, 0, 'any'],
  ['probeKnownTail', 'kernel/mod.rs', 'let tail = if known.is_empty()', 0, 3],
  ['probeCannotExecute', 'kernel/mod.rs', 'Err(format!(', 0, 1],
  ['readVersionTimeout', 'kernel/mod.rs', 'Duration::from_secs(5), command.output())', 0, 2],
  ['executeKernelFailed', 'kernel/mod.rs', '.with_context(|| format!("', 0, 0],
  ['checkUpdateFailed', 'kernel/mod.rs', 'let fallback = |res: Result<KernelReleaseInfo>', 0, 8],
  // ---- kernel/updater.rs：阶段标签与下载方式文案（类型在本模块，下载逻辑不在）----
  ['stageChecking', 'kernel/updater.rs', 'Self::Checking =>', 0, 0],
  ['stageDownloading', 'kernel/updater.rs', 'Self::Downloading =>', 0, 0],
  ['stageVerifying', 'kernel/updater.rs', 'Self::Verifying =>', 0, 0],
  ['stageExtracting', 'kernel/updater.rs', 'Self::Extracting =>', 0, 0],
  ['stageInstalling', 'kernel/updater.rs', 'Self::Installing =>', 0, 0],
  ['sourceGhProxyLabel', 'kernel/updater.rs', 'id: "gh-proxy",', 0, 1],
  ['sourceGhProxyHint', 'kernel/updater.rs', 'id: "gh-proxy",', 0, 2],
  ['sourceGhFastLabel', 'kernel/updater.rs', 'id: "ghfast",', 0, 1],
  ['sourceGhFastHint', 'kernel/updater.rs', 'id: "ghfast",', 0, 2],
  ['sourceOfficialLabel', 'kernel/updater.rs', 'id: "official",', 0, 1],
  ['sourceOfficialHint', 'kernel/updater.rs', 'id: "official",', 0, 2],
  ['sourceProxyLabel', 'kernel/updater.rs', 'id: "proxy",', 0, 1],
  ['sourceProxyHint', 'kernel/updater.rs', 'id: "proxy",', 0, 2],
  ['sourceLocalLabel', 'kernel/updater.rs', 'id: "local",', 0, 1],
  ['sourceLocalHint', 'kernel/updater.rs', 'id: "local",', 0, 2],
  ['sourceLabelFallback', 'kernel/updater.rs', '.unwrap_or(', 1, 0],
  ['directTransport', 'kernel/updater.rs', 'let direct = transport_label ==', 0, 0],
  // ---- kernel/updater.rs：下载/解压/安装流水线的进度文案与错误消息 ----
  // 阶段进度（KernelUpdateProgress.message）与 anyhow 错误文本都会原样进界面，
  // 所以下面这些必须与 Rust 逐字一致；锚点定位全部用 ASCII，中文由脚本自己抄。
  ['transportAppProxyLabel', 'kernel/updater.rs', '{url}")', 0],
  ['transportSystemProxyLabel', 'kernel/updater.rs', '{url}")', 1],
  ['releaseAssetMissing', 'kernel/updater.rs', '{asset_name}"))?;', 0],
  ['releaseUpdateAvailable', 'kernel/updater.rs', '{}", info.kind.display_name(), release.tag_name)', 0],
  ['releaseUpToDate', 'kernel/updater.rs', '", info.kind.display_name())', 0],
  ['localZipRequired', 'kernel/updater.rs', 'let path = local_file.ok_or_else(|| anyhow::anyhow!("', 0],
  ['localFileReading', 'kernel/updater.rs', '{}", path.display()),', 0],
  ['localFileReadFailed', 'kernel/updater.rs', ': {}", path.display()))?;', 0],
  ['localArchiveSizeInvalid', 'kernel/updater.rs', ': {} bytes", bytes.len());', 0],
  ['localDigestVerifying', 'kernel/updater.rs', 'SHA-256".into(),', 0],
  ['localDigestSkipped', 'kernel/updater.rs', 'message: "', 1],
  ['checkingOfficialRelease', 'kernel/updater.rs', 'Release', 6],
  ['fallbackReleaseNotice', 'kernel/updater.rs', 'Release', 7],
  ['logReleaseSource', 'kernel/updater.rs', '#{transport_index}",', 0],
  ['logSourceBuiltinFallback', 'kernel/updater.rs', '" } else { "api.github.com/', 0, 0, 'nonAscii', 0],
  ['logSourceApiOrCache', 'kernel/updater.rs', '" } else { "api.github.com/', 0, 0, 'nonAscii', 1],
  ['logTargetAsset', 'kernel/updater.rs', '{} / {}', 0],
  ['archiveSizeInvalid', 'kernel/updater.rs', ': {} bytes", asset.size);', 0],
  ['logDownloadSizeMismatch', 'kernel/updater.rs', 'if bytes.len() as u64 != asset.size {', 0, 2],
  ['downloadSizeMismatch', 'kernel/updater.rs', '{}", asset.size, bytes.len());', 0],
  ['logDownloadComplete', 'kernel/updater.rs', 'SHA-256",', 1],
  ['verifyingSha256', 'kernel/updater.rs', 'SHA-256".into(),', 1],
  ['logVerifyError', 'kernel/updater.rs', '] {} {error}", kind.display_name()));', 0],
  ['logSha256Passed', 'kernel/updater.rs', '] {} SHA-256', 0],
  ['extractingStaging', 'kernel/updater.rs', 'message: "', 3],
  ['logExtractToStaging', 'kernel/updater.rs', 'tokio::task::spawn_blocking(move || {', 0, -4],
  ['logStagedVersion', 'kernel/updater.rs', 'Ok(Some(line)) => crate::startup_log(&format!("[', 0],
  ['logStagedNoOutput', 'kernel/updater.rs', 'Ok(None) => crate::startup_log("[', 0],
  ['logStagedProbeFailed', 'kernel/updater.rs', 'Err(error) => crate::startup_log(&format!("[', 0],
  ['logCompatOnly', 'kernel/updater.rs', 'GODEBUG=cpu.all=off', 0],
  ['logCompatStillFails', 'kernel/updater.rs', 'x.exe -v",', 0],
  ['logStagedFile', 'kernel/updater.rs', ': {}", staging.display()));', 0],
  ['stagedVersionMissing', 'kernel/updater.rs', '.with_context(|| format!("{}', 0],
  ['logStagedVersionMismatch', 'kernel/updater.rs', '{staged_version}', 0],
  ['stagedVersionMismatch', 'kernel/updater.rs', ': {staged_version}");', 0],
  ['logPrepareReplace', 'kernel/updater.rs', 'backup.display()', 0, -3],
  ['installingBackupAndReplace', 'kernel/updater.rs', 'message: "', 4],
  ['backupOldKernelFailed', 'kernel/updater.rs', '.context("', 0],
  ['replaceFailedRolledBack', 'kernel/updater.rs', 'return Err(error).context("', 0],
  ['logReplacedStillFails', 'kernel/updater.rs', ': {error}', 1],
  ['newKernelVerifyFailedRolledBack', 'kernel/updater.rs', 'return Err(error).context("', 1],
  ['logInstallComplete', 'kernel/updater.rs', '.map(|p| p.display().to_string())', 0, -6],
  ['noOldKernelToBackup', 'kernel/updater.rs', '.unwrap_or_else(|| "', 0],
  ['kernelUpdatedTo', 'kernel/updater.rs', '{}", kind.display_name(), staged_version);', 0],
  ['compatModeSuffix', 'kernel/updater.rs', 'message.push_str("', 0],
  ['transportSourceJoin', 'kernel/updater.rs', '{}", transport.label, source.label());', 0],
  ['channelFailedNextTransport', 'kernel/updater.rs', 'message: format!("{label}', 0],
  ['downloadAllFailed', 'kernel/updater.rs', '{}", asset.name, failures.join("', 0, 0, 'nonAscii', 0],
  ['failureJoiner', 'kernel/updater.rs', '{}", asset.name, failures.join("', 0, 0, 'nonAscii', 1],
  ['httpFailed', 'kernel/updater.rs', 'Err(error) => bail!("HTTP', 0],
  ['cancelledMarker', 'kernel/updater.rs', 'bail!("', 7],
  ['downloadExceededLimit', 'kernel/updater.rs', 'bail!("', 8],
  ['downloadingAsset', 'kernel/updater.rs', 'message: format!("', 2],
  ['downloadTruncated', 'kernel/updater.rs', '", bytes.len(), expected);', 0],
  ['downloadInterrupted', 'kernel/updater.rs', ': {error}"),', 1],
  ['noChannelForSource', 'kernel/updater.rs', 'if order.is_empty() {', 0, 2],
  ['releaseFetchFailed', 'kernel/updater.rs', ': {remote_error}",', 0],
  ['kernelDownloadCancelled', 'kernel/updater.rs', 'bail!("', 11],
  ['assetDigestMissing', 'kernel/updater.rs', '.context("GitHub', 0],
  ['assetDigestInvalid', 'kernel/updater.rs', '.context("GitHub', 1],
  ['sha256Mismatch', 'kernel/updater.rs', 'bail!("SHA-256', 0],
  ['zipOpenFailed', 'kernel/updater.rs', 'let mut zip = zip::ZipArchive::new(Cursor::new(archive)).context("', 0],
  ['zipMissingExecutable', 'kernel/updater.rs', '{}", kind.executable_name())', 0],
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function pickLiteral(lines, anchor, occurrence, offset, name, mode = 'nonAscii', pick = undefined) {
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    console.error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
    process.exit(1);
  }
  if (occurrence + 1 < hits.length && offset !== 0) {
    // 同一锚点多次命中时，偏移是按第 occurrence 次命中的行算的，这里只是提醒别串位。
    console.warn(`${name}: anchor "${anchor}" matches ${hits.length} lines, using #${occurrence}`);
  }
  const index = hits[occurrence] + offset;
  const line = lines[index];
  if (line === undefined) {
    console.error(`${name}: line ${index + 1} out of range`);
    process.exit(1);
  }
  const found = literalsIn(line);
  const accepted = mode === 'any' ? found : found.filter(hasNonAscii);
  if (pick === undefined) {
    if (accepted.length !== 1) {
      console.error(
        `${name}: expected exactly 1 ${mode} literal at ${index + 1}, got ${accepted.length} in:\n${line}`,
      );
      process.exit(1);
    }
    return { text: accepted[0], line: index + 1 };
  }
  // 显式给了 pick：允许一行里有多个候选，但下标必须存在（多行 format! 里那行要有唯一性）。
  if (pick >= accepted.length) {
    console.error(
      `${name}: line ${index + 1} has ${accepted.length} ${mode} literal(s), want #${pick}:\n${line}`,
    );
    process.exit(1);
  }
  return { text: accepted[pick], line: index + 1 };
}

function toCppArray(text) {
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];
const report = [];

for (const [name, file, anchor, occurrence, rawOffset, mode, pick] of entries) {
  // 省略 offset 就是 0（同一行里定位）。
  const offset = rawOffset === undefined ? 0 : rawOffset;
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  const { text, line } = pickLiteral(lines, anchor, occurrence, offset, name, mode, pick);
  const parts = toCppArray(text);
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- ${file}:${line} （${[...text].length} 个码点）`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push(`QString ${name}()`);
  body.push('{');
  if (hasNonAscii(text)) {
    body.push('    return fromCodePoints({');
    for (let i = 0; i < parts.length; i += 8) {
      body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
    }
    body.push('    });');
  } else {
    const escaped = text.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    body.push(`    return QStringLiteral("${escaped}");`);
  }
  body.push('}');
  body.push('');
  functions.push(body.join('\n'));
  declarations.push(`QString ${name}();`);
  report.push({ name, chars: [...text].length, asciiSkeleton: skeleton });
}

const source = [
  '// 由 tools/gen-kernel-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
  '// 这些文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）。',
  '// 模板里的 {} / {protocol} / {error} 等占位符原样保留，由调用方按 Rust 的 format! 语义替换。',
  '#include "kernel/KernelMessages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace Kernel::Messages {',
  'namespace {',
  'QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  ...functions,
  '}',
  '',
].join('\n');

const header = [
  '// 由 tools/gen-kernel-cjk-codepoints.mjs 生成，请勿手改。',
  '// 对应 Rust src/kernel/mod.rs 与 src/kernel/updater.rs 里的用户可见文本。',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  'namespace Kernel::Messages {',
  '',
  ...declarations,
  '',
  '}',
  '',
].join('\n');

fs.writeFileSync('D:/project/cpp/vcc/src/kernel/KernelMessages.cpp', source);
fs.writeFileSync('D:/project/cpp/vcc/src/kernel/KernelMessages.h', header);
console.log(JSON.stringify(report, null, 0));
console.log('generated ' + entries.length + ' message(s)');
