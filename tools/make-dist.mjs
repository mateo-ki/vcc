// 把 Release 输出裁成一个干净的可分发目录 dist/，并打印每一类剪掉了多少。
//   node tools/make-dist.mjs [binaryDir]
// 默认 binaryDir = build-rel。剪枝依据是本机实测（见 cmake/vcc.iss 顶部注释）：
// platforms 被 DeployRuntime.cmake 拷了两份、插件目录整目录复制会把 debug 版一起带上。
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const src = path.resolve(process.argv[2] ?? 'build-rel', 'Release');
const out = path.join(src, 'dist');
if (!fs.existsSync(path.join(src, 'vcc.exe'))) {
    console.error(`没找到 ${src}/vcc.exe —— 先 cmake --build build-rel --config Release`);
    process.exit(1);
}

// 这些是"系统里有、或别的 DLL 硬导入"的白名单外排除项；按扩展名/角色剪。
const DROP_TOP = [/\.pdb$/i, /\.exp$/i, /\.lib$/i, /^Qt6Svg\.dll$/i, /^vcc.*Tests\.exe$/i, /^gtest/i,
                  /^gmock/i, /^icudtd/i, /^Qt6.*d\.dll$/i, /^d3dcompiler_47$/i,
                  /^dxcompiler/i, /^dxil/i, /^opengl32sw/i];
// 死插件/多余插件一律不带走 .pdb：vcpkg 的插件目录里每个 dll 旁边都躺着同名的
// qdirect2d.pdb 有 15.9 MB，platforms 那 37 MB 里 34 MB 是 pdb。
const DROP_ANYWHERE = [/\.pdb$/i, /\.exp$/i, /\.lib$/i];
// 只带真用得到的插件：qwindows 是唯一的平台插件（offscreen 只有测试要）；
// PNG/ICO 在 Qt6 里是 Qt6Gui 内置，imageformats 整目录可删；
// TLS 只留系统后端 qschannel（qopensslbackend 需要 libssl-3-x64.dll，包里根本没带 → 加载不了）；
// iconengines 的 qsvgicon 需要 Qt6Svg.dll（同样没带）→ 删。
const KEEP = {
    platforms: [/^qwindows\.dll$/i],
    styles: [/^qmodernwindowsstyle\.dll$/i],
    tls: [/^qschannelbackend\.dll$/i],
    networkinformation: [/^qnetworklistmanager\.dll$/i],
};
const KEEP_PLUGIN_DIRS = Object.keys(KEEP);

const dirSize = (p) => {
    if (!fs.existsSync(p)) return 0;
    let total = 0;
    for (const entry of fs.readdirSync(p, { withFileTypes: true })) {
        const full = path.join(p, entry.name);
        total += entry.isDirectory() ? dirSize(full) : fs.statSync(full).size;
    }
    return total;
};
const mb = (n) => (n / 1024 / 1024).toFixed(1) + ' MB';

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out, { recursive: true });

const dropped = new Map();
const note = (label, size) => dropped.set(label, (dropped.get(label) ?? 0) + size);

// 1) 顶层：可执行、qt.conf、必需 DLL
for (const entry of fs.readdirSync(src, { withFileTypes: true })) {
    if (!entry.isFile()) continue;
    const size = fs.statSync(path.join(src, entry.name)).size;
    const hit = DROP_TOP.find((re) => re.test(entry.name));
    if (hit) { note('顶层: ' + hit.source, size); continue; }
    if (!/^vcc\.exe$|^qt\.conf$/i.test(entry.name) && !/\.dll$/i.test(entry.name)) {
        note('顶层: 其他', size);
        continue;
    }
    fs.copyFileSync(path.join(src, entry.name), path.join(out, entry.name));
}

// 2) plugins：只留需要的类别，且丢掉 debug 版与加载不了的图像插件
for (const category of KEEP_PLUGIN_DIRS) {
    const from = path.join(src, 'plugins', category);
    if (!fs.existsSync(from)) { note('缺插件目录 ' + category, 0); continue; }
    const target = path.join(out, 'plugins', category);
    fs.mkdirSync(target, { recursive: true });
    for (const file of fs.readdirSync(from)) {
        const size = fs.statSync(path.join(from, file)).size;
        if (DROP_ANYWHERE.find((re) => re.test(file))) { note('插件: pdb/中间文件', size); continue; }
        if (!KEEP[category].find((re) => re.test(file))) { note('插件: 不在白名单 ' + category, size); continue; }
        fs.copyFileSync(path.join(from, file), path.join(target, file));
    }
}
// exe 同级那份 platforms/ 是 DeployRuntime 的"二次保底"，qt.conf 已经把 Plugins 指向
// plugins/，所以只带一份。
note('platforms/ 重复那份', dirSize(path.join(src, 'platforms')));

// 3) 依赖闭包自查：包里有没有引用了却没带的 DLL
const OBJDUMP = 'D:/software/msys64/mingw64/bin/objdump.exe';
const systemDlls = /^(api-ms-win-|ext-ms-win-|xmllite|d3d9|d3d12|dwrite|wininet|uiautomationcore|authz|kernel32|user32|gdi32|ole32|oleaut32|shell32|advapi32|ws2_32|winmm|comdlg32|setupapi|crypt32|bcrypt|version|imm32|dwmapi|uxtheme|secur32|ncrypt|wtsapi32|iphlpapi|userenv|pdh|psapi|rpcrt4|shlwapi|wintrust|winhttp|msvcrt|vcruntime140|vcruntime140_1|msvcp140|msvcp140_1|msvcp140_2|ucrtbase|nlaapi|wlanapi|dbghelp|qdpi|d3d11|dxgi|ddraw|dsound|hid|ksuser|avifil32|avicap32|msvfw32|glu32|opengl32|winmmbase|mpr|netapi32|samcli|sehdinst|suhd|devobj|cfgmgr32|wldclient|fwpuclnt|rasapi32|dnsapi|wevtapi|winspool|msimg32|comctl32|gdiplus|windowscodecs|propsys|objects|shcore|bcp47|phpivot|kernel.appcore|apppolicylicense|rt_workqueue|ntdll|sechost|profapi|powrprof|msacm32|resutils|synchronization|resourcepolicy|fibonacci)/i;
const missing = new Set();
if (fs.existsSync(OBJDUMP)) {
    const binaries = [];
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) walk(full);
            else if (/\.(exe|dll)$/i.test(entry.name)) binaries.push(full);
        }
    };
    walk(out);
    const present = new Set(binaries.map((f) => path.basename(f).toLowerCase()));
    for (const binary of binaries) {
        let dump = '';
        try {
            dump = execFileSync(OBJDUMP, ['-p', binary], { encoding: 'utf8', maxBuffer: 1 << 26 });
        } catch { continue; }
        for (const match of dump.matchAll(/DLL Name:\s*([^\s\0()]+)/gi)) {
            const name = match[1].toLowerCase();
            if (!present.has(name) && !systemDlls.test(name) && !name.endsWith('.dll.a')) {
                missing.add(`${name}  ← ${path.basename(binary)}`);
            }
        }
    }
} else {
    console.log('（没有 objdump，跳过依赖闭包自查）');
}

const total = dirSize(out);
console.log(`dist 目录: ${out}`);
console.log(`保留体积: ${mb(total)}   （源 Release 目录 ${mb(dirSize(src))}）`);
for (const [label, size] of [...dropped].sort((a, b) => b[1] - a[1])) {
    if (size > 0) console.log(`  剪掉 ${mb(size).padStart(9)}  ${label}`);
}
if (missing.size) {
    console.log('缺依赖（包里没带、也不是系统 DLL）:');
    for (const line of missing) console.log('  ' + line);
} else if (fs.existsSync(OBJDUMP)) {
    console.log('依赖闭包: 通过（每个导入都能在包或系统里找到）');
}
