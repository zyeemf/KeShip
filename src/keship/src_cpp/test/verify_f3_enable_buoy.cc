/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * F3 验证：确认 `MakeBuoyancyPluginSDF` 生成的 SDF 里 <enable_buoyancy> 的口径
 *   期望：bodies 为空 ⇒ false（enable_hydro 仍按需）；bodies 非空 ⇒ true
 *   2026-10-24：原为只打印的探针（全文唯一 return 0，恒 PASS），现补断言使其成为真回归用例。
 */
#include <cstdio>
#include <string>

#include "ModelManager.hh"

static int g_fails = 0;

static void check(const char *_tag, const keship::BuoyancyConfig &_cfg,
                  const keship::HydroConfig *_hydro,
                  bool _expBuoy, bool _expHydro)
{
  const std::string sdf = keship::ModelManager::MakeBuoyancyPluginSDF(_cfg, _hydro);
  auto find = [&](const std::string &_k) -> std::string {
    auto p = sdf.find(_k);
    if (p == std::string::npos) return "（缺）";
    auto q = sdf.find("</", p);
    return sdf.substr(p + _k.size(), q - (p + _k.size()));
  };
  const std::string sBuoy = find("<enable_buoyancy>");
  const std::string sHydro = find("<enable_hydro>");
  const std::string eBuoy = _expBuoy ? "true" : "false";
  const std::string eHydro = _expHydro ? "true" : "false";
  // 严格比对字面值：「元素缺失」不等于 false，必须报错
  const bool ok = (sBuoy == eBuoy) && (sHydro == eHydro);
  if (!ok) ++g_fails;
  printf("[%s] %-32s enable_buoyancy=%-6s(期望 %-5s) enable_hydro=%-6s(期望 %s)\n",
         ok ? "PASS" : "FAIL", _tag, sBuoy.c_str(), eBuoy.c_str(),
         sHydro.c_str(), eHydro.c_str());
}

int main()
{
  keship::HydroConfig hydro;
  hydro.linkName = "base_link";

  // ① bodies 为空、无 hydro（典型：只挂插件但没配浮力体）
  { keship::BuoyancyConfig c; check("① bodies 空 / 无 hydro", c, nullptr, false, false); }
  // ② bodies 为空、有 hydro（F3 关键场景：只开水动力，不得因此开浮力）
  { keship::BuoyancyConfig c; check("② bodies 空 / 有 hydro ★关键", c, &hydro, false, true); }
  // ③④ bodies 非空（正常浮力）
  {
    keship::BuoyancyConfig c;
    keship::BuoyancyBodyConfig b;
    b.name = "body_0";
    b.type = keship::PrimitiveType::Cylinder;
    b.cyl.radius = 0.26; b.cyl.length = 2.6;
    c.bodies.push_back(b);
    check("③ bodies 非空 / 无 hydro", c, nullptr, true, false);
    check("④ bodies 非空 / 有 hydro", c, &hydro, true, true);
  }

  printf("F3_ENABLE_BUOY %s (fails=%d)\n", g_fails ? "FAIL" : "PASS", g_fails);
  return g_fails ? 1 : 0;
}
