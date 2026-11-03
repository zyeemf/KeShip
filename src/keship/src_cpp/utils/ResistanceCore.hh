/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— 阻力计算内核（不依赖 Qt / gz；GUI 与无头测试共用同一份实现）
 *
 * 职责：给定船体主尺度与航速，算出**总阻力及其分解**（摩擦 / 兴波 / 空气）、
 *       并给出「最优方形系数」与「模型→实船」的外推结果。
 *
 * 为什么单独抽成内核：RP-01/02/05/06 四个实验项都要用同一套阻力口径，
 * 若把公式散在对话框里，GUI 曲线与无头判据会各算一套、对不上账。
 * 因此这里只放**纯函数**（无状态、无线程、无 IO），对话框与测试都只是它的薄壳。
 *
 * 公式与系数出处（逐条对应实现，便于审稿人追溯）：
 *   ① Fn = V/√(gL)、Re = V·L/ν —— 傅汝德相似与雷诺相似的定义式
 *   ② Cf = 0.075/(lgRe − 2)² —— **ITTC-57 1957 公式**（平板摩擦阻力线）
 *   ③ (1+k) = 0.93 + 0.487118·c14·(B/L)^1.06806·(T/L)^0.46106·
 *             (L/∇^(1/3))^0.121563·(L³/∇)^0.36486·(1−Cp)^(−0.604247)
 *      —— **Holtrop & Mennen (1982)** 形状因子回归式；c14 = 1 + 0.011·Cstern
 *         （本模块取 Cstern = 0 ⇒ c14 = 1，即常规单桨驱逐艉/巡洋艉船型）
 *   ④ S = L(2T+B)·√Cm·(0.453 + 0.4425Cb − 0.2862Cm − 0.003467·B/T + 0.3696Cwp)
 *      —— **Holtrop & Mennen (1982)** 湿表面积回归式（无球鼻艏项 ABT = 0）
 *   ⑤ 兴波：Cr ∝ Fn²，即 Rw ∝ V⁴ —— **傅汝德 V⁴ 律**（低中 Fn 段位移船兴波阻力的
 *      经典量级近似）；锚点 Cr(Fn=0.30) = 1.50e-3（Taylor / Series 60 剩余阻力
 *      图谱在同 Fn、Cb≈0.6 处的量级上限，取上限即偏保守）。高 Fn 用饱和项封顶，
 *      避免 V⁴ 律在 Fn>0.5 后无界增长（详见 .cc 内注释）
 *   ⑥ Cb 对兴波的影响：以 **Watson & Gilfillan 最优方形系数式 Cb_opt = 1.08 − 1.68Fn**
 *      在设计 Fn=0.30 处的值 0.576 为中心作二次惩罚（系数为标定值，见报告）
 *   ⑦ 空气阻力 Rwind = ½ρ_air·Cd·A_front·V_wind²，**Cd = 1.0**（钝体正面迎风，
 *      Hoerner《Fluid-Dynamic Drag》钝体系数取下限的保守取法）
 *   ⑧ 实船外推：**三因次法（ITTC-78 的简化形式）** Cts = Ctm − (1+k)(Cfm − Cfs)，
 *      即保持剩余阻力系数相同、只修正摩擦项
 */

#ifndef KESHIP_RESISTANCE_CORE_HH_
#define KESHIP_RESISTANCE_CORE_HH_

#include <vector>

namespace keship {

/// 阻力计算的输入：船体主尺度 + 流体属性 + 环境风
/// 默认值即 RP-01 教学船的基准工况（6 m 水线长、1 m³ 排水、15℃ 海水）
struct ResistanceInput {
  double Lwl = 6.0;      ///< 水线长 (m)
  double B = 1.5;        ///< 型宽 (m)
  double T = 0.3;        ///< 吃水 (m)
  double Cb = 0.6;       ///< 方形系数
  double Cm = 0.9;       ///< 中横剖面系数
  double Cwp = 0.7;      ///< 水线面系数
  double volumeD = 1.0;  ///< 排水体积 (m³)
  double rhoWater = 1025.0;  ///< 海水密度 (kg/m³)
  double nu = 1.139e-6;      ///< 运动黏度 (m²/s)，15℃ 海水
  double rhoAir = 1.225;     ///< 空气密度 (kg/m³)
  double windSpeedMs = 0.0;  ///< **相对**风速 (m/s)；逆风时由调用方加上船速
  double frontalAreaM2 = 0.0;///< 水线以上正面受风面积 (m²)
  double appendageFactor = 1.0;  ///< 附体阻力修正 (1+k2)，只作用在黏性项
};

/// 单一航速下的阻力分解（单位：N）
struct ResistanceBreakdown {
  double speedMs = 0.0;  ///< 航速 (m/s)
  double Fn = 0.0;       ///< 傅汝德数
  double Re = 0.0;       ///< 雷诺数
  double Cf = 0.0;       ///< ITTC-57 平板摩擦系数（**不含**形状因子）
  double onePlusK = 0.0; ///< 形状因子 (1+k)
  double Rf = 0.0;       ///< 黏性阻力（已含 (1+k) 与附体修正）
  double Rr = 0.0;       ///< 兴波（剩余）阻力
  double Rwind = 0.0;    ///< 空气阻力
  double Rtotal = 0.0;   ///< 总阻力 = Rf + Rr + Rwind（恒等式由测试守住）
  double Ct = 0.0;       ///< 总阻力系数 = Rtotal / (½ρV²S)
};

/// 单点阻力计算。_speedMs ≤ 0 时返回全零（Ct 置 0，避免除零），不产生 NaN。
ResistanceBreakdown ResistanceAtSpeed(const ResistanceInput &_in, double _speedMs);

/// 等间距阻力曲线（含两端点）。_n < 2 时退化为单点；_vMax < _vMin 时自动交换。
std::vector<ResistanceBreakdown> ResistanceCurve(const ResistanceInput &_in,
                                                 double _vMin, double _vMax, int _n);

/// 在 Cb ∈ [0.5, 0.85] 上扫描，返回**平均总阻力最小**的方形系数。
/// 目标函数取 [_vMin,_vMax] 上等距采样的总阻力平均值：只盯单一航速会把结论
/// 绑死在那个速度点上，教学上无法解释「为什么这个 Cb 好」。
double CbForMinResistance(const ResistanceInput &_in, double _vMin, double _vMax);

/// 三因次法（傅汝德相似 + ITTC-57 摩擦修正）外推实船总阻力 (N)。
/// _model 为模型尺度输入，_lambda 为缩尺比（实船/模型），_shipSpeedMs 为实船航速。
/// ⚠ 几何相似前提：实船湿面积按 λ² 缩放到模型，故 _model 的主尺度应自洽
///   （L:B:T:Cb 与实船一致）——本函数不做尺度校验，只做系数换算。
double ModelToShipResistance(const ResistanceInput &_model, double _lambda,
                             double _shipSpeedMs);

/// 湿表面积 (m²)，Holtrop 回归式。GUI 展示与 Ct 归一化共用，故公开。
double WettedSurfaceM2(const ResistanceInput &_in);

/// 形状因子 (1+k)，Holtrop 回归式。GUI 展示用。
double FormFactorOnePlusK(const ResistanceInput &_in);

/// 傅汝德数，GUI 坐标轴标注用。
double FroudeNumber(const ResistanceInput &_in, double _speedMs);

}  // namespace keship

#endif  // KESHIP_RESISTANCE_CORE_HH_
