/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L6/L7 控制链 —— **纯算法内核**独立测试（不依赖 Qt / gz / 仿真进程）
 *
 * 为什么先测这一层：控制律最典型的错误是「符号反了 / 钳位没生效 / RMS 口径不对」，
 * 这些在仿真里只表现为"船跑歪了"，肉眼归因不到具体一行。故本测试对
 * `ControlLaws.hh` 逐条做**数值判据**（与手算对照），失败即 return 1，可接 CI。
 *
 * 覆盖（对应交付要求 1~6）：
 *   ① PID 差动方向 + 差值 = kp·e（钳位前）
 *   ② 积分饱和钳位
 *   ③ LOS 期望航向（零偏、正负号）
 *   ④ DCPA 与手算一致（两条匀速直线）
 *   ⑤ 指标：正弦横向偏差的 RMS 与手算一致（附稳态/调节时间/超调）
 *   ⑥ 负例：diffLimitN = 0 ⇒ 差动恒 0（证明钳位真的在起作用）
 * 另外补测（同一份实现的边界）：共模航速环限幅、COLREGs 偏置符号、DP 极坐标、ZN 整定。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ControlLaws.hh"

namespace {

int gFail = 0;

void Check(bool _cond, const std::string &_msg) {
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (!_cond) ++gFail;
}

void Note(const std::string &_msg) { std::printf("[NOTE]  %s\n", _msg.c_str()); }

bool Near(double _a, double _b, double _tol = 1e-9) {
  return std::abs(_a - _b) <= _tol * std::max({1.0, std::abs(_a), std::abs(_b)});
}

keship::ControlParams BaseParams() {
  keship::ControlParams p;
  p.law = keship::ControlLaw::kPidHeading;
  p.kp = 1.0;
  p.ki = 0.0;
  p.kd = 0.0;
  p.baseThrustN = 100.0;
  p.diffLimitN = 80.0;
  return p;
}

// ── ① PID 差动方向 ────────────────────────────────────────────────────────
void TestDiffThrustDirection() {
  auto p = BaseParams();
  const double e = 10.0;   // 航向误差为正 ⇒ 需增大 yaw ⇒ 左转 ⇒ Δt > 0 ⇒ R > L
  const auto d = keship::ComputeDiffThrust(e, 0.0, 0.0, p);

  std::printf("        e=%.1f°  kp=%.1f ⇒ Δt_raw=%.3f N, L=%.1f N, R=%.1f N\n",
              e, p.kp, d.diffRaw, d.left, d.right);

  Check(Near(d.diffRaw, p.kp * e), "①a 差动量 = kp·e（钳位前，=10.000 N）");
  Check(Near(d.diff, p.kp * e), "①b 未超限时差动量不被改动");
  Check(d.right > d.left, "①c 误差为正 ⇒ 右桨推力大于左桨（yaw 增大方向）");
  Check(Near(d.right - d.left, 2.0 * p.kp * e), "①d 两桨推力差 = 2·kp·e（=20.000 N）");
  Check(Near(d.left, p.baseThrustN - p.kp * e) &&
            Near(d.right, p.baseThrustN + p.kp * e),
        "①e L = base − Δt、R = base + Δt");

  // 反向同样成立（符号对称）
  const auto d2 = keship::ComputeDiffThrust(-e, 0.0, 0.0, p);
  Check(d2.left > d2.right && Near(d2.diff, -p.kp * e),
        "①f 误差为负 ⇒ 左桨大于右桨（对称）");
}

// ── ② 积分饱和钳位 ────────────────────────────────────────────────────────
void TestIntegralClamp() {
  const double lim = 80.0;
  const double i1 = keship::ClampIntegral(1000.0, 1.0, lim);
  const double i2 = keship::ClampIntegral(1000.0, 2.0, lim);
  const double i3 = keship::ClampIntegral(-1000.0, 2.0, lim);
  std::printf("        ki=1 ⇒ ∫=%.3f ; ki=2 ⇒ ∫=%.3f ; ki=2(负) ⇒ ∫=%.3f\n",
              i1, i2, i3);

  Check(Near(i1, lim), "②a |ki·∫| ≤ diffLimit（ki=1 ⇒ ∫=80）");
  Check(Near(i2, lim / 2.0), "②b ki 翻倍 ⇒ 积分上限减半（∫=40）");
  Check(Near(i3, -lim / 2.0), "②c 负向对称钳位（∫=−40）");
  Check(Near(keship::ClampIntegral(1000.0, 0.0, lim), 0.0),
        "②d ki=0 ⇒ 不累积（返回 0，防无意义漂移）");

  // 钳位后即使误差为 0，积分项也不会把差动推过限幅
  auto p = BaseParams();
  p.kp = 0.0; p.ki = 2.0;
  const auto d = keship::ComputeDiffThrust(0.0, i2, 0.0, p);
  Check(std::abs(d.diff) <= p.diffLimitN + 1e-9 && !d.saturated,
        "②e 钳位后的积分项恰好顶到限幅而不越界");
}

// ── ③ LOS 期望航向 ────────────────────────────────────────────────────────
void TestLosHeading() {
  const double path = 45.0;
  const double delta = 2.0;

  const double h0 = keship::LosDesiredHeading(path, 0.0, delta);
  const double hPos = keship::LosDesiredHeading(path, 5.0, delta);
  const double hNeg = keship::LosDesiredHeading(path, -5.0, delta);
  std::printf("        ψ_path=45°, Δ=2 m ⇒ e=0:%.3f°  e=+5:%.3f°  e=−5:%.3f°\n",
              h0, hPos, hNeg);

  Check(Near(h0, path), "③a 横向误差为 0 ⇒ 期望航向 = 路径航向");
  Check(hPos < path, "③b e>0（船偏港侧）⇒ 偏置为负 ⇒ 右转回到航线");
  Check(hNeg > path, "③c e<0（船偏星侧）⇒ 偏置为正 ⇒ 左转回到航线");
  Check(Near(hPos - path, std::atan(-5.0 / delta) * keship::kRad2Deg, 1e-12),
        "③d 偏置幅度 = atan(−e/Δ)");

  // 横向误差算式本身的符号：路径沿 +X，船在 +Y（港侧）⇒ e > 0
  const double ePort = keship::CrossTrackErrorM(0.0, 5.0, 0.0, 0.0, 10.0, 0.0);
  const double eStar = keship::CrossTrackErrorM(0.0, -5.0, 0.0, 0.0, 10.0, 0.0);
  Check(Near(ePort, 5.0) && Near(eStar, -5.0),
        "③e 横向偏差：路径 +X 时 +Y 侧（港侧）为正");
}

// ── ④ DCPA 手算对照 ───────────────────────────────────────────────────────
void TestDcpa() {
  // 局面 A：A 船 (0,0) 以 1 m/s 沿 +x；B 船 (10,3) 以 1 m/s 沿 −x ⇒ 相对速度 (−2,0)
  //   t_cpa = −(10·(−2)+3·0)/4 = 5 s；DCPA = |(10−10, 3)| = 3 m
  double t1 = -1.0;
  const double d1 = keship::Dcpa(0, 0, 1, 0, 10, 3, -1, 0, &t1);
  std::printf("        对遇局面：tx=%.3f s, DCPA=%.3f m（手算 5.000 / 3.000）\n", t1, d1);
  Check(Near(t1, 5.0, 1e-12), "④a t_cpa = 5.000 s");
  Check(Near(d1, 3.0, 1e-12), "④b DCPA = 3.000 m");

  // 局面 B：静止障碍 (3,4)，本船 (0,0) 以 1 m/s 沿 +x
  //   t_cpa = 3 s；DCPA = |(3−3, 4)| = 4 m（= 垂直距离）
  double t2 = -1.0;
  const double d2 = keship::Dcpa(0, 0, 1, 0, 3, 4, 0, 0, &t2);
  std::printf("        静止障碍：tx=%.3f s, DCPA=%.3f m（手算 3.000 / 4.000）\n", t2, d2);
  Check(Near(t2, 3.0, 1e-12), "④c 静止障碍 t_cpa = 3.000 s");
  Check(Near(d2, 4.0, 1e-12), "④d 静止障碍 DCPA = 4.000 m（垂距）");

  // 已驶过（t_cpa < 0 ⇒ 取 0，DCPA = 当前距离）
  double t3 = -1.0;
  const double d3 = keship::Dcpa(5, 0, 1, 0, 0, 0, 0, 0, &t3);
  Check(Near(t3, 0.0) && Near(d3, 5.0), "④e t_cpa<0 ⇒ 取 0，DCPA = 当前距离 5 m");
}

// ── ⑤ 指标：正弦横向偏差的 RMS 与手算一致 ─────────────────────────────────
void TestMetricsRms() {
  const int n = 200;
  const double ampLat = 2.0, ampHead = 1.0, target = 30.0;
  std::vector<keship::ControlSample> tr;
  for (int i = 0; i < n; ++i) {
    const double ph = 2.0 * keship::kPi * i / n;   // 整周期采样 ⇒ Σsin² = n/2
    keship::ControlSample s;
    s.t = 0.1 * i;
    s.x = static_cast<double>(i) * 0.1;            // 沿 +x 直航
    s.y = 0.0;
    s.yawDeg = target;
    s.speedMs = 1.0;
    s.lateralErrM = ampLat * std::sin(ph);
    s.headingErrDeg = ampHead * std::sin(ph);
    s.thrustL = 100.0;
    s.thrustR = 100.0;
    tr.push_back(s);
  }
  keship::MetricContext ctx;
  ctx.law = keship::ControlLaw::kPidHeading;
  ctx.targetHeadingDeg = target;
  ctx.targetSpeedMs = 1.0;
  const auto m = keship::ComputeMetrics(tr, ctx);

  // RMS = A/√2（正弦整周期），横向 2/√2 = 1.414214，航向 1/√2 = 0.707107
  const double expLat = ampLat / std::sqrt(2.0);
  const double expHead = ampHead / std::sqrt(2.0);
  std::printf("        横向 RMS 实测 %.6f / 手算 %.6f ；航向 RMS 实测 %.6f / 手算 %.6f\n",
              m.lateralRmsM, expLat, m.headingRmseDeg, expHead);
  Check(m.valid, "⑤a 指标有效（≥3 点）");
  Check(std::abs(m.lateralRmsM - expLat) < 1e-6, "⑤b 横向 RMS = A/√2 = 1.414214 m");
  Check(std::abs(m.headingRmseDeg - expHead) < 1e-6, "⑤c 航向 RMSE = A/√2 = 0.707107°");
  Check(std::abs(m.maxLateralM - ampLat) < 1e-6, "⑤d 最大横向偏差 = 幅值 2.000 m");
  Check(Near(m.meanThrustN, 100.0), "⑤e 平均推力 = (L+R)/2 = 100 N");

  // 调节时间 / 超调 / 稳态误差：构造一阶趋近 + 10% 超调的阶跃响应
  std::vector<keship::ControlSample> step;
  for (int i = 0; i <= 20; ++i) {
    keship::ControlSample s;
    s.t = static_cast<double>(i);
    // t<5 未响应；t>=5 精确到位；t=12 处给一个 33° 的峰值（超调 10%）
    s.yawDeg = (i < 5) ? 0.0 : target;
    if (i == 12) s.yawDeg = 33.0;
    s.headingErrDeg = target - s.yawDeg;
    s.speedMs = 1.0;
    step.push_back(s);
  }
  keship::MetricContext c2 = ctx;
  c2.targetHeadingDeg = target;
  const auto m2 = keship::ComputeMetrics(step, c2);
  std::printf("        阶跃：settling=%.3f s, overshoot=%.3f%%, steadyErr=%.4f°\n",
              m2.settlingTimeS, m2.overshootPct, m2.headingSteadyErrDeg);
  Check(Near(m2.settlingTimeS, 13.0), "⑤f 调节时间 = 最后一次出带后的下一采样点 t=13 s");
  Check(std::abs(m2.overshootPct - 10.0) < 1e-9, "⑤g 超调 = (33−30)/30 = 10.000%");
  Check(std::abs(m2.headingSteadyErrDeg) < 1e-9, "⑤h 末 20% 稳态误差 = 0°");

  // 数据不足 ⇒ invalid（不许把 2 个点算成指标）
  std::vector<keship::ControlSample> few(tr.begin(), tr.begin() + 2);
  Check(!keship::ComputeMetrics(few, ctx).valid, "⑤i 少于 3 点 ⇒ valid=false（不编数）");
}

// ── ⑥ 负例：限幅为 0 ⇒ 差动恒 0 ───────────────────────────────────────────
void TestZeroDiffLimit() {
  auto p = BaseParams();
  p.diffLimitN = 0.0;
  bool allZero = true;
  for (double e = -50.0; e <= 50.0; e += 5.0) {
    const auto d = keship::ComputeDiffThrust(e, 10.0, 3.0, p);
    if (std::abs(d.diff) > 1e-12) allZero = false;
    if (!Near(d.left, p.baseThrustN) || !Near(d.right, p.baseThrustN)) allZero = false;
  }
  Check(allZero, "⑥a diffLimitN=0 ⇒ 差动输出恒 0、两桨恒为 base（钳位真的生效）");

  // 超限时 diffRaw 与 diff 必须不同 —— 否则"没钳位"和"钳位了"无法区分
  auto q = BaseParams();
  const auto dq = keship::ComputeDiffThrust(200.0, 0.0, 0.0, q);   // 200 N > 80 N
  Check(std::abs(dq.diffRaw - 200.0) < 1e-9 && Near(dq.diff, 80.0) && dq.saturated,
        "⑥b 超限时 diffRaw=200 N 被钳到 80 N 且置 saturated");
}

// ── 补充：共模航速环 ──────────────────────────────────────────────────────
void TestCommonThrust() {
  auto p = BaseParams();
  const double up = keship::ComputeCommonThrust(1.0, 0.0, 0.0, p);
  const double dn = keship::ComputeCommonThrust(-3.0, 0.0, 0.0, p);
  Check(Near(up, 1.0) && Near(dn, -3.0), "⑦a 共模增量 = kp·(v_d−v)，正负对称");
  Check(Near(keship::ComputeCommonThrust(1e6, 0.0, 0.0, p), p.diffLimitN),
        "⑦b 共模增量同样受 diffLimit 限幅");
}

// ── 补充：COLREGs 偏置符号 ────────────────────────────────────────────────
void TestColregs() {
  const double headOn = keship::ColregsHeadingBiasDeg(0.0, 20.0, 1.5);
  const double crossStar = keship::ColregsHeadingBiasDeg(60.0, 20.0, 1.5);
  const double crossPort = keship::ColregsHeadingBiasDeg(-60.0, 20.0, 1.5);
  const double far = keship::ColregsHeadingBiasDeg(0.0, 200.0, 1.5);
  const double over = keship::ColregsHeadingBiasDeg(170.0, 20.0, 1.5);
  std::printf("        对遇=%.2f°  交叉(右舷)=%.2f°  交叉(左舷)=%.2f°  远距=%.2f°\n",
              headOn, crossStar, crossPort, far);
  Check(headOn < -1.0 && crossStar < -1.0, "⑧a 对遇/右舷交叉 ⇒ 负偏置（右转）");
  Check(crossPort < 0.0 && crossPort > headOn, "⑧b 左舷交叉 ⇒ 仅小幅右转（不左转横越）");
  Check(Near(far, 0.0), "⑧c 超出探测距离 ⇒ 不干预");
  Check(Near(over, 0.0), "⑧d 追越局面本教学版不干预（边界见报告 §5）");
}

// ── 补充：DP 极坐标分解 ───────────────────────────────────────────────────
void TestDpPolar() {
  const auto c1 = keship::PolarDpCommand(10.0, 0.0, 1.0, 0.4);
  const auto c2 = keship::PolarDpCommand(0.0, -10.0, 1.0, 0.4);
  const auto c3 = keship::PolarDpCommand(0.2, 0.0, 1.0, 0.4);
  Check(Near(c1.courseDeg, 0.0) && Near(c1.speedMs, 1.0),
        "⑨a 目标在正东 10 m ⇒ 航迹向 0°、航速取上限 1 m/s");
  Check(Near(c2.courseDeg, -90.0) && Near(c2.speedMs, 1.0),
        "⑨b 目标在正南 10 m ⇒ 航迹向 −90°");
  Check(Near(c3.speedMs, 0.0), "⑨c 距目标 0.2 m（<0.5 m 到位半径）⇒ 期望航速 0");
  const auto c4 = keship::PolarDpCommand(1.0, 0.0, 1.0, 0.4);
  Check(Near(c4.speedMs, 0.4), "⑨d 近距离 ⇒ 航速随距离线性收敛（k·|e| = 0.4 m/s）");
}

// ── 补充：ZN 整定（阶跃识别 → 增益为正、量纲自洽） ─────────────────────────
void TestZieglerNichols() {
  // 构造一阶惯性+延迟：K = 0.5 deg/N（阶跃 40 N ⇒ 稳态 20°），L = 2 s，T = 6 s
  const double stepN = 40.0, K = 0.5, L = 2.0, T = 6.0;
  std::vector<keship::ControlSample> tr;
  for (int i = 0; i <= 400; ++i) {
    const double t = 0.05 * i;
    keship::ControlSample s;
    s.t = t;
    s.yawDeg = (t <= L) ? 0.0 : K * stepN * (1.0 - std::exp(-(t - L) / T));
    tr.push_back(s);
  }
  const auto f = keship::FitFirstOrderPlusDelay(tr, stepN);
  const auto g = keship::ZieglerNicholsPid(f);
  std::printf("        识别：K=%.3f deg/N, L=%.3f s, T=%.3f s ⇒ Kp=%.3f, Ki=%.4f, Kd=%.3f\n",
              f.gainDegPerN, f.deadTimeS, f.timeConstS, g.kp, g.ki, g.kd);
  Check(f.valid && std::abs(f.gainDegPerN - K) < 0.03, "⑩a 增益识别偏差 <0.03 deg/N");
  Check(std::abs(f.deadTimeS - L) < 0.6, "⑩b 纯延迟识别偏差 <0.6 s");
  Check(g.valid && g.kp > 0.0 && g.ki > 0.0 && g.kd > 0.0, "⑩c ZN 增益全为正");
  Check(Near(g.ki, g.kp / (2.0 * f.deadTimeS), 1e-6) &&
            Near(g.kd, g.kp * 0.5 * f.deadTimeS, 1e-6),
        "⑩d Ti=2L、Td=0.5L（Ki=Kp/Ti、Kd=Kp·Td）");
}

}  // namespace

int main() {
  std::printf("==== L6/L7 控制链 核心层（纯算法）测试 ====\n");
  TestDiffThrustDirection();
  TestIntegralClamp();
  TestLosHeading();
  TestDcpa();
  TestMetricsRms();
  TestZeroDiffLimit();
  TestCommonThrust();
  TestColregs();
  TestDpPolar();
  TestZieglerNichols();
  Note("本轮不覆盖：gz 话题/位姿订阅（需仿真）、Qt 对话框（需 QApplication）");
  std::printf("==== 失败 %d 项 ====\n", gFail);
  return gFail == 0 ? 0 : 1;
}
