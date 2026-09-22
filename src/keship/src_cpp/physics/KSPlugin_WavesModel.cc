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
#include "components/EnvFlow.hh"

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

  /// \brief 每水区一个发布者：向 `/keship/wavefield/<水区名>/parameters` 广播参数。
  ///
  /// ★ 2026-09-21（缺陷 W-2）：WaveVisual 订阅的正是这个 topic，但**此前全项目无人发布** ——
  /// 于是视觉水面永远停留在"生成水面模型那一刻的 SDF 快照"，
  /// 实验对话框设静水后船按静水受力、**画面里的水面却仍在起伏**。
  /// 这里补上发布端，WaveVisual 侧零改动（其 OnWavefield 已实现变化检测与重传）。
  public: std::map<std::string, gz::transport::Node::Publisher> wavePubs_;

  /// \brief 待补发次数（解决发布者/订阅者握手竞态：刚 Advertise 时订阅端可能尚未连上）
  public: std::map<std::string, int> pendingPublish_;

  /// \brief 各水区的「流」参数副本（2026-09-21，缺陷 W-3 的视觉侧接入）
  ///
  /// 流速/流向此前只存在于数据结构与 JSON 里、**没有任何消费者**。
  /// 这里随波浪参数一并广播给 `WaveVisual`，由它驱动水面细节的**定向漂移**。
  public: std::map<std::string, CurrentParams> currents_;

  public: void OnPms(const gz::msgs::Param &_msg);

  /// \brief 把某水区的当前参数标记为"需补发"（随后 N 帧各发一次）
  public: void MarkPublish(const std::string &_areaName);

  /// \brief 每帧把待补发的参数发出去
  public: void FlushPendingPublish();
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
  this->dataPtr->simTime_ = _info.simTime;

  /// 暂停期也执行 JSON 重建：GUI 载入世界期间仿真暂停（防模型跳起），
  /// 若暂停即返回，波浪场要到 resume 后 1s 才建立，而船"进入水区"的
  /// 波浪场查找在 resume 后首帧一次性执行 → 错过 → 静水（2026-08-12 修复：
  /// 暂停期 GUI 写完 JSON 的下一帧就建波浪场，resume 前已就绪）
  if (!this->dataPtr->initialized_)
  {
    this->dataPtr->RebuildFromJson(_ecm);
    this->dataPtr->initialized_ = true;
  }

  /// 每帧检查 JSON mtime（RebuildFromJson 内部有 mtime 守卫，
  /// 文件未变化立即返回，无重复工作）。替代原"每 1s 仿真时间轮询"，
  /// 暂停期不再被跳过，GUI 写入后下一帧生效
  this->dataPtr->RebuildFromJson(_ecm);

  /// 应用待处理 PMS 消息（实验对话框/环境编辑，立即生效）
  this->dataPtr->ApplyPendingPms(_ecm);

  /// 把待补发的波浪参数广播给 WaveVisual（★ W-2：视觉水面与物理同源）
  this->dataPtr->FlushPendingPublish();

  /// 统一移除待删除实体
  for (auto e : this->dataPtr->entitiesToRemove_)
    _ecm.RequestRemoveEntity(e, true);
  this->dataPtr->entitiesToRemove_.clear();
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::MarkPublish(const std::string &_areaName)
{
  /// 20 帧 ≈ 0.33 s（gz 默认 60 Hz 物理步长下按仿真步计），足够跨过握手窗口
  this->pendingPublish_[_areaName] = 20;
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::FlushPendingPublish()
{
  if (this->pendingPublish_.empty())
    return;

  for (auto it = this->pendingPublish_.begin(); it != this->pendingPublish_.end(); )
  {
    const std::string &name = it->first;
    auto wfIt = this->wavefields_.find(name);
    if (wfIt == this->wavefields_.end())
    {
      it = this->pendingPublish_.erase(it);
      continue;
    }

    auto pubIt = this->wavePubs_.find(name);
    if (pubIt == this->wavePubs_.end())
    {
      auto pub = this->node.Advertise<gz::msgs::Param>(
          "/keship/wavefield/" + name + "/parameters");
      if (!pub.Valid())
      {
        /// 发布失败：本帧不递减，下帧再试（避免"静默丢失"）
        ++it;
        continue;
      }
      pubIt = this->wavePubs_.emplace(name, std::move(pub)).first;
      gzmsg << "KSPlugin_WavesModel: 发布水区波浪参数话题 ["
            << name << "]" << std::endl;
    }

    /// 波浪参数 + 该水区的「流速/流向」（后者供 WaveVisual 驱动细节漂移）
    gz::msgs::Param msg = wfIt->second->Parameters();
    auto cit = this->currents_.find(name);
    if (cit != this->currents_.end())
    {
      auto &pm = *msg.mutable_params();
      pm["current_speed"].set_type(gz::msgs::Any::DOUBLE);
      pm["current_speed"].set_double_value(cit->second.speed);
      pm["current_direction"].set_type(gz::msgs::Any::DOUBLE);
      pm["current_direction"].set_double_value(cit->second.direction);
    }
    pubIt->second.Publish(msg);

    if (--it->second <= 0)
    {
      it = this->pendingPublish_.erase(it);
      gzmsg << "KSPlugin_WavesModel: 水区 [" << name
            << "] 参数已广播至 WaveVisual" << std::endl;
    }
    else
    {
      ++it;
    }
  }
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::RebuildFromJson(
    gz::sim::EntityComponentManager &_ecm)
{
  /// ★ T5（2026-09-22）：把水区的风/流参数写入 EnvFlow 组件，
  /// 供 KSPlugin_HydroBuoyancy 计算流力与风压力。
  /// 此前 windParams / currentParams 在**物理侧零消费者**（缺陷 W-3），
  /// 见 doc/modules/physics.md §5。新建与更新两条路径都要写。
  auto writeEnvFlow = [&_ecm](gz::sim::Entity _e, const WaterArea &_a) {
    auto *c = _ecm.Component<keship::components::EnvFlow>(_e);
    if (!c)
      c = _ecm.CreateComponent(_e, keship::components::EnvFlow());
    auto &d = c->Data();
    d.currentSpeed     = _a.currentParams.speed;
    d.currentDirection = _a.currentParams.direction;
    d.windSpeed        = _a.windParams.speed;
    d.windDirection    = _a.windParams.direction;
  };

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
    this->wavePubs_.erase(name);
    this->pendingPublish_.erase(name);
    this->currents_.erase(name);
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
      this->currents_[a.name] = a.currentParams;   // W-3：流参数随参数一并广播
      writeEnvFlow(e, a);                          // ★ T5：风/流参数供物理侧消费
      this->MarkPublish(a.name);
    }
    else
    {
      /// 已存在：参数整体更新（JSON 为准）
      it->second->SetWaveParams(a.waveParams);
      this->currents_[a.name] = a.currentParams;
      writeEnvFlow(this->entities_[a.name], a);    // ★ T5：更新路径同样要刷新
      /// ★ W-2：JSON 改了参数（环境编辑器/海况等级/水域重建）也要让视觉水面跟上
      this->MarkPublish(a.name);
    }
  }
}

/////////////////////////////////////////////////
void KSPlugin_WavesModelPrivate::ApplyPendingPms(
    gz::sim::EntityComponentManager &_ecm)
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
      /// 波浪场尚未建立（JSON 轮询滞后于载入世界的 PMS 下发）→
      /// 先按 JSON 重建再查一次，避免消息被提前丢弃（2026-08 竞态修复）
      this->RebuildFromJson(_ecm);
      wfIt = this->wavefields_.find(areaName);
      if (wfIt == this->wavefields_.end())
      {
        gzwarn << "KSPlugin_WavesModel: PMS 目标水区不存在 ["
               << areaName << "]，忽略" << std::endl;
        return;
      }
    }
    wfIt->second->Load(msg);
    this->MarkPublish(areaName);
    gzmsg << "KSPlugin_WavesModel: PMS 更新水区 [" << areaName << "]" << std::endl;
  }
  else
  {
    for (auto &[name, wf] : this->wavefields_)
    {
      wf->Load(msg);
      this->MarkPublish(name);
    }
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
