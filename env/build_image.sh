#!/usr/bin/env bash
# KeShip 镜像构建 —— 从「空 Ubuntu 起点」产出可分发的镜像。
#
# ★ 核心约束（方案 §4.2）：**镜像只能由脚本产出，禁止手工做一次**。
#   脚本更新 ⇒ 重跑本脚本 ⇒ 新镜像（脚本与镜像同源，防腐烂）。
#
# ★ 运行位置：在任一 WSL 发行版里跑（经 /mnt/c/.../wsl.exe 调 Windows 侧 wsl 命令）。
#
# 流程：import（干净起点）→ 建用户 → 投放源码 → install → check → export → 压缩分卷
#
# 用法: bash env/build_image.sh [选项]
#   --base-tar=<path>   空环境 tar（默认 /mnt/d/WSL/Ubuntu2404-Empty-sdust-331625.tar）
#   --distro=<name>     构建用发行版名（默认 KeShipBuild；重建会先注销它）
#   --profile=l0|full   安装档位（默认 full）
#   --out-dir=<dir>     输出目录（默认 /mnt/d/WSL/out）
#   --keep              构建完保留构建发行版（排错用）
#   --dry-run           只打印将执行的命令
#
# 2026-10-12 首轮实测修 4 处：
#   ① 空环境 tar **无 keship 用户** ⇒ import 后补建（+ sudo 免密 + wsl.conf 默认用户）
#   ② 调 wsl.exe 时**当前目录为 UNC** ⇒ 报 "Failed to translate" ⇒ 统一 cd /mnt/c 再调
#   ③ `tar -xf 'D:\…'` 把 D: 当远程主机 ⇒ 改用 WSL 路径 /mnt/d/…
#   ④ `zstd` 未装 ⇒ 自动降级 zstd → xz → gzip
set -uo pipefail

BASE_TAR="/mnt/d/WSL/Ubuntu2404-Empty-sdust-331625.tar"
DISTRO="KeShipBuild"
PROFILE="full"
OUT_DIR="/mnt/d/WSL/out"
KEEP=0
DRY=0
WSL_EXE="/mnt/c/Windows/System32/wsl.exe"
REPO="${HOME}/keship_ws"

for a in "$@"; do
  case "$a" in
    --base-tar=*) BASE_TAR="${a#*=}" ;;
    --distro=*)   DISTRO="${a#*=}" ;;
    --profile=*)  PROFILE="${a#*=}" ;;
    --out-dir=*)  OUT_DIR="${a#*=}" ;;
    --keep)       KEEP=1 ;;
    --dry-run)    DRY=1 ;;
    -h|--help)    sed -n '2,28p' "$0"; exit 0 ;;
  esac
done

log() { echo "[$(date +%H:%M:%S)] $*"; }
# ★ 修 ②：统一在 /mnt/c 下调 wsl.exe，避免 UNC 工作目录导致 "Failed to translate"
W()   { ( cd /mnt/c && "$WSL_EXE" "$@" ); }
run() { if [ "$DRY" = 1 ]; then echo "  (dry) $*"; else "$@"; fi; }

echo "KeShip 镜像构建"
echo "============================================"
echo "起点 tar  : ${BASE_TAR}"
echo "构建发行版: ${DISTRO}"
echo "安装档位  : ${PROFILE}"
echo "输出目录  : ${OUT_DIR}"
echo ""

# ---------------- 0. 前置 ----------------
[ -x "$WSL_EXE" ] || { echo "[失败] 找不到 $WSL_EXE（须在 WSL 内运行）" >&2; exit 1; }
[ -f "$BASE_TAR" ] || { echo "[失败] 起点 tar 不存在: $BASE_TAR" >&2; exit 1; }
[ -f "$REPO/env/keship-env.sh" ] || { echo "[失败] 缺 $REPO/env/keship-env.sh" >&2; exit 1; }
BASE_TAR_WIN="$(wslpath -w "$BASE_TAR")"
mkdir -p "$OUT_DIR"

# ---------------- 1. 干净起点 ----------------
log "[1/8] 注销旧构建环境（若有）"
if W -l -q 2>/dev/null | tr -d '\0\r' | grep -qx "$DISTRO"; then
  run W --terminate "$DISTRO" || true
  run W --unregister "$DISTRO"
  log "      已注销 $DISTRO"
else
  log "      （不存在，跳过）"
fi

log "[2/8] 从空环境 tar 导入（干净起点）"
INSTALL_LOC_WIN="$(wslpath -w "$OUT_DIR")\\wsl-${DISTRO}"
run W --import "$DISTRO" "$INSTALL_LOC_WIN" "$BASE_TAR_WIN"

# ---------------- 2. 补建 keship 用户（★ 修 ①）----------------
log "[3/8] 补建 keship 用户 + sudo 免密 + 默认用户"
if [ "$DRY" = 0 ]; then
  run W -d "$DISTRO" -u root -- bash -lc '
    set -e
    if ! id keship >/dev/null 2>&1; then
      useradd -m -s /bin/bash keship
      echo "keship:keship" | chpasswd
      usermod -aG sudo,adm keship
    fi
    echo "keship ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/keship
    chmod 440 /etc/sudoers.d/keship
    printf "[user]\ndefault=keship\n" > /etc/wsl.conf
    echo "  [OK] keship 用户就绪："; id keship
  '
else
  echo "  (dry) 建 keship 用户 + sudo 免密 + /etc/wsl.conf default=keship"
fi

# ---------------- 3. 环境探测 ----------------
log "[4/8] 探测：sudo 免密 / 网络"
if [ "$DRY" = 0 ]; then
  run W -d "$DISTRO" -u keship -- bash -lc '
    sudo -n true && echo "  [OK] sudo 免密" || echo "  [警告] sudo 非免密"
    curl -fsS -o /dev/null -w "  [OK] 网络 HTTP %{http_code}\n" --max-time 10 \
      https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu/ || echo "  [警告] 网络不通"
  '
fi

# ---------------- 4. 投放源码（★ 修 ③：用 WSL 路径）----------------
log "[5/8] 投放 KeShip 源码（git archive HEAD，不含 build/install/log/data）"
SRC_TAR="${OUT_DIR}/_keship_src.tar"
if [ "$DRY" = 0 ]; then
  ( cd "$REPO" && git archive --format=tar HEAD ) > "$SRC_TAR"
  log "      $(du -h "$SRC_TAR" | cut -f1) → ${DISTRO}:/home/keship/keship_ws"
  run W -d "$DISTRO" -u root -- bash -lc \
    "mkdir -p /home/keship/keship_ws && tar -xf '${SRC_TAR}' -C /home/keship/keship_ws && chown -R keship:keship /home/keship/keship_ws"
else
  echo "  (dry) git archive ⇒ ${SRC_TAR} ⇒ tar -xf ⇒ ${DISTRO}:/home/keship/keship_ws"
fi

# ---------------- 5. install ----------------
log "[6/8] install --profile=${PROFILE}（耗时较长）"
run W -d "$DISTRO" -u keship -- bash -lc \
  "cd /home/keship/keship_ws && bash env/keship-env.sh install --profile=${PROFILE}"

# ---------------- 6. 质量门 ----------------
log "[7/8] 质量门：check（判据 24/24 全绿）"
run W -d "$DISTRO" -u keship -- bash -lc \
  "cd /home/keship/keship_ws && bash env/keship-env.sh check"

# ---------------- 7. export + 压缩分卷（★ 修 ④）----------------
TS="$(date +%Y%m%d_%H%M)"
OUT_TAR="${OUT_DIR}/keship_${PROFILE}_${TS}.tar"
log "[8/8] 导出 → ${OUT_TAR}"
run W --terminate "$DISTRO" || true
run W --export "$DISTRO" "$(wslpath -w "$OUT_TAR")"

if [ "$DRY" = 0 ] && [ -f "$OUT_TAR" ]; then
  if   command -v zstd >/dev/null; then log "      压缩（zstd -19 --long）"; zstd -T0 -19 --long -f "$OUT_TAR" -o "${OUT_TAR}.zst"; COMP="${OUT_TAR}.zst"
  elif command -v xz   >/dev/null; then log "      压缩（xz -9）";            xz -T0 -9 -f "$OUT_TAR";                              COMP="${OUT_TAR}.xz"
  else                                  log "      压缩（gzip -9）";          gzip -9 -f "$OUT_TAR";                                COMP="${OUT_TAR}.gz"
  fi
  log "      2GB 分卷"
  split -b 2G -d -a 2 "$COMP" "${COMP}.part"
  rm -f "$OUT_TAR" "$COMP"
  log "      产出："; ls -lh "${COMP}.part"* 2>/dev/null | awk '{print "        "$5"  "$9}'
fi

# ---------------- 8. 清理 ----------------
if [ "$DRY" = 1 ]; then
  log "[清理] 注销 ${DISTRO}（dry-run：跳过）"
elif [ "$KEEP" = 0 ]; then
  log "[清理] 注销构建环境 ${DISTRO}"
  run W --unregister "$DISTRO"
else
  log "[保留] 构建环境 ${DISTRO}（--keep）"
fi

echo ""
echo "完成。产出目录：${OUT_DIR}"
