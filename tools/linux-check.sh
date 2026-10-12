#!/usr/bin/env bash
# Linux 可行性验证的一键入口。目的只有一个：把"我们盲写的移植改动"变成真报错。
#
#   sudo bash tools/linux-check.sh              # 装依赖 → 编可移植层 → 编整个程序
#   bash tools/linux-check.sh --no-install      # 已经装过依赖，跳过 apt
#   bash tools/linux-check.sh --portable        # 只编那 9 个可移植层，最快拿第一个信号
#   bash tools/linux-check.sh --with-tests      # 连 gtest 套件一起编（要多装 libgtest-dev）
#
# 只在仓库里开一个 build-linux/，绝不碰 Windows 那边的 build/ 和 build-rel/。
# 全程把输出留在 build-linux/check.log，失败时最后会打印错误条数和前若干条原文。

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-linux"
LOG="$BUILD_DIR/check.log"
INSTALL=1
PORTABLE_ONLY=0
WITH_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --no-install)   INSTALL=0 ;;
        --portable)     PORTABLE_ONLY=1 ;;
        --with-tests)   WITH_TESTS=1 ;;
        *) echo "未知参数: $arg"; exit 2 ;;
    esac
done

# 可移植层：审计确认这 9 个目标里零 Win32 调用（src/platform 与 src/sysproxy 才是主战场）。
PORTABLE_TARGETS=(vccCore vccModels vccParser vccDb vccExport vccGeo vccSpeedtest vccCollector vccApi)

mkdir -p "$BUILD_DIR"
: > "$LOG"

step() { echo; echo "=== $*"; }

step "环境"
{
    uname -a
    cat /etc/os-release 2>/dev/null | head -2
    cmake --version 2>/dev/null | head -1
    c++ --version 2>/dev/null | head -1
} | tee -a "$LOG"

if [ "$INSTALL" = "1" ]; then
    step "装依赖（apt）"
    # Qt6 的包名在 Ubuntu/Debian 各版本间不一致，所以逐个试、缺哪个报哪个，
    # 而不是整串安装失败就停 —— 那会让我们分不清是缺包还是代码问题。
    apt-get update >>"$LOG" 2>&1
    for pkg in cmake g++ ninja-build pkg-config qt6-base-dev libqt6svg6 \
               libyaml-cpp-dev libgl1-mesa-dev ; do
        apt-get install -y "$pkg" >>"$LOG" 2>&1 || echo "MISSING-PKG: $pkg" | tee -a "$LOG"
    done
    if [ "$WITH_TESTS" = "1" ]; then
        apt-get install -y libgtest-dev googletest >>"$LOG" 2>&1 \
            || echo "MISSING-PKG: libgtest-dev" | tee -a "$LOG"
    fi
fi

step "configure（不挂 vcpkg 工具链：Linux 上用发行版自己的 Qt6）"
CONFIGURE_ARGS=(-S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug
                -DVCC_BUILD_TESTS=$([ "$WITH_TESTS" = "1" ] && echo ON || echo OFF))
if ! cmake "${CONFIGURE_ARGS[@]}" 2>&1 | tee -a "$LOG"; then
    echo "CONFIGURE-FAILED —— 这是最有价值的一条信息：CMake 层就过不去"
    grep -nE "CMake Error|Could NOT find" "$LOG" | head -20
    exit 1
fi
echo "configure 通过；Qt 版本：$(grep -m1 -oE 'Found Qt: [0-9.]+' "$LOG" || echo 未打印)"

step "编可移植层（$([ "$PORTABLE_ONLY" = "1" ] && echo '只这些' || echo '先这些，再全量')）"
FAILED=()
for target in "${PORTABLE_TARGETS[@]}"; do
    if ! cmake --build "$BUILD_DIR" --target "$target" >>"$LOG" 2>&1; then
        FAILED+=("$target")
        echo "FAIL  $target"
    else
        echo "ok    $target"
    fi
done

if [ "$PORTABLE_ONLY" = "1" ]; then
    step "结果（可移植层）"
    echo "失败目标: ${FAILED[*]:-无}"
    grep -E "error:" "$LOG" | head -40
    exit 0
fi

step "编平台层与主程序"
for target in vccNet vccSysProxy vccPlatform vccKernel vccKernelManager vccProxy vccProbe \
              vccUpdater vccTelegram vccTasks vccRouter vccScheduler \
              vccGui vccApp vcc; do
    if cmake --build "$BUILD_DIR" --target "$target" >>"$LOG" 2>&1; then
        echo "ok    $target"
    else
        echo "FAIL  $target"
        FAILED+=("$target")
    fi
done

step "汇总"
echo "失败目标: ${FAILED[*]:-无}"
echo "error: 条数 = $(grep -cE 'error:' "$LOG")"
echo "--- 前 60 条错误原文（去重后）---"
grep -E "error:" "$LOG" | sort -u | head -60
echo
echo "完整日志: $LOG"
echo "把这份输出贴回来，我按真实报错逐轮修。"
