/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 波浪控制话题 `/keship/wavefield/parameters_set` 的**进程内唯一发布器**。
 *
 * ★ 2026-10-16 修复的静默失效缺陷：
 * gz-transport 中**同一 Node 对同一话题第二次 Advertise 会得到无效发布器**
 * （实测 Valid()==false，其 Publish() 静默失败并返回 false）。
 * GUI 主插件在启动/载入世界时即发布过该话题（MenuPluginKeship 载入世界段），
 * 实验对话框与 WaveResponseCore 若各自 Advertise 必然拿到无效发布器 ⇒
 * 发布全部静默失效（MS-02/MS-03 在 GUI 内「施加波浪」无反应、响应为直线，
 * 就是这个缺陷；无头 CLI 是独立进程、首个 Advertise，所以一直正常）。
 *
 * 纪律：全进程（主插件 / 实验对话框 / WaveResponseCore / keship_ctl）
 * **一律经本类发布**，保证每个进程只 Advertise 一次。
 * advertise 失败与 publish 失败都会打错误日志，不再静默。
 */

#ifndef KESHIP_WAVEFIELD_PUBLISHER_HH_
#define KESHIP_WAVEFIELD_PUBLISHER_HH_

#include <iostream>
#include <mutex>

#include <gz/transport/Node.hh>
#include <gz/msgs/param.pb.h>

namespace keship {

class WavefieldPublisher {
 public:
  explicit WavefieldPublisher(gz::transport::Node &_node) : node_(_node) {}

  /// 发布一条波浪参数消息（进程内复用同一发布器；失败打错误日志）
  bool Publish(const gz::msgs::Param &_msg) {
    if (!Publisher().Publish(_msg)) {
      std::cerr << "keship: 波浪参数发布失败（topic " << kWaveCtlTopic
                << "）" << std::endl;
      return false;
    }
    return true;
  }

  bool Valid() const { return Publisher().Valid(); }

 private:
  static constexpr const char *kWaveCtlTopic =
      "/keship/wavefield/parameters_set";

  /// 进程内唯一发布器：首次调用时 Advertise，之后复用。
  /// ⚠ 同一进程对同一话题多次 Advertise 会得到无效发布器（见文件头说明）。
  gz::transport::Node::Publisher &Publisher() const {
    static std::mutex mtx;
    static gz::transport::Node::Publisher pub;
    std::lock_guard<std::mutex> lock(mtx);
    if (!pub.Valid()) {
      pub = node_.Advertise<gz::msgs::Param>(kWaveCtlTopic);
      if (!pub.Valid())
        std::cerr << "keship: 波浪参数话题 advertise 失败 ["
                  << kWaveCtlTopic << "]" << std::endl;
    }
    return pub;
  }

  gz::transport::Node &node_;
};

}  // namespace keship

#endif  // KESHIP_WAVEFIELD_PUBLISHER_HH_
