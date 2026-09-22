/*
 * ForceAccumulator —— 受力汇总器（2026-09-22）
 *
 * 目的：把「力的作用点 → 绕 link 原点的力矩」这一换算**收敛到全工程唯一一处**，
 *       使"力矩参考点写错"从"靠注释和纪律"变成**结构性不可能**。
 *
 * 背景：gz 的 ExternalWorldWrenchCmd 把力作用于 **link 原点**（不是质心）——
 *       2026-09-22 实测确证（7 组对照，与 Gazebo 官方文档逐字一致），
 *       见 doc/audit/force_ref_calibration_2026-09-22.md §5b。
 *       任何"想当然按质心算力矩"的写法都会凭空多出 (link原点 − CoM) × F 的寄生力矩：
 *       KSPlugin_Thrust 曾因此在推进器置于质心时仍产生 α = 0.009991 rad/s² 的伪俯仰
 *       （已修，commit 756acd0）。
 *
 * 本类不做任何物理假设，只做一件事：
 *       τ_i = pureTorque_i + (applyPoint_i − link原点) × force_i
 *   参考点 link原点 由构造函数给定，此后不再出现第二个参考点。
 */
#ifndef KESHIP_UTILS_FORCEACCUMULATOR_HH_
#define KESHIP_UTILS_FORCEACCUMULATOR_HH_

#include <string>
#include <vector>

#include <gz/math/Vector3.hh>

namespace keship
{

/// 单个受力项。
///
/// ★ `applyPoint` 是**必填**（聚合初始化时无法省略）—— 这是"禁止隐含作用点"的
///   结构性约束：想加一项力，就必须交代它作用在哪里。
///   对于**确实无力臂**的量（如 Fossen 阻尼的力矩项、纯力矩），
///   把 applyPoint 填成 link 原点即可（叉乘自然为 0），并在注释里写明理由。
struct ForceTerm
{
  std::string name;                         ///< 名称（诊断用；建议带来源，如 "thrust/左桨"）
  gz::math::Vector3d force;                 ///< 世界系力
  gz::math::Vector3d applyPoint;            ///< 世界系**作用点**（力作用在哪里）
  gz::math::Vector3d pureTorque{0, 0, 0};   ///< 世界系纯力矩（无力臂项，如阻尼力矩）
};

/// 受力汇总器：累加力，并按**唯一参考点**（link 原点）折算力矩。
///
/// 用法：
/// @code
///   ForceAccumulator acc(linkWorldPos);                    // 参考点在此确定，全类唯一
///   acc.Add({"buoyancy", F_b, cobWorld});                  // 力 @ 浮心
///   acc.Add({"damping",  F_d, linkWorldPos, tauDamping});  // 无力臂项：作用点填 link 原点
///   acc.Add({"thrust/左", F_t, thrusterWorldPos});         // 力 @ 推进器
///   // 本类不依赖 gz-sim；由调用方把结果写入 ExternalWorldWrenchCmd：
///   ApplyForces(_ecm, acc.TotalForce(), acc.TotalTorque());
/// @endcode
class ForceAccumulator
{
  /// @param[in] _linkOriginWorldPos link 原点的世界坐标 —— **力矩参考点（全工程唯一）**
  public: explicit ForceAccumulator(const gz::math::Vector3d &_linkOriginWorldPos);

  /// \brief 加入一个受力项：累加力，并按作用点折算力矩。
  /// \param[in] _term 受力项（applyPoint 必填）
  public: void Add(const ForceTerm &_term);

  /// \brief 合计力（世界系）
  public: gz::math::Vector3d TotalForce() const { return this->totalForce_; }

  /// \brief 合计力矩（世界系，**绕 link 原点**）
  public: gz::math::Vector3d TotalTorque() const { return this->totalTorque_; }

  /// \brief 分项记录（诊断用）
  public: struct Entry
  {
    std::string name;           ///< 项名
    gz::math::Vector3d force;   ///< 该项的力
    gz::math::Vector3d torque;  ///< 该项贡献的力矩（绕 link 原点）
  };

  /// \brief 各分项（供日志核对"每一项贡献了多少力矩"）
  public: const std::vector<Entry> &Entries() const { return this->entries_; }

  /// \brief 力矩参考点（诊断用）
  public: gz::math::Vector3d ReferencePoint() const { return this->linkOrigin_; }

  private: gz::math::Vector3d linkOrigin_;
  private: gz::math::Vector3d totalForce_{0, 0, 0};
  private: gz::math::Vector3d totalTorque_{0, 0, 0};
  private: std::vector<Entry> entries_;
};

}  // namespace keship

#endif  // KESHIP_UTILS_FORCEACCUMULATOR_HH_
