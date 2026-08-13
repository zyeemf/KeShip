/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* FlowLayout 实现 — 基于 Qt 官方 FlowLayout example */

#include "FlowLayout.hh"
#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), hSpace_(hSpacing), vSpace_(vSpacing)
{
  setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
  while (QLayoutItem *item = takeAt(0))
    delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { itemList_.append(item); }

int FlowLayout::count() const { return itemList_.size(); }

QLayoutItem *FlowLayout::itemAt(int index) const
{
  return itemList_.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
  return index >= 0 && index < itemList_.size()
      ? itemList_.takeAt(index) : nullptr;
}

int FlowLayout::horizontalSpacing() const
{
  if (hSpace_ >= 0) return hSpace_;
  return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
  if (vSpace_ >= 0) return vSpace_;
  return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
  QObject *p = parent();
  if (!p) return -1;
  if (p->isWidgetType())
  {
    auto *pw = static_cast<QWidget *>(p);
    return pw->style()->pixelMetric(pm, nullptr, pw);
  }
  return static_cast<QLayout *>(p)->spacing();
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
  int left, top, right, bottom;
  getContentsMargins(&left, &top, &right, &bottom);
  QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
  int x = effectiveRect.x();
  int y = effectiveRect.y();
  int lineHeight = 0;
  int totalHeight = 0;
  int hSpacing = horizontalSpacing();
  int vSpacing = verticalSpacing();

  for (QLayoutItem *item : itemList_)
  {
    QSize itemSize = item->sizeHint();
    int nextX = x + itemSize.width() + hSpacing;
    if (nextX - hSpacing > effectiveRect.right() && lineHeight > 0)
    {
      x = effectiveRect.x();
      y = y + lineHeight + vSpacing;
      nextX = x + itemSize.width() + hSpacing;
      lineHeight = 0;
    }

    if (!testOnly)
      item->setGeometry(QRect(QPoint(x, y), itemSize));

    x = nextX;
    lineHeight = qMax(lineHeight, itemSize.height());
  }

  totalHeight = y + lineHeight - rect.y() + bottom;
  return totalHeight;
}

QSize FlowLayout::minimumSize() const
{
  QSize size;
  for (const QLayoutItem *item : itemList_)
    size = size.expandedTo(item->minimumSize());
  int left, top, right, bottom;
  getContentsMargins(&left, &top, &right, &bottom);
  size += QSize(left + right, top + bottom);
  return size;
}

QSize FlowLayout::sizeHint() const
{
  return minimumSize();
}

int FlowLayout::heightForWidth(int width) const
{
  return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
  QLayout::setGeometry(rect);
  doLayout(rect, false);
}
