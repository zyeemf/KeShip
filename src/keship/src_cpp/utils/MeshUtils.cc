/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 网格工具类实现 — 统一的 Assimp mesh 加载 */
/// ZhangYu 创建 2026.5.5

#include "MeshUtils.hh"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <set>
#include <map>

// ★ 2026-09-28 碰撞"凸包"模式：qhull（重入版）
#include <libqhull_r/qhull_ra.h>

#include "keship/utils/KeshipPaths.hh"

namespace keship
{

MeshDataSimple LoadMeshFromFile(const std::string &_filePath)
{
  MeshDataSimple result;

  Assimp::Importer importer;
  const unsigned int flags =
      aiProcess_Triangulate |
      aiProcess_JoinIdenticalVertices |
      aiProcess_GenSmoothNormals |
      aiProcess_PreTransformVertices;

  const aiScene *scene = importer.ReadFile(_filePath, flags);
  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
  {
    std::cerr << "MeshUtils: Assimp 加载文件失败: " << importer.GetErrorString() << std::endl;
    return result;
  }

  std::cout << "MeshUtils: 网格数量 " << scene->mNumMeshes << std::endl;

  /// 遍历所有网格，合并数据
  for (unsigned m = 0; m < scene->mNumMeshes; m++)
  {
    aiMesh *mesh = scene->mMeshes[m];
    unsigned vertexOffset = result.vertices.size();

    /// 提取顶点位置
    for (unsigned v = 0; v < mesh->mNumVertices; v++)
    {
      result.vertices.emplace_back(
          mesh->mVertices[v].x,
          mesh->mVertices[v].y,
          mesh->mVertices[v].z);
    }

    /// 提取 UV 坐标
    if (mesh->HasTextureCoords(0))
    {
      for (unsigned v = 0; v < mesh->mNumVertices; v++)
        result.texCoords.emplace_back(
            mesh->mTextureCoords[0][v].x,
            mesh->mTextureCoords[0][v].y);
    }

    /// 提取顶点法线（如果存在）
    if (mesh->HasNormals())
    {
      for (unsigned v = 0; v < mesh->mNumVertices; v++)
      {
        result.normals.emplace_back(
            mesh->mNormals[v].x,
            mesh->mNormals[v].y,
            mesh->mNormals[v].z);
      }
    }

    /// 提取三角形面
    for (unsigned f = 0; f < mesh->mNumFaces; f++)
    {
      aiFace face = mesh->mFaces[f];
      if (face.mNumIndices != 3)
        continue;

      unsigned i0 = face.mIndices[0] + vertexOffset;
      unsigned i1 = face.mIndices[1] + vertexOffset;
      unsigned i2 = face.mIndices[2] + vertexOffset;

      result.triangles.push_back({
          result.vertices[i0],
          result.vertices[i1],
          result.vertices[i2]});
      result.triangleIndices.push_back({i0, i1, i2});
    }

    /// 提取 diffuse 纹理路径（仅取第一个有纹理的 mesh）
    if (result.texturePath.empty() && mesh->mMaterialIndex < scene->mNumMaterials)
    {
      const aiMaterial *mat = scene->mMaterials[mesh->mMaterialIndex];
      aiString path;
      if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
        result.texturePath = path.C_Str();
    }
  }

  /// 诊断：顶点 Y 范围
  {
    int yp=0, yn=0; double ymn=1e9, ymx=-1e9;
    for (auto &v : result.vertices) {
      if (v.Y()>0) yp++; else yn++;
      if (v.Y()<ymn) ymn=v.Y(); if (v.Y()>ymx) ymx=v.Y();
    }
    std::cout << "MeshUtils: Y顶点 pos=" << yp << " neg=" << yn
              << " range=[" << ymn << "," << ymx << "] ";
  }
  std::cout << "MeshUtils: 加载完成 — "
            << result.vertices.size() << " 顶点, "
            << result.triangles.size() << " 三角形" << std::endl;

  /// 根据 DAE 元数据校正：根据 up_axis 声明和场景变换矩阵，
  /// 计算实际的 up-axis（effective），统一转换到 Z_UP。
  {
    DaeOrientationInfo info = ReadDaeOrientation(_filePath);
    if (!info.upAxis.empty()) {
      UpAxis effective = ParseUpAxis(info.upAxis);

      // NOTE: assimp's aiProcess_PreTransformVertices already bakes all
      // node transforms (incl. +-90deg rotations); the DAE's <matrix> no
      // longer implies a second Y<->Z remap. Only the declared up_axis
      // decides the conversion below.

      if (effective != UpAxis::Z_UP) {
        std::cout << "MeshUtils: DAE up_axis=" << info.upAxis
                  << " (hasYZSwap=" << info.hasYZSwap
                  << ", hasXZSwap=" << info.hasXZSwap
                  << ") â è½¬æ¢å° Z_UP" << std::endl;
        result = ConvertToZUp(result, effective);
      }
    }
  }

  return result;
}

/////////////////////////////////////////////////
DaeOrientationInfo ReadDaeOrientation(const std::string &_filePath)
{
  DaeOrientationInfo info;

  std::string ext;
  auto dotPos = _filePath.rfind('.');
  if (dotPos != std::string::npos)
    ext = _filePath.substr(dotPos);
  if (ext != ".dae" && ext != ".DAE")
    return info;

  std::ifstream ifs(_filePath);
  if (!ifs.is_open())
    return info;

  std::string line;
  while (std::getline(ifs, line) && info.upAxis.empty())
  {
    if (line.find("<up_axis>") != std::string::npos)
    {
      if (line.find("Y_UP")   != std::string::npos) info.upAxis = "Y_UP";
      if (line.find("Z_UP")   != std::string::npos) info.upAxis = "Z_UP";
      if (line.find("X_UP")   != std::string::npos) info.upAxis = "X_UP";
    }
    if (line.find("<geometry") != std::string::npos) break;
  }

  ifs.clear(); ifs.seekg(0);
  while (std::getline(ifs, line))
  {
    if (line.find("<matrix") != std::string::npos)
    {
      std::string::size_type p1 = line.find('>');
      std::string::size_type p2 = line.find("</matrix>");
      if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1)
      {
        std::string vals = line.substr(p1 + 1, p2 - p1 - 1);
        std::istringstream iss(vals);
        double a[16] = {0};
        for (int i = 0; i < 16 && iss >> a[i]; i++) {}
        if (std::abs(a[6]) > 1e-6 &&
            std::abs(a[6] + a[9]) < std::abs(a[6]) * 0.2)
          info.hasYZSwap = true;
        if (std::abs(a[2]) > 1e-6 &&
            std::abs(a[2] + a[8]) < std::abs(a[2]) * 0.2)
          info.hasXZSwap = true;
      }
      break;
    }
  }

  return info;
}

/////////////////////////////////////////////////
UpAxis ParseUpAxis(const std::string &_upAxisStr)
{
  if (_upAxisStr == "Y_UP")   return UpAxis::Y_UP;
  if (_upAxisStr == "Y_DOWN") return UpAxis::Y_DOWN;
  if (_upAxisStr == "X_UP")   return UpAxis::X_UP;
  if (_upAxisStr == "X_DOWN") return UpAxis::X_DOWN;
  if (_upAxisStr == "Z_DOWN") return UpAxis::Z_DOWN;
  return UpAxis::Z_UP;
}

/////////////////////////////////////////////////
MeshDataSimple ConvertToZUp(const MeshDataSimple &_mesh, UpAxis _source)
{
  if (_source == UpAxis::Z_UP)
    return _mesh;  // 已经是目标坐标系

  MeshDataSimple out = _mesh;

  /// 轴重映射到 (x, y, z)。z 分量符号 = +1（正方向朝上）或 -1（需翻转）。
  /// 目标：z 轴朝上（正方向）。
  /// 例如 Z_DOWN → 保持 z 分量但取反；Y_UP → y 移到 z。
  int srcIdx = 0;     // 源轴映射到目标 x
  int srcIdy = 1;     // 源轴映射到目标 y
  int srcIdz = 2;     // 源轴映射到目标 z
  double signZ = 1.0; // 目标 z 分量的符号

  switch (_source) {
    case UpAxis::Z_DOWN: srcIdz = 2; signZ = -1.0; break;
    case UpAxis::Y_UP:   srcIdz = 1; srcIdy = 2;   break;
    case UpAxis::Y_DOWN: srcIdz = 1; srcIdy = 2; signZ = -1.0; break;
    case UpAxis::X_UP:   srcIdz = 0; srcIdx = 2;   break;
    case UpAxis::X_DOWN: srcIdz = 0; srcIdx = 2; signZ = -1.0; break;
    default: break; // Z_UP already returned
  }

  /// 顶点变换
  for (auto &v : out.vertices) {
    double src[3] = {v.X(), v.Y(), v.Z()};
    v.X() = src[srcIdx];
    v.Y() = src[srcIdy];
    v.Z() = src[srcIdz] * signZ;
  }

  /// 三角形顶点坐标变换 + 绕序补偿
  /// z→-z 反射后，叉积 n_y 符号翻转（Δy 不变，dz 取反），需交换 v1↔v2 恢复朝外法线
  for (auto &tri : out.triangles) {
    double s[3];
    s[0] = tri.v0.X(); s[1] = tri.v0.Y(); s[2] = tri.v0.Z();
    tri.v0.X() = s[srcIdx]; tri.v0.Y() = s[srcIdy]; tri.v0.Z() = s[srcIdz] * signZ;
    s[0] = tri.v1.X(); s[1] = tri.v1.Y(); s[2] = tri.v1.Z();
    tri.v1.X() = s[srcIdx]; tri.v1.Y() = s[srcIdy]; tri.v1.Z() = s[srcIdz] * signZ;
    s[0] = tri.v2.X(); s[1] = tri.v2.Y(); s[2] = tri.v2.Z();
    tri.v2.X() = s[srcIdx]; tri.v2.Y() = s[srcIdy]; tri.v2.Z() = s[srcIdz] * signZ;
    if (signZ < 0.0)
      std::swap(tri.v1, tri.v2);
  }

/// 法线变换
  for (auto &n : out.normals) {
    double src[3] = {n.X(), n.Y(), n.Z()};
    n.X() = src[srcIdx];
    n.Y() = src[srcIdy];
    n.Z() = src[srcIdz] * signZ;
  }

  return out;
}

} // namespace keship

/////////////////////////////////////////////////
/// 规范化缓存实现 — 任意格式模型 → 标准化 OBJ（Z-UP 右手系）
/////////////////////////////////////////////////

namespace
{
  /// 文件名脱敏（只保留 a-z A-Z 0-9 _ -）
  std::string NormalizeCacheName(const std::string &_name)
  {
    std::string out;
    out.reserve(_name.size());
    for (char c : _name)
    {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
          || (c >= '0' && c <= '9') || c == '_' || c == '-')
        out += c;
      else
        out += '_';
    }
    if (out.empty())
      out = "unnamed";
    return out;
  }

  /// 文件"印章"：mtime + size，用于缓存失效判定
  std::string FileStamp(const std::string &_path)
  {
    std::error_code ec;
    auto ft = std::filesystem::last_write_time(_path, ec);
    std::ostringstream oss;
    if (!ec)
      oss << ft.time_since_epoch().count();
    else
      oss << "0";
    oss << ":";
    auto sz = std::filesystem::file_size(_path, ec);
    oss << (ec ? 0 : sz);
    return oss.str();
  }

  /// SHA-1（RFC 3174）— 缓存 key 使用，确定性哈希
  struct Sha1Ctx
  {
    uint32_t h[5];
    uint64_t bits;
    unsigned char buf[64];
    size_t bufLen;
  };

  void Sha1Init(Sha1Ctx &c)
  {
    c.h[0] = 0x67452301; c.h[1] = 0xEFCDAB89;
    c.h[2] = 0x98BADCFE; c.h[3] = 0x10325476; c.h[4] = 0xC3D2E1F0;
    c.bits = 0; c.bufLen = 0;
  }

  static inline uint32_t Sha1Rotl(uint32_t x, int n)
  {
    return (x << n) | (x >> (32 - n));
  }

  void Sha1Block(Sha1Ctx &c, const unsigned char *p)
  {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16)
           | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 80; ++i)
      w[i] = Sha1Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4];
    for (int i = 0; i < 80; ++i)
    {
      uint32_t f, k;
      if (i < 20)      { f = (b & cc) | ((~b) & d);   k = 0x5A827999; }
      else if (i < 40) { f = b ^ cc ^ d;              k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
      else             { f = b ^ cc ^ d;              k = 0xCA62C1D6; }
      uint32_t tmp = Sha1Rotl(a, 5) + f + e + k + w[i];
      e = d; d = cc; cc = Sha1Rotl(b, 30); b = a; a = tmp;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d; c.h[4] += e;
  }

  void Sha1Update(Sha1Ctx &c, const void *data, size_t len)
  {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    c.bits += uint64_t(len) * 8;
    while (len > 0)
    {
      size_t take = 64 - c.bufLen;
      if (take > len) take = len;
      std::memcpy(c.buf + c.bufLen, p, take);
      c.bufLen += take; p += take; len -= take;
      if (c.bufLen == 64)
      {
        Sha1Block(c, c.buf);
        c.bufLen = 0;
      }
    }
  }

  std::string Sha1Hex(const std::string &_input)
  {
    Sha1Ctx c;
    Sha1Init(c);
    Sha1Update(c, _input.data(), _input.size());

    uint64_t bits = c.bits;
    unsigned char pad = 0x80;
    Sha1Update(c, &pad, 1);
    unsigned char zero = 0;
    while (c.bufLen != 56)
      Sha1Update(c, &zero, 1);
    unsigned char lenBytes[8];
    for (int i = 0; i < 8; ++i)
      lenBytes[i] = static_cast<unsigned char>(bits >> (56 - i * 8));
    Sha1Update(c, lenBytes, 8);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 5; ++i)
      oss << std::setw(8) << c.h[i];
    return oss.str();
  }

  /// ★ 2026-09-25 性能修复（④）配套：sidecar 快速路径的"源部分 key"。
  /// 只依赖源文件本身（绝对路径 + mtime + size），**不依赖 mesh 解析结果** ——
  /// 正是它让"命中判定"能提到 LoadMeshFromFile 之前。
  std::string SourceKey8(const std::string &_srcPath)
  {
    /// 注意：本函数在匿名 namespace 内，MeshFileCacheKey 属于 keship ⇒ 必须限定
    return Sha1Hex(keship::MeshFileCacheKey(_srcPath) + "|gzmap1").substr(0, 8);
  }

  /// ★ 2026-09-25 性能修复（④）：写 sidecar（供下次**免解析**命中缓存）。
  /// 内容两行：keyInput 原文 + 纹理绝对路径（无纹理则写空行）。
  /// ⚠ 两个调用点的时序要求**不同**，见各自位置注释：
  ///   · 缓存**命中**分支：必须补写 —— 否则本次改动前生成的老缓存永远没有 sidecar，
  ///     快速路径永不生效（命中分支在写盘之前就 return 了）；
  ///   · 缓存**生成**分支：必须在 CleanupOldCaches **之后**调用，否则被清理规则删掉。
  void WriteCacheSidecar(const std::string &_cacheDir, const std::string &_srcName,
                         const std::string &_srcPath,
                         const std::string &_keyInput, const std::string &_texAbs)
  {
    std::ofstream kf(_cacheDir + NormalizeCacheName(_srcName) + "_"
                     + SourceKey8(_srcPath) + ".key");
    if (kf.is_open())
      kf << _keyInput << "\n" << _texAbs << "\n";
  }

  /// 将标准系网格写为"无变换"OBJ（顶点原样输出，不引入坐标变换）
  bool WriteNormalizedObj(const std::string &_objPath,
                          const std::string &_mtlBase,
                          const keship::MeshDataSimple &_mesh)
  {
    std::ofstream file(_objPath);
    if (!file.is_open())
    {
      std::cerr << "MeshUtils: 无法写入缓存 OBJ " << _objPath << std::endl;
      return false;
    }

    file << "# Normalized mesh generated by keship (Z-UP right-handed)\n";
    file << "mtllib " << _mtlBase << ".mtl\n";
    file << "o ke_mesh_normalized\n";
    file << "usemtl material_1\n";

    for (const auto &v : _mesh.vertices)
      file << "v " << v.X() << " " << v.Y() << " " << v.Z() << "\n";
    for (const auto &uv : _mesh.texCoords)
      file << "vt " << uv.first << " " << uv.second << "\n";
    for (const auto &n : _mesh.normals)
      file << "vn " << n.X() << " " << n.Y() << " " << n.Z() << "\n";

    int vBase = 1;
    const bool hasVT = !_mesh.texCoords.empty();
    const bool hasVN = !_mesh.normals.empty();
    for (const auto &tri : _mesh.triangleIndices)
    {
      file << "f ";
      for (unsigned k = 0; k < 3; ++k)
      {
        if (k > 0)
          file << " ";
        file << (tri[k] + vBase);
        if (hasVT && hasVN)
          file << "/" << (tri[k] + vBase) << "/" << (tri[k] + vBase);
        else if (hasVT)
          file << "/" << (tri[k] + vBase);
        else if (hasVN)
          file << "//" << (tri[k] + vBase);
      }
      file << "\n";
    }

    file.close();
    return true;
  }

  /// 写 MTL（漫反射纹理按相对文件名引用，与 OBJ 同目录）
  bool WriteNormalizedMtl(const std::string &_mtlPath,
                          const std::string &_texFileName)
  {
    std::ofstream file(_mtlPath);
    if (!file.is_open())
    {
      std::cerr << "MeshUtils: 无法写入缓存 MTL " << _mtlPath << std::endl;
      return false;
    }
    file << "newmtl material_1\n";
    file << "Kd 0.7 0.7 0.7\n";
    file << "Ks 0.05 0.05 0.05\n";
    file << "Ns 10.0\n";
    if (!_texFileName.empty())
      file << "map_Kd " << _texFileName << "\n";
    file.close();
    return true;
  }

  /// 清理同一源模型（同名前缀）的陈旧缓存文件
  void CleanupOldCaches(const std::string &_dir, const std::string &_prefix,
                        const std::string &_keepBase)
  {
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(_dir, ec))
    {
      if (!entry.is_regular_file(ec))
        continue;
      std::string fn = entry.path().filename().string();
      if (fn.rfind(_prefix, 0) != 0)
        continue;
      if (fn == _keepBase + ".obj" || fn == _keepBase + ".mtl")
        continue;
      if (fn.rfind(_keepBase + "_", 0) == 0)
        continue;
      std::filesystem::remove(entry.path(), ec);
    }
  }
} // namespace

namespace keship
{

/////////////////////////////////////////////////
std::string MeshFileCacheKey(const std::string &_filePath)
{
  std::error_code ec;
  std::string abs = std::filesystem::absolute(_filePath, ec).string();
  if (ec || abs.empty())
    abs = _filePath;
  return Sha1Hex(abs + "|" + FileStamp(_filePath));
}

/////////////////////////////////////////////////
bool LoadNormalizedObjVertices(const std::string &_objPath,
                               std::vector<gz::math::Vector3d> &_out)
{
  _out.clear();
  if (_objPath.empty())
    return false;

  std::ifstream f(_objPath);
  if (!f.is_open())
  {
    std::cerr << "MeshUtils: 无法打开规范化 OBJ " << _objPath << std::endl;
    return false;
  }

  /// 只认顶点行 "v x y z"。规范化 OBJ 由 WriteNormalizedObj 生成，格式固定；
  /// 其它行（vt / vn / f / o / usemtl / mtllib / #）一律跳过。
  /// 用 strtod 而非 istringstream：wamv 有 230 万行，istringstream 的构造/析构
  /// 开销会让"流式"失去意义。
  std::string line;
  line.reserve(160);
  while (std::getline(f, line))
  {
    if (line.size() < 5 || line[0] != 'v' || (line[1] != ' ' && line[1] != '\t'))
      continue;
    const char *p = line.c_str() + 1;
    char *end = nullptr;
    const double x = std::strtod(p, &end);
    if (end == p) continue;
    p = end;
    const double y = std::strtod(p, &end);
    if (end == p) continue;
    p = end;
    const double z = std::strtod(p, &end);
    if (end == p) continue;
    _out.emplace_back(x, y, z);
  }
  return !_out.empty();
}

/////////////////////////////////////////////////
/// ★ 2026-09-28：3D 凸包（qhull 重入版）→ 三角面（顶点坐标）
/// @return 三角面列表（每面 3 个顶点，坐标已复制）；失败返回空
static std::vector<std::array<gz::math::Vector3d, 3>>
ConvexHullTriangles(const std::vector<gz::math::Vector3d> &_verts)
{
  std::vector<std::array<gz::math::Vector3d, 3>> out;
  if (_verts.size() < 4)
    return out;   ///< 3D 凸包至少 4 个顶点

  /// ★★ 2026-09-28 修（**必须保留**）：先按**容差合并重合点**，再算凸包，且**不使用 `QJ`**。
  ///   原实现用 `QJ`(joggle)：它把所有输入点轻微抖动，使大量"几乎共面"的点变成真极点
  ///   ⇒ 凸包面片里出现大量**针状三角形**（实测 1,410 面中 115 个 shape ratio < 1e-3，
  ///   最小 4.2e-6）。而 **dartsim/ODE 的 trimesh 建树无法处理这类面片 ⇒ gz sim SIGSEGV**
  ///   （实测 `--collision hull` 单实例 spawn 即 exit 139 / 地址 0x4；
  ///     同一文件作 `<visual>` 完全正常、手写四面体 OBJ 作 collision 也正常
  ///     ⇒ 问题只在"针状面 + collision 路径"）。
  ///   改为：按 1e-6·包围盒对角线 合并重合点 + 纯 `Qt` 三角化。
  ///   若 qhull 仍失败，上层会回退原 collision（既有安全网）。
  gz::math::Vector3d lo = _verts[0], hi = _verts[0];
  for (const auto &v : _verts)
  {
    /// gz-math 的 `Vector3::Min/Max` 返回 void（就地修改）⇒ 逐分量比较
    if (v.X() < lo.X()) lo.X(v.X());
    if (v.Y() < lo.Y()) lo.Y(v.Y());
    if (v.Z() < lo.Z()) lo.Z(v.Z());
    if (v.X() > hi.X()) hi.X(v.X());
    if (v.Y() > hi.Y()) hi.Y(v.Y());
    if (v.Z() > hi.Z()) hi.Z(v.Z());
  }
  const double tol = std::max(1e-12, 1e-6 * (hi - lo).Length());
  std::map<std::array<long long, 3>, size_t> seen;
  std::vector<gz::math::Vector3d> merged;
  merged.reserve(_verts.size());
  for (const auto &v : _verts)
  {
    const std::array<long long, 3> key{
        (long long)std::llround(v.X() / tol),
        (long long)std::llround(v.Y() / tol),
        (long long)std::llround(v.Z() / tol)};
    if (seen.emplace(key, merged.size()).second)
      merged.push_back(v);
  }
  if (merged.size() < 4)
    return out;   ///< 合并后不足 4 点 ⇒ 退化，交给上层回退
  /// qhull 要的是连续 double 数组（dim=3）
  std::vector<coordT> pts;
  pts.reserve(merged.size() * 3);
  for (const auto &v : merged)
  {
    pts.push_back(v.X());
    pts.push_back(v.Y());
    pts.push_back(v.Z());
  }

  qhT qh_qh;
  qhT *qh = &qh_qh;
  QHULL_LIB_CHECK
  qh_zero(qh, stderr);

  FILE *outFile = tmpfile();
  FILE *errFile = tmpfile();
  if (!outFile || !errFile)
  {
    if (outFile) fclose(outFile);
    if (errFile) fclose(errFile);
    return out;
  }

  /// "Qt" = 三角化输出。**刻意不用 `QJ`** —— 见上方注释：joggle 会造出针状面 ⇒ dartsim 崩溃。
  char flags[] = "qhull Qt";
  const int rc = qh_new_qhull(qh, 3, (int)(pts.size() / 3), pts.data(), 0,
                              flags, outFile, errFile);
  if (rc == 0)
  {
    /// 遍历 facets（跳过上包络），把每个多边形面扇形三角化
    facetT *facet;
    vertexT *vertex, **vertexp;
    FORALLfacets
    {
      if (facet->upperdelaunay) continue;
      std::vector<gz::math::Vector3d> poly;
      FOREACHvertex_(facet->vertices)
      {
        const int id = qh_pointid(qh, vertex->point);
        if (id >= 0 && id < (int)_verts.size())
          poly.push_back(_verts[id]);
      }
      for (size_t i = 1; i + 1 < poly.size(); ++i)
        out.push_back({poly[0], poly[i], poly[i + 1]});
    }
  }

  fclose(outFile);
  fclose(errFile);
  qh_freeqhull(qh, !qh_ALL);
  int curlong = 0, totlong = 0;
  qh_memfreeshort(qh, &curlong, &totlong);
  return out;
}

/////////////////////////////////////////////////
std::string GenerateConvexHullCache(const std::string &_srcPath)
{
  if (_srcPath.empty())
    return "";

  std::error_code ec;
  const std::string srcStem = NormalizeCacheName(
      std::filesystem::path(_srcPath).stem().string());
  /// ⚠ 2026-09-28 修：源路径若**已在 `normalized/` 下**（`ApplyCollisionModeToSdf`
  ///   拿到的正是规范化 OBJ 自身），就放**同目录** —— 否则会拼出
  ///   `normalized/normalized/`（实测踩到）。
  const bool inCacheDir =
      _srcPath.find("/normalized/") != std::string::npos ||
      _srcPath.find("\\normalized\\") != std::string::npos;
  const std::string cdir = inCacheDir
      ? std::filesystem::path(_srcPath).parent_path().string() + "/"
      : std::filesystem::path(_srcPath).parent_path().string() + "/normalized/";
  const std::string hullPath =
      /// ⚠ 版本号进文件名：修了退化面过滤后必须换名，否则旧坏缓存被直接命中
      cdir + srcStem + "_" + SourceKey8(_srcPath) + "_hull_v4.obj";

  /// 命中已有凸包缓存（文件名含源 stamp ⇒ 源变化自动失效）
  if (std::filesystem::exists(hullPath, ec))
  {
    std::cout << "MeshUtils: 命中凸包缓存 " << hullPath << std::endl;
    return hullPath;
  }

  /// 载入源网格 → 算凸包
  MeshDataSimple mesh = LoadMeshFromFile(_srcPath);
  if (mesh.vertices.empty())
  {
    std::cerr << "MeshUtils: 凸包生成失败 — 网格为空: " << _srcPath << std::endl;
    return "";
  }
  const auto tris = ConvexHullTriangles(mesh.vertices);
  if (tris.empty())
  {
    std::cerr << "MeshUtils: 凸包生成失败 — qhull 未产出三角面: " << _srcPath << std::endl;
    return "";
  }

  /// 写 OBJ（顶点 + 面；无纹理 —— 碰撞体不需要渲染）
  std::error_code ec2;
  if (!std::filesystem::exists(cdir, ec2))
    std::filesystem::create_directories(cdir, ec2);

  std::ofstream ofs(hullPath);
  if (!ofs)
  {
    std::cerr << "MeshUtils: 无法写入凸包 OBJ " << hullPath << std::endl;
    return "";
  }
  ofs << "# 凸包（qhull，仅用于 <collision>）generated by keship 2026-09-28\n";
  ofs << "o ke_convex_hull\n";

  /// 顶点去重（凸包顶点数很少，用 map 精确去重）
  std::map<std::array<double, 3>, int> vmap;
  std::vector<gz::math::Vector3d> verts;
  std::vector<std::array<int, 3>> faces;
  size_t droppedDegenerate = 0, droppedSliver = 0;
  for (const auto &t : tris)
  {
    std::array<int, 3> idx{};
    for (int k = 0; k < 3; ++k)
    {
      const std::array<double, 3> key{t[k].X(), t[k].Y(), t[k].Z()};
      auto it = vmap.find(key);
      if (it == vmap.end())
      {
        const int id = (int)verts.size() + 1;   ///< OBJ 是 1-based
        vmap[key] = id;
        verts.push_back(t[k]);
        idx[k] = id;
      }
      else
      {
        idx[k] = it->second;
      }
    }

    /// ★★ 2026-09-28 修（**必须保留**）：丢弃退化三角形。
    ///   qhull 的 `QJ`(joggle) 与扇形三角化都会产出"两个顶点坐标完全相同"的面片，
    ///   经上面的**精确去重**后即变成 `f a b b`（重复索引）—— 零面积面片会让
    ///   ODE 的 trimesh 算法线时除零 ⇒ **dartsim 直接 SIGSEGV**
    ///   （实测：`--collision hull` **单实例** spawn 即 `gz sim` exit 139 / 地址 0x4；
    ///     旧输出 1,708 面中 296 个是这种退化面，而全细节 OBJ 为 0 个）。
    if (idx[0] == idx[1] || idx[1] == idx[2] || idx[0] == idx[2])
    {
      ++droppedDegenerate;
      continue;
    }
    /// 第二道防线：极薄"针状"面片（面积/最长边² < 1e-4）—— dartsim/ODE 对这类面片不安全
    const auto &A = verts[idx[0] - 1];
    const auto &B = verts[idx[1] - 1];
    const auto &C = verts[idx[2] - 1];
    const double area = 0.5 * ((B - A).Cross(C - A)).Length();
    double l2 = (A - B).SquaredLength();
    const double l2b = (B - C).SquaredLength();
    const double l2c = (A - C).SquaredLength();
    if (l2b > l2) l2 = l2b;
    if (l2c > l2) l2 = l2c;
    if (l2 <= 0.0 || area < 1e-4 * l2)
    {
      ++droppedSliver;
      continue;
    }
    faces.push_back(idx);
  }
  for (const auto &v : verts)
    ofs << "v " << v.X() << " " << v.Y() << " " << v.Z() << "\n";

  /// ★★ 2026-09-28 修（**必须保留**）：**凸包 OBJ 必须写 `vn` 法线**。
  ///   实测同一份凸包几何（378 顶点 / 1095 面）：
  ///     · 只写 `v`+`f`                ⇒ dartsim/ODE 的 collision 建树 **SIGSEGV**
  ///       （`gz sim` exit 139 / 地址 0x4；同一文件作 `<visual>` 却完全正常）；
  ///     · 写 `vn` + `f a//n b//n c//n` ⇒ **完全正常**。
  ///   旁证：全细节 OBJ 自带 `vn`（200,390 条）加载正常；手写四面体虽无 `vn` 但仅 4 面亦正常
  ///   ⇒ 触发条件是"网格有一定规模 + 缺 `vn`"。**凸包 OBJ 不能省法线。**
  ///   逐面一条面法线即可（碰撞体不需要平滑法线）。
  for (const auto &f : faces)
  {
    const auto &A = verts[f[0] - 1];
    const auto &B = verts[f[1] - 1];
    const auto &C = verts[f[2] - 1];
    gz::math::Vector3d nrm = (B - A).Cross(C - A);
    const double len = nrm.Length();
    if (len > 0.0)
      nrm = nrm / len;
    ofs << "vn " << nrm.X() << " " << nrm.Y() << " " << nrm.Z() << "\n";
  }
  for (size_t i = 0; i < faces.size(); ++i)
    ofs << "f " << faces[i][0] << "//" << (i + 1) << " "
        << faces[i][1] << "//" << (i + 1) << " "
        << faces[i][2] << "//" << (i + 1) << "\n";
  ofs.close();

  std::cout << "MeshUtils: 凸包缓存已生成 " << hullPath << "（"
            << verts.size() << " 顶点, " << faces.size() << " 三角面；"
            << "丢弃退化面 " << droppedDegenerate << " / 针状面 " << droppedSliver << "；"
            << "源网格 " << mesh.vertices.size() << " 顶点）" << std::endl;
  return hullPath;
}

/////////////////////////////////////////////////
std::string NormalizeMeshToSDFCache(const std::string &_srcPath)
{
  if (_srcPath.empty())
    return "";

  // A 方案：已是规范化缓存产物（models_normalized/ 目录段）且文件存在 →
  // 直接返回原路径（跳过），避免 ksm 载入时把缓存 OBJ 再次当作源规范化，
  // 造成链式版本堆积（载入-保存循环每轮新增 ~22MB）
  const bool inCacheDir =
      _srcPath.find("/normalized/") != std::string::npos ||
      _srcPath.find("\\normalized\\") != std::string::npos;
  if (inCacheDir)
  {
    std::error_code ec;
    if (std::filesystem::exists(_srcPath, ec))
      return _srcPath;
  }

  /// ★★★ 2026-09-25 性能修复（④）：**缓存命中判定前置（sidecar 快速路径）**。
  ///
  /// 原顺序是「LoadMeshFromFile（全量解析）→ 用解析出的纹理路径算 key
  /// → 才检查缓存」⇒ **"命中缓存"只省了写盘，没省解析**（实测 wamv 125MB
  /// 每次调用仍花 3.8s 解析）；而船的 base SDF 有 visual+collision 两个 `<uri>`，
  /// 单次加载要付两次这个代价（③ 已把两次并成一次）。
  ///
  /// 修法：把"算 key 所需输入"落盘成 sidecar（`<stem>_<srcKey8>.key`，两行：
  ///   第 1 行 = 当时的 keyInput 原文；第 2 行 = 纹理绝对路径 texAbs）。
  /// 命中判定只需 **读这个小文件 + stat**，无需解析网格：
  ///   keyInput' = MeshFileCacheKey(源) + "|gzmap1" [+ "|" + MeshFileCacheKey(texAbs)]
  /// 再校验 `<stem>_<sha1(keyInput')[0..8]>.obj/.mtl` 存在。
  ///
  /// ⚠ **失效语义与原来严格一致**（本修法的关键）：
  ///   · 源文件变化     → MeshFileCacheKey(源) 变     ⇒ keyInput' 变 ⇒ 未命中；
  ///   · **纹理文件变化** → MeshFileCacheKey(texAbs) 变 ⇒ 同上 ⇒ 未命中并重建；
  ///   · 纹理被删       → FileStamp 返回 "0"          ⇒ 同样未命中 ⇒ 走原逻辑降级纯色。
  /// 因此 sidecar **不能只存 keyInput** —— 必须同时存 texAbs，才能重算纹理 stamp。
  {
    std::error_code ec;
    const std::string srcStem = NormalizeCacheName(
        std::filesystem::path(_srcPath).stem().string());
    const std::string cdir =
        std::filesystem::path(_srcPath).parent_path().string() + "/normalized/";
    const std::string keyFile =
        cdir + srcStem + "_" + SourceKey8(_srcPath) + ".key";
    if (std::filesystem::exists(keyFile, ec))
    {
      std::ifstream kf(keyFile);
      if (kf.is_open())
      {
        std::string keyInputStored, texAbsStored;
        std::getline(kf, keyInputStored);
        std::getline(kf, texAbsStored);
        if (!keyInputStored.empty())
        {
          /// 用 sidecar 里的纹理路径**重算** keyInput（与写盘时同一公式）
          std::string keyInputNow = MeshFileCacheKey(_srcPath) + "|gzmap1";
          if (!texAbsStored.empty())
            keyInputNow += "|" + MeshFileCacheKey(texAbsStored);
          const std::string k8 = Sha1Hex(keyInputNow).substr(0, 8);
          const std::string objHit = cdir + srcStem + "_" + k8 + ".obj";
          const std::string mtlHit = cdir + srcStem + "_" + k8 + ".mtl";
          if (std::filesystem::exists(objHit, ec) &&
              std::filesystem::exists(mtlHit, ec))
          {
            std::cout << "MeshUtils: 命中规范化缓存（sidecar 快速路径）"
                      << objHit << std::endl;
            return objHit;
          }
        }
      }
    }
  }

  MeshDataSimple mesh = LoadMeshFromFile(_srcPath);
  if (mesh.vertices.empty() || mesh.triangles.empty())
  {
    std::cerr << "MeshUtils: 规范化失败 — 网格为空或加载失败: "
              << _srcPath << std::endl;
    return "";
  }

  /// 1. 解析纹理绝对路径（相对路径以源模型目录为基准）
  std::string texAbs;
  {
    std::error_code ec;
    std::string tp = mesh.texturePath;
    if (!tp.empty() && tp.rfind("file://", 0) == 0)
      tp = tp.substr(7);
    if (!tp.empty())
    {
      std::filesystem::path texP(tp);
      if (!texP.is_absolute())
      {
        std::filesystem::path srcP(_srcPath);
        texP = srcP.parent_path() / texP;
      }
      texAbs = texP.lexically_normal().string();
      if (!std::filesystem::exists(texAbs, ec))
      {
        std::cerr << "MeshUtils: 纹理文件不存在，降级为纯色渲染: "
                  << texAbs << std::endl;
        mesh.texturePath.clear();
        texAbs.clear();
      }
    }
  }

  /// 2. 缓存 key：源文件 + 纹理文件（mtime+size），变化即失效
  std::string keyInput = MeshFileCacheKey(_srcPath);
  keyInput += "|gzmap1";
  if (!texAbs.empty())
    keyInput += "|" + MeshFileCacheKey(texAbs);
  std::string key = Sha1Hex(keyInput).substr(0, 8);

  /// 3. 缓存路径（源模型目录下 normalized/，与源同生命周期，可整体删除重建）
  std::string cacheDir =
      std::filesystem::path(_srcPath).parent_path().string() + "/normalized/";
  {
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec))
      std::filesystem::create_directories(cacheDir, ec);
  }

  std::string srcName = std::filesystem::path(_srcPath).stem().string();
  std::string base = NormalizeCacheName(srcName) + "_" + key;
  std::string objPath = cacheDir + base + ".obj";
  std::string mtlPath = cacheDir + base + ".mtl";

  /// 4. 命中缓存（key 已嵌入文件名，OBJ+MTL 齐全即有效）
  {
    std::error_code ec;
    if (std::filesystem::exists(objPath, ec)
        && std::filesystem::exists(mtlPath, ec))
    {
      std::cout << "MeshUtils: 命中规范化缓存 " << objPath << std::endl;
      /// ★ 2026-09-25（④）：**命中分支也要补写 sidecar**。
      /// 老缓存（本次改动前生成的）没有 sidecar；若只在本函数末尾的"生成"分支写，
      /// 那么命中路径永远走不到那里 ⇒ 快速路径永不生效（探针实测过这个缺陷）。
      /// 这里没有清理风险（不经过 CleanupOldCaches）。
      WriteCacheSidecar(cacheDir, srcName, _srcPath, keyInput, texAbs);
      return objPath;
    }
  }

  /// 5. 生成缓存：OBJ + 纹理副本 + MTL
  MeshDataSimple outMesh = mesh;
  {
    DaeOrientationInfo info = ReadDaeOrientation(_srcPath);
    if (info.upAxis == "Z_UP")
    {
      for (auto &v : outMesh.vertices)
      {
        double x = v.X(), y = v.Y(), z = v.Z();
        v.X() = x; v.Y() = -z; v.Z() = y;
      }
      for (auto &n : outMesh.normals)
      {
        double x = n.X(), y = n.Y(), z = n.Z();
        n.X() = x; n.Y() = -z; n.Z() = y;
      }
      for (auto &t : outMesh.triangles)
      {
        double x = t.v0.X(), y = t.v0.Y(), z = t.v0.Z();
        t.v0.X() = x; t.v0.Y() = -z; t.v0.Z() = y;
        x = t.v1.X(); y = t.v1.Y(); z = t.v1.Z();
        t.v1.X() = x; t.v1.Y() = -z; t.v1.Z() = y;
        x = t.v2.X(); y = t.v2.Y(); z = t.v2.Z();
        t.v2.X() = x; t.v2.Y() = -z; t.v2.Z() = y;
      }
    }
  }
  if (!WriteNormalizedObj(objPath, base, outMesh))
    return "";

  std::string texDestName;
  if (!texAbs.empty())
  {
    std::filesystem::path texP(texAbs);
    texDestName = base + "_tex" + texP.extension().string();
    std::error_code ec;
    std::filesystem::copy_file(texAbs, cacheDir + texDestName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
      std::cerr << "MeshUtils: 纹理复制失败，降级为纯色渲染: "
                << texAbs << " (" << ec.message() << ")" << std::endl;
      texDestName.clear();
    }
  }

  if (!WriteNormalizedMtl(mtlPath, texDestName))
  {
    std::filesystem::remove(objPath);
    return "";
  }

  /// 6. 清理陈旧缓存（同一源模型的旧 key 文件）
  CleanupOldCaches(cacheDir, NormalizeCacheName(srcName) + "_", base);

  /// 6b. ★ 2026-09-25（④）：写 sidecar，供下次**免解析**命中。
  /// ⚠ 必须放在 CleanupOldCaches **之后**（清理规则会删掉不匹配 `<base>_*` 的文件）。
  WriteCacheSidecar(cacheDir, srcName, _srcPath, keyInput, texAbs);

  std::cout << "MeshUtils: 规范化缓存已生成 " << objPath << " ("
            << mesh.vertices.size() << " 顶点, "
            << mesh.triangleIndices.size() << " 三角形"
            << (texDestName.empty() ? ", 无纹理" : ", 含纹理") << ")" << std::endl;
  return objPath;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
MeshCacheCleanupResult CleanupUnusedMeshCache()
{
  MeshCacheCleanupResult result;

  /// 清空 robot_model 下所有 normalized/ 缓存目录（缓存随源同目录，下次加载自动重建）
  const std::string robotDir = KeshipRobotModelDir();
  std::error_code ec;
  if (!std::filesystem::exists(robotDir, ec))
    return result;

  std::filesystem::recursive_directory_iterator end;
  for (std::filesystem::recursive_directory_iterator it(robotDir, ec);
       !ec && it != end; it.increment(ec))
  {
    if (!it->is_directory(ec)) { ec.clear(); continue; }
    if (it->path().filename() != "normalized")
      continue;
    std::filesystem::directory_iterator cend;
    for (std::filesystem::directory_iterator cit(it->path(), ec);
         !ec && cit != cend; cit.increment(ec))
    {
      if (!cit->is_regular_file(ec)) { ec.clear(); continue; }
      result.freedBytes += cit->file_size(ec); ec.clear();
      std::filesystem::remove(cit->path(), ec); ec.clear();
      ++result.deleted;
      std::cout << "MeshUtils: 清理网格缓存 "
                << cit->path().filename().string() << std::endl;
    }
    ec.clear();
  }

  return result;
}

int RemoveStaleMeshFiles(const std::string &_dir, const std::string &_stemPrefix)
{
  if (_dir.empty() || _stemPrefix.empty())
    return 0;

  std::error_code ec;
  if (!std::filesystem::exists(_dir, ec) || ec)
    return 0;

  int deleted = 0;
  std::filesystem::directory_iterator end;
  for (std::filesystem::directory_iterator it(_dir, ec);
       !ec && it != end; it.increment(ec))
  {
    if (!it->is_regular_file(ec)) { ec.clear(); continue; }
    const std::string name = it->path().filename().string();
    /// 只认 <prefix>_<tag>.obj 与旧式 <prefix>.obj —— 同目录可能还有其它来源的
    /// 文件（如 hull_visual.obj / hull_vis_a.mtl），不越界删
    if (name.rfind(_stemPrefix, 0) != 0) continue;
    const size_t p = _stemPrefix.size();
    if (name.size() <= p) continue;
    const char sep = name[p];
    if (sep != '_' && sep != '.') continue;
    /// 用 `<`（不是 `<=`）：`<prefix>.obj` 的长度恰好是 p+4，必须允许
    /// （2026-09-19：探针 `mesh_stale_cleanup_probe.cc` 抓出此处原为 `<=`，
    ///  导致旧式无 tag 文件 hull_vis.obj 漏删）
    if (name.size() < p + 4 ||
        name.compare(name.size() - 4, 4, ".obj") != 0) continue;

    std::filesystem::remove(it->path(), ec);
    if (!ec) ++deleted;
    ec.clear();
  }
  return deleted;
}

} // namespace keship
