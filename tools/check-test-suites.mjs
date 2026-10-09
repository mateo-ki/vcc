import { execFile } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

// 每个测试 exe 都必须真的跑到 >0 条用例。
//   node tools/check-test-suites.mjs [binaryDir]
//
// 为什么需要它：gtest 的空二进制（源文件没进构建、或新建 tests.cmake 后没重新
// configure）会打印 "Running 0 tests" 然后 [ PASSED ]，ctest 也跟着报 Passed。
// 今天真就这么绿了两次。这里把"0 用例"当成失败，而不是当成通过。
const binaryDir = process.argv[2] || 'build-gui';
const debugDir = path.join(binaryDir, 'Debug');
const env = {
  ...process.env,
  QT_QPA_PLATFORM: 'offscreen',
  QT_PLUGIN_PATH: 'D:/vcpkg/installed/x64-windows/debug/Qt6/plugins',
};

if (!fs.existsSync(debugDir)) {
  console.error(`no such dir: ${debugDir}`);
  process.exit(2);
}

const exes = fs
  .readdirSync(debugDir)
  .filter((name) => /^vcc.*Tests\.exe$/.test(name))
  .sort();

if (exes.length === 0) {
  console.error('没有找到任何 vcc*Tests.exe —— 这本身就该算失败');
  process.exit(2);
}

function runOne(file) {
  return new Promise((resolve) => {
    execFile(path.join(debugDir, file), [], { env, windowsNonConsoleGuiWorkaround: true, maxBuffer: 32 * 1024 * 1024 },
      (error, stdout) => {
        const text = String(stdout || '');
        const ran = /(\d+) tests? from (\d+) test suites? ran/.exec(text);
        const passed = /(\d+) tests?\.?\s*$/.exec(text.split('\n').find((l) => l.includes('[  PASSED  ]')) || '');
        const failed = [...text.matchAll(/^\[  FAILED  \] (\S+)/gm)].map((m) => m[1]);
        resolve({
          file: file.replace('.exe', ''),
          ran: ran ? Number(ran[1]) : 0,
          passed: passed ? Number(passed[1]) : 0,
          failed,
          crashed: Boolean(error) && !text.includes('[  PASSED  ]') && failed.length === 0,
        });
      });
  });
}

const results = [];
for (const exe of exes) {
  results.push(await runOne(exe));
}

let bad = 0;
for (const r of results) {
  const flag = r.ran === 0 ? 'ZERO-TESTS' : r.failed.length ? `FAIL(${r.failed.length})` : r.crashed ? 'CRASH' : 'ok';
  if (flag !== 'ok') bad += 1;
  console.log(`${flag.padEnd(11)} ${r.file.padEnd(24)} ran=${String(r.ran).padStart(3)} passed=${String(r.passed).padStart(3)} ${r.failed.slice(0, 3).join(' ')}`);
}
const total = results.reduce((sum, r) => sum + r.ran, 0);
console.log(`\n合计 ${results.length} 套件 / ${total} 用例，问题套件 ${bad} 个`);
process.exit(bad === 0 ? 0 : 1);
