/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 标准载荷模型（model.config + model.sdf）解析 — 只取平台真正需要的三样 */

#ifndef KESHIP_PAYLOAD_MODEL_HH_
#define KESHIP_PAYLOAD_MODEL_HH_

#include <string>
#include <vector>

namespace keship
{

/// @brief 模型里的一个传感器（只取"是哪类"和"量测点在哪"）
struct PayloadSensorDef
{
  std::string gzType;              ///< model.sdf 的 <sensor type="...">
  double pose[6] = {0, 0, 0, 0, 0, 0};   ///< 量测点相对安装面（link 原点），x y z r p y
};

/// @brief 一个标准载荷模型目录的解析结果
///
/// 接口契约（2026-11-01 定，张老师）：平台**只读**这三样，其余一律忽略并由平台覆盖：
///   1. 首个 <link> 的首个 <visual> 的 mesh uri
///   2. 该 <visual> 自己的 <pose>（缺省 0）
///   3. 该 <link> 下每个 <sensor> 的 type 与 <pose>
/// `<update_rate>` / `<samples>` / `<image>` 等参数**一律不读**（默认值由 KeShip 定）。
struct PayloadModelDef
{
  bool valid = false;
  std::string dir;                     ///< 模型目录绝对路径
  std::string modelName;               ///< model.config 的 <name>
  std::string meshPath;                ///< mesh 绝对路径（空 = 该模型无网格）
  bool meshAlreadyZUp = true;          ///< 目录约定：payload 模型为 Z-up（不做 Y→Z 旋转）
  double visualPose[6] = {0, 0, 0, 0, 0, 0};
  std::vector<PayloadSensorDef> sensors;
};

/// @brief 解析一个标准模型目录
/// @param _dir 形如 <models_all>/payload_model/PAY-LIDAR
/// @param _out 解析结果；失败时 valid = false
/// @return 是否成功（model.sdf 存在且能解析出 <model>）
bool LoadPayloadModel(const std::string &_dir, PayloadModelDef &_out);

/// @brief 把 model.sdf 的 sensor type 归一到小写规范名（"gpu_lidar"→"gpu_lidar"）
///        只做格式归一，不做平台类型映射 —— 映射在 ModelManager 里维护。
std::string NormalizeGzSensorType(const std::string &_gzType);

/// @brief 解析 posix 风格的 6 元 pose 字符串；字段不足时保持缺省值
bool ParsePose6(const std::string &_text, double _out[6]);

}  // namespace keship

#endif
