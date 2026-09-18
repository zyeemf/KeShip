/*
 * map_probe.cpp — MapWidget 底部文字布局取证探针
 *
 * 目的：确认 X 轴刻度数字（drawText @ ph+4~ph+16）与操作提示文字
 *       （drawText @ ph+2~ph+16）是否在同一纵向区间内重叠。
 * 做法：构造 MapWidget 并进入 polygonMode（水域绘制），
 *       分别截取「有提示文字」与「提示为空」两态，并放大底部区域。
 */
#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QString>
#include <vector>
#include <utility>

#include "MapWidget.hh"

static void shoot(MapWidget &_w, const QString &_path, const QString &_crop)
{
  _w.repaint();
  QApplication::processEvents();

  const QImage img = _w.grab().toImage();
  img.save(_path);

  // 裁出底部 60px 并放大 2 倍，便于肉眼核对文字是否叠在一起
  const int h = img.height();
  QImage bottom = img.copy(0, h - 60, img.width(), 60);
  bottom.scaled(img.width() * 2, 120, Qt::IgnoreAspectRatio,
                Qt::FastTransformation)
      .save(_crop);

  qInfo().noquote() << "  已保存" << _path << "尺寸" << img.width() << "x" << img.height();
}

int main(int argc, char **argv)
{
  QApplication app(argc, argv);

  MapWidget w;
  w.resize(520, 420);
  w.setPolygonMode(true);   // 水域绘制模式：会设提示「左键放置顶点 … 双击闭合」

  // 画一个四边形，让画面接近真实使用（世界坐标，米）
  std::vector<std::pair<double,double>> verts;
  verts.push_back({5.0, 5.0});
  verts.push_back({45.0, 5.0});
  verts.push_back({45.0, 28.0});
  verts.push_back({5.0, 28.0});
  w.setPolygonVertices(verts);
  w.show();
  QApplication::processEvents();

  qInfo().noquote() << "[1] 水域绘制模式（有操作提示）";
  shoot(w, "/tmp/map_hint.png", "/tmp/map_hint_bottom.png");

  qInfo().noquote() << "[2] 对照：提示清空（观察刻度数字是否被让出）";
  w.setHintText(QString());
  shoot(w, "/tmp/map_nohint.png", "/tmp/map_nohint_bottom.png");

  return 0;
}
