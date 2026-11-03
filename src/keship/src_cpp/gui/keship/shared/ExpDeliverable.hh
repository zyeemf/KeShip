/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 通用实验交付物读写（schema: keship.exp.v1）
 *
 * 为什么要这个模块（2026-10-27）：
 *   链判据要求「后一节点的输入，必须是前一节点产出的、可机器校验的文件」。
 *   项目此前只有 GmDeliverable（HS-04 → MS-03）一处**专用**交付物，
 *   八链补齐中还有 SD-08 → HS-05、SC-08 → SC-06、RP-05 → SD-08 等多条跨实验交付，
 *   逐个写一套序列化必然字段漂移。本模块把 GmDeliverable 的做法泛化为一套统一格式。
 *
 * 设计约束：
 *   ① **校验 expId**：下游读错实验的交付物必须被拒绝（防"串号"静默用错数据）；
 *   ② 只依赖 Qt（QJsonDocument）⇒ 放 shared/，不放 utils/（`keship_ctl` 不链 Qt）；
 *   ③ 字段分四段 + extra：object（对象）/ condition（工况）/ params（参数）/
 *      metrics（指标）/ extra（实验特有，如型值表）。
 */

#ifndef KESHIP_EXP_DELIVERABLE_HH_
#define KESHIP_EXP_DELIVERABLE_HH_

#include <string>

#include <QJsonObject>

namespace keship {

/// 交付物 schema 版本。字段结构变更须升版本号，并让 Valid 拒绝旧版本。
constexpr const char *kExpSchemaV1 = "keship.exp.v1";

/// 通用实验交付物
struct ExpDeliverable {
  std::string schema = kExpSchemaV1;  ///< 固定 "keship.exp.v1"
  std::string expId;                  ///< 如 "SD-08" —— 下游按此校验
  std::string createdAt;              ///< ISO 8601

  QJsonObject object;     ///< 对象：船型/模型（type/name/L/B/D/rho…）
  QJsonObject condition;  ///< 工况：海况/波高/周期/风/吃水…
  QJsonObject params;     ///< 输入参数：实验自身的可调参数
  QJsonObject metrics;    ///< 输出指标：可被下游直接引用的量化结果
  QJsonObject extra;      ///< 实验特有载荷（如型值表 stations/waterlines/halfBreadths）
};

/// 当前本地时间 → ISO 8601（秒精度）
std::string NowIso8601();

/// 组装为 JSON 对象
QJsonObject ExpToJson(const ExpDeliverable &_d);

/// 从 JSON 对象解析（不校验 expId）
bool ExpFromJson(const QJsonObject &_obj, ExpDeliverable &_d, std::string *_err);

/// 写文件（失败时 *_err 给可读原因）
bool WriteExpDeliverable(const std::string &_path, const ExpDeliverable &_d,
                         std::string *_err);

/// 读文件（失败时 *_err 给可读原因）
bool ReadExpDeliverable(const std::string &_path, ExpDeliverable &_d,
                        std::string *_err);

/// 校验：schema 版本 + expId 必须匹配
bool ValidExpDeliverable(const ExpDeliverable &_d,
                         const std::string &_expectExpId, std::string *_err);

}  // namespace keship

#endif  // KESHIP_EXP_DELIVERABLE_HH_
