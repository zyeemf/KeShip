#!/bin/bash
# ============================================================
#  KeShip 一键启动脚本
#  功能: GPU预热 → 编译 → 激活环境 → 启动(自动重试)
#
#  用法:
#    ~/keship_ws/start_keship.sh              # 编译 + 启动
#    ~/keship_ws/start_keship.sh --no-build   # 跳过编译
#    ~/keship_ws/start_keship.sh --headless   # 无头模式
#
#  单例互斥：同一时刻只允许一个 KeShip 实例运行，
#  重复启动会被拒绝（避免多实例 gz-transport 服务/话题互相干扰）。
# ============================================================
set -e

# ── 参数解析 ──
NO_BUILD=false
HEADLESS=false
EXTRA_GZ_ARGS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build)     NO_BUILD=true; shift ;;
        --headless)     HEADLESS=true; shift ;;
        --no-gpu-warm)  NO_GPU_WARM=true; shift ;;
        *)  EXTRA_GZ_ARGS="$EXTRA_GZ_ARGS $1"; shift ;;
    esac
done

KSHIP_WS="$HOME/keship_ws"
cd "$KSHIP_WS"

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
# Step 1: GPU 预热（解决 WSLg D3D12/NVIDIA 驱动竞争条件）
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

# 至少加载成功一个就认为预热完成
success = any('✓' in l for l in libs_tried)
print('  ' + '\n  '.join(libs_tried))
if success:
    print('[GPU] 预热成功 — GPU 管线已就绪')
else:
    print('[GPU] 警告: 未能预加载任何 GPU 库，首次启动可能失败')
sys.exit(0 if success else 0)  # 总是继续，不阻塞启动
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
# Step 2: 激活 ROS 2 环境（必须先于编译）
# ═══════════════════════════════════════════════════════════
echo "[环境] 激活 ROS 2 + Gazebo 环境..."
. /opt/ros/jazzy/setup.bash
. "$KSHIP_WS/install/setup.bash"
echo "[环境] 完成"

echo ""

# ═══════════════════════════════════════════════════════════
# Step 3: 编译
# ═══════════════════════════════════════════════════════════
if [ "$NO_BUILD" = true ]; then
    echo "[编译] 已跳过 (--no-build)"
else
    echo "[编译] 正在编译 keship 包..."
    # 注意：--log-base 是 colcon 全局参数，必须放在子命令 build 之前
    # （放在 build 之后会报 "unrecognized arguments: --log-base ..."，2026-08-14 修复）
    colcon --log-base "$KSHIP_WS/data/log" build --packages-select keship --merge-install
    echo "[编译] 完成"
fi

echo ""

# ═══════════════════════════════════════════════════════════
# Step 4: 启动 KeShip（带崩溃自动重试）
# ═══════════════════════════════════════════════════════════
MAX_RETRIES=2
ATTEMPT=0
LAUNCH_ARGS=""
[ "$HEADLESS" = true ] && LAUNCH_ARGS="$LAUNCH_ARGS headless:=true"

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
    ros2 launch keship keship.launch.py $LAUNCH_ARGS
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
