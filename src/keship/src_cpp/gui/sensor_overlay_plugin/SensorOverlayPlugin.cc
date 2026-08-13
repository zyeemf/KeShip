/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 传感器画面叠加插件实现
 * Zhangyu 创建 2026.5.16
 */

#include "SensorOverlayPlugin.hh"
#include "TopicUtil.hh"

#include <gz/plugin/Register.hh>
#include <gz/msgs/stringmsg.pb.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlEngine>

#include <iostream>

namespace keship
{

//////////////////////////////////////////////////////////////////////////////////
// CameraImageProvider
//////////////////////////////////////////////////////////////////////////////////
QImage CameraImageProvider::requestImage(const QString &_id, QSize *_size,
                                          const QSize &)
{
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = images_.find(_id);
  if (it != images_.end() && !it->second.isNull())
  {
    if (_size) *_size = it->second.size();
    return it->second;
  }
  if (_size) *_size = QSize(640, 480);
  return QImage(640, 480, QImage::Format_RGB888);
}

void CameraImageProvider::setImage(const QString &_key, const QImage &_img)
{
  std::lock_guard<std::mutex> lock(mtx_);
  images_[_key] = _img;
}

//////////////////////////////////////////////////////////////////////////////////
// SensorOverlayPlugin
//////////////////////////////////////////////////////////////////////////////////
SensorOverlayPlugin::SensorOverlayPlugin()
    : gz::gui::Plugin()
{
  /// 注册 image provider 到 QML 引擎
  imageProvider_ = new CameraImageProvider;
  // 稍后在 LoadConfig 中 QQmlEngine 可用时再 addImageProvider

  /// 初始化 rclcpp
  if (!rclcpp::ok()) rclcpp::init(0, nullptr);
  rosNode_ = std::make_shared<rclcpp::Node>("keship_sensor_overlay");
  spinThread_ = std::thread([this]() { rclcpp::spin(rosNode_); });
}

SensorOverlayPlugin::~SensorOverlayPlugin()
{
  rclcpp::shutdown();
  if (spinThread_.joinable()) spinThread_.join();
}

void SensorOverlayPlugin::LoadConfig(const tinyxml2::XMLElement *)
{
  if (this->CardItem() && this->CardItem()->window())
  {
    /// 为 QML window 注册 image provider
    auto *engine = QQmlEngine::contextForObject(this->CardItem())->engine();
    if (engine)
      engine->addImageProvider("camera", imageProvider_);
  }

  /// 订阅 gz-transport 指令
  gzNode_.Subscribe("/keship/sensor_view",
    static_cast<void (SensorOverlayPlugin::*)(const gz::msgs::StringMsg &)>(
      &SensorOverlayPlugin::onSensorViewCmd), this);

  std::cout << "SensorOverlayPlugin: 就绪" << std::endl;
}

//////////////////////////////////////////////////////////////////////////////////
void SensorOverlayPlugin::onSensorViewCmd(const gz::msgs::StringMsg &_msg)
{
  auto json = QJsonDocument::fromJson(
      QByteArray::fromStdString(_msg.data()));
  if (!json.isObject()) return;

  auto obj = json.object();
  QString action = obj["action"].toString();
  QString modelName = obj["model"].toString();
  QString typeLabel = obj["type"].toString();
  QString instName = obj["name"].toString();

  if (action == "open" && !modelName.isEmpty() && !instName.isEmpty())
    openViewer(modelName, typeLabel, instName);
  else if (action == "close")
    closeViewer(modelName + " / " + instName);
}

//////////////////////////////////////////////////////////////////////////////////
void SensorOverlayPlugin::openViewer(const QString &_modelName,
                                      const QString &_typeLabel,
                                      const QString &_instName)
{
  QString viewerId = _modelName + " / " + _instName;

  /// 如果已存在，不重复打开
  if (viewerData_.count(viewerId)) return;

  auto &vd = viewerData_[viewerId];
  vd.id = viewerId;
  vd.typeLabel = _typeLabel;
  vd.modelName = _modelName;
  vd.instName = _instName;

  if (_typeLabel == "相机")
    subscribeCamera(viewerId, _modelName, _instName);
  else if (_typeLabel == "激光雷达")
    subscribeLidar(viewerId, _modelName, _instName);

  /// 更新 QML viewer 列表
  QVariantMap entry;
  entry["id"] = viewerId;
  entry["typeLabel"] = _typeLabel;
  entry["modelName"] = _modelName;
  entry["instName"] = _instName;
  viewerList_.append(entry);
  emit viewersChanged();
}

//////////////////////////////////////////////////////////////////////////////////
void SensorOverlayPlugin::closeViewer(const QString &_viewerId)
{
  subs_.erase(_viewerId);
  viewerData_.erase(_viewerId);

  for (int i = 0; i < viewerList_.size(); i++)
    if (viewerList_[i].toMap()["id"].toString() == _viewerId)
      { viewerList_.removeAt(i); break; }
  emit viewersChanged();
}

//////////////////////////////////////////////////////////////////////////////////
void SensorOverlayPlugin::subscribeCamera(const QString &_viewerId,
    const QString &_modelName, const QString &_instName)
{
  std::string topic = keship::SanitizeRosTopic(
      _modelName.toStdString() + "/" + _instName.toStdString());

  auto sub = rosNode_->create_subscription<sensor_msgs::msg::Image>(
      topic, rclcpp::SensorDataQoS(),
      [this, _viewerId](sensor_msgs::msg::Image::ConstSharedPtr _msg) {
        if (_msg->encoding != "rgb8") return;
        auto it = viewerData_.find(_viewerId);
        if (it == viewerData_.end()) return;
        {
          std::lock_guard<std::mutex> lock(it->second.mtx);
          it->second.cameraImage = QImage(
              _msg->data.data(), (int)_msg->width, (int)_msg->height,
              QImage::Format_RGB888).copy();
        }
        /// 通知 QML 刷新
        if (imageProvider_)
        {
          /// 同时推给 image provider（给 QML Image source 用）
          QImage img;
          {
            std::lock_guard<std::mutex> lock(it->second.mtx);
            img = it->second.cameraImage;
          }
          imageProvider_->setImage(_viewerId, img);
        }
        refreshCounter_++;
        emit refreshCounterChanged();
      });
  subs_[_viewerId] = sub;
}

//////////////////////////////////////////////////////////////////////////////////
void SensorOverlayPlugin::subscribeLidar(const QString &_viewerId,
    const QString &_modelName, const QString &_instName)
{
  std::string topic = keship::SanitizeRosTopic(
      _modelName.toStdString() + "/" + _instName.toStdString());

  auto sub = rosNode_->create_subscription<sensor_msgs::msg::LaserScan>(
      topic, rclcpp::SensorDataQoS(),
      [this, _viewerId](sensor_msgs::msg::LaserScan::ConstSharedPtr _msg) {
        auto it = viewerData_.find(_viewerId);
        if (it == viewerData_.end()) return;
        {
          std::lock_guard<std::mutex> lock(it->second.mtx);
          it->second.lidarRanges = _msg->ranges;
          it->second.lidarAngleMin = _msg->angle_min;
          it->second.lidarAngleIncr = _msg->angle_increment;
          it->second.lidarRangeMin = _msg->range_min;
          it->second.lidarRangeMax = _msg->range_max;
          it->second.lidarNewData = true;
        }
        refreshCounter_++;
        emit refreshCounterChanged();
      });
  subs_[_viewerId] = sub;
}

} // namespace keship

//////////////////////////////////////////////////////////////////////////////////
GZ_ADD_PLUGIN(keship::SensorOverlayPlugin,
              gz::gui::Plugin)
