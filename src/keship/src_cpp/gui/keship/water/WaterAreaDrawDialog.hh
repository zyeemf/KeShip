/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水区绘制对话框 — 2D 俯视地图多边形绘制 */

#ifndef WATER_AREA_DRAW_DIALOG_HH_
#define WATER_AREA_DRAW_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include "keship/utils/WaterArea.hh"

class MapWidget;

class WaterAreaDrawDialog : public QDialog
{
  Q_OBJECT

public:
  explicit WaterAreaDrawDialog(QWidget *parent = nullptr);

  keship::WaterArea getWaterArea() const { return result_; }
  void setDefaultName(const QString &_name)
      { nameEdit_->setText(_name); }
  /// 注入被编辑水域完整数据（几何/风浪流参数），编辑完成后 getWaterArea
  /// 保留其参数，仅几何与名称可按用户操作变化（修复编辑重置风浪参数 bug）
  /// 该方法**仅在编辑既有水域时调用**，故一并区分窗口标题（新建为"绘制水域"）
  void setInitialArea(const keship::WaterArea &_area)
  {
    result_ = _area;
    setWindowTitle(QString::fromUtf8("编辑水域"));
  }
  void setPolygonVertices(
      const std::vector<std::pair<double,double>> &_verts);
  void setSceneModels(const std::vector<keship::ModelFootprint> &_models);
  void setReferenceWaterAreas(
      const std::vector<keship::WaterArea> &_areas);
  QString name() const { return nameEdit_->text().trimmed(); }

private slots:
  void onPolygonFinished();

private:
  MapWidget *map_;
  QLineEdit *nameEdit_;
  keship::WaterArea result_;
};

#endif
