/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 海况等级（WMO/Douglas）与风级（Beaufort）换算模块 —— 纯数学，无外部依赖 */

#ifndef KESHIP_SEA_STATE_HH_
#define KESHIP_SEA_STATE_HH_

#include <array>
#include <cmath>
#include <string>

namespace keship
{

  /// \brief 单级海况/风级定义
  struct SeaStateBand
  {
    int level;             ///< 等级（海况 0~9 / 风级 0~12）
    std::string nameZh;    ///< 中文名称
    double lo;             ///< 下限（含，单位 m 或 m/s）
    double hi;             ///< 上限（不含，单位 m 或 m/s）
    double mid;            ///< 代表值（区间中值，9 级/12 级取略高于下限的代表值）

    /// \brief 是否包含值 _v（[lo, hi)）
    bool Contains(double _v) const { return _v >= lo && _v < hi; }
  };

  /// \brief WMO/Douglas 海况等级表（0~9 级，最常用标准），波段单位：有效波高 Hs [m]
  /// 0 级仅 Hs=0；1 级 (0, 0.1)；8 级含 14.0；9 级 >14
  inline const std::array<SeaStateBand, 10> kSeaStateTable = {{
      {0, "无浪",  0.0,   0.0,   0.0},
      {1, "微浪",  0.0,   0.1,   0.05},
      {2, "小浪",  0.1,   0.5,   0.3},
      {3, "轻浪",  0.5,   1.25,   0.875},
      {4, "中浪",  1.25,  2.5,    1.875},
      {5, "大浪",  2.5,   4.0,    3.25},
      {6, "巨浪",  4.0,   6.0,    5.0},
      {7, "狂浪",  6.0,   9.0,    7.5},
      {8, "狂涛",  9.0,   14.0000001, 11.5},
      {9, "怒涛",  14.0000001,  1e6,    14.5},
  }};

  /// \brief Beaufort 风级表（0~12 级，WMO），波段单位：海面 10m 风速 [m/s]
  inline const std::array<SeaStateBand, 13> kBeaufortTable = {{
      {0, "静风",    0.0,   0.3,    0.0},
      {1, "软风",    0.3,   1.6,    0.9},
      {2, "轻风",    1.6,   3.4,    2.5},
      {3, "微风",    3.4,   5.5,    4.4},
      {4, "和风",    5.5,   8.0,    6.7},
      {5, "清劲风",  8.0,   10.8,   9.4},
      {6, "强风",    10.8,  13.9,   12.3},
      {7, "疾风",    13.9,  17.2,   15.5},
      {8, "大风",    17.2,  20.8,   19.0},
      {9, "烈风",    20.8,  24.5,   22.6},
      {10, "狂风",   24.5,  28.5,   26.5},
      {11, "暴风",   28.5,  32.7,   30.6},
      {12, "飓风",   32.7,  1e6,    36.0},
  }};

  namespace SeaState
  {
    constexpr double kGravity = 9.81;   ///< m/s^2
    /// \brief PMS 谱自洽系数：Hs = 0.04 · gain · Tp^2（充分成长 Pierson–Moskowitz）
    constexpr double kHsPeriodFactor = 0.04;
    /// \brief 充分成长风浪经验：Hs = 0.243 · U^2 / g（Sverdrup–Munk）
    constexpr double kHsWindFactor = 0.243;
    /// \brief 谱峰周期下限 [s]（低海况下 PM 公式给出过短周期，
    /// 波长小于水面网格步长会导致渲染"一格一格"；下限 2.5s 对应
    /// 波长 ≈9.8m，与水面网格目标步长匹配）
    constexpr double kMinTp = 2.5;

    /// \brief 由 PMS 参数求有效波高 Hs [m]
    inline double HsFromParams(double _gain, double _period)
    {
      const double t = _period < 0.0 ? 0.0 : _period;
      return kHsPeriodFactor * _gain * t * t;
    }

    /// \brief 由有效波高 Hs 求谱峰周期 Tp [s]（PM 充分成长，自洽，且受 kMinTp 下限约束）
    inline double TpFromHs(double _hs)
    {
      if (_hs <= 0.0) return 5.0;
      return std::max(kMinTp, std::sqrt(_hs / kHsPeriodFactor));
    }

    /// \brief 由有效波高 Hs 求风速 U [m/s]（充分成长风浪经验）
    inline double WindFromHs(double _hs)
    {
      if (_hs <= 0.0) return 0.0;
      return std::sqrt(_hs * kGravity / kHsWindFactor);
    }

    /// \brief 查海况等级（0~9），由有效波高 Hs；0 级仅 Hs=0，其余从 1 级起
    inline int LevelFromHs(double _hs)
    {
      if (_hs <= 0.0) return 0;
      for (const auto &b : kSeaStateTable)
        if (b.Contains(_hs)) return b.level;
      return 9;
    }

    /// \brief 查风级（0~12）由风速 v
    inline int BeaufortFromSpeed(double _v)
    {
      if (_v <= 0.0) return 0;
      for (const auto &b : kBeaufortTable)
        if (b.Contains(_v)) return b.level;
      return 12;
    }

    /// \brief 由等级取代表值 Hs / 风速
    inline double MidHs(int _level) { return kSeaStateTable[_level < 0 ? 0 : _level > 9 ? 9 : _level].mid; }
    inline double MidSpeed(int _level) { return kBeaufortTable[_level < 0 ? 0 : _level > 12 ? 12 : _level].mid; }

    /// \brief 由海况等级自动生成风浪参数（Hs 中值 → Tp、U、波高增益）
    /// 增益按实际周期（含 kMinTp 截断）反算：gain = Hs/(0.04·Tp²)，
    /// 保证 Hs 恒等于该级中值，从而"选择的等级 = 反推的等级"恒定一致；
    /// 无截断时增益数学上恰为 1.0（原语义不变）。0 级（及负数）设 0
    inline void ParamsFromLevel(int _seaState, double *_tp, double *_wind,
                                double *_gain)
    {
      const double hs = MidHs(_seaState);
      const double tp = TpFromHs(hs);
      if (_tp)   *_tp   = tp;
      if (_wind) *_wind = WindFromHs(hs);
      if (_gain) *_gain = (_seaState <= 0)
          ? 0.0 : hs / (kHsPeriodFactor * tp * tp);
    }

    /// \brief 由海况等级自动生成风浪参数（Hs 中值 → Tp、U）
    inline void ParamsFromLevel(int _seaState, double *_tp, double *_wind)
    {
      ParamsFromLevel(_seaState, _tp, _wind, nullptr);
    }
  } // namespace SeaState

} // namespace keship

#endif