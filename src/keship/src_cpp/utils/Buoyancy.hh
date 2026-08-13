/*
 * Copyright (C) 2024–2026 KeShip Team
 */
// zhangyu 2026-03-4 创建

/**
 * @brief 浮力计算类
 */

#ifndef BUOYANCY_HH_
#define BUOYANCY_HH_
//----------------------------------------------------------------------------
#include <gz/math/Vector3.hh>
#include <gz/math/Pose3.hh>
#include <gz/math/Plane.hh>
//----------------------------------------------------------------------------
#include <vector>
#include <map>
#include <unordered_map>
//----------------------------------------------------------------------------
#include <assimp/mesh.h>
//----------------------------------------------------------------------------

//////-----------------------------------------------------------------------------
namespace keship
{

    struct MeshDataSimple; // 前向声明，定义在 MeshUtils.hh

    /// @brief 多边形结构体：用于表示裁剪后的水线面界面
    struct Polygon
    {
        /// \brief 多边形的顶点索引
        std::vector<gz::math::Vector3d> v; // 按顺序排列的顶点：闭合多边形，首尾连接但不重复
        /// \brief 多边形的单位法线向量，指向外
        gz::math::Vector3d normal_;
    };
    /// @brief 三角形结构体
    struct Triangle
    {
    public:
        /// \brief 三角形的三个顶点索引
        int v[3];
        /// \brief 三角形的单位法线向量，指向船体外
        gz::math::Vector3d normal_;
        /// \brief 三角形的面积
        double area_;
        /// \brief 三角形的重心坐标
        gz::math::Vector3d centroid_;
        /// \brief 三角形的体积贡献
        double volume_;
    };

    /// @brief 三角形与水线面的交线段
    struct Segment
    {
        int p[2]; // 两个交点的索引
        Segment() = default;
        Segment(int _p0, int _p1) : p{_p0, _p1} {}
    };

    /**-------------------------------------------------------------------------------------------
     * @brief 浮力计算类
     */
    class Buoyancy
    {
    public:
        /// \brief 构造函数
        Buoyancy();
        /// \brief 析构函数
        ~Buoyancy();

        /// \brief 计算浮力：物体所受的浮力等于其排开的液体的重量
        /// \param _volume 物体的体积（单位：m³）
        /// \param _density 液体的密度（单位：kg/m³）
        /// \param _gravity 重力加速度（单位：m/s²）
        /// \return double 浮力（单位：Newton）
        static double CalculateBuoyancy(const double _volume, double _density, double _gravity);

        /// \brief 设置诊断日志文件名前缀（多模型避免交错，如 "模型名_link"）
        /// 前缀非空时日志写入 "{prefix}_diag_buoy.log"
        void setDiagLogPrefix(const std::string &_prefix);

    public:
        std::vector<gz::math::Vector3d> vertices_; // 所有顶点坐标
        std::vector<Triangle> triangles_;          // 所有三角形

    private:
        std::vector<double> vertexDistances_; // 每个顶点到水线面的距离
        std::vector<char> vertexSigns_;       // 每个顶点的位置关系（上方、下方、水线面上），每一帧根据水面高度更新

    private:
        static constexpr double EPS = 1e-6; // 容差，可根据需要调整
        // 自定义哈希：将坐标量化到网格，然后组合哈希值
        struct PointHash
        {
            std::size_t operator()(const gz::math::Vector3d &v) const
            {
                int64_t ix = static_cast<int64_t>(std::floor(v.X() / EPS + 0.5));
                int64_t iy = static_cast<int64_t>(std::floor(v.Y() / EPS + 0.5));
                int64_t iz = static_cast<int64_t>(std::floor(v.Z() / EPS + 0.5));
                // 使用标准哈希组合技术
                std::size_t h = std::hash<int64_t>{}(ix);
                h ^= std::hash<int64_t>{}(iy) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<int64_t>{}(iz) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        // 自定义相等比较：比较量化后的网格坐标
        struct PointEqual
        {
            bool operator()(const gz::math::Vector3d &a, const gz::math::Vector3d &b) const
            {
                int64_t ix_a = static_cast<int64_t>(std::floor(a.X() / EPS + 0.5));
                int64_t iy_a = static_cast<int64_t>(std::floor(a.Y() / EPS + 0.5));
                int64_t iz_a = static_cast<int64_t>(std::floor(a.Z() / EPS + 0.5));
                int64_t ix_b = static_cast<int64_t>(std::floor(b.X() / EPS + 0.5));
                int64_t iy_b = static_cast<int64_t>(std::floor(b.Y() / EPS + 0.5));
                int64_t iz_b = static_cast<int64_t>(std::floor(b.Z() / EPS + 0.5));
                return ix_a == ix_b && iy_a == iy_b && iz_a == iz_b;
            }
        };
        /**
         * @brief 计算点z是否在水线面z0上方、下方或近似水线面上
         * @param z 点的z坐标
         * @param z0 水线面的z坐标
         * @param eps 容差，默认1e-6
         * @return int 1表示上方，-1表示下方，0表示近似水线面上
         */

    private:
        /// 交点管理
        std::unordered_map<gz::math::Vector3d, int, PointHash, PointEqual> intersectionPointsMap_; // 交点坐标到索引的映射
        std::vector<gz::math::Vector3d> intersectionPoints_;                                       // 所有唯一的交点坐标（三角形与水线面的交点）

    public:
        /// \brief 从mesh中读取数据
        void loadMeshData(const aiMesh *_mesh);
        void loadMeshData(std::string shapeType);
        /// \brief 从 MeshUtils 加载的简化网格数据填充
        void loadMeshData(const MeshDataSimple &_data);

    public:
        /// \brief 计算所有多边形的体积贡献（同时更新 centerOfBuoyancy_）
        double calculateTotalVolumeContribution(gz::math::Pose3d _linkPose, double _fluidLevel);
        /// \brief 使用任意世界空间裁剪面计算体积贡献（支持倾斜水面）
        double calculateTotalVolumeContribution(gz::math::Pose3d _linkPose,
            const gz::math::Plane<double> &_waterPlane);
        /// \brief 获取浮心（局部坐标系），须在 calculateTotalVolumeContribution 之后调用
        gz::math::Vector3d getCenterOfBuoyancy() const { return centerOfBuoyancy_; }

    private:
        /// \brief 计算顶点与水面的位置关系
        void calculateVertexVsWaterplane(
            int _vertexIndex,
            const gz::math::Plane<double> &_waterPlane,
            double eps = 1e-6);
        /// \brief 计算所有顶点与水面的位置关系
        void calculateAllVertexVsWaterplane(const gz::math::Plane<double> &_waterPlane);
        /// \brief 计算湿表面的体积贡献
        void calculateWetSurfaceVolumeContribution();
        void calculateWetSurfaceVolumeContribution_parallel();

    private:
        ///
        std::vector<Segment> waterPlaneSegments_; // 所有三角形与水线面的交线段
        /// \brief 从线段构建水线面多边形
        std::vector<Polygon> buildPolygonFromSegments();
        /// \brief 计算水线面多边形的体积贡献
        double calculateWaterPlanePolygonVolumeContribution(const gz::math::Plane<double> &_waterPlane);
        /// \brief 计算单个多边形的面积
        void calculatePolygonArea(const Polygon &_polygon, double &_area, gz::math::Vector3d &_centroid) const;

    private:
        /// \brief 获取交点的索引，若不存在则添加
        int getIntersectionPointIndex(const gz::math::Vector3d &_point);

    private:
        /// \brief 剪裁三角形，生成湿表面多边形和交线段
        // void processTriangle(const Triangle &_triangle, std::vector<Polygon> &_polygons);
        /// \brief 计算单个三角形的体积贡献
        // void calculateTriangleContribution(const Triangle &_triangle, double &_volume, double &_centroid) const;

        

    private:
        double fluideLevel_;                         // 水线面高度
        double volume_;                              // 排水量（单位：m³）
        gz::math::Vector3d centerOfBuoyancy_;        // 浮心（局部坐标系）

        /// \brief 诊断日志文件名前缀（空 = 默认 "diag_buoy.log"）
        std::string diagLogPrefix_;
        /// \brief 诊断限频计数器（成员变量，避免多实例共享 static）
        int diagPlaneCount_ = 0;
        int diagVolCount_ = 0;

    }; // class Buoyancy

} // namespace keship

#endif // BUOYANCY_HH_
