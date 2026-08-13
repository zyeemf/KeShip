/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Fossen 6-DOF 船舶水动力学模型 — 阻尼 only */
/// 2026.07.29: M_A 通过 DART <fluid_added_mass> 隐式积分，插件仅计算 −D(ν)·ν

#ifndef KESHIP_FOSSEN_MODEL_HH_
#define KESHIP_FOSSEN_MODEL_HH_

#include <Eigen/Dense>
#include <array>
#include <string>
#include <vector>

namespace keship
{

/// ── 惯性校验（通用工具，GUI 调用） ─────────────────
struct InertiaValidation
{
  bool valid = true;
  std::string message;
};

InertiaValidation ValidateInertia(double _mass,
    double _ixx, double _iyy, double _izz,
    double _ixy = 0, double _ixz = 0, double _iyz = 0);

/// ── 阻尼项通用化 ─────────────────────────────────────

struct DampingFactor
{
  int dof;
  int order;
};

struct DampingTerm
{
  int target;
  double coefficient;
  std::vector<DampingFactor> factors;
};

/// ── FossenModel ──────────────────────────────────────

class FossenModel
{
public:
  FossenModel();

  /// 设置阻尼项（通用，支持任意阶次和交叉耦合）
  void setDampingTerms(const std::vector<DampingTerm> &_terms);

  /// 设置线性阻尼系数 (deprecated，内部转为 8 个一阶对角 DampingTerm)
  /// 顺序: Xu, Yv, Zw, Kp, Mq, Nr, Yr, Nv
  void setDampingCoeffs(const std::array<double, 8> &_coeffs);

  /// 计算 −D(ν)·ν
  void computeDamping(const Eigen::Vector<double, 6> &_nu,
                       Eigen::Vector<double, 6> &_tau) const;

  bool valid() const { return !dampingTerms_.empty(); }

private:
  void computeDampingForces(const Eigen::Vector<double, 6> &_nu,
                            Eigen::Vector<double, 6> &_tau) const;

  std::array<double, 8>  dampingCoeffs_{};
  std::vector<DampingTerm> dampingTerms_;
};

} // namespace keship

#endif // KESHIP_FOSSEN_MODEL_HH_
