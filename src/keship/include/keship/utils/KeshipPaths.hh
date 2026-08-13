/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* KeShip 运行时数据路径统一管理 — 全部位于 keship_ws/data/ 下，便于整体迁移 */

#ifndef KESHIP_PATHS_HH_
#define KESHIP_PATHS_HH_

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace keship
{
  /// keship_ws 根目录（优先 $HOME/keship_ws，兜底 /home/keship/keship_ws）
  inline std::string KeshipHomeWsDir()
  {
    const char *home = std::getenv("HOME");
    return home ? (std::string(home) + "/keship_ws")
                : "/home/keship/keship_ws";
  }

  /// 统一运行时数据根目录
  inline std::string KeshipDataDir()
  {
    return KeshipHomeWsDir() + "/data";
  }

  /// 水区配置（GUI 写入 / 插件启动+热更新读取）
  inline std::string KeshipWaterAreasFile()
  {
    return KeshipDataDir() + "/keship_water_areas.json";
  }

  /// GUI 设置
  inline std::string KeshipSettingsFile()
  {
    return KeshipDataDir() + "/settings.json";
  }

  /// 数学船模 OBJ（原 ~/.keship/models/）
  inline std::string KeshipModelsDir()
  {
    return KeshipDataDir() + "/models/";
  }

  /// 统一模型资源根目录（models_all/，含 ks_model/ks_world/robot_model）
  inline std::string KeshipAllModelsDir()
  {
    return KeshipHomeWsDir() + "/models_all";
  }

  /// 模型库目录（models_all/ks_model/，原 models_ksm/，.ksm 散文件）
  inline std::string KeshipKsmDir()
  {
    return KeshipAllModelsDir() + "/ks_model";
  }

  /// 原始模型目录（models_all/robot_model/，原 models_robot/ + models_env/，.dae/.stl）
  inline std::string KeshipRobotModelDir()
  {
    return KeshipAllModelsDir() + "/robot_model";
  }

  /// 实验任务目录（keship_ws/task/，.toml）
  inline std::string KeshipTaskDir()
  {
    return KeshipHomeWsDir() + "/task";
  }

  /// KSW 世界清单目录（models_all/ks_world/，原 models_ksw/）
  inline std::string KeshipWorldsDir()
  {
    return KeshipAllModelsDir() + "/ks_world";
  }

  /// SP-03 静水稳性实验临时目录（原 ~/.keship/sp03/）
  inline std::string KeshipSp03Dir()
  {
    return KeshipDataDir() + "/experiments/sp03/";
  }

  /// 排水量验证实验临时目录（原 ~/.keship/disp/）
  inline std::string KeshipDispDir()
  {
    return KeshipDataDir() + "/experiments/disp/";
  }

  /// 波浪视觉 OBJ 临时目录（原 /tmp/keship/waves/）
  inline std::string KeshipWavesDir()
  {
    return KeshipDataDir() + "/waves/";
  }

  /// DEBUG 预览图目录（原 /tmp/keship_2d_preview_*）
  inline std::string KeshipPreviewDir()
  {
    return KeshipDataDir() + "/preview/";
  }

  /// 测试工具输出目录（原 /tmp/test_*）
  inline std::string KeshipTestDir()
  {
    return KeshipDataDir() + "/test/";
  }

  /// 确保全部数据子目录存在
  inline void EnsureKeshipDataDirs()
  {
    std::error_code ec;
    std::filesystem::create_directories(KeshipModelsDir(), ec);
    std::filesystem::create_directories(KeshipSp03Dir(), ec);
    std::filesystem::create_directories(KeshipDispDir(), ec);
    std::filesystem::create_directories(KeshipWavesDir(), ec);
    std::filesystem::create_directories(KeshipPreviewDir(), ec);
    std::filesystem::create_directories(KeshipTestDir(), ec);
    std::filesystem::create_directories(KeshipWorldsDir(), ec);
    std::filesystem::create_directories(KeshipKsmDir(), ec);
    std::filesystem::create_directories(KeshipRobotModelDir(), ec);
    std::filesystem::create_directories(KeshipTaskDir(), ec);
  }
} // namespace keship

#endif  // KESHIP_PATHS_HH_
