/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 菜单插件： keship */
/// Zhangyu 创建 2026.1.16

#ifndef GZ_GUI_HELLOPLUGIN_HH_
#define GZ_GUI_HELLOPLUGIN_HH_

#include <string>
#include <list>
#include <thread>
#include <mutex>
#include <map>
#include <functional>

#include <gz/gui/qt.h>
#include <gz/gui/Plugin.hh>
#include <gz/transport/Node.hh>
#include <gz/msgs/entity_factory.pb.h>

#include <QDialog>
#include <QStandardItemModel>
#include <set>

#include <rclcpp/rclcpp.hpp>

#include "ModelManager.hh"
#include "WaterAreaManager.hh"
#include "WaterAreaVisualManager.hh"
#include "keship/utils/KeshipPaths.hh"


class MenuPluginKeship : public gz::gui::Plugin
{
  Q_OBJECT

  Q_PROPERTY(QStringList modelNames READ GetModelNames NOTIFY ModelNamesChanged)
  Q_PROPERTY(bool paused READ isPaused NOTIFY PausedChanged)
  Q_PROPERTY(QStringList sensorList READ GetSensorList NOTIFY SensorListChanged)
  Q_PROPERTY(int selectedIndex READ GetSelectedIndex NOTIFY SelectedIndexChanged)
  Q_PROPERTY(QVariantList modelStaticFlags READ GetModelStaticFlags NOTIFY ModelStaticFlagsChanged)
  Q_PROPERTY(QVariantList modelMathHullFlags READ GetModelMathHullFlags NOTIFY ModelMathHullFlagsChanged)
  Q_PROPERTY(int customWaterAreaCount READ CustomWaterAreaCount NOTIFY ModelNamesChanged)
  Q_PROPERTY(QStringList waterAreaNames READ GetWaterAreaNames NOTIFY ModelNamesChanged)
  Q_PROPERTY(QStringList waterAreaSeaStates READ GetWaterAreaSeaStates NOTIFY ModelNamesChanged)
  Q_PROPERTY(QVariantList worldHierarchy READ GetWorldHierarchy NOTIFY ModelNamesChanged)


  /// \brief Constructor
  public: MenuPluginKeship();
  /// \brief Destructor
  public: virtual ~MenuPluginKeship();

  /// \brief Called by Gazebo GUI when plugin is instantiated.
  public: virtual void LoadConfig(const tinyxml2::XMLElement *_pluginElem)
      override;

  /// \brief Get model names.
  public: QStringList GetModelNames() const;
  /// \brief Get sensor list.
  public: QStringList GetSensorList() const { return sensorList_; }
  /// \brief Get selected index.
  public: int GetSelectedIndex() const { return selectedIndex_; }
  /// \brief Get paused state.
  public: bool isPaused() const { return paused_; }
  /// \brief Get model static flags.
  public: QVariantList GetModelStaticFlags() const;
  public: QVariantList GetModelMathHullFlags() const;
  /// \brief 层次树行（模型/水域/子 ksw 链，平铺含深度；与保存/载入对话框同构）
  public: QVariantList GetWorldHierarchy() const;
  /// \brief Get custom water area count.
  public: int CustomWaterAreaCount() const
      { return (int)customWaterAreas_.size(); }
  /// \brief Get water area names.
  public: QStringList GetWaterAreaNames() const;
  /// \brief Get water area sea levels (自定义水域列表海况列).
  public: QStringList GetWaterAreaSeaStates() const;
  /// \brief Model names changed signal.
  signals: void ModelNamesChanged();
  /// \brief Model static flags changed signal.
  signals: void ModelStaticFlagsChanged();
  signals: void ModelMathHullFlagsChanged();
  /// \brief Paused state changed signal.
  signals: void PausedChanged();
  /// \brief Sensor list changed signal.
  signals: void SensorListChanged();
  /// \brief Selected index changed signal.
  signals: void SelectedIndexChanged();


  /// \brief Load world from file.
  protected slots: void OnBtn_LoadWorld();
  /// \brief Pause/Resume toggle.
  protected slots: void OnPauseToggle();
  /// \brief Save world (all models).
  protected slots: void OnBtn_SaveWorld();
  /// \brief 保存子世界（按子 World 链独立导出；弹对话框确认）
  protected slots: void OnBtn_SaveSubWorld(const QString &_chain);
  /// \brief Set world origin (lat/lon/elev/heading, runtime only).
  protected slots: void OnBtn_WorldSettings();
  /// \brief Load model from file.
  protected slots: void OnBtn_LoadModel();
  /// \brief 载入 .ksm 模型文件（2026-10-25 菜单拆分：只列 .ksm）
  protected slots: void OnBtn_LoadKsmModel();
  /// \brief 载入原始模型文件（stl/dae/obj/sdf/gltf/glb，直接选文件）
  protected slots: void OnBtn_LoadRawModel();
  /// \brief Load shape from file.
  protected slots: void OnBtn_LoadShape(const QString &_shapeType);
  /// \brief Delete model.
  protected slots: void OnBtn_DeleteModelByName(const QString &_instanceName);
  /// \brief Delete selected model.
  protected slots: void OnBtn_DeleteModelByIndex(int _index);
  /// \brief Delete all models.
  protected slots: void OnBtn_DeleteAllModels();
  /// \brief Search model.
  protected slots: void OnSearchModel(const QString &_searchText);
  /// \brief 显示模型属性对话框
  protected slots: void OnBtn_ShowModelSettingsDialogByIndex(int _index);
  /// \brief 复制模型（弹属性对话框，可命名与改位姿）
  protected slots: void OnBtn_DuplicateModelByIndex(int _midx);
  /// \brief 复制水域（弹命名/微调对话框）
  protected slots: void OnBtn_DuplicateWaterArea(int _widx);
  /// \brief 复制子世界（弹命名/整体偏移对话框）
  protected slots: void OnBtn_DuplicateSubWorld(const QString &_chain);
  /// \brief 删除子世界（整链模型+水域，弹确认框）
  protected slots: void OnBtn_DeleteSubWorld(const QString &_chain);
  /// \brief 子世界属性（改名 + 整体位姿偏移 + 实体清单）
  protected slots: void OnBtn_ShowSubWorldSettings(const QString &_chain);
  /// \brief 移动模型到目标子世界链（弹目标链选择对话框，可新建链）
  protected slots: void OnBtn_MoveModelByIndex(int _midx);
  /// \brief 移动水域到目标子世界链（弹目标链选择对话框）
  protected slots: void OnBtn_MoveWaterByIndex(int _widx);
  /// \brief 移动子世界（弹目标父链选择对话框，整链挂到目标父链下）
  protected slots: void OnBtn_MoveSubWorld(const QString &_chain);
  /// \brief 圆柱体浮力测试
  protected slots: void OnBtn_CylinderBuoyancyTest();
  /// \brief HS-03 漂浮圆柱体初稳性
  protected slots: void OnBtn_HS03_Stability();
  /// \brief HS-04 初稳性倾斜实验
  protected slots: void OnBtn_HS04_Inclining();
  /// \brief HS-05 数学船型稳性对比
  protected slots: void OnBtn_HS05_HullStability();
  /// \brief 编程计算排水量 (Phase D)
  protected slots: void OnBtn_HS09_CodeRunner();
  /// \brief MS-01 回转性观测（差动推力回转试验）
  protected slots: void OnBtn_MS01_TurningCircle();
  /// \brief MS-04 方向稳定性与 Z 形试验
  protected slots: void OnBtn_MS04_Zigzag();
  /// \brief MS-02 升沉与纵摇响应（规则波激励）
  protected slots: void OnBtn_MS02_WaveResponse();
  /// \brief MS-03 横摇 RAO 测量
  protected slots: void OnBtn_MS03_Rao();
  /// ★ 2026-10-16 新增：MS-06 RAO 编程计算实践（MS-03 姊妹实验）
  protected slots: void OnBtn_MS06_RaoPractice();
  /// \brief SC-08 传感器感知与位姿估计
  protected slots: void OnBtn_SC08_SensorEstimation();
  /// \brief 任务设定 (Phase 13)
  protected slots: void OnBtn_TaskSet();
  /// \brief 任务监控 (Phase 13)
  protected slots: void OnBtn_TaskMonitor();
  /// \brief 新建数学船型 (Phase B)
  protected slots: void OnBtn_NewMathHull();
  /// \brief 排水量测量实验 (Phase C)
  protected slots: void OnBtn_Displacement();
  /// \brief 编辑浮力体
  protected slots: void OnBtn_EditBuoyancy(int _index);
  /// \brief 编辑推进器
  protected slots: void OnBtn_EditThrust(int _index);
  /// \brief 保存模型为 .ksm
  protected slots: void OnBtn_SaveModel(int _index);
  /// \brief 编辑流体动力系数
  protected slots: void OnBtn_EditHydro(int _index);
  /// \brief 编辑传感器
  protected slots: void OnBtn_EditSensor(int _index);
  /// \brief 传感器下拉选中
  protected slots: void OnSensorSelected(const QString &_sensorId);
  /// \brief 刷新传感器列表
  protected slots: void RefreshSensorList();
  /// \brief 一键添加默认矩形水域（仅无水域时可用）
  protected slots: void OnBtn_AddDefaultWaterArea();
  /// \brief 添加自定义水区
  protected slots: void OnBtn_AddWaterArea();
  /// \brief 环境参数编辑（风浪流；按 Areas 索引）
  protected slots: void OnBtn_ShowWaterEnv(int _index);
  /// \brief 编辑水区（统一入口：绘制编辑）
  protected slots: void OnBtn_EditWaterArea(int _index);
  /// \brief 删除水区
  protected slots: void OnBtn_DeleteWaterArea(int _index);
  /// \brief 惯性计算工具
  protected slots: void OnBtn_InertiaCalculator();

  /// \brief SD-06 代理几何体建模与简化（三档对比：Mesh/ConvexHull/None）
  protected slots: void OnBtn_SD06_ProxyGeometry();

  /// \brief 清理未被 ksm 引用的网格规范化缓存（models_normalized）
  protected slots: void OnBtn_CleanMeshCache();


  private: std::unique_ptr<keship::ModelManager> modelManager;
  private: bool paused_ = false;
  private: QStringList sensorList_;
  private: rclcpp::Node::SharedPtr rosNode_;
  private: std::thread spinThread_;

  private: void openSensorView(const QString &_modelName,
                               const QString &_typeLabel,
                               const QString &_instName);
  private: void CloseStaleViewers();
  private: void NotifyModelListChanged();
  /// \brief 请求设置仿真暂停/继续，并同步 paused_ 与 PausedChanged 信号。
  ///        （所有暂停/恢复统一走此通道，保证按钮状态与真实状态一致）
  /// \brief 文件名合法性检查（两个载入入口共用，勿复制第二份）
  private: bool CheckModelFileName(const QString &_filePath);
  /// \brief .ksm 载入流程（选定文件后调用；与无头入口共用 ModelManager 实现）
  private: void LoadKsmModelFile(const QString &_filePath);
  /// \brief 原始模型载入流程（选定文件后调用）
  private: void LoadRawModelFile(const QString &_filePath);
  private: bool SetSimulationPaused(bool _pause);

  /// \brief 单例化获取编辑器对话框（key = 类型/模型名）。
  ///        已有同 key 窗口则置前返回 nullptr；否则用 _factory 创建并注册。
  private: QDialog *acquireEditor(const QString &_key,
                                  std::function<QDialog *()> &&_factory);

  private: std::map<QString, QDialog *> openViewers_;
  /// \brief 编辑器对话框单例表（key = 类型/模型名）
  private: std::map<QString, QDialog *> editorDialogs_;

  private: QDialog *hs03Dialog_ = nullptr;
  private: QDialog *hs04Dialog_ = nullptr;
  private: QDialog *hs05Dialog_ = nullptr;
  private: QDialog *ms01Dialog_ = nullptr;
  private: QDialog *ms04Dialog_ = nullptr;
  private: QDialog *ms02Dialog_ = nullptr;
  private: QDialog *ms03Dialog_ = nullptr;
  private: QDialog *ms06Dialog_ = nullptr;
  private: QDialog *sc08Dialog_ = nullptr;

  private: QDialog *taskSetDialog_ = nullptr;
  private: QDialog *taskMonitorDialog_ = nullptr;

  private: int selectedIndex_ = -1;
  private: std::set<std::string> mathHullNames_;
  private: keship::WaterAreaManager waterAreaMgr_;
  private: keship::WaterAreaVisualManager waterVisualMgr_;
  private: std::vector<keship::WaterArea> customWaterAreas_;
  private: mutable std::recursive_mutex waterAreaMutex_;
  private: void WriteWaterAreasJson(const std::string &_path = keship::KeshipWaterAreasFile());
  private: void RebuildWaterAreas();
  /// \brief 按名删除单个水域（customs + 管理 + waterSource 清理；不重建视觉）
  private: void DeleteWaterAreaByName(const std::string &_name);
  /// \brief 收集链（含嵌套子链）下全部模型实例名
  private: std::vector<std::string> CollectChainModels(const QString &_chain) const;
  /// \brief 收集链（含嵌套子链）下全部水区实例名
  private: std::vector<std::string> CollectChainWaters(const QString &_chain) const;
  /// \brief 全部现有子世界链（去重排序；含父前缀，如 A、A/B）
  private: QStringList AllExistingChains() const;
  /// \brief 链是否存在（= 是否有实体位于该链或其子链）
  private: bool ChainExists(const QString &_chain) const;
  /// \brief 链前缀重写（改名/整体移动共用）：oldChain 下全部实体 path 前缀替换为 newChain
  /// @return false = 新链已存在冲突或未改动
  private: bool RewriteChain(const QString &_oldChain, const QString &_newChain);
  /// \brief 复制模型核心：缓存全部配置到新名并 spawn，登记来源（baseName=新名防 JSON 键冲突）
  /// @param _chain 目标链（空 = 顶层）；_pos 目标位姿（弧度）
  private: bool DuplicateModelCore(const std::string &_name,
                                   const std::string &_newName,
                                   const QString &_chain,
                                   const double _pos[6]);
  /// \brief 复制水域核心：拷贝 WaterArea + 微调 + 来源登记
  private: bool DuplicateWaterCore(const std::string &_name,
                                   const std::string &_newName,
                                   const QString &_chain,
                                   double _dx, double _dy, double _baseHeight);
  /// \brief 移动模型核心：更新 source.path（含同名 baseName 冲突检查）
  private: bool MoveModelToChain(const std::string &_name, const QString &_chain);
  /// \brief 移动水域核心：更新 waterSource.path（含同名 baseName 冲突检查）
  private: bool MoveWaterToChain(const std::string &_name, const QString &_chain);
  /// \brief 副本名去重（base_副本、base_副本2、…）
  private: static QString UniqueCopyName(const QString &_base,
                                         const std::set<QString> &_existing);
  /// \brief 拆分链串 → 段列表
  private: static std::vector<std::string> SplitChain(const QString &_chain);
  /// \brief 实体所在链（模型：source.path）
  private: static QString ModelChainOf(
      const keship::ModelManager::WorldModelSource &_src);
  /// \brief 实体所在链（水域：source.path）
  private: static QString WaterChainOf(
      const keship::ModelManager::WaterWorldSource &_src);
  /// \brief 通过 PMS 控制 topic 下发波浪参数（KSPlugin_WavesModel 应用；
  ///        _areaName 空 = 更新全部水区）
  private: void PublishWaveParams(const keship::WaveParams &_wp,
                                  const std::string &_areaName = "");
  /// \brief 持久发送器：避免每次新建 Advertise 的握手竞态导致消息丢失

  private: struct CachedPose { double x = 0, y = 0, yaw = 0; };
  private: std::map<std::string, CachedPose> modelPoseCache_;
  private: mutable std::mutex modelPoseCacheMutex_;
  private: std::vector<keship::ModelFootprint> GatherSceneFootprints() const;
};

#endif
