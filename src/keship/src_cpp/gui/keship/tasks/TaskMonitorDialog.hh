/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: 任务监控面板 — 实时监控 + 自动评分 */

#ifndef TASK_MONITOR_DIALOG_HH_
#define TASK_MONITOR_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/double.pb.h>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "MapWidget.hh"
#include "TaskTomlParser.hh"
#include "TaskScoring.hh"

/// 线程安全共享状态（生命周期独立于对话框，支持 gz-transport 回调安全访问）
struct TaskMonitorState {
  std::mutex mutex;
  bool alive = true;

  // 船位（导航惯例：0=北/+Y，顺时针）
  double shipX = 0, shipY = 0, shipHdg = 0;
  bool poseFound = false;

  // 推力（所有推进器指令推力之和，绝对值）
  double totalThrust = 0;
  bool thrustFound = false;

  // 任务配置
  keship::TaskConfig config;

  // 计时（steady_clock 精确计时）
  std::chrono::steady_clock::time_point startTime;
  bool isRunning = false;
  bool isFinished = false;
  double elapsedSec = 0;

  // 航路点进度
  int currentWaypointIdx = 0;
  std::vector<bool> waypointReached;

  // 评分数据累积
  std::vector<double> posErrors;
  std::vector<double> lateralDevs;
  std::vector<double> driftDists;
  std::vector<double> headingErrors;
  std::vector<double> thrustSamples;
  double maxLateralDev = 0;

  // 最终评分
  keship::ScoreResult finalScore;
};

class TaskMonitorDialog : public QDialog {
  Q_OBJECT
 public:
  TaskMonitorDialog(gz::transport::Node &_transportNode,
                    const std::string &_worldName,
                    rclcpp::Node::SharedPtr _rosNode,
                    QWidget *_parent = nullptr);
  ~TaskMonitorDialog() override;

  /// 加载 TOML 任务文件
  bool loadTask(const std::string &_tomlPath);

  /// 向受控船下拉列表添加模型
  void addModel(const QString &_name);

  /// 按模型存储推进器信息（供推力监控用）
  void setThrusterInfo(const std::string &_modelName,
                       const std::vector<std::string> &_thrusterNames,
                       double _totalMaxForce);

 private slots:
  void onUpdateDisplay();   // 30Hz: 地图 + 状态刷新
  void onUpdateSecond();    // 1Hz: 时间/秒级状态
  void onStartStop();
  void onEndTask();

 private:
  void setupUI();
  void initPoseSubscription();
  void initThrustSubscriptions(const std::string &_modelName);
  void unsubscribeThrust();
  void checkWaypointProgress();
  void checkTrackingDeviation();
  void checkDpDrift();
  void finishTask();
  void publishTaskStatus();    // 发布任务状态到 gz-transport 供学生控制程序订阅
  void initTaskPublisher();    // 初始化任务状态发布器

  // gz transport
  gz::transport::Node &node_;
  std::string worldName_;
  std::string poseTopic_;
  std::vector<std::string> thrustTopics_;

  // ROS 2 任务状态发布
  rclcpp::Node::SharedPtr rosNode_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr taskStatusPub_;
  std::string taskRosTopic_;
  int publishCounter_ = 0;

  // 共享状态
  std::shared_ptr<TaskMonitorState> state_;

  // 定时器
  QTimer *displayTimer_;    // 33ms (~30Hz)
  QTimer *secondTimer_;     // 1000ms (1Hz)

  // UI
  MapWidget *map_;
  QLabel *nameLabel_, *timeLabel_, *scoreLabel_, *wpLabel_;
  QLabel *distLabel_, *hdgLabel_, *thrustLabel_;
  QComboBox *modelCombo_;
  QPushButton *startBtn_, *loadBtn_, *endBtn_, *closeBtn_;

  // 推进器信息（按模型名索引）
  std::string currentModel_;
  std::map<std::string, std::vector<std::string>> thrusterNamesMap_;
  std::map<std::string, double> maxThrustMap_;
};

#endif
