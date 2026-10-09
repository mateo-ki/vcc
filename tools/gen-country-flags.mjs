import fs from 'node:fs';

// 从 vpn-parser 的 gui/widgets.rs 解析 country_flag_and_name 的 match 表，
// 生成纯 ASCII 码点表实现：node tools/gen-country-flags.mjs
//
// 为什么必须脚本化：每项是「国旗 emoji + 中文名」。国旗是 U+1F1E6..U+1F1FF 的区域
// 指示符**代理对**，我转录时会静默丢字符；中文同样不许手打。整张表只能从 Rust 源码
// 逐码点抽出来。
const RUST = 'D:/project/rust/vs/vpn-parser/src/gui/widgets.rs';
const OUT = 'D:/project/cpp/vcc/src/gui/CountryNames.cpp';

const unquote = (literal) => literal.slice(1, -1).replace(/\\"/g, '"').replace(/\\\\/g, '\\');
const PAIR = String.raw`("(?:[^"\\]|\\.)*")\s*,\s*("(?:[^"\\]|\\.)*")`;

const rows = [];
let fallback = null;
for (const line of fs.readFileSync(RUST, 'utf8').split(/\r?\n/)) {
  const named = new RegExp(`^\\s*"([A-Z]{2})"(?:\\s*\\|\\s*"([A-Z]{2})")?\\s*=>\\s*\\(${PAIR}\\),`)
    .exec(line);
  if (named) {
    rows.push({
      codes: [named[1], named[2]].filter(Boolean),
      flag: unquote(named[3]),
      name: unquote(named[4]),
    });
    continue;
  }
  const def = new RegExp(`^\\s*_\\s*=>\\s*\\(${PAIR}\\),`).exec(line);
  if (def && rows.length > 0 && fallback === null) {
    fallback = { flag: unquote(def[1]), name: unquote(def[2]) };
  }
}

if (rows.length < 19 || !fallback) {
  console.error(`解析失败：rows=${rows.length} fallback=${JSON.stringify(fallback)}`);
  process.exit(1);
}

const cps = (text) =>
  [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));

const body = [];
body.push('// 由 tools/gen-country-flags.mjs 从 vpn-parser 的 gui/widgets.rs 生成，请勿手改。');
body.push('// 国旗是区域指示符代理对（U+1F1E6..U+1F1FF），中文名与 emoji 都不允许手打转录。');
body.push('#include "gui/CountryNames.h"');
body.push('');
body.push('#include <QString>');
body.push('');
body.push('#include <initializer_list>');
body.push('#include <vector>');
body.push('');
body.push('namespace {');
body.push('');
body.push('using CodePoints = std::vector<char32_t>;');
body.push('');
body.push('CodePoints cp(std::initializer_list<char32_t> points)');
body.push('{');
body.push('    return CodePoints(points);');
body.push('}');
body.push('');
body.push('QString fromCodePoints(const CodePoints &points)');
body.push('{');
body.push('    return QString::fromUcs4(points.data(), qsizetype(points.size()));');
body.push('}');
body.push('');
body.push('struct Row {');
body.push('    std::vector<const char *> codes;');
body.push('    CodePoints flag;');
body.push('    CodePoints name;');
body.push('};');
body.push('');
body.push('const std::vector<Row> &rows()');
body.push('{');
body.push('    static const std::vector<Row> kRows = {');
for (const row of rows) {
  const codes = row.codes.map((code) => `"${code}"`).join(', ');
  body.push(`        { { ${codes} },`);
  body.push(`          cp({ ${cps(row.flag).join(', ')} }),`);
  body.push(`          cp({ ${cps(row.name).join(', ')} }),`);
  body.push('        },');
}
body.push('    };');
body.push('    return kRows;');
body.push('}');
body.push('');
body.push(`const CodePoints &fallbackFlag() // ${JSON.stringify(fallback.name)}`);
body.push('{');
body.push(`    static const CodePoints value{ ${cps(fallback.flag).join(', ')} };`);
body.push('    return value;');
body.push('}');
body.push('');
body.push('const CodePoints &fallbackName()');
body.push('{');
body.push(`    static const CodePoints value{ ${cps(fallback.name).join(', ')} };`);
body.push('    return value;');
body.push('}');
body.push('');
body.push('}');
body.push('');
body.push('namespace Gui {');
body.push('');
body.push('CountryDisplay countryFlagAndName(const QString &code)');
body.push('{');
body.push('    const QString upper = code.toUpper(); // Rust 的 to_ascii_uppercase');
body.push('    for (const Row &row : rows()) {');
body.push('        for (const char *candidate : row.codes) {');
body.push('            if (upper == QLatin1String(candidate)) {');
body.push('                return {fromCodePoints(row.flag), fromCodePoints(row.name)};');
body.push('            }');
body.push('        }');
body.push('    }');
body.push('    return {fromCodePoints(fallbackFlag()), fromCodePoints(fallbackName())};');
body.push('}');
body.push('');
body.push('}');
body.push('');

fs.writeFileSync(OUT, body.join('\n'));
console.log(`wrote ${OUT}: ${rows.length} 项 + 兜底 ${JSON.stringify(fallback.name)}`);
for (const row of rows) {
  console.log(`  ${row.codes.join('|')} flag=${[...row.flag].length}cp name=${JSON.stringify(row.name)}`);
}
