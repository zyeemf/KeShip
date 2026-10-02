#!/bin/bash
# ============================================================
#  KeShip 一键启动脚本
#  顺序: 互斥 → 激活环境 → 编译 → 文档检查 → GPU预热 → 启动(自动重试)
#
#  用法:
#    ~/keship_ws/start_keship.sh              # 编译 + 启动
#    ~/keship_ws/start_keship.sh --no-build   # 跳过编译
#    ~/keship_ws/start_keship.sh --headless   # 无头模式
#    ~/keship_ws/start_keship.sh --no-gpu-warm# 跳过 GPU 预热
#    ~/keship_ws/start_keship.sh --help       # 帮助
#    其余参数 → 转发为 launch 的 extra_gz_args（最终传给 Gazebo）
#
#  退出码（start_keshipv0010.bat 依赖此约定）:
#    0  正常退出
#    1  已有实例在运行 / 启动失败
#    2  编译失败（未启动仿真）
#    3  环境缺失（未找到 ROS 2）
#
#  ★ 顺序约束（2026-09-20 重排，改动前请先读）:
#    1) 编译**必须**晚于激活 ROS 环境 —— CMAKE_PREFIX_PATH / AMENT_PREFIX_PATH
#       由 `source /opt/ros/jazzy/setup.bash` 注入，CMakeLists.txt 的
#       find_package(ros_gz_sim REQUIRED) 依赖它们。把它们调换会导致编译必失败。
#    2) GPU 预热**必须**紧邻启动 —— 预热是为 gz 启动做准备，放在编译前
#       会在编译失败时白白改动驱动状态却不起仿真。
#    3) install/setup.bash 是**编译产物**，首次构建前不存在，
#       故编译前只能"有则加载"，编译成功后还要补加载一次（否则 launch 找不到包）。
#
#  单例互斥：同一时刻只允许一个 KeShip 实例运行，
#  重复启动会被拒绝（避免多实例 gz-transport 服务/话题互相干扰）。
# ============================================================
set -e

# ── 参数解析 ──
NO_BUILD=false
HEADLESS=false
NO_GPU_WARM=false
NO_D3D12=false
EXTRA_GZ_ARGS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build)     NO_BUILD=true; shift ;;
        --headless)     HEADLESS=true; shift ;;
        --no-gpu-warm)  NO_GPU_WARM=true; shift ;;
        --no-d3d12)     NO_D3D12=true; shift ;;
        --help|-h)
            echo "KeShip 启动脚本"
            echo "  --no-build     跳过编译，直接启动（运行时可能是旧版本）"
            echo "  --headless     无头模式（gz sim -s，无 GUI）"
            echo "  --no-gpu-warm  跳过 GPU 预热"
            echo "  --no-d3d12     退回 Mesa 默认渲染（软件渲染），用于对比/回滚"
            echo "  其余参数       转发为 Gazebo 的 extra_gz_args"
            echo "退出码: 0 正常 / 1 已有实例或启动失败 / 2 编译失败 / 3 环境缺失"
            exit 0
            ;;
        *)  EXTRA_GZ_ARGS="${EXTRA_GZ_ARGS:+$EXTRA_GZ_ARGS }$1"; shift ;;
    esac
done

KSHIP_WS="$HOME/keship_ws"
cd "$KSHIP_WS"

# ═══════════════════════════════════════════════════════════
# 检测：源码是否比上次构建更新（仅在 --no-build 时提示）
#   目的：防止"改了代码却用 [1] 快速启动跑旧二进制"的静默降级
# ═══════════════════════════════════════════════════════════
check_stale_build() {
    local ref="$KSHIP_WS/install/lib/libKSPlugin_HydroBuoyancy.so"
    if [ ! -f "$ref" ]; then
        return 0
    fi
    local n
    n=$(find "$KSHIP_WS/src/keship/src_cpp" "$KSHIP_WS/src/keship/src_py" \
             "$KSHIP_WS/src/keship/include" \
        -type f \( -name '*.cc' -o -name '*.hh' -o -name '*.py' -o -name '*.qml' \) \
        -newer "$ref" 2>/dev/null | wc -l)
    if [ "$n" -gt 0 ]; then
        echo ""
        echo "  ┌──────────────────────────────────────┐"
        echo "  │ 提示: 检测到 $n 个源文件比上次构建更新       │"
        echo "  │ 当前启动的是旧版本，若需生效请改用 [2]     │"
        echo "  └──────────────────────────────────────┘"
    fi
}

# ═══════════════════════════════════════════════════════════
# 单例互斥：已有 KeShip 实例在运行则拒绝启动
# （flock 原子锁，进程退出自动释放；pgrep 辅助排查异常残留）
# ═══════════════════════════════════════════════════════════
mkdir -p "$KSHIP_WS/data"
LOCK_FILE="$KSHIP_WS/data/keship.lock"
exec 9>"$LOCK_FILE"
if ! flock -n 9; then
    echo ""
    echo "  [错误] 已有 KeShip 实例正在运行！"
    echo "         同时运行多个实例会互相干扰导致仿真错乱。"
    echo "         请先关闭现有实例，再重新启动。"
    echo ""
    exit 1
fi

if pgrep -f "[g]z sim -v 4" > /dev/null 2>&1; then
    echo ""
    echo "  [错误] 检测到 gz sim 进程已在运行，拒绝启动。"
    echo "         请先关闭现有实例，再重新启动。"
    echo ""
    exit 1
fi

echo ""
echo "╔══════════════════════════════════════╗"
echo "║     KeShip 船舶仿真平台启动          ║"
echo "╚══════════════════════════════════════╝"
echo ""

# ═══════════════════════════════════════════════════════════
# Step 1: 激活 ROS 2 环境（必须先于编译 —— 见顶部顺序约束 1）
# ═══════════════════════════════════════════════════════════
echo "[环境] 激活 ROS 2 + Gazebo 环境..."
if [ ! -f /opt/ros/jazzy/setup.bash ]; then
    echo ""
    echo "  [错误] 未找到 /opt/ros/jazzy/setup.bash"
    echo "         请先按 doc/BUILD.md 安装 ROS 2 Jazzy 与依赖。"
    echo ""
    exit 3
fi
. /opt/ros/jazzy/setup.bash

# install/setup.bash 是编译产物：有则加载，无则等编译后补加载
INSTALL_SETUP="$KSHIP_WS/install/setup.bash"
INSTALL_SOURCED=false
if [ -f "$INSTALL_SETUP" ]; then
    . "$INSTALL_SETUP"
    INSTALL_SOURCED=true
else
    echo "[环境] install/setup.bash 尚不存在（首次构建），编译成功后将补加载"
fi
echo "[环境] 完成"

echo ""

# ═══════════════════════════════════════════════════════════
# Step 2: 编译（闸门：失败即中止，不做后续任何准备）
# ═══════════════════════════════════════════════════════════
if [ "$NO_BUILD" = true ]; then
    echo "[编译] 已跳过 (--no-build)"
    check_stale_build
else
    echo "[编译] 正在编译 keship 包（通常 10~30 秒）..."
    # 注意：--log-base 是 colcon 全局参数，必须放在子命令 build 之前
    # （放在 build 之后会报 "unrecognized arguments: --log-base ..."，2026-08-14 修复）
    if ! colcon --log-base "$KSHIP_WS/data/log" build --packages-select keship --merge-install; then
        echo ""
        echo "╔══════════════════════════════════════╗"
        echo "║   编译失败 —— 未启动仿真              ║"
        echo "╚══════════════════════════════════════╝"
        echo ""
        echo "  请修正上面的编译错误后重新运行本脚本。"
        echo ""
        echo "  ⚠ 注意: install/ 中仍是上一次成功构建的版本。"
        echo "    若改用 [1] 快速启动，运行的将是旧二进制，"
        echo "    与当前源码不一致。"
        echo ""
        exit 2
    fi
    echo "[编译] 完成"
fi

# 首次构建后 install/setup.bash 才出现，此时补加载（否则 launch 找不到 keship 包）
if [ "$INSTALL_SOURCED" != true ] && [ -f "$INSTALL_SETUP" ]; then
    . "$INSTALL_SETUP"
    INSTALL_SOURCED=true
    echo "[环境] 已补加载 install/setup.bash"
fi

echo ""

# ═══════════════════════════════════════════════════════════
# Step 3: 代码变更自动检查（非阻塞）
#   仅在编译成功之后执行 —— 编译失败时跑它没有意义
# ═══════════════════════════════════════════════════════════
if [ -f "$KSHIP_WS/tools/auto_check.sh" ]; then
    echo "[文档] 代码变更自动检查..."
    bash "$KSHIP_WS/tools/auto_check.sh" --quick || true
    echo ""
fi

# ═══════════════════════════════════════════════════════════
# Step 3.5: 渲染后端 —— 显式启用 Mesa 的 D3D12 后端（走 NVIDIA 独显）
#   背景（2026-10-01 实测，详见 doc/plans/performance_optimization_plan.md §18）：
#     WSL2 上 Mesa 默认走 WSLg 的 X11/DRI3 路径 ⇒ 依赖 /dev/dri（本机不存在）
#     ⇒ 静默回落 CPU 软件渲染 llvmpipe。
#     显式 GALLIUM_DRIVER=d3d12 即可走 D3D12 → NVIDIA RTX 5060，实测
#     4 路相机 0.9622→0.9952、4 路 lidar 0.7221→0.9950（约 8× / 80×）。
#   ⚠ 只用 export（不写 ~/.bashrc）⇒ 仅影响 KeShip，不污染全局环境。
#   ⚠ MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA 必须给，否则默认落到 AMD 集显。
# ═══════════════════════════════════════════════════════════
if [ "$NO_D3D12" = true ]; then
    echo "[渲染] 已禁用 D3D12 (--no-d3d12)，使用 Mesa 默认（通常为 llvmpipe 软件渲染）"
else
    export GALLIUM_DRIVER=d3d12
    export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
    unset MESA_LOADER_DRIVER_OVERRIDE   # 该变量不是选 Gallium 后端的方式，留着会干扰
    echo "[渲染] 已启用 D3D12 后端（NVIDIA 独显）:"
    echo "         GALLIUM_DRIVER=$GALLIUM_DRIVER"
    echo "         MESA_D3D12_DEFAULT_ADAPTER_NAME=$MESA_D3D12_DEFAULT_ADAPTER_NAME"
fi

echo ""

# ═══════════════════════════════════════════════════════════
# Step 4: GPU 预热（解决 WSLg D3D12/NVIDIA 驱动竞争条件）
#   放在编译之后、紧邻启动 —— 见顶部顺序约束 2
# ═══════════════════════════════════════════════════════════
gpu_warmup() {
    # 仅在 WSL 环境下执行
    if ! grep -qi "microsoft\|wsl" /proc/version 2>/dev/null; then
        echo "[GPU] 非 WSL 环境，跳过预热"
        return 0
    fi

    echo "[GPU] WSL 检测到，正在预热 GPU 管线..."

    # 方法: Python ctypes 加载 EGL/D3D12 库
    # 这会触发 Mesa → Gallium → D3D12 → NVIDIA 驱动链的完整初始化
    python3 -c "
import ctypes, sys

libs_tried = []
for lib_path in ['libEGL.so.1', 'libEGL.so', '/usr/lib/wsl/lib/libd3d12.so', '/usr/lib/wsl/lib/libd3d12core.so']:
    try:
        lib = ctypes.CDLL(lib_path)
        libs_tried.append(f'{lib_path} ✓')
    except Exception as e:
        libs_tried.append(f'{lib_path} ✗ ({e})')

success = any('✓' in l for l in libs_tried)
print('  ' + '\n  '.join(libs_tried))
if success:
    print('[GPU] 预热成功 — GPU 管线已就绪')
else:
    print('[GPU] 警告: 未能预加载任何 GPU 库，首次启动可能失败')
# 预热结果只用于提示：历史上曾因 GPU 环境异常导致无法启动，
# 故此处无论成功与否都不阻塞后续启动（原实现写成 0 if success else 0，
# 两个分支相同，属无效表达式，已改为明确的 sys.exit(0)）。
sys.exit(0)
" 2>/dev/null || echo "[GPU] 警告: Python 预热执行异常，继续启动..."

    # 给驱动一点时间稳定
    sleep 1
}

if [ "$NO_GPU_WARM" != true ]; then
    gpu_warmup
else
    echo "[GPU] 已跳过预热 (--no-gpu-warm)"
fi

echo ""

# ═══════════════════════════════════════════════════════════
# Step 5: 启动 KeShip（带崩溃自动重试）
# ═══════════════════════════════════════════════════════════
MAX_RETRIES=2
ATTEMPT=0

# 用数组构造命令：extra_gz_args 的值可能含空格，必须作为一个整体传入
# （旧实现把未知参数拼进 EXTRA_GZ_ARGS 却从不转发，属静默失效）
LAUNCH_CMD=(ros2 launch keship keship.launch.py)
if [ "$HEADLESS" = true ]; then
    LAUNCH_CMD+=(headless:=true)
fi
if [ -n "$EXTRA_GZ_ARGS" ]; then
    LAUNCH_CMD+=("extra_gz_args:=$EXTRA_GZ_ARGS")
fi

echo "[启动] 正在启动 KeShip 仿真..."
echo ""

while [ $ATTEMPT -le $MAX_RETRIES ]; do
    ATTEMPT=$((ATTEMPT + 1))

    if [ $ATTEMPT -gt 1 ]; then
        echo ""
        echo "──────────────────────────────────────"
        echo "  ⚠ 第 $ATTEMPT 次尝试（共 $((MAX_RETRIES + 1)) 次）..."
        echo "──────────────────────────────────────"
        sleep 2
    fi

    # 执行启动（set +e 确保崩溃退出码能被捕获，不被 set -e 拦截）
    set +e
    "${LAUNCH_CMD[@]}"
    EXIT_CODE=$?
    set -e

    # 判断退出码
    case $EXIT_CODE in
        0)
            echo ""
            echo "[启动] KeShip 正常退出"
            exit 0
            ;;
        139)  # SIGSEGV
            CRASH_TYPE="内存段错误 (SIGSEGV)"
            ;;
        134)  # SIGABRT
            CRASH_TYPE="异常中止 (SIGABRT)"
            ;;
        *)
            CRASH_TYPE="退出码 $EXIT_CODE"
            ;;
    esac

    if [ $ATTEMPT -le $MAX_RETRIES ]; then
        echo "[启动] 检测到崩溃: $CRASH_TYPE，准备重试..."
    else
        echo "[启动] 已达到最大重试次数 ($((MAX_RETRIES + 1)))，启动失败"
        echo "[启动] 建议: 关闭 WSL 终端，等待几秒后重试"
        exit $EXIT_CODE
    fi
done
