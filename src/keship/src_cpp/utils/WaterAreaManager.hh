/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 多片水域管理器 */

#ifndef KESHIP_WATER_AREA_MANAGER_HH_
#define KESHIP_WATER_AREA_MANAGER_HH_

#include <string>
#include <vector>

#include "keship/utils/WaterArea.hh"

namespace keship
{
  /// \brief 多片水域管理器
  ///
  /// 水域定义集合（纯数据，不依赖 Gazebo；JSON 反序列化见 WaterAreaSerializer）。
  /// 提供 XY 坐标查找。
  class WaterAreaManager
  {
    public: WaterAreaManager() = default;

    /// \brief 根据 XY 坐标查找所属水域
    /// \return 找到返回水域指针，否则返回 nullptr
    public: const WaterArea* Lookup(double _x, double _y) const;

    /// \brief 水域数量
    public: size_t Count() const { return areas_.size(); }

    /// \brief 获取所有水域（只读）
    public: const std::vector<WaterArea>& Areas() const { return areas_; }

    /// \brief 清空所有水区
    public: void Clear() { areas_.clear(); }

    /// \brief 添加一个水区
    public: void AddArea(const WaterArea &_a) { areas_.push_back(_a); }

    private:
      std::vector<WaterArea> areas_;
  };

} // namespace keship

#endif
