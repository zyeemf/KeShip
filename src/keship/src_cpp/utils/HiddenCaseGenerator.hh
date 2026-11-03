/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 隐藏验收数据生成器：随机合成横摇时程 + 平台算法基准值（MS-06 防作弊核心） */

#ifndef KESHIP_HIDDEN_CASE_GENERATOR_HH_
#define KESHIP_HIDDEN_CASE_GENERATOR_HH_

#include <string>
#include <vector>

#include "WaveResponseCore.hh"

namespace keship {

/// MS-06 学生代码读取的数据文件（可见轮与隐藏组共用同一路径）
constexpr const char *kRoundDataPath = "/tmp/ms06_round_data.csv";

/// 一组隐藏验收数据
struct HiddenWaveCase {
  double period = 5.0;            ///< 波浪周期 (s)
  double waveAmp = 0.5;           ///< 波浪波幅 (m)
  double genRao = 10.0;           ///< 生成时用的“真值” RAO（仅调试展示用）
  std::vector<WaveSample> trail;  ///< 合成横摇时程（60 s @ 0.1 s）
  double refRao = -1.0;           ///< 平台算法（computeMetrics）在该数据上的结果 = 验收基准
};

/// 生成 _count 组随机隐藏数据（同 seed 可复现）
/// 随机范围：周期 3~8 s、波幅 0.3~0.8 m、生成 RAO 5~25 °/m、相位随机、噪声 ≤2%
std::vector<HiddenWaveCase> GenerateHiddenCases(int _count, unsigned _seed);

/// 把一轮数据写成学生代码可读的格式：首行 "wave_amp=<m>"，其后每行 "t,roll"
bool WriteRoundDataCsv(const std::vector<WaveSample> &_trail, double _waveAmp,
                       const std::string &_path);

}  // namespace keship

#endif
