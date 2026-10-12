@echo off
rem ASCII-only on purpose: a .cmd with CJK in it breaks under some codepages.
rem Ninja dev loop for vcc. Same vcpkg Qt as build/, single-config Debug, ~1s no-op.
rem
rem   tools\ninja-build.cmd configure   first time only (needs build-ninja/)
rem   tools\ninja-build.cmd             build everything (default: --parallel 8)
rem   tools\ninja-build.cmd vccGui      build one target
rem   tools\ninja-build.cmd test        run ctest -j8 (offscreen Qt, no PATH tweaks needed:
rem                                     DeployRuntime already dropped the debug Qt DLLs next to the exes)
rem
rem Nothing below hardcodes a machine layout: the project root comes from this script's
rem own folder, the MSVC env comes from vswhere, and the toolchain/plugin paths come from
rem VCPKG_ROOT (set it once, or pass it inline).
rem
rem Packaging still goes through the VS-generator tree: node tools/package.mjs build-rel
setlocal
cd /d "%~dp0.."

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=D:\vcpkg"

rem Locate vcvars64.bat instead of assuming where Visual Studio is installed.
set "VCVARS="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do set "VCVARS=%%i"
if not defined VCVARS (
  echo tools\ninja-build.cmd: vswhere found no VC vcvars64.bat - install the C++ toolset or set VCVARS manually.
  exit /b 1
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 exit /b 1

if "%1"=="configure" (
  cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCC_BUILD_TESTS=ON
  exit /b %errorlevel%
)
if "%1"=="clean" (
  cmake --build build-ninja --clean-first --parallel 8
  exit /b %errorlevel%
)
if "%1"=="test" (
  set QT_QPA_PLATFORM=offscreen
  set QT_PLUGIN_PATH=%VCPKG_ROOT%/installed/x64-windows/debug/Qt6/plugins
  ctest --test-dir build-ninja -j8
  exit /b %errorlevel%
)
if "%1"=="" (
  cmake --build build-ninja --parallel 8
  exit /b %errorlevel%
)
cmake --build build-ninja --target %1 --parallel 8
