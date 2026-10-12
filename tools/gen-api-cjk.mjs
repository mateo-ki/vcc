import fs from 'node:fs';

// 由 tools/gen-cjk-codepoints.mjs 改写，只服务 src/api/：
// src/api/mod.rs 里的每条中文响应文本都要与 Rust 逐字一致（401/409/后台任务提示会
// 直接显示给用户），所以这里也不允许手打——用 ASCII 锚点定位，附近找不到就报错退出。
//   node tools/gen-api-cjk.mjs
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const target = {
  header: 'D:/project/cpp/vcc/src/api/ApiMessages.h',
  source: 'D:/project/cpp/vcc/src/api/ApiMessages.cpp',
  ns: 'Api::Messages',
  include: '#include "api/ApiMessages.h"',
};

const entries = [
  // 认证失败：401 的响应体，GUI 直接把 message 显示出来。
  ['authFailed', 'api/mod.rs', 'StatusCode::UNAUTHORIZED', 0, 'nonAscii'],
  // 更新内核前必须停代理：409 的前缀，后面接 KernelKind::display_name()。
  ['kernelUpdateBlocked', 'api/mod.rs', 'Json(ApiResponse::<()>::err(format!(', 0, 'nonAscii'],
  // 节点连不通时把节点标成不可用，成功/失败各自的后缀文本。
  ['markedUnavailableSuffix', 'api/mod.rs', 'to_owned(),', 0, 'nonAscii'],
  ['markFailurePrefix', 'api/mod.rs', 'Err(update_error) => format!(', 0, 'nonAscii'],
  // Gist 发现后台启动的提示（POST /api/gist/discover 的 message 字段）。
  ['gistStartedMessage', 'api/mod.rs', '"max_pages": max_pages,', 0, 'nonAscii'],
];

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
      if (found.length === 1) return { text: found[0], line: index + 1 };
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
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];
for (const [name, file, anchor, occurrence, mode] of entries) {
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  const { text, line } = pickLiteral(lines, anchor, occurrence, name, mode);
  const parts = toCppArray(text);
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- ${file}:${line}`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push('QString ' + name + '()');
  body.push('{');
  body.push('    return fromCodePoints({');
  for (let i = 0; i < parts.length; i += 8) {
    body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
  }
  body.push('    });');
  body.push('}');
  functions.push(body.join('\n'));
  declarations.push(`QString ${name}();`);
  console.log(`${name}: ${parts.length} chars, skeleton = ${JSON.stringify(skeleton)}`);
}

const source = [
  '// 由 tools/gen-api-cjk.mjs 从 Rust 源生成，请勿手改。',
  '// 这些文本是 HTTP API 的响应体，必须与 vpn-parser 的 Rust 实现逐字一致。',
  target.include,
  '',
  '#include <initializer_list>',
  '',
  `namespace ${target.ns} {`,
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
  '// 由 tools/gen-api-cjk.mjs 生成，请勿手改（中文由码点数组拼出，防止转录丢字）。',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  `namespace ${target.ns} {`,
  '',
  ...declarations,
  '',
  '}',
  '',
].join('\n');

fs.writeFileSync(target.source, source);
fs.writeFileSync(target.header, header);
console.log(`api: wrote ${functions.length} message(s)`);
