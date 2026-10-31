/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 外部型值表（学生编程产出）—— 解析 / 校验 / 插值
 *
 * 用途：SD-10「型值表编程生成可浮船型」。学生程序在沙箱内算出一张
 *       「站位 x 水线 -> 半宽」表，经 stdout 以 CSV 交回平台；平台据此
 *       填 ShipHullSpec::offsets，由 ShipHullGenerator 在生成网格时
 *       改走查表分支（halfBreadthFunc），静水力/排水量/浮力随之自动跟随。
 *
 * 数据契约（学生侧，写进实验模板注释）：
 *   每行一个型值点，格式 x,z,halfBreadth，逗号分隔，单位 m
 *   只给右舷（halfBreadth >= 0），左舷由平台镜像
 *   行序不限；重复点、缺格、负半宽一律报错并指出行号
 *
 * 坐标约定与 ShipHullGenerator 一致（Gazebo Z_UP）：
 *   x 属于 [0, L] 艏向 · y 属于 [-B/2, B/2] · z 属于 [-D, 0]（水线 0，龙骨 -D）
 */

#ifndef KESHIP_HULL_OFFSETS_HH_
#define KESHIP_HULL_OFFSETS_HH_

#include <cstddef>
#include <string>
#include <vector>

#include "ShipHullGenerator.hh"   // LinesData

namespace keship {

/// 校验阈值（集中定义，便于单测与文档口径对齐）
struct OffsetsLimits {
  double volumeTolPct  = 2.0;     ///< V6 体积对拍容差 [%]
  double tipTolFrac    = 0.01;    ///< V5 端部半宽上限 = tipTolFrac * B
  int    minStations   = 21;      ///< 最小站位数（含两端）
  int    minWaterlines = 11;      ///< 最小水线数（含两端）
  std::size_t maxTriangles = 10000;  ///< 浮力网格三角数上限（与插件同口径）
};

/// 校验报告
struct OffsetsReport {
  bool ok = false;
  std::vector<std::string> errors;    ///< 每条含位置/行号，可直接显示给学生
  std::vector<std::string> warnings;  ///< 不阻断，但必须提示
  int nStations = 0;
  int nWaterlines = 0;
  double maxHalfBreadth = 0.0;
  int estTriangles = 0;               ///< 侧面 2*Nx*Nz*2 + 甲板 2*Nx
  double volumeFromTable = 0.0;       ///< 型值表积分体积 [m^3]
  double exactVolume = -1.0;          ///< 解析参考值；<0 表示无（自由题模式）
  double volumeErrPct = 0.0;          ///< (表积分 - 解析)/解析 * 100
};

/// 解析学生 stdout 的 CSV。成功返回 true 且 _out 可用；失败时 _errors 含行号。
bool ParseOffsetsCsv(const std::string &_text, LinesData &_out,
                     std::vector<std::string> &_errors);

/// 六条校验（V1/V2 由 ParseOffsetsCsv 保证，此处复核 V3~V6 与网格密度）
OffsetsReport ValidateOffsets(const LinesData &_d, double _L, double _B,
                              double _D, double _exactVolume,
                              const OffsetsLimits &_lim = OffsetsLimits{});

/// 双线性插值；区间外返回 0（保证船体封闭）
double SampleOffsetsBilinear(const LinesData &_d, double _x, double _z);

/// 型值表积分体积（梯形，与 ShipHullGenerator::computeHydroProps 同口径）
double VolumeFromOffsets(const LinesData &_d);

/// 解析参考体积：Wigley 经典族 = 4LBD/9（标准题模式的判分基准）
double WigleyExactVolume(double _L, double _B, double _D);

}  // namespace keship

#endif  // KESHIP_HULL_OFFSETS_HH_
