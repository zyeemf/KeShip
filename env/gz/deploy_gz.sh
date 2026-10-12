#!/usr/bin/env bash
# L1：自建 gz-sim（含 S1 patch）—— 源码 → 构建 → 部署到 /opt/keship-gz → 验证
#
# ★ 编排脚本；真正的改动由 patch_gzsim_addedmass_s1.py 完成（精确匹配 + 唯一命中断言 + .bak_s1 备份）。
# ★ 幂等：已部署则提示，加 --force 重建。
#
# 用法:
#   bash env/gz/deploy_gz.sh [选项]
#   # 装系统路径（需 sudo 免密或密码）：sudo bash env/gz/deploy_gz.sh
#   # 免 sudo 用户级部署：bash env/gz/deploy_gz.sh --prefix=$HOME/keship-gz
# 选项:
#   --src=<dir>      源码树（默认 ~/gz-build/gz-sim-8.11.0）
#   --tarball=<p>    源码包（默认 ~/gz-build/gz-sim-8.11.0.tar.gz；src 不存在则解压）
#   --prefix=<dir>   安装前缀（默认 /opt/keship-gz；用户目录前缀免 sudo）
#   --jobs=<n>       并行数（默认 nproc）
#   --force          强制重建（即使已部署）
#   --skip-patch     跳过打补丁（源码已打过时）
#   --dry-run        只打印将执行的命令
set -uo pipefail

GZ_DIR="${HOME}/gz-build"
SRC="${GZ_DIR}/gz-sim-8.11.0"
TARBALL="${GZ_DIR}/gz-sim-8.11.0.tar.gz"
PREFIX="/opt/keship-gz"
JOBS="$(nproc)"
FORCE=0
SKIP_PATCH=0
DRY=0
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for a in "$@"; do
  case "$a" in
    --src=*)       SRC="${a#*=}" ;;
    --tarball=*)   TARBALL="${a#*=}" ;;
    --prefix=*)    PREFIX="${a#*=}" ;;
    --jobs=*)      JOBS="${a#*=}" ;;
    --force)       FORCE=1 ;;
    --skip-patch)  SKIP_PATCH=1 ;;
    --dry-run)     DRY=1 ;;
    -h|--help)     sed -n '2,25p' "$0"; exit 0 ;;
  esac
done

log() { echo "[$(date +%H:%M:%S)] $*"; }
run() { if [ "$DRY" = 1 ]; then echo "  (dry) $*"; else "$@"; fi; }

echo "KeShip L1：自建 gz-sim 部署"
echo "============================================"
echo "源码树  : ${SRC}"
echo "源码包  : ${TARBALL}"
echo "安装前缀: ${PREFIX}"
echo "并行数  : ${JOBS}"
echo ""

# ---------------- 安装权限预检 ----------------
# root 仅在前缀（或其父目录）不可写时才必需；用户目录前缀免 sudo 即可部署
if [ "$DRY" = 0 ] && [ "$(id -u)" -ne 0 ]; then
  _target="$PREFIX"
  while [ ! -d "$_target" ] && [ "$_target" != "/" ] && [ "$_target" != "$HOME" ]; do
    _target="$(dirname "$_target")"
  done
  if [ ! -w "$_target" ]; then
    echo "[失败] 前缀 ${PREFIX} 归属 $(stat -c %U "$_target" 2>/dev/null || echo root)，当前用户无写权限。" >&2
    echo "       若装系统路径请用 sudo 运行；或改用 --prefix=\$HOME/xxx 免 sudo 部署。" >&2
    exit 1
  fi
  echo "[提示] 非 root 运行：安装到用户前缀 ${PREFIX}（免 sudo）"
  echo ""
fi

# ---------------- 1. 源码就位 ----------------
log "[1/6] 准备源码树"
if [ ! -d "$SRC" ]; then
  [ -f "$TARBALL" ] || { echo "[失败] 源码树与源码包都不存在：$SRC / $TARBALL" >&2; exit 1; }
  run tar -xzf "$TARBALL" -C "$(dirname "$SRC")"
  log "      已解压 $(basename "$TARBALL")"
else
  log "      源码树已存在，跳过解压"
fi

# ---------------- 2. S1 patch ----------------
log "[2/6] S1 patch（运行时 SetFluidAddedMass 通路）"
if [ "$SKIP_PATCH" = 1 ]; then
  log "      --skip-patch：跳过"
elif grep -q "SetFluidAddedMass" "$SRC/include/gz/sim/Link.hh" 2>/dev/null; then
  log "      源码已含 SetFluidAddedMass ⇒ 已 patch，跳过（幂等）"
else
  run python3 "${HERE}/patch_gzsim_addedmass_s1.py" \
    || { echo "[失败] S1 patch 失败（源码状态异常，勿强行继续）" >&2; exit 1; }
fi

# ---------------- 3. 构建 ----------------
BUILD="${GZ_DIR}/build"
log "[3/6] cmake + make（-j${JOBS}）"
run mkdir -p "$BUILD"
run cmake -S "$SRC" -B "$BUILD" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
[ "$DRY" = 1 ] || cmake --build "$BUILD" -j"$JOBS" || { echo "[失败] make 失败" >&2; exit 1; }

# ---------------- 4. 安装 ----------------
log "[4/6] make install → ${PREFIX}"
run cmake --install "$BUILD"

# ---------------- 5. 部署验证（T4 判据）----------------
log "[5/6] 部署验证"
if [ "$DRY" = 0 ]; then
  SO="${PREFIX}/lib/libgz-sim8.so"
  if [ -e "$SO" ]; then
    if nm -D --defined-only "$(readlink -f "$SO")" 2>/dev/null | grep -q "SetFluidAddedMass"; then
      echo "  [OK] SetFluidAddedMass 符号已定义（$SO -> $(readlink -f "$SO")）"
    else
      echo "  [!!] SetFluidAddedMass 符号未找到（S1 patch 未生效？）"
    fi
  else
    echo "  [!!] 未找到 $SO"
  fi
  # 可加载性：用 LD_LIBRARY_PATH 优先解析到自建库（注意：gz CLI 的 --version 走 gz 配置，
  # 读到的是系统版，属预期；自建库靠 LD_LIBRARY_PATH 在 dlopen 时优先命中）
  if LD_LIBRARY_PATH="${PREFIX}/lib" python3 -c "import ctypes; ctypes.CDLL('libgz-sim8.so'); print('  [OK] 自建库可加载（LD_LIBRARY_PATH 优先命中）')" 2>/dev/null; then
    :
  else
    echo "  [!!] 自建库加载失败"
  fi
  echo "  注：gz sim --version 显示的是系统版本（CLI 经 gz 配置解析库），不代表部署失败"
fi

# ---------------- 6. 静默库（B：DART 告警抑制）----------------
log "[6/6] 编译 DART 告警静默库（kship_quiet_inertia.so）"
if [ "$DRY" = 0 ] && [ -f "${HERE}/kship_quiet_inertia.cpp" ]; then
  QSRC="${HERE}/kship_quiet_inertia.cpp"
  QOUT="${GZ_DIR}/kship_quiet_inertia.so"
  run g++ -shared -fPIC -O2 "$QSRC" -o "$QOUT" -ldl
  log "      产出 $QOUT"
else
  log "      （源码不在（或 dry-run），跳过）"
fi

echo ""
echo "完成。${PREFIX} 已就绪（S2 在 GUI 下生效需 start_keship.sh 探测到它）"
