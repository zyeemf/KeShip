/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水区 Waves 模型管理器实现 */

#include "WaterAreaVisualManager.hh"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include <gz/msgs/entity_factory.pb.h>
#include <gz/msgs/entity.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/scene.pb.h>
#include <gz/transport/Node.hh>

namespace keship
{

std::string WaterAreaVisualManager::MakeSurfaceSdf(const WaterArea &_area) const
{
  if (shaderDir_.empty())
  {
    std::cerr << "WaterAreaVisualManager: shaderDir 未设置" << std::endl;
    return "";
  }

  double w = _area.xMax - _area.xMin;
  double h = _area.yMax - _area.yMin;
  double cx = (_area.xMin + _area.xMax) * 0.5;
  double cy = (_area.yMin + _area.yMax) * 0.5;

  std::string vertexShader   = shaderDir_ + "GerstnerWaves_vs_330.glsl";
  std::string fragmentShader = shaderDir_ + "GerstnerWaves_fs_330.glsl";
  std::string textureDir     = shaderDir_ + "../textures/";
  std::string bumpMapPath    = textureDir + "wave_normals.dds";
  std::string cubeMapPath    = textureDir + "skybox_lowres.dds";

  bool usePolygonMesh = false;
  std::string objPath;

  {
    WaterArea areaForMesh = _area;
    if (areaForMesh.vertices.size() < 3)
    {
      areaForMesh.vertices = {
        {_area.xMin, _area.yMin}, {_area.xMax, _area.yMin},
        {_area.xMax, _area.yMax}, {_area.xMin, _area.yMax}};
    }

    MeshDataSimple mesh = GeneratePolygonMesh(areaForMesh);
    if (!mesh.triangles.empty())
    {
      objPath = GetTempMeshPath(_area.name);
      if (WritePolygonObjFile(mesh, objPath))
        usePolygonMesh = true;
    }
  }

  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.9\">\n"
      << "  <model name=\"waves_" << _area.name << "\">\n"
      << "    <static>true</static>\n"
      << "    <pose>" << cx << " " << cy << " " << _area.baseHeight
      << " 0 0 0</pose>\n"
      << "    <link name=\"link\">\n"
      << "      <visual name=\"water_surface\">\n"
      << "        <plugin filename=\"libWaveVisual.so\""
      << " name=\"keship::WaveVisual\">\n"
      << "          <shader>\n"
      << "            <vertex>" << vertexShader << "</vertex>\n"
      << "            <fragment>" << fragmentShader << "</fragment>\n"
      << "            <parameters>\n"
      << "              <rescale>0.125</rescale>\n"
      /// ★ 2026-09-21（S2）：bumpScale 语义变了 —— 着色器改用**模型局部坐标（米）**
      /// 采样法线贴图（原先用归一化 uv0，导致细节世界尺度随水域尺寸变化）。
      /// 故这里从 75 改为 0.15 ⇒ 每 1/0.15 ≈ 6.67 m 一个纹理周期（与原先观感一致），
      /// 第二层再乘 0.43 ⇒ ≈2.9 m，两层共同覆盖 2.9~6.7 m 的细节尺度。
      << "              <bumpScale>0.15 0.15</bumpScale>\n"
      << "              <bumpSpeed>0.01 0.0</bumpSpeed>\n"
      << "              <hdrMultiplier>0.4</hdrMultiplier>\n"
      << "              <fresnelPower>5.0</fresnelPower>\n"
      << "              <shallowColor>0 0.1 0.2 1.0</shallowColor>\n"
      << "              <deepColor>0 0.05 0.2 1.0</deepColor>\n"
      << "              <bumpMap>" << bumpMapPath << "</bumpMap>\n"
      << "              <cubeMap>" << cubeMapPath << "</cubeMap>\n"
      << "            </parameters>\n"
      << "          </shader>\n"
      << "          <wavefield>\n"
      << "            <topic>/keship/wavefield/"
      << _area.name << "/parameters</topic>\n"
      << "            <wave>\n"
      << "              <model>" << _area.waveParams.model << "</model>\n"
      << "              <period>" << _area.waveParams.period << "</period>\n"
      << "              <number>" << _area.waveParams.number << "</number>\n"
      << "              <scale>" << _area.waveParams.scale << "</scale>\n"
      << "              <gain>" << _area.waveParams.gain << "</gain>\n"
      << "              <direction>" << _area.waveParams.direction
      << "</direction>\n"
      << "              <angle>" << _area.waveParams.angle << "</angle>\n"
      << "              <steepness>" << _area.waveParams.steepness
      << "</steepness>\n"
      << "              <amplitude>" << _area.waveParams.amplitude
      << "</amplitude>\n"
      << "              <tau>" << _area.waveParams.tau << "</tau>\n"
      << "            </wave>\n"
      << "          </wavefield>\n"
      << "        </plugin>\n";

  if (usePolygonMesh)
  {
    sdf << "        <geometry>\n"
        << "          <mesh>\n"
        << "            <uri>file://" << objPath << "</uri>\n"
        << "          </mesh>\n"
        << "        </geometry>\n";
  }
  else
  {
    sdf << "        <geometry>\n"
        << "          <plane>\n"
        << "            <normal>0 0 1</normal>\n"
        << "            <size>" << w << " " << h << "</size>\n"
        << "          </plane>\n"
        << "        </geometry>\n";
  }

  sdf << "        <visibility_flags>8</visibility_flags>\n"
      << "      </visual>\n"
      << "    </link>\n"
      << "  </model>\n"
      << "</sdf>\n";

  return sdf.str();
}

void WaterAreaVisualManager::SpawnAll(
    const std::vector<WaterArea> &_areas,
    gz::transport::Node &_node)
{
  EnsureKeshipDataDirs();

  /// 全场景扫描 waves_* 前缀模型（含上个 GUI 进程残留的同名水面），
  /// 不依赖本进程 spawnedNames_
  std::vector<std::string> staleNames;
  {
    gz::msgs::Empty req;
    gz::msgs::Scene rep;
    bool ok;
    if (_node.Request("/world/keship_world/scene/info",
                      req, 1000, rep, ok))
    {
      for (int i = 0; i < rep.model_size(); ++i)
      {
        const std::string &name = rep.model(i).name();
        if (name.rfind("waves_", 0) == 0)
          staleNames.push_back(name);
      }
    }
  }
  spawnedNames_.clear();

  if (!staleNames.empty())
  {
    std::cout << "WaterAreaVisualManager: 场景中发现旧水面 "
              << staleNames.size() << " 个，逐一删除" << std::endl;
    for (const auto &name : staleNames)
    {
      gz::msgs::Entity req;
      gz::msgs::Boolean rep;
      bool result;
      req.set_name(name);
      req.set_type(gz::msgs::Entity::MODEL);
      bool ok = _node.Request("/world/keship_world/remove",
                              req, 5000, rep, result);
      if (!(ok && result && rep.data()))
        std::cerr << "WaterAreaVisualManager: 删除残留水面 "
                  << name << " 失败" << std::endl;
    }

    bool allGone = false;
    /// 2026-10-26：原来固定 20 次 × 500 ms 步进，实测在 GUI 线程上阻塞约 523 ms。
    /// 改为 500 次 × 20 ms —— 等待上限仍是 10 s，但旧水面一消失就立即返回。
    for (int retry = 0; retry < 500; ++retry)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));

      gz::msgs::Empty req;
      gz::msgs::Scene rep;
      bool ok;
      if (!_node.Request("/world/keship_world/scene/info",
                          req, 1000, rep, ok))
        continue;

      allGone = true;
      for (const auto &name : staleNames)
      {
        bool found = false;
        for (int i = 0; i < rep.model_size(); ++i)
        {
          if (rep.model(i).name() == name) { found = true; break; }
        }
        if (found) { allGone = false; break; }
      }
      if (allGone)
      {
        std::cout << "WaterAreaVisualManager: 旧水面已全部清除 ("
                  << retry + 1 << " 次轮询)" << std::endl;
        break;
      }
    }
    if (!allGone)
    {
      std::cerr << "WaterAreaVisualManager: 等待旧水面清除超时 (10s)"
                << std::endl;
    }
  }

  for (const auto &area : _areas)
  {
    if (area.name.empty())
      continue;

    std::string sdf = MakeSurfaceSdf(area);
    if (sdf.empty())
      continue;

    std::string name = "waves_" + area.name;

    gz::msgs::EntityFactory req;
    gz::msgs::Boolean rep;
    bool result;

    req.set_name(name);
    req.set_allow_renaming(false);
    req.set_sdf(sdf);

    bool ok = _node.Request("/world/keship_world/create/blocking",
                            req, 10000, rep, result);
    if (ok && result && rep.data())
    {
      spawnedNames_.insert(name);
      std::cout << "WaterAreaVisualManager: 水面模型 " << name
                << " 已创建" << std::endl;
    }
    else
    {
      std::cerr << "WaterAreaVisualManager: 水面模型 " << name
                << " 创建失败" << std::endl;
    }
  }
}

bool WaterAreaVisualManager::PointInPolygon(
    double x, double y,
    const std::vector<std::pair<double,double>>& verts)
{
  bool inside = false;
  size_t n = verts.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++)
  {
    if ((verts[i].second > y) != (verts[j].second > y) &&
        x < (verts[j].first - verts[i].first) * (y - verts[i].second)
            / (verts[j].second - verts[i].second) + verts[i].first)
    {
      inside = !inside;
    }
  }
  return inside;
}

///////////////////////////////////////////////////////////////////////////////
/// 网格 LOD：沿单轴生成"中心密、外围疏"的非均匀采样坐标（2026-09-21）
///
/// 背景：原先全水域用**单一等密度**网格。默认水域（500×500 m）为
/// 197×197 = 38 809 顶点，每顶点在 vertex shader 里跑 Nwaves 个 Gerstner 分量；
/// 而学生视野通常只看船周围 50~100 m，**远处 80% 顶点几乎不可见却按最高密度计算**。
///
/// 做法：保留规则拓扑（(nx−1)×(ny−1) 个四边形），只把**顶点位置**按半径分级拉开 ——
/// 相邻格共享顶点，因此**不会产生 T-junction 裂缝**，三角形拼接逻辑无需改动。
std::vector<double> WaterAreaVisualManager::BuildAxisSamples(double _lo,
    double _hi, double _center, double _hFine,
    const std::vector<LodBand> &_bands)
{
  std::vector<double> out;
  if (!(_hi > _lo) || !(_hFine > 0.0))
  {
    out.push_back(_lo);
    out.push_back(_hi);
    return out;
  }

  /// 半跨：取中心到两端的较大者（水域可不对称，此时另一侧按需裁剪）
  const double halfSpan = std::max(_center - _lo, _hi - _center);
  if (halfSpan <= 0.0)
  {
    out.push_back(_lo);
    out.push_back(_hi);
    return out;
  }

  /// 1) 生成"相对中心的半径"序列（升序，首元素 0）
  std::vector<double> radii;
  radii.push_back(0.0);

  double r = 0.0;
  double step = _hFine;
  double prevEnd = 0.0;
  for (size_t k = 0; k < _bands.size(); ++k)
  {
    double rEnd = _bands[k].radiusFrac * halfSpan;
    /// 绝对下限：防止小水域时中心细区被比例压得太小（否则步长被迫拉大）
    if (_bands[k].minRadius > 0.0 && rEnd < _bands[k].minRadius)
      rEnd = _bands[k].minRadius;
    /// 绝对上限：抑制"大水域时中心细区被比例撑得过大"
    if (_bands[k].maxRadius > 0.0 && rEnd > _bands[k].maxRadius)
      rEnd = _bands[k].maxRadius;
    /// 档位必须单调递增，否则后面的档会被前面的吞掉
    if (rEnd < prevEnd) rEnd = prevEnd;
    prevEnd = rEnd;

    if (k > 0) step = _hFine * _bands[k].stepMul;   // 首档用基准步长
    if (!(step > 0.0)) step = _hFine;
    while (r + step <= rEnd - 1e-9)
    {
      r += step;
      radii.push_back(r);
    }
    /// 档边界对齐：补一个正好落在 rEnd 的采样点，避免档间出现比 step 更宽的缝
    if (r < rEnd - 1e-9)
    {
      r = rEnd;
      radii.push_back(r);
    }
  }

  /// 2) 末档继续延伸到覆盖整个半跨（含超出 halfSpan 一点点，供裁剪取用）
  step = _hFine * (_bands.empty() ? 1.0 : _bands.back().stepMul);
  if (!(step > 0.0)) step = _hFine;
  while (r + step < halfSpan)
  {
    r += step;
    radii.push_back(r);
  }
  if (r < halfSpan)
    radii.push_back(halfSpan);

  /// 3) 由半径序列生成坐标：负侧（含中心）→ 正侧，并裁剪到 [_lo, _hi]
  for (auto it = radii.rbegin(); it != radii.rend(); ++it)
  {
    const double v = _center - *it;
    if (v >= _lo - 1e-9 && v <= _hi + 1e-9)
      out.push_back(v);
  }
  for (size_t k = 1; k < radii.size(); ++k)
  {
    const double v = _center + radii[k];
    if (v >= _lo - 1e-9 && v <= _hi + 1e-9)
      out.push_back(v);
  }

  if (out.size() < 2)
  {
    out.clear();
    out.push_back(_lo);
    out.push_back(_hi);
    return out;
  }

  /// 4) 保证两端贴到水域边界（否则边缘会缺一条带）
  if (out.front() > _lo + 1e-9) out.insert(out.begin(), _lo);
  if (out.back() < _hi - 1e-9) out.push_back(_hi);

  return out;
}

///////////////////////////////////////////////////////////////////////////////
/// 中心区基准步长：按 PMS 最高显著分量波长自适应，每波长 ≥3 个网格单元，
/// 避免波浪欠采样呈现"一格一格"/aliasing（2026-08 波浪显示修复）。
/// 最高显著分量为 n=(number-1)/2，λ_min = λ_p / scale^((number-1)/2)
/// （PMS 采样 n 从 -(number-1)/2 到 +(number-1)/2，见 Wavefield::RecalculatePms）。
double WaterAreaVisualManager::ComputeBaseStep(const WaveParams &_wp)
{
  double targetStep = 1.0;
  if (_wp.model == "PMS" && _wp.period > 0.0 && _wp.number > 0)
  {
    constexpr double kG = 9.81;
    const double lambdaP = kG * _wp.period * _wp.period / (2.0 * M_PI);
    const double nMax = (static_cast<double>(_wp.number) - 1.0) / 2.0;
    const double lambdaMin = lambdaP / std::pow(_wp.scale, nMax);
    targetStep = lambdaMin / 3.0;
  }
  if (targetStep < 0.5) targetStep = 0.5;
  if (targetStep > 5.0) targetStep = 5.0;
  return targetStep;
}

///////////////////////////////////////////////////////////////////////////////
/// 网格 LOD 档位（唯一来源，GeneratePolygonMesh 与 ComputeMeshStats 共用）
const std::vector<WaterAreaVisualManager::LodBand> &
WaterAreaVisualManager::LodBands()
{
  /// 半径区间取值依据：
  /// - 细区 40~80 m：40 m 是"船周围必须精细"的最小范围（小水域也能保证质量），
  ///   80 m 是上限（再远肉眼分不出密度差）；
  /// - 中区 120~240 m：一眼能看到的范围；
  /// - 再远即使步长很大也看不出来。
  static const std::vector<LodBand> kBands = {
      {0.15, 1.0, 40.0, 80.0},     // 细区：保持基准步长（船所在的核心区）
      {0.45, 2.5, 120.0, 240.0},   // 中区：2.5 倍步长
      {1.01, 6.0, -1.0, -1.0}};    // 其余：6 倍步长
  return kBands;
}

///////////////////////////////////////////////////////////////////////////////
WaterAreaVisualManager::MeshStats WaterAreaVisualManager::ComputeMeshStats(
    const WaterArea &_area) const
{
  MeshStats st;

  /// 跑一遍**真实生成路径**（含多边形裁剪 + 上限夹紧），
  /// 采样点数由 GeneratePolygonMesh 回传 —— 不自己重算，避免与真实生成不一致。
  MeshDataSimple mesh = GeneratePolygonMesh(_area, &st.samplesX, &st.samplesY);
  st.vertices = mesh.vertices.size();
  st.triangles = mesh.triangles.size();
  return st;
}

///////////////////////////////////////////////////////////////////////////////
MeshDataSimple WaterAreaVisualManager::GeneratePolygonMesh(
    const WaterArea &_area, size_t *_samplesX, size_t *_samplesY) const
{
  double bboxW = _area.xMax - _area.xMin;
  double bboxH = _area.yMax - _area.yMin;
  double cx = (_area.xMin + _area.xMax) * 0.5;
  double cy = (_area.yMin + _area.yMax) * 0.5;

  /// 中心区基准步长（与 ComputeMeshStats 共用同一实现）
  double targetStep = ComputeBaseStep(_area.waveParams);

  /// ★ 步长不得超过半跨的一半：否则极小水域（如 1×1 m）每轴只采到 1~2 个点，
  /// 裁剪后顶点数不足 3 ⇒ 生成不出任何三角面（2026-09-21 边界探针发现）。
  /// 默认水域 halfSpan=250 m ⇒ 上限 125 m，远大于 targetStep 的实际取值（≤5 m），不受影响。
  {
    const double halfSpan = std::max(bboxW, bboxH) * 0.5;
    if (halfSpan > 0.0 && targetStep > halfSpan * 0.5)
      targetStep = halfSpan * 0.5;
  }

  /// ── 网格 LOD（2026-09-21）：中心密、外围疏 ──────────────────────
  /// targetStep 仍是**中心区**的步长（保持船周围的质量不变），
  /// 外围按档位放大步长 —— 远处顶点在屏幕上不足 1 像素，放大后视觉几乎无损，
  /// 但顶点数可降约 5~7 倍。档位半径按半跨比例给，随水域尺寸自动缩放。
  const std::vector<LodBand> &kLodBands = LodBands();

  double stepScale = 1.0;
  std::vector<double> xs = BuildAxisSamples(
      _area.xMin, _area.xMax, cx, targetStep, kLodBands);
  std::vector<double> ys = BuildAxisSamples(
      _area.yMin, _area.yMax, cy, targetStep, kLodBands);

  /// 总点数上限保护：水域极大或周期极小时仍不至于爆网格。
  /// ★ **必须循环夹紧**：档位半径有绝对上下限（minRadius/maxRadius），
  /// 所以"按比例放大步长"**不是等比例减少点数** —— 单次放大不保证收敛
  /// （2026-09-21 实测：±5000 m + Tp=0.1 单次放大后仍有 36 万顶点）。
  constexpr int kMaxSamplesPerAxis = 400;
  for (int guard = 0; guard < 12; ++guard)
  {
    if (static_cast<int>(xs.size()) <= kMaxSamplesPerAxis &&
        static_cast<int>(ys.size()) <= kMaxSamplesPerAxis)
      break;
    const double over = std::max(
        static_cast<double>(xs.size()) / kMaxSamplesPerAxis,
        static_cast<double>(ys.size()) / kMaxSamplesPerAxis);
    /// 略超一点（×1.1）以抵消"档位边界对齐点"带来的非线性，确保收敛
    stepScale *= std::max(1.1, over * 1.1);
    xs = BuildAxisSamples(_area.xMin, _area.xMax, cx,
                          targetStep * stepScale, kLodBands);
    ys = BuildAxisSamples(_area.yMin, _area.yMax, cy,
                          targetStep * stepScale, kLodBands);
  }

  const int nx = static_cast<int>(xs.size());
  const int ny = static_cast<int>(ys.size());
  /// 回传**实际采用**的采样点数（上限夹紧之后的值）——
  /// 调用方不得自己重算（重算会漏掉上面的循环夹紧，得到不一致的数字）
  if (_samplesX) *_samplesX = static_cast<size_t>(nx);
  if (_samplesY) *_samplesY = static_cast<size_t>(ny);
  if (nx < 2 || ny < 2)
  {
    std::cerr << "GeneratePolygonMesh: LOD 采样点不足, 回退 plane" << std::endl;
    return MeshDataSimple{};
  }

  MeshDataSimple mesh;
  std::vector<std::vector<int>> idx(nx, std::vector<int>(ny, -1));

  for (int i = 0; i < nx; i++)
  {
    for (int j = 0; j < ny; j++)
    {
      double wx = xs[i];
      double wy = ys[j];
      if (!PointInPolygon(wx, wy, _area.vertices))
        continue;

      double lx = wx - cx;
      double ly = wy - cy;
      double uvU = (wx - _area.xMin) / bboxW;
      double uvV = (wy - _area.yMin) / bboxH;

      idx[i][j] = static_cast<int>(mesh.vertices.size());
      mesh.vertices.emplace_back(lx, ly, 0.0);
      mesh.normals.emplace_back(0.0, 0.0, 1.0);
      mesh.texCoords.emplace_back(uvU, uvV);
    }
  }

  for (int i = 0; i < nx - 1; i++)
  {
    for (int j = 0; j < ny - 1; j++)
    {
      int p00 = idx[i][j];
      int p10 = idx[i + 1][j];
      int p01 = idx[i][j + 1];
      int p11 = idx[i + 1][j + 1];

      std::vector<int> corners;
      if (p00 >= 0) corners.push_back(p00);
      if (p10 >= 0) corners.push_back(p10);
      if (p11 >= 0) corners.push_back(p11);
      if (p01 >= 0) corners.push_back(p01);

      if (corners.size() < 3)
        continue;

      if (corners.size() == 4)
      {
        mesh.triangles.push_back({
            mesh.vertices[p00], mesh.vertices[p10], mesh.vertices[p11]});
        mesh.triangles.push_back({
            mesh.vertices[p00], mesh.vertices[p11], mesh.vertices[p01]});
        mesh.triangleIndices.push_back(
            {static_cast<unsigned>(p00),
             static_cast<unsigned>(p10),
             static_cast<unsigned>(p11)});
        mesh.triangleIndices.push_back(
            {static_cast<unsigned>(p00),
             static_cast<unsigned>(p11),
             static_cast<unsigned>(p01)});
      }
      else
      {
        mesh.triangles.push_back({
            mesh.vertices[corners[0]],
            mesh.vertices[corners[1]],
            mesh.vertices[corners[2]]});
        mesh.triangleIndices.push_back(
            {static_cast<unsigned>(corners[0]),
             static_cast<unsigned>(corners[1]),
             static_cast<unsigned>(corners[2])});
      }
    }
  }

  if (mesh.triangles.empty())
  {
    /// ★ 必须**返回空 mesh**：调用方（MakeSurfaceSdf）是以
    /// `!mesh.vertices.empty()` 判断"多边形网格可用"的 ——
    /// 若这里只打印警告却把 vertices 留着（例如退化水域只采到 1 个点），
    /// 调用方会误判为可用，进而写出一个画不出任何面的 OBJ
    /// （2026-09-21 边界探针发现）。
    std::cerr << "GeneratePolygonMesh: 多边形内无有效三角面, 回退 plane"
              << std::endl;
    return MeshDataSimple{};
  }

  return mesh;
}

bool WaterAreaVisualManager::WritePolygonObjFile(
    const MeshDataSimple &_mesh,
    const std::string &_filePath)
{
  if (_mesh.vertices.empty() || _mesh.triangles.empty())
    return false;

  std::string dir = _filePath;
  auto pos = dir.find_last_of('/');
  if (pos != std::string::npos)
    dir = dir.substr(0, pos);

  std::error_code ec;
  if (!std::filesystem::exists(dir, ec))
    std::filesystem::create_directories(dir, ec);

  std::ofstream file(_filePath);
  if (!file.is_open())
    return false;

  file << "# Polygon water mesh generated by keship\n";
  file << "o water_surface\n";

  for (const auto &v : _mesh.vertices)
    file << "v " << v.X() << " " << v.Y() << " " << v.Z() << "\n";

  for (const auto &uv : _mesh.texCoords)
    file << "vt " << uv.first << " " << uv.second << "\n";

  for (const auto &n : _mesh.normals)
    file << "vn " << n.X() << " " << n.Y() << " " << n.Z() << "\n";

  int vBase = 1;
  for (size_t t = 0; t < _mesh.triangleIndices.size(); t++)
  {
    const auto &tri = _mesh.triangleIndices[t];
    file << "f " << (tri[0] + vBase) << "/" << (tri[0] + vBase)
         << "/" << (tri[0] + vBase)
         << " " << (tri[1] + vBase) << "/" << (tri[1] + vBase)
         << "/" << (tri[1] + vBase)
         << " " << (tri[2] + vBase) << "/" << (tri[2] + vBase)
         << "/" << (tri[2] + vBase) << "\n";
  }

  file.close();
  std::cout << "WaterAreaVisualManager: OBJ 写入成功 "
            << _mesh.vertices.size() << " 顶点, "
            << _mesh.triangleIndices.size() << " 三角形 → "
            << _filePath << std::endl;
  return true;
}

std::string WaterAreaVisualManager::SanitizeName(
    const std::string &_name)
{
  std::string out;
  out.reserve(_name.size());
  for (char c : _name)
  {
    if (std::isalnum(static_cast<unsigned char>(c))
        || c == '_' || c == '-')
      out += c;
    else
      out += '_';
  }
  if (out.empty())
    out = "unnamed";
  return out;
}

std::string WaterAreaVisualManager::GetTempMeshPath(
    const std::string &_areaName) const
{
  auto id = ++meshGenerationId_;
  return tempDir_ + "polygon_" + SanitizeName(_areaName) + "_" + std::to_string(id) + ".obj";
}

void WaterAreaVisualManager::DeleteAll(gz::transport::Node &_node)
{
  for (const auto &name : spawnedNames_)
  {
    gz::msgs::Entity req;
    gz::msgs::Boolean rep;
    bool result;

    req.set_name(name);
    req.set_type(gz::msgs::Entity::MODEL);

    bool ok = _node.Request("/world/keship_world/remove",
                            req, 5000, rep, result);
    if (ok && result && rep.data())
    {
      std::cout << "WaterAreaVisualManager: 水面模型 " << name
                << " 已删除" << std::endl;
    }
    else
    {
      std::cerr << "WaterAreaVisualManager: 水面模型 " << name
                << " 删除失败" << std::endl;
    }
  }
  spawnedNames_.clear();

  std::error_code ec;
  if (std::filesystem::exists(tempDir_, ec))
  {
    for (auto &entry : std::filesystem::directory_iterator(tempDir_, ec))
    {
      if (entry.is_regular_file() && entry.path().extension() == ".obj")
        std::filesystem::remove(entry.path(), ec);
    }
  }
}

} // namespace keship
