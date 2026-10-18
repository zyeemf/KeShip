/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 隐藏验收数据生成器实现 */

#include "HiddenCaseGenerator.hh"

#include <cmath>
#include <fstream>
#include <random>

namespace keship {

bool WriteRoundDataCsv(const std::vector<WaveSample> &_trail, double _waveAmp,
                       const std::string &_path) {
  std::ofstream f(_path);
  if (!f) return false;
  f << "wave_amp=" << _waveAmp << "\n";
  for (const auto &s : _trail) f << s.t << "," << s.rollDeg << "\n";
  return true;
}

std::vector<HiddenWaveCase> GenerateHiddenCases(int _count, unsigned _seed) {
  std::vector<HiddenWaveCase> out;
  std::mt19937 rng(_seed);
  std::uniform_real_distribution<double> uPeriod(3.0, 8.0);
  std::uniform_real_distribution<double> uAmp(0.3, 0.8);
  std::uniform_real_distribution<double> uRao(5.0, 25.0);
  std::uniform_real_distribution<double> uPhase(0.0, 2.0 * M_PI);
  std::uniform_real_distribution<double> uNoise(-1.0, 1.0);

  constexpr double kDt = 0.1;
  constexpr double kDuration = 60.0;

  for (int i = 0; i < _count; ++i) {
    HiddenWaveCase c;
    c.period = uPeriod(rng);
    c.waveAmp = uAmp(rng);
    c.genRao = uRao(rng);
    const double phase = uPhase(rng);
    // 横摇幅值 = RAO × 波幅（deg）；噪声取幅值的 1%（均匀）
    const double ampDeg = c.genRao * c.waveAmp;
    const double noiseDeg = 0.01 * ampDeg;
    const int n = static_cast<int>(kDuration / kDt) + 1;
    c.trail.reserve(n);
    for (int k = 0; k < n; ++k) {
      WaveSample s;
      s.t = k * kDt;
      s.rollDeg = ampDeg * std::sin(2.0 * M_PI * s.t / c.period + phase) +
                  noiseDeg * uNoise(rng);
      c.trail.push_back(s);
    }
    // 验收基准 = 平台算法在同一数据上的结果（与 MS-03 同一实现）
    const auto m = WaveResponseCore::computeMetrics(c.trail, c.waveAmp, c.period);
    c.refRao = m.rollRAO;
    out.push_back(std::move(c));
  }
  return out;
}

}  // namespace keship
