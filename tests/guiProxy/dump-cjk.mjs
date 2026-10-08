import fs from 'node:fs';

// 临时勘查脚本：把 Rust 源里所有含非 ASCII 的字符串字面量按行号列出来。
const file = process.argv[2];
const from = Number(process.argv[3] ?? 1);
const to = Number(process.argv[4] ?? 1e9);
const text = fs.readFileSync(file, 'utf8');
const lines = text.split(/\r?\n/);
const starts = [0];
for (let i = 0; i < lines.length; i += 1) {
    starts.push(starts[i] + lines[i].length + 1);
}

function lineOf(pos) {
    let low = 0;
    let high = starts.length - 1;
    while (low < high) {
        const mid = (low + high) >> 1;
        if (starts[mid + 1] <= pos) low = mid + 1;
        else high = mid;
    }
    return low + 1;
}

const BS = String.fromCharCode(92);
const QUOTE = String.fromCharCode(34);
const out = [];
let index = 0;
while (index < text.length) {
    const ch = text[index];
    if (ch === '/' && text[index + 1] === '/') {
        index = text.indexOf('\n', index);
        if (index < 0) break;
        index += 1;
        continue;
    }
    if (ch === '/' && text[index + 1] === '*') {
        index = text.indexOf('*/', index);
        if (index < 0) break;
        index += 2;
        continue;
    }
    if (ch === 'r') {
        let j = index + 1;
        let hashes = 0;
        while (text[j] === '#') { hashes += 1; j += 1; }
        if (text[j] === QUOTE && hashes <= 3) {
            const closer = QUOTE + '#'.repeat(hashes);
            const end = text.indexOf(closer, j + 1);
            if (end > 0) {
                const body = text.slice(j + 1, end);
                if (/[^\x00-\x7f]/.test(body)) out.push({ line: lineOf(index), raw: body });
                index = end + closer.length;
                continue;
            }
        }
    }
    if (ch === QUOTE) {
        let i = index + 1;
        let body = '';
        while (i < text.length) {
            const c = text[i];
            if (c === BS) {
                if (text[i + 1] === '\n' || text[i + 1] === '\r') {
                    i += 2;
                    if (text[i - 1] === '\n' && text[i] === '\r') i += 1;
                    while (i < text.length && (text[i] === ' ' || text[i] === '\t')) i += 1;
                    continue;
                }
                body += text.slice(i, i + 2);
                i += 2;
                continue;
            }
            if (c === QUOTE) break;
            body += c;
            i += 1;
        }
        const unescaped = body
            .replace(new RegExp('\\\\u\\{([0-9a-fA-F]{1,6})\\}', 'g'), (_, h) => String.fromCodePoint(parseInt(h, 16)))
            .replace(new RegExp(BS + 'n', 'g'), '\n')
            .replace(new RegExp(BS + 't', 'g'), '\t')
            .replace(new RegExp(BS + QUOTE, 'g'), QUOTE)
            .replace(new RegExp(BS + BS, 'g'), BS);
        if (/[^\x00-\x7f]/.test(unescaped)) out.push({ line: lineOf(index), raw: unescaped });
        index = i + 1;
        continue;
    }
    index += 1;
}

for (const item of out) {
    if (item.line < from || item.line > to) continue;
    const skeleton = [...item.raw].filter((c) => c.codePointAt(0) < 0x7f).join('');
    console.log(item.line + '\t' + JSON.stringify(skeleton) + '\t' + JSON.stringify(item.raw));
}
