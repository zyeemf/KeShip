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
      << "              <bumpScale>75 75</bumpScale>\n"
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
    for (int retry = 0; retry < 20; ++retry)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

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

MeshDataSimple WaterAreaVisualManager::GeneratePolygonMesh(
    const WaterArea &_area) const
{
  double bboxW = _area.xMax - _area.xMin;
  double bboxH = _area.yMax - _area.yMin;
  double cx = (_area.xMin + _area.xMax) * 0.5;
  double cy = (_area.yMin + _area.yMax) * 0.5;

  // 网格步长按 PMS 最高显著分量波长自适应：每波长 ≥3 个网格单元，
  // 避免波浪欠采样呈现"一格一格"/aliasing（2026-08 波浪显示修复）
  // 最高显著分量为 n=(number-1)/2，波长 λ_min = λ_p / scale^((number-1)/2)
  // （PMS 采样 n 从 -(number-1)/2 到 +(number-1)/2，见 Wavefield::RecalculatePms）
  double targetStep = 1.0;
  const auto &wp = _area.waveParams;
  if (wp.model == "PMS" && wp.period > 0.0 && wp.number > 0)
  {
    constexpr double kG = 9.81;
    const double lambdaP = kG * wp.period * wp.period / (2.0 * M_PI);
    const double nMax = (static_cast<double>(wp.number) - 1.0) / 2.0;
    const double lambdaMin = lambdaP / std::pow(wp.scale, nMax);
    targetStep = lambdaMin / 3.0;
  }
  if (targetStep < 0.5) targetStep = 0.5;
  if (targetStep > 5.0) targetStep = 5.0;

  int divsX = static_cast<int>(std::ceil(bboxW / targetStep));
  int divsY = static_cast<int>(std::ceil(bboxH / targetStep));
  if (divsX < 50) divsX = 50;
  if (divsX > 400) divsX = 400;
  if (divsY < 50) divsY = 50;
  if (divsY > 400) divsY = 400;
  double stepX = bboxW / divsX;
  double stepY = bboxH / divsY;

  MeshDataSimple mesh;
  std::vector<std::vector<int>> idx(divsX + 1,
      std::vector<int>(divsY + 1, -1));

  for (int i = 0; i <= divsX; i++)
  {
    for (int j = 0; j <= divsY; j++)
    {
      double wx = _area.xMin + i * stepX;
      double wy = _area.yMin + j * stepY;
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

  for (int i = 0; i < divsX; i++)
  {
    for (int j = 0; j < divsY; j++)
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
    std::cerr << "GeneratePolygonMesh: 多边形内无网格点, 回退 plane"
              << std::endl;
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
