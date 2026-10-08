import fs from 'node:fs';

// 由 sing_box.rs / xray.rs 生成纯 ASCII 的中文文案码点表：
//   node tools/gen-kernel-config-cjk-codepoints.mjs [--check]
//
// 为什么必须脚本化：这些文本是内核配置生成器的错误消息，会进界面和日志，必须与
// vpn-parser 逐字一致；而我手打中文时会静默丢字符。
//
// 定位方式：ASCII 锚点（+ 第 occurrence 次命中 + 行偏移）。以下三种情况一律硬失败：
//   1. 锚点命中 0 次；
//   2. 锚点命中多次却没给 occurrence；
//   3. 目标行上非 ASCII 字面量不是恰好 1 个（这就是"锚点抓到隔壁那条"的探测器）。
// 另外还打印 ASCII 骨架 + 行号，方便人和 Rust 源码逐行对眼。
const RUST = 'D:/project/rust/vs/vpn-parser/src';
const SING_BOX = 'proxy/sing_box.rs';
const XRAY = 'proxy/xray.rs';

// [name, file, anchor, occurrence(期望命中次数=1 时写 0 并要求唯一), offset, expectSkeleton]
const entries = [
  // ---- sing_box.rs ----
  ['singBoxKernelNotFound', SING_BOX, 'let binary = find_kernel_executable(KernelKind::SingBox)', 0, 1, ' Sing-box '],
  ['singBoxBatchFailed', SING_BOX, 'Err(error) => results.extend(batch.iter().map(|node| ProxyProbeResult {', 0, 4, 'Sing-box : {error}'],
  ['singBoxConfigUnsupported', SING_BOX, 'Err(error) => immediate.push(ProxyProbeResult {', 0, 4, 'Sing-box : {error}'],
  ['singBoxStartupFailed', SING_BOX, 'return Err(anyhow!("', 0, 0, ': {}'],
  ['singBoxNoLogLine', SING_BOX, '.filter(|line| !line.is_empty())', 0, 2, ''],
  ['singBoxRealProxyOk', SING_BOX, 'message: "Sing-box', 0, 0, 'Sing-box '],
  ['singBoxRealProxyFailed', SING_BOX, 'message: format!("Sing-box', 2, 0, 'Sing-box : {error}'],
  ['singBoxProbeTargetOk', SING_BOX, 'ms (Sing-box', 0, 0, '{elapsed} ms (Sing-box   {target})'],
  ['singBoxUnreachable', SING_BOX, 'message: format!("Sing-box', 3, 0, 'Sing-box : {error}'],
  ['probeConcurrencyClosed', SING_BOX, '.map_err(|_| anyhow!(', 0, 0, ''],
  ['dohUrlInvalid', SING_BOX, 'url::Url::parse(server).map_err(|error| anyhow!(', 0, 0, 'DoH : {error}'],
  ['dohUrlMissingHost', SING_BOX, 'ok_or_else(|| anyhow!("', 1, 0, 'DoH : {server}'],
  ['singBoxProtocolUnsupported', SING_BOX, '_ => Err(anyhow!("Sing-box', 0, 0, 'Sing-box : {}'],
  ['singBoxWgMissingAddress', SING_BOX, 'address/ip', 0, 0, 'Sing-box WireGuard  address/ip'],
  ['singBoxGeoSiteUnsupported', SING_BOX, 'return Err(anyhow!("Sing-box', 0, 0, 'Sing-box  GeoSite : {value}'],
  ['singBoxGeoIpUnsupported', SING_BOX, 'return Err(anyhow!("Sing-box', 1, 0, 'Sing-box  GeoIP : {value}'],
  ['singBoxPortRuleInvalid', SING_BOX, '.map_err(|_| anyhow!("Sing-box', 0, 0, 'Sing-box : {value}'],
  ['singBoxMissingField', SING_BOX, 'fn required_string', 0, 1, 'Sing-box : {}'],

  // ---- xray.rs ----
  ['xrayKernelNotFound', XRAY, 'let binary = find_kernel_executable(KernelKind::Xray)', 0, 1, ' Xray '],
  ['xrayBatchFailed', XRAY, 'Err(error) => results.extend(batch.iter().map(|node| ProxyProbeResult {', 0, 4, 'Xray : {error}'],
  ['xrayConfigUnsupported', XRAY, 'Err(error) => immediate.push(ProxyProbeResult {', 0, 4, 'Xray : {error}'],
  ['xrayRealProxyOk', XRAY, 'message: "Xray', 0, 0, 'Xray '],
  ['xrayRealProxyFailed', XRAY, 'message: format!("Xray', 2, 0, 'Xray : {error}'],
  ['xrayBatchStartupFailed', XRAY, 'let _ = tokio::fs::remove_dir_all(&directory).await;', 0, 2, 'Xray : {}'],
  ['xrayUnknownError', XRAY, 'detail.lines().last().unwrap_or(', 0, 0, ''],
  ['xrayTunUnsupported', XRAY, 'return Err(anyhow!("Xray', 0, 0, 'Xray  TUN '],
  ['xrayPortRuleInvalid', XRAY, '.map_err(|_| anyhow!("Xray', 0, 0, 'Xray : {port}'],
  ['xrayProcessNameUnsupported', XRAY, 'return Err(anyhow!("Xray', 1, 0, 'Xray : {name}'],
  ['xrayProtocolUnsupported', XRAY, 'return Err(anyhow!("Xray', 2, 0, 'Xray : {}'],
  ['xrayMissingField', XRAY, "fn required<'a>", 0, 1, 'Xray : {}'],
  // xray.rs 测试里断言错误文本含这段；单独生成，测试用码点比对而不是手打。
  ['xrayProcessNameMarker', XRAY, 'error.to_string().contains(', 0, 0, ''],
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

const files = new Map();
function linesOf(file) {
  if (!files.has(file)) {
    files.set(file, fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/));
  }
  return files.get(file);
}

function fail(message) {
  console.error('FATAL: ' + message);
  process.exit(1);
}

function pickLiteral(entry) {
  const [name, file, anchor, occurrence, offset, expectSkeleton] = entry;
  const lines = linesOf(file);
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length === 0) fail(`${name}: anchor ${JSON.stringify(anchor)} matched 0 line(s) in ${file}`);
  if (hits.length > 1 && occurrence === 0 && offset !== 0) {
    fail(`${name}: anchor ${JSON.stringify(anchor)} is ambiguous (${hits.length} hits: ${hits.map((h) => h + 1).join(',')}), pass an explicit occurrence`);
  }
  if (hits.length <= occurrence) {
    fail(`${name}: anchor ${JSON.stringify(anchor)} matched ${hits.length} line(s), want #${occurrence}`);
  }
  if (hits.length > 1 && occurrence === 0) {
    console.warn(`${name}: anchor ${JSON.stringify(anchor)} hits ${hits.length} lines (${hits.map((h) => h + 1).join(',')}), taking #0`);
  }
  const index = hits[occurrence] + offset;
  const line = lines[index];
  if (line === undefined) fail(`${name}: line ${index + 1} out of range`);
  const found = literalsIn(line).filter(hasNonAscii);
  if (found.length !== 1) {
    fail(
      `${name}: expected exactly 1 non-ASCII literal at ${file}:${index + 1}, got ${found.length}:\n${line}`,
    );
  }
  const text = found[0];
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  if (skeleton !== expectSkeleton) {
    fail(
      `${name}: ${file}:${index + 1} skeleton is ${JSON.stringify(skeleton)} but entry expects ` +
        `${JSON.stringify(expectSkeleton)} —— 锚点大概抓到了隔壁那条，或 Rust 文本变了`,
    );
  }
  return { text, line: index + 1 };
}

function toCppArray(text) {
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];
const report = [];
const claimed = new Set();

for (const entry of entries) {
  const [name, file] = entry;
  const { text, line } = pickLiteral(entry);
  claimed.add(file + ':' + line);
  const parts = toCppArray(text);
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- ${file}:${line}，${[...text].length} 个字符（含 ASCII 部分）`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push('QString ' + name + '()');
  body.push('{');
  body.push('    return fromCodePoints({');
  for (let i = 0; i < parts.length; i += 8) {
    body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
  }
  body.push('    });');
  body.push('}');
  body.push('');
  functions.push(body.join('\n'));
  declarations.push('QString ' + name + '();');
  report.push({ name, rust: file + ':' + line, chars: [...text].length, skeleton });
}

// 覆盖检查：这两个 Rust 文件里所有非 ASCII 字面量都要么被上面认领，要么是有意跳过的
// （注释、测试里不参与断言的文本），列出来让人过一遍眼。
const skipped = [];
for (const [file, lines] of files) {
  lines.forEach((line, index) => {
    if (!hasNonAscii(line)) return;
    if (claimed.has(file + ':' + (index + 1))) return;
    for (const literal of literalsIn(line).filter(hasNonAscii)) {
      skipped.push(`${file}:${index + 1} ${JSON.stringify(literal)}`);
    }
  });
}

const header = [
  '// 由 tools/gen-kernel-config-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
  '// 对应 proxy/sing_box.rs 与 proxy/xray.rs 里的用户可见文本：内核不支持某条规则时',
  '// 必须报错而不是静默丢弃，所以这些文本本身就是行为规约的一部分。',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  '#include <initializer_list>',
  '',
  'namespace Proxy::Messages {',
  '',
  '/// Rust 的 `format!` 只扫描模板一次，插进去的值不会被二次求值；这里同样一遍扫描，',
  '/// 免得节点名里真有 "{error}" 之类的字面量时被连环替换。',
  'struct Placeholder {',
  '    const char *marker;',
  '    QString value;',
  '};',
  '',
  'inline QString fill(const QString &text, std::initializer_list<Placeholder> args)',
  '{',
  '    QString out;',
  '    out.reserve(text.size() + 16);',
  '    for (qsizetype index = 0; index < text.size();) {',
  '        bool matched = false;',
  '        if (text.at(index) == QLatin1Char(\'{\')) {',
  '            for (const Placeholder &arg : args) {',
  '                const QString marker = QLatin1String(arg.marker);',
  '                if (!marker.isEmpty() && text.mid(index, marker.size()) == marker) {',
  '                    out += arg.value;',
  '                    index += marker.size();',
  '                    matched = true;',
  '                    break;',
  '                }',
  '            }',
  '        }',
  '        if (!matched) {',
  '            out += text.at(index);',
  '            ++index;',
  '        }',
  '    }',
  '    return out;',
  '}',
  '',
  ...declarations,
  '',
  '}',
  '',
].join('\n');

const source = [
  '// 由 tools/gen-kernel-config-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
  '// 模板里的 {} / {error} / {value} 等占位符原样保留，由调用方用 Messages::fill 替换。',
  '#include "proxy/KernelConfigMessages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace Proxy::Messages {',
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

const HEADER_OUT = 'D:/project/cpp/vcc/src/proxy/KernelConfigMessages.h';
const SOURCE_OUT = 'D:/project/cpp/vcc/src/proxy/KernelConfigMessages.cpp';

if (process.argv.includes('--check')) {
  const onDiskHeader = fs.existsSync(HEADER_OUT) ? fs.readFileSync(HEADER_OUT, 'utf8') : '';
  const onDiskSource = fs.existsSync(SOURCE_OUT) ? fs.readFileSync(SOURCE_OUT, 'utf8') : '';
  if (onDiskHeader !== header || onDiskSource !== source) {
    fail('src/proxy/KernelConfigMessages.h/.cpp 与 Rust 源不同步，重新跑一次生成脚本');
  }
  console.log('kernel config messages up to date (' + entries.length + ' message(s))');
} else {
  fs.writeFileSync(HEADER_OUT, header);
  fs.writeFileSync(SOURCE_OUT, source);
  console.log(JSON.stringify(report, null, 0));
  console.log('generated ' + entries.length + ' message(s)');
  console.log('未被认领的非 ASCII 字面量（请逐条确认是否漏了）:');
  for (const item of skipped) console.log('  ' + item);
}
