/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 模型管理器类 */
/// Zhangyu 创建 2026.1.16

#ifndef MODEL_MANAGER_HH_
#define MODEL_MANAGER_HH_

#include <array>
#include <string>
#include <memory>
#include <tuple>
#include <vector>
#include <list>

#include <gz/transport/Node.hh>
#include <gz/msgs/entity_factory.pb.h>

#include <assimp/Importer.hpp>

#include "FossenModel.hh"
#include <assimp/scene.h>

#include "Tetrahedron.hh"
#include "PrimitiveMeshGenerator.hh"
#include "ShipHullGenerator.hh"
#include "keship/utils/WaterArea.hh"

namespace keship
{
  /////////////////////////////////////////////////
  /// 浮力体配置（GUI → ModelManager）
  struct BuoyancyBodyConfig
  {
    std::string name;
    PrimitiveType type = PrimitiveType::Box;
    BoxParams box;
    CylinderParams cyl;
    double pose[6] = {0, 0, 0, 0, 0, 0}; // x y z roll pitch yaw
  };

  struct BuoyancyConfig
  {
    std::string linkName = "base_link";
    double fluidDensity = 1000.0;
    std::vector<BuoyancyBodyConfig> bodies;
  };

  /////////////////////////////////////////////////
  /// 推进器配置（GUI → ModelManager）
  enum class ThrusterType { Fixed };  // 后续: Azimuth, Tunnel

  struct ThrusterBodyConfig
  {
    std::string name;
    ThrusterType type = ThrusterType::Fixed;
    double pose[6] = {0, 0, 0, 0, 0, 0};  // x y z roll pitch yaw, 推力沿 local +X
    double maxForce = 1000.0;               // 最大推力 (N)
  };

  struct ThrusterConfig
  {
    std::string linkName = "base_link";
    std::vector<ThrusterBodyConfig> bodies;
  };

  /////////////////////////////////////////////////
  /// 流体动力配置（GUI → ModelManager）
  struct HydroConfig
  {
    std::string linkName = "base_link";
    std::array<double, 36> addedMass{};  // 6×6 row-major
    std::array<double, 8>  damping{};
    std::vector<DampingTerm> dampingTerms;
    std::string dampingMode = "fossen";  // "fossen" | "pose_based"
    double dampingRatio = 0.1;
  };

  /////////////////////////////////////////////////
  /// 传感器类型枚举
  enum class SensorType { IMU, NavSat, Magnetometer, Camera, Lidar };
  inline const char *sensorTypeName(SensorType _t)
  {
    switch (_t) {
      case SensorType::IMU:          return "IMU";
      case SensorType::NavSat:       return "NavSat";
      case SensorType::Magnetometer: return "Magnetometer";
      case SensorType::Camera:       return "Camera";
      case SensorType::Lidar:        return "Lidar";
    }
    return "Unknown";
  }
  inline SensorType sensorTypeFromName(const std::string &_s)
  {
    if (_s == "IMU")          return SensorType::IMU;
    if (_s == "NavSat")       return SensorType::NavSat;
    if (_s == "Magnetometer") return SensorType::Magnetometer;
    if (_s == "Camera")       return SensorType::Camera;
    if (_s == "Lidar")        return SensorType::Lidar;
    return SensorType::IMU; // fallback
  }

  /// 单个传感器实例配置
  struct SensorInstance
  {
    SensorType type = SensorType::Camera;
    std::string name;
    double pose[6] = {0, 0, 0, 0, 0, 0};  // x y z roll pitch yaw
  };

  /// 传感器配置（GUI → ModelManager）
  struct SensorConfig
  {
    std::string linkName = "base_link";
    std::vector<SensorInstance> instances;

    bool empty() const { return instances.empty(); }
    bool has(SensorType _t) const {
      for (auto &i : instances) if (i.type == _t) return true;
      return false;
    }
    int count(SensorType _t) const {
      int n = 0;
      for (auto &i : instances) if (i.type == _t) n++;
      return n;
    }
  };

  /////////////////////////////////////////////////
  enum class TestShapeType : int
  {
    Cube = 0,
    Sphere = 1,
    Cylinder = 2,
    Cone = 3,
  };

  /////////////////////////////////////////////////
  struct MeshData
  {
    std::vector<aiVector3D> vertices;              // 顶点坐标数组 [x,y,z,x,y,z,...]
    std::vector<unsigned int> indices;             // 三角形索引数组 [i0,i1,i2, i0,i1,i2,...]
    std::vector<float> normals;                    // 法线数组（可选）
    std::vector<float> texCoords;                  // 纹理坐标数组（可选）
    std::vector<keship::Tetrahedron> tetrahedrons; // 四面体列表
  };

  /////////////////////////////////////////////////
  /// 惯性参数（质量 + 惯性张量 + 重心偏移）
  struct InertiaParams
  {
    double mass = 0;
    double ixx = 0, ixy = 0, ixz = 0;
    double iyy = 0, iyz = 0;
    double izz = 0;
    double cogX = 0, cogY = 0, cogZ = 0;
  };

  /////////////////////////////////////////////////
  /// 数学船型的插件可选参数（2026-09-19 新增，供无头指令入口 keship_ctl 表达实验模型）
  ///
  /// **默认全部关闭 → SpawnMathHull 产出的 SDF 与改动前逐字节一致**，GUI 路径零变化。
  /// 为什么需要：原实现硬编码 `<enable_hydro>false</enable_hydro>`，生成的是
  /// **零阻尼**船体 —— 网格船体没有 pose_based 阻尼（该分支需 hasCylinderGeometry_），
  /// 会长时间垂荡；这正是 doc/plans/ui_improvement_plan.md §7.5 里 ① 的同一类缺陷。
  struct MathHullPluginOpts
  {
    bool enableHydro = false;        ///< <enable_hydro>；开启后才接受 <linear_damping>
    bool publishSubVolume = false;   ///< <publish_subvolume>；发布 /keship/sp01/subvol/<名>
    double damping[3] = {0, 0, 0};   ///< <linear_damping> 的 Zw / Kp / Mq（全 0 = 不写该段）
    bool hasDamping() const
    { return damping[0] > 0.0 || damping[1] > 0.0 || damping[2] > 0.0; }
  };

  /////////////////////////////////////////////////
  class ModelManager
  {
  public:
    /// \brief 构造函数
    ModelManager();
    /// \brief 构造函数
    ModelManager(const std::string &_worldName);

    /// \brief 析构函数
    ~ModelManager();

    /**
     * @brief 在Gazebo中从文件生成模型
     *
     * @param _worldName 世界名称
     * @param _instanceName 实例名称
     * @param _filePath 模型文件路径
     * @param _pose 模型位置和旋转 (x y z roll pitch yaw)
     * @return true 成功
     * @return false 失败
     */
    bool SpawnModelInGazeboFromFile(
        // const std::string &_worldName,
        const std::string &_instanceName,
        const std::string &_filePath,
        double _pos[6],
        bool _isStatic,
        bool _isAddBuoyancy);

    bool SpawnModelInGazeboFromShape(
        // const std::string &_worldName,
        const std::string &_instanceName,
        const TestShapeType &_shapeType,
        const std::vector<double>& _shapeParams,
        double _pos[6],
        bool _isStatic,
        bool _isAddBuoyancy);

    /// @brief 生成圆柱体浮力测试模型
    /// @param _skipSpawn true 时仅缓存 SDF+配置不 spawn（等后续 SpawnModelWithBuoyancy 调用）
    bool SpawnCylinderInGazebo(
        const std::string &_instanceName,
        double _diameter,
        double _length,
        double _density,
        double _pos[6],
        bool _skipSpawn = false);

    /// @brief 生成数学船型模型（Phase B）
    /// @param _instanceName 实例名称
    /// @param _spec 船型规格
    /// @param _pos 初始位姿
    /// @param _opts 插件可选参数。**默认全部关闭 = 与既有行为逐字节一致**
    ///              （原实现硬编码 `<enable_hydro>false</enable_hydro>`，
    ///               无阻尼、无浸水体积发布 —— 网格船体在零阻尼下会长时间垂荡，
    ///               见 doc/plans/ui_improvement_plan.md §7.5）
    bool SpawnMathHull(const std::string &_instanceName,
                       const ShipHullSpec &_spec,
                       double _pos[6],
                       const MathHullPluginOpts &_opts = MathHullPluginOpts{});

    /// @brief 从有效文件类型生成SDF字符串
    /// @param _instanceName 实例名称
    /// @param _filePath 文件路径
    /// @param isStatic 是否静态模型
    /// @param isBuoyancy 是否添加浮力项
    /// @return SDF字符串
    std::string MakeSDFStringFromValidFileType(const std::string &_instanceName, const std::string &_filePath, bool isStatic, bool isBuoyancy,
        bool _addInertia = false, const InertiaParams &_inertia = InertiaParams{});
    std::string MakeSDFStringFromSTL(const std::string &_instanceName, const std::string &_stlFilePath);
    

    std::string MakeSDFString_TestShape(const TestShapeType &_shapeType, const std::vector<double>& _shapeParams);

    /// @brief 从DAE文件加载网格数据
    /// @param _daePath DAE文件路径
    /// @param _linkName 链接名称
    /// @return 是否成功加载
    bool LoadMeshDataFromDae(const std::string &_daePath, MeshData &_meshData);

    /// @brief 从网格数据生成四面体
    /// @param _meshData 网格数据
    /// @return 是否成功生成四面体
    bool GenerateTetrahedronsFromMesh(MeshData &_meshData);

    /**
     * @brief 在Gazebo中从SDF字符串生成模型
     *
     * @param _worldName 世界名称
     * @param _instanceName 实例名称
     * @param _sdfContent SDF内容字符串
     * @return true 成功
     * @return false 失败
     */
    bool SpawnModelInGazeboFromSDFString(
        // const std::string &_worldName,
        const std::string &_instanceName,
        const std::string &_sdfContent = "");

    /**
     * @brief 删除Gazebo中的模型
     *
     * @param _worldName 世界名称
     * @param _modelName 模型名称
     * @return true 成功
     * @return false 失败
     */
    bool DeleteModelInGazeboByName(const std::string &_instanceName,
        bool _preserveSource = false);
    bool DeleteModelInGazeboByIndex(int _index);
    std::pair<std::string, uint32_t> GetModelNameAndIDByIndex(int _index);
    bool GetModelPoseByIndex(int _index, gz::msgs::Pose &_pose);
    bool GetModelPoseByName(const std::string &_instanceName, gz::msgs::Pose &_pose);
    bool GetModelPoseByIDFromGazebo(uint32_t _id, double _pose[6]);
    bool GetModelIDByInstanceNameFromGazebo(const std::string &_instanceName, uint32_t &_id);

    bool DeleteAllModelsInGazebo();

    bool SetModelPoseByIDInGazebo(uint32_t _id, double _pose[6]);

    /// @brief 重建模型并应用浮力体配置（保存pose → 删除 → 生成新SDF → spawn → 恢复pose）
    bool RebuildModelWithBuoyancy(const std::string &_instanceName,
                                  const BuoyancyConfig &_config);

    /// @brief 重建模型并应用推进器配置（保留已有浮力体）
    bool RebuildModelWithThrust(const std::string &_instanceName,
                                const ThrusterConfig &_config);

    /// @brief 生成合并的浮力-水动力插件 SDF（BuoyancyConfig + 可选 HydroConfig）
    static std::string MakeBuoyancyPluginSDF(const BuoyancyConfig &_config,
        const HydroConfig *_hydro = nullptr);

    /// @brief 生成推进器插件 SDF 字符串
    static std::string MakeThrustPluginSDF(const ThrusterConfig &_config);

    /// @brief 生成传感器 SDF 字符串（放在 <link> 内部）
    static std::string MakeSensorSDF(const SensorConfig &_config,
                                      const std::string &_modelName,
                                      const std::string &_worldName = "keship_world");

    /// @brief 重建模型并应用流体动力配置（保留已有浮力体/推进器）
    bool RebuildModelWithHydro(const std::string &_instanceName,
                               const HydroConfig &_config);

    /// @brief 获取/缓存流体动力配置
    bool GetCachedHydroConfig(const std::string &_name, HydroConfig &_out) const;
    void CacheHydroConfig(const std::string &_name, const HydroConfig &_cfg)
    { hydroConfigCache_[_name] = _cfg; }

    /// @brief 重建模型并应用传感器配置（保留已有浮力体/推进器/流体动力）
    bool RebuildModelWithSensors(const std::string &_instanceName,
                                  const SensorConfig &_config);

    /// @brief 获取/缓存传感器配置
    bool GetCachedSensorConfig(const std::string &_name, SensorConfig &_out) const;
    void CacheSensorConfig(const std::string &_name, const SensorConfig &_cfg)
    { sensorConfigCache_[_name] = _cfg; }

    /// @brief 保存模型为 .ksm 文件
    /// @param _sdfName 保存到 BASE_SDF 段的模型原名（空 = 原样使用缓存 SDF 内名称）
    bool SaveModelToKSM(const std::string &_name, const std::string &_filepath,
                        const std::string &_sdfName = "");

/// @brief 来源世界文件信息（0.0.4：保存时按子 World 链还原用）
    struct WorldModelSource
    {
      std::string kswPath;          /// 来源 KSW 绝对路径（空 = 独立新增）
      std::vector<std::string> ns;  /// 载入时的命名空间标签（如 前海，仅影响实例名）
      std::vector<std::string> path;/// 子 World 命名链（如 前海东部/内湾，空 = 顶层）
      std::string baseName;         /// JSON 原键名（叶子标签，如 KeShip03）
    };

    /// @brief 水区来源（0.0.4：保存时还原子 World 嵌套位置）
    struct WaterWorldSource
    {
      std::vector<std::string> path;    /// 子 World 命名链（空 = 顶层 waters）
      std::string baseName;             /// JSON 原键名
    };

    /// @brief 0.0.4 世界清单模型叶子条目（载入时展平结果）
    struct WorldManifestItem
    {
      std::string baseName;                     /// JSON 原键名（叶子标签）
      std::vector<std::string> path;            /// 子 World 命名链（空 = 顶层）
      std::string ksmPath;                      /// 展开后的绝对路径
      std::array<double, 6> pose{};             /// x y z r p y
      bool isStatic = false;
    };

    /// @brief 0.0.4 世界清单水域叶子条目（载入时展平结果）
    struct WorldWaterItem
    {
      std::vector<std::string> path;            /// 子 World 命名链（空 = 顶层）
      std::string baseName;                     /// JSON 原键名
      WaterArea area;                           /// 完整水域（attributes + fluids 已归并）
    };

    /// @brief 保存世界清单（0.0.4 四段结构 → JSON，单文件夹产物）
    /// @param _dirpath 目标世界文件夹（已创建）
    /// @param _worldName 世界名（= attributes.name 与目录名）
    /// @param _selectedModels 勾选的实例名列表（空 = 全部场景模型；
    ///        但 _rootChain 非空时空 = 仅导出水域，不导出模型）
    /// @param _waterAreasJSON 水区 JSON（空 = 不保存水域）
    /// @param _failCount 输出保存失败模型计数
    /// @param _rootChain 子 World 链（空 = 保存整个世界）；非空 = 仅导出该链内
    ///        实体并剥离链前缀（子 ksw 独立导出用）
    bool SaveWorldManifest(const std::string &_dirpath,
        const std::string &_worldName,
        const std::vector<std::string> &_selectedModels = {},
        const std::string &_waterAreasJSON = "",
        int *_failCount = nullptr,
        const std::string &_rootChain = "");

    /// @brief 加载世界清单（0.0.4 递归解析，返回展平条目）
    /// @param _filepath .ksw 文件绝对路径
    /// @param _out 输出展平的模型条目（path=子链，baseName=JSON 原键名）
    /// @param _waters 输出展平的水域条目
    /// @param _worldName 输出世界名（attributes.name）
    /// @param _worldPose 输出世界整体位姿（attributes.pose，可为 nullptr）
    /// @return 版本非 0.0.4 或解析失败返回 false
    bool LoadWorldManifest(const std::string &_filepath,
        std::vector<WorldManifestItem> &_out,
        std::vector<WorldWaterItem> &_waters,
        std::string *_worldName = nullptr,
        std::array<double, 6> *_worldPose = nullptr);

    /// @brief 模型来源元数据：登记 / 查询 / 清理
    void SetModelSource(const std::string &_instanceName,
        const WorldModelSource &_src);
    bool GetModelSource(const std::string &_instanceName,
        WorldModelSource &_out) const;
    void ClearModelSources();
    const std::map<std::string, WorldModelSource> &GetModelSources() const
    { return modelSource_; }

    /// @brief 水域来源元数据：登记 / 查询 / 清理
    void SetWaterSource(const std::string &_instanceName,
        const WaterWorldSource &_src);
    bool GetWaterSource(const std::string &_instanceName,
        WaterWorldSource &_out) const;
    void ClearWaterSources();
    /// @brief 按实例名移除单个水域来源元数据
    void RemoveWaterSource(const std::string &_instanceName);
    const std::map<std::string, WaterWorldSource> &GetWaterSources() const
    { return waterSource_; }

    /// @brief 解析 .ksm 文件，返回四段配置
    static bool ParseKSMFile(const std::string &_filepath,
                             std::string &_baseSDF,
                             BuoyancyConfig &_buoyCfg,
                             ThrusterConfig &_thrustCfg,
                             HydroConfig &_hydroCfg,
                             SensorConfig &_sensorCfg);

    /// @brief 将 SDF 字符串中所有文件 mesh <uri> 替换为规范化缓存 OBJ 路径
    ///
    /// 统一加载层坐标系：任意格式（DAE/OBJ/STL/glTF）模型经
    /// NormalizeMeshToSDFCache 规范化为 Z-UP 标准 OBJ，使视觉/碰撞/预览
    /// 与物理配置（CoG/浮力体/传感器）一致。非 file:// 与白名单外 uri 原样保留。
    /// @param _sdf 含 <mesh><uri>file://... 的 SDF 字符串
    /// @param _sdfBaseDir SDF 文件所在目录，用于解析相对路径（可为空）
    /// @return 替换后的 SDF
    std::string NormalizeMeshUrisInSDF(const std::string &_sdf,
                                       const std::string &_sdfBaseDir = "");

    /// @brief 从 SDF 字符串中解析惯性参数（独立于缓存）
    static bool ParseInertiaFromSDF(const std::string &_sdf, InertiaParams &_out);

    /// @brief [Deprecated] 不再使用。M_A 现在完全在插件中显式计算。
    ///        之前将 fluid_added_mass 注入 SDF 以便 DART 隐式处理。
    /// @param _sdf [in/out] 缓存的 base SDF
    /// @param _addedMass 6x6 row-major 附加质量（无量纲系数）
    /// @return true 注入成功
    static bool InjectFluidAddedMass(std::string &_sdf,
                                      const double _addedMass[36]);

    /// @brief 从 SDF 字符串中解析静态标记
    static bool ParseStaticFromSDF(const std::string &_sdf, bool &_out);

    /// @brief 获取模型缓存的浮力体配置（用于编辑器还原）
    bool GetCachedBuoyancyConfig(const std::string &_name,
                                  BuoyancyConfig &_out) const;

    /// @brief 获取模型缓存的推进器配置（用于编辑器还原）
    bool GetCachedThrusterConfig(const std::string &_name,
                                  ThrusterConfig &_out) const;

    /// 手动缓存 SDF / 浮力体配置（用于尚未 spawn 的模型）
    /// 入库前统一做 mesh uri 规范化，保证后续 Rebuild 使用标准坐标系
    void CacheModelSDF(const std::string &_name, const std::string &_sdf)
    { sdfCache_[_name] = NormalizeMeshUrisInSDF(_sdf); }
    std::string GetCachedModelSDF(const std::string &_name) const
    {
      auto it = sdfCache_.find(_name);
      return (it != sdfCache_.end()) ? it->second : "";
    }
    void CacheBuoyancyConfig(const std::string &_name, const BuoyancyConfig &_cfg)
    { buoyancyConfigCache_[_name] = _cfg; }
    void CacheThrusterConfig(const std::string &_name, const ThrusterConfig &_cfg)
    { thrusterConfigCache_[_name] = _cfg; }

    /// 首次生成模型并附加浮力体配置
    bool SpawnModelWithBuoyancy(const std::string &_instanceName,
                                const BuoyancyConfig &_config,
                                const double _pos[6] = nullptr);

    /// 首次生成模型并附加推进器配置（保留已有浮力体）
    bool SpawnModelWithThrust(const std::string &_instanceName,
                              const ThrusterConfig &_config,
                              const double _pos[6] = nullptr);

    /**
     * @brief 获取模型列表，返回值不能修改
     *
     * @param _worldName 世界名称
     * @return std::vector<std::string> 模型名称列表
     * const 表示 不能修改 modelList
     *
     */
    std::map<std::string, uint32_t> GetModelList() const;

    /// \brief 获取内部 transport 节点引用（供 marker 等复用）
    gz::transport::Node &GetNode() { return *node; }

    /// \brief 获取世界名称
    const std::string &GetWorldName() const { return worldName; }

    /// \brief 通知 ROS 2 侧模型创建/删除
    void NotifyModelSpawned(const std::string &_name, const SensorConfig &_sensorCfg);
    void NotifyModelDeleted(const std::string &_name);

    /// 从缓存的 base SDF 中解析惯性参数
    bool GetCachedModelInertia(const std::string &_name,
        InertiaParams &_out) const;

    /// 更新缓存 SDF 中的惯性参数
    bool UpdateCachedSDFInertia(const std::string &_name,
        const InertiaParams &_inertia);

    /// 用所有缓存配置重建模型（不改任何配置，仅重新 assemble+spawn）
    bool RebuildModelWithAllPlugins(const std::string &_instanceName);

    /// 模型几何体类型
    enum class ModelGeometryType { Box, Cylinder, Mesh };

    /// 从缓存的 base SDF 中解析模型 visual 几何体
    bool GetCachedModelGeometry(const std::string &_name,
        ModelGeometryType &_type, double &_dx, double &_dy, double &_dz,
        double &_radius, double &_length, std::string &_meshPath) const;

    /// @brief 获取模型静态标记（优先查缓存，未命中则从 SDF 解析）
    bool GetCachedModelStatic(const std::string &_name, bool &_isStatic) const;

    /// spawn 后调用，计算并缓存模型的 2D 局部包围盒
    void CacheModelFootprint(const std::string &_name);

    /// 获取包围盒缓存（只读，供 MenuPluginKeship::GatherSceneFootprints 使用）
    const std::map<std::string, keship::ModelFootprint> &GetBBoxCache() const
    { return modelBBoxCache_; }

    /// 确保所有 mesh 模型的指定轮廓方案已计算（懒加载）
    void EnsureFootprintOutlines(keship::ModelRenderScheme _scheme);

  private:
    /// \brief 将 SDF 规范化（mesh uri → 缓存 OBJ）后写入 SDF 缓存
    void CacheNormalizedSDF(const std::string &_name, const std::string &_sdf)
    { sdfCache_[_name] = NormalizeMeshUrisInSDF(_sdf); }

    /// \brief 节点用于通信
    std::unique_ptr<gz::transport::Node> node;
    /// \brief 模型列表，map<实例名称, 模型索引>
    std::map<std::string, uint32_t> modelList;
    /// \brief 模型 SDF 缓存（不含浮力插件），用于重建时保留原始几何体
    std::map<std::string, std::string> sdfCache_;
    /// \brief 浮力体配置缓存，用于编辑器还原
    std::map<std::string, BuoyancyConfig> buoyancyConfigCache_;
    /// \brief 推进器配置缓存，用于编辑器还原
    std::map<std::string, ThrusterConfig> thrusterConfigCache_;
    /// \brief 流体动力配置缓存，用于编辑器还原
    std::map<std::string, HydroConfig> hydroConfigCache_;
    /// \brief 传感器配置缓存，用于编辑器还原
    std::map<std::string, SensorConfig> sensorConfigCache_;
    /// \brief 静态模型标记缓存
    mutable std::map<std::string, bool> staticCache_;
    /// \brief 模型局部包围盒缓存（spawn 时填充，删除时清理）
    std::map<std::string, keship::ModelFootprint> modelBBoxCache_;
    /// \brief 模型来源元数据（0.0.4 世界实例化登记，保存子 World 嵌套使用）
    std::map<std::string, WorldModelSource> modelSource_;
    /// \brief 水域来源元数据（0.0.4 世界实例化登记，保存子 World 嵌套使用）
    std::map<std::string, WaterWorldSource> waterSource_;
    /// \brief 规范化缓存绝对路径 → 源 mesh uri（robot_model/... 相对形式），保存 ksm 时反查
    std::map<std::string, std::string> meshCacheToSrcUri_;
    /// \brief 世界名称
    std::string worldName;
  };



} // namespace keship

#endif // MODEL_MANAGER_HH_