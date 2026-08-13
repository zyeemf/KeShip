/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Fossen 6-DOF 船舶水动力学模型 — 阻尼 only 实现 */
/// 2026.07.29: M_A 通过 DART <fluid_added_mass> 隐式积分，插件仅计算 −D(ν)·ν

#include "FossenModel.hh"
#include <cmath>
#include <sstream>

namespace keship
{

// ─── 惯性校验 ─────────────────────────────────────────

InertiaValidation ValidateInertia(
    double _mass, double _ixx, double _iyy, double _izz,
    double _ixy, double _ixz, double _iyz)
{
  if (_mass <= 0.0)
    return {false, "质量必须大于 0"};

  if (_ixx <= 0.0 || _iyy <= 0.0 || _izz <= 0.0)
    return {false, "Ixx、Iyy、Izz 必须大于 0"};

  if (_ixx + _iyy < _izz)
  {
    std::ostringstream oss;
    oss << "iXX (" << _ixx << ") + iYY (" << _iyy << ") = "
        << (_ixx + _iyy) << " < iZZ (" << _izz << ")，违反惯性三角不等式";
    return {false, oss.str()};
  }
  if (_ixx + _izz < _iyy)
  {
    std::ostringstream oss;
    oss << "iXX (" << _ixx << ") + iZZ (" << _izz << ") = "
        << (_ixx + _izz) << " < iYY (" << _iyy << ")，违反惯性三角不等式";
    return {false, oss.str()};
  }
  if (_iyy + _izz < _ixx)
  {
    std::ostringstream oss;
    oss << "iYY (" << _iyy << ") + iZZ (" << _izz << ") = "
        << (_iyy + _izz) << " < iXX (" << _ixx << ")，违反惯性三角不等式";
    return {false, oss.str()};
  }

  return {true, ""};
}

// ─── FossenModel ──────────────────────────────────────

FossenModel::FossenModel()
{
  dampingCoeffs_.fill(0.0);
}

void FossenModel::setDampingTerms(const std::vector<DampingTerm> &_terms)
{
  dampingTerms_ = _terms;
}

void FossenModel::setDampingCoeffs(const std::array<double, 8> &_coeffs)
{
  dampingCoeffs_ = _coeffs;

  dampingTerms_.clear();
  dampingTerms_.reserve(8);

  auto add = [&](int target, int dof, int order, double coeff) {
    if (std::abs(coeff) > 0.0)
      dampingTerms_.push_back({target, coeff, {{dof, order}}});
  };

  add(0, 0, 1, _coeffs[0]);   // Xu
  add(1, 1, 1, _coeffs[1]);   // Yv
  add(2, 2, 1, _coeffs[2]);   // Zw
  add(3, 3, 1, _coeffs[3]);   // Kp
  add(4, 4, 1, _coeffs[4]);   // Mq
  add(5, 5, 1, _coeffs[5]);   // Nr
  add(1, 5, 1, _coeffs[6]);   // Yr
  add(5, 1, 1, _coeffs[7]);   // Nv
}



void FossenModel::computeDampingForces(
    const Eigen::Vector<double, 6> &_nu,
    Eigen::Vector<double, 6> &_tau) const
{
  for (const auto &term : dampingTerms_)
  {
    double val = term.coefficient;
    for (const auto &f : term.factors)
    {
      double absv = std::abs(_nu[f.dof]);
      double contrib = (f.order == 0) ? absv : _nu[f.dof];
      for (int i = 1; i < f.order; ++i)
        contrib *= absv;
      val *= contrib;
    }
    _tau(term.target) -= val;
  }
}

void FossenModel::computeDamping(
    const Eigen::Vector<double, 6> &_nu,
    Eigen::Vector<double, 6> &_tau) const
{
  computeDampingForces(_nu, _tau);
}

} // namespace keship
