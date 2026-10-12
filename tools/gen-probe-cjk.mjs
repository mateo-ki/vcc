import fs from 'node:fs';

// Generates a pure-ASCII C++ code-point table for the user-visible text in
// src/proxy_probe.rs (the mihomo real-connectivity prober):
//   node tools/gen-probe-cjk.mjs
//
// Why scripted: these strings end up in the UI and in logs, so they must match the
// Rust implementation character for character, and hand-typing them in this
// environment silently drops CJK characters.
//
// Locating scheme: ASCII anchor + occurrence + line offset. Hard failure when
//   1. the anchor matches 0 lines;
//   2. the anchor is ambiguous without an explicit occurrence;
//   3. the target line does not hold exactly one non-ASCII literal.
// The printed ASCII skeleton is what a human eyeballs against the Rust source.
const RUST = 'D:/project/rust/vs/vpn-parser/src';
const PROBE = 'proxy_probe.rs';

// [name, file, anchor, occurrence, offset, expected ASCII skeleton]
const entries = [
  ['mihomoNotFound', PROBE, 'let mihomo = find_mihomo_executable().ok_or_else', 0, 1, ' mihomo.exe MIHOMO_PATH Mihomo  bin/mihomo/mihomo.exe'],
  ['mihomoBatchFailed', PROBE, 'let (ids, outcome) = joined.map_err', 0, 0, 'Mihomo : {error}'],
  ['mihomoConfigUnsupported', PROBE, 'message: format!("Mihomo', 1, 0, 'Mihomo : {reason}'],
  ['mihomoSpawnFailed', PROBE, '.with_context(|| format!(', 0, 0, ' {} '],
  ['mihomoStartupFailed', PROBE, 'return Err(anyhow!("Mihomo', 0, 0, 'Mihomo : {detail}'],
  ['mihomoDelayOk', PROBE, 'message: format!("{delay} ms', 0, 0, '{delay} ms (  {target})'],
  ['mihomoDelayFailed', PROBE, 'let result = match outcome {', 0, 11, ': {error}'],
  ['mihomoDelayNoValue', PROBE, '_ => Err(format!(', 0, 0, '{target_name}: '],
  ['mihomoResultCountMismatch', PROBE, 'if results.len() != total || results.iter().any(Option::is_none) {', 0, 1, ''],
  ['mihomoValidateTaskFailed', PROBE, 'let (mut batch, outcome) = joined.map_err', 0, 0, ': {error}'],
  ['mihomoUnknownConfigError', PROBE, 'let text = diagnostic', 0, 5, ''],
  ['mihomoProtocolUnsupported', PROBE, 'nodes_to_clash_proxies(std::slice::from_ref(node))', 0, 5, 'Mihomo : {}'],
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);
const literalsIn = (line) => [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);

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
  const index = hits[occurrence] + offset;
  const line = lines[index];
  if (line === undefined) fail(`${name}: line ${index + 1} out of range`);
  const found = literalsIn(line).filter(hasNonAscii);
  if (found.length !== 1) {
    fail(`${name}: expected exactly 1 non-ASCII literal at ${file}:${index + 1}, got ${found.length}:\n${line}`);
  }
  const text = found[0];
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  if (skeleton !== expectSkeleton) {
    fail(`${name}: ${file}:${index + 1} skeleton is ${JSON.stringify(skeleton)} but entry expects ${JSON.stringify(expectSkeleton)}`);
  }
  return { text, line: index + 1 };
}

const toCppArray = (text) =>
  [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));

const functions = [];
const declarations = [];
const claimed = new Set();

for (const entry of entries) {
  const [name, file] = entry;
  const { text, line } = pickLiteral(entry);
  claimed.add(file + ':' + line);
  const parts = toCppArray(text);
  const skeleton = [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- ${file}:${line}, ${[...text].length} code points (ASCII included)`);
  body.push(`// ASCII skeleton: ${JSON.stringify(skeleton)}`);
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
  console.log(`${name}: ${file}:${line} ${[...text].length} chars skeleton=${JSON.stringify(skeleton)}`);
}

// Coverage: every non-ASCII literal in proxy_probe.rs must be claimed here or in
// proxy/KernelConfigMessages.mjs (shared texts such as the concurrency-permit one).
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
  '// Generated by tools/gen-probe-cjk.mjs from the Rust source - do not edit by hand.',
  '// User-visible text of the mihomo real-connectivity prober (proxy_probe.rs).',
  '// Templates keep their {} / {error} / {reason} markers; callers substitute via',
  '// Proxy::Messages::fill (see proxy/KernelConfigMessages.h).',
  '#pragma once',
  '',
  '#include <QString>',
  '',
  'namespace ProxyProbe::Messages {',
  '',
  ...declarations,
  '',
  '}',
  '',
].join('\n');

const source = [
  '// Generated by tools/gen-probe-cjk.mjs from the Rust source - do not edit by hand.',
  '// Code points only: no CJK byte ever appears in this file, so a dropped character',
  '// cannot happen silently. verify-cjk-literals.mjs checks the literals that do exist.',
  '#include "proxy_probe/ProbeMessages.h"',
  '',
  '#include <initializer_list>',
  '',
  'namespace ProxyProbe::Messages {',
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

const HEADER_OUT = 'D:/project/cpp/vcc/src/proxy_probe/ProbeMessages.h';
const SOURCE_OUT = 'D:/project/cpp/vcc/src/proxy_probe/ProbeMessages.cpp';

if (process.argv.includes('--check')) {
  const currentHeader = fs.readFileSync(HEADER_OUT, 'utf8');
  const currentSource = fs.readFileSync(SOURCE_OUT, 'utf8');
  if (currentHeader !== header || currentSource !== source) {
    fail('generated files are out of date, re-run: node tools/gen-probe-cjk.mjs');
  }
  console.log('probe cjk messages up to date');
} else {
  fs.writeFileSync(HEADER_OUT, header);
  fs.writeFileSync(SOURCE_OUT, source);
  console.log(`wrote ${functions.length} message(s)`);
}

if (skipped.length) {
  console.log('unclaimed non-ASCII literals (review, not a failure):');
  for (const entry of skipped) console.log('  ' + entry);
}
