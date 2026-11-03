/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 标准载荷模型解析实现 — tinyxml2，只读 mesh / <visual><pose> / <sensor type+pose> */

#include "PayloadModel.hh"

#include <tinyxml2.h>

#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace keship
{

namespace
{
namespace fs = std::filesystem;

/// 取元素的 <pose> 子节点文本（没有则返回空串）
std::string PoseTextOf(const tinyxml2::XMLElement *_elem)
{
  if (_elem == nullptr)
    return "";
  const tinyxml2::XMLElement *p = _elem->FirstChildElement("pose");
  if (p == nullptr || p->GetText() == nullptr)
    return "";
  return p->GetText();
}

/// 按 model://<name>/ 或相对路径解析成绝对路径
std::string ResolveMeshUri(const std::string &_uri, const std::string &_modelDir)
{
  if (_uri.empty())
    return "";
  std::string rel = _uri;
  if (rel.rfind("model://", 0) == 0)
  {
    rel = rel.substr(8);                 // 去掉 "model://"
    auto slash = rel.find('/');
    if (slash != std::string::npos)
      rel = rel.substr(slash + 1);       // 去掉 "<模型名>/"，剩下 meshes/xxx.glb
    else
      rel.clear();
  }
  if (rel.empty())
    return "";
  fs::path p(rel);
  if (!p.is_absolute())
    p = fs::path(_modelDir) / p;
  std::error_code ec;
  const fs::path canon = fs::weakly_canonical(p, ec);
  return ec ? p.lexically_normal().string() : canon.string();
}
}  // namespace

//////////////////////////////////////////////////////////////////////////////////
bool ParsePose6(const std::string &_text, double _out[6])
{
  if (_text.empty())
    return false;
  std::istringstream iss(_text);
  double v[6];
  int n = 0;
  while (n < 6 && (iss >> v[n]))
    n++;
  if (n == 0)
    return false;
  for (int i = 0; i < n; i++)
    _out[i] = v[i];
  for (int i = n; i < 6; i++)
    _out[i] = 0.0;                       // 缺省补齐（SDF 允许只写 3 位）
  return true;
}

//////////////////////////////////////////////////////////////////////////////////
std::string NormalizeGzSensorType(const std::string &_gzType)
{
  std::string out;
  for (char c : _gzType)
  {
    if (c == ' ' || c == '\t')
      continue;
    out += (char)std::tolower((unsigned char)c);
  }
  return out;
}

//////////////////////////////////////////////////////////////////////////////////
bool LoadPayloadModel(const std::string &_dir, PayloadModelDef &_out)
{
  _out = PayloadModelDef();
  _out.dir = _dir;
  std::error_code ec;
  const fs::path sdfPath = fs::path(_dir) / "model.sdf";
  if (!fs::exists(sdfPath, ec))
  {
    std::cerr << "LoadPayloadModel: 缺少 model.sdf: " << sdfPath.string() << std::endl;
    return false;
  }

  tinyxml2::XMLDocument doc;
  if (doc.LoadFile(sdfPath.string().c_str()) != tinyxml2::XML_SUCCESS)
  {
    std::cerr << "LoadPayloadModel: model.sdf 解析失败: " << sdfPath.string() << std::endl;
    return false;
  }

  const tinyxml2::XMLElement *sdf = doc.FirstChildElement("sdf");
  const tinyxml2::XMLElement *model = (sdf != nullptr)
      ? sdf->FirstChildElement("model")
      : doc.FirstChildElement("model");
  if (model == nullptr)
  {
    std::cerr << "LoadPayloadModel: model.sdf 里没有 <model>: " << sdfPath.string() << std::endl;
    return false;
  }

  const char *nameAttr = model->Attribute("name");
  _out.modelName = (nameAttr != nullptr) ? nameAttr : "";

  /// 只取**首个 link**（标准载荷模型就是单 link；多 link 属装甲/复杂件，本期不支持）
  const tinyxml2::XMLElement *link = model->FirstChildElement("link");
  if (link == nullptr)
  {
    std::cerr << "LoadPayloadModel: <model> 下没有 <link>: " << sdfPath.string() << std::endl;
    return false;
  }

  /// 首个 visual → mesh uri + visual pose
  if (const tinyxml2::XMLElement *visual = link->FirstChildElement("visual"))
  {
    ParsePose6(PoseTextOf(visual), _out.visualPose);
    const tinyxml2::XMLElement *geom = visual->FirstChildElement("geometry");
    const tinyxml2::XMLElement *mesh = (geom != nullptr)
        ? geom->FirstChildElement("mesh") : nullptr;
    const tinyxml2::XMLElement *uri = (mesh != nullptr)
        ? mesh->FirstChildElement("uri") : nullptr;
    if (uri != nullptr && uri->GetText() != nullptr)
    {
      _out.meshPath = ResolveMeshUri(uri->GetText(), _dir);
      if (!_out.meshPath.empty() && !fs::exists(_out.meshPath, ec))
      {
        std::cerr << "LoadPayloadModel: mesh 不存在，按无网格处理: "
                  << _out.meshPath << std::endl;
        _out.meshPath.clear();
      }
    }
  }

  /// 每个 <sensor> → type + pose（参数一律忽略）
  for (const tinyxml2::XMLElement *s = link->FirstChildElement("sensor");
       s != nullptr; s = s->NextSiblingElement("sensor"))
  {
    const char *typeAttr = s->Attribute("type");
    if (typeAttr == nullptr)
      continue;
    PayloadSensorDef def;
    def.gzType = NormalizeGzSensorType(typeAttr);
    ParsePose6(PoseTextOf(s), def.pose);
    _out.sensors.push_back(def);
  }

  /// <frame name="..."><pose/></frame>（模型级，不是 link 子元素）
  for (const tinyxml2::XMLElement *fr = model->FirstChildElement("frame");
       fr != nullptr; fr = fr->NextSiblingElement("frame"))
  {
    const char *fn = fr->Attribute("name");
    if (fn == nullptr)
      continue;
    PayloadFrameDef def;
    def.name = fn;
    ParsePose6(PoseTextOf(fr), def.pose);
    _out.frames.push_back(def);
  }

  _out.valid = true;
  return true;
}

}  // namespace keship
