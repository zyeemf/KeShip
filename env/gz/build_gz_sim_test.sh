#!/usr/bin/env bash
# ============================================================
# 试构建 gz-sim 8.11.0（**不改任何代码**）
#   目的：验证 ① 能构建成功 ② 产物可被 KeShip 加载
#   安装到用户前缀，不动系统 / 不动 ROS vendor
# ============================================================
set +u
source /opt/ros/jazzy/setup.bash
set -u

SRC=/home/keship/gz-build/gz-sim-8.11.0
BUILD=/home/keship/gz-build/build
PREFIX=/home/keship/gz-build/install

mkdir -p "$BUILD"
cd "$BUILD" || exit 1

echo "############ [1] cmake configure ############"
cmake "$SRC" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  > /home/keship/gz-build/cmake.log 2>&1
RC=$?
echo "  cmake rc=$RC"
if [ $RC -ne 0 ]; then
  echo "--- cmake 日志尾部 ---"
  tail -40 /home/keship/gz-build/cmake.log
  exit 1
fi
echo "  [OK] configure 通过"
grep -iE "found|not found|missing" /home/keship/gz-build/cmake.log | head -10

echo
echo "############ [2] build（-j32）############"
START=$(date +%s)
make -j32 > /home/keship/gz-build/make.log 2>&1
RC=$?
END=$(date +%s)
echo "  make rc=$RC  耗时 $((END-START)) s"
if [ $RC -ne 0 ]; then
  echo "--- make 错误 ---"
  grep -iE "error" /home/keship/gz-build/make.log | head -20
  tail -30 /home/keship/gz-build/make.log
  exit 1
fi
echo "  [OK] build 通过"
tail -3 /home/keship/gz-build/make.log

echo
echo "############ [3] install ############"
make install > /home/keship/gz-build/install.log 2>&1
echo "  install rc=$?"
echo "--- 产物 ---"
ls -l "$PREFIX/lib/" 2>/dev/null | grep -E "libgz-sim8" | head -10
echo "--- 版本确认 ---"
strings "$PREFIX/lib/libgz-sim8.so.8.11.0" 2>/dev/null | grep -m2 -E "^8\.11\.0|GZ_SIM_VERSION" | head -3
echo
echo "############ 完成 ############"
