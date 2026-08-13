/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 四面体类实现
 * ZhangYu 创建 2026.5.5
 */

#include "Tetrahedron.hh"

namespace keship
{
  //-------------------------------------------------------------------------  
  Tetrahedron::Tetrahedron(const gz::math::Vector3d &_v0, const gz::math::Vector3d &_v1,
                          const gz::math::Vector3d &_v2, const gz::math::Vector3d &_v3)
  {
    this->vertices[0] = _v0;
    this->vertices[1] = _v1;
    this->vertices[2] = _v2;
    this->vertices[3] = _v3;
  }

  //-------------------------------------------------------------------------  
  double Tetrahedron::Volume() const
  {
    // 计算向量
    gz::math::Vector3d v1 = this->vertices[1] - this->vertices[0];
    gz::math::Vector3d v2 = this->vertices[2] - this->vertices[0];
    gz::math::Vector3d v3 = this->vertices[3] - this->vertices[0];
    
    // 计算标量三重积的绝对值，再除以6
    double volume = std::abs(v1.Dot(v2.Cross(v3))) / 6.0;
    return volume;
  }

  //-------------------------------------------------------------------------  
  std::array<gz::math::Vector3d, 4> Tetrahedron::Vertices() const
  {
    return this->vertices;
  }

} // namespace keship