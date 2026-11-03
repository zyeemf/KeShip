/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-01~SC-12 控制设计链（L6）+ 感知→控制链（L7）—— **纯算法层**
 *
 * 为什么要单独一层（2026-10-27）：
 *   链判据要求「控制律的**方向与限幅**必须能被机器复核」——这类错误最典型
 *   （桨推反了、钳位没生效、LOS 符号反了）在仿真里往往只是"船跑歪了"，
 *   肉眼看不出是哪一条写错。故把**不含状态、不含 gz、不含 Qt** 的算式全部下沉到
 *   本头文件，供三处共用**同一份实现**：
 *     ① `ControlExpCore`（GUI 与无头共用，发推力/采样/算指标）
 *     ② `test/control_core_test.cc`（不连仿真即可断言符号与钳位）
 *     ③ 以后的无头入口
 *   与 `TurningExperimentCore` 把 `computeMetrics` 做成 static 纯函数是同一思路。
 *
 * ★★ 平台约定（**推导过程，别改**）：两桨**只有纵向推力**，平台不含舵。
 *   `KSPlugin_Thrust.cc:184` —— 推力方向 = body local +X 的反方向（`force = dir*(-mag)`），
 *   即 **正推力沿模型 local −X**，故模型艏向对应 yaw + 180°（部分模型正是如此）。
 *   右桨（舷侧在 +Y，因 starboard = forward × up 且 forward = −X）增推 ⇒ 绕 +Z 力矩为正
 *   ⇒ **yaw 增大** ⇒ 艏向向**左（港侧）**偏。
 *   ⇒ 结论（全文件统一）：
 *        Δt ≡ thrustR − thrustL，**Δt > 0 ⇒ yaw 增大 ⇒ 左转**；
 *        反之「右转 / starboard」= **yaw 偏置取负**。
 *   这与题目给的控制律 `L = base − Δt, R = base + Δt, Δt = kp·e`（e = ψ_d − ψ）自洽：
 *   e > 0（需增大 yaw）⇒ Δt > 0 ⇒ R > L ⇒ yaw 增大 ⇒ e 收敛。✔
 *
 * ★ 艏向 vs 航迹向（不能混）：几何类控制律（LOS / DP / COLREGs）算出来的是**航迹向**
 *   （世界系角度），而反馈量是模型 **yaw**；两者可能相差 180°（见上）。故控制核心在起航后
 *   **标定一次** `yawCourseOffset = 航迹向 − yaw`（量化到 90° 的整数倍），再换算下发。
 *   纯算法层只接收已换算好的 yaw 目标，不做隐式假设。
 *
 * ★ `targetHeadingDeg` 的双重口径（**已在两处注释，用错会"看起来对其实不对"**）：
 *   · kPidHeading：按**模型 yaw** 解释（与指标同口径，SC-04 直接可测）；
 *   · kColregs   ：按**航迹向**解释（几何决策基准），核心内部换算成 yaw 后下发。
 */

#ifndef KESHIP_CONTROL_LAWS_HH_
#define KESHIP_CONTROL_LAWS_HH_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace keship {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;

/// 航路点默认到达半径 (m)。SC-06/SC-10 用；`ControlParams` 里没有该字段（题目固定了结构体），
/// 故以常量给出并在指标里回填到 `MetricContext::arrivalRadiusM`。
constexpr double kDefaultArrivalRadiusM = 2.0;

/// 控制律种类
enum class ControlLaw {
  kOpenLoop,    ///< 开环：固定推力，不反馈（SC-01 开环组）
  kPidHeading,  ///< 航向 PID → 差动（SC-01 闭环组 / SC-02 / SC-03 / SC-04 / SC-09）
  kPidSpeed,    ///< 航速 PID → 共模（SC-05）
  kLos,         ///< LOS 循迹（SC-06 / SC-10）
  kDp,          ///< 动力定位：位置→期望航向+期望速度（SC-07）
  kColregs,     ///< 简化 COLREGs 单障碍避碰（SC-11）
  kFormation    ///< 简化编队：虚拟领航者 + 固定偏移，按 DP 律跟随（SC-12）
};

inline const char *ControlLawKey(ControlLaw _l) {
  switch (_l) {
    case ControlLaw::kOpenLoop:   return "open_loop";
    case ControlLaw::kPidHeading: return "pid_heading";
    case ControlLaw::kPidSpeed:   return "pid_speed";
    case ControlLaw::kLos:        return "los";
    case ControlLaw::kDp:         return "dp";
    case ControlLaw::kColregs:    return "colregs";
    case ControlLaw::kFormation:  return "formation";
  }
  return "pid_heading";
}

inline const char *ControlLawName(ControlLaw _l) {
  switch (_l) {
    case ControlLaw::kOpenLoop:   return "开环（固定推力）";
    case ControlLaw::kPidHeading: return "航向 PID（差动）";
    case ControlLaw::kPidSpeed:   return "航速 PID（共模）";
    case ControlLaw::kLos:        return "LOS 循迹";
    case ControlLaw::kDp:         return "动力定位 DP";
    case ControlLaw::kColregs:    return "COLREGs 避碰";
    case ControlLaw::kFormation:  return "编队保持";
  }
  return "航向 PID";
}

inline bool ParseControlLaw(const std::string &_s, ControlLaw &_out) {
  if (_s == "open_loop")   { _out = ControlLaw::kOpenLoop;   return true; }
  if (_s == "pid_heading") { _out = ControlLaw::kPidHeading; return true; }
  if (_s == "pid_speed")   { _out = ControlLaw::kPidSpeed;   return true; }
  if (_s == "los")         { _out = ControlLaw::kLos;        return true; }
  if (_s == "dp")          { _out = ControlLaw::kDp;         return true; }
  if (_s == "colregs")     { _out = ControlLaw::kColregs;    return true; }
  if (_s == "formation")   { _out = ControlLaw::kFormation;  return true; }
  return false;
}

/// 控制参数（字段由接口约定固定；新增字段只加在末尾并保持默认值可用）
struct ControlParams {
  ControlLaw law = ControlLaw::kPidHeading;
  double kp = 1.0, ki = 0.0, kd = 0.0;
  double baseThrustN = 100.0, diffLimitN = 80.0;  ///< 共模基准推力 / 差动（及共模）限幅 (N)
  double targetHeadingDeg = 30.0;                 ///< 见文件头「双重口径」
  double targetSpeedMs = 1.0;
  double targetXM = 0.0, targetYM = 0.0;
  double lookaheadM = 2.0;                        ///< LOS 前视距离 Δ (m)
  bool   disturbance = false;
  double windMs = 0.0, currentMs = 0.0, currentDirDeg = 0.0;
  bool   useEstimatedPose = false;                ///< L7：航向反馈改用 SC-08 估计值
  double obstacleXM = 0.0, obstacleYM = 0.0, obstacleRadiusM = 1.5;   ///< SC-11
  double formationOffsetXM = -3.0, formationOffsetYM = 0.0;           ///< SC-12
  double simSeconds = 60.0;
  /// 扩展（有默认值，旧调用不受影响）
  double posGainPerS = 0.40;   ///< DP 位置环增益 (1/s)：v = min(vMax, k·|e|)
  double arriveRadiusM = kDefaultArrivalRadiusM;  ///< 航路点到达半径 (m)
  /// kOpenLoop 的**固定差动量** Δt = R−L (N)：0 = 两桨等推力直航（SC-01 开环组）；
  /// >0 = 固定差动（SC-09 自整定的阶跃识别用，仍然**不含任何反馈**）。
  double openLoopDiffN = 0.0;
};

/// 采样点（每帧一行；`t` 为相对开始的仿真时间）
struct ControlSample {
  double t = 0.0;
  double x = 0.0, y = 0.0;
  double yawDeg = 0.0;
  double speedMs = 0.0;
  double lateralErrM = 0.0;   ///< 见各控制律：LOS=横向偏差（正=偏港侧）；DP/编队=到目标点距离
  double headingErrDeg = 0.0; ///< ψ_d(yaw 系) − yaw
  double thrustL = 0.0, thrustR = 0.0;
};

/// 控制指标（-1 = 无定义/数据不足）
struct ControlMetrics {
  double headingRmseDeg = -1;
  double headingSteadyErrDeg = -1;
  double settlingTimeS = -1;
  double overshootPct = -1;
  double speedSteadyErrPct = -1;
  double lateralRmsM = -1;
  double maxLateralM = -1;
  double dpHoldErrM = -1;
  double arrivalRate = -1;
  double meanThrustN = -1;
  double minDcpaM = -1;
  double formationErrM = -1;
  bool valid = false;
};

/// 指标计算所需的上下文（纯数据，便于独立复算）
struct MetricContext {
  ControlLaw law = ControlLaw::kPidHeading;
  double targetHeadingDeg = 0.0;
  double targetSpeedMs = 0.0;
  double targetXM = 0.0, targetYM = 0.0;
  double startXM = 0.0, startYM = 0.0, startYawDeg = 0.0;
  bool   hasObstacle = false;
  double obstacleXM = 0.0, obstacleYM = 0.0, obstacleRadiusM = 1.5;
  bool   formation = false;
  /// 编队目标点（世界系，start 时一次算出；见 ControlExpCore::start）
  double formationTargetXM = 0.0, formationTargetYM = 0.0;
  int    waypointsTotal = 0, waypointsReached = 0;
  double arrivalRadiusM = kDefaultArrivalRadiusM;
};

// ───────────────────────────── 通用数学 ─────────────────────────────
inline double Clamp(double _v, double _lo, double _hi) {
  return _v < _lo ? _lo : (_v > _hi ? _hi : _v);
}

/// 归一化到 (−180, 180]
inline double WrapDeg180(double _d) {
  if (!std::isfinite(_d)) return 0.0;
  _d = std::fmod(_d, 360.0);
  if (_d > 180.0) _d -= 360.0;
  if (_d <= -180.0) _d += 360.0;
  return _d;
}

// ─────────────────────── ① 差动推力（航向环） ───────────────────────
/// 差动/共模限幅结算
struct DiffThrust {
  double diff = 0.0;    ///< Δt（**钳位后**，N）—— 「一侧比另一侧多多少」
  double diffRaw = 0.0; ///< Δt（钳位前，N）—— 便于断言「钳位真的生效」
  double left = 0.0;    ///< thrustL = base − Δt
  double right = 0.0;   ///< thrustR = base + Δt
  bool   saturated = false;
};

/// 积分饱和钳位：保证 |ki·∫e| ≤ diffLimit，即 |∫e| ≤ diffLimit/|ki|。
/// ki = 0 时积分项不参与（返回 0，防无意义的累积漂移）。
inline double ClampIntegral(double _integral, double _ki, double _diffLimitN) {
  const double lim = std::max(0.0, _diffLimitN);
  if (std::abs(_ki) < 1e-9) return 0.0;
  const double iMax = lim / std::abs(_ki);
  if (!std::isfinite(iMax)) return 0.0;
  return Clamp(_integral, -iMax, iMax);
}

/// 航向 PID → 差动推力。`_errDeg = ψ_d − ψ`（正 ⇒ 需增大 yaw ⇒ 左转 ⇒ Δt > 0）
inline DiffThrust ComputeDiffThrust(double _errDeg, double _integral,
                                    double _derivDegPerS,
                                    const ControlParams &_p) {
  DiffThrust out;
  const double raw = _p.kp * _errDeg + _p.ki * _integral + _p.kd * _derivDegPerS;
  out.diffRaw = raw;
  const double lim = std::max(0.0, _p.diffLimitN);
  out.diff = Clamp(raw, -lim, lim);
  out.saturated = std::abs(raw - out.diff) > 1e-12;
  out.left = _p.baseThrustN - out.diff;
  out.right = _p.baseThrustN + out.diff;
  return out;
}

/// 航速 PID → **共模**增量（两桨同加同减）。
/// `_errMs = v_d − v`（正 ⇒ 加速）。与差动环共用同一限幅参数，便于对照。
inline double ComputeCommonThrust(double _errMs, double _integral,
                                  double _derivMsPerS, const ControlParams &_p) {
  const double raw = _p.kp * _errMs + _p.ki * _integral + _p.kd * _derivMsPerS;
  return Clamp(raw, -std::max(0.0, _p.diffLimitN), std::max(0.0, _p.diffLimitN));
}

// ─────────────────────────── ② LOS 循迹 ───────────────────────────
/// 航段 a→b 的**有符号横向偏差**：**正 = 船在航段左侧（港侧）**。
///   e = c·dy − s·dx，其中 (c,s) 为 a→b 单位向量，(dx,dy) = p − a。
///   推导：路径沿 +X，(c,s)=(1,0)，船在 (0,+5)（港侧）⇒ e = +5 ✔
inline double CrossTrackErrorM(double _px, double _py,
                               double _ax, double _ay,
                               double _bx, double _by) {
  const double dx0 = _bx - _ax, dy0 = _by - _ay;
  const double len = std::hypot(dx0, dy0);
  if (len < 1e-9) return std::hypot(_px - _ax, _py - _ay);
  const double c = dx0 / len, s = dy0 / len;
  const double dx = _px - _ax, dy = _py - _ay;
  return c * dy - s * dx;
}

/// LOS 期望**航向（航迹向口径）**：ψ_d = ψ_path + atan(−e/Δ)。
///   e > 0（船在港侧）⇒ 偏置为负 ⇒ 右转回来 ✔
inline double LosDesiredHeading(double _pathHeadingDeg, double _lateralErrM,
                                double _lookaheadM) {
  const double delta = (_lookaheadM > 1e-3) ? _lookaheadM : 1e-3;
  return _pathHeadingDeg + std::atan(-_lateralErrM / delta) * kRad2Deg;
}

// ───────────────────────── ③ DP 极坐标分解 ─────────────────────────
struct PolarCommand {
  double courseDeg = 0.0;  ///< 期望航迹向（世界系）
  double speedMs = 0.0;    ///< 期望航速
};

/// 位置误差 → 期望航迹向 + 期望航速。
///   course = atan2(ey, ex)；speed = min(vMax, k·|e|)；|e| < 0.5 m 视为到位（speed=0）。
inline PolarCommand PolarDpCommand(double _exM, double _eyM, double _targetSpeedMs,
                                   double _posGainPerS = 0.40,
                                   double _stopRadiusM = 0.5) {
  PolarCommand c;
  const double dist = std::hypot(_exM, _eyM);
  c.courseDeg = (dist > 1e-6) ? std::atan2(_eyM, _exM) * kRad2Deg : 0.0;
  if (dist <= _stopRadiusM) {
    c.speedMs = 0.0;
    return c;
  }
  const double k = (_posGainPerS > 1e-3) ? _posGainPerS : 0.40;
  c.speedMs = std::min(std::max(0.0, _targetSpeedMs), k * dist);
  return c;
}

// ───────────────────────────── ④ DCPA ─────────────────────────────
/// 两船（或船-障碍）**匀速直线**最近会遇距离。
///   p = b − a，v = v_b − v_a；t_cpa = −(p·v)/|v|²（<0 取 0）；DCPA = |p + v·t_cpa|。
/// 障碍视为静止（v_b = 0）时即为「本船当前速度外推的最近距离」。
inline double Dcpa(double _ax, double _ay, double _avx, double _avy,
                   double _bx, double _by, double _bvx, double _bvy,
                   double *_tcpaSec = nullptr) {
  const double px = _bx - _ax, py = _by - _ay;
  const double vx = _bvx - _avx, vy = _bvy - _avy;
  const double vv = vx * vx + vy * vy;
  double t = 0.0;
  if (vv > 1e-12) t = -(px * vx + py * vy) / vv;
  if (t < 0.0) t = 0.0;
  if (_tcpaSec) *_tcpaSec = t;
  return std::hypot(px + vx * t, py + vy * t);
}

// ─────────────────────── ⑤ 简化 COLREGs 决策 ───────────────────────
/// 单障碍、静态、无会遇船舶 —— **教学级简化**（边界见报告 §5）。
/// 输入：`_relBearingDeg` = 障碍方位角相对**本船航迹向**（右正左负），`_rangeM` 距离。
/// 输出：航向偏置（°）。**负 = 右转（starboard）**，与文件头的 yaw 约定一致。
///   对遇（|rel| ≤ 15°）           → 右转 20°（幅度按距离紧迫度折算）
///   交叉（15° < rel ≤ 112.5°）     → 我船为让路船，右转 25°
///   交叉（−112.5° ≤ rel < −15°）   → 我船为直航船，小幅右转 8°（不左转横越对方船首）
///   追越/其余（|rel| > 112.5°）    → 保持 0°（本教学版不建模追越让路）
inline double ColregsHeadingBiasDeg(double _relBearingDeg, double _rangeM,
                                    double _obstacleRadiusM,
                                    double _detectRangeM = 60.0) {
  if (!std::isfinite(_rangeM) || _rangeM > _detectRangeM) return 0.0;
  if (_rangeM <= std::max(0.0, _obstacleRadiusM)) return 0.0;  // 已进入障碍内：无意义
  const double rel = WrapDeg180(_relBearingDeg);
  const double arm = std::abs(rel);
  /// 紧迫度：越近转得越多（0→1，最近仍保留 30% 幅度，避免临界抖动）
  const double urgency = Clamp(0.3 + 0.7 * (1.0 - _rangeM / _detectRangeM), 0.3, 1.0);
  double bias = 0.0;
  if (arm <= 15.0) {
    bias = -20.0;                       // 对遇：右转
  } else if (rel > 15.0 && rel <= 112.5) {
    bias = -25.0;                       // 交叉（对方在我右舷）：让路，右转
  } else if (rel < -15.0 && rel >= -112.5) {
    bias = -8.0;                        // 交叉（对方在我左舷）：直航船，小幅右转
  } else {
    bias = 0.0;                         // 追越
  }
  return bias * urgency;
}

// ──────────────────────────── ⑥ 指标 ────────────────────────────
/// 从轨迹 + 上下文算指标（**纯函数**：GUI / 无头 / 单测同一份）。
/// 口径：
///   headingRmseDeg     = sqrt(mean(headingErr²))（全段，不扣均值）
///   headingSteadyErr   = 末 20% 的 mean(|headingErr|)
///   settlingTimeS      = 「最后一次出带」之后一点的 t；带 = max(2%·|目标航向|, 0.5°)
///   overshootPct       = (越过目标的峰值) / |目标−初始| × 100；跨度 < kMinOvershootSpanDeg 时为 -1（N/A）
///                        **仅 kPidHeading 计算**：其余律的目标随时间切换，分子分母不在同一基准，恒 -1
///   lateralRmsM        = sqrt(mean(lateral²))；maxLateralM = max|lateral|
///   dpHoldErrM         = 末 20% 到目标点（DP/编队目标）的平均距离
///   arrivalRate        = 已到达航路点 / 总航路点（无航路点 = −1）
///   minDcpaM           = 逐帧按当时速度外推的 DCPA 最小值（障碍静止）
///   formationErrM      = 末 20% 到编队目标点的平均距离
/// 超调百分比的最小可分辨跨度（度）。
/// 低于它时「越过量/跨度」会病态放大：船本来就在目标航向上，1° 的抖动就能算出 1000%+。
/// 这不是超调，是分母趋零。故一律判 N/A（-1），界面显示「—」。
constexpr double kMinOvershootSpanDeg = 2.0;

inline ControlMetrics ComputeMetrics(const std::vector<ControlSample> &_trail,
                                      const MetricContext &_ctx) {
  ControlMetrics m;
  const size_t n = _trail.size();
  if (n < 3) return m;

  const size_t win = std::max<size_t>(1, n / 5);
  const size_t winFrom = n - win;

  double sumH2 = 0.0, sumLat2 = 0.0, sumThr = 0.0;
  double maxLat = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const auto &s = _trail[i];
    sumH2 += s.headingErrDeg * s.headingErrDeg;
    sumLat2 += s.lateralErrM * s.lateralErrM;
    maxLat = std::max(maxLat, std::abs(s.lateralErrM));
    sumThr += 0.5 * (s.thrustL + s.thrustR);
  }
  m.headingRmseDeg = std::sqrt(sumH2 / static_cast<double>(n));
  m.lateralRmsM = std::sqrt(sumLat2 / static_cast<double>(n));
  m.maxLateralM = maxLat;
  m.meanThrustN = sumThr / static_cast<double>(n);

  double sumH = 0.0, sumV = 0.0;
  for (size_t i = winFrom; i < n; ++i) {
    sumH += std::abs(_trail[i].headingErrDeg);
    sumV += _trail[i].speedMs;
  }
  m.headingSteadyErrDeg = sumH / static_cast<double>(win);

  // 航速稳态误差（相对误差，%）
  if (_ctx.targetSpeedMs > 0.05) {
    const double vMean = sumV / static_cast<double>(win);
    m.speedSteadyErrPct = (vMean - _ctx.targetSpeedMs) / _ctx.targetSpeedMs * 100.0;
  }

  // 调节时间（±2% 带）
  {
    const double band = std::max(0.02 * std::abs(_ctx.targetHeadingDeg), 0.5);
    size_t lastOut = n;  // n = 从未出带
    for (size_t i = 0; i < n; ++i)
      if (std::abs(_trail[i].headingErrDeg) > band) lastOut = i;
    if (lastOut == n) m.settlingTimeS = 0.0;                  // 一开始就在带内
    else if (lastOut + 1 < n) m.settlingTimeS = _trail[lastOut + 1].t;
    else m.settlingTimeS = -1.0;                              // 结束仍在带外
  }

  // 超调
  {
    // ★ 2026-11-03（D11）：口径收窄到「固定航向阶跃」这一类。
    //   只有 kPidHeading（SC-02 / SC-03 / SC-04）是「目标航向固定、从起航航向跃变过去」，
    //   此时 (越过峰值 / 跨度) 才有物理意义。其余律：
    //     · kLos      SC-06 / SC-10 —— 目标是路径，航路段随时切换；
    //     · kColregs  SC-11        —— 期望航向随障碍相对位置变；
    //     · kFormation SC-12       —— 跟随虚拟领航者；
    //     · kDp       SC-07        —— 期望航向由位置误差反算；
    //     · kPidSpeed SC-05        —— 目标是航速，航向本就不参与。
    //   这些模式下 tgt 每段变化，而 y0 取首帧 ⇒ 分子分母不在同一基准，
    //   比值失去意义（实测 SC-10 曾算出 7213%）。故一律 N/A，界面显示「—」。
    const bool stepLike = (_ctx.law == ControlLaw::kPidHeading);
    const double tgt = _ctx.targetHeadingDeg;
    const double y0 = _trail.front().yawDeg;
    const double span = std::abs(tgt - y0);
    if (!stepLike || span < kMinOvershootSpanDeg) {
      m.overshootPct = -1.0;   ///< N/A：非阶跃律，或跨度太小（分母趋零）
    } else {
      const double dir = (tgt >= y0) ? 1.0 : -1.0;
      double peak = 0.0;
      for (size_t i = 0; i < n; ++i)
        peak = std::max(peak, dir * (_trail[i].yawDeg - tgt));
      m.overshootPct = std::max(0.0, peak / span * 100.0);
    }
  }

  // DP 保持误差 / 编队误差
  if (_ctx.law == ControlLaw::kDp) {
    double sum = 0.0;
    for (size_t i = winFrom; i < n; ++i)
      sum += std::hypot(_trail[i].x - _ctx.targetXM, _trail[i].y - _ctx.targetYM);
    m.dpHoldErrM = sum / static_cast<double>(win);
  } else if (_ctx.law == ControlLaw::kFormation) {
    double sum = 0.0;
    for (size_t i = winFrom; i < n; ++i)
      sum += std::hypot(_trail[i].x - _ctx.formationTargetXM,
                        _trail[i].y - _ctx.formationTargetYM);
    m.formationErrM = sum / static_cast<double>(win);
    m.dpHoldErrM = m.formationErrM;
  }

  // 航路点到达率
  if (_ctx.waypointsTotal > 0) {
    m.arrivalRate = Clamp(static_cast<double>(_ctx.waypointsReached) /
                              static_cast<double>(_ctx.waypointsTotal), 0.0, 1.0);
  }

  // 最小 DCPA（障碍静止：DCPA 由本船当时速度外推）
  if (_ctx.hasObstacle) {
    double best = -1.0;
    for (size_t i = 0; i < n; ++i) {
      double vx = 0.0, vy = 0.0;
      if (i > 0) {
        const double dt = _trail[i].t - _trail[i - 1].t;
        if (dt > 1e-6) {
          vx = (_trail[i].x - _trail[i - 1].x) / dt;
          vy = (_trail[i].y - _trail[i - 1].y) / dt;
        }
      }
      const double d = Dcpa(_trail[i].x, _trail[i].y, vx, vy,
                            _ctx.obstacleXM, _ctx.obstacleYM, 0.0, 0.0) -
                       _ctx.obstacleRadiusM;   // 扣半径 = 净空
      if (best < 0.0 || d < best) best = d;
    }
    m.minDcpaM = best;
  }

  m.valid = true;
  return m;
}

// ─────────────────── ⑦ 一阶惯性+纯延迟识别 / ZN 整定 ───────────────────
/// 开环阶跃响应的三参数（教学用切线法，不是最小二乘辨识）
struct StepResponseFit {
  double gainDegPerN = 0.0;    ///< K：稳态偏航变化 / 推力阶跃 (deg/N)
  double deadTimeS = 0.0;      ///< L：纯延迟 (s)
  double timeConstS = 0.0;     ///< T：时间常数 (s)
  double steadyYawDeg = 0.0;   ///< 稳态偏航角
  double maxSlopeDegPerS = 0.0;///< 最大航向变化率 (deg/s)
  bool   valid = false;
};

/// 从**开环阶跃**轨迹（推力阶跃 `_thrustStepN`）切线法拟合 K/T/L。
/// 要求：轨迹 ≥ 10 点、|Δ推力| > 1e-6、稳态偏航变化 > 0.3°。
inline StepResponseFit FitFirstOrderPlusDelay(
    const std::vector<ControlSample> &_trail, double _thrustStepN) {
  StepResponseFit f;
  const size_t n = _trail.size();
  if (n < 10 || std::abs(_thrustStepN) < 1e-6) return f;
  const double y0 = _trail.front().yawDeg;
  const size_t tail = std::max<size_t>(3, n / 10);
  double ss = 0.0;
  for (size_t i = n - tail; i < n; ++i) ss += _trail[i].yawDeg;
  ss /= static_cast<double>(tail);
  f.steadyYawDeg = ss;
  const double dy = ss - y0;
  if (std::abs(dy) < 0.3) return f;

  // 中心差分求最大斜率
  double bestSlope = 0.0, bestT = 0.0, bestY = 0.0;
  for (size_t i = 1; i + 1 < n; ++i) {
    const double dt = _trail[i + 1].t - _trail[i - 1].t;
    if (dt < 1e-6) continue;
    const double slope = (_trail[i + 1].yawDeg - _trail[i - 1].yawDeg) / dt;
    if (std::abs(slope) > std::abs(bestSlope)) {
      bestSlope = slope;
      bestT = _trail[i].t;
      bestY = _trail[i].yawDeg;
    }
  }
  if (std::abs(bestSlope) < 1e-6) return f;
  f.maxSlopeDegPerS = bestSlope;

  // 切线 y = y0 + slope·(t − t_i)；反解 y = y0 的时刻 = 延迟 L
  const double tIntercept = bestT - (bestY - y0) / bestSlope;
  const double L = std::max(0.0, tIntercept - _trail.front().t);
  // 切线达到稳态值的时刻 ⇒ T = t_ss − L
  const double tSs = bestT + (dy - (bestY - y0)) / bestSlope;
  const double T = std::max(1e-3, (tSs - _trail.front().t) - L);

  f.gainDegPerN = dy / _thrustStepN;
  f.deadTimeS = L;
  f.timeConstS = T;
  f.valid = (std::abs(f.gainDegPerN) > 1e-9) && (L > 0.05) && (T > 0.1);
  return f;
}

struct ZnGains {
  double kp = 0.0, ki = 0.0, kd = 0.0;
  bool valid = false;
};

/// Ziegler–Nichols **反应曲线法** PID：Kp = 1.2T/(K·L)，Ti = 2L，Td = 0.5L
/// （K 的单位是 deg/N，误差单位为度 ⇒ kp 单位 N/deg，可直接喂给 ComputeDiffThrust）
inline ZnGains ZieglerNicholsPid(const StepResponseFit &_f, double _scale = 1.0) {
  ZnGains g;
  if (!_f.valid) return g;
  const double k = std::abs(_f.gainDegPerN);
  if (k < 1e-9 || _f.deadTimeS < 0.05 || _f.timeConstS < 0.1) return g;
  const double kp = 1.2 * _f.timeConstS / (k * _f.deadTimeS) * _scale;
  const double ti = 2.0 * _f.deadTimeS;
  const double td = 0.5 * _f.deadTimeS;
  g.kp = Clamp(kp, 0.02, 50.0);
  g.ki = g.kp / ti;
  g.kd = g.kp * td;
  g.valid = std::isfinite(g.kp) && std::isfinite(g.ki) && std::isfinite(g.kd);
  return g;
}

}  // namespace keship

#endif  // KESHIP_CONTROL_LAWS_HH_
