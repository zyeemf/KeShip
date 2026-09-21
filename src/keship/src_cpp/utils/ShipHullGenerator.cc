/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型生成器 v2 实现 — Gazebo Z_UP 直接生成 */

#include "ShipHullGenerator.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace keship {

// ═══════════════════════════════════════════════════════════
// ShipHullSpec 工厂方法
// ═══════════════════════════════════════════════════════════

ShipHullSpec ShipHullSpec::WigleyClassic(double L, double B, double D, double rho) {
  ShipHullSpec s; s.L=L; s.B=B; s.D=D; s.rho=rho;
  s.wlType=WaterlineType::PARABOLIC; s.wlExponent=2.0;
  s.secType=SectionType::PARABOLIC; s.secExponent=2.0;
  return s;
}
ShipHullSpec ShipHullSpec::WigleyFlat(double L, double B, double D, double rho) {
  ShipHullSpec s; s.L=L; s.B=B; s.D=D; s.rho=rho;
  s.wlType=WaterlineType::PARABOLIC; s.wlExponent=2.0;
  s.secType=SectionType::PARABOLIC; s.secExponent=4.0;
  return s;
}
ShipHullSpec ShipHullSpec::WigleySharp(double L, double B, double D, double rho) {
  ShipHullSpec s; s.L=L; s.B=B; s.D=D; s.rho=rho;
  s.wlType=WaterlineType::PARABOLIC; s.wlExponent=2.0;
  s.secType=SectionType::PARABOLIC; s.secExponent=1.5;
  return s;
}
ShipHullSpec ShipHullSpec::WigleyFine(double L, double B, double D, double rho) {
  ShipHullSpec s; s.L=L; s.B=B; s.D=D; s.rho=rho;
  s.wlType=WaterlineType::PARABOLIC; s.wlExponent=3.0;
  s.secType=SectionType::PARABOLIC; s.secExponent=2.0;
  return s;
}
ShipHullSpec ShipHullSpec::LewisU(double L, double B, double D, double rho) {
  ShipHullSpec s; s.L=L; s.B=B; s.D=D; s.rho=rho;
  s.wlType=WaterlineType::PARABOLIC; s.wlExponent=2.0;
  s.secType=SectionType::LEWIS_U;
  return s;
}
ShipHullSpec ShipHullSpec::LewisV(double L, double B, double D, double rho) {
  ShipHullSpec s; s.L=L; s.B=B; s.D=D; s.rho=rho;
  s.wlType=WaterlineType::PARABOLIC; s.wlExponent=2.0;
  s.secType=SectionType::LEWIS_V;
  return s;
}

// ═══════════════════════════════════════════════════════════
// 曲面形状函数
// ═══════════════════════════════════════════════════════════

double ShipHullGenerator::waterlineShape(double _xi) const {
  double t = std::clamp(std::abs(_xi), 0.0, 1.0);
  return 1.0 - std::pow(t, spec_.wlExponent);
}

double ShipHullGenerator::sectionShape(double _zeta) const {
  double t = std::clamp(_zeta, 0.0, 1.0);
  switch (spec_.secType) {
    case SectionType::PARABOLIC:
      return 1.0 - std::pow(t, spec_.secExponent);
    case SectionType::LEWIS_U:
      return std::sqrt(std::max(0.0, 1.0 - t * t));
    case SectionType::LEWIS_V:
      return 1.0 - t;
  }
  return 1.0;
}

double ShipHullGenerator::halfBreadthFunc(double _x, double _z) const {
  double xi = 2.0 * (_x / spec_.L) - 1.0;  // ∈ [-1, 1]
  double wl = waterlineShape(xi);
  if (wl <= 0.0) return 0.0;
  // z ∈ [-D, 0] → zeta = -z/D ∈ [0, 1]
  double zeta = -_z / spec_.D;
  return spec_.B * 0.5 * wl * sectionShape(zeta);
}

// ═══════════════════════════════════════════════════════════
// 构造
// ═══════════════════════════════════════════════════════════

ShipHullGenerator::ShipHullGenerator(const ShipHullSpec &_spec)
    : spec_(_spec) {
  generate();
  computeHydroProps();
  computeVolumeCurve();
  computeLinesData();
  verifyMesh(buoyMesh_);
}

// ═══════════════════════════════════════════════════════════
// 网格生成 — Gazebo Z_UP 直接生成，无坐标变换
// ═══════════════════════════════════════════════════════════

void ShipHullGenerator::generate() {
  generateMesh(spec_.visNx,  spec_.visNz,  visMesh_);
  generateMesh(spec_.buoyNx, spec_.buoyNz, buoyMesh_);

  // Catto totalVolume: Σ |V_k| (散度定理，符号已由法线方向保证)
  // 仅在浮力网格上计算
  cattoVolume_ = 0.0;
  for (const auto &tri : buoyMesh_.triangles) {
    // 三角形中心
    gz::math::Vector3d c = (tri.v0 + tri.v1 + tri.v2) / 3.0;
    // 法线 × 面积 / 2
    gz::math::Vector3d e1 = tri.v1 - tri.v0;
    gz::math::Vector3d e2 = tri.v2 - tri.v0;
    gz::math::Vector3d n = e1.Cross(e2);  // 未归一化，|n| = 2*area
    double vk = n.Dot(c) / 6.0;           // V_k = A*(n̂·c)/3, n=n̂*2A → V_k = n·c/6
    cattoVolume_ += vk;                   // 带符号累加，法线朝外则封闭体 V>0
  }
  cattoVolume_ = std::abs(cattoVolume_);
}

void ShipHullGenerator::generateMesh(int _Nx, int _Nz,
                                      MeshDataSimple &_mesh) {
  _mesh.vertices.clear();
  _mesh.triangles.clear();
  _mesh.normals.clear();

  double L = spec_.L, B = spec_.B, D = spec_.D;
  double dx = L / _Nx;
  double dz = D / _Nz;  // 正数

  // ============================================================
  // Step 1: 顶点生成 — Gazebo Z_UP
  //   vs(i,j) = 右舷, vp(i,j) = 左舷
  //   z_j = -j*dz ∈ [-D, 0]  (水线=0, 龙骨=-D)
  //   zeta_j = -z_j/D = j/_Nz ∈ [0, 1]
  // ============================================================
  for (int i = 0; i <= _Nx; ++i) {
    double x = i * dx;
    for (int j = 0; j <= _Nz; ++j) {
      double z = -j * dz;           // Gazebo Z_UP
      double halfY = halfBreadthFunc(x, z);
      _mesh.vertices.push_back({x,  halfY, z});  // 右舷
      _mesh.vertices.push_back({x, -halfY, z});  // 左舷
    }
  }

  int stride = _Nz + 1;  // 每个"半边"的顶点数

  // index helper: vs(i,j) = 2 * (i*stride + j)
  //               vp(i,j) = 2 * (i*stride + j) + 1
  auto vs = [stride](int i, int j) -> int { return 2 * (i * stride + j); };
  auto vp = [stride](int i, int j) -> int { return 2 * (i * stride + j) + 1; };

  // ============================================================
  // Step 2: 三角形生成 — 严格推导的绕序
  //
  // 右舷 (法线朝 +y):
  //   T1 = (vs_{i,j}, vs_{i+1,j}, vs_{i,j+1})
  //   T2 = (vs_{i,j+1}, vs_{i+1,j}, vs_{i+1,j+1})
  //
  // 左舷 (法线朝 -y):
  //   T1 = (vp_{i,j}, vp_{i,j+1}, vp_{i+1,j})
  //   T2 = (vp_{i,j+1}, vp_{i+1,j+1}, vp_{i+1,j})
  // ============================================================
  for (int i = 0; i < _Nx; ++i) {
    for (int j = 0; j < _Nz; ++j) {
      // 右舷
      _mesh.triangles.push_back({
        _mesh.vertices[vs(i,j)], _mesh.vertices[vs(i+1,j)], _mesh.vertices[vs(i,j+1)]
      });
      _mesh.triangles.push_back({
        _mesh.vertices[vs(i,j+1)], _mesh.vertices[vs(i+1,j)], _mesh.vertices[vs(i+1,j+1)]
      });
      // 左舷
      _mesh.triangles.push_back({
        _mesh.vertices[vp(i,j)], _mesh.vertices[vp(i,j+1)], _mesh.vertices[vp(i+1,j)]
      });
      _mesh.triangles.push_back({
        _mesh.vertices[vp(i,j+1)], _mesh.vertices[vp(i+1,j+1)], _mesh.vertices[vp(i+1,j)]
      });
    }
  }

  // ============================================================
  // Step 3: 甲板封盖 (z=0)
  //   D1 = (vs_{i,0}, vp_{i,0}, vs_{i+1,0})  → n_z > 0
  //   D2 = (vp_{i,0}, vp_{i+1,0}, vs_{i+1,0}) → n_z > 0
  // ============================================================
  for (int i = 0; i < _Nx; ++i) {
    _mesh.triangles.push_back({
      _mesh.vertices[vs(i,0)], _mesh.vertices[vp(i,0)], _mesh.vertices[vs(i+1,0)]
    });
    _mesh.triangles.push_back({
      _mesh.vertices[vp(i,0)], _mesh.vertices[vp(i+1,0)], _mesh.vertices[vs(i+1,0)]
    });
  }

  // 龙骨: 尖底自然闭合 (y=0)，无需封盖

  // ============================================================
  // Step 4: 顶点法线 — 面法线面积加权平均
  // DART ODE 碰撞检测需要法线，否则加载 mesh OBJ 会 segfault
  // ============================================================
  {
    // 坐标→索引映射 (三角形存顶点副本，需坐标匹配)
    auto fmt = [](const gz::math::Vector3d &v) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%.8g,%.8g,%.8g", v.X(), v.Y(), v.Z());
      return std::string(buf);
    };
    std::map<std::string, int> coordToIdx;
    for (size_t vi = 0; vi < _mesh.vertices.size(); ++vi)
      coordToIdx[fmt(_mesh.vertices[vi])] = static_cast<int>(vi);

    _mesh.normals.assign(_mesh.vertices.size(), {0, 0, 0});
    for (const auto &tri : _mesh.triangles) {
      gz::math::Vector3d e1 = tri.v1 - tri.v0;
      gz::math::Vector3d e2 = tri.v2 - tri.v0;
      gz::math::Vector3d fn = e1.Cross(e2);  // 面法线(未归一化=面积加权)
      int i0 = coordToIdx.at(fmt(tri.v0));
      int i1 = coordToIdx.at(fmt(tri.v1));
      int i2 = coordToIdx.at(fmt(tri.v2));
      _mesh.normals[i0] = _mesh.normals[i0] + fn;
      _mesh.normals[i1] = _mesh.normals[i1] + fn;
      _mesh.normals[i2] = _mesh.normals[i2] + fn;
    }
    for (auto &n : _mesh.normals) n.Normalize();
  }
}

// ═══════════════════════════════════════════════════════════
// 网格验证
// ═══════════════════════════════════════════════════════════

void ShipHullGenerator::verifyMesh(const MeshDataSimple &_mesh) {
  // 抽样检查法线方向
  for (const auto &tri : _mesh.triangles) {
    double cy = (tri.v0.Y() + tri.v1.Y() + tri.v2.Y()) / 3.0;
    gz::math::Vector3d e1 = tri.v1 - tri.v0;
    gz::math::Vector3d e2 = tri.v2 - tri.v0;
    // cross(e1,e2).y = e1.z*e2.x - e1.x*e2.z
    double ny = e1.Z() * e2.X() - e1.X() * e2.Z();
    if (cy > 0.01 && ny < 0) {
      std::cerr << "ShipHullGenerator: WARNING starboard normal inward! "
                << "cy=" << cy << " ny=" << ny << std::endl;
    }
    if (cy < -0.01 && ny > 0) {
      std::cerr << "ShipHullGenerator: WARNING port normal inward! "
                << "cy=" << cy << " ny=" << ny << std::endl;
    }
  }

  std::cout << "ShipHullGenerator: mesh verified — "
            << _mesh.vertices.size() << " vertices, "
            << _mesh.triangles.size() << " triangles, "
            << "cattoVol=" << cattoVolume_ << " m³" << std::endl;
}

// ═══════════════════════════════════════════════════════════
// 静水力计算 — 梯形积分
// ═══════════════════════════════════════════════════════════

void ShipHullGenerator::computeHydroProps() {
  double L = spec_.L, B = spec_.B, D = spec_.D;
  double rho = spec_.rho;
  double rhoW = 1000.0;  // 淡水

  /// 设计吃水的**初值**（棱柱体近似，仅兜底）；
  /// 精确值由 computeVolumeCurve() 末尾解 V_sub(d) = (ρ/ρw)·V_total 覆盖
  /// —— 顺序原因：本函数在构造函数里先于 computeVolumeCurve() 执行。
  double d = (rho / rhoW) * D;
  hydro_.draft = d;

  // === 水线面属性 (z=0) ===
  // Aw = 2 ∫ y(x,0) dx,  Ix = 2/3 ∫ y³(x,0) dx
  {
    int Nx = std::max(spec_.visNx, 40);  // 静水力用高分辨率
    double dx = L / Nx;
    std::vector<double> yVals(Nx+1), y3Vals(Nx+1);
    for (int i = 0; i <= Nx; ++i) {
      double x = i * dx;
      double y = halfBreadthFunc(x, 0.0);
      yVals[i] = y;
      y3Vals[i] = y * y * y;
    }
    hydro_.Aw  = 2.0 * trapz(yVals,  dx);
    hydro_.Ix  = (2.0 / 3.0) * trapz(y3Vals, dx);
    hydro_.Cwp = hydro_.Aw / (L * B);
    hydro_.Ci  = hydro_.Ix / (L * B * B * B / 12.0);
  }

  // === 排水体积 & KB ===
  // ∇ = ∫ A_s(x) dx,  A_s(x) = 2 ∫ y(x,z) dz
  // KB = ∫ A_s(x)·KB_x(x) dx / ∇
  {
    int Nx = 40, Nz = 40;
    double dx = L / Nx, dz = D / Nz;
    std::vector<double> areas(Nx+1), kbX(Nx+1);
    for (int i = 0; i <= Nx; ++i) {
      double x = i * dx;
      std::vector<double> yVals(Nz+1), yzVals(Nz+1);
      for (int j = 0; j <= Nz; ++j) {
        double zAbs = j * dz;  // NED坐标中的深度
        // 在 Gazebo Z_UP 中: z = -zAbs, zeta = zAbs/D = j/Nz
        double zGz = -zAbs;
        double y = halfBreadthFunc(x, zGz);
        yVals[j]  = y;
        yzVals[j] = y * zAbs;  // 深度加权
      }
      areas[i] = 2.0 * trapz(yVals,  dz);
      double mz  = 2.0 * trapz(yzVals, dz);
      kbX[i] = (areas[i] > 1e-12) ? mz / areas[i] : 0.0;
    }
    hydro_.volume = trapz(areas, dx);
    if (hydro_.volume > 1e-12) {
      std::vector<double> akb(Nx+1);
      for (int i = 0; i <= Nx; ++i) akb[i] = areas[i] * kbX[i];
      hydro_.KB = trapz(akb, dx) / hydro_.volume;
    } else {
      hydro_.KB = 0.0;
    }
  }

  // === BM & GM ===
  hydro_.BM = (hydro_.volume > 1e-12) ? hydro_.Ix / hydro_.volume : 0.0;
  double KG = D / 2.0;  // 均质船体
  hydro_.GM = hydro_.KB + hydro_.BM - KG;

  std::cout << "ShipHullGenerator: hydro — vol=" << hydro_.volume
            << " KB=" << hydro_.KB << " BM=" << hydro_.BM
            << " GM=" << hydro_.GM << " Aw=" << hydro_.Aw
            << " Cwp=" << hydro_.Cwp << " Ci=" << hydro_.Ci << std::endl;
}

// ═══════════════════════════════════════════════════════════
// 排水量曲线
// ═══════════════════════════════════════════════════════════

void ShipHullGenerator::computeVolumeCurve() {
  double D = spec_.D;
  int nSamples = 20;
  int Nx = 40, Nz = 40;
  double dx = spec_.L / Nx, dz = D / Nz;

  hydro_.draftCurve.resize(nSamples);
  hydro_.volumeCurve.resize(nSamples);

  for (int k = 0; k < nSamples; ++k) {
    double dk = D * (k + 1) / nSamples;  // 吃水
    hydro_.draftCurve[k] = dk;

    std::vector<double> areas(Nx+1);
    for (int i = 0; i <= Nx; ++i) {
      double x = i * dx;
      std::vector<double> yVals(Nz+1);
      // ★ 2026-09-19 修 B：自**龙骨**向上积分到水线。
      // 水线在局部 z = dk − D（局部 z：甲板 0 → 龙骨 −D），
      // 故积分区间是 z ∈ [−D, dk−D]，即自龙骨起 j·dz。
      // 原实现写 `zGz = -j*dz`（自**甲板**向下）→ 积的是**水线以上**的体积，
      // 与结构体注释「排水体积」及所有调用者的预期相反（V_top + V_sub ≡ V_total）。
      // 见 doc/plans/headless_ctl_plan.md §16。
      int nCut = static_cast<int>(dk / D * Nz);
      if (nCut > Nz) nCut = Nz;
      for (int j = 0; j <= nCut; ++j) {
        double zGz = -D + j * dz;   // 自龙骨 −D 向上
        yVals[j] = halfBreadthFunc(x, zGz);
      }
      // 梯形积分到吃水
      double area = 0.0;
      for (int j = 0; j < nCut; ++j) {
        area += (yVals[j] + yVals[j+1]) * 0.5 * dz;
      }
      areas[i] = 2.0 * area;
    }
    hydro_.volumeCurve[k] = trapz(areas, dx);
  }

  /// ★ 2026-09-19 修 ④：设计吃水 = 解 V_sub(d) = (ρ/ρw)·V_total，
  /// 而非 computeHydroProps 里那个**棱柱体近似** (ρ/ρw)·D
  /// （把船当等截面体；WigleyClassic 差 0.15 m，细长船型失真严重）。见 §16。
  /// 放在这里是因为**构造函数中 computeHydroProps() 先于 computeVolumeCurve() 执行**，
  /// 只有此处体积曲线才就绪。
  {
    const double target = (spec_.rho / 1000.0) * hydro_.volume;   // 需排开的水量
    const auto &dc = hydro_.draftCurve;
    const auto &vc = hydro_.volumeCurve;
    if (vc.size() >= 2 && dc.size() == vc.size() && vc.back() > 1e-9) {
      if (target <= vc.front()) {
        hydro_.draft = dc.front();
      } else if (target >= vc.back()) {
        hydro_.draft = dc.back();
      } else {
        double lo = dc.front(), hi = dc.back();
        for (int it = 0; it < 60; ++it) {   // 体积曲线单调递增 → 二分
          const double mid = 0.5 * (lo + hi);
          if (volumeAtDraft(mid) < target) lo = mid; else hi = mid;
        }
        hydro_.draft = 0.5 * (lo + hi);
      }
    }
  }

  /// ★★ 缺陷 ⑩ 修复（2026-09-20）：设计吃水处的**正确**静水力 → 新字段。
  /// **落点是硬约束**：computeHydroProps() 在构造函数里先于本函数执行，
  /// 那时 hydro_.draft 还是棱柱体初值 (ρ/ρw)·D，不是解出来的设计吃水。
  /// 原口径错：Aw/Ix 取 z=0（甲板，最宽处）、volume/KB 按全深度积分、
  /// GM = KB + BM − KG 坐标系混用（KB 是"距甲板深度"却当"距龙骨高度"用）。
  /// 新字段**不动** volume/KB/BM/Aw/Ix/GM —— 4 处 `m = rho*volume` 依赖 volume 是全船体积。
  /// 见 doc/plans/headless_ctl_plan.md §26。
  {
    const double d = hydro_.draft;
    const double zw = d - spec_.D;                 // 水线局部 z（甲板 0 → 龙骨 −D）
    const int Nx = 400;
    const double dx = spec_.L / Nx;

    /// 设计吃水处水线面
    {
      std::vector<double> yV(Nx + 1), y3V(Nx + 1);
      for (int i = 0; i <= Nx; ++i) {
        const double y = halfBreadthFunc(i * dx, zw);
        yV[i] = y; y3V[i] = y * y * y;
      }
      hydro_.AwD = 2.0 * trapz(yV, dx);
      hydro_.IxD = (2.0 / 3.0) * trapz(y3V, dx);
    }

    /// 浸水体积与浮心高度（自龙骨向上积分到水线）
    {
      const int Nz = 400;
      const double dz = d / Nz;
      std::vector<double> areas(Nx + 1), mz(Nx + 1);
      for (int i = 0; i <= Nx; ++i) {
        const double x = i * dx;
        std::vector<double> yV(Nz + 1), yzV(Nz + 1);
        for (int j = 0; j <= Nz; ++j) {
          const double zGz = -spec_.D + j * dz;    // 自龙骨 −D 向上
          const double y = halfBreadthFunc(x, zGz);
          yV[j] = y;
          yzV[j] = y * (j * dz);                   // 距龙骨高度加权
        }
        areas[i] = 2.0 * trapz(yV, dz);
        mz[i] = 2.0 * trapz(yzV, dz);              // = areas * KB_x
      }
      /// ★ 复用同一条体积曲线 → 与 `m = rho*volume` 的质量**精确自洽**
      /// （若另用高分辨率重算会与曲线差 ~0.07%，见 §26.3）
      hydro_.volumeD = volumeAtDraft(d);
      hydro_.KBup = (hydro_.volumeD > 1e-12) ? trapz(mz, dx) / hydro_.volumeD : 0.0;
    }

    hydro_.BMd = (hydro_.volumeD > 1e-12) ? hydro_.IxD / hydro_.volumeD : 0.0;
    hydro_.GMd = hydro_.KBup + hydro_.BMd - spec_.D / 2.0;   // 均质船体 KG = D/2
  }
}

double ShipHullGenerator::volumeAtDraft(double _draft) const {
  const auto &dc = hydro_.draftCurve;
  const auto &vc = hydro_.volumeCurve;
  if (dc.empty()) return 0.0;
  if (_draft <= dc.front()) return vc.front();
  if (_draft >= dc.back())  return vc.back();

  // 线性插值
  auto it = std::upper_bound(dc.begin(), dc.end(), _draft);
  int i = static_cast<int>(it - dc.begin());
  double t = (_draft - dc[i-1]) / (dc[i] - dc[i-1]);
  return vc[i-1] + t * (vc[i] - vc[i-1]);
}

// ═══════════════════════════════════════════════════════════
// 型值表
// ═══════════════════════════════════════════════════════════

void ShipHullGenerator::computeLinesData() {
  int Nx = std::max(spec_.visNx, 40);
  int Nz = std::max(spec_.visNz, 20);
  double L = spec_.L, D = spec_.D;
  double dx = L / Nx, dz = D / Nz;

  lines_.stations.resize(Nx + 1);
  lines_.waterlines.resize(Nz + 1);
  lines_.halfBreadths.resize(Nx + 1, std::vector<double>(Nz + 1));

  for (int i = 0; i <= Nx; ++i) {
    double x = i * dx;
    lines_.stations[i] = x;
    for (int j = 0; j <= Nz; ++j) {
      double zGz = -j * dz;
      if (i == 0) lines_.waterlines[j] = zGz;
      lines_.halfBreadths[i][j] = halfBreadthFunc(x, zGz);
    }
  }
}

// ═══════════════════════════════════════════════════════════
// OBJ 输出 — 无坐标变换
// ═══════════════════════════════════════════════════════════

bool ShipHullGenerator::writeVisualOBJ(const std::string &_fp) const {
  return writeOBJ(visMesh_, _fp, spec_.L * 0.5);
}

bool ShipHullGenerator::writeBuoyancyOBJ(const std::string &_fp) const {
  return writeOBJ(buoyMesh_, _fp, spec_.L * 0.5);
}

std::string ShipHullGenerator::meshTag() const {
  /// FNV-1a 64 —— 只求"内容不同则指纹不同"，不用于安全用途。
  /// 输入：两个网格的顶点数 / 三角形数 + 全部顶点坐标的 8 字节位模式。
  /// 三角形顶点位置与 vertices 重复（generateMesh 存的是副本），故不必再遍历三角形。
  uint64_t h = 1469598103934665603ULL;
  auto mixByte = [&h](unsigned char _b) {
    h ^= static_cast<uint64_t>(_b);
    h *= 1099511628211ULL;
  };
  auto mixU64 = [&](uint64_t _v) {
    for (int i = 0; i < 8; ++i) mixByte(static_cast<unsigned char>((_v >> (8 * i)) & 0xFFULL));
  };
  auto mixMesh = [&](const MeshDataSimple &_m) {
    mixU64(_m.vertices.size());
    mixU64(_m.triangles.size());
    for (const auto &v : _m.vertices) {
      const double c[3] = {v.X(), v.Y(), v.Z()};
      for (double x : c) {
        uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(x), "double/uint64 尺寸不一致");
        std::memcpy(&bits, &x, sizeof(bits));
        mixU64(bits);
      }
    }
  };
  mixMesh(visMesh_);
  mixMesh(buoyMesh_);

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return std::string(buf, 8);   ///< 取前 8 位十六进制，足够区分且文件名短
}

bool ShipHullGenerator::writeOBJ(const MeshDataSimple &_mesh,
                                  const std::string &_filepath,
                                  double _cx) const {
  std::ofstream f(_filepath);
  if (!f.is_open()) {
    std::cerr << "ShipHullGenerator: 无法写入 " << _filepath << std::endl;
    return false;
  }

  f << "# KeShip ShipHullGenerator v2 — Gazebo Z_UP\n";

  // 坐标→索引映射 (三角形存顶点副本，需坐标匹配)
  auto fmt = [](const gz::math::Vector3d &v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.8g,%.8g,%.8g", v.X(), v.Y(), v.Z());
    return std::string(buf);
  };
  std::map<std::string, int> coordToIdx;
  for (size_t vi = 0; vi < _mesh.vertices.size(); ++vi) {
    coordToIdx[fmt(_mesh.vertices[vi])] = static_cast<int>(vi) + 1;
  }

  // 顶点位置 v
  for (size_t vi = 0; vi < _mesh.vertices.size(); ++vi) {
    f << "v " << (_mesh.vertices[vi].X() - _cx) << " "
      << _mesh.vertices[vi].Y() << " "
      << _mesh.vertices[vi].Z() << "\n";
  }

  // 顶点法线 vn (DART ODE 碰撞检测需要)
  if (!_mesh.normals.empty()) {
    for (size_t vi = 0; vi < _mesh.normals.size(); ++vi) {
      f << "vn " << _mesh.normals[vi].X() << " "
        << _mesh.normals[vi].Y() << " "
        << _mesh.normals[vi].Z() << "\n";
    }
  }

  // 三角形面 f v1//vn1 v2//vn2 v3//vn3
  for (const auto &tri : _mesh.triangles) {
    int i0 = coordToIdx.at(fmt(tri.v0));
    int i1 = coordToIdx.at(fmt(tri.v1));
    int i2 = coordToIdx.at(fmt(tri.v2));
    if (_mesh.normals.empty()) {
      f << "f " << i0 << " " << i1 << " " << i2 << "\n";
    } else {
      f << "f " << i0 << "//" << i0 << " "
        << i1 << "//" << i1 << " "
        << i2 << "//" << i2 << "\n";
    }
  }

  f.close();
  std::cout << "ShipHullGenerator: OBJ → " << _filepath
            << " (" << _mesh.vertices.size() << "v, "
            << _mesh.triangles.size() << "f)" << std::endl;
  return true;
}

// ═══════════════════════════════════════════════════════════
// 梯形积分
// ═══════════════════════════════════════════════════════════

double ShipHullGenerator::trapz(const std::vector<double> &_y, double _dx) {
  if (_y.size() < 2) return 0.0;
  double sum = 0.0;
  for (size_t i = 0; i + 1 < _y.size(); ++i)
    sum += (_y[i] + _y[i+1]);
  return sum * 0.5 * _dx;
}

}  // namespace keship
