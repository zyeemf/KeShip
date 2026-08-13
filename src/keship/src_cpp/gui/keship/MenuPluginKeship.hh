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
  /// \brief 圆柱体浮力测试
  protected slots: void OnBtn_CylinderBuoyancyTest();
  /// \brief SP-02 漂浮圆柱体初稳性
  protected slots: void OnBtn_SP02_Stability();
  /// \brief SP-03 初稳性倾斜实验
  protected slots: void OnBtn_SP03_Inclining();
  /// \brief SP-04 数学船型稳性对比
  protected slots: void OnBtn_SP04_HullStability();
  /// \brief 编程计算排水量 (Phase D)
  protected slots: void OnBtn_PR01_CodeRunner();
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
  private: bool SetSimulationPaused(bool _pause);

  /// \brief 单例化获取编辑器对话框（key = 类型/模型名）。
  ///        已有同 key 窗口则置前返回 nullptr；否则用 _factory 创建并注册。
  private: QDialog *acquireEditor(const QString &_key,
                                  std::function<QDialog *()> &&_factory);

  private: std::map<QString, QDialog *> openViewers_;
  /// \brief 编辑器对话框单例表（key = 类型/模型名）
  private: std::map<QString, QDialog *> editorDialogs_;

  private: QDialog *sp01Dialog_ = nullptr;
  private: QDialog *sp02Dialog_ = nullptr;
  private: QDialog *sp03Dialog_ = nullptr;

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
  /// \brief 通过 PMS 控制 topic 下发波浪参数（KSPlugin_WavesModel 应用；
  ///        _areaName 空 = 更新全部水区）
  private: void PublishWaveParams(const keship::WaveParams &_wp,
                                  const std::string &_areaName = "");
  /// \brief 持久发送器：避免每次新建 Advertise 的握手竞态导致消息丢失
  private: gz::transport::Node::Publisher waveParamsPub_;
  private: std::string waveCtlTopic_ = "/keship/wavefield/parameters_set";

  private: struct CachedPose { double x = 0, y = 0, yaw = 0; };
  private: std::map<std::string, CachedPose> modelPoseCache_;
  private: mutable std::mutex modelPoseCacheMutex_;
  private: std::vector<keship::ModelFootprint> GatherSceneFootprints() const;
};

#endif
