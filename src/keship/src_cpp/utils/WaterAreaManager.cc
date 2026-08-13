/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 多片水域管理器实现 */

#include "WaterAreaManager.hh"

namespace keship
{

const WaterArea* WaterAreaManager::Lookup(double _x, double _y) const
{
  for (const auto &area : areas_)
  {
    if (area.Contains(_x, _y))
      return &area;
  }
  return nullptr;
}

} // namespace keship
