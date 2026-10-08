import fs from 'node:fs';

// 从 Rust 源里抽取内核模块的中文提示文本，生成纯 ASCII 的 C++ 码点表：
//   node tools/gen-kernel-cjk-codepoints.mjs
// 这些文本是用户可见的错误消息（以及下载弹框的文案），必须与 vpn-parser 逐字一致，
// 所以不允许手打——本环境会静默丢掉 astral 平面的字符。
//
// 与 gen-cjk-codepoints.mjs 的差别：这里用 ASCII 锚点 + **精确行偏移** 定位字面量，
// 找到 0 个、多于 1 个或非 ASCII 模式下的纯 ASCII 都直接报错退出，绝不猜。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

// [name, file, anchor, occurrence, offset, mode]
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
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function pickLiteral(lines, anchor, occurrence, offset, name, mode = 'nonAscii') {
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
  if (accepted.length !== 1) {
    console.error(
      `${name}: expected exactly 1 ${mode} literal at ${index + 1}, got ${accepted.length} in:\n${line}`,
    );
    process.exit(1);
  }
  return { text: accepted[0], line: index + 1 };
}

function toCppArray(text) {
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];
const report = [];

for (const [name, file, anchor, occurrence, offset, mode] of entries) {
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  const { text, line } = pickLiteral(lines, anchor, occurrence, offset, name, mode);
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
