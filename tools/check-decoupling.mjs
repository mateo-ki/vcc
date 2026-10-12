// 分层与解耦守卫。跑法：`node tools/check-decoupling.mjs`（非零退出 = 有问题）。
//
// 为什么需要它：静态库允许"双向依赖"，链接 exe 时才把符号凑齐，所以 vccGui↔vccApp
// 那种缠了很久的环**一直能编过、测试全绿**，构建系统里写的"谁依赖谁"却是假的。
// 这里不看编译器，看两件事：
//   A. 代码事实：src/ 里每一条 `#include "x/y.h"` 归到它所属的 CMake 目标；
//   B. 声明事实：src/CMakeLists.txt 里每个目标自己写了哪些 target_link_libraries。
// 然后钉住五条规则（前四条红就退出码 1）：
//   1) 反向依赖：低层 include 高层的头；
//   2) 未声明耦合：include 边不在"自己声明 + 传染闭包"里；
//   3) 只靠传染：能编过，但没在自己身上声明（列出来，不判红 —— 传染是 CMake 的合法语义）；
//   4) 目标级依赖环 / 头文件 include 环；
//   5) 依赖名打错：vcc_port_layer 用 if(TARGET) 过滤依赖，名字写错会被**静默丢掉**，
//      所以这里凡是 vcc 开头、又不在已定义目标集合里的名字，一律判红。
import fs from 'node:fs';
import path from 'node:path';

const SRC = 'src';
const CMAKE = 'src/CMakeLists.txt';

// 期望的分层，数字越小越底层。改这张表等于改架构约定，要连 README 的目录说明一起改。
const LAYER = {
    vccCore: 0,
    vccModels: 1,
    vccNet: 2, vccSysProxy: 2,
    vccParser: 3, vccExport: 3, vccKernel: 3,
    vccKernelManager: 4,
    vccDb: 5, vccGeo: 5, vccPlatform: 5,
    vccProxy: 6, vccTasks: 6, vccRouter: 6,
    vccProbe: 7, vccCollector: 7, vccTelegram: 7,
    vccSpeedtest: 8, vccUpdater: 8,
    vccScheduler: 9,
    vccApi: 10,
    vccContext: 11,
    vccGui: 12,
    vccApp: 13,
    vcc: 14,
};

// 目录 -> 目标。kernel/ 与 collector/ 各被切成两个目标，app/ 里 AppContext 单独成层，
// 所以按文件名再分一次；这两处特例必须和 src/CMakeLists.txt 的挂载保持一致。
function targetOf(file) {
    const parts = file.split('/');
    const dir = parts[1] ?? '';
    const base = path.basename(file);
    if (dir === 'kernel' && /^(KernelManager|KernelMessages|KernelRelease|KernelSource)\./.test(base)) return 'vccKernelManager';
    if (dir === 'collector' && /^Telegram\./.test(base)) return 'vccTelegram';
    // AppContext 独立成 vccContext，就是为了断开 vccGui <-> vccApp 的环。
    if (dir === 'app' && /^AppContext\./.test(base)) return 'vccContext';
    if (dir === 'app' && base.startsWith('main.')) return 'vcc';
    const byDir = {
        core: 'vccCore', models: 'vccModels', parser: 'vccParser', db: 'vccDb',
        net: 'vccNet', sysproxy: 'vccSysProxy', geo: 'vccGeo', ip: 'vccGeo',
        kernel: 'vccKernel', export: 'vccExport', platform: 'vccPlatform',
        proxy: 'vccProxy', proxy_probe: 'vccProbe', speedtest: 'vccSpeedtest',
        scheduler: 'vccScheduler', router: 'vccRouter', tasks: 'vccTasks',
        collector: 'vccCollector', api: 'vccApi', updater: 'vccUpdater',
        gui: 'vccGui', app: 'vccApp',
    };
    return byDir[dir] ?? null;
}

const files = [];
(function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, entry.name).replaceAll('\\', '/');
        if (entry.isDirectory()) walk(p);
        else if (/\.(cpp|h)$/.test(entry.name)) files.push(p);
    }
})(SRC);

const includeRe = /^[ \t]*#include[ \t]+"([^"]+)"/gm;
const edges = new Map();       // "A->B" -> 次数
const sample = new Map();      // "A->B" -> 第一个触发它的文件
const headerEdges = new Map(); // 头 -> Set<头>
const unknownIncludes = [];
const upward = [];

function noteEdge(from, to, file) {
    if (to === null || from === null || from === to) return;
    const key = `${from}->${to}`;
    edges.set(key, (edges.get(key) ?? 0) + 1);
    if (!sample.has(key)) sample.set(key, file);
}

for (const file of files) {
    const from = targetOf(file);
    for (const [, spec] of fs.readFileSync(file, 'utf8').matchAll(includeRe)) {
        // 同目录兄弟头（gui 里大量 `#include "ProxyPage.h"`）先按当前目录解析。
        const target = (spec.includes('/') ? path.join(SRC, spec) : path.join(path.dirname(file), spec))
            .replaceAll('\\', '/');
        if (!fs.existsSync(target)) {
            unknownIncludes.push(`${file} -> "${spec}"（文件不存在）`);
            continue;
        }
        const to = targetOf(target);
        noteEdge(from, to, file);
        if (file.endsWith('.h')) {
            if (!headerEdges.has(file)) headerEdges.set(file, new Set());
            headerEdges.get(file).add(target);
        }
        if (LAYER[from] !== undefined && LAYER[to] !== undefined && LAYER[to] > LAYER[from]) {
            upward.push(`${file} (${from}) -> ${target} (${to})`);
        }
    }
}

// --- 声明事实 -------------------------------------------------------------
const cmake = fs.readFileSync(CMAKE, 'utf8');
const folded = cmake.replace(/\r?\n[ \t]*/g, ' ');
const declared = new Map();
const defined = new Set();
for (const m of folded.matchAll(/add_library\(\s*(vcc\w*)/g)) defined.add(m[1]);
for (const m of folded.matchAll(/vcc_port_layer\(\s*(vcc\w+)/g)) defined.add(m[1]);
defined.add('vcc');

function note(target, text) {
    const libs = declared.get(target) ?? new Set();
    for (const dep of text.matchAll(/vcc\w*/g)) {
        if (dep[0] !== target) libs.add(dep[0]);
    }
    declared.set(target, libs);
}
for (const m of folded.matchAll(/target_link_libraries\(\s*(vcc\w*)\s+(?:PUBLIC|PRIVATE|INTERFACE)?([^)]*)\)/g)) {
    note(m[1], m[2]);
}
for (const m of folded.matchAll(/vcc_port_layer\(\s*(vcc\w+)\s+\S+([^)]*)\)/g)) {
    note(m[1], m[2]);
}
// 别名：vccKernelConfig 就是 vccProxy（拆开会形成链接环，见 CMake 里的注释）。
const alias = new Map();
for (const m of folded.matchAll(/add_library\((vcc\w*) ALIAS (vcc\w*)\)/g)) alias.set(m[1], m[2]);
for (const [name, real] of alias) {
    defined.add(name);
    for (const [, libs] of declared) if (libs.has(name)) libs.add(real);
    declared.set(name, declared.get(real) ?? new Set());
}

const typos = [];
for (const [target, libs] of declared) {
    for (const lib of libs) {
        if (!defined.has(lib)) typos.push(`${target} 声明依赖 ${lib} —— 这个名字没有对应的目标`);
    }
}

const declaredFor = (target) => declared.get(target) ?? new Set();
const closure = new Map();
function reach(target) {
    if (closure.has(target)) return closure.get(target);
    const acc = new Set();
    closure.set(target, acc);          // 先占位，防递归
    for (const lib of declaredFor(target)) {
        if (acc.has(lib)) continue;
        acc.add(lib);
        for (const inner of reach(lib)) acc.add(inner);
    }
    return acc;
}

const undeclared = [];
const inherited = [];
for (const [key, count] of edges) {
    const [from, to] = key.split('->');
    if (!defined.has(from) || !defined.has(to)) continue;
    if (!reach(from).has(to)) {
        undeclared.push(`${key}  (${count} 处，例如 ${sample.get(key)})`);
    } else if (!declaredFor(from).has(to)) {
        inherited.push(`${key}  (${count} 处)`);
    }
}

// 目标级环（Tarjan SCC，规模很小，直接写）
const targetGraph = new Map();
for (const [key] of edges) {
    const [from, to] = key.split('->');
    if (!defined.has(from) || !defined.has(to)) continue;
    if (!targetGraph.has(from)) targetGraph.set(from, new Set());
    targetGraph.get(from).add(to);
}
const cycles = [];
{
    const index = new Map();
    const low = new Map();
    const onStack = new Set();
    const stack = [];
    let counter = 0;
    const strongConnect = (node) => {
        index.set(node, counter); low.set(node, counter); counter += 1;
        stack.push(node); onStack.add(node);
        for (const next of targetGraph.get(node) ?? []) {
            if (!index.has(next)) { strongConnect(next); low.set(node, Math.min(low.get(node), low.get(next))); }
            else if (onStack.has(next)) { low.set(node, Math.min(low.get(node), index.get(next))); }
        }
        if (low.get(node) === index.get(node)) {
            const component = [];
            let popped;
            do { popped = stack.pop(); onStack.delete(popped); component.push(popped); } while (popped !== node);
            if (component.length > 1) cycles.push(component.join(' <-> '));
        }
    };
    for (const node of targetGraph.keys()) if (!index.has(node)) strongConnect(node);
}

// 头文件 include 环
const headerCycles = [];
{
    const state = new Map();
    const stack = [];
    const dfs = (node) => {
        state.set(node, 1);
        stack.push(node);
        for (const next of headerEdges.get(node) ?? []) {
            if (state.get(next) === 1) headerCycles.push(stack.slice(stack.indexOf(next)).concat(next).join(' -> '));
            else if (!state.has(next)) dfs(next);
        }
        stack.pop();
        state.set(node, 2);
    };
    for (const node of headerEdges.keys()) if (!state.has(node)) dfs(node);
}

function report(title, items, { info = false } = {}) {
    console.log(`\n${title}`);
    if (!items.length) { console.log('  无'); return false; }
    console.log([...new Set(items)].map(s => `  ${s}`).join('\n'));
    return !info;
}

let failed = false;
console.log(`扫描 ${files.length} 个文件、${edges.size} 条目标级依赖边、${defined.size} 个目标`);
failed |= report('1) 反向依赖（低层 include 高层）', upward);
failed |= report('2) 未声明耦合（include 边不在传染闭包里）', undeclared);
report('3) 只靠传染链上（合法，但建议写到自己身上）', inherited, { info: true });
failed |= report('4a) 目标级依赖环', cycles);
failed |= report('4b) 头文件 include 环', headerCycles);
failed |= report('5) 依赖名打错（vcc_port_layer 的 if(TARGET) 会静默丢掉）', typos);
failed |= report('6) include 了不存在的文件', unknownIncludes);

const unused = [];
for (const [from, libs] of declared) {
    for (const to of libs) if (!edges.has(`${from}->${to}`)) unused.push(`${from} -> ${to}`);
}
report('附：声明了但代码里没有 include 边（静态库链接顺序可能需要，仅提示）', unused, { info: true });

console.log(failed ? '\nDECOUPLING GUARD FAILED' : '\ndecoupling ok');
process.exit(failed ? 1 : 0);
