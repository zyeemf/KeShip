/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SDF 组装纯函数单测（2026-10-25）
 *
 * 覆盖的组装点：
 *   1) MakeMathHullSDF        —— 数学船型 SDF（质量/惯性/浮力网格/水动力标签/附加质量）
 *   2) ComposeRebuiltSDF      —— 重建 SDF（插件不丢失不变量、配置重拼）
 *   3) MakeBuoyancyPluginSDF  —— 浮力插件段（空体/有体/带水动力）
 *   4) InjectFluidAddedMass   —— 附加质量注入（零矩阵跳过）
 *   5) EstimateMathHullHydro  —— 默认水动力系数（轻量/完整）
 *
 * 全部是纯函数：不连仿真、不写文件，秒级完成，进 ctest -L regression。
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "ModelManager.hh"
#include "ShipHullGenerator.hh"

static int g_fail = 0;

static void check(bool _ok, const char *_what)
{
  std::printf("[%s] %s\n", _ok ? "PASS" : "FAIL", _what);
  if (!_ok) ++g_fail;
}

static bool has(const std::string &_s, const std::string &_sub)
{
  return _s.find(_sub) != std::string::npos;
}

int main()
{
  const keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0, 500.0);
  const std::string vis = "/tmp/keship_test/HullA_vis.obj";
  const std::string buoy = "/tmp/keship_test/HullA_buoy.obj";

  // ---------------- 1) MakeMathHullSDF ----------------
  {
    keship::MathHullPluginOpts opts;   // 全默认 = 旧行为
    const std::string sdf = keship::ModelManager::MakeMathHullSDF("HullA", spec, opts, vis, buoy);
    check(has(sdf, "<model name=\"HullA\">"), "数学船型 SDF：模型名");
    check(has(sdf, "libKSPlugin_HydroBuoyancy.so"), "数学船型 SDF：浮力插件");
    check(has(sdf, "<enable_hydro>false</enable_hydro>"), "默认不启用 fossen 阻尼");
    check(has(sdf, "<buoyancy name=\"hull\">"), "数学船型 SDF：网格浮力体");
    check(has(sdf, buoy), "数学船型 SDF：浮力网格路径");
    check(has(sdf, "file://" + vis), "数学船型 SDF：可视网格路径");
    check(has(sdf, "1998.4"), "质量 = rho × 体积（≈1998 kg）");
    check(!has(sdf, "<linear_damping>"), "默认不写 linear_damping");
    check(!has(sdf, "<fluid_added_mass>"), "默认不注入附加质量");
  }

  // 轻量档水动力
  const keship::HydroConfig light =
      keship::ModelManager::EstimateMathHullHydro(spec, false);
  {
    keship::MathHullPluginOpts opts;
    opts.enableHydro = true;
    opts.hydro = &light;
    const std::string sdf = keship::ModelManager::MakeMathHullSDF("HullB", spec, opts, vis, buoy);
    check(has(sdf, "<enable_hydro>true</enable_hydro>"), "轻量档：enable_hydro=true");
    const char *names[8] = {"Xu", "Yv", "Zw", "Kp", "Mq", "Nr", "Yr", "Nv"};
    bool all = true;
    for (auto *n : names)
      if (!has(sdf, std::string("<") + n + ">")) all = false;
    check(all, "轻量档：8 项 <linear_damping> 齐全");
    check(!has(sdf, "<damping>"), "轻量档：不写二次项");
    check(!has(sdf, "<fluid_added_mass>"), "轻量档：不注入附加质量");
  }

  // 完整档水动力
  const keship::HydroConfig full =
      keship::ModelManager::EstimateMathHullHydro(spec, true);
  {
    keship::MathHullPluginOpts opts;
    opts.enableHydro = true;
    opts.hydro = &full;
    const std::string sdf = keship::ModelManager::MakeMathHullSDF("HullC", spec, opts, vis, buoy);
    check(has(sdf, "<damping>") && has(sdf, "<term target="), "完整档：写入二次阻尼项");
    check(has(sdf, "<fluid_added_mass>"), "完整档：注入附加质量");
  }

  // 浸水体积发布
  {
    keship::MathHullPluginOpts opts;
    opts.publishSubVolume = true;
    const std::string sdf = keship::ModelManager::MakeMathHullSDF("HullD", spec, opts, vis, buoy);
    check(has(sdf, "<publish_subvolume>true</publish_subvolume>"),
          "publish_subvolume 开关生效");
  }

  // ---------------- 2) ComposeRebuiltSDF ----------------
  {
    // 自包含插件（数学船型）：重建应原样返回，插件一个不丢
    keship::MathHullPluginOpts opts;
    const std::string base = keship::ModelManager::MakeMathHullSDF("HullE", spec, opts, vis, buoy);
    const keship::SensorConfig sc;
    const keship::ThrusterConfig tc;
    const keship::BuoyancyConfig bc;
    const keship::HydroConfig hc;
    const std::string out = keship::ModelManager::ComposeRebuiltSDF(
        base, "HullE", "keship_world", false, sc, false, tc, false, hc, false, bc);
    check(out == base, "重建：自包含插件模型整段重放（内容不变）");
  }
  {
    // 普通网格模型 + 浮力配置：重建应重新拼出浮力插件
    const std::string base =
        "<?xml version=\"1.0\"?>\n<sdf version=\"1.10\">\n  <model name=\"M1\">\n"
        "    <link name=\"base_link\"><inertial><mass>10</mass></inertial></link>\n"
        "  </model>\n</sdf>\n";
    keship::BuoyancyConfig bc;
    keship::BuoyancyBodyConfig body;
    body.name = "hull";
    body.type = keship::PrimitiveType::Box;
    body.box.dx = 2.0; body.box.dy = 1.0; body.box.dz = 0.5;
    bc.bodies.push_back(body);
    const keship::SensorConfig sc;
    const keship::ThrusterConfig tc;
    const keship::HydroConfig hc = light;
    const std::string out = keship::ModelManager::ComposeRebuiltSDF(
        base, "M1", "keship_world", false, sc, false, tc, true, hc, true, bc);
    check(has(out, "libKSPlugin_HydroBuoyancy.so"), "重建：按配置拼出浮力插件");
    check(has(out, "<enable_buoyancy>true</enable_buoyancy>"), "重建：有浮力体时启用浮力");
    check(has(out, "<box><size>2 1 0.5</size></box>"), "重建：浮力体几何写入");
    check(has(out, "<linear_damping>"), "重建：水动力标签写入");
  }
  {
    // 未知插件也必须保住（通用不变量，不只 HydroBuoyancy）
    const std::string base =
        "<?xml version=\"1.0\"?>\n<sdf version=\"1.10\">\n  <model name=\"M2\">\n"
        "    <link name=\"base_link\"></link>\n"
        "    <plugin filename=\"libSomethingNew.so\" name=\"x\"/>\n"
        "  </model>\n</sdf>\n";
    const keship::SensorConfig sc;
    const keship::ThrusterConfig tc;
    const keship::HydroConfig hc;
    const keship::BuoyancyConfig bc;
    const std::string out = keship::ModelManager::ComposeRebuiltSDF(
        base, "M2", "keship_world", false, sc, false, tc, false, hc, false, bc);
    check(out == base, "重建：未知插件不被丢弃（通用不变量）");
  }

  // ---------------- 3) MakeBuoyancyPluginSDF ----------------
  {
    keship::BuoyancyConfig empty;
    const std::string seg = keship::ModelManager::MakeBuoyancyPluginSDF(empty);
    check(has(seg, "<enable_buoyancy>false</enable_buoyancy>"),
          "浮力段：无浮力体时不启用（避免整船网格浮力挂死）");
  }
  {
    keship::BuoyancyConfig bc;
    keship::BuoyancyBodyConfig body;
    body.name = "b1";
    body.type = keship::PrimitiveType::Cylinder;
    body.cyl.radius = 0.5; body.cyl.length = 2.0;
    bc.bodies.push_back(body);
    const std::string seg = keship::ModelManager::MakeBuoyancyPluginSDF(bc, &light);
    check(has(seg, "<enable_buoyancy>true</enable_buoyancy>"), "浮力段：有浮力体时启用");
    check(has(seg, "<cylinder><radius>0.5</radius><length>2</length></cylinder>"),
          "浮力段：圆柱几何写入");
    check(has(seg, "<enable_hydro>true</enable_hydro>"), "浮力段：带水动力时启用");
  }

  // ---------------- 4) InjectFluidAddedMass ----------------
  {
    std::string zero = "<inertial><mass>1</mass></inertial>";
    double zeros[36] = {0};
    keship::ModelManager::InjectFluidAddedMass(zero, zeros);
    check(!has(zero, "<fluid_added_mass>"), "附加质量：零矩阵不注入");

    std::string some = "<inertial><mass>1</mass></inertial>";
    double am[36] = {0};
    am[0] = 100.0; am[7] = 200.0;
    keship::ModelManager::InjectFluidAddedMass(some, am);
    check(has(some, "<fluid_added_mass>"), "附加质量：非零矩阵注入");
  }

  // ---------------- 5) EstimateMathHullHydro ----------------
  {
    check(light.dampingTerms.empty(), "估算：轻量档无二次项");
    check(std::fabs(light.damping[2]) > 1.0, "估算：Zw 非零");
    check(light.addedMass[0] == 0.0, "估算：轻量档无附加质量");
    check(full.dampingTerms.size() == 6, "估算：完整档 6 条二次项");
    check(full.addedMass[0] > 0.0, "估算：完整档附加质量非零");
  }

  std::printf("RESULT fail=%d\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
