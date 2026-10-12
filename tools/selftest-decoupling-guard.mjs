// 反向验：把守卫逐条"弄坏"，确认它真的会红 —— 而不是永远绿的空断言。
// 全部在临时副本里做，**不碰工作树**。跑法：node tools/selftest-decoupling-guard.mjs
import { cpSync, mkdirSync, rmSync, writeFileSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const tmp = path.resolve(process.env.TEMP || '/tmp', 'vcc-decoupling-selftest');
rmSync(tmp, { recursive: true, force: true });
mkdirSync(tmp, { recursive: true });
cpSync('src', path.join(tmp, 'src'), { recursive: true });
mkdirSync(path.join(tmp, 'tools'), { recursive: true });
cpSync('tools/check-decoupling.mjs', path.join(tmp, 'tools', 'check-decoupling.mjs'));

const at = (rel) => path.join(tmp, 'src', rel);
const appendTo = (rel, text) => writeFileSync(at(rel), readFileSync(at(rel), 'utf8') + text);

const cases = [
    {
        name: '反向依赖：vccCore include 了 vccApp 的头',
        break: () => appendTo('core/Log.cpp', '\n#include "app/MainWindow.h"\n'),
        expect: '1) 反向依赖',
    },
    {
        name: '未声明耦合：vccKernel 用了 vccSysProxy 却没声明',
        break: () => appendTo('kernel/KernelTypes.cpp', '\n#include "sysproxy/RegRunner.h"\n'),
        expect: '2) 未声明耦合',
    },
    {
        name: '目标级环：core -> app 而 app -> core 本来就有',
        break: () => appendTo('core/Encoding.cpp', '\n#include "app/AppContext.h"\n'),
        expect: '4a) 目标级依赖环',
    },
    {
        name: '头文件环：两个临时头互相 include',
        break: () => {
            writeFileSync(at('core/SelftestA.h'), '#pragma once\n#include "core/SelftestB.h"\n');
            writeFileSync(at('core/SelftestB.h'), '#pragma once\n#include "core/SelftestA.h"\n');
            appendTo('core/Log.cpp', '\n#include "core/SelftestA.h"\n');
        },
        expect: '4b) 头文件 include 环',
    },
    {
        name: '依赖名打错：vcc_port_layer 里写 vccSpeedtset（if(TARGET) 会静默丢掉）',
        break: () => {
            const p = path.join(tmp, 'src', 'CMakeLists.txt');
            writeFileSync(p, readFileSync(p, 'utf8').replace(
                'vcc_port_layer(vccScheduler scheduler', 'vcc_port_layer(vccScheduler scheduler vccSpeedtset'));
        },
        expect: '5) 依赖名打错',
    },
    {
        name: 'include 了不存在的文件',
        break: () => appendTo('core/Ids.cpp', '\n#include "core/NoSuchHeader.h"\n'),
        expect: '6) include 了不存在的文件',
    },
];

let failures = 0;
for (const testCase of cases) {
    // 每个用例都从干净的 src 副本重新开始。
    rmSync(path.join(tmp, 'src'), { recursive: true, force: true });
    cpSync('src', path.join(tmp, 'src'), { recursive: true });
    testCase.break();
    let out = '';
    let code = 0;
    try {
        out = execFileSync(process.execPath, ['tools/check-decoupling.mjs'],
            { cwd: tmp, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
    } catch (error) {
        out = `${error.stdout ?? ''}${error.stderr ?? ''}`;
        code = error.status ?? -1;
    }
    const caught = code !== 0 && out.includes(testCase.expect);
    console.log(`${caught ? 'OK  ' : 'FAIL'}  ${testCase.name}  (exit=${code})`);
    if (!caught) {
        failures += 1;
        console.log(out.split('\n').filter(l => l.trim().startsWith('1)') || l.trim().startsWith('2)')
            || l.trim().startsWith('4') || l.trim().startsWith('5)') || l.trim().startsWith('6)')
            || l.trim().startsWith('core/') || l.trim().startsWith('kernel/')).join('\n'));
    }
}

// 最后确认：没被弄坏的副本必须是绿的（否则上面任何一条红都不说明问题）。
rmSync(path.join(tmp, 'src'), { recursive: true, force: true });
cpSync('src', path.join(tmp, 'src'), { recursive: true });
let cleanOk = true;
try {
    execFileSync(process.execPath, ['tools/check-decoupling.mjs'], { cwd: tmp, stdio: 'ignore' });
} catch { cleanOk = false; }
console.log(`${cleanOk ? 'OK  ' : 'FAIL'}  未破坏的副本保持绿`);
if (!cleanOk) failures += 1;

rmSync(tmp, { recursive: true, force: true });
console.log(failures ? `\n${failures} 条没被抓住` : '\n六条规则都抓得住');
process.exit(failures ? 1 : 0);
