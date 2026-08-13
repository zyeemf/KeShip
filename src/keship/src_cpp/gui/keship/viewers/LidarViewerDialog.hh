/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 激光雷达查看器 — QDialog + QWidget(QPainter) 方案 */
/// Zhangyu 创建 2026.5.15

#ifndef LIDAR_VIEWER_DIALOG_HH_
#define LIDAR_VIEWER_DIALOG_HH_

#include <QDialog>
#include <QTimer>
#include <QWidget>
#include <QWheelEvent>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <mutex>
#include <vector>
#include <memory>

/// 激光雷达 2D 点云渲染 widget（QPainter 方案，避免 OpenGL Core Profile 兼容问题）
class LidarViewWidget : public QWidget
{
  Q_OBJECT

public:
  explicit LidarViewWidget(QWidget *_parent = nullptr);

  void setScanData(const std::vector<float> &_ranges,
                   float _angleMin, float _angleIncr,
                   float _rangeMin, float _rangeMax);

protected:
  void paintEvent(QPaintEvent *_event) override;
  void wheelEvent(QWheelEvent *_event) override;

private:
  std::vector<float> ranges_;
  float angleMin_ = -M_PI, angleIncr_ = M_PI / 180.f;
  float rangeMin_ = 0.1f, rangeMax_ = 50.f;
  float dist_ = -1.f;
  std::mutex mtx_;
};

/// 激光雷达查看器 dialog
class LidarViewerDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LidarViewerDialog(
      rclcpp::Node::SharedPtr _rosNode,
      const QString &_modelName,
      const QString &_instName,
      const QString &_typePrefix = "lidar",
      QWidget *_parent = nullptr);

private:
  LidarViewWidget *widget_;
  QTimer *timer_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr sub_;
  std::vector<float> ranges_;
  float angleMin_ = -M_PI, angleIncr_ = M_PI / 180.f;
  float rangeMin_ = 0.1f, rangeMax_ = 50.f;
  std::mutex mtx_;
  bool newData_ = false;
};

#endif
