/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase C: 排水量验证实验对话框 */

#ifndef DISPLACEMENT_VERIFY_DIALOG_HH_
#define DISPLACEMENT_VERIFY_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <QPaintEvent>

#include <memory>
#include <mutex>
#include <vector>
#include <string>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/double.pb.h>

#include "ShipHullGenerator.hh"

/// 排水量数据点
struct DispDataPoint {
  double payload = 0.0;    // 配载 [kg]
  double draft = 0.0;      // 吃水 [m]
  double volMeas = 0.0;    // 实测排水量 [m³]
  double volTheory = 0.0;  // 理论排水量 [m³]
  double error = 0.0;      // 相对误差 [%]
};

/// 排水量曲线图
class DispChartWidget : public QWidget {
  Q_OBJECT
 public:
  explicit DispChartWidget(QWidget *_parent = nullptr);
  void setTheory(const std::vector<double> &_drafts,
                 const std::vector<double> &_volumes);
  void setData(const std::vector<DispDataPoint> &_pts);
  void clearData();
 protected:
  void paintEvent(QPaintEvent *_event) override;
 private:
  std::vector<double> thDrafts_, thVolumes_;
  std::vector<DispDataPoint> pts_;
  double dMin_ = 0, dMax_ = 1, vMin_ = 0, vMax_ = 5;
};

/// 位姿共享状态
struct DispPoseState {
  std::mutex mutex;
  bool alive = true;
  double hullZ = 0.0;
  bool found = false;
};

/// 实测浸水体积共享状态（插件话题回调线程 → UI 线程）
/// 来源：KSPlugin_HydroBuoyancy 的 <publish_subvolume> 话题（HS-02 专用）
struct DispVolState {
  std::mutex mutex;
  bool alive = true;
  double subVol = 0.0;
  bool hasSample = false;   ///< 是否收到过样本（未收到时不得当 0 用，否则又变成"假实测"）
};

class DisplacementVerifyDialog : public QDialog {
  Q_OBJECT
 public:
  DisplacementVerifyDialog(gz::transport::Node &_transportNode,
                           const std::string &_worldName,
                           QWidget *_parent = nullptr);
  ~DisplacementVerifyDialog() override;

 private slots:
  void onTypeChanged();
  void onSpawn();
  void onPayloadChanged(double _kg);
  void onRecord();
  void onDeleteSelected();
  void onClearAll();
  void onUpdatePose();

 private:
  void setupUI();
  void updateStatusDisplay();
  void updateChart();
  void initPoseSubscription();
  void initVolumeSubscription();
  std::string makeSDF() const;
  bool doDeleteHull();
  /// @param _keepRecords true = 只重建模型（**保留**已记录的数据表与当前配载），
  ///        供"改配载 → 重建"用；false = 全新一轮（清表、配载归零）。
  bool doSpawnHull(bool _keepRecords = false);
  /// 改配载后按新质量**重建**模型（防抖定时器的超时槽）
  void respawnForPayload();
  bool resolveEntityID(const std::string &_name, uint32_t &_id);

  gz::transport::Node &transportNode_;
  std::string worldName_;
  std::string hullName_ = "DispTest_hull";
  std::string tempDir_;

  std::shared_ptr<DispPoseState> poseState_;
  std::string poseTopic_;
  std::string payloadTopic_;
  gz::transport::Node::Publisher payloadPub_;

  /// 实测浸水体积订阅（HS-02：实测 ∇ 的真实来源）
  std::shared_ptr<DispVolState> volState_;
  std::string subVolTopic_;

  QComboBox *typeCombo_;
  QDoubleSpinBox *lengthSpin_, *breadthSpin_, *depthSpin_;
  QDoubleSpinBox *densitySpin_;

  QSlider *payloadSlider_;
  QDoubleSpinBox *payloadSpin_;
  QLabel *draftLabel_, *volLabel_, *thVolLabel_, *errLabel_;

  QPushButton *spawnBtn_, *recordBtn_, *clearBtn_;

  QTableWidget *dataTable_;
  DispChartWidget *chartWidget_;
  std::vector<DispDataPoint> recordedData_;

  QTimer *poseTimer_;
  /// 配载防抖（2026-09-19）：滑条 valueChanged 会连续触发（0~1000 量程最多上千次），
  /// 而改配载要**重建模型** → 必须等用户停下再重建一次。
  QTimer *payloadDebounce_ = nullptr;

  uint32_t hullEntityId_ = 0;
  bool spawned_ = false;
  double currentDraft_ = 0.0;
  double currentPayload_ = 0.0;

  keship::ShipHullSpec params_;
  keship::ShipHullGenerator *generator_ = nullptr;  // 构造时预计算

  static constexpr double kFluidDensity = 1000.0;
  static constexpr double kGravity = 9.81;
};

#endif
