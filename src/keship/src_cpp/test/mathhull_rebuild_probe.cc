/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 回归探针：数学船型"生成 → 改惯性（模拟属性对话框改质量）→ RebuildModelWithAllPlugins"
 * 期望：重建后仍浮在水面（原缺陷 2026-10-25：重建丢掉网格浮力插件 ⇒ 自由落体）
 *
 * 需要一套运行中的世界（本探针**不**起仿真）：
 *   GZ_PARTITION=... ROS_DOMAIN_ID=... DISPLAY=... ./mathhull_rebuild_probe
 * 成功后退出码 0；失败 1。
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "ModelManager.hh"
#include "ShipHullGenerator.hh"

int main()
{
  keship::ModelManager mm("keship_world");

  const std::string name = "RbProbe";
  keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0, 500.0);
  double pos[6] = {0, 0, 0.6, 0, 0, 0};

  keship::MathHullPluginOpts opts;
  opts.enableHydro = true;
  keship::HydroConfig hc = keship::ModelManager::EstimateMathHullHydro(spec, false);
  opts.hydro = &hc;

  if (!mm.SpawnMathHull(name, spec, pos, opts))
  {
    std::printf("FAIL  生成失败\n");
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::seconds(8));

  auto getZ = [&](double &_z) {
    uint32_t id = 0;
    if (!mm.GetModelIDByInstanceNameFromGazebo(name, id)) return false;
    double p[6];
    if (!mm.GetModelPoseByIDFromGazebo(id, p)) return false;
    _z = p[2];
    return true;
  };

  double z0 = 0;
  if (!getZ(z0))
  {
    std::printf("FAIL  读不到初始位姿\n");
    return 1;
  }
  std::printf("  重建前 z=%.4f\n", z0);

  keship::InertiaParams ip;
  if (!mm.GetCachedModelInertia(name, ip))
  {
    std::printf("FAIL  读不到缓存惯性\n");
    return 1;
  }
  std::printf("  改质量: %.1f → %.1f kg\n", ip.mass, ip.mass + 1.0);
  ip.mass += 1.0;   ///< 复刻"属性对话框里 +1 kg"
  mm.UpdateCachedSDFInertia(name, ip);

  if (!mm.RebuildModelWithAllPlugins(name))
  {
    std::printf("FAIL  重建失败\n");
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::seconds(3));
  double z1 = 0, z2 = 0;
  getZ(z1);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  getZ(z2);
  std::printf("  重建后 z: %.4f → %.4f\n", z1, z2);

  const bool floats = std::fabs(z2) < 1.0;
  const bool stable = std::fabs(z2 - z1) < 0.05;
  std::printf("%s  重建后仍浮稳（z=%.4f, |Δz|=%.4f）\n",
              (floats && stable) ? "PASS" : "FAIL", z2, std::fabs(z2 - z1));
  return (floats && stable) ? 0 : 1;
}
