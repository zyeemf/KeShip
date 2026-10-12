#!/usr/bin/env bash
# 排查：-Dgz-sim8_DIR 是否真的改变了 find_package(gz-sim8) 的结果
echo "=== A 日志里 gz-sim8 相关 ==="
grep -aiE "gz-sim8|gzs_incs|Found gz" /tmp/build_A.log | head -20

echo
echo "=== vendor 是否也有 include 目录 ==="
ls -d /opt/ros/jazzy/opt/gz_sim_vendor/include/gz/sim8 2>/dev/null && echo "  [有]" || echo "  [无]"
ls -l /opt/ros/jazzy/opt/gz_sim_vendor/include/gz/sim8/gz/sim/Link.hh 2>/dev/null
printf '  vendor Link.hh 含 SetFluidAddedMass: '
grep -c "SetFluidAddedMass" /opt/ros/jazzy/opt/gz_sim_vendor/include/gz/sim8/gz/sim/Link.hh 2>/dev/null

echo
echo "=== 自建 Link.hh 含 SetFluidAddedMass ==="
printf '  '
grep -c "SetFluidAddedMass" /home/keship/gz-build/install/include/gz/sim8/gz/sim/Link.hh

echo
echo "=== gz_sim_vendor 的 CMake config 路径 ==="
find /opt/ros/jazzy/opt/gz_sim_vendor -name "gz-sim8-config.cmake" 2>/dev/null | head -3
echo "=== 自建的 ==="
find /home/keship/gz-build/install -name "gz-sim8-config.cmake" 2>/dev/null | head -3
