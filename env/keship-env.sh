#!/usr/bin/env bash
# ============================================================
#  keship-env.sh —— KeShip 环境配置（单一事实来源：env/deps.manifest）
#
#  三种模式：
#    check                    只读自检，输出可读诊断报告
#                             （学生出问题也能跑，报告可直接发给老师）
#    install [--profile=l0]   安装缺失项（L0：基础 + gazebo + ros + keship 依赖）
#    install --profile=full   在 L0 基础上加 L1（自建 gz-sim → /opt/keship-gz/，见 T4）
#    repair                   只补缺失项（等价于 install 的幂等收敛）
#
#  可选参数：
#    --mirror=tuna|official   apt 镜像（默认 tuna；official 用于海外/镜像异常时回退）
#
#  设计纪律：
#    * 只读优先 —— check 绝不改动系统
#    * 幂等 —— 已满足的项不重复安装
#    * 每项独立判定，判据不混用（pkg 用 dpkg / cmd 用 command -v）
#
#  ⚠ 不要用 set -u：/opt/ros/jazzy/setup.bash 引用未绑定变量（本项目已知坑）
#
#  apt 源策略（2026-10-05 实测，非推测）：
#    * Ubuntu : 清华镜像 https（空镜像已带）
#    * ROS    : 默认清华镜像 https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu
#               —— 官方 packages.ros.org 的 https 不通（000），只有 http 通；
#                  基线 ros2.sources 亦为 http ⇒ 官方仅作回退（--mirror=official）
#    * OSRF   : 只能用官方 https://packages.osrfoundation.org/gazebo/ubuntu-stable
#               —— 清华/中科大的 osrf 镜像均为 404（不存在）
# ============================================================

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="${SCRIPT_DIR}/deps.manifest"
MIRROR="${KESHIP_MIRROR:-tuna}"

if [ -t 1 ]; then
  C_OK=$'\033[32m'; C_BAD=$'\033[31m'; C_DIM=$'\033[2m'; C_RST=$'\033[0m'
else
  C_OK=''; C_BAD=''; C_DIM=''; C_RST=''
fi

if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; else SUDO=""; fi

layer_name() {
  case "$1" in
    base)   echo "base · 基础工具链" ;;
    gazebo) echo "gazebo · Gazebo Harmonic" ;;
    ros)    echo "ros · ROS 2 Jazzy" ;;
    keship) echo "keship · KeShip 直接依赖" ;;
    *)      echo "$1" ;;
  esac
}

# ---------- 检查单项：输出 "状态|详情" ----------
check_item() {
  local kind="$1" ident="$2"
  case "$kind" in
    pkg)
      local v
      v="$(dpkg-query -W -f='${Version}' "$ident" 2>/dev/null)"
      if [ -n "$v" ]; then printf 'OK|%s' "$v"; else printf 'MISSING|'; fi
      ;;
    cmd)
      local p
      p="$(command -v "$ident" 2>/dev/null)"
      if [ -n "$p" ]; then printf 'OK|%s' "$p"; else printf 'MISSING|'; fi
      ;;
    *)
      printf 'UNKNOWN|'
      ;;
  esac
}

# ---------- 列出缺失的 pkg 项（空格分隔）----------
missing_pkgs() {
  local out=""
  while IFS='|' read -r layer kind ident baseline desc; do
    case "$layer" in ''|'#'*) continue ;; esac
    [ "$kind" = "pkg" ] || continue
    if [ -z "$(dpkg-query -W -f='${Version}' "$ident" 2>/dev/null)" ]; then
      out="$out $ident"
    fi
  done < "$MANIFEST"
  printf '%s' "$out"
}

# ---------- apt 源 ----------
ros_source_url() {
  if [ "$MIRROR" = "official" ]; then
    echo "http://packages.ros.org/ros2/ubuntu"
  else
    echo "https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu"
  fi
}

ensure_ros_source() {
  local key=/usr/share/keyrings/ros-archive-keyring.gpg
  local list=/etc/apt/sources.list.d/ros2.list
  local src=/etc/apt/sources.list.d/ros2.sources
  # 2026-10-12（T6 幂等验证发现）：基线用 deb822 的 ros2.sources（符号链接），
  # 旧实现只查 ros2.list ⇒ 误判"未配置"⇒ repair 在"无缺失"时仍要 sudo 写文件 ⇒ 挂住。
  if { [ -f "$list" ] && grep -q "ros2" "$list" 2>/dev/null; } \
     || [ -e "$src" ]; then
    echo "  [跳过] ROS 源已配置"
    return 0
  fi
  echo "  [配置] ROS 源 -> $(ros_source_url)"
  if [ ! -f "$key" ]; then
    $SUDO curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o "$key" || return 1
  fi
  echo "deb [arch=amd64 signed-by=$key] $(ros_source_url) noble main" | $SUDO tee "$list" >/dev/null
}

ensure_osrf_source() {
  local key=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg
  local list=/etc/apt/sources.list.d/gazebo-stable.list
  local src=/etc/apt/sources.list.d/gazebo-stable.sources
  if [ -f "$list" ] || [ -e "$src" ]; then
    echo "  [跳过] OSRF 源已配置"
    return 0
  fi
  echo "  [配置] OSRF 源（无国内镜像，只能用官方 https）"
  if [ ! -f "$key" ]; then
    $SUDO curl -fsSL https://packages.osrfoundation.org/gazebo.gpg -o "$key" || return 1
  fi
  echo "deb [arch=amd64 signed-by=$key] https://packages.osrfoundation.org/gazebo/ubuntu-stable noble main" | $SUDO tee "$list" >/dev/null
}

# ---------- check ----------
do_check() {
  if [ ! -f "$MANIFEST" ]; then
    echo "[错误] 找不到依赖清单: $MANIFEST" >&2
    return 2
  fi

  # ROS 的命令（ros2 等）只有 source 之后才在 PATH 中 —— 2026-10-05 实测踩到
  if [ -f /opt/ros/jazzy/setup.bash ]; then
    source /opt/ros/jazzy/setup.bash >/dev/null 2>&1
  fi

  local total=0 ok=0 missing=0
  local last_layer=""

  echo "KeShip 环境自检报告"
  echo "============================================"
  echo "主机: $(hostname)"
  echo "系统: $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-未知}")"
  echo "内核: $(uname -r)"
  echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "清单: ${MANIFEST}"
  echo "--------------------------------------------"

  while IFS='|' read -r layer kind ident baseline desc; do
    case "$layer" in ''|'#'*) continue ;; esac
    if [ "$layer" != "$last_layer" ]; then
      echo ""
      echo "$(layer_name "$layer")"
      last_layer="$layer"
    fi

    total=$((total + 1))
    local res status detail
    res="$(check_item "$kind" "$ident")"
    status="${res%%|*}"
    detail="${res#*|}"

    if [ "$status" = "OK" ]; then
      ok=$((ok + 1))
      printf '  %s[OK]%s    %-32s %s\n' "$C_OK" "$C_RST" "$ident" "${C_DIM}${detail}${C_RST}"
    else
      missing=$((missing + 1))
      printf '  %s[缺失]%s  %-32s %s\n' "$C_BAD" "$C_RST" "$ident" "${C_DIM}${desc}${C_RST}"
    fi
  done < "$MANIFEST"

  echo ""
  echo "--------------------------------------------"
  echo "汇总: 共 ${total} 项，已满足 ${ok}，缺失 ${missing}"
  if [ "$missing" -eq 0 ]; then
    echo "${C_OK}结论: 环境完整${C_RST}"
    return 0
  fi
  echo "${C_BAD}结论: 环境不完整 —— 缺失 ${missing} 项${C_RST}"
  echo ""
  echo "修复建议: bash env/keship-env.sh repair"
  return 1
}

# ---------- install / repair ----------
do_install() {
  local profile="l0"
  local a
  for a in "$@"; do
    case "$a" in
      --profile=*) profile="${a#*=}" ;;
      --mirror=*)  MIRROR="${a#*=}" ;;
    esac
  done

  echo "KeShip 环境安装"
  echo "============================================"
  echo "profile: ${profile}    mirror: ${MIRROR}"
  echo ""

  echo "[1/5] 配置 apt 源"
  ensure_ros_source  || { echo "  [失败] ROS 源配置"; return 1; }
  ensure_osrf_source || { echo "  [失败] OSRF 源配置"; return 1; }
  echo ""

  echo "[2/5] 扫描缺失包"
  local pkgs
  pkgs="$(missing_pkgs)"
  if [ -z "$pkgs" ]; then
    echo "  全部已满足，无需安装"
    echo "  ⇒ 跳过 apt update / apt install（幂等零动作，不需要 sudo）"
    echo ""
    echo "[5/5] 复检"
    echo "--------------------------------------------"
    do_check
    return $?
  fi
  echo "  待安装:${pkgs}"
  echo ""

  echo "[3/5] apt update"
  if ! $SUDO apt-get update -qq; then
    echo "  [失败] apt update"
    return 1
  fi
  echo "  [OK]"
  echo ""

  echo "[4/5] apt install（下载量大时可能较久）"
  if ! $SUDO apt-get install -y $pkgs; then
    echo "  [失败] apt install"
    return 1
  fi
  echo "  [OK]"
  echo ""

  if [ "$profile" = "full" ]; then
    echo "[L1] 自建 gz-sim（含 S1 patch）部署"
    local here
    here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ -f "$here/gz/deploy_gz.sh" ]; then
      if [ "$(id -u)" -eq 0 ]; then
        bash "$here/gz/deploy_gz.sh" \
          || echo "  [警告] L1 部署失败 —— 可稍后手动: sudo bash env/gz/deploy_gz.sh"
      else
        # 非 root：免 sudo 装用户前缀（start_keship.sh 已能自动探测 $HOME/keship-gz）
        echo "  [L1] 非 root ⇒ 安装到用户前缀 \$HOME/keship-gz（免 sudo）"
        bash "$here/gz/deploy_gz.sh" --prefix="$HOME/keship-gz" \
          || echo "  [警告] L1 部署失败 —— 可稍后手动: bash env/gz/deploy_gz.sh --prefix=\$HOME/keship-gz"
      fi
    else
      echo "  [警告] 找不到 $here/gz/deploy_gz.sh"
    fi
    echo ""
  fi

  echo "[5/5] 复检"
  echo "--------------------------------------------"
  do_check
}

# ---------- 入口 ----------
CMD="${1:-}"
shift 2>/dev/null || true

case "$CMD" in
  check)
    do_check
    ;;
  install|repair)
    do_install "$@"
    ;;
  *)
    echo "用法: bash env/keship-env.sh <命令> [选项]"
    echo ""
    echo "命令:"
    echo "  check                    只读自检，输出诊断报告"
    echo "  install [--profile=…]    安装缺失项（l0 | full，默认 l0）"
    echo "  repair [--profile=…]     只补缺失项（幂等）"
    echo ""
    echo "选项:"
    echo "  --mirror=tuna|official   apt 镜像（默认 tuna）"
    exit 2
    ;;
esac
