/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* QOpenGLWidget 浮力体/模型 3D 预览 — 纯 OpenGL，零外部依赖 */
/// ZhangYu 创建 2026.5.9

#ifndef BUOYANCY_3D_VIEW_HH_
#define BUOYANCY_3D_VIEW_HH_

#include <QOpenGLWidget>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QPointF>
#include <vector>
#include <memory>

struct Buoyancy3DModelInfo
{
  bool isBox = true;
  double boxDx = 1.0, boxDy = 1.0, boxDz = 1.0;
  double cylRadius = 0.5, cylLength = 1.0;
  std::string meshPath;  // 非空时加载 mesh 文件替代 box/cylinder
};

struct Buoyancy3DBodyInfo
{
  bool isBox = true;
  double boxDx = 1.0, boxDy = 1.0, boxDz = 1.0;
  double cylRadius = 0.5, cylLength = 1.0;
  double poseX = 0, poseY = 0, poseZ = 0;
  double poseRoll = 0, posePitch = 0, poseYaw = 0;
};

struct Thruster3DInfo
{
  double poseX = 0, poseY = 0, poseZ = 0;
  double poseRoll = 0, posePitch = 0, poseYaw = 0;
  double maxForce = 1000.0;
};

struct Sensor3DInfo
{
  int type = 0;  // SensorType enum: 0=IMU,1=NavSat,2=Mag,3=Camera,4=Lidar
  double poseX = 0, poseY = 0, poseZ = 0;
  double poseRoll = 0, posePitch = 0, poseYaw = 0;
};

class Buoyancy3DView : public QOpenGLWidget
{
  Q_OBJECT

public:
  explicit Buoyancy3DView(QWidget *parent = nullptr);
  ~Buoyancy3DView() override;

  void setModel(const Buoyancy3DModelInfo &_model);
  void syncBodies(const std::vector<Buoyancy3DBodyInfo> &_bodies);
  void setSelectedIndex(int _idx);
  void updateBody(int _idx, const Buoyancy3DBodyInfo &_info);

  void syncThrusters(const std::vector<Thruster3DInfo> &_thrusters);
  void updateThruster(int _idx, const Thruster3DInfo &_info);
  void setSelectedThrusterIdx(int _idx);

  void syncSensors(const std::vector<Sensor3DInfo> &_sensors);
  void updateSensor(int _idx, const Sensor3DInfo &_info);
  void setSelectedSensorIdx(int _idx);

  void setCoGPosition(float _x, float _y, float _z);
  void setShowCoG(bool _show);

  void setWaterLevel(float _z);
  void setShowWaterline(bool _show);
  void setModelZOffset(float _z);

protected:
  void initializeGL() override;
  void resizeGL(int _w, int _h) override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent *_ev) override;
  void mouseMoveEvent(QMouseEvent *_ev) override;
  void mouseReleaseEvent(QMouseEvent *_ev) override;
  void wheelEvent(QWheelEvent *_ev) override;

private:
  struct Geo {
    int vertexCount = 0;
    int indexCount = 0;
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vao;
    bool isBox = true;
  };
  void buildBoxGeo(Geo &_geo, double _dx, double _dy, double _dz);
  void buildCylGeo(Geo &_geo, double _r, double _l);
  void buildMeshGeo(Geo &_geo, const std::string &_path);
  void buildArrowGeo();
  void buildSensorDirGeo();
  void buildSphereGeo(Geo &_geo, double _r, int _segs = 16);
  void buildAxisGeo();
  void buildWaterlineGeo();
  void drawGeo(Geo &_geo, bool _wireframe);
  void drawLines(Geo &_geo, int _first, int _count);
  QMatrix4x4 bodyMatrix(const Buoyancy3DBodyInfo &_bi) const;
  QMatrix4x4 thrusterMatrix(const Thruster3DInfo &_ti) const;
  QMatrix4x4 sensorMatrix(const Sensor3DInfo &_si) const;
  void updateCamera();

  std::unique_ptr<QOpenGLShaderProgram> prog_;

  Geo modelGeo_;
  Buoyancy3DModelInfo modelInfo_;
  bool hasModel_ = false;

  struct BodyEntry {
    Geo geo;
    Buoyancy3DBodyInfo info;
  };
  std::vector<std::unique_ptr<BodyEntry>> bodies_;
  int selectedIdx_ = -1;

  /// 推进器箭头
  Geo arrowGeo_;
  bool arrowBuilt_ = false;
  std::vector<Thruster3DInfo> thrusters_;
  int selectedThrustIdx_ = -1;

  /// 传感器标记（共享几何体，每传感器独立位姿）
  Geo sensorBoxGeo_;
  Geo sensorCylGeo_;
  Geo sensorSphereGeo_;
  bool sensorGeosBuilt_ = false;
  std::vector<Sensor3DInfo> sensors_;
  int selectedSensorIdx_ = -1;

  /// 相机视线箭头（+X 方向，随姿态旋转）
  Geo sensorDirGeo_;
  bool sensorDirBuilt_ = false;

  /// 坐标轴（共享几何体，在选中项位置渲染）
  Geo axisLineGeo_;       // GL_LINES: 6 vertices for X/Y/Z axes
  Geo axisOriginGeo_;     // small sphere at origin
  bool axisBuilt_ = false;
  float axisLength_ = 0.5f;

  /// CoG 预览球
  Geo cogSphereGeo_;
  bool cogSphereBuilt_ = false;
  bool showCoG_ = false;
  QVector3D cogPos_{0, 0, 0};

  /// 水线面
  Geo waterlineGeo_;
  bool waterlineBuilt_ = false;
  float waterLevel_ = 0.0f;
  bool showWaterline_ = false;
  float modelZOffset_ = 0.0f;

  /// mesh 质心（原始坐标中的 v_average），用于摄像机聚焦
  double meshCx_ = 0, meshCy_ = 0, meshCz_ = 0;

  /// 相机
  float theta_ = 1.2f, phi_ = 0.8f, dist_ = 10.0f;
  QVector3D center_{0, 0, 0};
  QPointF lastMouse_;
  bool dragging_ = false;
};

#endif // BUOYANCY_3D_VIEW_HH_
