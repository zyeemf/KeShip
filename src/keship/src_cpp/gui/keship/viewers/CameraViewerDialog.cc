/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 相机画面查看器实现 — QDialog + QLabel 方案
 * Zhangyu 创建 2026.5.15
 */

#include "CameraViewerDialog.hh"
#include "TopicUtil.hh"

#include <QVBoxLayout>

CameraViewerDialog::CameraViewerDialog(
    rclcpp::Node::SharedPtr _rosNode,
    const QString &_modelName,
    const QString &_instName,
    const QString &_typePrefix,
    QWidget *_parent)
    : QDialog(_parent)
{
  setWindowTitle("相机: " + _modelName + " / " + _instName);
  setMinimumSize(400, 300);
  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  label_ = new QLabel(this);
  label_->setMinimumSize(400, 300);
  label_->setAlignment(Qt::AlignCenter);
  label_->setStyleSheet("background-color: black;");
  layout->addWidget(label_);

  std::string topic = keship::SanitizeRosTopic(
      _modelName.toStdString() + "/" + _typePrefix.toStdString() + "/" + _instName.toStdString());

  sub_ = _rosNode->create_subscription<sensor_msgs::msg::Image>(
      topic, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::ConstSharedPtr _msg) {
        if (_msg->encoding != "rgb8") return;
        std::lock_guard<std::mutex> lock(mtx_);
        image_ = QImage(_msg->data.data(), (int)_msg->width, (int)_msg->height,
                        QImage::Format_RGB888).copy();
      });

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, [this]() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!image_.isNull())
      label_->setPixmap(QPixmap::fromImage(image_).scaled(
          label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  });
  timer_->start(33);
}

CameraViewerDialog::~CameraViewerDialog() = default;
