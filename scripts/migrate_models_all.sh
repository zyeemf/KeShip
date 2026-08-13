#!/bin/bash
# KeShip 模型目录重构——一次性迁移脚本（复制留存版，原目录只读）
# v2：替换串含完整前缀（file:// + keship_ws 整段），避免中段子串导致路径断裂
# 用法：bash /mnt/e/KeShip/ks_workspace/program/plans/migrate_models_all.sh
set -e
cd ~/keship_ws || exit 1

echo "[1/5] 重建新目录结构（清除上次损坏复制品）"
rm -rf models_all
mkdir -p models_all/ks_model models_all/robot_model models_all/ks_world

echo "[2/5] 复制原目录（保留原样）"
cp -r models_ksm/*   models_all/ks_model/
cp -r models_robot/* models_all/robot_model/
cp -r models_env/*   models_all/robot_model/
cp -r models_ksw/*   models_all/ks_world/

echo "[3/5] 重写 .ksm 内 mesh 引用为相对路径（完整前缀替换）"
find models_all -name '*.ksm' -exec sed -i \
  -e 's|file:///home/keship/keship_ws/models_robot/|robot_model/|g' \
  -e 's|file:///home/keship/keship_ws/models/yanhu/mesh/yanhu.dae|robot_model/yanhu/yanhu.dae|g' \
  -e 's|file:///home/keship/keship_ws/data/|data/|g' \
  -e 's|file:///home/keship/.keship/models/|data/models/|g' \
  -e 's|/home/keship/keship_ws/models_robot/|robot_model/|g' \
  -e 's|/home/keship/.keship/models/|data/models/|g' {} +

echo "[4/5] 顺带修复：6 个清单文件 keship_world.ksw -> keship_world.kswf"
find models_all/ks_world -type f -name 'keship_world.ksw' \
  -exec sh -c 'mv "$1" "$(dirname "$1")/keship_world.kswf"' _ {} \;

echo "[5/5] 自检：残留旧前缀计数（应为 0）"
grep -rc "keship_ws/models_robot\|home/keship/.keship/" models_all/ \
  | grep -v ":0" || echo "无残留 ✓"

echo "=== 迁移完成 ==="