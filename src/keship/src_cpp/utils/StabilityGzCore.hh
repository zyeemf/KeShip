/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-08 完整稳性综合校核 —— 静稳性曲线 GZ(φ) 内核（纯计算，不依赖 Qt / gz）
 *
 * 为什么单独抽内核：HS-08 的对话框、无头判据、实验报告三处都要用同一条 GZ 曲线，
 * 若各自实现必然对不上账；故这里只放纯函数，调用方只做展示。
 *
 * 坐标与符号约定（全模块统一）：
 *   x 从艉向艏、y 指向右舷、z 从龙骨基线向上；φ > 0 表示**右舷下沉**。
 *   GZ(φ) > 0 为复原力臂，GZ(φ) < 0 为倾覆力臂；对称船必有 GZ(−φ) = −GZ(φ)。
 *
 * 物理口径：等体积倾斜 —— 先按"排水体积不变"解出新水线，再算浮心横向移动量 KN，
 *   最后 GZ = KN − KG·sinφ。**不做**任何 GZ 形状的经验假设。
 *
 * ⚠ 大倾角的关键点（教学最易被误导处）：
 *   wall-sided 解析式 GZ = sinφ·(GM + ½·BM·tan²φ) 只在"舷侧直立、水线同时切到
 *   两舷"时成立。对本题驳船（B=1.5, T=0.3, D=1.0）：
 *     · 舭部出水角 atan(2T/B)      = 21.8°（高舷侧龙骨角离开水面）
 *     · 甲板边缘入水角 atan(2(D−T)/B) = 43.0°
 *   前者更早，故 **21.8° 之后 wall-sided 即失效**（25° 偏差 −3.4%，30° 偏差 −16%）。
 *   本内核在 21.8° 后按被水线截断的真实剖面分段积分，绝不再套解析式。
 *
 * 剖面假设（输入只有水线处半宽，必须说明）：
 *   非矩形模式把每站视为**舷侧直立**的矩形剖面，从 z=0 延伸到 z=D，
 *   水线宽度取输入半宽。真实剖面的舭部圆弧/外飘无法由单一半宽还原，
 *   故本模式给出的是"同一水线宽度下最保守的直立舷侧等价船"。
 */

#ifndef KESHIP_STABILITY_GZ_CORE_HH_
#define KESHIP_STABILITY_GZ_CORE_HH_

#include <vector>

namespace keship {

/// 一站的横剖面：x 位置、水线处半宽（左右舷对称，故只存半宽）
struct HullStation {
  double x = 0.0;            // 距艉 m
  double halfBreadth = 0.0;  // 水线处半宽 m
};

/// GZ 计算输入。默认值即 HS-08 教学驳船基准工况（6 m × 1.5 m × 1.0 m，吃水 0.3 m）
struct GzInput {
  std::vector<HullStation> stations;
  double L = 6.0, B = 1.5, D = 1.0;   // 主尺度
  double draft = 0.3;                 // 正浮吃水
  double KG = 0.5;                    // 重心高（距龙骨）
  double rhoWater = 1000.0;           // 只供调用方换算重量/力矩；GZ 是长度，与密度无关
  bool   boxSection = true;           // true=强制 b=B/2 的矩形驳船（有解析解可校核）；
                                      // false=按 stations 的半宽表构造变半宽船
};

/// 单点 GZ（度）。|φ| > 90° 时按 90° 取值（横倾曲线的物理定义域到 90° 为止）；
/// 排水体积 ≤ 0（空表/零吃水）或输入含 NaN 时返回 0，不产生 NaN。
double GZ(double _phiDeg, const GzInput &_in);

struct GzPoint { double phiDeg = 0.0, gzM = 0.0; };

/// 等间距 GZ 曲线，含 0 与 _maxDeg 两个端点。_stepDeg ≤ 0 时按 1° 处理。
std::vector<GzPoint> GzCurve(const GzInput &_in, double _maxDeg, double _stepDeg);

struct GzSummary {
  double gzMaxM = 0.0;        // GZ 最大值
  double angleGzMaxDeg = 0.0; // 最大 GZ 对应横倾角
  double vanishingAngleDeg = 0.0; // 消失角（GZ 回落到 0；90° 内不回落则取 90°）
  double gm0M = 0.0;          // 初稳性高（= dGZ/dφ 在 0 处，弧度制）
  double area0to30 = 0.0;     // 0~30° 面积 m·rad
  double area30to40 = 0.0;    // 30~40°
  double area0to40 = 0.0;
  double area40toVanishing = 0.0;
  // IMO A.749(18) / IS Code 2008 五项衡准
  bool passArea0to30 = false, passArea30to40 = false, passArea40toVan = false,
       passGzMax = false, passGm0 = false;
  bool allPass = false;
};

/// 汇总曲线特征与 IMO 五项衡准。
/// 五条阈值逐字取自 IMO A.749(18) 第 3.1 节 / IS Code 2008 Part A 2.2：
///   ① 0~30° 面积 ≥ 0.055 m·rad     ② 30~40° 面积 ≥ 0.030 m·rad
///   ③ 40°~消失角 面积 ≥ 0.090 m·rad ④ GZmax ≥ 0.20 m 且对应角 ≥ 30°
///   ⑤ 初稳性高 GM0 ≥ 0.15 m
GzSummary Summarize(const GzInput &_in);

// ─────────────────────────────────────────────────────────────────────────────
// 内部共享几何（detail）：仅供 DamageStabilityCore 复用同一套剖面/水线求解，
// 避免"完整船"与"破舱船"各写一套积分而漂移。**不属于对外教学 API**，
// GUI/实验报告请只用上面的 GZ / GzCurve / Summarize。
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

/// 一个剖面在水线以下的面积与一阶矩（对 y、z）
struct SectionProps {
  double area = 0.0;     // ∫ dA            m²
  double yMoment = 0.0;  // ∫ y·dA          m³
  double zMoment = 0.0;  // ∫ z·dA          m³
};

/// 归一化后的船体几何（站位按 [0,L] 等距加密 + 保留原始节点，梯形积分不丢形状）
struct HullGeom {
  std::vector<double> x;      // 站位 m（升序，覆盖 [0,L]）
  std::vector<double> halfB;  // 对应水线半宽 m
  double length = 0.0;        // L
  double depth = 0.0;         // D
  double draft = 0.0;         // T
  double displacement = 0.0;  // 正浮排水体积 ∇ m³
  double maxHalfB = 0.0;
  double minHalfB = 0.0;
  double lcbX = 0.0;          // 正浮浮心纵向位置 m
  double waterplaneArea = 0.0;
  double waterplaneIx = 0.0;  // ∫ y²dA = ∫ (2/3)b³ dx  m⁴
  double waterplaneIl = 0.0;  // 对过漂心的横轴的惯性矩 m⁴
  double lcfX = 0.0;          // 漂心纵向位置 m
  bool valid = false;
};

/// 船体整体浮力（对 x/y/z 的一阶矩）
struct HullProps {
  double volume = 0.0;
  double yMoment = 0.0;
  double zMoment = 0.0;
  double xMoment = 0.0;
};

HullGeom BuildHullGeom(const GzInput &_in);

/// 直立舷侧矩形剖面（宽 2b、高 D）被水线 z = z0 + tanφ·y 截断后的精确积分。
/// 分段线性 → 逐段解析积分，不做数值采样：既消除离散误差，也保证 GZ(0) 严格为 0。
SectionProps SectionBelowWaterline(double _halfB, double _depth, double _tanPhi, double _z0);

HullProps HullAt(const HullGeom &_g, double _tanPhi, double _z0);

/// 等体积水线：解 z0 使 HullAt(φ,z0).volume == _targetVolume
double SolveWaterlineZ0(const HullGeom &_g, double _phiDeg, double _targetVolume);

/// 单点 GZ（已持有几何，避免曲线/汇总时反复归一化站位）
double GzAt(const HullGeom &_g, double _phiDeg, double _KG);

}  // namespace detail

}  // namespace keship

#endif  // KESHIP_STABILITY_GZ_CORE_HH_
