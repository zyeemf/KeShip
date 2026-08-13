/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 相机画面查看器 — QDialog + QLabel 方案 */
/// Zhangyu 创建 2026.5.15

#ifndef CAMERA_VIEWER_DIALOG_HH_
#define CAMERA_VIEWER_DIALOG_HH_

#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <mutex>
#include <memory>

class CameraViewerDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CameraViewerDialog(
      rclcpp::Node::SharedPtr _rosNode,
      const QString &_modelName,
      const QString &_instName,
      const QString &_typePrefix = "camera",
      QWidget *_parent = nullptr);
  ~CameraViewerDialog() override;

private:
  QLabel *label_;
  QTimer *timer_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
  QImage image_;
  std::mutex mtx_;
};

#endif
