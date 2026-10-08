import fs from 'node:fs';

// 从 Rust 源里抽取中文提示文本，生成纯 ASCII 的 C++ 码点表：
//   node tools/gen-cjk-codepoints.mjs
// 这些文本会进入错误消息和界面，必须与 Rust 逐字一致，所以不允许我手打。
// 每条用一个 ASCII 锚点定位，再在附近几行里找那个非 ASCII 字面量；
// 找到 0 个或多个就报错退出，绝不猜。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const entries = [
  ['platformUnsupported', 'system_proxy/mod.rs', 'Err(anyhow!("', 0],
  ['wininetNotifyFailed', 'system_proxy/mod.rs', 'Err(anyhow!("', 4],
  ['regExitCode', 'system_proxy/mod.rs', 'format!("reg.exe', 0],
  ['deleteValueFailed', 'system_proxy/mod.rs', 'reg_failure_detail(&output)', 0],
  ['modifyFailed', 'system_proxy/mod.rs', 'let detail = reg_failure_detail', 0],
  ['writtenPrefix', 'system_proxy/terminal.rs', 'parts.push(format!(', 0],
  ['removedPrefix', 'system_proxy/terminal.rs', 'parts.push(format!(', 1],
  ['untouchedPrefix', 'system_proxy/terminal.rs', 'parts.push(format!(', 2],
  ['noChange', 'system_proxy/terminal.rs', 'if parts.is_empty()', 0],
  ['joiner', 'system_proxy/terminal.rs', 'parts.join(', 0],
  ['writeFailed', 'system_proxy/terminal.rs', 'String::from_utf8_lossy(&output.stdout)', 1],
  ['removeFailed', 'system_proxy/terminal.rs', 'String::from_utf8_lossy(&output.stdout)', 2],
  ['alreadyYours', 'system_proxy/terminal.rs', 'if !is_ours(&existing)', 0],
  ['windowsOnly', 'system_proxy/terminal.rs', 'pub fn enable(_port: u16)', 0],
  ['noProxyValue', 'system_proxy/terminal.rs', 'pub const NO_PROXY_VALUE', 0, 'any'],
];

// 纯 ASCII 的常量（比如 NO_PROXY 列表）直接生成字符串字面量，可读性优先；
// 含中文的才走码点数组。

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function pickLiteral(lines, anchor, occurrence, name, mode = 'nonAscii') {
  const accept = mode === 'any' ? () => true : hasNonAscii;
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    console.error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
    process.exit(1);
  }
  const center = hits[occurrence];
  for (let offset = 0; offset <= 3; offset += 1) {
    for (const index of offset === 0 ? [center] : [center - offset, center + offset]) {
      const line = lines[index];
      if (line === undefined) continue;
      const found = literalsIn(line).filter(accept);
      if (found.length === 1) return found[0];
      if (found.length > 1) {
        console.error(`${name}: ${found.length} matching literals near "${anchor}"`);
        process.exit(1);
      }
    }
  }
  console.error(`${name}: no non-ASCII literal near "${anchor}" (line ${center + 1})`);
  process.exit(1);
}

function toCppArray(text) {
  // 一律用十六进制码点：混着 "x" 字面量会让 initializer_list<char32_t> 推不出公共类型。
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];

for (const [name, file, anchor, occurrence, mode] of entries) {
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  const literal = pickLiteral(lines, anchor, occurrence, name, mode);
  const parts = toCppArray(literal);
  const skeleton = [...literal].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- ${file}`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push('QString ' + name + '()');
  body.push('{');
  if (hasNonAscii(literal)) {
    body.push('    return fromCodePoints({');
    for (let i = 0; i < parts.length; i += 8) {
      body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
    }
    body.push('    });');
  } else {
    // 纯 ASCII：直接写字面量，NO_PROXY 那种长列表还要能被人读懂。
    const escaped = literal.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    body.push(`    return QStringLiteral("${escaped}");`);
  }
  body.push('}');
  body.push('');
  functions.push(body.join('\n'));
  declarations.push('QString ' + name + '();');
  const asciiSkeleton = [...literal].filter((c) => c.codePointAt(0) < 0x7f).join('');
  console.log(`${name}: ${parts.length} chars, ascii skeleton = ${JSON.stringify(asciiSkeleton)}`);
}

const source = [
  '// 由 tools/gen-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
  '// 这些提示文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）。',
  '#include "sysproxy/Messages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace SysProxy::Messages {',
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
].join('\n');

const header = [
  '// 由 tools/gen-cjk-codepoints.mjs 生成，请勿手改。',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  'namespace SysProxy::Messages {',
  '',
  ...declarations,
  '',
  '}',
  '',
].join('\n');

fs.writeFileSync('D:/project/cpp/vcc/src/sysproxy/Messages.cpp', source);
fs.writeFileSync('D:/project/cpp/vcc/src/sysproxy/Messages.h', header);
console.log('generated ' + entries.length + ' message(s)');
