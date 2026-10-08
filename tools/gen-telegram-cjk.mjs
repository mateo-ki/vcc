import fs from 'node:fs';

// telegram.rs → C++ 的用户可见文本与测试夹具，全部从 Rust 源逐字抽取，生成纯 ASCII 的
// 码点表（本环境会静默丢非 ASCII 字符，手打中文等于赌博）。
//   node tools/gen-telegram-cjk.mjs
//
// src 侧：按「ASCII 锚点 + 行偏移」定位字面量，写进 src/collector/Telegram.cpp 的
//   BEGIN/END GENERATED 标记之间（标记丢了就报错退出，绝不猜）。
// tests 侧：把 mod tests 里每一条含非 ASCII 的字面量按所在行号生成 line<N>()，
//   外加 fixture() 的 r#"..."# 原始串，整体写到 tests/collector/TelegramCjkGenerated.h。
//
// 支持跨行字面量：Rust 的 "…\<newline>   续行" 会吃掉换行与下一行行首空白，这里同规则。
const RUST_FILE = 'D:/project/rust/vs/vpn-parser/src/collector/telegram.rs';
const CPP_TARGET = 'D:/project/cpp/vcc/src/collector/Telegram.cpp';
const TEST_HEADER = 'D:/project/cpp/vcc/tests/collector/TelegramCjkGenerated.h';

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

const rust = fs.readFileSync(RUST_FILE, 'utf8');
const rustLines = rust.split(/\r?\n/);

// 行号（0 基）→ 该行起始字符偏移
const lineStart = (() => {
  const starts = [0];
  for (let i = 0; i < rustLines.length; i += 1) {
    starts.push(starts[i] + rustLines[i].length + 1); // +1 是换行符
  }
  return starts;
})();

function unescapeRust(body) {
  // 普通字符串字面量里的 \n \t \r \\ \" \0 \u{...}；\<newline> 续行已在上游处理。
  return body.replace(/\\(u\{[0-9a-fA-F]{1,6}\}|.)/g, (all, token) => {
    if (token.startsWith('u')) {
      return String.fromCodePoint(parseInt(token.slice(2, -1), 16));
    }
    switch (token) {
      case 'n': return '\n';
      case 't': return '\t';
      case 'r': return '\r';
      case '0': return '\0';
      case '\\': return '\\';
      case '"': return '"';
      default:
        throw new Error('unsupported escape: ' + all);
    }
  });
}

/// 从 offset（必须指向起始引号）读一个字面量。raw=true 时读 r#"…"#。
/// 返回 { text, end }，end 是结束引号后一位的偏移。
function readLiteral(offset, raw) {
  if (raw) {
    if (rust.slice(offset, offset + 3) !== 'r#"') {
      throw new Error('expected r#" at offset ' + offset);
    }
    const end = rust.indexOf('"#', offset + 3);
    if (end < 0) throw new Error('unterminated raw literal');
    return { text: rust.slice(offset + 3, end), end: end + 2 };
  }
  if (rust[offset] !== '"') throw new Error('expected " at offset ' + offset);
  let index = offset + 1;
  let out = '';
  while (index < rust.length) {
    const ch = rust[index];
    if (ch === '\\') {
      const next = rust[index + 1];
      if (next === '\n' || next === '\r') {
        // Rust：吃掉换行 + 下一行行首空白
        index += 2;
        if (rust[index - 1] === '\r' && rust[index] === '\n') {
          index += 1;
        }
        while (index < rust.length && (rust[index] === ' ' || rust[index] === '\t')) index += 1;
        continue;
      }
      out += rust.slice(index, index + 2);
      index += 2;
      continue;
    }
    if (ch === '"') {
      return { text: unescapeRust(out), end: index + 1 };
    }
    out += ch;
    index += 1;
  }
  throw new Error('unterminated literal at offset ' + offset);
}

/// 从 offset 读一个 Rust 原始串：r"…"# / r#"…"# / r##"…"## 都支持。
function readRawLiteral(offset) {
  if (rust[offset] !== 'r') throw new Error('expected r at offset ' + offset);
  let index = offset + 1;
  let hashes = 0;
  while (rust[index] === '#') {
    hashes += 1;
    index += 1;
  }
  if (rust[index] !== '"') throw new Error('not a raw literal at offset ' + offset);
  const closer = '"' + '#'.repeat(hashes);
  const end = rust.indexOf(closer, index + 1);
  if (end < 0) throw new Error('unterminated raw literal at offset ' + offset);
  return { text: rust.slice(index + 1, end), end: end + closer.length, startLine: 0 };
}

/// 锚点行往后 3 行里找第一个原始串——用于把 Regex 模式逐字搬过来。
function pickPattern(anchor, occurrence, name) {
  const hits = [];
  rustLines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    throw new Error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
  }
  const from = hits[occurrence];
  for (let index = from; index <= from + 3 && index < rustLines.length; index += 1) {
    let cursor = lineStart[index];
    const limit = lineStart[index + 1];
    while (cursor < limit) {
      if (rust[cursor] === 'r' && (rust[cursor + 1] === '"' || rust[cursor + 1] === '#')) {
        const found = readRawLiteral(cursor);
        return { ...found, startLine: index + 1 };
      }
      cursor += 1;
    }
  }
  throw new Error(`${name}: no raw literal within 3 lines of "${anchor}"`);
}

/// 第 lineIndex 行（0 基）里的字面量；mode=nonAscii 只留含非 ASCII 的，any 全留。
function literalsOnLine(lineIndex, mode = 'nonAscii') {
  const out = [];
  let cursor = lineStart[lineIndex];
  const limit = lineStart[lineIndex + 1];
  while (cursor < limit) {
    if (rust[cursor] === '"') {
      const found = readLiteral(cursor, false);
      const accept = mode === 'any' ? true : hasNonAscii(found.text);
      if (accept) out.push({ ...found, startLine: lineIndex + 1 });
      cursor = found.end;
      continue;
    }
    cursor += 1;
  }
  return out;
}

/// 锚点第 occurrence 次命中的行 + offset 行，取该行唯一字面量；命中 0 个或多个都报错。
function pick(anchor, occurrence, offset, name, mode = 'nonAscii') {
  const hits = [];
  rustLines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    throw new Error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
  }
  if (occurrence + 1 < hits.length && offset !== 0) {
    console.warn(`${name}: anchor "${anchor}" matches ${hits.length} lines, using #${occurrence}`);
  }
  const targetIndex = hits[occurrence] + offset;
  const found = literalsOnLine(targetIndex, mode);
  if (found.length !== 1) {
    throw new Error(
      `${name}: expected exactly 1 ${mode} literal on line ${targetIndex + 1}, got ${found.length}:\n`
      + rustLines[targetIndex],
    );
  }
  return found[0];
}

function toCppArray(text) {
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

function functionBody(name, text, fileLine, forceCodePoints = false) {
  const parts = toCppArray(text);
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- telegram.rs:${fileLine}（${[...text].length} 个码点）`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push(`QString ${name}()`);
  body.push('{');
  if (hasNonAscii(text) || forceCodePoints) {
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
  return { text: body.join('\n'), chars: [...text].length, skeleton };
}

// ---- src 侧：模块自己的用户可见文本 ----
const srcEntries = [
  // name, anchor, occurrence, offset
  ['egressInApp', 'if self.via_proxy {', 0, 2],
  ['egressSystem', 'Ok(via) => {', 0, 1],
  ['systemProxyUnusable', 'Err(_) => {', 0, 4],
  ['needEgress', 'format!("https://t.me/s/{channel}")', 1, 1],
  ['notModified', 'message: "not modified', 0, 0],
  ['contentUnchanged', 'message: format!(', 0, 1],
  ['failureNote', 'failures.join(', 0, 0],
  ['zeroNodes', 'let message = format!(', 0, 1],
  ['okSummary', 'let message = format!(', 1, 1],
];

const srcReport = [];
const srcFunctions = [];
for (const [name, anchor, occurrence, offset] of srcEntries) {
  const found = pick(anchor, occurrence, offset, name);
  const made = functionBody(name, found.text, found.startLine);
  srcFunctions.push(made.text);
  srcReport.push({ name, line: found.startLine, chars: made.chars, skeleton: made.skeleton });
}

// ---- 正则模式：逐字搬过来（模式里有中文标点，也绝不允许手打）----
const patternEntries = [
  ['patternBody', 'static BODY: Lazy<Regex>'],
  ['patternTags', 'static TAGS: Lazy<Regex>'],
  ['patternPostId', 'static POST_ID:'],
  ['patternHref', 'static HREF:'],
  ['patternNodeUri', 'static NODE_URI:'],
  ['patternHttpUrl', 'static HTTP_URL:'],
  ['patternEntity', 'static ENTITY:'],
];
const patternFunctions = [];
for (const [name, anchor] of patternEntries) {
  const found = pickPattern(anchor, 0, name);
  const made = functionBody(name, found.text, found.startLine, true);
  patternFunctions.push(made.text);
  srcReport.push({ name, line: found.startLine, chars: made.chars, skeleton: made.skeleton });
}

const generatedBlock = [
  '// ==== BEGIN GENERATED: tools/gen-telegram-cjk.mjs（勿手改，重跑脚本即可再生成）====',
  '// telegram.rs 里的用户可见文本与正则模式。模板中的 {} / {pages_read} 等占位符原样保留，',
  '// 由 formatText() 按 Rust format! 的语义替换。',
  'namespace Collector::Telegram::Messages {',
  'namespace {',
  'QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  ...patternFunctions,
  ...srcFunctions,
  '} // namespace Collector::Telegram::Messages',
  '// ==== END GENERATED: tools/gen-telegram-cjk.mjs ====',
  '',
].join('\n');

if (!fs.existsSync(CPP_TARGET)) {
  throw new Error('missing ' + CPP_TARGET + ' — 先写 Telegram.cpp（含 BEGIN/END GENERATED 标记）');
}
const cppText = fs.readFileSync(CPP_TARGET, 'utf8');
const beginMark = '// ==== BEGIN GENERATED: tools/gen-telegram-cjk.mjs';
const endMark = '// ==== END GENERATED: tools/gen-telegram-cjk.mjs ====';
const begin = cppText.indexOf(beginMark);
const end = cppText.indexOf(endMark);
if (begin < 0 || end < 0 || end < begin) {
  throw new Error('Telegram.cpp 里的 GENERATED 标记丢失或错位');
}
fs.writeFileSync(
  CPP_TARGET,
  cppText.slice(0, begin) + generatedBlock + cppText.slice(end + endMark.length + 1),
);

// ---- tests 侧：mod tests 里所有含非 ASCII 的字面量 + fixture 原始串 ----
const testsStart = rustLines.findIndex((line) => line.includes('mod tests {'));
if (testsStart < 0) throw new Error('找不到 mod tests');

// fixture() 的 r#"…"#：正文里有成对的 HTML 属性引号，扫描时整段跳过，只按原始串取。
const fixtureLine = rustLines.findIndex((line) => line.includes('fn fixture() -> String'));
if (fixtureLine < 0) throw new Error('找不到 fn fixture() -> String');
const rawAt = rust.indexOf('r#"', lineStart[fixtureLine]);
if (rawAt < 0) throw new Error('fixture() 里找不到 r#" 原始串');
const fixture = readLiteral(rawAt, true);
const fixtureEndLine = (() => {
  let count = 0;
  for (let i = 0; i < fixture.end; i += 1) {
    if (rust[i] === '\n') count += 1;
  }
  return count; // fixture.end 所在行的 0 基下标
})();

const testLiterals = [];
for (let index = testsStart; index < rustLines.length; index += 1) {
  if (index > fixtureLine && index <= fixtureEndLine) continue;
  testLiterals.push(...literalsOnLine(index));
}

const testFunctions = [];
const testReport = [];
for (const item of testLiterals) {
  const made = functionBody('line' + item.startLine, item.text, item.startLine);
  testFunctions.push(made.text);
  testReport.push({ name: 'line' + item.startLine, chars: made.chars, skeleton: made.skeleton });
}
const fixtureMade = functionBody('fixtureHtml', fixture.text, fixtureLine + 1);

const header = [
  '// 由 tools/gen-telegram-cjk.mjs 从 telegram.rs 的 mod tests 生成，请勿手改。',
  '// line<N>() 对应 telegram.rs 第 N 行的字面量；fixtureHtml() 对应 fn fixture() 的 r#"…"#。',
  '#pragma once',
  '',
  '#include <QString>',
  '#include <initializer_list>',
  '',
  'namespace Collector::Telegram::TestText {',
  'namespace {',
  'QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  testFunctions.join('\n'),
  fixtureMade.text,
  '} // namespace Collector::Telegram::TestText',
  '',
].join('\n');
fs.writeFileSync(TEST_HEADER, header);

console.log(JSON.stringify({ src: srcReport, tests: testReport, fixtureChars: fixtureMade.chars }, null, 0));
console.log('src messages: ' + srcFunctions.length + '，test literals: ' + testFunctions.length);
