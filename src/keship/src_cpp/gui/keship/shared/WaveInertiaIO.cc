/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-04 初稳性交付物 IO 实现（QJsonDocument）。见 WaveInertiaIO.hh 文件头。 */

#include "WaveInertiaIO.hh"

#include <cmath>
#include <fstream>
#include <sstream>

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace keship {
namespace {

constexpr const char *kSchema = "keship.hs04.gm.v1";

QJsonObject toJson(const GmDeliverable &_d) {
  QJsonObject o;
  o["schema"] = kSchema;
  o["model"] = QString::fromStdString(_d.model);
  o["exported_at"] = QString::fromStdString(_d.exportedAt);
  o["gm_experiment_m"] = _d.gmExperiment;
  o["gm_theory_m"] = _d.gmTheory;
  o["displacement_m3"] = _d.displacementM3;
  o["displacement_kg"] = _d.displacementKg;
  o["mass_kg"] = _d.massKg;
  o["ixx_kg_m2"] = _d.ixxKgM2;
  o["cog_z_m"] = _d.cogZM;
  o["length_m"] = _d.lengthM;
  o["breadth_m"] = _d.breadthM;
  o["height_m"] = _d.heightM;
  o["density_kg_m3"] = _d.densityKgM3;
  o["water_density"] = _d.waterDensity;
  o["ballast_offset_y"] = _d.ballastOffsetY;
  o["notes"] = QString::fromStdString(_d.notes);
  QJsonObject obj;
  obj["type"] = QString::fromStdString(_d.object.type);
  obj["name"] = QString::fromStdString(_d.object.name);
  obj["length_m"] = _d.object.lengthM;
  obj["breadth_m"] = _d.object.breadthM;
  obj["height_m"] = _d.object.heightM;
  obj["density_kg_m3"] = _d.object.densityKgM3;
  obj["ksm_file"] = QString::fromStdString(_d.object.ksmFile);
  o["object"] = obj;
  return o;
}

bool num(const QJsonObject &_o, const char *_key, double &_out) {
  const auto it = _o.find(_key);
  if (it == _o.end() || !it->isDouble()) return false;
  _out = it->toDouble();
  return true;
}

bool str(const QJsonObject &_o, const char *_key, std::string &_out) {
  const auto it = _o.find(_key);
  if (it == _o.end() || !it->isString()) return false;
  _out = it->toString().toStdString();
  return true;
}

}  // namespace

bool ValidGmDeliverable(const GmDeliverable &_d, std::string *_err) {
  auto fail = [&_err](const std::string &_m) {
    if (_err) *_err = _m;
    return false;
  };
  if (!(_d.gmExperiment > 0.0) && !(_d.gmTheory > 0.0))
    return fail("GM 实验值与理论值均非正（倾斜实验未完成？）");
  if (!(_d.displacementKg > 0.0))
    return fail("排水量非正");
  if (!(_d.ixxKgM2 > 0.0))
    return fail("纵向转动惯量 Ixx 非正（无法计算理论横摇周期）");
  return true;
}

bool WriteGmDeliverable(const std::string &_path, const GmDeliverable &_d,
                        std::string *_err) {
  QFile f(QString::fromStdString(_path));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (_err) *_err = "无法打开文件写入: " + _path;
    return false;
  }
  const QJsonDocument doc(toJson(_d));
  f.write(doc.toJson(QJsonDocument::Indented));
  f.close();
  return true;
}

bool ReadGmDeliverable(const std::string &_path, GmDeliverable &_d,
                       std::string *_err) {
  QFile f(QString::fromStdString(_path));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (_err) *_err = "无法打开文件: " + _path;
    return false;
  }
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
  f.close();
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    if (_err) *_err = "JSON 解析失败: " + pe.errorString().toStdString();
    return false;
  }
  const QJsonObject o = doc.object();
  std::string schema;
  if (!str(o, "schema", schema) || schema != kSchema) {
    if (_err) *_err = "schema 不符（期望 " + std::string(kSchema) + "）";
    return false;
  }
  GmDeliverable d;
  str(o, "model", d.model);
  str(o, "exported_at", d.exportedAt);
  str(o, "notes", d.notes);
  num(o, "gm_experiment_m", d.gmExperiment);
  num(o, "gm_theory_m", d.gmTheory);
  num(o, "displacement_m3", d.displacementM3);
  num(o, "displacement_kg", d.displacementKg);
  num(o, "mass_kg", d.massKg);
  num(o, "ixx_kg_m2", d.ixxKgM2);
  num(o, "cog_z_m", d.cogZM);
  num(o, "length_m", d.lengthM);
  num(o, "breadth_m", d.breadthM);
  num(o, "height_m", d.heightM);
  num(o, "density_kg_m3", d.densityKgM3);
  num(o, "water_density", d.waterDensity);
  num(o, "ballast_offset_y", d.ballastOffsetY);
  // object 段（可选；缺省时用顶层船型参数兜底，兼容旧文件）
  if (o.contains("object") && o.value("object").isObject()) {
    const QJsonObject obj = o.value("object").toObject();
    str(obj, "type", d.object.type);
    str(obj, "name", d.object.name);
    num(obj, "length_m", d.object.lengthM);
    num(obj, "breadth_m", d.object.breadthM);
    num(obj, "height_m", d.object.heightM);
    num(obj, "density_kg_m3", d.object.densityKgM3);
    str(obj, "ksm_file", d.object.ksmFile);
  } else {
    d.object.type = "math_hull";
    d.object.lengthM = d.lengthM;
    d.object.breadthM = d.breadthM;
    d.object.heightM = d.heightM;
    d.object.densityKgM3 = d.densityKgM3;
  }
  _d = d;
  return true;
}

double TheoreticalRollPeriod(const GmDeliverable &_d) {
  if (!(_d.gmExperiment > 0.0) && !(_d.gmTheory > 0.0)) return -1.0;
  if (!(_d.displacementKg > 0.0) || !(_d.ixxKgM2 > 0.0)) return -1.0;
  const double gm = (_d.gmExperiment > 0.0) ? _d.gmExperiment : _d.gmTheory;
  if (!(gm > 0.0)) return -1.0;
  return 2.0 * M_PI * std::sqrt(_d.ixxKgM2 / (_d.displacementKg * gm));
}

}  // namespace keship
