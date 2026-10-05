/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * Buoyancy 模块实现
 * ZhangYu 创建 2026.3.4
 */
#include "Buoyancy.hh"
#include "MeshUtils.hh"
#include "DiagnosticLog.hh"

//----------------------------------------------------------
#include <iostream>
#include <set>
//----------------------------------------------------------
//----------------------------------------------------------
#include <gz/math/Quaternion.hh>
//----------------------------------------------------------
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
//----------------------------------------------------------

// using namespace keship;

keship::Buoyancy::Buoyancy()
    : fluideLevel_(0.0), volume_(0.0)
{
}
keship::Buoyancy::~Buoyancy()
{
}

void keship::Buoyancy::setDiagLogPrefix(const std::string &_prefix)
{
    this->diagLogPrefix_ = _prefix;
}

//---------------------------------------------------------------------------------------------------------------------------
/**
 * @brief 从mesh中读取数据
 * @param _mesh 输入的网格模型
 */
void keship::Buoyancy::loadMeshData(const aiMesh *_mesh)
{
    /// 清除旧数据（防止 Configure 重入时累积）
    this->vertices_.clear();
    this->triangles_.clear();
    this->vertexSigns_.clear();
    this->vertexDistances_.clear();
    this->volume_ = 0.0;
    ResetIntersectionIndex();   /// ★ F1：网格换了 ⇒ 旧交点与其索引一律作废（漏清会越界）

    /// 读取顶点数据
    std::cout << "Buoyancy::loadMeshData 顶点数：" << _mesh->mNumVertices << std::endl;
    this->vertices_.reserve(_mesh->mNumVertices);        // 预分配空间，避免后续动态分配
    this->vertexSigns_.reserve(_mesh->mNumVertices);     // 预分配空间，避免后续动态分配
    this->vertexDistances_.reserve(_mesh->mNumVertices); // 预分配空间，避免后续动态分配
    for (unsigned v = 0; v < _mesh->mNumVertices; v++)
    {
        this->vertices_.emplace_back(_mesh->mVertices[v].x, _mesh->mVertices[v].y, _mesh->mVertices[v].z);
        this->vertexSigns_.emplace_back(0);
        this->vertexDistances_.emplace_back(0.0);
    }
    /// 读取顶点法线数据（如果存在）
    std::cout << "Buoyancy::loadMeshData 法线数：" << _mesh->mNumVertices << std::endl;
    std::vector<gz::math::Vector3d> vertexNormals;
    if (_mesh->HasNormals())
    {
        vertexNormals.reserve(_mesh->mNumVertices); // 预分配空间，避免后续动态分配
        for (unsigned i = 0; i < _mesh->mNumVertices; i++)
        {
            vertexNormals.emplace_back(_mesh->mNormals[i].x, _mesh->mNormals[i].y, _mesh->mNormals[i].z);
        }
    }
    /// 读取三角形面数据
    std::cout << "Buoyancy::loadMeshData 三角形数：" << _mesh->mNumFaces << std::endl;
    this->triangles_.reserve(_mesh->mNumFaces); // 预分配空间，避免后续动态分配
    for (size_t i = 0; i < _mesh->mNumFaces; i++)
    {
        aiFace face = _mesh->mFaces[i];
        if (face.mNumIndices != 3)
        {
            std::cout << "Buoyancy::loadMeshData " << " 不是三角形，索引数为：" << face.mNumIndices << std::endl;
            continue; // 只处理三角形面
        }
        int idx0 = face.mIndices[0];
        int idx1 = face.mIndices[1];
        int idx2 = face.mIndices[2];
        gz::math::Vector3d v0 = this->vertices_[idx0];
        gz::math::Vector3d v1 = this->vertices_[idx1];
        gz::math::Vector3d v2 = this->vertices_[idx2];
        auto cross = (v1 - v0).Cross(v2 - v0);
        double area = cross.Length() * 0.5;  // 必须在 Normalize() 之前计算！
        gz::math::Vector3d faceNormal = cross.Normalize();
        /// 如果模型提供了顶点法线，可以用顶点法线平均来验证方向是否一致
        if (!vertexNormals.empty())
        {
            gz::math::Vector3d avgNormal = (vertexNormals[idx0] + vertexNormals[idx1] + vertexNormals[idx2]) / 3.0;
            avgNormal.Normalize();
            if (faceNormal.Dot(avgNormal) < 0.0) // 如果面法线和平均法线方向相反，则翻转
            {
                // avgNormal = -avgNormal;
                faceNormal = -faceNormal;
            }
        }
        keship::Triangle tri;
        tri.v[0] = idx0;
        tri.v[1] = idx1;
        tri.v[2] = idx2;
        tri.normal_ = faceNormal;
        tri.area_ = area;
        tri.centroid_ = (v0 + v1 + v2) / 3.0;
        /// ★ 缺陷 C 修复：浮心累加要用**四面体**形心（见 Buoyancy.hh 的 tetraCentroid_ 说明）
        tri.tetraCentroid_ = (v0 + v1 + v2) / 4.0;
        tri.volume_ = area * tri.normal_.Dot(tri.centroid_) / 3.0;
        this->triangles_.emplace_back(tri);
    }
}
void keship::Buoyancy::loadMeshData(std::string shapeType)
{
    ResetIntersectionIndex();   /// ★ F1：网格换了 ⇒ 旧交点与其索引一律作废
    if (shapeType == "cube")
    {
        this->vertices_.reserve(8);        // 预分配空间，避免后续动态分配
        this->vertexSigns_.reserve(8);     // 预分配空间，避免后续动态分配
        this->vertexDistances_.reserve(8); // 预分配空间，避免后续动态分配
        double _x = 1.0, _y = 1.0, _z = 1.0;
        for (int i = 0; i < 2; ++i)
        {
            for (int j = 0; j < 2; ++j)
            {
                for (int k = 0; k < 2; ++k)
                {
                    this->vertices_.emplace_back(
                        gz::math::Vector3d(i * _x - _x / 2.0,
                                           j * _y - _y / 2.0, k * _z - _z / 2.0));
                }
            }
        }

        /// 读取立方体网格数据
    }
    else if (shapeType == "sphere")
    {
        /// 读取球体网格数据
    }
    else
    {
        std::cout << "Buoyancy::loadMeshData 不支持的形状类型：" << shapeType << std::endl;
    }
}
void keship::Buoyancy::loadMeshData(const MeshDataSimple &_data)
{
    std::cout << "Buoyancy::loadMeshData(MeshDataSimple) 顶点数：" << _data.vertices.size()
              << "，三角形数：" << _data.triangles.size() << std::endl;

    /// 清空旧数据
    this->vertices_.clear();
    this->triangles_.clear();
    this->vertexSigns_.clear();
    this->vertexDistances_.clear();
    this->volume_ = 0.0;
    ResetIntersectionIndex();   /// ★ F1：网格换了 ⇒ 旧交点与其索引一律作废（漏清会越界）

    const size_t nv = _data.vertices.size();
    const size_t nt = _data.triangles.size();

    this->vertices_.reserve(nv);
    this->vertexSigns_.reserve(nv);
    this->vertexDistances_.reserve(nv);
    this->triangles_.reserve(nt);

    /// 复制顶点
    for (const auto &v : _data.vertices)
        this->vertices_.push_back(v);
    this->vertexSigns_.resize(nv, 0);
    this->vertexDistances_.resize(nv, 0.0);

    /// 转换三角形（Triangle3 → 带几何属性的 Triangle）
    for (const auto &tri : _data.triangles)
    {
        gz::math::Vector3d v0 = tri.v0;
        gz::math::Vector3d v1 = tri.v1;
        gz::math::Vector3d v2 = tri.v2;

        // 在 vertices_ 中查找索引
        int idx0 = -1, idx1 = -1, idx2 = -1;
        for (size_t k = 0; k < nv; k++)
        {
            if ((vertices_[k] - v0).Length() < 1e-6) idx0 = k;
            if ((vertices_[k] - v1).Length() < 1e-6) idx1 = k;
            if ((vertices_[k] - v2).Length() < 1e-6) idx2 = k;
        }
        if (idx0 < 0 || idx1 < 0 || idx2 < 0)
        {
            std::cerr << "Buoyancy::loadMeshData: 三角形顶点在顶点列表中找不到" << std::endl;
            continue;
        }

        auto cross = (v1 - v0).Cross(v2 - v0);
        double area = cross.Length() * 0.5;  // 必须在 Normalize() 之前计算！
        gz::math::Vector3d faceNormal = cross.Normalize();

        keship::Triangle triOut;
        triOut.v[0] = idx0;
        triOut.v[1] = idx1;
        triOut.v[2] = idx2;
        triOut.normal_ = faceNormal;
        triOut.area_ = area;
        triOut.centroid_ = (v0 + v1 + v2) / 3.0;
        /// ★ 缺陷 C 修复：浮心累加要用**四面体**形心（见 Buoyancy.hh 的 tetraCentroid_ 说明）
        triOut.tetraCentroid_ = (v0 + v1 + v2) / 4.0;
        triOut.volume_ = area * triOut.normal_.Dot(triOut.centroid_) / 3.0;
        this->triangles_.push_back(triOut);
    }

    /// 诊断：顶点 Y 范围
    double yMin = 1e9, yMax = -1e9;
    int yPos = 0, yNeg = 0;
    for (const auto &v : this->vertices_) {
        if (v.Y() > 0) yPos++; else yNeg++;
        if (v.Y() < yMin) yMin = v.Y();
        if (v.Y() > yMax) yMax = v.Y();
    }
    std::cout << "Buoyancy::loadMeshData 顶点Y: pos=" << yPos << " neg=" << yNeg
              << " range=[" << yMin << "," << yMax << "]" << std::endl;

    /// 诊断：统计左右舷三角形体积符号
    int stbdPos = 0, stbdNeg = 0, portPos = 0, portNeg = 0;
    double stbdSum = 0, portSum = 0;
    for (const auto &t : this->triangles_) {
        if (t.centroid_.Y() > 0) {  // 右舷 y>0
            if (t.volume_ > 0) { stbdPos++; stbdSum += t.volume_; }
            else { stbdNeg++; stbdSum += t.volume_; }
        } else {  // 左舷 y<0
            if (t.volume_ > 0) { portPos++; portSum += t.volume_; }
            else { portNeg++; portSum += t.volume_; }
        }
    }
    std::cout << "Buoyancy::loadMeshData 诊断: "
              << "右舷 pos=" << stbdPos << " neg=" << stbdNeg << " sum=" << stbdSum
              << "  左舷 pos=" << portPos << " neg=" << portNeg << " sum=" << portSum
              << std::endl;

    /// 打印前 3 个右舷和前 3 个左舷三角形的详细信息
    int stbdCount = 0, portCount = 0;
    for (const auto &t : this->triangles_) {
        if (t.centroid_.Y() > 0 && stbdCount < 3) {
            std::cout << "  右舷 tri[" << stbdCount << "] c=("
                      << t.centroid_.X() << "," << t.centroid_.Y() << "," << t.centroid_.Z()
                      << ") n=(" << t.normal_.X() << "," << t.normal_.Y() << "," << t.normal_.Z()
                      << ") vol=" << t.volume_ << std::endl;
            stbdCount++;
        }
        if (t.centroid_.Y() < 0 && portCount < 3) {
            std::cout << "  左舷 tri[" << portCount << "] c=("
                      << t.centroid_.X() << "," << t.centroid_.Y() << "," << t.centroid_.Z()
                      << ") n=(" << t.normal_.X() << "," << t.normal_.Y() << "," << t.normal_.Z()
                      << ") vol=" << t.volume_ << std::endl;
            portCount++;
        }
    }

    std::cout << "Buoyancy::loadMeshData(MeshDataSimple) 完成："
              << this->vertices_.size() << " 顶点，"
              << this->triangles_.size() << " 三角形" << std::endl;
}

//---------------------------------------------------------------------------------------------------------------------------
/**
 * @brief 计算总体积贡献（水平水面，向后兼容）
 */
double keship::Buoyancy::calculateTotalVolumeContribution(gz::math::Pose3d _linkPose, double _fluidLevel)
{
    return calculateTotalVolumeContribution(_linkPose,
        gz::math::Plane<double>(gz::math::Vector3d(0, 0, 1), _fluidLevel));
}

/**
 * @brief 计算总体积贡献（任意方向裁剪面）
 */
double keship::Buoyancy::calculateTotalVolumeContribution(gz::math::Pose3d _linkPose,
    const gz::math::Plane<double> &_waterPlane)
{
    /// 将世界空间平面变换到船舶局部坐标系
    auto pos = _linkPose.Pos();
    auto rot = _linkPose.Rot();
    auto normal = rot.Inverse().RotateVector(_waterPlane.Normal());
    auto offset = _waterPlane.Offset() - _waterPlane.Normal().Dot(pos);
    auto plane = gz::math::Plane<double>(normal, offset);

    /// 诊断日志文件名（多模型实例区分，避免交错）
    const std::string diagFile = this->diagLogPrefix_.empty()
        ? "diag_buoy.log"
        : this->diagLogPrefix_ + "_diag_buoy.log";

    /// [DIAG-D2] 水线平面变换诊断（写日志文件，限频）
    {
      if (this->diagPlaneCount_ < 600)
      {
        this->diagPlaneCount_++;
        auto e = rot.Euler();
        DiagnosticLog::write(diagFile,
            "[KSBuoy] tag,posX,posY,posZ,rotRoll,rotPitch,rotYaw,worldNx,worldNy,worldNz,worldOff,localNx,localNy,localNz,localOff",
            std::string("[plane],")
            + std::to_string(pos.X()) + "," + std::to_string(pos.Y()) + "," + std::to_string(pos.Z())
            + "," + std::to_string(e.X()) + "," + std::to_string(e.Y()) + "," + std::to_string(e.Z())
            + "," + std::to_string(_waterPlane.Normal().X()) + "," + std::to_string(_waterPlane.Normal().Y()) + "," + std::to_string(_waterPlane.Normal().Z())
            + "," + std::to_string(_waterPlane.Offset())
            + "," + std::to_string(normal.X()) + "," + std::to_string(normal.Y()) + "," + std::to_string(normal.Z())
            + "," + std::to_string(offset));
      }
    }

    /// 计算所有顶点与水线面的位置关系
    calculateAllVertexVsWaterplane(plane);

    /// 清零浮心累加器
    centerOfBuoyancy_ = gz::math::Vector3d(0, 0, 0);

    /// 计算湿表面的体积贡献
    calculateWetSurfaceVolumeContribution();
    double wetSurfaceVol = volume_;

    /// 计算水线面多边形的体积贡献（同时累加浮心）
    double waterPolyVol = calculateWaterPlanePolygonVolumeContribution(plane);

    /// ★ 2026-09-19 修 A：盖面项必须计入总体积。
    /// 原先该量被算出来后**只用于浮心累加、返回值直接丢弃** —— 于是
    /// 分子（浮心）含盖面、分母（volume_）不含，**口径自相矛盾**；
    /// 且返回的浮力体积偏大（细长船型可达 +60%），使船浮得过高。
    /// 见 doc/plans/headless_ctl_plan.md §16。
    volume_ += waterPolyVol;

    /// [DIAG-D2] 体积构成诊断（写日志文件，限频）
    {
      if (this->diagVolCount_ < 600)
      {
        this->diagVolCount_++;
        DiagnosticLog::write(diagFile,
            "[KSBuoy] tag,wetSurface,waterPoly,total",
            std::string("[vol],")
            + std::to_string(wetSurfaceVol) + "," + std::to_string(waterPolyVol)
            + "," + std::to_string(volume_));
      }
    }

    /// 归一化浮心 = Σ(vol_i * centroid_i) / Σ(vol_i)
    if (volume_ > 1e-9)
        centerOfBuoyancy_ /= volume_;

    return volume_;
}

/**
 * @brief 从线段构建水线面多边形
 */
std::vector<keship::Polygon> keship::Buoyancy::buildPolygonFromSegments()
{
    std::vector<Polygon> result;
    if (waterPlaneSegments_.empty())
        return result;

    // 构建邻接表
    std::map<int, std::vector<int>> adj;
    for (const auto &seg : waterPlaneSegments_)
    {
        adj[seg.p[0]].push_back(seg.p[1]);
        adj[seg.p[1]].push_back(seg.p[0]);
    }

    /// 调试信息已关闭 << std::endl;
    std::map<int, int> degreeCount;
    for (const auto &entry : adj)
    {
        degreeCount[entry.second.size()]++;
    }
    for (const auto &entry : degreeCount)
    {
        // [DEBUG disabled] std::cout << "顶点度为 " << entry.first << " 的数量：" << entry.second << std::endl;
    }
        // [DEBUG disabled] std::cout.flush();

    std::set<int> visited;

    /// ★ 2026-09-29 性能修复（计划 §21.5 第 1 项）：把"当前环成员"的判定从
    ///   `std::find(loop…)`（每步 O(|loop|) ⇒ 整体 **O(N²)**，N = 水线交点数）
    ///   换成 **O(1) 成员标记**。
    ///
    ///   实现：按点 id 的"代号(generation)"数组 —— 每开始一个新环就 `++loopGen`
    ///   （等价于清空标记，O(1)），判定 = `inLoopGen[id] == loopGen`。
    ///
    ///   ⚠ **语义完全等价**：判定的仍是"该点是否已在本环中"，push 的时机与顺序均不变
    ///   ⇒ `loop` 的内容与顺序**逐位不变** ⇒ 返回体积逐位不变
    ///   （由 `data/test/perf_buoyance_equiv.cc` 的改动前后高精度对照验证）。
    ///
    ///   尺寸取 `max(intersectionPoints_.size(), adj 最大键 + 1)` —— 两者都是点 id 的合法上界
    ///   （所有 id 都来自 `getIntersectionPointIndex`，且 `intersectionPoints_` 每次调用前清空）。
    size_t idBound = intersectionPoints_.size();
    for (const auto &kv : adj)
        idBound = std::max(idBound, static_cast<size_t>(kv.first) + 1);
    std::vector<int> inLoopGen(idBound, 0);
    int loopGen = 0;

    for (const auto &entry : adj)
    {
        int start = entry.first;
        if (visited.count(start) || entry.second.empty())
            continue;

        ++loopGen;   /// 新环 ⇒ 新一代（等价于清空成员标记，O(1)）
        std::vector<int> loop;
        int current = start;
        int prev = -1;

        bool validLoop = true;
        int step = 0;
        const int maxStep = adj.size() * 3; // 最大步数，防止无限循环
        while (step < maxStep && validLoop)
        {
            // visited.insert(current);
            // loop.push_back(current);

            /// 检查是否回到起点形成闭环
            if (current == start && !loop.empty())
                break;
            /// 标记为已访问
            if (visited.count(current) == 0)
            {
                visited.insert(current);
            }
            /// 如果还没添加，则添加到环中
            /// （★ 2026-09-29：O(1) 成员标记，语义与原先的 std::find 等价 —— 见函数开头说明）
            if (inLoopGen[static_cast<size_t>(current)] != loopGen)
            {
                inLoopGen[static_cast<size_t>(current)] = loopGen;
                loop.push_back(current);
            }

            const auto &neighbors = adj[current];
            // if (neighbors.size() != 2) //  // 检查每个顶点的邻居数是否为2
            // {
            //     // 非流形，终止此环（可能无法闭合）
            //     loop.clear();
            //     break;
            // }
            if (neighbors.empty()) // 允许复杂拓扑
            {
                // loop.clear();
                validLoop = false;
                break;
            }

            // int next = (neighbors[0] != prev) ? neighbors[0] : neighbors[1];
            int next = -1; // 智能选择下一个顶点

            for (int n : neighbors) // 优先选择未访问的邻居
            {
                if (n != prev && (visited.count(n) == 0 || n == start))
                {
                    next = n;
                    break;
                }
            }
            if (next == -1) // 所有邻居都已访问，尝试已经访问的（可能形成环）
            {
                for (int n : neighbors)
                {
                    if (n != prev)
                    {
                        next = n;
                        break;
                    }
                }
            }
            if (next == -1)
            {
                validLoop = false;
                break;
            }
            prev = current;
            current = next;
            step++;
        }
        if (validLoop && loop.size() >= 3)
        {
            bool isClosed = (current == start); // 检查是否闭合
            // 转换为坐标
            std::vector<gz::math::Vector3d> vertices;
            for (int id : loop)
            {
                vertices.push_back(intersectionPoints_[id]);
            }
            if (isClosed || loop.size() > 10) //
            {
                Polygon poly;
                poly.v = vertices;
                poly.normal_ = gz::math::Vector3d(0, 0, 1); // 水线面外法向向上
                result.push_back(poly);

            }
        }
    }

    return result;
}

/**
 * @brief 计算单个多边形的面积
 * @param _polygon 多边形
 * @return 多边形的面积（单位：m²）
 */
void keship::Buoyancy::calculatePolygonArea(const Polygon &_polygon, double &_area, gz::math::Vector3d &_centroid) const
{
    // 三角剖分法（以第一个顶点为基准）
    double totalArea = 0.0;
    gz::math::Vector3d centroid(0, 0, 0);
    int n = _polygon.v.size();
    for (int i = 1; i < n - 1; ++i)
    {
        const gz::math::Vector3d &v0 = _polygon.v[0];
        const gz::math::Vector3d &v1 = _polygon.v[i];
        const gz::math::Vector3d &v2 = _polygon.v[i + 1];
        gz::math::Vector3d a = v1 - v0;
        gz::math::Vector3d b = v2 - v0;
        gz::math::Vector3d cross = a.Cross(b);
        double area = cross.Length() * 0.5;
        gz::math::Vector3d triCentroid = (v0 + v1 + v2) / 3.0;
        totalArea += area;
        centroid += triCentroid * area;
    }
    _area = totalArea;
    _centroid = centroid;
    if (totalArea > 1e-6)
        _centroid /= totalArea;
    else
        _centroid = gz::math::Vector3d(0, 0, 0);
}

/**
 * @brief 获取交点的索引，若不存在则添加
 * @param _point 交点坐标
 * @return 交点的唯一索引
 */
int keship::Buoyancy::getIntersectionPointIndex(const gz::math::Vector3d &_point)
{
    /// ★ 2026-09-29 性能修复（优化计划 F1）：线性扫描 → **空间哈希 + 27 邻域探测**。
    ///
    ///   原实现（见 git 历史）对每个交点都要扫一遍已有交点表 ⇒ 每次 O(N)、整体 **O(N²)**；
    ///   实测占整次浮力计算的 **46%~92%**（512 段圆柱下 92%）—— 是当前每步最大开销。
    ///
    ///   **判据与原实现完全一致**：返回"第一个欧氏距离 < EPS 的已有点"的索引。
    ///   由于格边长 = EPS，任何与 _point 距离 < EPS 的点各轴 |Δ| < EPS
    ///   ⇒ 量化格差 ≤ 1 ⇒ **必落在 27 个邻格内**；扫全 27 格取**最小索引**
    ///   ⇒ 与线性扫描的"第一个命中"**同解**（可逐位等价，由 perf_buoyance_equiv 对照验证）。
    ///
    ///   ⚠ 索引必须与 `intersectionPoints_` 同步清空（见 `ResetIntersectionIndex()`）。
    ///
    ///   **混合策略（2026-09-29 实测后加）**：交点表很短时，27 次哈希查询的固定开销
    ///   反而**超过**线性扫描（实测：生产默认 32 段圆柱 0.226 → 0.266 ms，**+18%** ✗）。
    ///   故 `N <= kLinearScanMax` 时直接走原线性扫描 —— 它本身就是参考实现，**等价性平凡成立**；
    ///   超过阈值才走哈希（实测 512 段：28.0 → 4.99 ms，**5.62×**）。
    ///   ⚠ 小 N 分支**也必须把新点登记进哈希索引**，否则 N 越过阈值后索引不完整。
    if (intersectionPoints_.size() <= kLinearScanMax)
    {
        for (size_t i = 0; i < intersectionPoints_.size(); ++i)
        {
            if ((intersectionPoints_[i] - _point).Length() < EPS)
                return static_cast<int>(i);
        }
        const int newIdx = static_cast<int>(intersectionPoints_.size());
        intersectionPoints_.push_back(_point);
        /// 线性阶段**不碰索引**（零额外开销）；刚跨过阈值时一次性建好，供后续哈希分支使用
        if (intersectionPoints_.size() == kLinearScanMax + 1)
            RebuildIntersectionIndex();
        return newIdx;
    }

    const int64_t ix = QuantToCell(_point.X());
    const int64_t iy = QuantToCell(_point.Y());
    const int64_t iz = QuantToCell(_point.Z());

    int best = -1;
    for (int dx = -1; dx <= 1; ++dx)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dz = -1; dz <= 1; ++dz)
            {
                auto it = intersectionCells_.find(CellKey(ix + dx, iy + dy, iz + dz));
                if (it == intersectionCells_.end())
                    continue;
                for (int idx : it->second)
                {
                    /// 已找到更早的命中 ⇒ 不可能再更小，跳过（不改变 min 结果，只是省比较）
                    if (best >= 0 && idx >= best)
                        continue;
                    if (idx >= 0 && idx < static_cast<int>(intersectionPoints_.size())
                        && (intersectionPoints_[idx] - _point).Length() < EPS)
                        best = idx;
                }
            }
        }
    }
    if (best >= 0)
        return best;

    /// 未找到 ⇒ 追加，并登记到所属格
    const int index = static_cast<int>(intersectionPoints_.size());
    intersectionPoints_.push_back(_point);
    intersectionCells_[CellKey(ix, iy, iz)].push_back(index);
    return index;
}

void keship::Buoyancy::ResetIntersectionIndex()
{
    intersectionPoints_.clear();
    intersectionCells_.clear();
}

void keship::Buoyancy::RebuildIntersectionIndex()
{
    /// 按当前交点表一次性建索引（O(N)，只在线性阶段跨过阈值时调用一次）
    intersectionCells_.clear();
    intersectionCells_.reserve(intersectionPoints_.size() * 2);
    for (size_t i = 0; i < intersectionPoints_.size(); ++i)
    {
        const auto &p = intersectionPoints_[i];
        intersectionCells_[CellKey(QuantToCell(p.X()), QuantToCell(p.Y()), QuantToCell(p.Z()))]
            .push_back(static_cast<int>(i));
    }
}

/**
 * @brief 计算点与面的位置关系
 * @param _vertex 点的坐标
 * @param _waterPlane 水线面
 * @param eps 容差，默认1e-6
 * @return int 1表示上方，-1表示下方，0表示近似水线面上
 */
void keship::Buoyancy::calculateVertexVsWaterplane(
    int _vertexIndex,
    const gz::math::Plane<double> &_waterPlane,
    double eps)
{
    auto distance = _waterPlane.Distance(this->vertices_[_vertexIndex]);
    if (std::isnan(distance))
        std::cout << "Buoyancy::calculateVertexVsWaterplane  注意！！！，距离为NaN" << std::endl;
    if (distance > eps)
    {
        this->vertexSigns_[_vertexIndex] = 1; // 水面上方
        this->vertexDistances_[_vertexIndex] = distance;
    }
    else if (distance < -eps)
    {
        this->vertexSigns_[_vertexIndex] = -1; // 水面下方
        this->vertexDistances_[_vertexIndex] = distance;
    }
    else
    {
        this->vertexSigns_[_vertexIndex] = 0; // 近似水线面上
        this->vertexDistances_[_vertexIndex] = 0;
    }
}
/**
 * @brief 计算所有顶点与水面的位置关系
 * @param _waterPlane 水线面
 */
void keship::Buoyancy::calculateAllVertexVsWaterplane(const gz::math::Plane<double> &_waterPlane)
{
    double eps = 1e-6;
    double distance = 0.0;
    int i;
    for (i = 0; i < this->vertices_.size(); i++)
    {
        // this->calculateVertexVsWaterplane(i, _waterPlane);

        ///
        distance = _waterPlane.Distance(this->vertices_[i]);
        if (std::isnan(distance))
            std::cout << "Buoyancy::calculateVertexVsWaterplane  注意！！！，距离为NaN" << std::endl;
        if (distance > eps)
        {
            this->vertexSigns_[i] = 1; // 水面上方
            this->vertexDistances_[i] = distance;
        }
        else if (distance < -eps)
        {
            this->vertexSigns_[i] = -1; // 水面下方
            this->vertexDistances_[i] = distance;
        }
        else
        {
            this->vertexSigns_[i] = 0; // 近似在水线面内
            this->vertexDistances_[i] = 0;
        }
        // if(i<10){
        //     std::cout<<"Buoyancy::calculateAllVertexVsWaterplane "
        //     <<_waterPlane.Offset()<<";"<<_waterPlane.Normal()<<";"<<vertices_[i]<<";"<<distance<<std::endl;
        // }
        // else{getchar();}
    }

}

/**
 * @brief 计算湿表面的体积贡献 - 并行实现
 */
void keship::Buoyancy::calculateWetSurfaceVolumeContribution_parallel()
{
    ///--- 计算所有三角形的情况：水面上方、水面下方、水面与三角形相交
    volume_ = 0.0;
    waterPlaneSegments_.clear();
    ResetIntersectionIndex();   /// ★ F1：交点与其空间哈希索引**必须同步清空**

    // 并行计算参数
    const size_t numThreads = std::thread::hardware_concurrency();
    const size_t numTriangles = triangles_.size();
    const size_t batchSize = numTriangles / numThreads + 1;

    // 线程局部变量
    std::vector<double> threadVolumes(numThreads, 0.0);
    std::vector<std::vector<Segment>> threadWaterPlaneSegments(numThreads);
    std::vector<std::vector<gz::math::Vector3d>> threadIntersectionPoints(numThreads);
    std::vector<std::map<gz::math::Vector3d, int>> threadIntersectionPointMaps(numThreads);

    // 互斥锁
    std::mutex intersectionPointsMutex;

    // 线程函数
    auto processBatch = [&](size_t threadId, size_t startIdx, size_t endIdx)
    {
        double localVolume = 0.0;
        std::vector<Segment> localSegments;
        std::vector<gz::math::Vector3d> localIntersectionPoints;
        std::map<gz::math::Vector3d, int> localIntersectionPointMap;

        auto getLocalIntersectionPointIndex = [&](const gz::math::Vector3d &_point)
        {
            double eps = 1e-6;
            // 先在本地查找
            for (size_t i = 0; i < localIntersectionPoints.size(); ++i)
            {
                if ((localIntersectionPoints[i] - _point).Length() < eps)
                {
                    return i;
                }
            }
            // 本地未找到，添加到本地
            size_t index = localIntersectionPoints.size();
            localIntersectionPointMap[_point] = index;
            localIntersectionPoints.push_back(_point);
            return index;
        };

        for (size_t i = startIdx; i < endIdx && i < numTriangles; ++i)
        {
            const auto &tri = triangles_[i];
            int signs[3] = {vertexSigns_[tri.v[0]], vertexSigns_[tri.v[1]], vertexSigns_[tri.v[2]]};
            int signSum = signs[0] + signs[1] + signs[2];
            int signMulti = signs[0] * signs[1] * signs[2];

            /// 情况1：三角形完全在水面上方：不贡献体积
            if (signSum == 3 || signSum == 2)
            {
                continue;
            }
            /// 情况2：三角形完全在水面下方：贡献体积
            else if (signSum == -3 || signSum == -2)
            {
                localVolume += tri.volume_;
            }
            /// 情况3：三角形与水面相交：三角形贡献部分体积，同时计算水线面多边形
            else
            {
                if (signMulti == 0) // 一个或两个顶点在水线面中
                {
                    switch (signSum)
                    {
                    case -1: // 1、计算体积贡献；2、记录水线面多边形的边
                        localVolume += tri.volume_;
                    case 1: // 1、不计算体积贡献；2、记录水线面多边形的边
                        for (int j = 0; j < 3; ++j)
                        {
                            int k = (j + 1) % 3;
                            if (signs[j] == 0 && signs[k] == 0) // 说明该边与水线面平行
                            {
                                // 记录水线面多边形的边
                                localSegments.emplace_back(
                                    getLocalIntersectionPointIndex(vertices_[tri.v[j]]),
                                    getLocalIntersectionPointIndex(vertices_[tri.v[k]]));
                                break;
                            }
                        }
                        break;
                    case 0: // 水上水下都是三角形
                    {
                        int a = -1, b = -1, c = -1;
                        gz::math::Vector3d bc_mid;
                        for (int j = 0; j < 3; ++j)
                        {
                            if (signs[j] == 0)
                                a = tri.v[j];
                            else if (signs[j] > 0)
                                b = tri.v[j];
                            else
                                c = tri.v[j];
                        }
                        bc_mid = vertices_[b] + (vertexDistances_[b] / (vertexDistances_[b] - vertexDistances_[c])) * (vertices_[c] - vertices_[b]);
                        /// 记录水线面多边形的边
                        localSegments.emplace_back(
                            getLocalIntersectionPointIndex(vertices_[a]),
                            getLocalIntersectionPointIndex(bc_mid));
                        /// 计算体积贡献
                        auto vol = ((vertices_[a] - bc_mid).Cross(vertices_[c] - bc_mid)).Length() * 0.5 * tri.normal_.Dot((vertices_[a] + bc_mid + vertices_[c]) / 3.0) / 3.0;
                        localVolume += vol;

                        // std::cout << "Buoyancy::processBatch " << " 三角形 " << i << " 体积贡献：" << vol << " 三角形体积：" << tri.volume_ << std::endl;
                        // getchar();
                    }
                    break;
                    }
                }
                else
                {
                    int a;     // 水面一侧三角形的一个顶点a
                    int bc[2]; // 水面另一侧三角形的两个顶点b、c
                    int index = 0;
                    gz::math::Vector3d ab_mid, ac_mid; //  两条相交边的交点
                    double vol;
                    switch (signSum)
                    {
                    case 1: // 水下是三角形
                        for (int j = 0; j < 3; ++j)
                        {
                            if (signs[j] < 0)
                            {
                                a = tri.v[j];
                            }
                            else
                            {
                                bc[index++] = tri.v[j];
                            }
                        }
                        /// 计算线段与水线面的交点
                        ab_mid = vertices_[a] +
                                 (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[0]])) * (vertices_[bc[0]] - vertices_[a]);
                        ac_mid = vertices_[a] +
                                 (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[1]])) * (vertices_[bc[1]] - vertices_[a]);
                        /// 记录水线面多边形的边
                        localSegments.emplace_back(
                            getLocalIntersectionPointIndex(ab_mid),
                            getLocalIntersectionPointIndex(ac_mid));
                        /// 计算体积贡献
                        vol = ((vertices_[a] - ab_mid).Cross(ac_mid - ab_mid)).Length() * 0.5 * tri.normal_.Dot((vertices_[a] + ab_mid + ac_mid) / 3.0) / 3.0;
                        localVolume += vol;
                        // std::cout << "Buoyancy::processBatch " << " 三角形 " << i << " 体积贡献：" << vol << " 三角形体积：" << tri.volume_ << std::endl;
                        // getchar();
                        break;
                    case -1: // 水上是三角形
                        for (int j = 0; j < 3; ++j)
                        {
                            if (signs[j] > 0)
                            {
                                a = tri.v[j];
                            }
                            else
                            {
                                bc[index++] = tri.v[j];
                            }
                        }
                        /// 计算线段与水线面的交点
                        ab_mid = vertices_[a] +
                                 (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[0]])) * (vertices_[bc[0]] - vertices_[a]);
                        ac_mid = vertices_[a] +
                                 (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[1]])) * (vertices_[bc[1]] - vertices_[a]);
                        /// 记录水线面多边形的边
                        localSegments.emplace_back(
                            getLocalIntersectionPointIndex(ab_mid),
                            getLocalIntersectionPointIndex(ac_mid));
                        /// 计算体积贡献
                        vol = (tri.volume_ - ((vertices_[a] - ab_mid).Cross(ac_mid - ab_mid)).Length() * 0.5 * tri.normal_.Dot((vertices_[a] + ab_mid + ac_mid) / 3.0) / 3.0);
                        localVolume += vol;
                        // std::cout << "Buoyancy::processBatch " << " 三角形 " << i << " 体积贡献：" << vol << " 三角形体积：" << tri.volume_ << std::endl;
                        // getchar();
                        break;
                    }
                }
            }
        }

        // 保存线程局部结果
        threadVolumes[threadId] = localVolume;
        threadWaterPlaneSegments[threadId] = std::move(localSegments);
        threadIntersectionPoints[threadId] = std::move(localIntersectionPoints);
        threadIntersectionPointMaps[threadId] = std::move(localIntersectionPointMap);
    };

    // 创建并启动线程
    std::vector<std::thread> threads;
    for (size_t i = 0; i < numThreads; ++i)
    {
        size_t startIdx = i * batchSize;
        size_t endIdx = startIdx + batchSize;
        threads.emplace_back(processBatch, i, startIdx, endIdx);
    }

    // 等待所有线程完成
    for (auto &thread : threads)
    {
        thread.join();
    }

    // 合并结果
    // 1. 合并体积
    for (double v : threadVolumes)
    {
        volume_ += v;
    }

    // 2. 合并交点
    std::map<gz::math::Vector3d, int> globalIntersectionPointMap;
    for (const auto &localPoints : threadIntersectionPoints)
    {
        for (const auto &point : localPoints)
        {
            if (globalIntersectionPointMap.find(point) == globalIntersectionPointMap.end())
            {
                globalIntersectionPointMap[point] = intersectionPoints_.size();
                intersectionPoints_.push_back(point);
            }
        }
    }

    // 3. 合并水线面线段
    for (size_t i = 0; i < numThreads; ++i)
    {
        const auto &localSegments = threadWaterPlaneSegments[i];
        const auto &localPoints = threadIntersectionPoints[i];

        for (const auto &seg : localSegments)
        {
            // 转换局部索引为全局索引
            int p0 = -1, p1 = -1;
            if (seg.p[0] < localPoints.size())
            {
                const auto &point0 = localPoints[seg.p[0]];
                p0 = globalIntersectionPointMap[point0];
            }
            if (seg.p[1] < localPoints.size())
            {
                const auto &point1 = localPoints[seg.p[1]];
                p1 = globalIntersectionPointMap[point1];
            }
            if (p0 != -1 && p1 != -1)
            {
                waterPlaneSegments_.emplace_back(p0, p1);
            }
        }
    }


}

/**
 * @brief 计算湿表面的体积贡献
 */
void keship::Buoyancy::calculateWetSurfaceVolumeContribution()
{
    ///--- 计算所有三角形的情况：水面上方、水面下方、水面与三角形相交
    volume_ = 0.0;
    waterPlaneSegments_.clear();
    ResetIntersectionIndex();   /// ★ F1：交点与其空间哈希索引**必须同步清空**

    int stdcount = 0;
    for (const auto &tri : triangles_)
    {
        stdcount++;
        int signs[3] = {vertexSigns_[tri.v[0]], vertexSigns_[tri.v[1]], vertexSigns_[tri.v[2]]};
        int signSum = signs[0] + signs[1] + signs[2];
        int signMulti = signs[0] * signs[1] * signs[2];

        /// 情况1：三角形完全在水面上方：不贡献体积
        if (signSum == 3 || signSum == 2)
        {
            continue;
        }
        /// 情况2：三角形完全在水面下方：贡献体积和浮心
        else if (signSum == -3 || signSum == -2)
        {
            volume_ += tri.volume_;
            /// ★ 缺陷 C：必须用四面体形心 tetraCentroid_（= centroid_×3/4）
            centerOfBuoyancy_ += tri.volume_ * tri.tetraCentroid_;
        }
        /// 情况3：三角形与水面相交：三角形贡献部分体积，同时计算水线面多边形
        else
        {
            if (signMulti == 0) // 一个或两个顶点在水线面中
            {
                switch (signSum)
                {
                case -1: // 1、计算体积贡献和浮心；2、记录水线面多边形的边
                    volume_ += tri.volume_;
                    /// ★ 缺陷 C：必须用四面体形心 tetraCentroid_（= centroid_×3/4）
                    centerOfBuoyancy_ += tri.volume_ * tri.tetraCentroid_;
                case 1: // 1、不计算体积贡献；2、记录水线面多边形的边
                    for (int i = 0; i < 3; ++i)
                    {
                        int j = (i + 1) % 3;
                        if (signs[i] == 0 && signs[j] == 0) // 说明该边与水线面平行
                        {
                            // 记录水线面多边形的边
                            waterPlaneSegments_.emplace_back(
                                getIntersectionPointIndex(vertices_[tri.v[i]]),
                                getIntersectionPointIndex(vertices_[tri.v[j]]));
                            break;
                        }
                    }
                    break;
                case 0: // 水上水下都是三角形
                {
                    int a = -1, b = -1, c = -1;
                    gz::math::Vector3d bc_mid;
                    for (int i = 0; i < 3; ++i)
                    {
                        if (signs[i] == 0)
                            a = tri.v[i];
                        else if (signs[i] > 0)
                            b = tri.v[i];
                        else
                            c = tri.v[i];
                    }
                    /// ★ 2026-10-05 修复（退化三角形 ⇒ NaN）：
                    ///   若三个顶点**全部**落在水线面内（signs 全 0），则 b/c 保持 -1，
                    ///   下一行会 `vertices_[-1]` 越界 ⇒ subVol 变 NaN、仿真冻结。
                    ///   该三角形体积贡献为 0 ⇒ 正确做法是**跳过**。
                    if (b < 0 || c < 0)
                        break;
                    bc_mid = vertices_[b] + (vertexDistances_[b] / (vertexDistances_[b] - vertexDistances_[c])) * (vertices_[c] - vertices_[b]);
                    /// 记录水线面多边形的边
                    waterPlaneSegments_.emplace_back(
                        getIntersectionPointIndex(vertices_[a]),
                        getIntersectionPointIndex(bc_mid));
                    /// 计算体积贡献和浮心
                    double partialVol = ((vertices_[a] - bc_mid).Cross(vertices_[c] - bc_mid)).Length() * 0.5 * tri.normal_.Dot((vertices_[a] + bc_mid + vertices_[c]) / 3.0) / 3.0;
                    volume_ += partialVol;
                    /// ★ 缺陷 C：浮心用**四面体**形心 /4.0（体积仍用三角形形心 /3.0，勿混）
                    centerOfBuoyancy_ += partialVol * ((vertices_[a] + bc_mid + vertices_[c]) / 4.0);
                }
                break;
                }
            }
            else
            {
                int a;     // 水面一侧三角形的一个顶点a
                int bc[2]; // 水面另一侧三角形的两个顶点b、c
                int index = 0;
                gz::math::Vector3d ab_mid, ac_mid; //  两条相交边的交点
                switch (signSum)
                {
                case 1: // 水下是三角形（一顶点在水下A，两顶点在水上B、C）
                    for (int i = 0; i < 3; ++i)
                    {
                        if (signs[i] < 0)
                        {
                            a = tri.v[i];
                        }
                        else
                        {
                            bc[index++] = tri.v[i];
                        }
                    }
                    /// 计算线段与水线面的交点
                    ab_mid = vertices_[a] +
                             (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[0]])) * (vertices_[bc[0]] - vertices_[a]);
                    ac_mid = vertices_[a] +
                             (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[1]])) * (vertices_[bc[1]] - vertices_[a]);
                    /// 记录水线面多边形的边
                    waterPlaneSegments_.emplace_back(
                        getIntersectionPointIndex(ab_mid),
                        getIntersectionPointIndex(ac_mid));
                    /// 计算体积贡献和浮心（浸没部分近似为三角形形心）
                    {
                        double partialVol = ((vertices_[a] - ab_mid).Cross(ac_mid - ab_mid)).Length() * 0.5 * tri.normal_.Dot((vertices_[a] + ab_mid + ac_mid) / 3.0) / 3.0;
                        volume_ += partialVol;
                        /// ★ 缺陷 C：浮心用**四面体**形心 /4.0（体积仍用三角形形心 /3.0，勿混）
                        centerOfBuoyancy_ += partialVol * ((vertices_[a] + ab_mid + ac_mid) / 4.0);
                    }
                    break;
                case -1: // 水上是三角形（一顶点在水上A，两顶点在水下B、C）
                    for (int i = 0; i < 3; ++i)
                    {
                        if (signs[i] > 0)
                        {
                            a = tri.v[i];
                        }
                        else
                        {
                            bc[index++] = tri.v[i];
                        }
                    }
                    /// 计算线段与水线面的交点
                    ab_mid = vertices_[a] +
                             (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[0]])) * (vertices_[bc[0]] - vertices_[a]);
                    ac_mid = vertices_[a] +
                             (vertexDistances_[a] / (vertexDistances_[a] - vertexDistances_[bc[1]])) * (vertices_[bc[1]] - vertices_[a]);
                    /// 记录水线面多边形的边
                    waterPlaneSegments_.emplace_back(
                        getIntersectionPointIndex(ab_mid),
                        getIntersectionPointIndex(ac_mid));
                    /// 计算体积贡献和浮心（浸没部分 = 全三角 - 水上四面体）
                    {
                        double upperVol = ((vertices_[a] - ab_mid).Cross(ac_mid - ab_mid)).Length() * 0.5 * tri.normal_.Dot((vertices_[a] + ab_mid + ac_mid) / 3.0) / 3.0;
                        double subVol = tri.volume_ - upperVol;
                        /// ★ 缺陷 C：上、下两块的浮心都用**四面体**形心 /4.0
                        /// （体积 subVol 仍由三角形形心算出的 upperVol 相减，勿改）
                        gz::math::Vector3d upperCentroid = (vertices_[a] + ab_mid + ac_mid) / 4.0;
                        volume_ += subVol;
                        centerOfBuoyancy_ += tri.volume_ * tri.tetraCentroid_ - upperVol * upperCentroid;
                    }
                    break;
                }
            }
        }
    }
}

double keship::Buoyancy::calculateWaterPlanePolygonVolumeContribution(const gz::math::Plane<double> &_waterPlane)
{

    /// 构建多边形
    auto polygons = buildPolygonFromSegments();

    /// 显示所有的水线面多边形的顶点

    /// 计算多边形的体积贡献
    if (polygons.empty())
        return 0.0;

    /// 计算多边形的体积贡献和浮心
    double volume = 0.0;
    double area = 0.0;
    gz::math::Vector3d centroid(0.0, 0.0, 0.0);
    for (const auto &poly : polygons)
    {
        calculatePolygonArea(poly, area, centroid);

        double polyVol = area * _waterPlane.Normal().Dot(centroid) / 3.0;
        volume += polyVol;
        /// ★ 缺陷 C：盖面是"以原点为顶点的锥体"，其体积形心 = (3·centroid + O)/4
        /// = centroid × 3/4；用 centroid 原值会让浮心偏大 4/3。
        /// ⚠️ polyVol 的计算（用 centroid 作高度参考）**不变**。
        centerOfBuoyancy_ += polyVol * centroid * 0.75;
    }

    ///
    return volume;
}
