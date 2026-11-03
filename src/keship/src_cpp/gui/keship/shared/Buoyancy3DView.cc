/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* QOpenGLWidget 浮力体/模型 3D 预览 — 实现 */
/// ZhangYu 创建 2026.5.9

#include "Buoyancy3DView.hh"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <cmath>
#include <algorithm>
#include "MeshUtils.hh"
#include <iostream>

static const char *kVertSrc = R"(
#version 120
uniform mat4 u_mvp;
attribute vec3 a_pos;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
})";

static const char *kFragSrc = R"(
#version 120
uniform vec4 u_color;
void main() {
    gl_FragColor = u_color;
})";

/***********************************************************************
 * 构造 / 析构
 ***********************************************************************/

Buoyancy3DView::Buoyancy3DView(QWidget *parent) : QOpenGLWidget(parent)
{
  setMinimumSize(350, 300);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
}

Buoyancy3DView::~Buoyancy3DView()
{
  makeCurrent();
  modelGeo_.vbo.destroy();
  modelGeo_.ibo.destroy();
  modelGeo_.vao.destroy();
  for (auto &b : bodies_)
  { b->geo.vbo.destroy(); b->geo.ibo.destroy(); b->geo.vao.destroy(); }
  arrowGeo_.vbo.destroy(); arrowGeo_.ibo.destroy(); arrowGeo_.vao.destroy();
  sensorBoxGeo_.vbo.destroy(); sensorBoxGeo_.ibo.destroy(); sensorBoxGeo_.vao.destroy();
  sensorCylGeo_.vbo.destroy(); sensorCylGeo_.ibo.destroy(); sensorCylGeo_.vao.destroy();
  sensorSphereGeo_.vbo.destroy(); sensorSphereGeo_.ibo.destroy(); sensorSphereGeo_.vao.destroy();
  axisLineGeo_.vbo.destroy(); axisLineGeo_.ibo.destroy(); axisLineGeo_.vao.destroy();
  axisOriginGeo_.vbo.destroy(); axisOriginGeo_.ibo.destroy(); axisOriginGeo_.vao.destroy();
  waterlineGeo_.vbo.destroy(); waterlineGeo_.ibo.destroy(); waterlineGeo_.vao.destroy();
  prog_.reset();
  doneCurrent();
}

/***********************************************************************
 * OpenGL 初始化
 ***********************************************************************/

void Buoyancy3DView::initializeGL()
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  f->glClearColor(0.75f, 0.75f, 0.75f, 1.0f);
  f->glEnable(GL_DEPTH_TEST);
  f->glEnable(GL_BLEND);
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  f->glEnable(GL_LINE_SMOOTH);

  prog_ = std::make_unique<QOpenGLShaderProgram>();
  prog_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
  prog_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
  prog_->bindAttributeLocation("a_pos", 0);
  if (!prog_->link())
    std::cerr << "Buoyancy3DView: shader link failed: " << prog_->log().toStdString() << std::endl;

  /// OpenGL 上下文就绪，构建此前 setModel/syncBodies 预存的几何体
  if (hasModel_)
  {
    if (!modelInfo_.meshPath.empty())
      buildMeshGeo(modelGeo_, modelInfo_.meshPath);
    else if (modelInfo_.isBox)
      buildBoxGeo(modelGeo_, modelInfo_.boxDx, modelInfo_.boxDy, modelInfo_.boxDz);
    else
      buildCylGeo(modelGeo_, modelInfo_.cylRadius, modelInfo_.cylLength);
  }
  for (auto &b : bodies_)
  {
    auto &info = b->info;
    if (info.isBox)
      buildBoxGeo(b->geo, info.boxDx, info.boxDy, info.boxDz);
    else
      buildCylGeo(b->geo, info.cylRadius, info.cylLength);
  }
  if (!thrusters_.empty() && !arrowBuilt_)
    buildArrowGeo();

  /// 传感器共享几何体（延迟构建）
  if (!sensors_.empty() && !sensorGeosBuilt_)
  {
    buildBoxGeo(sensorBoxGeo_, 0.15, 0.1, 0.1);     // Camera
    buildCylGeo(sensorCylGeo_, 0.08, 0.15);          // Lidar
    buildSphereGeo(sensorSphereGeo_, 0.08);           // IMU/NavSat/Mag
    sensorGeosBuilt_ = true;
  }

  /// 相机视线箭头（延迟构建）
  if (!sensors_.empty() && !sensorDirBuilt_)
  {
    buildSensorDirGeo();
    sensorDirBuilt_ = true;
  }

  /// 坐标轴几何体（延迟构建）
  if (!axisBuilt_)
    buildAxisGeo();

  updateCamera();
}

void Buoyancy3DView::resizeGL(int _w, int _h)
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  f->glViewport(0, 0, _w, _h);
}

/***********************************************************************
 * 主渲染
 ***********************************************************************/

void Buoyancy3DView::paintGL()
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  prog_->bind();

  QMatrix4x4 proj;
  proj.perspective(45.0f, (float)width()/std::max(height(),1), 0.1f, 500.0f);

  QVector3D eye(std::cos(theta_)*std::sin(phi_)*dist_ + center_.x(),
                std::sin(theta_)*std::sin(phi_)*dist_ + center_.y(),
                std::cos(phi_)*dist_ + center_.z());
  QMatrix4x4 view;
  view.lookAt(eye, center_, QVector3D(0,0,1));

  /// 1. 模型（不写深度，避免遮挡同位置浮力体）
  if (hasModel_)
  {
    f->glDepthMask(GL_FALSE);
    prog_->setUniformValue("u_mvp", proj * view);
    prog_->setUniformValue("u_color", 0.3f, 0.3f, 0.3f, 0.4f);
    drawGeo(modelGeo_, false);
    f->glDepthMask(GL_TRUE);
  }

  /// 2. 浮力体半透明面（全部不写深度，避免互相遮挡）
  f->glDepthMask(GL_FALSE);
  for (int i = 0; i < (int)bodies_.size(); i++)
  {
    QMatrix4x4 mm = bodyMatrix(bodies_[i]->info);
    QMatrix4x4 bodyMVP = proj * view * mm;
    prog_->setUniformValue("u_mvp", bodyMVP);
    if (i == selectedIdx_)
      prog_->setUniformValue("u_color", 1.0f, 0.5f, 0.0f, 0.7f);
    else
      prog_->setUniformValue("u_color", 0.3f, 0.6f, 0.9f, 0.5f);
    drawGeo(bodies_[i]->geo, false);
  }
  f->glDepthMask(GL_TRUE);

  /// 3. 推进器箭头（半透明，不写深度）
  if (arrowBuilt_ && !thrusters_.empty())
  {
    f->glDepthMask(GL_FALSE);
    for (int i = 0; i < (int)thrusters_.size(); i++)
    {
      QMatrix4x4 mm = thrusterMatrix(thrusters_[i]);
      prog_->setUniformValue("u_mvp", proj * view * mm);
      if (i == selectedThrustIdx_)
        prog_->setUniformValue("u_color", 1.0f, 0.5f, 0.0f, 0.9f);
      else
        prog_->setUniformValue("u_color", 0.3f, 0.6f, 0.9f, 0.7f);
      drawGeo(arrowGeo_, false);
    }
    f->glDepthMask(GL_TRUE);
  }

  /// 4. 传感器标记（半透明，不写深度）
  if (sensorGeosBuilt_ && !sensors_.empty())
  {
    f->glDepthMask(GL_FALSE);
    for (int i = 0; i < (int)sensors_.size(); i++)
    {
      QMatrix4x4 mm = sensorMatrix(sensors_[i]);
      prog_->setUniformValue("u_mvp", proj * view * mm);
      if (i == selectedSensorIdx_)
        prog_->setUniformValue("u_color", 1.0f, 0.5f, 0.0f, 0.7f);
      else
        prog_->setUniformValue("u_color", 0.3f, 0.8f, 0.5f, 0.5f);
      /// 根据类型选择共享几何体
      int st = sensors_[i].type;
      if (st == 3)       // Camera → box
        drawGeo(sensorBoxGeo_, false);
      else if (st == 4)  // Lidar → cylinder
        drawGeo(sensorCylGeo_, false);
      else               // IMU(0)/NavSat(1)/Mag(2) → sphere
        drawGeo(sensorSphereGeo_, false);

      /// Camera → 视线箭头（+X 经姿态旋转，指示镜头朝向）
      if (st == 3)
      {
        prog_->setUniformValue("u_color", 1.0f, 0.85f, 0.1f, 0.9f);
        drawGeo(sensorDirGeo_, false);
      }
    }
    f->glDepthMask(GL_TRUE);
  }

  /// 5. 坐标轴
  QMatrix4x4 axisMM;
  bool hasSelAxis = false;

  if (axisBuilt_)
  {
    f->glDepthMask(GL_FALSE);
    f->glLineWidth(2.5f);

    /// 5a. 模型原点坐标系（始终显示）
    QMatrix4x4 originMVP = proj * view;
    float al = axisLength_;

    prog_->setUniformValue("u_color", 1.0f, 0.2f, 0.2f, 0.9f);
    prog_->setUniformValue("u_mvp", originMVP);
    drawLines(axisLineGeo_, 0, 2);  // X 轴红线
    QMatrix4x4 tipM;
    tipM = originMVP; tipM.translate(al, 0, 0);
    prog_->setUniformValue("u_mvp", tipM);
    drawGeo(axisOriginGeo_, false);  // X 末端红球

    prog_->setUniformValue("u_color", 0.2f, 1.0f, 0.2f, 0.9f);
    prog_->setUniformValue("u_mvp", originMVP);
    drawLines(axisLineGeo_, 2, 2);  // Y 轴绿线
    tipM = originMVP; tipM.translate(0, al, 0);
    prog_->setUniformValue("u_mvp", tipM);
    drawGeo(axisOriginGeo_, false);  // Y 末端绿球

    prog_->setUniformValue("u_color", 0.2f, 0.4f, 1.0f, 0.9f);
    prog_->setUniformValue("u_mvp", originMVP);
    drawLines(axisLineGeo_, 4, 2);  // Z 轴蓝线
    tipM = originMVP; tipM.translate(0, 0, al);
    prog_->setUniformValue("u_mvp", tipM);
    drawGeo(axisOriginGeo_, false);  // Z 末端蓝球

    prog_->setUniformValue("u_color", 1.0f, 1.0f, 1.0f, 0.9f);
    prog_->setUniformValue("u_mvp", originMVP);
    drawGeo(axisOriginGeo_, false);  // 原点白球

    /// 5b. 选中项坐标系（叠加在原点之上）
    if (selectedIdx_ >= 0 && selectedIdx_ < (int)bodies_.size())
    {
      axisMM = bodyMatrix(bodies_[selectedIdx_]->info);
      hasSelAxis = true;
    }
    else if (selectedThrustIdx_ >= 0 && selectedThrustIdx_ < (int)thrusters_.size())
    {
      axisMM = thrusterMatrix(thrusters_[selectedThrustIdx_]);
      hasSelAxis = true;
    }
    else if (selectedSensorIdx_ >= 0 && selectedSensorIdx_ < (int)sensors_.size())
    {
      axisMM = sensorMatrix(sensors_[selectedSensorIdx_]);
      hasSelAxis = true;
    }

    if (hasSelAxis)
    {
      QMatrix4x4 selMVP = proj * view * axisMM;

      prog_->setUniformValue("u_color", 1.0f, 0.3f, 0.3f, 1.0f);
      prog_->setUniformValue("u_mvp", selMVP);
      drawLines(axisLineGeo_, 0, 2);  // X
      tipM = selMVP; tipM.translate(al, 0, 0);
      prog_->setUniformValue("u_mvp", tipM);
      drawGeo(axisOriginGeo_, false);

      prog_->setUniformValue("u_color", 0.3f, 1.0f, 0.3f, 1.0f);
      prog_->setUniformValue("u_mvp", selMVP);
      drawLines(axisLineGeo_, 2, 2);  // Y
      tipM = selMVP; tipM.translate(0, al, 0);
      prog_->setUniformValue("u_mvp", tipM);
      drawGeo(axisOriginGeo_, false);

      prog_->setUniformValue("u_color", 0.3f, 0.5f, 1.0f, 1.0f);
      prog_->setUniformValue("u_mvp", selMVP);
      drawLines(axisLineGeo_, 4, 2);  // Z
      tipM = selMVP; tipM.translate(0, 0, al);
      prog_->setUniformValue("u_mvp", tipM);
      drawGeo(axisOriginGeo_, false);

      prog_->setUniformValue("u_color", 1.0f, 1.0f, 1.0f, 1.0f);
      prog_->setUniformValue("u_mvp", selMVP);
      drawGeo(axisOriginGeo_, false);
    }

    f->glLineWidth(1.0f);
    f->glDepthMask(GL_TRUE);
  }

  /// 6. CoG 预览球
  if (showCoG_)
  {
    if (!cogSphereBuilt_)
    {
      buildSphereGeo(cogSphereGeo_, 0.15);
      cogSphereBuilt_ = true;
    }
    f->glDepthMask(GL_FALSE);
    QMatrix4x4 cogMVP = proj * view;
    cogMVP.translate(cogPos_);
    prog_->setUniformValue("u_mvp", cogMVP);
    prog_->setUniformValue("u_color", 1.0f, 0.4f, 0.1f, 0.8f);
    drawGeo(cogSphereGeo_, false);
    f->glDepthMask(GL_TRUE);
  }

  /// 7. 水线面
  if (showWaterline_)
  {
    if (!waterlineBuilt_)
      buildWaterlineGeo();
    f->glDepthMask(GL_FALSE);
    QMatrix4x4 wlMatrix;
    wlMatrix.translate(0, 0, waterLevel_ + modelZOffset_);
    prog_->setUniformValue("u_mvp", proj * view * wlMatrix);
    prog_->setUniformValue("u_color", 0.15f, 0.35f, 0.75f, 0.35f);
    drawGeo(waterlineGeo_, false);
    f->glDepthMask(GL_TRUE);
  }

  prog_->release();
}

/***********************************************************************
 * 几何体构建
 ***********************************************************************/

void Buoyancy3DView::buildBoxGeo(Geo &_geo, double _dx, double _dy, double _dz)
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  float hx = (float)(_dx*0.5), hy = (float)(_dy*0.5), hz = (float)(_dz*0.5);
  _geo.isBox = true;

  struct { float x,y,z, nx,ny,nz; } v[] = {
    {-hx,-hy,-hz,0,0,-1},{ hx,-hy,-hz,0,0,-1},{ hx, hy,-hz,0,0,-1},{-hx, hy,-hz,0,0,-1},
    {-hx,-hy, hz,0,0, 1},{ hx,-hy, hz,0,0, 1},{ hx, hy, hz,0,0, 1},{-hx, hy, hz,0,0, 1},
    {-hx,-hy,-hz,0,-1,0},{ hx,-hy,-hz,0,-1,0},{ hx,-hy, hz,0,-1,0},{-hx,-hy, hz,0,-1,0},
    {-hx, hy,-hz,0, 1,0},{ hx, hy,-hz,0, 1,0},{ hx, hy, hz,0, 1,0},{-hx, hy, hz,0, 1,0},
    {-hx,-hy,-hz,-1,0,0},{-hx, hy,-hz,-1,0,0},{-hx, hy, hz,-1,0,0},{-hx,-hy, hz,-1,0,0},
    { hx,-hy,-hz, 1,0,0},{ hx, hy,-hz, 1,0,0},{ hx, hy, hz, 1,0,0},{ hx,-hy, hz, 1,0,0},
  };
  uint16_t idx[] = {
    0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
    12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23,
  };
  _geo.vertexCount = 24;
  _geo.indexCount = 36;

  _geo.vao.create();
  _geo.vao.bind();
  _geo.vbo.create(); _geo.vbo.bind();
  _geo.vbo.allocate(v, sizeof(v));
  _geo.ibo.create(); _geo.ibo.bind();
  _geo.ibo.allocate(idx, sizeof(idx));
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*6, (void*)0);
  f->glEnableVertexAttribArray(0);
  _geo.vao.release();
}

void Buoyancy3DView::buildCylGeo(Geo &_geo, double _r, double _l)
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  float hl = (float)(_l*0.5), r = (float)_r;
  int segs = 32;
  _geo.isBox = false;

  int nvTop = 1+segs, nvBot = 1+segs, nvSide = segs*2;
  int nv = nvTop+nvBot+nvSide, nt = segs+segs+segs*2;

  QByteArray vd;
  vd.resize(nv * 6 * (int)sizeof(float));
  auto *vf = reinterpret_cast<float*>(vd.data());
  int vi = 0;

  vf[vi++]=0; vf[vi++]=0; vf[vi++]=hl; vf[vi++]=0; vf[vi++]=0; vf[vi++]=1;
  for (int i = 0; i < segs; i++)
  { double a = 2.0*M_PI*i/segs;
    vf[vi++]=r*(float)cos(a); vf[vi++]=r*(float)sin(a); vf[vi++]=hl;
    vf[vi++]=0; vf[vi++]=0; vf[vi++]=1; }

  vf[vi++]=0; vf[vi++]=0; vf[vi++]=-hl; vf[vi++]=0; vf[vi++]=0; vf[vi++]=-1;
  for (int i = 0; i < segs; i++)
  { double a = 2.0*M_PI*((segs-i)%segs)/segs;
    vf[vi++]=r*(float)cos(a); vf[vi++]=r*(float)sin(a); vf[vi++]=-hl;
    vf[vi++]=0; vf[vi++]=0; vf[vi++]=-1; }

  for (int i = 0; i < segs; i++)
  { double a = 2.0*M_PI*i/segs;
    float x=r*(float)cos(a), y=r*(float)sin(a), nx=(float)cos(a), ny=(float)sin(a);
    vf[vi++]=x; vf[vi++]=y; vf[vi++]= hl; vf[vi++]=nx; vf[vi++]=ny; vf[vi++]=0;
    vf[vi++]=x; vf[vi++]=y; vf[vi++]=-hl; vf[vi++]=nx; vf[vi++]=ny; vf[vi++]=0; }

  QByteArray id;
  id.resize(nt * 3 * (int)sizeof(uint16_t));
  auto *ix = reinterpret_cast<uint16_t*>(id.data());
  int ii=0;
  int topBase=0, botBase=nvTop, sideBase=nvTop+nvBot;
  for (int i=0;i<segs;i++) { ix[ii++]=topBase; ix[ii++]=topBase+1+i; ix[ii++]=topBase+1+(i+1)%segs; }
  for (int i=0;i<segs;i++) { ix[ii++]=botBase; ix[ii++]=botBase+1+i; ix[ii++]=botBase+1+(i+1)%segs; }
  for (int i=0;i<segs;i++)
  { int t0=sideBase+i*2,b0=sideBase+i*2+1,t1=sideBase+((i+1)%segs)*2,b1=sideBase+((i+1)%segs)*2+1;
    ix[ii++]=t0;ix[ii++]=b0;ix[ii++]=t1; ix[ii++]=b0;ix[ii++]=b1;ix[ii++]=t1; }

  _geo.vertexCount = nv;
  _geo.indexCount = nt*3;

  _geo.vao.create();
  _geo.vao.bind();
  _geo.vbo.create(); _geo.vbo.bind();
  _geo.vbo.allocate(vd.constData(), vd.size());
  _geo.ibo.create(); _geo.ibo.bind();
  _geo.ibo.allocate(id.constData(), id.size());
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*6, (void*)0);
  f->glEnableVertexAttribArray(0);
  _geo.vao.release();
}

void Buoyancy3DView::buildArrowGeo()
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  int segs = 8;
  /// 箭头：杆部 x∈[-0.5, 0.3], r=0.04; 头部 x∈[0.3, 0.5], r_base=0.12
  float shaftR = 0.04f, shaftX0 = -0.5f, shaftX1 = 0.3f;
  float headR = 0.12f, headX0 = 0.3f, headX1 = 0.5f;

  int nvShaftCap0 = 1 + segs;   // 杆后端圆心+环
  int nvShaftCap1 = 1 + segs;   // 杆前端圆心+环
  int nvShaftSide = segs * 2;
  int nvHeadBase = 1 + segs;    // 头部底面圆心+环
  int nvHeadSide = segs * 2;    // 头部锥面 + 顶点重复
  int nv = nvShaftCap0 + nvShaftCap1 + nvShaftSide + nvHeadBase + nvHeadSide;
  int nt = segs*2 + segs*2 + segs*2 + segs*2; // caps + side + head_base + head_side

  QByteArray vd;
  vd.resize(nv * 6 * (int)sizeof(float));
  auto *vf = reinterpret_cast<float*>(vd.data());
  int vi = 0;

  /// cap0 (x=shaftX0, back)
  vf[vi++]=shaftX0; vf[vi++]=0; vf[vi++]=0; vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0;
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*((segs-i)%segs)/(double)segs;
    vf[vi++]=shaftX0; vf[vi++]=shaftR*(float)cos(a); vf[vi++]=shaftR*(float)sin(a);
    vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0; }

  /// cap1 (x=shaftX1, front)
  vf[vi++]=shaftX1; vf[vi++]=0; vf[vi++]=0; vf[vi++]=1; vf[vi++]=0; vf[vi++]=0;
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*i/segs;
    vf[vi++]=shaftX1; vf[vi++]=shaftR*(float)cos(a); vf[vi++]=shaftR*(float)sin(a);
    vf[vi++]=1; vf[vi++]=0; vf[vi++]=0; }

  /// shaft side
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*i/segs;
    float cx=(float)cos(a), sx=(float)sin(a);
    vf[vi++]=shaftX0; vf[vi++]=shaftR*cx; vf[vi++]=shaftR*sx; vf[vi++]=0; vf[vi++]=cx; vf[vi++]=sx;
    vf[vi++]=shaftX1; vf[vi++]=shaftR*cx; vf[vi++]=shaftR*sx; vf[vi++]=0; vf[vi++]=cx; vf[vi++]=sx; }

  /// head base cap (x=headX0)
  vf[vi++]=headX0; vf[vi++]=0; vf[vi++]=0; vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0;
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*((segs-i)%segs)/(double)segs;
    vf[vi++]=headX0; vf[vi++]=headR*(float)cos(a); vf[vi++]=headR*(float)sin(a);
    vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0; }

  /// head cone side (每个 seg 一个三角形: base_ring + tip)
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*i/segs;
    float cx=(float)cos(a), sx=(float)sin(a);
    float an = 2.0f*M_PI*(i+0.5f)/segs;
    float nx=(float)cos(an)*0.9487f, ny=(float)sin(an)*0.9487f, nz=0.3162f;
    vf[vi++]=headX0; vf[vi++]=headR*cx; vf[vi++]=headR*sx; vf[vi++]=nx; vf[vi++]=ny; vf[vi++]=nz;
    vf[vi++]=headX1; vf[vi++]=0; vf[vi++]=0; vf[vi++]=nx; vf[vi++]=ny; vf[vi++]=nz; }

  QByteArray id;
  id.resize(nt * 3 * (int)sizeof(uint16_t));
  auto *ix = reinterpret_cast<uint16_t*>(id.data());
  int ii=0;
  int base0=nvShaftCap0, base1=nvShaftCap0+nvShaftCap1, side0=nvShaftCap0+nvShaftCap1+nvShaftSide;
  int headSide0=nvShaftCap0+nvShaftCap1+nvShaftSide+nvHeadBase;

  /// shaft caps
  int c0=0, c1=nvShaftCap0;
  for (int i=0;i<segs;i++) { ix[ii++]=c0; ix[ii++]=c0+1+i; ix[ii++]=c0+1+(i+1)%segs; }
  for (int i=0;i<segs;i++) { ix[ii++]=c1; ix[ii++]=c1+1+i; ix[ii++]=c0+1+(i+1)%segs; }
  /// shaft sides
  for (int i=0;i<segs;i++)
  { int t0=side0+i*2,b0=side0+i*2+1,t1=side0+((i+1)%segs)*2,b1=side0+((i+1)%segs)*2+1;
    ix[ii++]=t0;ix[ii++]=b0;ix[ii++]=t1; ix[ii++]=b0;ix[ii++]=b1;ix[ii++]=t1; }
  /// head base cap (reverse order for outward normal)
  int hb=side0+nvShaftSide;
  for (int i=0;i<segs;i++) { ix[ii++]=hb; ix[ii++]=hb+1+i; ix[ii++]=hb+1+(i+1)%segs; }
  /// head cone
  for (int i=0;i<segs;i++)
  { int hi0=headSide0+i*2, hi1=headSide0+i*2+1;
    ix[ii++]=hi0;ix[ii++]=hi1;ix[ii++]=headSide0+((i+1)%segs)*2; }

  arrowGeo_.vertexCount = nv;
  arrowGeo_.indexCount = nt*3;

  arrowGeo_.vao.create();
  arrowGeo_.vao.bind();
  arrowGeo_.vbo.create(); arrowGeo_.vbo.bind();
  arrowGeo_.vbo.allocate(vd.constData(), vd.size());
  arrowGeo_.ibo.create(); arrowGeo_.ibo.bind();
  arrowGeo_.ibo.allocate(id.constData(), id.size());
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*6, (void*)0);
  f->glEnableVertexAttribArray(0);
  arrowGeo_.vao.release();
  arrowBuilt_ = true;
}

void Buoyancy3DView::buildSensorDirGeo()
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  int segs = 16;
  /// 相机视线箭头：杆部 x∈[0, 0.22], r=0.025; 头部 x∈[0.22, 0.40], r_base=0.06
  float shaftR = 0.025f, shaftX0 = 0.0f, shaftX1 = 0.22f;
  float headR = 0.06f, headX0 = 0.22f, headX1 = 0.40f;

  int nvShaftCap0 = 1 + segs;   // 杆后端圆心+环
  int nvShaftCap1 = 1 + segs;   // 杆前端圆心+环
  int nvShaftSide = segs * 2;
  int nvHeadBase = 1 + segs;    // 头部底面圆心+环
  int nvHeadSide = segs * 2;    // 头部锥面 + 顶点重复
  int nv = nvShaftCap0 + nvShaftCap1 + nvShaftSide + nvHeadBase + nvHeadSide;
  int nt = segs*2 + segs*2 + segs*2 + segs*2; // caps + side + head_base + head_side

  QByteArray vd;
  vd.resize(nv * 6 * (int)sizeof(float));
  auto *vf = reinterpret_cast<float*>(vd.data());
  int vi = 0;

  /// cap0 (x=shaftX0, back)
  vf[vi++]=shaftX0; vf[vi++]=0; vf[vi++]=0; vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0;
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*((segs-i)%segs)/(double)segs;
    vf[vi++]=shaftX0; vf[vi++]=shaftR*(float)cos(a); vf[vi++]=shaftR*(float)sin(a);
    vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0; }

  /// cap1 (x=shaftX1, front)
  vf[vi++]=shaftX1; vf[vi++]=0; vf[vi++]=0; vf[vi++]=1; vf[vi++]=0; vf[vi++]=0;
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*i/segs;
    vf[vi++]=shaftX1; vf[vi++]=shaftR*(float)cos(a); vf[vi++]=shaftR*(float)sin(a);
    vf[vi++]=1; vf[vi++]=0; vf[vi++]=0; }

  /// shaft side
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*i/segs;
    float cx=(float)cos(a), sx=(float)sin(a);
    vf[vi++]=shaftX0; vf[vi++]=shaftR*cx; vf[vi++]=shaftR*sx; vf[vi++]=0; vf[vi++]=cx; vf[vi++]=sx;
    vf[vi++]=shaftX1; vf[vi++]=shaftR*cx; vf[vi++]=shaftR*sx; vf[vi++]=0; vf[vi++]=cx; vf[vi++]=sx; }

  /// head base cap (x=headX0)
  vf[vi++]=headX0; vf[vi++]=0; vf[vi++]=0; vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0;
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*((segs-i)%segs)/(double)segs;
    vf[vi++]=headX0; vf[vi++]=headR*(float)cos(a); vf[vi++]=headR*(float)sin(a);
    vf[vi++]=-1; vf[vi++]=0; vf[vi++]=0; }

  /// head cone side (每个 seg 一个三角形: base_ring + tip)
  for (int i=0;i<segs;i++)
  { double a=2.0*M_PI*i/segs;
    float cx=(float)cos(a), sx=(float)sin(a);
    float an = 2.0f*M_PI*(i+0.5f)/segs;
    float nx=(float)cos(an)*0.9487f, ny=(float)sin(an)*0.9487f, nz=0.3162f;
    vf[vi++]=headX0; vf[vi++]=headR*cx; vf[vi++]=headR*sx; vf[vi++]=nx; vf[vi++]=ny; vf[vi++]=nz;
    vf[vi++]=headX1; vf[vi++]=0; vf[vi++]=0; vf[vi++]=nx; vf[vi++]=ny; vf[vi++]=nz; }

  QByteArray id;
  id.resize(nt * 3 * (int)sizeof(uint16_t));
  auto *ix = reinterpret_cast<uint16_t*>(id.data());
  int ii=0;
  int base0=nvShaftCap0, base1=nvShaftCap0+nvShaftCap1, side0=nvShaftCap0+nvShaftCap1+nvShaftSide;
  int headSide0=nvShaftCap0+nvShaftCap1+nvShaftSide+nvHeadBase;

  /// shaft caps
  int c0=0, c1=nvShaftCap0;
  for (int i=0;i<segs;i++) { ix[ii++]=c0; ix[ii++]=c0+1+i; ix[ii++]=c0+1+(i+1)%segs; }
  for (int i=0;i<segs;i++) { ix[ii++]=c1; ix[ii++]=c1+1+i; ix[ii++]=c0+1+(i+1)%segs; }
  /// shaft sides
  for (int i=0;i<segs;i++)
  { int t0=side0+i*2,b0=side0+i*2+1,t1=side0+((i+1)%segs)*2,b1=side0+((i+1)%segs)*2+1;
    ix[ii++]=t0;ix[ii++]=b0;ix[ii++]=t1; ix[ii++]=b0;ix[ii++]=b1;ix[ii++]=t1; }
  /// head base cap (reverse order for outward normal)
  int hb=side0+nvShaftSide;
  for (int i=0;i<segs;i++) { ix[ii++]=hb; ix[ii++]=hb+1+i; ix[ii++]=hb+1+(i+1)%segs; }
  /// head cone
  for (int i=0;i<segs;i++)
  { int hi0=headSide0+i*2, hi1=headSide0+i*2+1;
    ix[ii++]=hi0;ix[ii++]=hi1;ix[ii++]=headSide0+((i+1)%segs)*2; }

  sensorDirGeo_.vertexCount = nv;
  sensorDirGeo_.indexCount = nt*3;

  sensorDirGeo_.vao.create();
  sensorDirGeo_.vao.bind();
  sensorDirGeo_.vbo.create(); sensorDirGeo_.vbo.bind();
  sensorDirGeo_.vbo.allocate(vd.constData(), vd.size());
  sensorDirGeo_.ibo.create(); sensorDirGeo_.ibo.bind();
  sensorDirGeo_.ibo.allocate(id.constData(), id.size());
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*6, (void*)0);
  f->glEnableVertexAttribArray(0);
  sensorDirGeo_.vao.release();
}

void Buoyancy3DView::buildSphereGeo(Geo &_geo, double _r, int _segs)
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  _geo.isBox = false;

  int latSteps = _segs / 2;
  int lonSteps = _segs;
  int nv = (latSteps + 1) * (lonSteps + 1);
  int nt = latSteps * lonSteps * 2;

  QByteArray vd;
  vd.resize(nv * 6 * (int)sizeof(float));
  auto *vf = reinterpret_cast<float*>(vd.data());
  int vi = 0;

  for (int ilat = 0; ilat <= latSteps; ilat++)
  {
    float theta = (float)(M_PI * ilat / latSteps);  // 0..PI, 从顶到下
    float st = sinf(theta), ct = cosf(theta);
    for (int ilon = 0; ilon <= lonSteps; ilon++)
    {
      float phi = (float)(2.0 * M_PI * ilon / lonSteps);
      float nx = st * cosf(phi), ny = st * sinf(phi), nz = ct;
      vf[vi++] = nx * (float)_r;
      vf[vi++] = ny * (float)_r;
      vf[vi++] = nz * (float)_r;
      vf[vi++] = nx;
      vf[vi++] = ny;
      vf[vi++] = nz;
    }
  }

  QByteArray id;
  id.resize(nt * 3 * (int)sizeof(uint16_t));
  auto *ix = reinterpret_cast<uint16_t*>(id.data());
  int ii = 0;
  for (int ilat = 0; ilat < latSteps; ilat++)
  {
    for (int ilon = 0; ilon < lonSteps; ilon++)
    {
      uint16_t a = (uint16_t)(ilat * (lonSteps + 1) + ilon);
      uint16_t b = a + (uint16_t)(lonSteps + 1);
      ix[ii++] = a; ix[ii++] = b; ix[ii++] = a + 1;
      ix[ii++] = b; ix[ii++] = b + 1; ix[ii++] = a + 1;
    }
  }

  _geo.vertexCount = nv;
  _geo.indexCount = nt * 3;

  _geo.vao.create();
  _geo.vao.bind();
  _geo.vbo.create(); _geo.vbo.bind();
  _geo.vbo.allocate(vd.constData(), vd.size());
  _geo.ibo.create(); _geo.ibo.bind();
  _geo.ibo.allocate(id.constData(), id.size());
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*6, (void*)0);
  f->glEnableVertexAttribArray(0);
  _geo.vao.release();
}

void Buoyancy3DView::buildAxisGeo()
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

  /// 3 条轴线：原点→轴向，GL_LINES 6 顶点（无索引，无 normal）
  float al = axisLength_;
  float axisLines[] = {
    0, 0, 0,  al, 0, 0,   // X 轴
    0, 0, 0,  0, al, 0,   // Y 轴
    0, 0, 0,  0, 0, al,   // Z 轴
  };
  axisLineGeo_.vertexCount = 6;
  axisLineGeo_.indexCount = 0;

  axisLineGeo_.vao.create();
  axisLineGeo_.vao.bind();
  axisLineGeo_.vbo.create(); axisLineGeo_.vbo.bind();
  axisLineGeo_.vbo.allocate(axisLines, (int)sizeof(axisLines));
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, (void*)0);
  f->glEnableVertexAttribArray(0);
  axisLineGeo_.vao.release();

  /// 原点小球
  buildSphereGeo(axisOriginGeo_, 0.04);
  axisBuilt_ = true;
}

void Buoyancy3DView::drawGeo(Geo &_geo, bool /*_wireframe*/)
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  _geo.vao.bind();
  if (_geo.indexCount > 0)
    f->glDrawElements(GL_TRIANGLES, _geo.indexCount, GL_UNSIGNED_SHORT, nullptr);
  else
    f->glDrawArrays(GL_TRIANGLES, 0, _geo.vertexCount);
  _geo.vao.release();
}

void Buoyancy3DView::drawLines(Geo &_geo, int _first, int _count)
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  _geo.vao.bind();
  f->glDrawArrays(GL_LINES, _first, _count);
  _geo.vao.release();
}

void Buoyancy3DView::setWaterLevel(float _z) { waterLevel_ = _z; }
void Buoyancy3DView::setShowWaterline(bool _show) { showWaterline_ = _show; }
void Buoyancy3DView::setModelZOffset(float _z) { modelZOffset_ = _z; }

void Buoyancy3DView::buildWaterlineGeo()
{
  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  if (!f) return;

  const float sz = 15.0f;
  float verts[] = {
    -sz, -sz, 0.0f,
     sz, -sz, 0.0f,
     sz,  sz, 0.0f,
    -sz, -sz, 0.0f,
     sz,  sz, 0.0f,
    -sz,  sz, 0.0f,
  };
  int nVerts = 6;

  uint16_t idx[] = {0, 1, 2, 3, 4, 5};

  waterlineGeo_.vbo.create();
  waterlineGeo_.vbo.bind();
  waterlineGeo_.vbo.allocate(verts, nVerts * 3 * sizeof(float));

  waterlineGeo_.ibo.create();
  waterlineGeo_.ibo.bind();
  waterlineGeo_.ibo.allocate(idx, nVerts * sizeof(uint16_t));

  waterlineGeo_.vao.create();
  waterlineGeo_.vao.bind();
  waterlineGeo_.vbo.bind();
  f->glEnableVertexAttribArray(0);
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  waterlineGeo_.ibo.bind();
  waterlineGeo_.vao.release();

  waterlineGeo_.vertexCount = nVerts;
  waterlineGeo_.indexCount = nVerts;
  waterlineBuilt_ = true;
}

QMatrix4x4 Buoyancy3DView::bodyMatrix(const Buoyancy3DBodyInfo &_bi) const
{
  QMatrix4x4 m;
  m.translate((float)_bi.poseX, (float)_bi.poseY, (float)_bi.poseZ);
  m.rotate((float)(_bi.poseYaw * 180.0 / M_PI),   0, 0, 1);
  m.rotate((float)(_bi.posePitch * 180.0 / M_PI), 0, 1, 0);
  m.rotate((float)(_bi.poseRoll * 180.0 / M_PI),  1, 0, 0);
  return m;
}

QMatrix4x4 Buoyancy3DView::thrusterMatrix(const Thruster3DInfo &_ti) const
{
  QMatrix4x4 m;
  m.translate((float)_ti.poseX, (float)_ti.poseY, (float)_ti.poseZ);
  m.rotate((float)(_ti.poseYaw * 180.0 / M_PI),   0, 0, 1);
  m.rotate((float)(_ti.posePitch * 180.0 / M_PI), 0, 1, 0);
  m.rotate((float)(_ti.poseRoll * 180.0 / M_PI),  1, 0, 0);
  return m;
}

QMatrix4x4 Buoyancy3DView::sensorMatrix(const Sensor3DInfo &_si) const
{
  QMatrix4x4 m;
  m.translate((float)_si.poseX, (float)_si.poseY, (float)_si.poseZ);
  m.rotate((float)(_si.poseYaw * 180.0 / M_PI),   0, 0, 1);
  m.rotate((float)(_si.posePitch * 180.0 / M_PI), 0, 1, 0);
  m.rotate((float)(_si.poseRoll * 180.0 / M_PI),  1, 0, 0);
  return m;
}

/***********************************************************************
 * 鼠标交互
 ***********************************************************************/

void Buoyancy3DView::mousePressEvent(QMouseEvent *_ev)
{
  if (_ev->button() == Qt::LeftButton)
  { dragging_ = true; lastMouse_ = _ev->pos(); }
}

void Buoyancy3DView::mouseMoveEvent(QMouseEvent *_ev)
{
  if (!dragging_) return;
  QPointF d = _ev->pos() - lastMouse_;
  lastMouse_ = _ev->pos();
  theta_ -= d.x() * 0.005f;
  phi_   += d.y() * 0.005f;
  phi_ = std::max(0.05f, std::min(phi_, (float)M_PI - 0.05f));
  update();
}

void Buoyancy3DView::mouseReleaseEvent(QMouseEvent *_ev)
{
  if (_ev->button() == Qt::LeftButton) dragging_ = false;
}

void Buoyancy3DView::wheelEvent(QWheelEvent *_ev)
{
  dist_ -= _ev->angleDelta().y() * 0.02f;
  dist_ = std::max(0.5f, std::min(dist_, 100.0f));
  update();
}

/***********************************************************************
 * 公开 API
 ***********************************************************************/

void Buoyancy3DView::buildMeshGeo(Geo &_geo, const std::string &_path)
{
  // 与 KSM 加载一致：先走规范化缓存，保证所有 DAE/STL/OBJ 预览与 KSM 同坐标系
  std::string meshPath = _path;
  std::string normalized = keship::NormalizeMeshToSDFCache(_path);
  if (!normalized.empty())
    meshPath = normalized;

  auto mesh = keship::LoadMeshFromFile(meshPath);
  if (mesh.triangles.empty()) return;

  double cx = 0, cy = 0, cz = 0;
  for (auto &vt : mesh.vertices) {
    cx += vt.X(); cy += vt.Y(); cz += vt.Z();
  }
  cx /= mesh.vertices.size(); cy /= mesh.vertices.size(); cz /= mesh.vertices.size();

  meshCx_ = cx; meshCy_ = cy; meshCz_ = cz;

  // 相对形心的最大距离（包围半径）—— 供 updateCamera 自动定视距，
  // 否则大尺寸网格会因沿用默认圆柱尺寸而贴面显示（2026-10-15）
  double r2 = 0.0;
  for (auto &vt : mesh.vertices)
  {
    const double dx = vt.X() - cx, dy = vt.Y() - cy, dz = vt.Z() - cz;
    r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
  }
  meshRadius_ = std::sqrt(r2);

  std::vector<float> v;
  for (auto &t : mesh.triangles)
  {
    for (auto *p : {&t.v0, &t.v1, &t.v2})
    {
      v.push_back((float)(p->X()));
      v.push_back((float)(p->Y()));
      v.push_back((float)(p->Z()));
      v.push_back(0); v.push_back(0); v.push_back(0);
    }
  }
  _geo.vertexCount = (int)(v.size()/6);
  _geo.indexCount = 0;

  QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
  _geo.vao.create();
  _geo.vao.bind();
  _geo.vbo.create(); _geo.vbo.bind();
  _geo.vbo.allocate(v.data(), (int)(v.size()*sizeof(float)));
  f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*6, (void*)0);
  f->glEnableVertexAttribArray(0);
  _geo.vao.release();
}

void Buoyancy3DView::setModel(const Buoyancy3DModelInfo &_model)
{
  modelInfo_ = _model;
  hasModel_ = true;

  if (isValid())  /// OpenGL 上下文已就绪
  {
    makeCurrent();
    if (!_model.meshPath.empty())
    {
      buildMeshGeo(modelGeo_, _model.meshPath);
    }
    else if (_model.isBox)
    {
      buildBoxGeo(modelGeo_, _model.boxDx, _model.boxDy, _model.boxDz);
      meshCx_ = meshCy_ = meshCz_ = 0;
    }
    else
    {
      buildCylGeo(modelGeo_, _model.cylRadius, _model.cylLength);
      meshCx_ = meshCy_ = meshCz_ = 0;
    }
    doneCurrent();
  }
  updateCamera();
  update();
}

void Buoyancy3DView::syncBodies(const std::vector<Buoyancy3DBodyInfo> &_bodies)
{
  /// 数量对齐（不涉及 OpenGL）
  while (bodies_.size() > _bodies.size())
    bodies_.pop_back();
  while (bodies_.size() < _bodies.size())
    bodies_.push_back(std::make_unique<BodyEntry>());

  bool glReady = isValid();
  if (glReady) makeCurrent();

  for (size_t i = 0; i < _bodies.size(); i++)
  {
    auto &b = bodies_[i];
    const auto &info = _bodies[i];
    bool needBuild = glReady &&
        ((b->info.isBox != info.isBox) ||
         (info.isBox && (b->info.boxDx!=info.boxDx || b->info.boxDy!=info.boxDy || b->info.boxDz!=info.boxDz)) ||
         (!info.isBox && (b->info.cylRadius!=info.cylRadius || b->info.cylLength!=info.cylLength)));
    if (needBuild)
    {
      b->geo.vbo.destroy(); b->geo.ibo.destroy(); b->geo.vao.destroy();
      if (info.isBox) buildBoxGeo(b->geo, info.boxDx, info.boxDy, info.boxDz);
      else            buildCylGeo(b->geo, info.cylRadius, info.cylLength);
    }
    b->info = info;
  }

  if (glReady) doneCurrent();
  updateCamera();
  update();
}

void Buoyancy3DView::setSelectedIndex(int _idx)
{
  selectedIdx_ = _idx;
  update();
}

void Buoyancy3DView::updateBody(int _idx, const Buoyancy3DBodyInfo &_info)
{
  if (_idx < 0 || _idx >= (int)bodies_.size()) return;

  auto &b = bodies_[_idx];
  bool needBuild = (b->info.isBox != _info.isBox)
      || (_info.isBox && (b->info.boxDx!=_info.boxDx || b->info.boxDy!=_info.boxDy || b->info.boxDz!=_info.boxDz))
      || (!_info.isBox && (b->info.cylRadius!=_info.cylRadius || b->info.cylLength!=_info.cylLength));

  if (isValid())
  {
    makeCurrent();
    if (needBuild)
    {
      b->geo.vbo.destroy(); b->geo.ibo.destroy(); b->geo.vao.destroy();
      if (_info.isBox) buildBoxGeo(b->geo, _info.boxDx, _info.boxDy, _info.boxDz);
      else             buildCylGeo(b->geo, _info.cylRadius, _info.cylLength);
    }
    doneCurrent();
  }
  b->info = _info;
  update();
}

void Buoyancy3DView::updateCamera()
{
  double maxDim = 2.0;
  if (hasModel_)
  {
    auto &m = modelInfo_;
    if (m.isBox)              maxDim = std::max({m.boxDx, m.boxDy, m.boxDz, maxDim});
    else if (!m.meshPath.empty()) maxDim = std::max({meshRadius_ * 2.0, maxDim});
    else                      maxDim = std::max({m.cylRadius*2, m.cylLength, maxDim});
  }
  for (auto &b : bodies_)
  {
    auto &i = b->info;
    if (i.isBox) maxDim = std::max({i.boxDx, i.boxDy, i.boxDz, maxDim});
    else         maxDim = std::max({i.cylRadius*2, i.cylLength, maxDim});
  }
  dist_ = (float)(maxDim * 2.5);
  if (hasModel_ && !modelInfo_.meshPath.empty())
    center_ = QVector3D(meshCx_, meshCy_, meshCz_);
  else
    center_ = QVector3D(0, 0, 0);
}

void Buoyancy3DView::syncThrusters(const std::vector<Thruster3DInfo> &_thrusters)
{
  thrusters_ = _thrusters;
  if (isValid() && !thrusters_.empty() && !arrowBuilt_)
  {
    makeCurrent();
    buildArrowGeo();
    doneCurrent();
  }
  updateCamera();
  update();
}

void Buoyancy3DView::updateThruster(int _idx, const Thruster3DInfo &_info)
{
  if (_idx < 0 || _idx >= (int)thrusters_.size()) return;
  thrusters_[_idx] = _info;
  update();
}

void Buoyancy3DView::setSelectedThrusterIdx(int _idx)
{
  selectedThrustIdx_ = _idx;
  update();
}

void Buoyancy3DView::syncSensors(const std::vector<Sensor3DInfo> &_sensors)
{
  sensors_ = _sensors;
  if (isValid() && !sensors_.empty() && !sensorGeosBuilt_)
  {
    makeCurrent();
    buildBoxGeo(sensorBoxGeo_, 0.15, 0.1, 0.1);
    buildCylGeo(sensorCylGeo_, 0.08, 0.15);
    buildSphereGeo(sensorSphereGeo_, 0.08);
    sensorGeosBuilt_ = true;
    doneCurrent();
  }
  updateCamera();
  update();
}

void Buoyancy3DView::updateSensor(int _idx, const Sensor3DInfo &_info)
{
  if (_idx < 0 || _idx >= (int)sensors_.size()) return;
  sensors_[_idx] = _info;
  update();
}

void Buoyancy3DView::setSelectedSensorIdx(int _idx)
{
  selectedSensorIdx_ = _idx;
  update();
}

void Buoyancy3DView::setCoGPosition(float _x, float _y, float _z)
{
  cogPos_ = QVector3D(_x, _y, _z);
  update();
}

void Buoyancy3DView::setShowCoG(bool _show)
{
  showCoG_ = _show;
  update();
}
