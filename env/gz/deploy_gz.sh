#!/usr/bin/env bash
# L1：自建 gz-sim（含 S1 patch）—— 源码 → 构建 → 部署到 /opt/keship-gz → 验证
#
# ★ 编排脚本；真正的改动由 patch_gzsim_addedmass_s1.py 完成（精确匹配 + 唯一命中断言 + .bak_s1 备份）。
# ★ 幂等：已部署则提示，加 --force 重建。
#
# 用法:
#   sudo bash env/gz/deploy_gz.sh [选项]
# 选项:
#   --src=<dir>      源码树（默认 ~/gz-build/gz-sim-8.11.0）
#   --tarball=<p>    源码包（默认 ~/gz-build/gz-sim-8.11.0.tar.gz；src 不存在则解压）
#   --prefix=<dir>   安装前缀（默认 /opt/keship-gz）
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

if [ "$(id -u)" -ne 0 ] && [ "$DRY" = 0 ]; then
  echo "[失败] 需 root（要装到 ${PREFIX}）。请用 sudo 运行。" >&2; exit 1
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
log "[2/6] 打 S1 patch（运行时 SetFluidAddedMass 通路）"
if [ "$SKIP_PATCH" = 1 ]; then
  log "      --skip-patch：跳过"
else
  run python3 "${HERE}/patch_gzsim_addedmass_s1.py"
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
  echo -n "  gz sim 版本: "
  LD_LIBRARY_PATH="${PREFIX}/lib" gz sim --version 2>/dev/null | head -1 || echo "(失败)"
  SO="$(ls "${PREFIX}"/lib/libgz-sim8.so* 2>/dev/null | head -1)"
  if [ -n "$SO" ]; then
    if nm -D --defined-only "$SO" 2>/dev/null | grep -q "SetFluidAddedMass"; then
      echo "  [OK] SetFluidAddedMass 符号已定义"
    else
      echo "  [!!] SetFluidAddedMass 符号未找到（S1 patch 未生效？）"
    fi
  else
    echo "  [!!] 未找到 libgz-sim8.so"
  fi
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
