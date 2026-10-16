/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 波浪参数**进程内快照**：实验关闭时的恢复基准。
 *
 * ★ 2026-10-16 修复（张老师 GUI 验收暴露）：
 * 实验对话框（HS-02/03/04/05）析构时原先**硬编码恢复 gain=1.0** ——
 * 而新增水域默认 0 级海况（gain=0，SeaState::ParamsFromLevel(0)），
 * 于是「关闭实验」反而把波浪从静水开到 1.0，船体开始晃动；
 * MS-02/MS-03 更甚：析构完全不恢复，CWR 规则波持续残留。
 * 该恢复此前因发布器冲突缺陷（同进程重复 Advertise 拿到无效发布器）
 * 从未真正生效，2026-10-16 修复发布通路后错误行为随之暴露。
 *
 * 正确做法：主插件每次「用户级」下发（PublishWaveParams：环境参数对话框 /
 * 海况设置 / 新增水区）时记录各水区当时参数；实验关闭时按快照**逐水区精确
 * 恢复**（带 area 标识），取代一切硬编码。实验自身下发的静水 / CWR 不经
 * PublishWaveParams，不会污染快照。
 */

#ifndef KESHIP_WAVE_PARAM_SNAPSHOT_HH_
#define KESHIP_WAVE_PARAM_SNAPSHOT_HH_

#include <map>
#include <mutex>
#include <string>

#include <gz/transport/Node.hh>
#include <gz/msgs/any.pb.h>
#include <gz/msgs/param.pb.h>

#include "keship/utils/WaterArea.hh"
#include "WavefieldPublisher.hh"

namespace keship {

class WaveParamSnapshot {
 public:
  static WaveParamSnapshot &Instance() {
    static WaveParamSnapshot s;
    return s;
  }

  /// 记录一次「用户级」下发（由主插件 PublishWaveParams 调用）
  void Save(const std::string &_area, const keship::WaveParams &_wp) {
    std::lock_guard<std::mutex> lock(mutex_);
    areas_[_area] = _wp;
  }

  /// 实验关闭时恢复：逐水区按快照下发；无快照则 no-op 并返回 false
  /// （无头 CLI 等无主插件的进程天然走此分支）
  bool Restore(gz::transport::Node &_node) {
    std::map<std::string, keship::WaveParams> snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (areas_.empty()) return false;
      snapshot = areas_;
    }
    bool ok = true;
    for (const auto &kv : snapshot)
      ok = PublishArea(_node, kv.first, kv.second) && ok;
    return ok;
  }

 private:
  WaveParamSnapshot() = default;

  /// 与主插件 PublishWaveParams 完全一致的消息构造（全字段 + area）
  static bool PublishArea(gz::transport::Node &_node,
                          const std::string &_area,
                          const keship::WaveParams &_wp) {
    gz::msgs::Param msg;
    auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value(_wp.model);  (*p)["model"] = v;
    v.set_string_value(_area);      (*p)["area"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(_wp.gain);      (*p)["gain"] = v;
    v.set_double_value(_wp.amplitude); (*p)["amplitude"] = v;
    v.set_double_value(_wp.period);    (*p)["period"] = v;
    v.set_double_value(_wp.direction); (*p)["direction"] = v;
    v.set_double_value(_wp.angle);     (*p)["angle"] = v;
    v.set_double_value(_wp.scale);     (*p)["scale"] = v;
    v.set_double_value(_wp.steepness); (*p)["steepness"] = v;
    v.set_double_value(_wp.tau);       (*p)["tau"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_INT32);
    v.set_int_value(_wp.number);       (*p)["number"] = v;
    return keship::WavefieldPublisher(_node).Publish(msg);
  }

  mutable std::mutex mutex_;
  std::map<std::string, keship::WaveParams> areas_;
};

}  // namespace keship

#endif  // KESHIP_WAVE_PARAM_SNAPSHOT_HH_
