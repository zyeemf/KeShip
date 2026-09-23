/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: 任务设定对话框 — waypoint / tracking 轨迹编辑器 / dp */

#ifndef TASK_SET_DIALOG_HH_
#define TASK_SET_DIALOG_HH_

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>

#include "MapWidget.hh"

class TaskSetDialog : public QDialog {
  Q_OBJECT
 public:
  TaskSetDialog(gz::transport::Node &_transportNode,
                const std::string &_worldName,
                QWidget *_parent = nullptr);
  ~TaskSetDialog() override = default;

  /// ★ 2026-09-23 新增（张老师反馈"任务设定对话框看不到场景模型预览"）：
  /// 注入场景模型足迹，供地图显示（与"绘制水域"对话框行为对齐）。
  /// 由 MenuPluginKeship 在创建本对话框后调用 GatherSceneFootprints() 注入。
  void setSceneModels(const std::vector<keship::ModelFootprint> &_models);

 signals:
  void taskSaved(const QString &_filePath);

 protected:
  void closeEvent(QCloseEvent *_event) override;

 private slots:
  void onTaskTypeChanged();
  bool onSave();
  void onShapeChanged(int _idx);           // 轨迹形状切换
  void onGenerateTrajectory();             // 生成轨迹
  void onRefPointPlaced(int _idx, double _x, double _y);  // 参考点放置回调
  void onFreeDrawChanged();                // 自定义绘制改变

 private:
  void setupUI();
  void setupTrackingPanel();               // 构建 trPanel_
  void setupShapePanel(int _idx, QWidget *_parent);  // 构建单个形状参数面板
  void generateLinePath();
  void generateSinePath();
  void generateCirclePath();
  void generateEllipsePath();
  bool isDirty() const;
  void setDirty(bool _d);

  gz::transport::Node &node_;
  std::string worldName_;

  QRadioButton *wpRadio_, *trRadio_, *dpRadio_, *gotoRadio_;
  QLineEdit *nameEdit_;
  QSpinBox *timeSpin_;

  // waypoint 面板
  QWidget *wpPanel_;
  QDoubleSpinBox *radiusSpin_;

  // DP 面板
  QWidget *dpPanel_;
  QDoubleSpinBox *holdXSpin_, *holdYSpin_, *holdHdgSpin_;
  QDoubleSpinBox *tolSpin_;

  // goto 面板
  QWidget *gotoPanel_;
  QDoubleSpinBox *gotoXSpin_, *gotoYSpin_, *gotoHdgSpin_;
  QDoubleSpinBox *gotoRSpin_;

  // tracking 轨迹编辑器面板
  QWidget *trPanel_;
  QComboBox *shapeCombo_;
  std::vector<QWidget*> shapePages_;
  QLabel *trajLenLabel_;

  // 5 种形状的参数控件
  // 直线
  QDoubleSpinBox *lineX1_, *lineY1_, *lineX2_, *lineY2_;
  QSpinBox *lineN_;
  // 正弦
  QDoubleSpinBox *sineX1_, *sineY1_, *sineX2_, *sineY2_;
  QDoubleSpinBox *sineAmp_;
  QSpinBox *sineCycles_, *sineN_;
  // 圆
  QDoubleSpinBox *circCX_, *circCY_, *circR_;
  QDoubleSpinBox *circStart_, *circEnd_;
  QComboBox *circDir_;
  QSpinBox *circN_;
  // 椭圆
  QDoubleSpinBox *ellCX_, *ellCY_, *ellA_, *ellB_;
  QDoubleSpinBox *ellRot_, *ellStart_, *ellEnd_;
  QSpinBox *ellN_;
  // 自定义
  QDoubleSpinBox *freeInterval_;

  QPushButton *genBtn_;

  MapWidget *mapWidget_;

  // 评分权重
  QWidget *weightPanel_;
  QDoubleSpinBox *w1Spin_, *w2Spin_, *w3Spin_;
  QLabel *w1Label_, *w2Label_, *w3Label_;

  bool dirty_ = false;
  int lastTaskType_ = 0;  // 0=wp, 1=tracking, 2=dp, 3=goto
};

#endif
