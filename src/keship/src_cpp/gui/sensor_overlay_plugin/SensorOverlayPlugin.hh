/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 传感器画面叠加插件 — Gazebo Floating Plugin */
/// Zhangyu 创建 2026.5.16

#ifndef SENSOR_OVERLAY_PLUGIN_HH_
#define SENSOR_OVERLAY_PLUGIN_HH_

#include <gz/gui/Plugin.hh>
#include <gz/transport/Node.hh>
#include <gz/msgs/stringmsg.pb.h>

#include <QQuickImageProvider>
#include <QImage>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>

#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <vector>

namespace keship
{
  /// QQuickImageProvider 子类，为 QML Image 提供相机画面
  class CameraImageProvider : public QQuickImageProvider
  {
  public:
    CameraImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &_id, QSize *_size,
                        const QSize &_requestedSize) override;

    void setImage(const QString &_key, const QImage &_img);

  private:
    std::map<QString, QImage> images_;
    std::mutex mtx_;
  };

  /// Floating plugin：在 3D 视口上叠加传感器画面
  class SensorOverlayPlugin : public gz::gui::Plugin
  {
    Q_OBJECT

    /// 暴露给 QML：当前活跃的 viewer 列表
    Q_PROPERTY(QVariantList viewers READ viewers NOTIFY viewersChanged)

    /// 全局刷新计数器：每收到任意传感器新帧 +1，QML Image 绑定此值以触发重载
    Q_PROPERTY(int refreshCounter READ refreshCounter NOTIFY refreshCounterChanged)

  public:
    SensorOverlayPlugin();
    ~SensorOverlayPlugin() override;

    void LoadConfig(const tinyxml2::XMLElement *_pluginElem) override;

    QVariantList viewers() const { return viewerList_; }

    int refreshCounter() const { return refreshCounter_; }

  signals:
    void viewersChanged();
    void refreshCounterChanged();

  private slots:
    /// gz-transport 收到传感器查看指令
    void onSensorViewCmd(const gz::msgs::StringMsg &_msg);

  private:
    void openViewer(const QString &_modelName, const QString &_typeLabel,
                    const QString &_instName);
    void closeViewer(const QString &_viewerId);

    void subscribeCamera(const QString &_viewerId,
                         const QString &_modelName, const QString &_instName);
    void subscribeLidar(const QString &_viewerId,
                        const QString &_modelName, const QString &_instName);

    gz::transport::Node gzNode_;
    rclcpp::Node::SharedPtr rosNode_;
    std::thread spinThread_;

    CameraImageProvider *imageProvider_ = nullptr;

    struct ViewerData {
      QString id;        // "modelName / instName"
      QString typeLabel; // "相机" or "激光雷达"
      QString modelName;
      QString instName;
      QImage cameraImage;
      std::vector<float> lidarRanges;
      float lidarAngleMin = -M_PI, lidarAngleIncr = M_PI / 180.f;
      float lidarRangeMin = 0.1f, lidarRangeMax = 50.f;
      bool lidarNewData = false;
      std::mutex mtx;
    };
    std::map<QString, ViewerData> viewerData_;
    QVariantList viewerList_;

    /// 保存 rclcpp subscriptions 以保持生命周期
    std::map<QString, rclcpp::SubscriptionBase::SharedPtr> subs_;
    int refreshCounter_ = 0;
  };
}

#endif
