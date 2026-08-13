/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型生成器 v2 实现 — Gazebo Z_UP 直接生成 */

#include "ShipHullGenerator.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

  double d = (rho / rhoW) * D;  // 设计吃水
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
      // 只积分到吃水深度
      int nCut = static_cast<int>(dk / D * Nz);
      if (nCut > Nz) nCut = Nz;
      for (int j = 0; j <= nCut; ++j) {
        double zAbs = j * dz;
        double zGz = -zAbs;
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
