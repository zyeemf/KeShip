/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-04 初稳性交付物 IO（L3 链的机器可读出口）。
 *
 * ★ 2026-10-16 新增（张老师要求补齐 L3 链最后一个缺口）：
 * HS-04 倾斜实验的 GM 结论此前只显示在界面上，没有机器可读出口，MS-03
 * 无法与理论对照。本文件定义交付物结构与 JSON 读写（纯函数，可离屏测试）。
 *
 * 链判据（实验链与开发计划）：**后一节点的输入必须是前一节点产出的、可机器
 * 校验的文件交付物** —— HS-04 导出 `hs04_gm.json`，MS-03 导入后计算理论
 * 横摇周期 `T₀ = 2π√(Ixx/(Δ·GM))` 与实测峰值周期对照。
 */

#ifndef KESHIP_WAVE_INERTIA_IO_HH_
#define KESHIP_WAVE_INERTIA_IO_HH_

#include <string>

namespace keship {

/// 交付物携带的「船模信息」（张老师方案 2026-10-16）：导入方据此
/// **重建同一对象**，保证 L3 链「同一对象」的物理自洽。
struct GmObject {
  std::string type = "math_hull";  ///< "math_hull" | "ksm_model"
  std::string name;                 ///< 对象名（如 SP02_barge / ke0812_1）
  double lengthM = -1.0;
  double breadthM = -1.0;
  double heightM = -1.0;
  double densityKgM3 = -1.0;        ///< 材料密度（数学船型）
  std::string ksmFile;              ///< type=ksm_model 时的文件名
};

/// HS-04 初稳性交付物（纯数据，不依赖 Qt/Gazebo）
struct GmDeliverable {
  std::string model;           ///< 场景模型名（空 = 数学船型驳船）
  std::string exportedAt;      ///< ISO 时间戳
  double gmExperiment = -1.0;  ///< GM 实验值 (m，压载法线性拟合)
  double gmTheory = -1.0;      ///< GM 理论值 (m，船型参数计算)
  double displacementM3 = -1.0;  ///< 排水体积 (m³)
  double displacementKg = -1.0;  ///< 排水量 (kg)
  double massKg = -1.0;          ///< 船体质量 (kg)
  double ixxKgM2 = -1.0;         ///< 纵向转动惯量 (kg·m²)
  double cogZM = 0.0;            ///< 重心高度 (m)
  // ── 工况（供溯源）──
  double lengthM = -1.0;
  double breadthM = -1.0;
  double heightM = -1.0;
  double densityKgM3 = -1.0;   ///< 船体材料密度 (kg/m³)
  double waterDensity = 1025.0;
  double ballastOffsetY = 0.0;  ///< 记录时压载偏移 (m)
  std::string notes;

  GmObject object;   ///< 船模信息（可重建同一对象；旧文件可缺省）
};

/// 字段级校验：schema 版本 + 关键数值范围（GM/Δ/Ixx 必须为正）
bool ValidGmDeliverable(const GmDeliverable &_d, std::string *_err = nullptr);

/// 写交付物 JSON（失败时 _err 有原因）
bool WriteGmDeliverable(const std::string &_path, const GmDeliverable &_d,
                        std::string *_err = nullptr);

/// 读交付物 JSON（非法文件/缺字段返回 false 且 _err 有原因）
bool ReadGmDeliverable(const std::string &_path, GmDeliverable &_d,
                       std::string *_err = nullptr);

/// 由交付物计算理论横摇周期 T₀ = 2π√(Ixx/(Δ·GM))（s；数据不足返回 -1）
double TheoreticalRollPeriod(const GmDeliverable &_d);

}  // namespace keship

#endif  // KESHIP_WAVE_INERTIA_IO_HH_
