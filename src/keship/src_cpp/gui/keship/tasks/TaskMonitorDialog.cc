/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: 任务监控面板实现 */

#include "TaskMonitorDialog.hh"
#include "TopicUtil.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QString>

#include <gz/math/Quaternion.hh>
#include <cmath>
#include <iostream>
#include <sstream>
#include "keship/utils/KeshipPaths.hh"

// ═══════════════════════════════════════════════════════════
// 工具函数
// ═══════════════════════════════════════════════════════════

/// 规范化航向差 [-180, 180]
static double headingDiff(double _a, double _b) {
  double d = _a - _b;
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}

/// 点到线段的最短距离
static double pointToSegmentDist(double _px, double _py,
                                  double _ax, double _ay,
                                  double _bx, double _by) {
  double abx = _bx - _ax, aby = _by - _ay;
  double len2 = abx * abx + aby * aby;
  if (len2 < 1e-12) return std::hypot(_px - _ax, _py - _ay);
  double t = ((_px - _ax) * abx + (_py - _ay) * aby) / len2;
  t = std::max(0.0, std::min(1.0, t));
  double cx = _ax + t * abx, cy = _ay + t * aby;
  return std::hypot(_px - cx, _py - cy);
}

// ═══════════════════════════════════════════════════════════
// TaskMonitorDialog
// ═══════════════════════════════════════════════════════════

TaskMonitorDialog::TaskMonitorDialog(gz::transport::Node &_transportNode,
                                       const std::string &_worldName,
                                       rclcpp::Node::SharedPtr _rosNode,
                                       QWidget *_parent)
    : QDialog(_parent), node_(_transportNode), worldName_(_worldName),
      rosNode_(_rosNode) {
  setWindowTitle("任务执行");
  setModal(false);
  resize(750, 600);
  setAttribute(Qt::WA_DeleteOnClose);

  state_ = std::make_shared<TaskMonitorState>();
  setupUI();

  displayTimer_ = new QTimer(this);
  connect(displayTimer_, &QTimer::timeout, this, &TaskMonitorDialog::onUpdateDisplay);
  displayTimer_->start(33);  // ~30Hz

  secondTimer_ = new QTimer(this);
  connect(secondTimer_, &QTimer::timeout, this, &TaskMonitorDialog::onUpdateSecond);
  secondTimer_->start(1000);  // 1Hz

  std::cout << "Phase 10: 任务监控面板已创建" << std::endl;
}

TaskMonitorDialog::~TaskMonitorDialog() {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->alive = false;
  }
  if (!poseTopic_.empty()) node_.Unsubscribe(poseTopic_);
  unsubscribeThrust();
  displayTimer_->stop();
  secondTimer_->stop();
}

// ═══════════════════════════════════════════════════════════
// UI
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  // ---- 状态栏 ----
  auto *statusRow = new QHBoxLayout();
  nameLabel_ = new QLabel("未加载任务", this);
  nameLabel_->setStyleSheet("font-weight:bold; font-size:14px;");

  modelCombo_ = new QComboBox(this);
  modelCombo_->setMinimumWidth(120);
  modelCombo_->setToolTip("选择受控船");

  timeLabel_ = new QLabel("", this);
  scoreLabel_ = new QLabel("", this);
  scoreLabel_->setStyleSheet("font-weight:bold; color:#2266aa;");

  statusRow->addWidget(nameLabel_);
  statusRow->addWidget(new QLabel("受控船:", this));
  statusRow->addWidget(modelCombo_);
  statusRow->addStretch();
  statusRow->addWidget(timeLabel_);
  statusRow->addWidget(scoreLabel_);
  mainLayout->addLayout(statusRow);

  // ---- 地图 ----
  map_ = new MapWidget(this);
  map_->setAutoFollow(true);
  map_->setEditable(false);  // 监控模式：禁止编辑航路点
  map_->setHintText("");     // 只读监控无操作提示（避免误导"双击设航向"等编辑说明）
  mainLayout->addWidget(map_, 1);

  // ---- 状态信息行 ----
  auto *infoRow = new QHBoxLayout();
  wpLabel_ = new QLabel("", this);
  distLabel_ = new QLabel("", this);
  hdgLabel_ = new QLabel("", this);
  thrustLabel_ = new QLabel("", this);
  infoRow->addWidget(wpLabel_);
  infoRow->addWidget(distLabel_);
  infoRow->addWidget(hdgLabel_);
  infoRow->addWidget(thrustLabel_);
  infoRow->addStretch();
  mainLayout->addLayout(infoRow);

  // ---- 按钮 ----
  auto *btnRow = new QHBoxLayout();
  loadBtn_ = new QPushButton("加载任务", this);
  startBtn_ = new QPushButton("开始监控", this);
  endBtn_ = new QPushButton("结束并评分", this);
  closeBtn_ = new QPushButton("关闭", this);
  btnRow->addWidget(loadBtn_);
  btnRow->addWidget(startBtn_);
  btnRow->addWidget(endBtn_);
  btnRow->addStretch();
  btnRow->addWidget(closeBtn_);
  for (QPushButton *b : {loadBtn_, startBtn_, endBtn_, closeBtn_}) b->setMinimumHeight(30);
  mainLayout->addLayout(btnRow);

  // ---- 信号连接 ----
  connect(loadBtn_, &QPushButton::clicked, this, [this]() {
    QString path = QFileDialog::getOpenFileName(this, "加载任务",
        QString::fromStdString(keship::KeshipTaskDir()), "TOML (*.toml)");
    if (!path.isEmpty()) loadTask(path.toStdString());
  });

  connect(modelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) {
    currentModel_ = modelCombo_->currentText().toStdString();
    initPoseSubscription();
    initThrustSubscriptions(currentModel_);
    initTaskPublisher();
  });

  connect(startBtn_, &QPushButton::clicked, this, &TaskMonitorDialog::onStartStop);
  connect(endBtn_, &QPushButton::clicked, this, &TaskMonitorDialog::onEndTask);
  connect(closeBtn_, &QPushButton::clicked, this, &QDialog::close);
}

// ═══════════════════════════════════════════════════════════
// 加载任务
// ═══════════════════════════════════════════════════════════

bool TaskMonitorDialog::loadTask(const std::string &_tomlPath) {
  std::string err;
  auto cfg = keship::TaskTomlParser::load(_tomlPath, &err);
  if (!cfg) {
    QMessageBox::warning(this, "错误", QString::fromStdString(err));
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->config = *cfg;
    state_->isRunning = false;
    state_->isFinished = false;
    state_->elapsedSec = 0;
    state_->currentWaypointIdx = 0;
    state_->waypointReached.assign(cfg->waypoints.size(), false);
    state_->posErrors.clear();
    state_->lateralDevs.clear();
    state_->driftDists.clear();
    state_->headingErrors.clear();
    state_->thrustSamples.clear();
    state_->maxLateralDev = 0;
  }

  nameLabel_->setText(QString::fromStdString(cfg->name));
  // 转换 keship::TaskWaypoint → Waypoint（两个结构体字段相同但类型不同）
  std::vector<Waypoint> wpts;
  for (auto &tw : cfg->waypoints)
    wpts.push_back({tw.x, tw.y, tw.heading});
  map_->setWaypoints(wpts);
  map_->clearTrail();

  if (cfg->type == "tracking") {
    map_->setTrackingMode(true);
    map_->setTrackingTolerance(cfg->arrivalRadius);
  } else {
    map_->setTrackingMode(false);
  }
  if (cfg->type == "goto") {
    map_->setGotoTarget(cfg->targetX, cfg->targetY, cfg->gotoRadius);
  }
  if (cfg->type == "dp") {
    map_->setDpTarget(cfg->holdX, cfg->holdY, cfg->tolerance);
  } else {
    map_->clearDpTarget();
  }

  startBtn_->setText("开始监控");
  scoreLabel_->setText("");
  wpLabel_->setText("");
  distLabel_->setText("");
  hdgLabel_->setText("");

  // 预选受控船（如果 task 中没有 controlled_model，等待用户选择）
  std::cout << "Phase 10: 任务已加载 — " << _tomlPath
            << " (类型: " << cfg->type << ", 航路点: " << cfg->waypoints.size() << ")"
            << std::endl;
  return true;
}

void TaskMonitorDialog::addModel(const QString &_name) {
  modelCombo_->addItem(_name);
}

void TaskMonitorDialog::setThrusterInfo(const std::string &_modelName,
                                         const std::vector<std::string> &_names,
                                         double _totalMaxForce) {
  thrusterNamesMap_[_modelName] = _names;
  maxThrustMap_[_modelName] = (_totalMaxForce > 0) ? _totalMaxForce : 1.0;
}

// ═══════════════════════════════════════════════════════════
// Transport 订阅
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::initPoseSubscription() {
  if (!poseTopic_.empty()) node_.Unsubscribe(poseTopic_);
  if (modelCombo_->currentIndex() < 0) return;

  std::string model = modelCombo_->currentText().toStdString();
  poseTopic_ = "/world/" + worldName_ + "/pose/info";

  auto state = state_;  // shared_ptr 副本
  node_.Subscribe(poseTopic_,
    std::function<void(const gz::msgs::Pose_V&)>(
      [state, model](const gz::msgs::Pose_V &msg) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return;
        state->poseFound = false;
        for (int i = 0; i < msg.pose_size(); ++i) {
          if (msg.pose(i).name() == model) {
            state->shipX = msg.pose(i).position().x();
            state->shipY = msg.pose(i).position().y();
            auto &ori = msg.pose(i).orientation();
            gz::math::Quaterniond q(ori.w(), ori.x(), ori.y(), ori.z());
            // Yaw: 导航惯例 (0=北/+Y, 顺时针)
            state->shipHdg = q.Yaw() * 180.0 / M_PI;
            state->poseFound = true;
            break;
          }
        }
      }));
}

void TaskMonitorDialog::initThrustSubscriptions(const std::string &_modelName) {
  unsubscribeThrust();
  if (_modelName.empty()) return;

  // 从 per-model 缓存读取推进器名，否则尝试常见命名
  std::vector<std::string> names;
  auto it = thrusterNamesMap_.find(_modelName);
  if (it != thrusterNamesMap_.end()) names = it->second;
  if (names.empty()) {
    for (int i = 0; i < 4; ++i)
      names.push_back("thrust_" + std::to_string(i));
  }

  auto state = state_;
  for (const auto &name : names) {
    std::string topic = "/keship/thrust/" + _modelName + "/" + name;
    thrustTopics_.push_back(topic);
    try {
      node_.Subscribe(topic,
        std::function<void(const gz::msgs::Double&)>(
          [state](const gz::msgs::Double &msg) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->alive) return;
            state->totalThrust = std::abs(msg.data());
            state->thrustFound = true;
          }));
    } catch (...) {
      // 推进器 topic 可能不存在（模型无推进器），静默忽略
    }
  }
}

void TaskMonitorDialog::unsubscribeThrust() {
  for (const auto &t : thrustTopics_) {
    try { node_.Unsubscribe(t); } catch (...) {}
  }
  thrustTopics_.clear();
}

// ═══════════════════════════════════════════════════════════
// 定时器回调
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::onUpdateDisplay() {
  // 从共享状态原子复制
  double sx, sy, sh, thrust;
  bool poseOk, thrustOk, running, finished;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    sx = state_->shipX; sy = state_->shipY; sh = state_->shipHdg;
    poseOk = state_->poseFound;
    thrust = state_->totalThrust;
    thrustOk = state_->thrustFound;
    running = state_->isRunning;
    finished = state_->isFinished;
  }

  if (!poseOk) return;

  map_->setShipPose(sx, sy, sh);

  if (running && !finished) {
    map_->addTrailPoint(sx, sy);

    // 累积评分数据
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      // 仅在状态中追加，实际评分计算在 finishTask()
      auto &cfg = state_->config;
      if (cfg.type == "tracking") {
        // 计算当前横向偏差
        if (cfg.waypoints.size() >= 2) {
          double minDist = 1e9;
          for (size_t i = 1; i < cfg.waypoints.size(); ++i) {
            double d = pointToSegmentDist(sx, sy,
                cfg.waypoints[i-1].x, cfg.waypoints[i-1].y,
                cfg.waypoints[i].x, cfg.waypoints[i].y);
            if (d < minDist) minDist = d;
          }
          state_->lateralDevs.push_back(minDist);
          if (minDist > state_->maxLateralDev) state_->maxLateralDev = minDist;
        }
      } else if (cfg.type == "dp") {
        double drift = std::hypot(sx - cfg.holdX, sy - cfg.holdY);
        state_->driftDists.push_back(drift);
        double hErr = std::abs(headingDiff(sh, cfg.holdHdg > -900 ? cfg.holdHdg : sh));
        state_->headingErrors.push_back(hErr);
      }

      if (thrustOk)
        state_->thrustSamples.push_back(thrust);

      // waypoint 进度检查
      if (cfg.type == "waypoint")
        checkWaypointProgress();
    }
  }

  // 发布任务状态到 gz-transport（~1Hz，供学生控制程序订阅）
  if (++publishCounter_ >= 30) {
    publishCounter_ = 0;
    publishTaskStatus();
  }

  // 状态标签
  auto &cfg = state_->config;
  if (cfg.type == "waypoint") {
    int done = state_->currentWaypointIdx;
    int total = (int)cfg.waypoints.size();
    wpLabel_->setText(QString("航路点: %1/%2").arg(done).arg(total));

    int nextWp = done;
    if (nextWp >= total) nextWp = total - 1;
    if (nextWp >= 0) {
      double d = std::hypot(cfg.waypoints[nextWp].x - sx, cfg.waypoints[nextWp].y - sy);
      distLabel_->setText(QString("目标距离: %1 m").arg(d, 0, 'f', 1));
    }
  } else if (cfg.type == "tracking") {
    double maxDev = 0;
    { std::lock_guard<std::mutex> lock(state_->mutex);
      maxDev = state_->maxLateralDev; }
    wpLabel_->setText(QString("路径走廊: ±%1 m").arg(cfg.arrivalRadius, 0, 'f', 1));
    distLabel_->setText(QString("最大偏差: %1 m").arg(maxDev, 0, 'f', 2));
  } else if (cfg.type == "goto") {
    double dist = std::hypot(sx - cfg.targetX, sy - cfg.targetY);
    wpLabel_->setText(QString("目标: (%1, %2)").arg(cfg.targetX, 0, 'f', 0).arg(cfg.targetY, 0, 'f', 0));
    distLabel_->setText(QString("距离: %1 m / 抵达半径 %2 m").arg(dist, 0, 'f', 1).arg(cfg.gotoRadius, 0, 'f', 1));
  } else if (cfg.type == "dp") {
    double drift = std::hypot(sx - cfg.holdX, sy - cfg.holdY);
    distLabel_->setText(QString("漂移: %1 m").arg(drift, 0, 'f', 1));
  }

  if (cfg.type == "dp" && cfg.holdHdg > -900) {
    double hErr = std::abs(headingDiff(sh, cfg.holdHdg));
    hdgLabel_->setText(QString("航向偏差: %1°").arg(hErr, 0, 'f', 1));
  }

  thrustLabel_->setText(thrustOk
      ? QString("推力: %1 N").arg(thrust, 0, 'f', 0) : QString());
}

void TaskMonitorDialog::onUpdateSecond() {
  bool running, finished;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    running = state_->isRunning;
    finished = state_->isFinished;
    if (running) {
      auto now = std::chrono::steady_clock::now();
      state_->elapsedSec = std::chrono::duration<double>(now - state_->startTime).count();
    }
  }

  if (!running || finished) return;

  double elapsed = 0;
  int timeLimit = 300;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    elapsed = state_->elapsedSec;
    timeLimit = state_->config.timeLimit;
  }

  int remain = timeLimit - (int)elapsed;
  if (remain < 0) remain = 0;
  timeLabel_->setText(QString("时间: %1 / %2 s").arg((int)elapsed).arg(timeLimit));

  // 超时自动结束
  if (elapsed >= timeLimit) {
    finishTask();
    return;
  }

  // goto 抵达判定：进入抵达半径 → 自动结束
  if (state_->config.type == "goto") {
    double dist = std::hypot(state_->shipX - state_->config.targetX,
                              state_->shipY - state_->config.targetY);
    if (dist < state_->config.gotoRadius) {
      finishTask();
      return;
    }
  }

  // 全部航路点完成自动结束（仅 waypoint）
  if (state_->config.type == "waypoint") {
    int done = state_->currentWaypointIdx;
    int total = (int)state_->config.waypoints.size();
    if (done >= total && total > 0) {
      finishTask();
    }
  }
}

// ═══════════════════════════════════════════════════════════
// 航路点进度检查
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::checkWaypointProgress() {
  auto &cfg = state_->config;
  if (cfg.waypoints.empty()) return;

  while (state_->currentWaypointIdx < (int)cfg.waypoints.size()) {
    int idx = state_->currentWaypointIdx;
    double d = std::hypot(cfg.waypoints[idx].x - state_->shipX,
                           cfg.waypoints[idx].y - state_->shipY);

    if (d < cfg.arrivalRadius) {
      // 检查航向要求（若存在）
      double reqHdg = cfg.waypoints[idx].heading;
      if (reqHdg >= 0) {
        double hErr = std::abs(headingDiff(state_->shipHdg, reqHdg));
        if (hErr > 10.0) break;  // 航向偏差 > 10°，不算到达
      }

      state_->posErrors.push_back(d);
      state_->waypointReached[idx] = true;
      state_->currentWaypointIdx++;

      // 更新地图航路点状态
      map_->setWaypointStatus(idx, 2);  // 已完成
      int nextIdx = state_->currentWaypointIdx;
      if (nextIdx < (int)cfg.waypoints.size())
        map_->setWaypointStatus(nextIdx, 1);  // 当前目标
    } else {
      break;
    }
  }
}

// ═══════════════════════════════════════════════════════════
// 按钮操作
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::onStartStop() {
  bool wasRunning;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    wasRunning = state_->isRunning;
  }

  if (wasRunning) {
    // 停止
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->isRunning = false;
    startBtn_->setText("继续监控");
  } else {
    // 开始/继续
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->config.type.empty()) {
        QMessageBox::warning(this, "提示", "请先加载任务文件 (TOML)");
        return;
      }
    }
    if (modelCombo_->currentIndex() < 0) {
      QMessageBox::warning(this, "提示", "请先选择受控船");
      return;
    }
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->isRunning = true;
      state_->startTime = std::chrono::steady_clock::now();
      state_->isFinished = false;
      state_->elapsedSec = 0;
      state_->currentWaypointIdx = 0;
      state_->waypointReached.assign(state_->config.waypoints.size(), false);
      state_->posErrors.clear();
      state_->lateralDevs.clear();
      state_->driftDists.clear();
      state_->headingErrors.clear();
      state_->thrustSamples.clear();
      state_->maxLateralDev = 0;
    }
    map_->clearTrail();
    timeLabel_->setText("");
    scoreLabel_->setText("");

    // 初始化航路点状态
    auto &wpts = state_->config.waypoints;
    for (size_t i = 0; i < wpts.size(); ++i) map_->setWaypointStatus((int)i, 0);
    if (!wpts.empty()) map_->setWaypointStatus(0, 1);  // 第一个为目标

    startBtn_->setText("停止监控");
  }
}

void TaskMonitorDialog::onEndTask() {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->config.type.empty() || !state_->isRunning || state_->isFinished) return;
  }
  finishTask();
}

// ═══════════════════════════════════════════════════════════
// 任务结束 + 评分
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::finishTask() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->isFinished) return;
  state_->isRunning = false;
  state_->isFinished = true;

  const auto &cfg = state_->config;

  // 构建 TaskMetrics
  keship::TaskMetrics m;
  m.timeLimit = cfg.timeLimit;
  m.elapsedSec = state_->elapsedSec;
  auto mti = maxThrustMap_.find(currentModel_);
  m.maxThrust = (mti != maxThrustMap_.end()) ? mti->second : 1.0;

  if (cfg.type == "waypoint") {
    m.totalWaypoints = (int)cfg.waypoints.size();
    m.waypointsReached = state_->currentWaypointIdx;
    m.arrivalRadius = cfg.arrivalRadius;
    if (!state_->posErrors.empty()) {
      double sum = 0;
      for (double e : state_->posErrors) sum += e;
      m.meanPosError = sum / state_->posErrors.size();
    }
  } else if (cfg.type == "tracking") {
    m.tolerance = cfg.arrivalRadius;
    m.maxLateralDev = state_->maxLateralDev;
  } else if (cfg.type == "goto") {
    m.gotoRadius = cfg.gotoRadius;
    m.finalDistance = std::hypot(state_->shipX - cfg.targetX,
                                  state_->shipY - cfg.targetY);
  } else if (cfg.type == "dp") {
    m.tolerance = cfg.tolerance;
    if (!state_->driftDists.empty()) {
      double sumSq = 0;
      for (double d : state_->driftDists) sumSq += d * d;
      m.rmsDrift = std::sqrt(sumSq / state_->driftDists.size());
    }
    if (!state_->headingErrors.empty()) {
      double sum = 0;
      for (double e : state_->headingErrors) sum += e;
      m.meanHeadingErr = sum / state_->headingErrors.size();
    }
  }

  if (!state_->thrustSamples.empty()) {
    double sum = 0;
    for (double t : state_->thrustSamples) sum += t;
    m.meanThrust = sum / state_->thrustSamples.size();
  }

  // 计算评分
  keship::ScoreResult score;
  if (cfg.type == "waypoint")
    score = keship::TaskScoring::computeWaypoint(m);
  else if (cfg.type == "tracking")
    score = keship::TaskScoring::computeTracking(m);
  else if (cfg.type == "dp")
    score = keship::TaskScoring::computeDp(m);
  else if (cfg.type == "goto")
    score = keship::TaskScoring::computeGoto(m);
  state_->finalScore = score;

  startBtn_->setText("开始监控");

  // 用 QMetaObject::invokeMethod 在 GUI 线程弹窗（当前可能在 timer 线程上下文）
  int percent = (int)(score.total * 100);
  QString detail;
  for (auto &kv : score.individual) {
    detail += QString("  %1: %2\n")
        .arg(QString::fromStdString(kv.first))
        .arg(kv.second, 0, 'f', 2);
  }

  std::cout << "Phase 10: 任务结束 — 总分 " << percent << "/100" << std::endl;

  // 延迟弹窗到 GUI 线程
  QMetaObject::invokeMethod(this, [this, percent, detail]() {
    scoreLabel_->setText(QString("得分: %1 / 100").arg(percent));
    QMessageBox::information(this, "任务完成",
        QString("总分: %1 / 100\n\n各指标得分:\n%2").arg(percent).arg(detail));
  }, Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════
// 任务状态发布（供学生控制程序通过 gz-transport 订阅）
// ═══════════════════════════════════════════════════════════

void TaskMonitorDialog::initTaskPublisher() {
  if (!rosNode_ || modelCombo_->currentIndex() < 0) return;
  std::string model = modelCombo_->currentText().toStdString();
  taskRosTopic_ = keship::SanitizeRosTopic("/keship/task/" + model);
  taskStatusPub_ = rosNode_->create_publisher<std_msgs::msg::String>(taskRosTopic_, 10);
  std::cout << "Phase 10: 任务状态发布 (ROS 2) → " << taskRosTopic_ << std::endl;
}

void TaskMonitorDialog::publishTaskStatus() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto &cfg = state_->config;
  if (cfg.type.empty()) return;

  // 构建 JSON
  std::ostringstream js;
  js << "{";
  js << "\"type\":\"" << cfg.type << "\",";
  js << "\"time_limit\":" << cfg.timeLimit << ",";
  js << "\"elapsed\":" << state_->elapsedSec << ",";
  js << "\"running\":" << (state_->isRunning ? "true" : "false") << ",";
  js << "\"finished\":" << (state_->isFinished ? "true" : "false") << ",";

  if (cfg.type == "waypoint" || cfg.type == "tracking") {
    // 航路点列表
    js << "\"waypoints\":[";
    for (size_t i = 0; i < cfg.waypoints.size(); ++i) {
      if (i > 0) js << ",";
      js << "[" << cfg.waypoints[i].x << "," << cfg.waypoints[i].y << "]";
    }
    js << "],";

    // 航向要求
    js << "\"headings\":[";
    for (size_t i = 0; i < cfg.waypoints.size(); ++i) {
      if (i > 0) js << ",";
      js << cfg.waypoints[i].heading;
    }
    js << "],";

    // 到达状态
    js << "\"reached\":[";
    for (size_t i = 0; i < state_->waypointReached.size(); ++i) {
      if (i > 0) js << ",";
      js << (state_->waypointReached[i] ? "true" : "false");
    }
    js << "],";

    js << "\"current_waypoint\":" << state_->currentWaypointIdx << ",";
    js << "\"total_waypoints\":" << cfg.waypoints.size() << ",";
    js << "\"arrival_radius\":" << cfg.arrivalRadius << ",";

    // 当前目标航路点
    int cur = state_->currentWaypointIdx;
    if (cur >= (int)cfg.waypoints.size()) cur = (int)cfg.waypoints.size() - 1;
    if (cur >= 0) {
      js << "\"target_x\":" << cfg.waypoints[cur].x << ",";
      js << "\"target_y\":" << cfg.waypoints[cur].y << ",";
      js << "\"target_heading\":" << cfg.waypoints[cur].heading << ",";
      double dist = std::hypot(cfg.waypoints[cur].x - state_->shipX,
                                cfg.waypoints[cur].y - state_->shipY);
      js << "\"distance\":" << dist << ",";
    }

    js << "\"tolerance\":" << cfg.arrivalRadius;
  }

  if (cfg.type == "goto") {
    double dist = std::hypot(state_->shipX - cfg.targetX,
                              state_->shipY - cfg.targetY);
    js << "\"target_x\":" << cfg.targetX << ","
       << "\"target_y\":" << cfg.targetY << ","
       << "\"target_heading\":" << cfg.targetHeading << ","
       << "\"arrival_radius\":" << cfg.gotoRadius << ","
       << "\"distance\":" << dist;
  } else if (cfg.type == "dp") {
    js << "\"hold_x\":" << cfg.holdX << ",";
    js << "\"hold_y\":" << cfg.holdY << ",";
    js << "\"hold_heading\":" << cfg.holdHdg << ",";
    js << "\"tolerance\":" << cfg.tolerance << ",";
    double drift = std::hypot(state_->shipX - cfg.holdX, state_->shipY - cfg.holdY);
    js << "\"drift\":" << drift;
  }

  // 船位
  js << ",\"ship_x\":" << state_->shipX;
  js << ",\"ship_y\":" << state_->shipY;
  js << ",\"ship_heading\":" << state_->shipHdg;
  js << ",\"thrust\":" << (state_->thrustFound ? state_->totalThrust : 0.0);
  js << "}";

  auto msg = std_msgs::msg::String();
  msg.data = js.str();
  taskStatusPub_->publish(msg);
}
