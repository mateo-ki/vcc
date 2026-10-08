import fs from 'node:fs';

// 从 Rust 源里抽取订阅拉取模块的中文提示文本，生成纯 ASCII 的 C++ 码点表：
//   node tools/gen-collector-cjk-codepoints.mjs
// 这些文本是用户可见的错误消息（会进 last_status、任务面板和日志），必须与
// vpn-parser 逐字一致，所以不允许手打——本环境会静默丢掉 astral 平面的字符。
//
// 定位方式：ASCII 锚点 + 第 N 次命中 + 精确行偏移，并且（可选）校验命中行号，
// 找到 0 个、多于 1 个、行号对不上都直接报错退出，绝不猜。
// 每条生成后会打印「码点数 + ASCII 骨架」，方便核对有没有抓错锚点。
const RUST = 'D:/project/rust/vs/vpn-parser/src';
const OUT_HEADER = 'D:/project/cpp/vcc/src/collector/CollectorMessages.h';
const OUT_SOURCE = 'D:/project/cpp/vcc/src/collector/CollectorMessages.cpp';

// [name, file, anchor, occurrence, offset, expectedLine, mode]
const entries = [
  ['viaMirrorNote', 'collector/mod.rs', 'Some(host) => format!(', 0, 0, 242],
  ['directCandidatePrefix', 'collector/mod.rs', 'format!("{host}: {message}")', 0, 2, 412],
  ['candidateTaskFailed', 'collector/mod.rs', 'Err(error) => failures.push(format!(', 0, 0, 414],
  ['mirrorGateClosed', 'collector/mod.rs', '.map_err(|_|', 0, 0, 434],
  ['hostGateClosed', 'collector/mod.rs', '.map_err(|_|', 1, 0, 439],
  [
    'responseHeaderTimeout',
    'collector/mod.rs',
    'Err(_elapsed) => {',
    0,
    2,
    456,
  ],
  [
    'connectTimeoutFailed',
    'collector/mod.rs',
    'if error.is_timeout() && error.is_connect() {',
    0,
    1,
    462,
  ],
  ['gistDiscoveryName', 'collector/mod.rs', 'Source::scratch(GIST_DISCOVERY_SOURCE_ID', 0, 0, 545],
  ['gistPageNotModified', 'collector/mod.rs', 'summary.stopped_reason =', 0, 1, 583],
  ['gistPageFetchFailed', 'collector/mod.rs', 'summary.stopped_reason = format!(', 0, 1, 588],
  ['gistPageNoGists', 'collector/mod.rs', 'if gists.is_empty() {', 0, 1, 596],
  ['gistPageAllSeen', 'collector/mod.rs', 'if fresh.is_empty() {', 0, 2, 613],
  ['gistPageCap', 'collector/mod.rs', 'if summary.pages_scanned >= max_pages {', 0, 1, 628],
  ['gistRegisterCap', 'collector/mod.rs', 'if summary.registered >= GIST_MAX_NEW_PER_RUN {', 0, 1, 632],
  ['gistSearchUrlUnparsable', 'collector/mod.rs', 'let mut next = reqwest::Url::parse(&page_url)', 0, 1, 636],
  ['gistScanComplete', 'collector/mod.rs', 'if summary.stopped_reason.is_empty() {', 0, 1, 642],
  ['gistDiscoveryFinished', 'collector/mod.rs', 'let message = format!(', 2, 1, 645],
  ['gistPageHandled', 'collector/mod.rs', 'registered, nodes, rejected, pending,', 0, 0, 625],
  ['gistRemarkAuto', 'collector/mod.rs', 'url_template: format!("{raw_base}/{}/{}/raw", gist.user, gist.id),', 0, 2, 684],
  ['gistRegisterFailed', 'collector/mod.rs', 'warn!(gist = %gist.id,', 0, 0, 690],
  ['attemptsSuffix', 'collector/mod.rs', 'if attempts > 1 && !result.success && is_retryable_fetch_failure', 0, 1, 830],
  ['degradedStreakWarn', 'collector/mod.rs', 'streak >= DEGRADED_FAIL_STREAK && streak != source.fail_streak', 0, 3, 841],
  ['recordOutcomeFailed', 'collector/mod.rs', 'Err(error) => warn!(source = %source.name,', 0, 0, 844],
  ['subtaskExited', 'collector/mod.rs', '&source_metadata[index];', 0, 1, 959],
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function pickLiteral(lines, anchor, occurrence, offset, expectedLine, name, mode = 'nonAscii') {
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
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
  if (expectedLine !== undefined && index + 1 !== expectedLine) {
    console.error(
      `${name}: anchor "${anchor}" #${occurrence} +${offset} lands on line ${index + 1}, ` +
        `expected ${expectedLine}:\n${line}`,
    );
    process.exit(1);
  }
  const found = mode === 'any' ? literalsIn(line) : literalsIn(line).filter(hasNonAscii);
  if (found.length !== 1) {
    console.error(
      `${name}: expected exactly 1 ${mode} literal at line ${index + 1}, got ${found.length} in:\n${line}`,
    );
    process.exit(1);
  }
  return { text: found[0], line: index + 1 };
}

function toCppArray(text) {
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const functions = [];
const declarations = [];
const report = [];

for (const [name, file, anchor, occurrence, offset, expectedLine, mode] of entries) {
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  const { text, line } = pickLiteral(lines, anchor, occurrence, offset, expectedLine, name, mode);
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
  report.push({ name, codePoints: [...text].length, asciiSkeleton: skeleton });
}

const source = [
  '// 由 tools/gen-collector-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
  '// 这些文本要与 vpn-parser/src/collector/mod.rs 逐字一致（错误消息会写进 last_status、',
  '// 任务面板和日志，两个实现读同一份库，字符串比较依赖这个格式）。',
  '// 模板里的 {} / {host} / {error} / {attempts} 等占位符原样保留，',
  '// 由下面的 format() 按 Rust format! 的语义替换。',
  '#include "collector/CollectorMessages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace Collector {',
  'namespace Messages {',
  'namespace {',
  'QString fromCodePoints(std::initializer_list<char32_t> points)',
  '{',
  '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
  '}',
  '',
  '}',
  '',
  'QString format(const QString &text, const QVector<QString> &positional,',
  '               std::initializer_list<Arg> named)',
  '{',
  '    QString out;',
  '    out.reserve(text.size() + 32);',
  '    qsizetype positionalIndex = 0;',
  '    for (qsizetype index = 0; index < text.size();) {',
  '        if (text.at(index) == QLatin1Char(\'{\')) {',
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
  ...functions,
  '} // namespace Messages',
  '} // namespace Collector',
  '',
].join('\n');

const header = [
  '// 由 tools/gen-collector-cjk-codepoints.mjs 生成，请勿手改。',
  '// 对应 Rust src/collector/mod.rs 里的用户可见文本。',
  '#pragma once',
  '',
  '#include <QString>',
  '#include <QVector>',
  '',
  '#include <initializer_list>',
  '',
  'namespace Collector {',
  'namespace Messages {',
  '',
  '/// 具名占位符：marker 写 Rust 模板里的原样（含花括号），例如 "{error}"。',
  'struct Arg {',
  '    const char *marker;',
  '    QString value;',
  '};',
  '',
  '/// Rust 的 `format!`：第 i 个 `{}` 按顺序换成 positional[i]，`{name}` 按 marker 替换。',
  '/// 只扫一遍模板，插进去的值不会被二次求值（与 Rust 一致）。',
  'QString format(const QString &text, const QVector<QString> &positional = {},',
  '               std::initializer_list<Arg> named = {});',
  '',
  ...declarations,
  '',
  '} // namespace Messages',
  '} // namespace Collector',
  '',
].join('\n');

fs.writeFileSync(OUT_SOURCE, source);
fs.writeFileSync(OUT_HEADER, header);
for (const item of report) {
  console.log(`${item.name}: ${item.codePoints} codepoints, skeleton = ${JSON.stringify(item.asciiSkeleton)}`);
}
console.log('generated ' + entries.length + ' message(s)');
