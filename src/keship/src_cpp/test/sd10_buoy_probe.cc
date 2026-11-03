/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* SD-10 探针：离线复算「网格浮力」的浸水体积曲线 + 收敛性
 *
 * 用法:
 *   sd10_buoy_probe <buoy.obj>           # 测已有网格
 *   sd10_buoy_probe --gen <Nx> <Nz>      # 现场生成 Wigley 网格（测收敛性）
 *
 * 判据：网格加密后 V_sub 应收敛到解析值 V(T)=6(T-T^3/3)（L6/B1.5/D1）。
 *       若不收敛 ⇒ 算法问题；若收敛 ⇒ 低分辨率离散误差。
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <gz/math/Pose3.hh>
#include <gz/math/Plane.hh>

#include "Buoyancy.hh"
#include "MeshUtils.hh"
#include "ShipHullGenerator.hh"

namespace {

/// 浸没体积（模型上移 dz 后，船体局部 z 从 -1 到 -dz 的部分）
///
/// ⚠ 2026-10-31 订正：原写成 V(1-dz) = 6(T-T^3/3)，是**从水线往下**量的体积，
///   而浸没部分在船体**下部**（下部窄）。正确式子为
///       V_sub = V(1) - V(dz) = 4LBD/9 - 6(dz - dz^3/3)
///   错用会让 dz=0.5 处"偏差 −55%"，纯属判据错，产品无问题。
double V_analytic(double _dz)
{
  const double Vfull = 4.0;   // L6/B1.5/D1 -> 4LBD/9 = 4.0
  double d = _dz;
  if (d < 0.0) d = 0.0;
  if (d > 1.0) d = 1.0;
  return Vfull - 6.0 * (d - d * d * d / 3.0);
}

void Run(const keship::MeshDataSimple &_mesh, const std::string &_tag)
{
  std::printf("== %s: %zu 顶点, %zu 三角 ==\n", _tag.c_str(),
              _mesh.vertices.size(), _mesh.triangles.size());
  keship::Buoyancy b;
  b.loadMeshData(_mesh);
  double total = 0.0;
  for (const auto &t : b.triangles_) total += std::abs(t.volume_);
  std::printf("   totalVolume = %.6f m3\n", total);

  std::printf("   %8s %12s %12s %9s\n", "pose_z", "V_sub", "V_analytic", "err%");
  const double dzs[] = {0.0, 0.2, 0.346, 0.5, 0.653, 0.8};
  for (double dz : dzs) {
    gz::math::Pose3d pose(0.0, 0.0, dz, 0.0, 0.0, 0.0);
    gz::math::Plane<double> plane(gz::math::Vector3d(0.0, 0.0, 1.0), 0.0);
    const double v = b.calculateTotalVolumeContribution(pose, plane);
    const double va = V_analytic(dz);
    const double err = (va > 1e-9) ? 100.0 * (v - va) / va : 0.0;
    std::printf("   %8.3f %12.6f %12.6f %8.2f%%\n", dz, v, va, err);
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc >= 4 && std::string(argv[1]) == "--gen") {
    const int nx = std::atoi(argv[2]);
    const int nz = std::atoi(argv[3]);
    keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0, 500.0);
    spec.buoyNx = nx;
    spec.buoyNz = nz;
    keship::ShipHullGenerator g(spec);
    const auto &hp = g.hydroProps();
    std::printf("[HYDRO] draft=%.6f  volume=%.6f  volumeD=%.6f  KBup=%.6f  GMd=%.6f  AwD=%.6f\n",
                hp.draft, hp.volume, hp.volumeD, hp.KBup, hp.GMd, hp.AwD);
    std::printf("[CHECK] m=rho*volume=%.1f kg -> 需排开 %.4f m3\n",
                spec.rho * hp.volume, spec.rho * hp.volume / 1000.0);
    Run(g.buoyancyMesh(), "gen " + std::to_string(nx) + "x" + std::to_string(nz));
    return 0;
  }

  if (argc < 2) {
    std::printf("用法: %s <buoy.obj> | --gen <Nx> <Nz>\n", argv[0]);
    return 1;
  }
  auto mesh = keship::LoadMeshFromFile(argv[1]);
  if (mesh.vertices.empty()) {
    std::printf("加载失败: %s\n", argv[1]);
    return 1;
  }
  Run(mesh, argv[1]);
  return 0;
}
