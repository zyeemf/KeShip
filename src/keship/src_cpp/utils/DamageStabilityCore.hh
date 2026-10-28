/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-07 破舱稳性仿真 —— 单舱进水后的残存稳性内核（纯计算，不依赖 Qt / gz）
 *
 * 方法：**损失浮力法**（lost buoyancy）。破损舱进水后该舱不再提供浮力，
 *   于是"完整船浸水体 − μ·破损舱浸水部分"必须仍等于原排水体积；
 *   解出新水线（吃水）与平衡横倾角后，残存 GZ 曲线与 GM 由同一套剖面几何算出。
 *
 * 为什么用损失浮力法而不是增加重量法：
 *   教学平台不给舱内自由液面几何，增加重量法需要额外假设自由液面惯性矩；
 *   损失浮力法把破口当作与舷外连通，天然没有"舱内自由液面"这一项，
 *   与本项目"只给舱室长方体 + 渗透率"的数据条件匹配。
 *
 * 关键假设（报告里逐条列出，不粉饰）：
 *   · 舱室为轴对齐长方体，且必须落在船体半宽包络内（否则直接判不可用，不外推）；
 *   · 破损后不发生二次进水/甲板淹湿，最终水线一旦高过甲板边缘即判 canFloat=false；
 *   · 纵倾用"浮心纵向移动量 / 纵稳性高"的小角线性估算，不做三维纵倾迭代；
 *   · 进水只按 μ 折减体积，不再单独计舱内自由液面（损失浮力法口径）。
 */

#ifndef KESHIP_DAMAGE_STABILITY_CORE_HH_
#define KESHIP_DAMAGE_STABILITY_CORE_HH_

#include <string>
#include <vector>

#include "StabilityGzCore.hh"

namespace keship {

/// 矩形舱室（船体坐标系：x 沿船长、y 横向、z 距龙骨向上；y>0 为右舷）
struct Compartment {
  std::string name;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;
  double permeability = 0.95;   // 渗透率 μ（0=不进水，1=完全可浸）
};

struct DamageResult {
  double floodedVolume = 0.0;     // 进水量 m³
  double newDraft = 0.0;          // 破舱后正浮吃水（若可正浮）；= 船中处水线高
  double heelDeg = 0.0;           // 破舱后横倾角（+ 右舷下沉）
  double trimDeg = 0.0;           // 纵倾角（+ 艏倾；线性估算，见文件头）
  double gmResidual = 0.0;        // 残存初稳性高（平衡横倾处 dGZ/dφ）
  double gzMaxResidual = 0.0;     // 残存 GZ 最大值（−89°~+89°）
  bool   canFloat = false;        // 是否能继续漂浮
  bool   oneCompartmentStandard = false;  // 是否满足"一舱不沉"
  std::string note;
};

/// 单舱破损计算。_floodedIndex < 0 表示**所有**舱室同时破损（负例/极限工况）。
/// 不可漂浮时不会返回"看起来正常"的残存 GM：相关字段置 0，并在 note 中写明原因。
DamageResult FloodOne(const GzInput &_intact,
                      const std::vector<Compartment> &_comps,
                      int _floodedIndex);

}  // namespace keship

#endif  // KESHIP_DAMAGE_STABILITY_CORE_HH_
