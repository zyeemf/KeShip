/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 网格工具类 — 统一的 Assimp mesh 加载 */
/// ZhangYu 创建 2026.5.5

#ifndef MESH_UTILS_HH_
#define MESH_UTILS_HH_

#include <string>
#include <vector>
#include <array>
#include <cstdint>

#include <gz/math/Vector3.hh>

namespace keship
{

/// @brief 3D 三角形面片（三个顶点坐标）
struct Triangle3D
{
  gz::math::Vector3d v0, v1, v2;
};

/// @brief 从文件加载的网格数据
struct MeshDataSimple
{
  std::vector<gz::math::Vector3d> vertices;   // 顶点坐标
  std::vector<Triangle3D> triangles;           // 三角形面片
  std::vector<gz::math::Vector3d> normals;    // 顶点法线（可选）
  std::vector<std::pair<double,double>> texCoords;     // 每顶点 UV
  std::string texturePath;                             // diffuse 纹理路径
  std::vector<std::array<unsigned, 3>> triangleIndices; // 三角形顶点索引
};

/// @brief 源模型的"朝上"约定。6 种，完整覆盖所有建模工具。
enum class UpAxis {
  X_UP, X_DOWN,
  Y_UP, Y_DOWN,
  Z_UP, Z_DOWN
};

/// @brief DAE 文件方向元数据
struct DaeOrientationInfo
{
  std::string upAxis;   // "X_UP", "Y_UP", "Z_UP", 或空（非 DAE / 未找到）
  bool hasYZSwap;        // 是否存在绕 X 轴 ±90° 旋转的 Y↔Z 交换矩阵
  bool hasXZSwap{false}; // 是否存在绕 Y 轴 ±90° 旋转的 X↔Z 交换矩阵
};

/// @brief 读取 DAE 文件的 up_axis 声明和 YZ 交换矩阵
/// @return 方向元数据；文件不存在或非 DAE 时返回空状态
DaeOrientationInfo ReadDaeOrientation(const std::string &_filePath);

/// @brief 将 DAE up_axis 字符串转为 UpAxis 枚举
UpAxis ParseUpAxis(const std::string &_upAxisStr);

/// @brief 统一转换到 Gazebo Z_UP 坐标系（顶点坐标 + 顶点法线）
MeshDataSimple ConvertToZUp(const MeshDataSimple &_mesh, UpAxis _source);

/// @brief 使用 Assimp 加载网格文件，返回 Z_UP 坐标系下的网格数据
/// @param _filePath 模型文件路径（支持 DAE, STL, OBJ, GLTF 等）
/// @return 加载的网格数据（已校正为 Z_UP），加载失败时 vertices 为空
MeshDataSimple LoadMeshFromFile(const std::string &_filePath);

/// @brief 计算文件的缓存键（SHA1(绝对路径+修改时间+文件大小)）
/// @param _filePath 文件路径
/// @return 16 进制小写字符串（40 字符）
std::string MeshFileCacheKey(const std::string &_filePath);

/// @brief 将任意格式模型文件规范化为标准化 OBJ（Z-UP 右手系）并缓存
///
/// 处理流程：LoadMeshFromFile（烘焙节点矩阵 + up 轴校正）→ 输出为标准系顶点
/// → 写入"无变换" OBJ + MTL（纹理复制到缓存目录，mtllib 相对引用）。
/// gz-sim 渲染 / 项目预览两条加载路径对该 OBJ 均不再引入坐标变换，
/// 从而与物理配置（CoG/浮力体/传感器）统一到相对 link 的 Z-UP 坐标系。
///
/// 缓存失效：源文件或纹理的修改时间 / 大小变化 → key 变化 → 自动重建。
///
/// @param _srcPath 模型文件绝对路径（DAE/OBJ/STL/glTF 等，assimp 可读）
/// @return 缓存 OBJ 的绝对路径（可直接写入 SDF <uri>），失败返回空串
std::string NormalizeMeshToSDFCache(const std::string &_srcPath);

/// @brief 网格缓存清理结果统计
struct MeshCacheCleanupResult
{
  int deleted = 0;            ///< 删除文件数
  uint64_t freedBytes = 0;    ///< 释放字节数
  int kept = 0;               ///< 保留文件数（被 ksm 引用）
};

/// @brief 清理 models_normalized 中未被任何 ksm 引用的网格缓存（obj/mtl/tex 同族）
///
/// 引用来源：models_ksw（递归，含 <世界>.ksw 多层子文件夹）与 models_ksm 下
/// 全部 *.ksm 中 <uri> 引用的 models_normalized/<基名>.obj。
/// 被引用的家族（obj/mtl/tex）整体保留；未引用的删除并统计。
/// 注意：运行时正在使用但尚未保存为 ksm 的临时缓存可能被清理，
/// 下次加载时由 NormalizeMeshToSDFCache 自动重建（自愈）。
///
/// @return 清理统计
MeshCacheCleanupResult CleanupUnusedMeshCache();

} // namespace keship

#endif // MESH_UTILS_HH_
