; VCC 安装包 —— 编译（命令行，可复现）：
;   node tools/make-dist.mjs build-rel
;   ISCC.exe cmake\vcc.iss        (Inno Setup 6 的命令行编译器，装在哪就给哪)
; Compil32.exe 是 ISCC 的 GUI 壳（打开后 File→Open 选这个 .iss，或直接
;   Compil32.exe /cc cmake\vcc.iss），但脚本化只用 ISCC。
;
; 三条来自实测的 Inno 坑（别改回去）：
;   1) ISCC 是 32 位进程：[Files] 的 Source 里 {win} 不展开（报"文件不存在"），
;      {sys} 被重定向到 SysWOW64（那里没有 vcruntime140_1.dll）→ 取 64 位系统文件
;      必须写 {#GetEnv('WINDIR')}\Sysnative\...。
;   2) Inno 升级不会删掉"这次不在包里"的旧文件 → 目录体积只涨不跌，
;      必须靠 [InstallDelete] 点名删。
;   3) 程序把 data/ 建在 exe 同级（启动日志里 set_current_dir 就是它），
;      装到 Program Files 又没提权时，写库会静默失败 → 默认走 per-user 目录。

#define MyAppName "VCC"
#define MyAppVersion "1.1.1"
#define MyAppExeName "vcc.exe"
#define MyDist "..\build-rel\Release\dist"

[Setup]
AppId={{7C3D61D4-2A5B-4E97-9C53-5B4E0D9A1F66}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppName}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\build-rel\installer
OutputBaseFilename=VCC-{#MyAppVersion}-win64
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
SetupLogging=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Default.isl,ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#MyDist}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
; MSVC 运行库：release 版 Qt/vcpkg 依赖它，干净系统上没有。走 System32 绝对路径（见上面坑 1）。
Source: "{#GetEnv('WINDIR')}\Sysnative\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GetEnv('WINDIR')}\Sysnative\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GetEnv('WINDIR')}\Sysnative\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; 旧包结构留下的：exe 同级那份 platforms/（现在只有 plugins/platforms 一份）、
; 以及白名单里已经不带imageformats/iconengines/Qt6Svg（它们需要包里没带的 Qt6Svg/jasper/libwebp/tiff）。
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\plugins\imageformats"
Type: filesandordirs; Name: "{app}\plugins\iconengines"
Type: filesandordirs; Name: "{app}\plugins\styles"
Type: files; Name: "{app}\Qt6Svg.dll"
Type: files; Name: "{app}\opengl32sw.dll"
Type: files; Name: "{app}\d3dcompiler_47.dll"
Type: files; Name: "{app}\dxcompiler.dll"
Type: files; Name: "{app}\dxil.dll"
; 调试符号与测试可执行文件曾经被一起打进去过
Type: files; Name: "{app}\*.pdb"
Type: files; Name: "{app}\vcc*Tests.exe"
Type: files; Name: "{app}\gtest*.dll"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 程序自己在工作目录里建的东西（数据库、订阅分库、下载的内核与 geo 库）
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\logs"
