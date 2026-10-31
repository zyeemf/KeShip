/*
 * Copyright (C) 2024-2026 KeShip Team
 */
#include "HullOffsets.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <utility>

namespace keship {
namespace {

/// 坐标量化到 1e-6（学生按 6 位小数打印）—— 用于分组与去重
long long Q(double _v) { return static_cast<long long>(std::llround(_v * 1e6)); }

std::string Trim(const std::string &_s) {
  const char *ws = " \t\r\n";
  const auto b = _s.find_first_not_of(ws);
  if (b == std::string::npos) return std::string();
  const auto e = _s.find_last_not_of(ws);
  return _s.substr(b, e - b + 1);
}

bool ParseDouble(const std::string &_s, double &_v) {
  const std::string t = Trim(_s);
  if (t.empty()) return false;
  char *end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str()) return false;
  while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end != '\0') return false;
  if (!std::isfinite(v)) return false;
  _v = v;
  return true;
}

std::string Fmt(const char *_fmt, double _a) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), _fmt, _a);
  return std::string(buf);
}

}  // namespace

// ═══════════════════════════════════════════════════════════
// 解析：学生 stdout CSV -> LinesData
// ═══════════════════════════════════════════════════════════
bool ParseOffsetsCsv(const std::string &_text, LinesData &_out,
                     std::vector<std::string> &_errors) {
  _errors.clear();
  _out = LinesData{};

  struct Point {
    double x, z, y;
    int line;
  };
  std::vector<Point> pts;

  std::istringstream iss(_text);
  std::string raw;
  int lineNo = 0;
  while (std::getline(iss, raw)) {
    ++lineNo;
    const std::string line = Trim(raw);
    if (line.empty() || line[0] == '#') continue;

    std::vector<std::string> f;
    std::string cur;
    for (char c : line) {
      if (c == ',') {
        f.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    f.push_back(cur);

    if (f.size() != 3) {
      _errors.push_back("第 " + std::to_string(lineNo) + " 行：应为 3 列（x,z,halfBreadth），实际 " +
                        std::to_string(f.size()) + " 列");
      continue;
    }
    double x = 0.0, z = 0.0, y = 0.0;
    if (!ParseDouble(f[0], x) || !ParseDouble(f[1], z) || !ParseDouble(f[2], y)) {
      _errors.push_back("第 " + std::to_string(lineNo) + " 行：存在无法解析为数字的字段");
      continue;
    }
    pts.push_back({x, z, y, lineNo});
  }

  if (pts.empty()) {
    _errors.push_back("未读到任何型值点（stdout 为空或无有效数据行）");
    return false;
  }

  // V2 去重：同一 (x,z) 只能出现一次
  std::map<std::pair<long long, long long>, int> firstLine;
  std::vector<Point> uniq;
  for (const auto &p : pts) {
    const auto key = std::make_pair(Q(p.x), Q(p.z));
    const auto it = firstLine.find(key);
    if (it != firstLine.end()) {
      _errors.push_back("第 " + std::to_string(p.line) + " 行：坐标与第 " +
                        std::to_string(it->second) + " 行重复（x=" + Fmt("%.6f", p.x) +
                        ", z=" + Fmt("%.6f", p.z) + "）");
      continue;
    }
    firstLine[key] = p.line;
    uniq.push_back(p);
  }
  if (!_errors.empty()) return false;

  // 唯一站位（升序）与水线（降序：0 -> -D）
  std::vector<long long> qx, qz;
  for (const auto &p : uniq) {
    qx.push_back(Q(p.x));
    qz.push_back(Q(p.z));
  }
  std::sort(qx.begin(), qx.end());
  qx.erase(std::unique(qx.begin(), qx.end()), qx.end());
  std::sort(qz.begin(), qz.end(), std::greater<long long>());
  qz.erase(std::unique(qz.begin(), qz.end()), qz.end());

  _out.stations.clear();
  _out.waterlines.clear();
  for (long long v : qx) _out.stations.push_back(static_cast<double>(v) / 1e6);
  for (long long v : qz) _out.waterlines.push_back(static_cast<double>(v) / 1e6);

  const std::size_t nx = qx.size();
  const std::size_t nz = qz.size();

  // V1 网格完整：点数必须等于 nx*nz
  if (uniq.size() != nx * nz) {
    std::map<std::pair<long long, long long>, bool> has;
    for (const auto &p : uniq) has[std::make_pair(Q(p.x), Q(p.z))] = true;
    int reported = 0;
    for (std::size_t i = 0; i < nx && reported < 5; ++i) {
      for (std::size_t j = 0; j < nz && reported < 5; ++j) {
        if (has.find(std::make_pair(qx[i], qz[j])) == has.end()) {
          _errors.push_back("缺格：站位 x=" + Fmt("%.6f", qx[i] / 1e6) +
                            " 与水线 z=" + Fmt("%.6f", qz[j] / 1e6) +
                            " 处没有点（应 " + std::to_string(nx * nz) + " 点，实得 " +
                            std::to_string(uniq.size()) + " 点）");
          ++reported;
        }
      }
    }
    if (reported == 0) {
      _errors.push_back("点数与网格不匹配（应 " + std::to_string(nx * nz) + " 点，实得 " +
                        std::to_string(uniq.size()) + " 点），可能存在只出现在部分站位上的水线");
    }
    return false;
  }

  std::map<long long, int> xi, zj;
  for (std::size_t i = 0; i < nx; ++i) xi[qx[i]] = static_cast<int>(i);
  for (std::size_t j = 0; j < nz; ++j) zj[qz[j]] = static_cast<int>(j);

  _out.halfBreadths.assign(nx, std::vector<double>(nz, 0.0));
  for (const auto &p : uniq) {
    _out.halfBreadths[xi[Q(p.x)]][zj[Q(p.z)]] = p.y;
  }
  return true;
}

// ═══════════════════════════════════════════════════════════
// 校验 V3~V6 + 网格密度 + 三角数
// ═══════════════════════════════════════════════════════════
OffsetsReport ValidateOffsets(const LinesData &_d, double _L, double _B,
                              double _D, double _exactVolume,
                              const OffsetsLimits &_lim) {
  OffsetsReport r;
  if (_d.stations.size() < 2 || _d.waterlines.size() < 2) {
    r.errors.push_back("型值表维度不足（至少需要 2 站 x 2 水线）");
    return r;
  }

  const int nx = static_cast<int>(_d.stations.size()) - 1;
  const int nz = static_cast<int>(_d.waterlines.size()) - 1;
  r.nStations = static_cast<int>(_d.stations.size());
  r.nWaterlines = static_cast<int>(_d.waterlines.size());

  // 网格密度（阶段 0 量化：10x5 插值损失 Ix -2.16% / V -1.91%，故要求 >= 20x10）
  if (r.nStations < _lim.minStations) {
    r.errors.push_back("站位数 " + std::to_string(r.nStations) + " 不足，至少 " +
                       std::to_string(_lim.minStations) + "（含两端）");
  }
  if (r.nWaterlines < _lim.minWaterlines) {
    r.errors.push_back("水线数 " + std::to_string(r.nWaterlines) + " 不足，至少 " +
                       std::to_string(_lim.minWaterlines) + "（含两端）");
  }

  // V3 半宽范围
  double ymax = 0.0;
  const double yCap = 0.5 * _B * 1.001;
  int negCount = 0, overCount = 0;
  for (int i = 0; i <= nx; ++i) {
    for (int j = 0; j <= nz; ++j) {
      const double y = _d.halfBreadths[i][j];
      ymax = std::max(ymax, y);
      if (y < -1e-9) {
        if (++negCount <= 3) {
          r.errors.push_back("站位 x=" + Fmt("%.6f", _d.stations[i]) + " 水线 z=" +
                             Fmt("%.6f", _d.waterlines[j]) + "：半宽为负（" + Fmt("%.6f", y) +
                             "），只允许右舷非负值");
        }
      } else if (y > yCap) {
        if (++overCount <= 3) {
          r.errors.push_back("站位 x=" + Fmt("%.6f", _d.stations[i]) + " 水线 z=" +
                             Fmt("%.6f", _d.waterlines[j]) + "：半宽 " + Fmt("%.6f", y) +
                             " 超过 B/2=" + Fmt("%.6f", 0.5 * _B) +
                             "，检查是否漏了 /2（半宽 = B/2 * f）");
        }
      }
    }
  }
  if (negCount > 3) r.errors.push_back("共 " + std::to_string(negCount) + " 处负半宽（仅列前 3 处）");
  if (overCount > 3) r.errors.push_back("共 " + std::to_string(overCount) + " 处半宽超限（仅列前 3 处）");
  r.maxHalfBreadth = ymax;

  // V4 覆盖端点
  if (std::abs(_d.stations.front()) > 1e-6 || std::abs(_d.stations.back() - _L) > 1e-6) {
    r.errors.push_back("站位未覆盖 [0, L]：首 " + Fmt("%.6f", _d.stations.front()) +
                       "，末 " + Fmt("%.6f", _d.stations.back()) +
                       "（缺端点会导致船体端面开口）");
  }
  if (std::abs(_d.waterlines.front()) > 1e-6 || std::abs(_d.waterlines.back() + _D) > 1e-6) {
    r.errors.push_back("水线未覆盖 [-D, 0]：首 " + Fmt("%.6f", _d.waterlines.front()) +
                       "，末 " + Fmt("%.6f", _d.waterlines.back()) +
                       "（缺端点会导致船体底部/甲板开口）");
  }

  // V5 端部半宽趋零（艏艉必须收敛，否则无法闭合）
  const double tipTol = _lim.tipTolFrac * _B;
  for (int j = 0; j <= nz; ++j) {
    if (_d.halfBreadths[0][j] > tipTol) {
      r.errors.push_back("艉端 x=" + Fmt("%.6f", _d.stations.front()) + " 水线 z=" +
                         Fmt("%.6f", _d.waterlines[j]) + "：半宽 " +
                         Fmt("%.6f", _d.halfBreadths[0][j]) + " 未趋零（应 < " +
                         Fmt("%.6f", tipTol) + "）");
      break;
    }
  }
  for (int j = 0; j <= nz; ++j) {
    if (_d.halfBreadths[nx][j] > tipTol) {
      r.errors.push_back("艏端 x=" + Fmt("%.6f", _d.stations.back()) + " 水线 z=" +
                         Fmt("%.6f", _d.waterlines[j]) + "：半宽 " +
                         Fmt("%.6f", _d.halfBreadths[nx][j]) + " 未趋零（应 < " +
                         Fmt("%.6f", tipTol) + "）");
      break;
    }
  }

  // 三角数（侧面 2*Nx*Nz*2 + 甲板 2*Nx），超限则浮力被插件静默跳过
  r.estTriangles = 2 * nx * nz * 2 + 2 * nx;
  if (static_cast<std::size_t>(r.estTriangles) > _lim.maxTriangles) {
    r.errors.push_back("预估三角数 " + std::to_string(r.estTriangles) + " 超过浮力上限 " +
                       std::to_string(_lim.maxTriangles) + "，请降低网格密度");
  }

  // V6 体积对拍（仅标准题模式：exactVolume > 0）
  r.volumeFromTable = VolumeFromOffsets(_d);
  r.exactVolume = _exactVolume;
  if (_exactVolume > 1e-9) {
    r.volumeErrPct = 100.0 * (r.volumeFromTable - _exactVolume) / _exactVolume;
    if (std::abs(r.volumeErrPct) > _lim.volumeTolPct) {
      r.errors.push_back("体积 " + Fmt("%.6f", r.volumeFromTable) + " m3 与解析值 " +
                         Fmt("%.6f", _exactVolume) + " m3 偏差 " +
                         Fmt("%+.2f", r.volumeErrPct) + "%，超过 " +
                         Fmt("%.2f", _lim.volumeTolPct) +
                         "%（常见原因：半宽漏 /2、xi 漏乘 2、zeta 符号）");
    }
  } else {
    r.warnings.push_back("自由题模式：无解析参考体积，V6 不判定，仅报告体积 " +
                         Fmt("%.6f", r.volumeFromTable) + " m3");
  }

  r.ok = r.errors.empty();
  return r;
}

// ═══════════════════════════════════════════════════════════
// 双线性插值（区间外返回 0，保证封闭）
// ═══════════════════════════════════════════════════════════
double SampleOffsetsBilinear(const LinesData &_d, double _x, double _z) {
  const auto &xs = _d.stations;    // 升序
  const auto &zs = _d.waterlines;  // 降序：0 -> -D
  if (xs.size() < 2 || zs.size() < 2) return 0.0;
  if (_x < xs.front() - 1e-12 || _x > xs.back() + 1e-12) return 0.0;
  if (_z > zs.front() + 1e-12 || _z < zs.back() - 1e-12) return 0.0;

  std::size_t i = static_cast<std::size_t>(
      std::upper_bound(xs.begin(), xs.end(), _x) - xs.begin());
  i = (i == 0) ? 0 : i - 1;
  if (i >= xs.size() - 1) i = xs.size() - 2;

  std::size_t j = static_cast<std::size_t>(
      std::upper_bound(zs.begin(), zs.end(), _z, std::greater<double>()) - zs.begin());
  j = (j == 0) ? 0 : j - 1;
  if (j >= zs.size() - 1) j = zs.size() - 2;

  const double dx = xs[i + 1] - xs[i];
  const double dz = zs[j + 1] - zs[j];
  const double tx = (std::abs(dx) > 1e-12) ? (_x - xs[i]) / dx : 0.0;
  const double tz = (std::abs(dz) > 1e-12) ? (_z - zs[j]) / dz : 0.0;

  const auto &hb = _d.halfBreadths;
  return (1.0 - tx) * (1.0 - tz) * hb[i][j] + tx * (1.0 - tz) * hb[i + 1][j] +
         (1.0 - tx) * tz * hb[i][j + 1] + tx * tz * hb[i + 1][j + 1];
}

// ═══════════════════════════════════════════════════════════
// 型值表积分体积（梯形，与 computeHydroProps 同口径）
// ═══════════════════════════════════════════════════════════
double VolumeFromOffsets(const LinesData &_d) {
  const std::size_t nx = _d.stations.size();
  const std::size_t nz = _d.waterlines.size();
  if (nx < 2 || nz < 2) return 0.0;

  std::vector<double> areas(nx, 0.0);
  for (std::size_t i = 0; i < nx; ++i) {
    double a = 0.0;
    for (std::size_t j = 0; j + 1 < nz; ++j) {
      const double dz = std::abs(_d.waterlines[j + 1] - _d.waterlines[j]);
      a += 0.5 * (_d.halfBreadths[i][j] + _d.halfBreadths[i][j + 1]) * dz;
    }
    areas[i] = 2.0 * a;   // 左右舷
  }

  double v = 0.0;
  for (std::size_t i = 0; i + 1 < nx; ++i) {
    const double dx = _d.stations[i + 1] - _d.stations[i];
    v += 0.5 * (areas[i] + areas[i + 1]) * dx;
  }
  return v;
}

double WigleyExactVolume(double _L, double _B, double _D) {
  return 4.0 * _L * _B * _D / 9.0;
}

}  // namespace keship
