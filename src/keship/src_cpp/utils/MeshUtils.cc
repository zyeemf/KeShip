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
#include <set>

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

  /// 清理同一源模型（同名前缀+目录哈希）的陈旧缓存文件
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
std::string NormalizeMeshToSDFCache(const std::string &_srcPath)
{
  if (_srcPath.empty())
    return "";

  // A 方案：已是规范化缓存产物（models_normalized/ 目录段）且文件存在 →
  // 直接返回原路径（跳过），避免 ksm 载入时把缓存 OBJ 再次当作源规范化，
  // 造成链式版本堆积（载入-保存循环每轮新增 ~22MB）
  const bool inCacheDir =
      _srcPath.find("/models_normalized/") != std::string::npos ||
      _srcPath.find("\\models_normalized\\") != std::string::npos;
  if (inCacheDir)
  {
    std::error_code ec;
    if (std::filesystem::exists(_srcPath, ec))
      return _srcPath;
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

  /// 3. 缓存路径（data/models_normalized/）
  std::string cacheDir = KeshipDataDir() + "/models_normalized/";
  {
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec))
      std::filesystem::create_directories(cacheDir, ec);
  }

  std::string srcName = std::filesystem::path(_srcPath).stem().string();
  std::string dirHash = Sha1Hex(
      std::filesystem::path(_srcPath).parent_path().string()).substr(0, 8);
  std::string base = NormalizeCacheName(srcName) + "_" + dirHash + "_" + key;
  std::string objPath = cacheDir + base + ".obj";
  std::string mtlPath = cacheDir + base + ".mtl";

  /// 4. 命中缓存（key 已嵌入文件名，OBJ+MTL 齐全即有效）
  {
    std::error_code ec;
    if (std::filesystem::exists(objPath, ec)
        && std::filesystem::exists(mtlPath, ec))
    {
      std::cout << "MeshUtils: 命中规范化缓存 " << objPath << std::endl;
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
  CleanupOldCaches(cacheDir, NormalizeCacheName(srcName) + "_" + dirHash + "_",
                   base);

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

  /// 1. 收集所有 ksm 引用的缓存家族基名（models_all/ks_world 递归 + ks_model 散文件）
  std::set<std::string> referenced;
  const std::string needle = "models_normalized/";
  std::vector<std::string> scanDirs;
  scanDirs.push_back(KeshipWorldsDir());                   // models_all/ks_world
  scanDirs.push_back(KeshipKsmDir());                      // models_all/ks_model
  /// 过渡期：旧目录若仍存在（迁移验证阶段）也加入扫描，防误删仍被旧 ksm 引用的缓存
  {
    const char *legacyDirs[] = {"/models_ksw", "/models_ksm"};
    std::error_code lec;
    for (const char *ld : legacyDirs)
    {
      const std::string legacy = KeshipHomeWsDir() + ld;
      if (std::filesystem::exists(legacy, lec))
        scanDirs.push_back(legacy);
      lec.clear();
    }
  }

  std::error_code ec;
  std::filesystem::recursive_directory_iterator end;
  for (const auto &dir : scanDirs)
  {
    if (!std::filesystem::exists(dir, ec)) { ec.clear(); continue; }
    for (std::filesystem::recursive_directory_iterator it(dir, ec);
         !ec && it != end; it.increment(ec))
    {
      if (!it->is_regular_file(ec)) { ec.clear(); continue; }
      const std::string fname = it->path().filename().string();
      if (fname.size() <= 4 ||
          fname.compare(fname.size() - 4, 4, ".ksm") != 0)
        continue;

      std::ifstream ifs(it->path());
      if (!ifs) continue;
      std::string content((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());
      size_t pos = 0;
      while ((pos = content.find(needle, pos)) != std::string::npos)
      {
        pos += needle.size();
        const size_t endObj = content.find(".obj", pos);
        if (endObj != std::string::npos && endObj - pos < 200)
          referenced.insert(content.substr(pos, endObj - pos));
      }
    }
  }

  /// 2. 遍历缓存目录，先收集未引用家族，再统一删除（防迭代器失效）
  const std::string cacheDir = KeshipDataDir() + "/models_normalized/";
  if (!std::filesystem::exists(cacheDir, ec)) return result;
  {
    std::vector<std::filesystem::path> toRemove;
    std::vector<uint64_t> sizes;
    std::filesystem::directory_iterator end2;
    for (std::filesystem::directory_iterator it(cacheDir, ec);
         !ec && it != end2; it.increment(ec))
    {
      if (!it->is_regular_file(ec)) { ec.clear(); continue; }

      /// 家族基名：剥扩展名；仅当末段为 "_tex"（纹理副本）时再剥该段，
      /// 避免源名本身含 "_tex" 的模型（obj 文件名末段非 _tex）被误剥
      std::string base = it->path().filename().string();
      const size_t dot = base.rfind('.');
      if (dot != std::string::npos)
        base = base.substr(0, dot);
      const std::string texMark = "_tex";
      if (base.size() > texMark.size() &&
          base.compare(base.size() - texMark.size(), texMark.size(),
                       texMark) == 0)
        base = base.substr(0, base.size() - texMark.size());

      if (referenced.count(base) == 0)
      {
        toRemove.push_back(it->path());
        sizes.push_back(it->file_size(ec));
        ec.clear();
      }
      else
      {
        ++result.kept;
      }
    }
    for (size_t i = 0; i < toRemove.size(); ++i)
    {
      std::filesystem::remove(toRemove[i], ec);
      ec.clear();
      ++result.deleted;
      result.freedBytes += sizes[i];
      std::cout << "MeshUtils: 清理未引用缓存 " << toRemove[i].filename().string()
                << std::endl;
    }
  }
  return result;
}

} // namespace keship
