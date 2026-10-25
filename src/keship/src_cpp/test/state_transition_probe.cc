/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 状态迁移探针（2026-10-25）：对 6 类"可编辑属性"做 create → mutate → assert
 *
 * 判定（对应 state_matrix 的 I1~I4）：
 *   I1 修改后仍在场景（inScene）
 *   I2 核心行为未坏（浮体仍浮稳）
 *   I4 修改值读回一致（位姿）
 *   I3 插件不缩水由 ComposeRebuiltSDF 不变量保证（单测覆盖），这里看重建是否成功
 *
 * 需要运行中的世界（同 mathhull_rebuild_probe）。
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "ModelManager.hh"
#include "ShipHullGenerator.hh"

static int g_fail = 0;
static keship::ModelManager *g_mm = nullptr;

static void check(bool _ok, const char *_what)
{
  std::printf("[%s] %s\n", _ok ? "PASS" : "FAIL", _what);
  if (!_ok) ++g_fail;
}

static bool inScene(const std::string &_n)
{
  uint32_t id = 0;
  return g_mm->GetModelIDByInstanceNameFromGazebo(_n, id);
}

static bool poseOf(const std::string &_n, double _p[6])
{
  uint32_t id = 0;
  if (!g_mm->GetModelIDByInstanceNameFromGazebo(_n, id)) return false;
  return g_mm->GetModelPoseByIDFromGazebo(id, _p);
}

static bool spawnAt(const std::string &_n, double _x)
{
  keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0, 500.0);
  double pos[6] = {_x, 0, 0.6, 0, 0, 0};
  keship::MathHullPluginOpts opts;
  opts.enableHydro = true;
  static keship::HydroConfig hc = keship::ModelManager::EstimateMathHullHydro(spec, false);
  opts.hydro = &hc;
  return g_mm->SpawnMathHull(_n, spec, pos, opts);
}

static bool floats(const std::string &_n)
{
  double p1[6], p2[6];
  if (!poseOf(_n, p1)) return false;
  std::this_thread::sleep_for(std::chrono::seconds(2));
  if (!poseOf(_n, p2)) return false;
  return std::fabs(p2[2]) < 1.0 && std::fabs(p2[2] - p1[2]) < 0.05;
}

int main()
{
  keship::ModelManager mm("keship_world");
  g_mm = &mm;

  // 1) 位姿
  {
    if (!spawnAt("StPose", 0)) { std::printf("FAIL  位姿：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    uint32_t id = 0;
    mm.GetModelIDByInstanceNameFromGazebo("StPose", id);
    double np[6] = {3.0, -2.0, 0.8, 0, 0, 0.7854};
    bool ok = mm.SetModelPoseByIDInGazebo(id, np);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    double rp[6] = {0};
    poseOf("StPose", rp);
    check(ok && std::fabs(rp[0] - 3.0) < 0.2 && std::fabs(rp[1] + 2.0) < 0.2,
          "位姿修改：读回一致");
    check(inScene("StPose"), "位姿修改：仍在场景");
  }

  // 2) 浮力体（盒体替换网格浮力）
  {
    if (!spawnAt("StBuoy", 10)) { std::printf("FAIL  浮力体：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    keship::BuoyancyConfig bc;
    keship::BuoyancyBodyConfig b;
    b.name = "hull"; b.type = keship::PrimitiveType::Box;
    b.box.dx = 6.0; b.box.dy = 1.5; b.box.dz = 0.5;
    bc.bodies.push_back(b);
    bool ok = mm.RebuildModelWithBuoyancy("StBuoy", bc);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    check(ok && inScene("StBuoy"), "浮力体修改：重建成功且在场景");
    check(floats("StBuoy"), "浮力体修改：仍浮稳");
  }

  // 3) 水动力
  {
    if (!spawnAt("StHydro", 20)) { std::printf("FAIL  水动力：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0, 500.0);
    keship::HydroConfig hc = keship::ModelManager::EstimateMathHullHydro(spec, true);
    bool ok = mm.RebuildModelWithHydro("StHydro", hc);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    check(ok && inScene("StHydro"), "水动力修改：重建成功且在场景");
    check(floats("StHydro"), "水动力修改：仍浮稳");
  }

  // 4) 推进器
  {
    if (!spawnAt("StThrust", 30)) { std::printf("FAIL  推进器：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    keship::ThrusterConfig tc;
    keship::ThrusterBodyConfig t;
    t.name = "t1"; t.type = keship::ThrusterType::Fixed; t.maxForce = 100.0;
    tc.bodies.push_back(t);
    bool ok = mm.RebuildModelWithThrust("StThrust", tc);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    check(ok && inScene("StThrust"), "推进器修改：重建成功且在场景");
    check(floats("StThrust"), "推进器修改：仍浮稳");
  }

  // 5) 传感器
  {
    if (!spawnAt("StSensor", 40)) { std::printf("FAIL  传感器：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    keship::SensorConfig sc;
    keship::SensorInstance si;
    si.type = keship::SensorType::IMU;
    si.name = "imu1";
    sc.instances.push_back(si);
    bool ok = mm.RebuildModelWithSensors("StSensor", sc);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    check(ok && inScene("StSensor"), "传感器修改：重建成功且在场景");
    check(floats("StSensor"), "传感器修改：仍浮稳");
  }

  // 6) 碰撞方式
  {
    if (!spawnAt("StColl", 50)) { std::printf("FAIL  碰撞：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    mm.SetCollisionMode("StColl", keship::CollisionMode::ConvexHull);
    bool ok = mm.RebuildModelWithAllPlugins("StColl");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    check(ok && inScene("StColl"), "碰撞方式修改：重建成功且在场景");
    check(floats("StColl"), "碰撞方式修改：仍浮稳");
  }

  // 7) 静止/动态
  {
    if (!spawnAt("StStatic", 60)) { std::printf("FAIL  静止/动态：生成失败\n"); return 1; }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    bool ok = mm.SetModelStatic("StStatic", true);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    bool isStatic = false;
    mm.GetCachedModelStatic("StStatic", isStatic);
    check(ok && isStatic && inScene("StStatic"), "静止切换：已成静态且在场景");

    bool back = mm.SetModelStatic("StStatic", false);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    mm.GetCachedModelStatic("StStatic", isStatic);
    check(back && !isStatic && inScene("StStatic"), "动态切换：已恢复且在场景");
  }

  std::printf("RESULT fail=%d\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
