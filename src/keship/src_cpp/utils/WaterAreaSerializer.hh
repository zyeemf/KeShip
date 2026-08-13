/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水域序列化器 — 供 GUI 和 Server 两侧共用 */

#ifndef KESHIP_WATER_AREA_SERIALIZER_HH_
#define KESHIP_WATER_AREA_SERIALIZER_HH_

#include <string>
#include <vector>

#include "keship/utils/WaterArea.hh"

namespace keship
{
  /// \brief 将水域列表序列化为 JSON 字符串
  std::string SerializeWaterAreas(const std::vector<WaterArea> &_areas);

  /// \brief 从 JSON 字符串反序列化水域列表
  std::vector<WaterArea> DeserializeWaterAreas(const std::string &_json);
}

#endif
