import fs from 'node:fs';

// 由 router.rs 抽取中文/箭头字面量，生成纯 ASCII 的 C++ 码点表：
//   node src/router/gen-router-cjk.mjs
// 选路决策的 reason 会进界面和日志，必须与 Rust 逐字一致，所以不允许手打。
// 每条用一个 ASCII 锚点定位，再在附近几行里找那个非 ASCII 字面量；
// 找到 0 个或多个就报错退出，绝不猜。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const outputs = {
  router: {
    header: 'D:/project/cpp/vcc/src/router/RouterMessages.h',
    source: 'D:/project/cpp/vcc/src/router/RouterMessages.cpp',
    ns: 'Router::Messages',
    include: '#include "router/RouterMessages.h"',
    inline: false,
  },
  tests: {
    header: 'D:/project/cpp/vcc/tests/router/RouterTestCjk.h',
    source: null,
    ns: 'RouterTestCjk',
    include: '#include "router/RouterTestCjk.h"',
    inline: true,
  },
};

// [函数名, Rust 文件, ASCII 锚点, 锚点第几次出现, 取值模式, 归属输出]
const entries = [
  // ---- 决策原因模板（router.rs 的 observe()）----
  ['breakerReason', 'router.rs', 'let reason = format!(', 0, 'nonAscii', 'router'],
  ['latencyDetail', 'router.rs', 'let detail = if degraded_by_latency {', 0, 'nonAscii', 'router'],
  ['bandwidthDetail', 'router.rs', 'KB/s", self.cfg.bandwidth_threshold_kbps', 0, 'nonAscii', 'router'],
  ['switchReason', 'router.rs', 'reason: format!("{detail}', 0, 'nonAscii', 'router'],

  // ---- 测试断言里 Rust 用的子串（mod tests）----
  ['latencyHead', 'router.rs', 'assert!(reason.contains(', 0, 'nonAscii', 'router'],
  ['bandwidthHead', 'router.rs', 'assert!(reason.contains(', 1, 'nonAscii', 'router'],

  // ---- 替补挑选测试的节点名与排除词（mod backup_tests）----
  ['currentNodeName', 'router.rs', 'node("c", ', 0, 'nonAscii', 'tests'],
  ['midNodeName', 'router.rs', 'node("b", ', 0, 'nonAscii', 'tests'],
  ['fastestNodeName', 'router.rs', 'node("a", ', 0, 'nonAscii', 'tests'],
  ['brokenNodeName', 'router.rs', 'node("x", ', 0, 'nonAscii', 'tests'],
  ['untestedNodeName', 'router.rs', 'node("y", ', 0, 'nonAscii', 'tests'],
  ['expiredThrottledNodeName', 'router.rs', 'node("z", ', 0, 'nonAscii', 'tests'],
  ['usableNodeName', 'router.rs', 'node("w", ', 0, 'nonAscii', 'tests'],
  ['onlyChildNodeName', 'router.rs', 'node("c", ', 1, 'nonAscii', 'tests'],
  ['excludeKeywords', 'router.rs', '&black, ', 0, 'nonAscii', 'tests'],
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
      if (found.length === 1) return { literal: found[0], line: index + 1, source: line.trim() };
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

const buckets = new Map();
for (const target of Object.keys(outputs)) {
  buckets.set(target, { functions: [], declarations: [] });
}

for (const [name, file, anchor, occurrence, mode, target] of entries) {
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  const { literal, line, source } = pickLiteral(lines, anchor, occurrence, name, mode);
  const parts = toCppArray(literal);
  const skeleton = [...literal].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const bucket = buckets.get(target);
  const config = outputs[target];
  const body = [];
  body.push(`// ${name} <- ${file}:${line}`);
  body.push(`// Rust 原文: ${source}`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  if (config.inline) {
    body.push(`inline QString ${name}()`);
  } else {
    body.push(`QString ${name}()`);
  }
  body.push('{');
  if (hasNonAscii(literal)) {
    body.push('    return ' + (config.inline ? 'detail::' : '') + 'fromCodePoints({');
    for (let i = 0; i < parts.length; i += 8) {
      body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
    }
    body.push('    });');
  } else {
    const escaped = literal.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    body.push(`    return QStringLiteral("${escaped}");`);
  }
  body.push('}');
  body.push('');
  bucket.functions.push(body.join('\n'));
  bucket.declarations.push(`${config.inline ? 'inline ' : ''}QString ${name}();`);
  console.log(`${target}.${name}: ${parts.length} chars, skeleton = ${JSON.stringify(skeleton)}`);
}

for (const [target, config] of Object.entries(outputs)) {
  const bucket = buckets.get(target);
  if (!bucket.functions.length) {
    continue;
  }
  const headerLines = [
    '// 由 gen-router-cjk.mjs 从 router.rs 生成，请勿手改。',
    '#pragma once',
    '',
    '#include <QString>',
    '',
  ];
  if (config.inline) {
    headerLines.push('#include <initializer_list>');
    headerLines.push('');
    headerLines.push(`namespace ${config.ns} {`);
    headerLines.push('namespace detail {');
    headerLines.push('inline QString fromCodePoints(std::initializer_list<char32_t> points)');
    headerLines.push('{');
    headerLines.push('    return QString::fromUcs4(points.begin(), qsizetype(points.size()));');
    headerLines.push('}');
    headerLines.push('');
    headerLines.push('}'); // 关 detail
    headerLines.push('');
    headerLines.push(`} // namespace ${config.ns}`);
    headerLines.push('');
  }
  if (config.inline) {
    // 只有测试用的夹具文本走 header-only：没有 .cpp 可挂，声明就得直接是定义。
    headerLines.push(`namespace ${config.ns} {`);
    headerLines.push('');
    headerLines.push(...bucket.functions);
    headerLines.push('}');
    headerLines.push('');
  } else {
    headerLines.push(`namespace ${config.ns} {`);
    headerLines.push('');
    headerLines.push(...bucket.declarations);
    headerLines.push('');
    headerLines.push('}');
    headerLines.push('');
  }
  fs.writeFileSync(config.header, headerLines.join('\n'));

  if (config.source) {
    const sourceLines = [
      '// 由 gen-router-cjk.mjs 从 router.rs 生成，请勿手改。',
      '// 这些决策原因文本要与 vpn-parser 的 Rust 实现逐字一致（会进界面提示和日志）。',
      config.include,
      '',
      '#include <initializer_list>',
      '',
      `namespace ${config.ns} {`,
      'namespace {',
      'QString fromCodePoints(std::initializer_list<char32_t> points)',
      '{',
      '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
      '}',
      '',
      '}',
      '',
      ...bucket.functions,
      '}',
      '',
    ];
    fs.writeFileSync(config.source, sourceLines.join('\n'));
  }
  console.log(`${target}: wrote ${bucket.functions.length} message(s)`);
}
