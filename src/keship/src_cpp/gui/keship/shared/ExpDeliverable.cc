/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 通用实验交付物读写 —— 实现 */

#include "ExpDeliverable.hh"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>

namespace keship {

std::string NowIso8601()
{
  return QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
}

QJsonObject ExpToJson(const ExpDeliverable &_d)
{
  QJsonObject o;
  o["schema"] = QString::fromStdString(_d.schema);
  o["expId"] = QString::fromStdString(_d.expId);
  o["createdAt"] = QString::fromStdString(
      _d.createdAt.empty() ? NowIso8601() : _d.createdAt);
  o["object"] = _d.object;
  o["condition"] = _d.condition;
  o["params"] = _d.params;
  o["metrics"] = _d.metrics;
  o["extra"] = _d.extra;
  return o;
}

bool ExpFromJson(const QJsonObject &_obj, ExpDeliverable &_d, std::string *_err)
{
  auto fail = [&](const std::string &_m) {
    if (_err) *_err = _m;
    return false;
  };

  if (!_obj.contains("schema") || !_obj["schema"].isString())
    return fail("缺少 schema 字段");
  _d.schema = _obj["schema"].toString().toStdString();
  _d.expId = _obj["expId"].toString().toStdString();
  _d.createdAt = _obj["createdAt"].toString().toStdString();

  if (!_obj["object"].isObject() || !_obj["metrics"].isObject())
    return fail("object / metrics 段缺失或类型错误");

  _d.object = _obj["object"].toObject();
  _d.condition = _obj["condition"].toObject();
  _d.params = _obj["params"].toObject();
  _d.metrics = _obj["metrics"].toObject();
  _d.extra = _obj["extra"].toObject();
  return true;
}

bool WriteExpDeliverable(const std::string &_path, const ExpDeliverable &_d,
                         std::string *_err)
{
  QFile f(QString::fromStdString(_path));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (_err) *_err = "无法写入文件: " + _path;
    return false;
  }
  const QJsonDocument doc(ExpToJson(_d));
  f.write(doc.toJson(QJsonDocument::Indented));
  f.close();
  return true;
}

bool ReadExpDeliverable(const std::string &_path, ExpDeliverable &_d,
                        std::string *_err)
{
  QFile f(QString::fromStdString(_path));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (_err) *_err = "无法读取文件: " + _path;
    return false;
  }
  const QByteArray raw = f.readAll();
  f.close();

  QJsonParseError pe{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (_err) *_err = "JSON 解析失败: " + pe.errorString().toStdString();
    return false;
  }
  if (!doc.isObject()) {
    if (_err) *_err = "JSON 顶层不是对象";
    return false;
  }
  return ExpFromJson(doc.object(), _d, _err);
}

bool ValidExpDeliverable(const ExpDeliverable &_d,
                         const std::string &_expectExpId, std::string *_err)
{
  auto fail = [&](const std::string &_m) {
    if (_err) *_err = _m;
    return false;
  };

  if (_d.schema != kExpSchemaV1)
    return fail("schema 版本不符：期望 " + std::string(kExpSchemaV1) +
                "，实为 " + _d.schema);
  if (_d.expId != _expectExpId)
    return fail("交付物属于 " + _d.expId + "，本实验需要 " + _expectExpId);
  if (_d.object.isEmpty())
    return fail("object 段为空");
  if (_d.metrics.isEmpty())
    return fail("metrics 段为空");
  return true;
}

}  // namespace keship
