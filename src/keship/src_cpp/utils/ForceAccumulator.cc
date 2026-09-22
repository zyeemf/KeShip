/*
 * ForceAccumulator 实现（2026-09-22）
 * 见 ForceAccumulator.hh 的设计说明。
 */
#include "ForceAccumulator.hh"

namespace keship
{

/////////////////////////////////////////////////
ForceAccumulator::ForceAccumulator(
    const gz::math::Vector3d &_linkOriginWorldPos)
    : linkOrigin_(_linkOriginWorldPos)
{
}

/////////////////////////////////////////////////
void ForceAccumulator::Add(const ForceTerm &_term)
{
  /// ★★★ 全工程**唯一**一处「作用点 → 绕 link 原点的力矩」换算。★★★
  ///
  /// 参考点 this->linkOrigin_ 由构造函数给定，此处不再出现第二个参考点。
  /// 新增任何一种力（浮力 / 阻尼 / 推力 / 将来的流速·风速外力）都只需调用 Add()，
  /// 并**如实填写 applyPoint** —— 不可能再出现"按质心算力矩"这类参考点错配。
  ///
  /// 对于确实无力臂的量（Fossen 阻尼的力矩项、pose_based 阻尼的力矩项），
  /// 调用方把 applyPoint 填成 link 原点即可：叉乘恒为 0，力矩全部来自 pureTorque。
  const gz::math::Vector3d torque =
      _term.pureTorque
      + (_term.applyPoint - this->linkOrigin_).Cross(_term.force);

  this->totalForce_ += _term.force;
  this->totalTorque_ += torque;
  this->entries_.push_back({_term.name, _term.force, torque});
}

}  // namespace keship
