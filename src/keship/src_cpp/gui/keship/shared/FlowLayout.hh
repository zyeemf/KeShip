/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* FlowLayout — Qt 流式布局，子 widget 自动换行 */

#ifndef FLOW_LAYOUT_HH_
#define FLOW_LAYOUT_HH_

#include <QLayout>
#include <QStyle>

class FlowLayout : public QLayout
{
public:
  explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
  ~FlowLayout() override;

  void addItem(QLayoutItem *item) override;
  int count() const override;
  QLayoutItem *itemAt(int index) const override;
  QLayoutItem *takeAt(int index) override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;
  QSize minimumSize() const override;
  QSize sizeHint() const override;
  void setGeometry(const QRect &rect) override;

  int horizontalSpacing() const;
  int verticalSpacing() const;

private:
  int doLayout(const QRect &rect, bool testOnly) const;
  int smartSpacing(QStyle::PixelMetric pm) const;

  QList<QLayoutItem *> itemList_;
  int hSpace_;
  int vSpace_;
};

#endif
