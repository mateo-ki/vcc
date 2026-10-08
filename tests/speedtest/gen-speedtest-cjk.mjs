import fs from 'node:fs';

// speedtest/mod.rs + scheduler/mod.rs 的用户可见文本抽取器：
//   node tests/speedtest/gen-speedtest-cjk.mjs            生成码点表
//   node tests/speedtest/gen-speedtest-cjk.mjs --list     列出两个 Rust 文件里所有非 ASCII 字面量
// 生成纯 ASCII 的 C++，因为这个环境里手打的中文会静默丢码位，而这些字符串会进
// 界面、日志和错误消息，必须与 vpn-parser 逐字一致。
// 定位方式与 tools/gen-kernel-cjk-codepoints.mjs 一样：ASCII 锚点 + 命中序号 + 行偏移，
// 命中 0 个或多个（或纯 ASCII 模式不符）就报错退出，绝不猜。

const RUST = 'D:/project/rust/vs/vpn-parser/src';
const OUT = {
  speedtestHeader: 'D:/project/cpp/vcc/src/speedtest/SpeedtestMessages.h',
  speedtestSource: 'D:/project/cpp/vcc/src/speedtest/SpeedtestMessages.cpp',
  schedulerHeader: 'D:/project/cpp/vcc/src/scheduler/SchedulerMessages.h',
  schedulerSource: 'D:/project/cpp/vcc/src/scheduler/SchedulerMessages.cpp',
  schedulerTests: 'D:/project/cpp/vcc/tests/scheduler/SchedulerTestMessages.h',
};

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function readLines(file) {
  return fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
}

// ---- --list：清点，确认没有漏掉任何一条 ----
if (process.argv.includes('--list')) {
  for (const file of ['speedtest/mod.rs', 'scheduler/mod.rs']) {
    console.log('#### ' + file);
    readLines(file).forEach((line, index) => {
      for (const literal of literalsIn(line)) {
        if (hasNonAscii(literal)) {
          console.log(`${index + 1}: ${JSON.stringify(literal)}`);
        }
      }
    });
  }
  process.exit(0);
}

// [name, file, anchor, occurrence, offset, mode]
const entries = [
  // ---- 强度档位文案（界面单选框与 hover 提示）----
  ['fastLabel', 'speedtest/mod.rs', 'Self::Fast =>', 0, 0],
  ['standardLabel', 'speedtest/mod.rs', 'Self::Standard =>', 0, 0],
  ['deepLabel', 'speedtest/mod.rs', 'Self::Deep =>', 0, 0],
  ['fastDescription', 'speedtest/mod.rs', 'Self::Fast =>', 1, 0],
  ['standardDescription', 'speedtest/mod.rs', 'Self::Standard =>', 1, 0],
  ['deepDescription', 'speedtest/mod.rs', 'Self::Deep =>', 1, 0],

  // ---- 探测/测速的错误与结果文案 ----
  // 两个 probe_full* 分支共用同一个模板（Rust 里是两份重复的字面量）。
  ['downloadUnmeasurable', 'speedtest/mod.rs', '({detail})"', 0, 0],
  ['networkGateClosed', 'speedtest/mod.rs', '((host, port), Err(', 0, 0],
  ['noNodesToTest', 'speedtest/mod.rs', 'anyhow::bail!(', 0, 0],
  ['noNodesOrAllFailed', 'speedtest/mod.rs', 'anyhow::bail!(', 1, 0],
  ['googleVerifyDone', 'speedtest/mod.rs', 'self.tasks.complete(', 0, 3],
  ['onlyReceivedBytes', 'speedtest/mod.rs', 'if total < 64 {', 0, 1],
  ['publicWebsiteNote', 'speedtest/mod.rs', 'if looks_like_public_website(&head) {', 0, 3],

  // ---- 日志（tracing 的 target 消息，同样逐字保留）----
  ['reusedVerifiedLog', 'speedtest/mod.rs', 'info!(reused = reused.len(),', 0, 0],
  ['downloadTopMeasuredLog', 'speedtest/mod.rs', 'info!(measured, ', 0, 0],

  // ---- scheduler/mod.rs ----
  ['refillSparseLog', 'scheduler/mod.rs', 'floor = SPARSE_VERIFIED_FLOOR,', 0, 2],
  ['refillDoneLog', 'scheduler/mod.rs', 'avg = ?sum.avg_ms,', 0, 1],
  ['refillFailedLog', 'scheduler/mod.rs', 'Err(e) => warn!(error = %e,', 0, 0],
  ['refillCountFailedLog', 'scheduler/mod.rs', 'Err(e) => warn!(error = %e,', 1, 0],
];

// 测试里的 assert 消息（只在 tests/ 用，不在 verify-cjk 的扫描范围里，照样不能手打）。
const testEntries = [
  ['concurrencyNotGreedy', 'scheduler/mod.rs', 'assert_eq!(opts.concurrency, REFILL_CONCURRENCY,', 0, 0],
  ['noDownloadThroughput', 'scheduler/mod.rs', '!opts.measure_download,', 0, 1],
  ['timeoutFollowsSettings', 'scheduler/mod.rs', 'assert_eq!(opts.timeout_ms, 2500,', 0, 0],
  ['reuseRecentlyVerified', 'scheduler/mod.rs', 'assert_eq!(opts.reuse_verified_minutes, 30,', 0, 0],
];

function pickLiteral(lines, anchor, occurrence, offset, name, mode = 'nonAscii') {
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) {
      hits.push(index);
    }
  });
  if (hits.length <= occurrence) {
    console.error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
    process.exit(1);
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
      `${name}: expected exactly 1 ${mode} literal at line ${index + 1}, got ${accepted.length} in:\n${line}`,
    );
    process.exit(1);
  }
  return { text: accepted[0], line: index + 1 };
}

function toCppArray(text) {
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

function unescapeRust(text) {
  // Rust 源码里的 \n \t \" \\ 在 C++ 里写法相同，原样留着即可；这条链只是显式记录意图。
  return text;
}

function emitFunction(name, file, line, text) {
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
    const escaped = unescapeRust(text).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    body.push(`    return QStringLiteral("${escaped}");`);
  }
  body.push('}');
  body.push('');
  return { body: body.join('\n'), declaration: `QString ${name}();`, skeleton, codePoints: [...text].length };
}

const report = [];
const buckets = { speedtest: [], scheduler: [], schedulerTests: [] };
for (const [name, file, anchor, occurrence, offset, mode] of entries) {
  const lines = readLines(file);
  const { text, line } = pickLiteral(lines, anchor, occurrence, offset, name, mode);
  const emitted = emitFunction(name, file, line, text);
  const bucket = file.startsWith('speedtest') ? buckets.speedtest : buckets.scheduler;
  bucket.push(emitted);
  report.push({ name, file, line, codePoints: emitted.codePoints, asciiSkeleton: emitted.skeleton });
}
for (const [name, file, anchor, occurrence, offset, mode] of testEntries) {
  const lines = readLines(file);
  const { text, line } = pickLiteral(lines, anchor, occurrence, offset, name, mode);
  buckets.schedulerTests.push(emitFunction(name, file, line, text));
}

const FORMAT_DOC = `
// Rust 的 format!：第 i 个 \`{}\` 按顺序换成 positional[i]，\`{name}\` 按 marker 替换。
// 只扫一遍模板，插进去的值不会被二次求值（与 Rust 一致）。
`;

function headerTop(comment) {
  return [
    '// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。',
    comment,
    '#pragma once',
    '',
    '#include <QString>',
    '#include <QVector>',
    '',
    '#include <initializer_list>',
    '',
  ];
}

function formatImpl(ns) {
  return [
    'QString format(const QString &text, const QVector<QString> &positional,',
    `               std::initializer_list<${ns}::Arg> named)`,
    '{',
    '    QString out;',
    '    out.reserve(text.size() + 32);',
    '    qsizetype positionalIndex = 0;',
    '    for (qsizetype index = 0; index < text.size();) {',
    "        if (text.at(index) == QLatin1Char('{')) {",
    '            if (text.mid(index, 2) == QLatin1String("{}")) {',
    '                out += positionalIndex < positional.size() ? positional.at(positionalIndex)',
    '                                                          : QString();',
    '                ++positionalIndex;',
    '                index += 2;',
    '                continue;',
    '            }',
    '            bool matched = false;',
    '            for (const Arg &arg : named) {',
    '                const QString marker = QLatin1String(arg.marker);',
    '                if (!marker.isEmpty() && text.mid(index, marker.size()) == marker) {',
    '                    out += arg.value;',
    '                    index += marker.size();',
    '                    matched = true;',
    '                    break;',
    '                }',
    '            }',
    '            if (matched) {',
    '                continue;',
    '            }',
    '        }',
    '        out += text.at(index);',
    '        ++index;',
    '    }',
    '    return out;',
    '}',
    '',
  ];
}

function argStruct() {
  return [
    '/// 具名占位符：marker 写 Rust 模板里的原样（含花括号），例如 "{detail}"。',
    'struct Arg {',
    '    const char *marker;',
    '    QString value;',
    '};',
    '',
    FORMAT_DOC.trimStart(),
    'QString format(const QString &text, const QVector<QString> &positional = {},',
    '               std::initializer_list<Arg> named = {});',
    '',
  ];
}

// ---- speedtest messages ----
const speedtestHeader = [
  ...headerTop(
    '// 对应 Rust src/speedtest/mod.rs 里的用户可见文本（界面档位文案、测速结果与错误消息、日志）。',
  ),
  'namespace Speedtest {',
  'namespace Messages {',
  '',
  ...argStruct(),
  ...buckets.speedtest.map((item) => item.declaration),
  '',
  '}',
  '}',
  '',
].join('\n');

const speedtestSource = [
  '// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。',
  '// 这些文本要与 vpn-parser/src/speedtest/mod.rs 逐字一致：档位文案进界面，测速消息进',
  '// latency message 与日志，两个实现读同一份库，比较字符串时依赖这些文本。',
  '// 模板里的 {} / {latency_ms} / {detail} 等占位符原样保留，由调用方按 Rust format! 语义替换。',
  '#include "speedtest/SpeedtestMessages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace Speedtest {',
  'namespace Messages {',
  'namespace {',
  'QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  ...formatImpl('Speedtest::Messages'),
  ...buckets.speedtest.map((item) => item.body),
  '}',
  '}',
  '',
].join('\n');

// ---- scheduler messages ----
const schedulerHeader = [
  ...headerTop('// 对应 Rust src/scheduler/mod.rs 里写进日志的文本。'),
  'namespace Scheduler {',
  'namespace Messages {',
  '',
  ...argStruct(),
  ...buckets.scheduler.map((item) => item.declaration),
  '',
  '}',
  '}',
  '',
].join('\n');

const schedulerSource = [
  '// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。',
  '// 对应 Rust src/scheduler/mod.rs 的 tracing 日志文本。',
  '#include "scheduler/SchedulerMessages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace Scheduler {',
  'namespace Messages {',
  'namespace {',
  'QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  ...formatImpl('Scheduler::Messages'),
  ...buckets.scheduler.map((item) => item.body),
  '}',
  '}',
  '',
].join('\n');

// ---- scheduler 测试断言消息（header-only）----
const schedulerTests = [
  '// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。',
  '// 对应 Rust src/scheduler/mod.rs 的 #[test] 断言消息：规则读不通时，测试要先能说明为什么。',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  '#include <initializer_list>',
  '',
  'namespace SchedulerTests {',
  'namespace {',
  'inline QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  ...buckets.schedulerTests.map((item) => item.body.replace('QString ', 'inline QString ')),
  '}',
  '',
].join('\n');

fs.writeFileSync(OUT.speedtestHeader, speedtestHeader);
fs.writeFileSync(OUT.speedtestSource, speedtestSource);
fs.writeFileSync(OUT.schedulerHeader, schedulerHeader);
fs.writeFileSync(OUT.schedulerSource, schedulerSource);
fs.writeFileSync(OUT.schedulerTests, schedulerTests);

console.log(JSON.stringify(report, null, 0));
console.log(
  `generated ${buckets.speedtest.length} speedtest + ${buckets.scheduler.length} scheduler + ${buckets.schedulerTests.length} test message(s)`,
);
