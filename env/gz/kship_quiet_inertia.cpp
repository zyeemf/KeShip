/*
 * kship_quiet_inertia.cpp —— KeShip 专用：静默 DART 的「非法空间惯性」告警
 *
 * 背景（2026-10-07）：
 *   DART 的 dart::dynamics::Inertia 语义是「刚体参数 (m,c,I)」，而附加质量 M_A
 *   是各向异性对称阵 ⇒ M_RB + M_A 不再是刚体惯性形式 ⇒ verifySpatialTensor 告警。
 *   该告警**已判定良性**（求解器用的是原始 6×6，M_A 正确生效）：
 *   见 doc/audit/inertia_warning_2026-10-07.md、doc/plans/s3_draft_dependent_hydro_plan.md §7.0/§7.1。
 *
 * 做法：LD_PRELOAD 覆盖 dart::common::colorErr（libdart 导出符号），
 *       吞掉**整个 Inertia.cpp** 发出的告警（该文件只做惯性参数校验），
 *       **其余 DART 输出原样透传**（前缀自行复现，避免依赖 dlsym 成功）。
 *
 * ⚠ 副作用：Inertia.cpp 的**其他**告警（如 setMoment 的「力矩非法」）也会被吞。
 *   本工程场景下力矩合法、不触发；若将来要保留，需按 `_line` 细分白名单。
 *
 * 编译：g++ -shared -fPIC -O2 -o kship_quiet_inertia.so kship_quiet_inertia.cpp -ldl
 * 使用：export LD_PRELOAD=/path/kship_quiet_inertia.so
 */
#include <dlfcn.h>
#include <iostream>
#include <sstream>
#include <string>

namespace dart
{
namespace common
{

typedef std::ostream &(*ColorErrFn)(const std::string &, const std::string &,
                                    unsigned int, int);

/// \brief 与 libdart 同名同签名 —— LD_PRELOAD 下本定义优先。
std::ostream &colorErr(const std::string &_msg, const std::string &_file,
                       unsigned int _line, int _color)
{
  static ColorErrFn orig = nullptr;
  static bool resolved = false;
  if (!resolved)
  {
    resolved = true;
    orig = reinterpret_cast<ColorErrFn>(dlsym(RTLD_NEXT,
        "_ZN4dart6common8colorErrERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES8_ji"));
  }

  /// 吞掉 Inertia.cpp 的全部告警（KeShip 已判定该类告警良性）
  if (_file.find("Inertia.cpp") != std::string::npos)
  {
    static std::ostringstream sink;
    return sink;
  }

  if (orig)
    return orig(_msg, _file, _line, _color);

  /// 回退：自行复现 DART 的前缀格式（无颜色），保证其余输出不丢
  const std::string::size_type slash = _file.find_last_of("/\\");
  const std::string base =
      (slash == std::string::npos) ? _file : _file.substr(slash + 1);
  std::cerr << _msg << " [" << base << ":" << _line << "] ";
  return std::cerr;
}

} // namespace common
} // namespace dart
