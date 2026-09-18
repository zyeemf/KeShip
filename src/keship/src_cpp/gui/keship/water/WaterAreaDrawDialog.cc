/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水区绘制对话框实现 */

#include "WaterAreaDrawDialog.hh"
#include "../tasks/MapWidget.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>

#include "keship/utils/SeaState.hh"

WaterAreaDrawDialog::WaterAreaDrawDialog(QWidget *parent)
    : QDialog(parent)
{
  /// 初始风浪参数对齐"按海况 1 级自动设置"（与默认水域一致的"标准 1 级"），
  /// 用户后续可在编辑器手动修改，届时以用户值为准
  keship::SeaState::ParamsFromLevel(1, &result_.waveParams.period,
                                    &result_.windParams.speed,
                                    &result_.waveParams.gain);
  setWindowTitle("绘制水域");
  resize(500, 450);

  auto *layout = new QVBoxLayout(this);

  nameEdit_ = new QLineEdit(this);
  nameEdit_->setPlaceholderText("水域名称");
  layout->addWidget(nameEdit_);

  map_ = new MapWidget(this);
  map_->setPolygonMode(true);
  layout->addWidget(map_, 1);

  auto *btnRow = new QHBoxLayout;
  auto *closeBtn = new QPushButton("完成", this);
  connect(closeBtn, &QPushButton::clicked, this, [this]() {
    if (map_->polygonVertices().size() >= 3)
      onPolygonFinished();
  });

  auto *cancelBtn = new QPushButton("取消", this);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  btnRow->addStretch();
  btnRow->addWidget(closeBtn);
  btnRow->addWidget(cancelBtn);
  btnRow->addStretch();
  for (QPushButton *b : {closeBtn, cancelBtn}) b->setMinimumHeight(30);
  layout->addLayout(btnRow);

  connect(map_, &MapWidget::polygonFinished,
          this, &WaterAreaDrawDialog::onPolygonFinished);
}

void WaterAreaDrawDialog::setPolygonVertices(
    const std::vector<std::pair<double,double>> &_verts)
{
  map_->setPolygonVertices(_verts);
}

void WaterAreaDrawDialog::setSceneModels(
    const std::vector<keship::ModelFootprint> &_models)
{
  map_->setSceneModels(_models);
  map_->fitToSceneModels();
}

void WaterAreaDrawDialog::setReferenceWaterAreas(
    const std::vector<keship::WaterArea> &_areas)
{
  map_->setReferenceWaterAreas(_areas);
  // 所有数据（当前多边形/模型/参考水域）就绪后统一适配视图并集
  map_->fitToAll();
}

void WaterAreaDrawDialog::onPolygonFinished()
{
  const auto &verts = map_->polygonVertices();
  if (verts.size() < 3) return;

  double xMin = verts[0].first, xMax = verts[0].first;
  double yMin = verts[0].second, yMax = verts[0].second;
  for (auto &v : verts) {
    xMin = std::min(xMin, v.first);
    xMax = std::max(xMax, v.first);
    yMin = std::min(yMin, v.second);
    yMax = std::max(yMax, v.second);
  }

  result_.xMin = xMin; result_.xMax = xMax;
  result_.yMin = yMin; result_.yMax = yMax;
  result_.vertices = verts;
  result_.name = nameEdit_->text().trimmed().toStdString();
  if (result_.name.empty())
    result_.name = "水域";
  accept();
}
