import fs from 'node:fs';

// 由 tools/gen-cjk-codepoints.mjs 改来的第二版：抽 src/platform/{mod.rs,firewall.rs,tray.rs}
// 里的中文提示，生成纯 ASCII 的 C++ 码点表（src/platform/Messages.h/.cpp）。
//
// 为什么不允许手打：这个环境会静默吞掉部分非 ASCII 字符，而托盘菜单、防火墙报告这些
// 文本是要和 Rust 版逐字对上的（同一台机器上可能同时装着 Rust 建的实例）。
//
// 每条用「ASCII 行锚点 + 往后扫 window 行 + 取第 index 个非 ASCII 字面量」定位。
// 命中 0 条或多条都会报错退出，绝不猜。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const entries = [
  // ---- platform/tray.rs：托盘菜单与提示 ----
  { name: 'menuShow', file: 'platform/tray.rs', anchor: 'let show_text = ', occurrence: 0, window: 0, index: 0 },
  { name: 'menuStopProxy', file: 'platform/tray.rs', anchor: 'let proxy_label = if running {', occurrence: 0, window: 3, index: 0 },
  { name: 'menuStartProxy', file: 'platform/tray.rs', anchor: 'let proxy_label = if running {', occurrence: 0, window: 3, index: 1 },
  { name: 'menuExit', file: 'platform/tray.rs', anchor: 'let exit_text = ', occurrence: 0, window: 0, index: 0 },
  { name: 'trayTooltip', file: 'platform/tray.rs', anchor: 'let tip = ', occurrence: 0, window: 0, index: 0 },
  { name: 'revealNoWindow', file: 'platform/tray.rs', anchor: 'reveal_main_window_from_tray: ', occurrence: 0, window: 0, index: 0 },
  { name: 'revealPosted', file: 'platform/tray.rs', anchor: 'ShowWindowAsync(hwnd=0x{:x})', occurrence: 0, window: 0, index: 0 },

  // ---- platform/mod.rs：提权与孤儿清理 ----
  { name: 'elevationFailed', file: 'platform/mod.rs', anchor: 'return Err(anyhow!(', occurrence: 0, window: 0, index: 0 },
  { name: 'tunNotSupported', file: 'platform/mod.rs', anchor: 'pub fn request_elevation(_extra_args', occurrence: 0, window: 1, index: 0 },
  { name: 'orphanCoreKilled', file: 'platform/mod.rs', anchor: 'tracing::info!(', occurrence: 0, window: 3, index: 0 },

  // ---- platform/firewall.rs：规则名与报告文本 ----
  { name: 'ruleNameTemplate', file: 'platform/firewall.rs', anchor: 'format!("{RULE_PREFIX}', occurrence: 0, window: 0, index: 0 },
  { name: 'netshFailed', file: 'platform/firewall.rs', anchor: '.map_err(|error| anyhow!(', occurrence: 0, window: 0, index: 0 },
  { name: 'canonicalizeFailed', file: 'platform/firewall.rs', anchor: '.map_err(|error| anyhow!(', occurrence: 1, window: 0, index: 0 },
  { name: 'addRuleFailed', file: 'platform/firewall.rs', anchor: 'return Err(anyhow!(', occurrence: 0, window: 1, index: 0 },
  { name: 'unknownError', file: 'platform/firewall.rs', anchor: '.unwrap_or(', occurrence: 0, window: 0, index: 0 },
  { name: 'alreadyAllowed', file: 'platform/firewall.rs', anchor: 'Ok(format!(', occurrence: 0, window: 0, index: 0 },
  { name: 'addedToAllowList', file: 'platform/firewall.rs', anchor: 'Ok(format!(', occurrence: 1, window: 0, index: 0 },
  { name: 'allReady', file: 'platform/firewall.rs', anchor: 'Ok(format!(', occurrence: 2, window: 0, index: 0 },
  { name: 'partialFailure', file: 'platform/firewall.rs', anchor: 'Err(anyhow!(', occurrence: 1, window: 1, index: 0 },
  { name: 'failureJoiner', file: 'platform/firewall.rs', anchor: 'failed.join(', occurrence: 0, window: 0, index: 0 },
  { name: 'windowsOnlySingle', file: 'platform/firewall.rs', anchor: 'pub fn ensure_allowed(program: &Path)', occurrence: 1, window: 1, index: 0 },
  { name: 'windowsOnlyMany', file: 'platform/firewall.rs', anchor: 'pub fn ensure_all(programs:', occurrence: 1, window: 2, index: 0 },
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function unescapeRustLiteral(text) {
  // Rust 源里的 "\0" 是给 Win32 的结尾零，不属于文本本身；其余 \\ 与 \" 还原成真字符。
  let out = text.replace(/\\0$/, '');
  out = out.replace(/\\(["\\])/g, '$1');
  return out;
}

function pick(lines, entry) {
  const hits = [];
  lines.forEach((line, index) => {
    if (line.trimStart().startsWith('//')) return; // 注释里的中文不算
    if (line.includes(entry.anchor)) hits.push(index);
  });
  if (hits.length <= entry.occurrence) {
    console.error(`${entry.name}: anchor "${entry.anchor}" matched ${hits.length} line(s), want #${entry.occurrence}`);
    process.exit(1);
  }
  const start = hits[entry.occurrence];
  const found = [];
  for (let offset = 0; offset <= entry.window; offset += 1) {
    const line = lines[start + offset];
    if (line === undefined) break;
    literalsIn(line).filter(hasNonAscii).forEach((literal) => found.push(unescapeRustLiteral(literal)));
  }
  if (found.length <= entry.index) {
    console.error(`${entry.name}: found ${found.length} non-ASCII literal(s) near "${entry.anchor}" (line ${start + 1}), want #${entry.index}`);
    process.exit(1);
  }
  if (found.length > entry.index + 1 && entry.window === 0) {
    console.error(`${entry.name}: ${found.length} literals on the anchor line, ambiguous`);
    process.exit(1);
  }
  return found[entry.index];
}

function toCppArray(text) {
  // 一律十六进制码点：initializer_list<char32_t> 不接受混着 "x" 字面量的推导。
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];

for (const entry of entries) {
  const lines = fs.readFileSync(RUST + '/' + entry.file, 'utf8').split(/\r?\n/);
  const literal = pick(lines, entry);
  const parts = toCppArray(literal);
  const skeleton = [...literal].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${entry.name} <- ${entry.file}`);
  body.push(`// 字符数: ${parts.length}，ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push(`QString ${entry.name}()`);
  body.push('{');
  body.push('    return fromCodePoints({');
  for (let i = 0; i < parts.length; i += 8) {
    body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
  }
  body.push('    });');
  body.push('}');
  body.push('');
  functions.push(body.join('\n'));
  declarations.push(`QString ${entry.name}();`);
  console.log(`${entry.name}: ${parts.length} chars, ascii skeleton = ${JSON.stringify(skeleton)}`);
}

const source = [
  '// 由 tools/gen-platform-strings.mjs 从 Rust 源生成，请勿手改。',
  '// 这些文本要与 vpn-parser 的 Rust 实现逐字一致：托盘菜单与防火墙报告会直接显示给用户。',
  '#include "platform/Messages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace Platform::Messages {',
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
  '// 由 tools/gen-platform-strings.mjs 生成，请勿手改。',
  '// 模板里保留 Rust 的 `{}` / `{:?}` / `{name}` 占位符，由 platform/Format.h 的 fillTemplate 填充。',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  'namespace Platform::Messages {',
  '',
  ...declarations,
  '',
  '}',
  '',
].join('\n');

fs.writeFileSync('D:/project/cpp/vcc/src/platform/Messages.cpp', source);
fs.writeFileSync('D:/project/cpp/vcc/src/platform/Messages.h', header);
console.log('generated ' + entries.length + ' message(s)');
