#!/bin/bash
# KeShip 仿真环境准备（**唯一来源**）—— 供 start_keship.sh 与所有 headless 台架 source。
#
# 2026-10-23 创建。起因：张老师指出「GUI 与无头测试路径不一致，是测试隐患」。
#   根因：环境配置（自建 gz-sim / 自建 gz-physics / DART 静默库）散落在 start_keship.sh
#         的 Step 3.6 / 3.7 里；任何绕过它的路径（如 `ros2 launch keship ...`）都会缺环境。
#   后果（已发生）：2026-10-22 夜的 E0~E6 全部跑在系统 gz-sim 8.10.0 上
#         （symAvail=0 ⇒ S2 降级），结论一度误判为"环境缺符号需自建"。
#   对策：把环境准备抽到本文件，**两条路径共用**；并提供 keship_env_check 自检。
#
# 用法：
#   source /home/keship/keship_ws/tools/keship_env.sh
#   keship_env_check          # 打印实际生效的库路径，便于日志留证
#
# ⚠ 本文件只 export 环境变量，**不启动任何进程**；不要写 exit（会被 source）。

# ── Step 1: 自建 gz-sim（含 S1 补丁的 Link::SetFluidAddedMass ⇒ S2 生效）──
KESHIP_GZ_PREFIX=""
if [ -n "${KESHIP_GZ_PREFIX_OVERRIDE:-}" ]; then
    KESHIP_GZ_PREFIX="$KESHIP_GZ_PREFIX_OVERRIDE"
elif [ -d "/opt/keship-gz/lib" ]; then
    KESHIP_GZ_PREFIX="/opt/keship-gz"
elif [ -d "$HOME/keship-gz/lib" ]; then
    KESHIP_GZ_PREFIX="$HOME/keship-gz"
fi
if [ -n "$KESHIP_GZ_PREFIX" ] && [ -d "$KESHIP_GZ_PREFIX/lib" ]; then
    export LD_LIBRARY_PATH="$KESHIP_GZ_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    # 自建 gz-sim（8.11.0）与系统 8.10 栈版本错配，需补 ROS vendor 的 Jetty 版依赖
    for _d in /opt/ros/jazzy/opt/gz_*_vendor/lib; do
        [ -d "$_d" ] && export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$_d"
    done
    if [ -d "$KESHIP_GZ_PREFIX/lib/gz-sim-8/plugins" ]; then
        export GZ_SIM_SYSTEM_PLUGIN_PATH="$KESHIP_GZ_PREFIX/lib/gz-sim-8/plugins${GZ_SIM_SYSTEM_PLUGIN_PATH:+:$GZ_SIM_SYSTEM_PLUGIN_PATH}"
    fi
    echo "[环境] 自建 gz-sim: $KESHIP_GZ_PREFIX（S2 出水取消 M_A 可生效）"
else
    echo "[环境] ⚠ 未发现自建 gz-sim —— S2 将自动降级（空中仍带 M_A）"
fi

# ── Step 2: 自建 gz-physics 引擎插件（关 DART 惯性自校验）+ DART 告警静默 ──
KESHIP_PHYSICS_PREFIX="${KESHIP_PHYSICS_PREFIX-/home/keship/gz-build/install-physics-7.8.0}"
if [ -n "$KESHIP_PHYSICS_PREFIX" ] && [ -d "$KESHIP_PHYSICS_PREFIX/lib/gz-physics-7/engine-plugins" ]; then
    export GZ_SIM_PHYSICS_ENGINE_PATH="$KESHIP_PHYSICS_PREFIX/lib/gz-physics-7/engine-plugins${GZ_SIM_PHYSICS_ENGINE_PATH:+:$GZ_SIM_PHYSICS_ENGINE_PATH}"
    export LD_LIBRARY_PATH="$KESHIP_PHYSICS_PREFIX/lib/gz-physics-7/engine-plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    echo "[环境] 自建 gz-physics 引擎插件: $KESHIP_PHYSICS_PREFIX"
else
    echo "[环境] ⚠ 未发现自建 gz-physics —— 走系统/vendor（DART 告警照旧）"
fi

KESHIP_QUIET_INERTIA="${KESHIP_QUIET_INERTIA-1}"
KESHIP_QUIET_SO=/home/keship/gz-build/kship_quiet_inertia.so
if [ "$KESHIP_QUIET_INERTIA" = "1" ] && [ -f "$KESHIP_QUIET_SO" ]; then
    export LD_PRELOAD="$KESHIP_QUIET_SO${LD_PRELOAD:+:$LD_PRELOAD}"
    echo "[环境] DART 告警静默库: $KESHIP_QUIET_SO"
fi

# ── 自检：打印实际生效的关键项（台架日志留证用）──
keship_env_check() {
    echo "=== KeShip 环境自检 ==="
    echo "  LD_LIBRARY_PATH 前 2 项："
    echo "$LD_LIBRARY_PATH" | tr ':' '\n' | head -2 | sed 's/^/    /'
    local gzsim=""
    if [ -n "$KESHIP_GZ_PREFIX" ] && [ -f "$KESHIP_GZ_PREFIX/lib/libgz-sim8.so.8" ]; then
        gzsim="$KESHIP_GZ_PREFIX/lib/libgz-sim8.so.8"
    fi
    if [ -n "$gzsim" ]; then
        local sym
        sym=$(nm -D --defined-only "$gzsim" 2>/dev/null | grep -c SetFluidAddedMass)
        echo "  gz-sim: $gzsim"
        echo "  SetFluidAddedMass 符号数: $sym（0 ⇒ S2 会降级）"
    else
        echo "  gz-sim: ⚠ 未使用自建版本（S2 会降级）"
    fi
    echo "  GZ_SIM_PHYSICS_ENGINE_PATH: ${GZ_SIM_PHYSICS_ENGINE_PATH:-<未设>}"
    echo "  LD_PRELOAD: ${LD_PRELOAD:-<未设>}"
    echo "======================"
}
