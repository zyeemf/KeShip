/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型驳船工厂实现。见 BargeFactory.hh 文件头。 */

#include "BargeFactory.hh"

#include <sstream>

#include "keship/utils/LinearDamping.hh"

namespace keship {

void BargeMassInertia(double _L, double _B, double _H, double _density,
                      double &_mass, double &_ixx, double &_iyy, double &_izz) {
  _mass = _density * _L * _B * _H;
  _ixx = _mass / 12.0 * (_B * _B + _H * _H);
  _iyy = _mass / 12.0 * (_L * _L + _H * _H);
  _izz = _mass / 12.0 * (_L * _L + _B * _B);
}

std::string MakeBargeSDF(const std::string &_name, double _L, double _B,
                         double _H, double _density, double _fluidDensity,
                         double _gravity) {
  double mass = 0.0, ixx = 0.0, iyy = 0.0, izz = 0.0;
  BargeMassInertia(_L, _B, _H, _density, mass, ixx, iyy, izz);

  /// 线性阻尼（2026-09-19 新增，与 HS-02 的 7.5 ① 同一修法）：
  /// 长方体走不了插件的 pose_based 分支（该分支需 hasCylinderGeometry_），
  /// 零阻尼会让横摇 60 s 不衰减、判稳判据永不满足。
  /// 长方体水线面恒为矩形 → A_wp = L·B 精确。
  const double A_wp = _L * _B;
  const keship::LinearDampingCoeffs damp = keship::MakeLinearDamping(
      A_wp, _L, _B, mass, ixx, iyy, 0.15, 0.10, _fluidDensity, _gravity);

  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.6\">\n"
      << "  <model name=\"" << _name << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <inertial>\n"
      << "        <mass>" << mass << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>" << ixx << "</ixx>\n"
      << "          <ixy>0</ixy>\n"
      << "          <ixz>0</ixz>\n"
      << "          <iyy>" << iyy << "</iyy>\n"
      << "          <iyz>0</iyz>\n"
      << "          <izz>" << izz << "</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << _L << " " << _B << " " << _H << "</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "        <material>\n"
      << "          <ambient>0.7 0.5 0.3 1.0</ambient>\n"
      << "          <diffuse>0.7 0.5 0.3 1.0</diffuse>\n"
      << "        </material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << _L << " " << _B << " " << _H << "</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
      << " name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <link_name>base_link</link_name>\n"
      << "      <enable_buoyancy>true</enable_buoyancy>\n"
      << "      <enable_hydro>true</enable_hydro>\n"
      << "      <linear_damping>\n"
      << "        <Zw>" << damp.Zw << "</Zw>\n"
      << "        <Kp>" << damp.Kp << "</Kp>\n"
      << "        <Mq>" << damp.Mq << "</Mq>\n"
      << "      </linear_damping>\n"
      << "      <fluid_density>" << _fluidDensity << "</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <buoyancy name=\"barge_hull\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << _L << " " << _B << " " << _H << "</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "  </model>\n"
      << "</sdf>\n";
  return sdf.str();
}

}  // namespace keship
