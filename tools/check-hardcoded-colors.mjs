// 静态守卫：src/gui 与 src/app 的字符串字面量里不许再出现写死的 #rrggbb。
// 跑法：node tools/check-hardcoded-colors.mjs
//
// Why：颜色只有一个来源 —— `Theme::Palette`。页面里按控件挂的样式表写 `{槽}`，由
// `Theme::setWidgetCss` 填色；自绘控件（NodeCardFrame/NodeBadge）在 paint 里取
// `Theme::current()`。运行时探针（AllPageColors.noHardcodedHexOutsideThePalette）只能
// 审到"这一轮真被建出来的控件"，条件分支里的颜色（比如表格某行才建的徽章）它看不见，
// 所以这条静态检查才是穷尽的那一道。

import fs from 'node:fs';
import path from 'node:path';

const ROOTS = ['src/gui', 'src/app'];
// 调色板本身当然是唯一可以写颜色的地方。
const ALLOW = new Set(['src/gui/Theme.cpp']);

function walk(dir, out = []) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name).replace(/\\/g, '/');
    if (entry.isDirectory()) walk(full, out);
    else if (/\.(h|cpp)$/.test(entry.name)) out.push(full);
  }
  return out;
}

// 注释里出现 #rrggbb 是"在解释一段颜色"，不是把它编进程序。只剥整行注释和块注释，
// 并且用等量空行替换，行号才对得上（行尾注释不动：URL 里的 // 会把真违规藏起来）。
function stripComments(text) {
  return text
    .replace(/\/\*[\s\S]*?\*\//g, (block) => '\n'.repeat(block.split('\n').length - 1))
    .replace(/^[ \t]*\/\/.*$/gm, '');
}

const violations = [];
let files = 0;
for (const dir of ROOTS) files += walk(dir).length;
for (const dir of ROOTS) {
  for (const file of walk(dir)) {
    if (ALLOW.has(file)) continue;
    const text = stripComments(fs.readFileSync(file, 'utf8'));
    // 只看字符串字面量内部的颜色。
    for (const match of text.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)) {
      for (const hex of match[1].match(/#[0-9a-fA-F]{6}\b/g) ?? []) {
        const line = text.slice(0, match.index).split('\n').length;
        violations.push(`${path.relative('.', file).replace(/\\/g, '/')}:${line}  ${hex}`);
      }
    }
  }
}

console.log(`checked ${files} file(s) under src/gui + src/app`);
for (const item of violations) console.log('写死的色值: ' + item);
if (violations.length) {
  console.log(`\n${violations.length} 处：把颜色改成 Theme 的语义槽（{accentBg} 这类）或 `
    + `Theme::current().<字段>，参考 src/gui/Theme.cpp 的 cssWithPalette()`);
  process.exit(1);
}
console.log('ok：颜色只有 Theme 一个来源');
