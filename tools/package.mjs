// 出安装包的一键入口：只编 vcc 目标（不是整个 Release 目录！），再 dist + Inno。
//   node tools/package.mjs [binaryDir]     默认 build-rel
//
// 为什么要单独有个入口：`cmake --build build-rel --config Release` 会把 40 个测试
// exe（每个约 11MB）一起重链，而打包根本用不到它们 —— make-dist 反手就把
// ^vcc.*Tests\.exe$ 全剪掉（实测剪掉 41.9MB）。只编 vcc 会连带编它依赖的那些 lib，
// 一个测试目标都不碰。
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const dir = process.argv[2] ?? 'build-rel';
const ISCC = 'D:/software/InnoSetup6/ISCC.exe';
const started = Date.now();

function run(label, cmd, args) {
    const t0 = Date.now();
    process.stdout.write(`\n=== ${label}\n`);
    execFileSync(cmd, args, { stdio: 'inherit' });
    console.log(`--- ${label}: ${((Date.now() - t0) / 1000).toFixed(1)}s`);
}

if (!fs.existsSync(path.join(dir, 'CMakeCache.txt'))) {
    console.error(`没有 ${dir}/CMakeCache.txt —— 先 configure 这个 binaryDir。`);
    console.error(`（新目录要带 vcpkg 工具链：cmake -S . -B ${dir} ` +
        `-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCC_BUILD_TESTS=ON）`);
    process.exit(1);
}
if (!fs.existsSync(ISCC)) {
    console.error(`找不到 Inno Setup 编译器：${ISCC}`);
    process.exit(1);
}

run('编译 vcc（只它 + 依赖的 lib，不碰测试目标）', 'cmake',
    ['--build', dir, '--config', 'Release', '--target', 'vcc', '--parallel', '8']);
run('裁剪 dist', process.execPath, ['tools/make-dist.mjs', dir]);
run('编译安装包', ISCC, ['cmake/vcc.iss']);

const pkg = path.join(dir, 'installer', 'VCC-1.0.0-win64.exe');
const info = fs.statSync(pkg);
console.log(`\n总计 ${((Date.now() - started) / 1000).toFixed(1)}s`);
console.log(`安装包: ${pkg}  ${(info.size / 1048576).toFixed(1)}MB  ${info.mtime.toLocaleString()}`);
