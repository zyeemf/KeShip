/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型驳船工厂（HS-04 倾斜实验与 MS-03/MS-06 交付物导入共用）。
 *
 * ★ 2026-10-16 新增（张老师方案）：GM 交付物携带「船模信息」，MS-03 导入时
 * 按**同一参数重建同一对象** —— 保证 L3 链「同一对象」的物理自洽，且未来可
 * 扩展到任意船型（数学船型参数 / ksm 文件引用）。
 *
 * 驳船 = 长方体 L×B×H，材料密度 _density，几何中心在原点（半浸平衡位置）；
 * 含长方体**解析线性阻尼**（A_wp = L·B 精确，见 HS-04 makeBargeSDF 原注释）。
 */

#ifndef KESHIP_BARGE_FACTORY_HH_
#define KESHIP_BARGE_FACTORY_HH_

#include <string>

namespace keship {

/// 长方体驳船的质量与转动惯量（与 SDF 内 <inertial> 完全一致；供交付物填写）
void BargeMassInertia(double _L, double _B, double _H, double _density,
                      double &_mass, double &_ixx, double &_iyy, double &_izz);

/// 生成驳船 SDF（含浮力插件与线性阻尼参数）
std::string MakeBargeSDF(const std::string &_name, double _L, double _B,
                         double _H, double _density,
                         double _fluidDensity = 1000.0,
                         double _gravity = 9.81);

}  // namespace keship

#endif  // KESHIP_BARGE_FACTORY_HH_
