/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 四面体类
 * ZhangYu 创建 2026.5.5
 */

#ifndef TETRAHEDRON_HH
#define TETRAHEDRON_HH

//-------------------------------------------------------------------------
#include <array>

#include <gz/math/Vector3.hh>

//-------------------------------------------------------------------------
namespace keship
{
  /**
   * @brief 四面体类，用于计算体积和排水量
   */
  class Tetrahedron
  {
  public:
    /**
     * @brief 构造函数
     * @param _v0 第一个顶点
     * @param _v1 第二个顶点
     * @param _v2 第三个顶点
     * @param _v3 第四个顶点
     */
    Tetrahedron(const gz::math::Vector3d &_v0, const gz::math::Vector3d &_v1,
                const gz::math::Vector3d &_v2, const gz::math::Vector3d &_v3);
    
    /**
     * @brief 计算四面体体积
     * @return 四面体体积
     */
    double Volume() const;
    
    /**
     * @brief 获取四面体的四个顶点
     * @return 顶点数组
     */
    std::array<gz::math::Vector3d, 4> Vertices() const;
    
  private:
    /// 四面体的四个顶点
    std::array<gz::math::Vector3d, 4> vertices;
  };

} // namespace keship


#endif