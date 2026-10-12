import fs from 'node:fs';
import path from 'node:path';

// 回验：C++ 里出现的每一条非 ASCII 字符串字面量，必须在 Rust 源里逐字存在。
// 中文/emoji 由我转录时最容易静默丢字符，这个脚本把这类错误变成硬失败。
//   node tools/verify-cjk-literals.mjs
const CPP_ROOT = 'D:/project/cpp/vcc/src';
const RUST_ROOT = 'D:/project/rust/vs/vpn-parser/src';
// 界面文案是本程序自己的中文，不该拿去 Rust 里找。
const allowlistFile = 'D:/project/cpp/vcc/tools/cjk-allowlist.txt';

function walk(dir, out = []) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full, out);
    else out.push(full);
  }
  return out;
}

const rustText = walk(RUST_ROOT)
  .filter((f) => f.endsWith('.rs'))
  .map((f) => fs.readFileSync(f, 'utf8'))
  .join('\n');
// Rust 源码里中文常用 \u{} 转义写，把这些也还原出来一起参与匹配。
const rustNormalized = rustText.replace(/\\u\{([0-9a-fA-F]+)\}/g, (_, hex) =>
  String.fromCodePoint(parseInt(hex, 16)),
);

const allowlist = fs.existsSync(allowlistFile)
  ? new Set(
      fs
        .readFileSync(allowlistFile, 'utf8')
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line && !line.startsWith('#')),
    )
  : new Set();

function stripComments(text) {
  return text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^[ \t]*\/\/.*$/gm, '');
}

const literals = new Set();
// 文案本身不指出去重后的字面量在哪，200 条报错就没法按文件分派 —— 记一份代表文件。
const origin = new Map();
for (const file of walk(CPP_ROOT).filter((f) => /\.(h|cpp)$/.test(f))) {
  const text = stripComments(fs.readFileSync(file, 'utf8'));
  // 只找普通字面量（含 QStringLiteral / fromUtf8 / 裸串）。
  for (const match of text.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)) {
    const raw = match[1];
    if (!/[^\x00-\x7f]/.test(raw)) continue;
    let decoded = raw;
    try {
      decoded = JSON.parse('"' + raw + '"');
    } catch {
      continue;
    }
    literals.add(decoded);
    if (!origin.has(decoded)) origin.set(decoded, file.replace(/\\/g, '/').split('/src/')[1]);
  }
}

// 逐段核对：只看连续的非 ASCII 片段（≥2 字符）。%1 这类占位符是我这边的格式差异，
// 不该算失败；但丢字、错字一定会让某个片段在 Rust 里找不到。
function segments(literal) {
  return (literal.match(/[^\x00-\x7f]{2,}/g) ?? []).filter(
    (segment) => !allowlist.has(segment),
  );
}

const missing = [];
for (const literal of literals) {
  for (const segment of segments(literal)) {
    if (!rustNormalized.includes(segment)) {
      missing.push({ literal, segment, file: origin.get(literal) });
    }
  }
}

console.log(`checked ${literals.size} non-ASCII literal(s) in src/`);
const byFile = new Map();
for (const { file } of missing) {
  byFile.set(file, (byFile.get(file) ?? 0) + 1);
}
for (const [file, count] of [...byFile].sort((a, b) => b[1] - a[1])) {
  console.log(`  ${count} 段 ${file}`);
}
for (const { literal, segment, file } of missing) {
  console.log(`NOT IN RUST SOURCE: ${JSON.stringify(segment)}  (${file} 来自 ${JSON.stringify(literal)})`);
}
if (missing.length && process.argv.includes('--update')) {
  // 只在"确实是我们自己写的新文案"时用这个模式，之后逐条审一下 allowlist。
  const extra = [...new Set(missing.map((entry) => entry.segment))];
  fs.appendFileSync(allowlistFile, extra.join('\n') + '\n');
  console.log(`allowlist += ${extra.length} 条`);
}
if (missing.length) {
  process.exit(1);
}
console.log('ok');
