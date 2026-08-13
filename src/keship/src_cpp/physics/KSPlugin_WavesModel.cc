/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 波浪场世界级系统实现 — 见 KSPlugin_WavesModel.hh 头部说明 */

#include "KSPlugin_WavesModel.hh"

#include <fstream>
#include <iterator>
#include <utility>

#include <gz/plugin/Register.hh>
#include <gz/sim/components/Name.hh>

#include "keship/utils/KeshipPaths.hh"
#include "keship/utils/WaterArea.hh"
#include "../utils/WaterAreaSerializer.hh"
#include "components/Wavefield.hh"

using namespace gz;
using namespace keship;

class keship::KSPlugin_WavesModelPrivate
{
  public: ~KSPlugin_WavesModelPrivate();

  /// \brief 从 JSON 加载全部水区，重建波浪场实体集合（仿真线程调用）。
  public: void RebuildFromJson(gz::sim::EntityComponentManager &_ecm);

  /// \brief 应用一条待处理 PMS 消息（仿真线程调用）。
  public: void ApplyPendingPms(gz::sim::EntityComponentManager &_ecm);

  /// \brief 波浪场强引用（唯一所有权，防销毁）
  public: std::map<std::string, Wavefield::Ptr> wavefields_;

  /// \brief 水区名 → 波浪场实体
  public: std::map<std::string, gz::sim::Entity> entities_;

  /// \brief JSON 文件轮询
  public: std::filesystem::file_time_type lastJsonMtime_;
  public: std::chrono::steady_clock::duration lastJsonCheckTime_{-1};
  public: bool initialized_ = false;

  /// \brief 仿真时间
  public: std::chrono::steady_clock::duration simTime_{0};

  /// \brief PMS 控制消息（transport 线程写入，仿真线程消费）
  public: gz::msgs::Param pendingPmsMsg_;
  public: bool hasPendingPms_ = false;
  public: std::mutex pmsMutex_;

  public: gz::transport::Node node;

  /// \brief 待删除实体（下一帧统一移除，避免迭代中修改 ECM）
  public: std::vector<gz::sim::Entity> entitiesToRemove_;

  public: void OnPms(const gz::msgs::Param &_msg);
};

/////////////////////////////////////////////////
KSPlugin_WavesModel::KSPlugin_WavesModel()
    : System(), dataPtr(std::make_unique<KSPlugin_WavesModelPrivate>())
{
}

KSPlugin_WavesModel::~KSPlugin_WavesModel() = default;

/////////////////////////////////////////////////
void KSPlugin_WavesModel::Configure(
    const gz::sim::Entity &,
    const std::shared_ptr<const sdf::Element> &,
    gz::sim::EntityComponentManager &,
    gz::sim::EventManager &)
{
  gzmsg << "KSPlugin_WavesModel: 配置完成（波浪场全局管理）" << std::endl;

  this->dataPtr->node.Subscribe("/keship/wavefield/parameters_set",
      &KSPlugin_WavesModelPrivate::OnPms, this->dataPtr.get());
}

/////////////////////////////////////////////////
void KSPlugin_WavesModel::PreUpdate(
    const gz::sim::UpdateInfo &_info,
    gz::sim::EntityComponentManager &_ecm)
{
  if (_info.paused)
    return;

  this->dataPtr->simTime_ = _info.simTime;

  /// 首次运行：读 JSON 建立波浪场实体（Configure 时实体未必齐备，
  /// 对齐 gz-waves WavesModel 的首次 PreUpdate 加载模式）
  if (!this->dataPtr->initialized_)
  {
    this->dataPtr->RebuildFromJson(_ecm);
    this->dataPtr->initialized_ = true;
  }

  /// 每 1s 轮询 JSON（GUI 修改水区波浪参数 → 立即/1s 内生效）
  /// 注：lastJsonCheckTime_ 初始 -1，首次 PreUpdate 先建立基准时间，
  /// 否则 sinceCheck 恒 0 导致轮询永不执行（2026-08-01 修复）
  if (this->dataPtr->lastJsonCheckTime_ < std::chrono::steady_clock::duration(0))
    this->dataPtr->lastJsonCheckTime_ = this->dataPtr->simTime_;
  double sinceCheck = std::chrono::duration<double>(
      this->dataPtr->simTime_ - this->dataPtr->lastJsonCheckTime_).count();
  if (sinceCheck >= 1.0)
  {
    this->dataPtr->lastJsonCheckTime_ = this->dataPtr->simTime_;
    this->dataPtr->RebuildFromJson(_ecm);
  }

  /// 应用待处理 PMS 消息（实验对话框/环境编辑，立即生效）
  this->dataPtr->ApplyPendingPms(_ecm);

  /// 统一移除待删除实体
  for (auto e : this->dataPtr->entitiesToRemove_)
    _ecm.RequestRemoveEntity(e, true);
  this->dataPtr->entitiesToRemove_.clear();
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::RebuildFromJson(
    gz::sim::EntityComponentManager &_ecm)
{
  std::error_code ec;
  auto mtime = std::filesystem::last_write_time(
      keship::KeshipWaterAreasFile(), ec);
  if (ec || mtime == this->lastJsonMtime_)
    return;
  this->lastJsonMtime_ = mtime;

  std::ifstream f(keship::KeshipWaterAreasFile());
  if (!f.is_open())
    return;
  std::string content((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  auto areas = keship::DeserializeWaterAreas(content);

  gzmsg << "KSPlugin_WavesModel: 加载 " << areas.size() << " 片水区波浪场"
        << std::endl;

  /// 1. 删除已不存在的水区波浪场（含 JSON 为空 = 删除全部）
  std::vector<std::string> toRemove;
  for (const auto &[name, entity] : this->entities_)
  {
    bool exists = false;
    for (const auto &a : areas)
    {
      if (a.name == name) { exists = true; break; }
    }
    if (!exists) toRemove.push_back(name);
  }
  for (const auto &name : toRemove)
  {
    this->entitiesToRemove_.push_back(this->entities_[name]);
    this->entities_.erase(name);
    this->wavefields_.erase(name);
    gzmsg << "KSPlugin_WavesModel: 移除水区波浪场 [" << name << "]" << std::endl;
  }

  /// 2. 创建/更新水区波浪场
  for (auto &a : areas)
  {
    auto it = this->wavefields_.find(a.name);
    if (it == this->wavefields_.end())
    {
      /// 新建波浪场对象 + 实体 + 组件
      auto wf = std::make_shared<Wavefield>();
      wf->SetWaveParams(a.waveParams);
      this->wavefields_[a.name] = wf;

      gz::sim::Entity e = _ecm.CreateEntity();
      _ecm.CreateComponent(e, gz::sim::components::Name("wavefield_" + a.name));
      auto comp = _ecm.CreateComponent(e, keship::components::Wavefield());
      comp->Data() = std::weak_ptr<Wavefield>(wf);
      this->entities_[a.name] = e;

      gzmsg << "KSPlugin_WavesModel: 创建波浪场 [" << a.name
            << "] gain=" << a.waveParams.gain << " entity=" << e << std::endl;
    }
    else
    {
      /// 已存在：参数整体更新（JSON 为准）
      it->second->SetWaveParams(a.waveParams);
    }
  }
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::ApplyPendingPms(
    gz::sim::EntityComponentManager &)
{
  gz::msgs::Param msg;
  {
    std::lock_guard<std::mutex> lock(this->pmsMutex_);
    if (!this->hasPendingPms_)
      return;
    msg = this->pendingPmsMsg_;
    this->hasPendingPms_ = false;
  }

  /// 解析目标水区（可选键 "area"；缺省 = 更新全部水区）
  std::string areaName;
  auto it = msg.params().find("area");
  if (it != msg.params().end() && it->second.has_string_value())
    areaName = it->second.string_value();

  if (!areaName.empty())
  {
    auto wfIt = this->wavefields_.find(areaName);
    if (wfIt == this->wavefields_.end())
    {
      gzwarn << "KSPlugin_WavesModel: PMS 目标水区不存在 ["
             << areaName << "]，忽略" << std::endl;
      return;
    }
    wfIt->second->Load(msg);
    gzmsg << "KSPlugin_WavesModel: PMS 更新水区 [" << areaName << "]" << std::endl;
  }
  else
  {
    for (auto &[name, wf] : this->wavefields_)
      wf->Load(msg);
    gzmsg << "KSPlugin_WavesModel: PMS 更新全部水区 (" << this->wavefields_.size()
          << ")" << std::endl;
  }
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::OnPms(const gz::msgs::Param &_msg)
{
  std::lock_guard<std::mutex> lock(this->pmsMutex_);
  this->pendingPmsMsg_ = _msg;
  this->hasPendingPms_ = true;
}

/////////////////////////////////////////////////
KSPlugin_WavesModelPrivate::~KSPlugin_WavesModelPrivate()
{
  {
    std::lock_guard<std::mutex> lock(this->pmsMutex_);
  }
  node.Unsubscribe("/keship/wavefield/parameters_set");
}

/////////////////////////////////////////////////
GZ_ADD_PLUGIN(keship::KSPlugin_WavesModel,
              gz::sim::System,
              KSPlugin_WavesModel::ISystemConfigure,
              KSPlugin_WavesModel::ISystemPreUpdate)

GZ_ADD_PLUGIN_ALIAS(KSPlugin_WavesModel, "keship::KSPlugin_WavesModel")
