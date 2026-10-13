// 异步收尾的所有权守卫。跑法：`node tools/check-async-owners.mjs`（非零退出 = 有问题）。
//
// AppContext::runAsync(work, onUi, uiOwner) 把 onUi 投递到 qApp —— qApp 永远比任何页面活得久，
// 所以 onUi 里裸捕 `this` 时，页面先死就是 use-after-free。它不会当场崩：写坏的是下一个借到
// 这块内存的测试，报"access violation"报在无关的那条上。2026-10-13 CI 上 6 个 GUI 套件同时红
// 就是这个形状，13 处调用没传 uiOwner。
//
// 判据（只看 onUi，work 在池线程里跑、不碰 widget）：一条 runAsync 调用若同时满足
//   1) 只有两个实参（没传 uiOwner），
//   2) 第二个实参的捕获列表里有 `this`，
//   3) 整条调用里没有任何 QPointer，
// 就判红。要么传 `this` 当 uiOwner（runAsync 会在 GUI 线程上二次判空），要么自己拿
// QPointer 守住 —— 一条调用里两者都不许缺。
import fs from 'node:fs';
import path from 'node:path';

const CLOSE = { ')': '(', ']': '[', '}': '{' };
const OPEN = '([{';

function splitArgs(text, from) {
    // text[from] 已经是 runAsync 后面的 '('。返回 { args: [..], end: 右括号下标 }。
    const args = [];
    let cur = '';
    let depth = 0;
    let quote = null;
    for (let i = from; i < text.length; ++i) {
        const c = text[i];
        if (quote !== null) {
            cur += c;
            if (c === '\\') {
                cur += text[++i];
            } else if (c === quote) {
                quote = null;
            }
            continue;
        }
        if (c === '"' || c === "'" || c === '`') {
            quote = c;
            cur += c;
            continue;
        }
        if (c === '/' && text[i + 1] === '/') {
            i = text.indexOf('\n', i);
            if (i < 0) {
                break;
            }
            cur += '\n';
            continue;
        }
        if (OPEN.includes(c)) {
            ++depth;
            if (depth === 1) {
                continue;
            }
        } else if (CLOSE[c]) {
            --depth;
            if (depth === 0) {
                args.push(cur);
                return { args, end: i };
            }
        } else if (c === ',' && depth === 1) {
            args.push(cur);
            cur = '';
            continue;
        }
        if (depth >= 1) {
            cur += c;
        }
    }
    return null;
}

function walk(dir) {
    return fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            return walk(full);
        }
        return full.endsWith('.cpp') ? [full] : [];
    });
}

const offenders = [];
for (const file of walk('src')) {
    const text = fs.readFileSync(file, 'utf8');
    const scan = /runAsync\(/g;
    let match = scan.exec(text);
    while (match !== null) {
        const parsed = splitArgs(text, match.index + match[0].length - 1);
        if (parsed === null) {
            break;
        }
        const call = text.slice(match.index, parsed.end + 1);
        if (parsed.args.length === 2) {
            const captured = /\[\s*([^\]]*)\]/.exec(parsed.args[1]);
            const capturesThis = captured !== null && /\bthis\b/.test(captured[1]);
            if (capturesThis && !/QPointer/.test(call)) {
                offenders.push(`${file}:${text.slice(0, match.index).split('\n').length}`);
            }
        }
        scan.lastIndex = parsed.end;
        match = scan.exec(text);
    }
}

if (offenders.length > 0) {
    console.error(`runAsync onUi captures raw this with no owner guard in ${offenders.length} place(s):`);
    for (const where of offenders) {
        console.error(`  ${where}`);
    }
    console.error('pass `this` as the third argument (uiOwner), or hold a QPointer in the continuation');
    process.exit(1);
}
console.log('async owner ok: every runAsync continuation that touches this is guarded');
