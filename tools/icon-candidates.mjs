// 把抓到的候选矢量图拼成一张对照页：每个图标给 128/48/32/16px，浅色和深色底各一行。
// 用法：node tools/icon-candidates.mjs tmp  → 生成 tmp/icon-candidates.html
import fs from 'node:fs';
import path from 'node:path';

const dir = path.resolve(process.argv[2] ?? 'tmp');
// 文件名 → 说明（许可是查 iconify collections 拿到的真实值）
const META = [
    ['ic-fluent-emoji-flat-hamster.svg', 'Fluent Emoji Flat · 仓鼠', 'MIT（微软）彩色，体型最接近土拨鼠'],
    ['ic-noto-hamster.svg', 'Noto Emoji · 仓鼠', 'Apache 2.0（Google）彩色写实一点'],
    ['ic-twemoji-hamster.svg', 'Twemoji · 仓鼠', 'CC BY 4.0 彩色，需署名'],
    ['ic-openmoji-hamster.svg', 'OpenMoji · 仓鼠', 'CC BY-SA 4.0 线框，SA 有传染性'],
    ['ic-iconmind-hamster-duotone-bold.svg', 'IconMind · 仓鼠 双色粗', 'MIT，双色块面，最像"应用图标"'],
    ['ic-iconmind-hamster-outline-regular.svg', 'IconMind · 仓鼠 线稿', 'MIT，细线，小尺寸容易糊'],
    ['ic-game-icons-beaver.svg', 'Game Icons · 海狸', 'CC BY 3.0 设计师剪影，可单色重上色'],
    ['ic-mdi-rodent.svg', 'Material Design · 啮齿类', 'Apache 2.0 通用鼠形，最抽象'],
];

const tile = (svg, size, dark) =>
    `<div class="cell${dark ? ' dark' : ''}"><div class="box" style="width:${size}px;height:${size}px">${svg}</div><span>${size}px${dark ? ' 深色' : ''}</span></div>`;

const rows = META.map(([file, name, note]) => {
    const p = path.join(dir, file);
    if (!fs.existsSync(p) || fs.statSync(p).size < 40) return '';
    // 保留各图标自己的 viewBox（emoji 系是 0 0 72 72、game-icons 是 0 0 128 128），
    // 只把固定 width/height 换成 100%，靠外层方框缩放。强行套 64 会把图形裁掉一半。
    let svg = fs.readFileSync(p, 'utf8');
    svg = svg.replace(/<svg\b([^>]*)>/, (m, attrs) => {
        const kept = attrs.replace(/\s(width|height)="[^"]*"/g, '');
        return `<svg${kept} width="100%" height="100%" style="position:absolute;inset:0">`;
    });
    return `<div class="card"><h2>${name}</h2><p>${note}</p>
      <div class="style"><em>浅底</em>${[128, 48, 32, 16].map((s) => tile(svg, s, false)).join('')}</div>
      <div class="style"><em>深底</em>${[128, 48, 32, 16].map((s) => tile(svg, s, true)).join('')}</div>
    </div>`;
}).join('\n');

fs.writeFileSync(path.join(dir, 'icon-candidates.html'), `<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<title>vcc 图标候选 · 真实矢量素材</title><style>
body{margin:0;padding:26px;font:14px/1.6 "Microsoft YaHei",system-ui,sans-serif;background:#eef1f5;color:#1c2430}
h1{font-size:20px;margin:0 0 4px}.sub{color:#6b7480;margin:0 0 20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(380px,1fr));gap:16px}
.card{background:#fff;border:1px solid #d3dae3;border-radius:14px;padding:16px}
.card h2{font-size:15px;margin:0 2px 2px}.card p{margin:0 0 12px;color:#6b7480;font-size:12.5px}
.style{display:flex;align-items:flex-end;gap:14px;flex-wrap:wrap;margin-bottom:10px}
.style em{font-style:normal;font-size:11.5px;color:#6b7480;flex:0 0 40px;text-align:right}
.cell{text-align:center}.cell span{display:block;font-size:10.5px;color:#6b7480;margin-top:4px}
.box{position:relative;display:inline-block;border-radius:22%;overflow:hidden;vertical-align:middle}
.dark .box{background:#12181f}.dark{padding:6px;border-radius:10px}
</style></head><body>
<h1>vcc 图标候选：真实矢量素材（不是我画的）</h1>
<p class="sub">来源 iconify（各开放图标库聚合）。每格 128 / 48 / 32 / 16px，浅底和深底各一行；方框只是示意圆角，实际 .ico 不做圆角。</p>
<div class="grid">${rows}</div>
</body></html>`);
console.log('写出 tmp/icon-candidates.html');
