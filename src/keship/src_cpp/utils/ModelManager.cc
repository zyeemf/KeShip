/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 模型管理器实现 */
/// Zhangyu 创建 2026.1.16

#include "ModelManager.hh"
#include "TopicUtil.hh"
#include "keship/utils/KeshipPaths.hh"
#include "WaterAreaSerializer.hh"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <functional>
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <filesystem>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <gz/msgs/empty.pb.h>
#include <gz/msgs/serialized_map.pb.h>
// #include <gz/msgs/world_stats.pb.h>
#include <gz/msgs/scene.pb.h>
#include <gz/math/Pose3.hh>
#include <gz/math/Quaternion.hh>
/// ★ 2026-09-23：解析 mesh uri（`model://…` / 相对路径）用 —— 见 GetCachedModelGeometry
#include <gz/common/Util.hh>

#include <gz/msgs/world_control.pb.h>
#include <gz/msgs/stringmsg.pb.h>

#include "CylinderMeshGenerator.hh"
#include "MeshUtils.hh"
#include "stb/stb_image.h"

/////////////////////////////////////////////////
using namespace keship;
/////////////////////////////////////////////////

namespace {

/// \brief 轻量 JSON 值（KSW v3 嵌套解析用）
struct JsonValue
{
  enum class Kind { Null, String, Number, Bool, Array, Object };
  Kind kind = Kind::Null;
  std::string str;
  double num = 0.0;
  bool boolean = false;
  std::vector<JsonValue> arr;
  std::vector<std::pair<std::string, JsonValue>> obj;   // 保序
};

/// \brief 轻量 JSON 解析（仅需 KSW v3 子集）(递归实现)
class JsonReader
{
public:
  explicit JsonReader(const std::string &_s) : s_(_s), pos_(0) {}

  bool Parse(JsonValue &_out)
  {
    try { SkipWs(); _out = ParseValue(); SkipWs(); return pos_ == s_.size(); }
    catch (...) { return false; }
  }

private:
  const std::string &s_;
  size_t pos_;

  char Peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
  char Next() { return pos_ < s_.size() ? s_[pos_++] : '\0'; }
  void SkipWs() { while (pos_ < s_.size() && std::isspace((unsigned char)s_[pos_])) ++pos_; }

  std::string ParseString()
  {
    if (Next() != '"') throw std::runtime_error("json: expected string");
    std::string out;
    while (pos_ < s_.size())
    {
      char c = Next();
      if (c == '"') return out;
      if (c == '\\')
      {
        char e = Next();
        switch (e)
        {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u':
          {
            char hex[5] = {Next(), Next(), Next(), Next(), 0};
            out += static_cast<char>(std::strtol(hex, nullptr, 16));
            break;
          }
          default: out += e;
        }
      }
      else out += c;
    }
    throw std::runtime_error("json: unterminated string");
  }

  JsonValue ParseValue()
  {
    char c = Peek();
    if (c == '{') return ParseObject();
    if (c == '[') return ParseArray();
    if (c == '"') { JsonValue v; v.kind = JsonValue::Kind::String; v.str = ParseString(); return v; }
    if (c == 't') { for (const char *t = "true"; *t; ++t) if (Next() != *t) throw std::runtime_error("json: true"); JsonValue v; v.kind = JsonValue::Kind::Bool; v.boolean = true; return v; }
    if (c == 'f') { for (const char *t = "false"; *t; ++t) if (Next() != *t) throw std::runtime_error("json: false"); JsonValue v; v.kind = JsonValue::Kind::Bool; v.boolean = false; return v; }
    if (c == 'n') { for (const char *t = "null"; *t; ++t) if (Next() != *t) throw std::runtime_error("json: null"); return JsonValue{}; }
    return ParseNumber();
  }

  JsonValue ParseNumber()
  {
    size_t start = pos_;
    if (Peek() == '-') Next();
    while (std::isdigit((unsigned char)Peek()) || Peek() == '.' || Peek() == 'e' || Peek() == 'E' || Peek() == '+' || Peek() == '-')
      Next();
    JsonValue v;
    v.kind = JsonValue::Kind::Number;
    v.num = std::strtod(s_.c_str() + start, nullptr);
    return v;
  }

  JsonValue ParseObject()
  {
    Next(); // '{'
    JsonValue v; v.kind = JsonValue::Kind::Object;
    SkipWs();
    if (Peek() == '}') { Next(); return v; }
    while (true)
    {
      SkipWs();
      std::string key = ParseString();
      SkipWs();
      if (Next() != ':') throw std::runtime_error("json: expected ':'");
      SkipWs();
      v.obj.emplace_back(std::move(key), ParseValue());
      SkipWs();
      char c = Next();
      if (c == ',') continue;
      if (c == '}') break;
      throw std::runtime_error("json: expected ',' or '}'");
    }
    return v;
  }

  JsonValue ParseArray()
  {
    Next(); // '['
    JsonValue v; v.kind = JsonValue::Kind::Array;
    SkipWs();
    if (Peek() == ']') { Next(); return v; }
    while (true)
    {
      SkipWs();
      v.arr.push_back(ParseValue());
      SkipWs();
      char c = Next();
      if (c == ',') continue;
      if (c == ']') break;
      throw std::runtime_error("json: expected ',' or ']'");
    }
    return v;
  }
};

/// @brief JSON 字符串转义
std::string JsonEscape(const std::string &_s)
{
  std::string out;
  out.reserve(_s.size() + 8);
  for (char c : _s)
  {
    switch (c)
    {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:   out += c;
    }
  }
  return out;
}

/// @brief 递归往层级树中插入叶子（v3 序列化）
/// @param _path 层级标签链（末元素 = 叶子键名）
/// @param _leaf 叶子 JSON 值
void InsertTreeLeaf(JsonValue &_root,
    const std::vector<std::string> &_path, JsonValue _leaf)
{
  JsonValue *node = &_root;
  for (size_t i = 0; i + 1 < _path.size(); ++i)
  {
    auto it = std::find_if(node->obj.begin(), node->obj.end(),
        [&](const auto &kv) { return kv.first == _path[i]; });
    if (it == node->obj.end())
    {
      JsonValue n; n.kind = JsonValue::Kind::Object;
      node->obj.emplace_back(_path[i], std::move(n));
      node = &node->obj.back().second;
    }
    else node = &it->second;
  }
  if (!_path.empty())
    node->obj.emplace_back(_path.back(), std::move(_leaf));
}

/// @brief 递归导出层级树到 JSON 文本（v3 序列化）
void WriteTree(std::ostream &_os, const JsonValue &_node, int _indent)
{
  const std::string pad(_indent * 2, ' ');
  const std::string closer(_indent > 0 ? std::string((_indent - 1) * 2, ' ') : "");
  _os << "{\n";
  for (size_t i = 0; i < _node.obj.size(); ++i)
  {
    const auto &[key, val] = _node.obj[i];
    _os << pad << "\"" << JsonEscape(key) << "\": ";
    switch (val.kind)
    {
      case JsonValue::Kind::Object:
        WriteTree(_os, val, _indent + 1);
        break;
      case JsonValue::Kind::String:
        _os << "\"" << JsonEscape(val.str) << "\"";
        break;
      case JsonValue::Kind::Number:
      {
        std::ostringstream ss;
        ss << val.num;
        _os << ss.str();
        break;
      }
      case JsonValue::Kind::Bool:
        _os << (val.boolean ? "true" : "false");
        break;
      case JsonValue::Kind::Array:
      {
        _os << "[";
        for (size_t k = 0; k < val.arr.size(); ++k)
        {
          if (k) _os << ",";
          switch (val.arr[k].kind)
          {
            case JsonValue::Kind::Number:
            {
              std::ostringstream ss; ss << val.arr[k].num; _os << ss.str();
              break;
            }
            case JsonValue::Kind::String:
              _os << "\"" << JsonEscape(val.arr[k].str) << "\"";
              break;
            case JsonValue::Kind::Bool:
              _os << (val.arr[k].boolean ? "true" : "false");
              break;
            default: _os << "null";
          }
        }
        _os << "]";
        break;
      }
      default: _os << "null";
    }
    _os << ((i + 1 < _node.obj.size()) ? ",\n" : "\n");
  }
  _os << closer << "}";
}

/// @brief 按优先级查找 model://<name> 所属的模型根目录
/// 顺序：models_all/robot_model → GZ_SIM_RESOURCE_PATH → GAZEBO_MODEL_PATH
std::string ResolveModelBase(const std::string &_modelName)
{
  if (_modelName.empty())
    return "";

  std::vector<std::string> roots;
  roots.push_back(keship::KeshipRobotModelDir());

  auto addEnv = [&](const char *_envName) {
    const char *v = std::getenv(_envName);
    if (!v || !*v)
      return;
    std::string s(v);
    size_t start = 0;
    while (true)
    {
      size_t pos = s.find(':', start);
      std::string part = s.substr(start, pos == std::string::npos ? pos : pos - start);
      if (!part.empty())
        roots.push_back(part);
      if (pos == std::string::npos)
        break;
      start = pos + 1;
    }
  };
  addEnv("GZ_SIM_RESOURCE_PATH");
  addEnv("GAZEBO_MODEL_PATH");

  for (const auto &root : roots)
  {
    std::error_code ec;
    std::filesystem::path modelPath =
        std::filesystem::path(root) / _modelName;
    if (std::filesystem::is_directory(modelPath, ec))
      return root;
  }
  return "";
}

/// @brief 把 SDF 里**第一个** `<model name="...">` 的名称替换为 _name
///
/// 用途：让「实例名」与「文件内模型名」解耦。`SpawnModelWithBuoyancy` /
/// `SpawnModelWithThrust` / `SpawnModelInGazeboFromSDFString` 的 create/blocking
/// 都依赖「缓存 SDF 里的模型名 == 实例名」才能按实例名取回 entity ID，
/// 因此写入缓存前必须统一。兼容单/双引号（历史文件两种都有）。
/// @return 是否真的替换过（未找到 model name 返回 false）
bool ReplaceFirstModelName(std::string &_sdf, const std::string &_name)
{
  const char *needle = "model name=\"";
  char quote = '"';
  size_t pos = _sdf.find(needle);
  if (pos == std::string::npos)
  {
    needle = "model name='";
    quote = '\'';
    pos = _sdf.find(needle);
  }
  if (pos == std::string::npos)
    return false;
  const size_t nameStart = pos + std::strlen(needle);
  const size_t nameEnd = _sdf.find(quote, nameStart);
  if (nameEnd == std::string::npos)
    return false;
  _sdf.replace(nameStart, nameEnd - nameStart, _name);
  return true;
}

/// @brief 若水动力已启用，把 6x6 附加质量注入 SDF 的 `<fluid_added_mass>`
///
/// ★ 2026-09-24 新增（修 P0-1）：注入逻辑原先**只写在 `SpawnModelWithBuoyancy` 一处**，
/// 导致另外 6 条**有 M_A 数据**的路径（`SpawnModelWithThrust` 与 5 个 Rebuild）全部丢失 M_A。
/// 现在统一走本函数 —— **新增加载路径时只需调它一次**，不必各自抄一遍
/// （平行路径各自实现正是漏补的成因，见 doc/audit/model_loading_audit_2026-09-24.md）。
///
/// 全零矩阵直接跳过（避免生成空的 `<fluid_added_mass>` 标签）。
/// @param _sdf 已组装完成的 SDF 字符串流（须含 `</inertial>`，否则注入失败并返回）
/// @param _hasHydro 该路径是否拿到了水动力配置
/// @param _hydro 水动力配置（提供 addedMass）
void ApplyAddedMassToSdf(std::ostringstream &_sdf, bool _hasHydro,
                         const HydroConfig &_hydro)
{
  if (!_hasHydro)
    return;
  bool anyNonZero = false;
  for (int i = 0; i < 36; ++i)
    if (std::abs(_hydro.addedMass[i]) > 1e-12) { anyNonZero = true; break; }
  if (!anyNonZero)
    return;
  std::string s = _sdf.str();
  ModelManager::InjectFluidAddedMass(s, _hydro.addedMass.data());
  _sdf.str("");
  _sdf << s;
}

/// @brief 把 SDF 里的 mesh uri 解析成本地绝对路径
/// 支持 model://、file://、相对路径（相对 SDF 目录或 keship 自定义目录）。
/// 解析不到/不安全返回空串。
std::string ResolveMeshUriToPath(const std::string &_uri,
                                 const std::string &_sdfBaseDir)
{
  if (_uri.empty())
    return "";

  // file:///abs/path 或 file://relative
  if (_uri.rfind("file://", 0) == 0)
  {
    std::string p = _uri.substr(7);
    if (p.empty())
      return "";
    std::error_code ec;
    if (std::filesystem::exists(p, ec))
      return p;
    // 相对 file:// 也尝试相对 SDF 目录
    std::filesystem::path cand = std::filesystem::path(_sdfBaseDir) / p;
    if (std::filesystem::exists(cand, ec))
      return cand.lexically_normal().string();
    return "";
  }

  // model://name/rel
  if (_uri.rfind("model://", 0) == 0)
  {
    std::string body = _uri.substr(8);
    auto slash = body.find('/');
    if (slash == std::string::npos)
      return "";
    std::string modelName = body.substr(0, slash);
    std::string relPath = body.substr(slash + 1);
    if (modelName.empty() || relPath.empty())
      return "";
    if (relPath[0] == '/' || relPath[0] == '\\')
      return "";                    // model://name//abs 形式拒绝
    if (relPath.find("..") != std::string::npos)
      return "";                    // 路径穿越拒绝
    std::string base = ResolveModelBase(modelName);
    if (base.empty())
      return "";
    std::filesystem::path cand = std::filesystem::path(base) / modelName / relPath;
    return cand.lexically_normal().string();
  }

  // 绝对路径
  if (_uri[0] == '/' || _uri[0] == '\\')
    return _uri;

  // 有协议（http:// 等）不支持
  if (_uri.find("://") != std::string::npos)
    return "";

  // 相对路径
  // 1) keship 自定义前缀：data/ 或 models_all/ → 相对 keship_ws
  if (_uri.rfind("data/", 0) == 0 || _uri.rfind("models_all/", 0) == 0)
  {
    std::filesystem::path cand =
        std::filesystem::path(keship::KeshipHomeWsDir()) / _uri;
    return cand.lexically_normal().string();
  }
  // 2) robot_model/ 等直接相对 models_all
  // 3) 其他相对路径 → 相对 SDF 所在目录（标准 SDF 行为）
  std::string p;
  std::error_code ec;
  std::filesystem::path candModel = std::filesystem::path(keship::KeshipAllModelsDir()) / _uri;
  if (std::filesystem::exists(candModel, ec))
    return candModel.lexically_normal().string();
  std::filesystem::path candSdf = std::filesystem::path(_sdfBaseDir) / _uri;
  if (std::filesystem::exists(candSdf, ec))
    return candSdf.lexically_normal().string();
  // 都不存在则返回 models_all 形式的候选（交由上层判断/兜底）
  return candModel.lexically_normal().string();
}

} // namespace

//////////////////////////////////////////////////////////////////////////////////////////////////
/// KSW 0.0.4 辅助工具（匿名命名空间）
namespace {

/// 构造字符串 / 数字 / 布尔 JSON 值
JsonValue JvMakeStr(const std::string &_s)
{ JsonValue v; v.kind = JsonValue::Kind::String; v.str = _s; return v; }
JsonValue JvMakeNum(double _n)
{ JsonValue v; v.kind = JsonValue::Kind::Number; v.num = _n; return v; }
JsonValue JvMakeBool(bool _b)
{ JsonValue v; v.kind = JsonValue::Kind::Bool; v.boolean = _b; return v; }

/// @brief 六元位姿数组 JSON 值
JsonValue JvMakePose(const double _pose[6])
{
  JsonValue arr; arr.kind = JsonValue::Kind::Array;
  for (int k = 0; k < 6; ++k) arr.arr.push_back(JvMakeNum(_pose[k]));
  return arr;
}

/// @brief 构造空的 0.0.4 世界节点（attributes + models + waters + subWorlds）
JsonValue JvMakeEmptyWorldNode(const std::string &_name)
{
  JsonValue node; node.kind = JsonValue::Kind::Object;
  JsonValue attr; attr.kind = JsonValue::Kind::Object;
  attr.obj.emplace_back("name", JvMakeStr(_name));
  const double zero[6] = {0, 0, 0, 0, 0, 0};
  attr.obj.emplace_back("pose", JvMakePose(zero));
  node.obj.emplace_back("attributes", std::move(attr));
  JsonValue models; models.kind = JsonValue::Kind::Object;
  JsonValue waters; waters.kind = JsonValue::Kind::Object;
  JsonValue subs; subs.kind = JsonValue::Kind::Object;
  node.obj.emplace_back("models", std::move(models));
  node.obj.emplace_back("waters", std::move(waters));
  node.obj.emplace_back("subWorlds", std::move(subs));
  return node;
}

/// @brief 世界节点取指定段对象（models / waters / subWorlds），不存在返回 nullptr
JsonValue *JvWorldSegment(JsonValue &_world, const std::string &_seg)
{
  for (auto &kv : _world.obj)
    if (kv.first == _seg && kv.second.kind == JsonValue::Kind::Object) return &kv.second;
  return nullptr;
}

/// @brief 沿子 World 链定位节点（不存在则逐层创建），返回目标世界节点
JsonValue *JvGetOrCreateSubWorld(JsonValue &_root,
    const std::vector<std::string> &_chain)
{
  JsonValue *cur = &_root;
  for (const auto &seg : _chain)
  {
    JsonValue *subs = JvWorldSegment(*cur, "subWorlds");
    if (!subs) return cur;
    auto it = std::find_if(subs->obj.begin(), subs->obj.end(),
        [&](const auto &kv) { return kv.first == seg && kv.second.kind == JsonValue::Kind::Object; });
    if (it != subs->obj.end()) cur = &it->second;
    else
    {
      subs->obj.emplace_back(seg, JvMakeEmptyWorldNode(seg));
      cur = &subs->obj.back().second;
    }
  }
  return cur;
}

/// @brief 从 0.0.4 水域叶子（attributes + fluids）解析为 WaterArea
keship::WaterArea BuildWaterAreaFromLeaf(const JsonValue &_leaf)
{
  keship::WaterArea a;
  auto getObj = [&](const std::string &key) -> const JsonValue *
  {
    for (const auto &kv : _leaf.obj)
      if (kv.first == key && kv.second.kind == JsonValue::Kind::Object) return &kv.second;
    return nullptr;
  };

  const JsonValue *attr = getObj("attributes");
  if (attr)
  {
    const JsonValue *bounds = nullptr;
    for (const auto &kv : attr->obj)
      if (kv.first == "bounds" && kv.second.kind == JsonValue::Kind::Object)
      { bounds = &kv.second; break; }
    if (bounds)
    {
      for (const auto &kv : bounds->obj)
      {
        if (kv.second.kind != JsonValue::Kind::Number) continue;
        if (kv.first == "xMin") a.xMin = kv.second.num;
        else if (kv.first == "xMax") a.xMax = kv.second.num;
        else if (kv.first == "yMin") a.yMin = kv.second.num;
        else if (kv.first == "yMax") a.yMax = kv.second.num;
      }
    }
    for (const auto &kv : attr->obj)
    {
      if (kv.first == "baseHeight" && kv.second.kind == JsonValue::Kind::Number)
        a.baseHeight = kv.second.num;
      else if (kv.first == "vertices" && kv.second.kind == JsonValue::Kind::Array)
      {
        for (const auto &pt : kv.second.arr)
        {
          if (pt.kind != JsonValue::Kind::Array || pt.arr.size() < 2) continue;
          auto get = [&](size_t i) { return (pt.arr[i].kind == JsonValue::Kind::Number) ? pt.arr[i].num : 0.0; };
          a.vertices.emplace_back(get(0), get(1));
        }
      }
    }
  }

  const JsonValue *fluids = getObj("fluids");
  if (fluids)
  {
    auto seg = [&](const std::string &key) -> const JsonValue *
    {
      for (const auto &kv : fluids->obj)
        if (kv.first == key && kv.second.kind == JsonValue::Kind::Object) return &kv.second;
      return nullptr;
    };
    const JsonValue *wave = seg("wave");
    if (wave)
    {
      for (const auto &kv : wave->obj)
      {
        if (kv.first == "model" && kv.second.kind == JsonValue::Kind::String)
          a.waveParams.model = kv.second.str;
        else if (kv.second.kind != JsonValue::Kind::Number) continue;
        else if (kv.first == "period") a.waveParams.period = kv.second.num;
        else if (kv.first == "number") a.waveParams.number = static_cast<int>(kv.second.num);
        else if (kv.first == "amplitude") a.waveParams.amplitude = kv.second.num;
        else if (kv.first == "scale") a.waveParams.scale = kv.second.num;
        else if (kv.first == "gain") a.waveParams.gain = kv.second.num;
        else if (kv.first == "direction") a.waveParams.direction = kv.second.num;
        else if (kv.first == "angle") a.waveParams.angle = kv.second.num;
        else if (kv.first == "steepness") a.waveParams.steepness = kv.second.num;
        else if (kv.first == "tau") a.waveParams.tau = kv.second.num;
      }
    }
    const JsonValue *wind = seg("wind");
    if (wind)
    {
      for (const auto &kv : wind->obj)
      {
        if (kv.second.kind != JsonValue::Kind::Number) continue;
        if (kv.first == "speed") a.windParams.speed = kv.second.num;
        else if (kv.first == "direction") a.windParams.direction = kv.second.num;
      }
    }
    const JsonValue *cur = seg("current");
    if (cur)
    {
      for (const auto &kv : cur->obj)
      {
        if (kv.second.kind != JsonValue::Kind::Number) continue;
        if (kv.first == "speed") a.currentParams.speed = kv.second.num;
        else if (kv.first == "direction") a.currentParams.direction = kv.second.num;
      }
    }
    for (const auto &kv : fluids->obj)
      if (kv.first == "directionAuto" && kv.second.kind == JsonValue::Kind::Bool)
        a.waveDirectionAuto = kv.second.boolean;
  }
  return a;
}

} // namespace 0.0.4 helpers
//////////////////////////////////////////////////////////////////////////////////////////////////

ModelManager::ModelManager()
{
  this->worldName = "keship_world";
  this->modelList = std::map<std::string, uint32_t>();

  this->node = std::make_unique<gz::transport::Node>();
}

ModelManager::ModelManager(const std::string &_worldName)
{
  this->worldName = _worldName;
  this->modelList = std::map<std::string, uint32_t>();

  this->node = std::make_unique<gz::transport::Node>();
  /// 预注册 model_ready topic，避免首次 Publish 时的注册延迟导致消息丢失
  this->node->Advertise<gz::msgs::StringMsg>("/keship/model_ready");
}

/////////////////////////////////////////////////
ModelManager::~ModelManager()
{
  this->modelList.clear();
}

/////////////////////////////////////////////////
/**
 * @brief 从文件路径加载模型到 Gazebo 中
 * @param _instanceName 实例名称（可以与模型名称不同）
 * @param _filePath 模型文件路径
 * @param _pos 模型初始位姿 [x, y, z, roll, pitch, yaw]
 * @return true 成功加载模型
 * @return false 加载模型失败
 *
 * @note 根据加载的文件类型区别处理：1. sdf文件；2. dae文件；3. stl文件；4. 其他文件类型
 */
bool ModelManager::SpawnModelInGazeboFromFile(
    const std::string &_instanceName,
    const std::string &_filePath,
    double _pos[6],
    bool _isStatic,
    bool _isAddBuoyancy)
{
  // this->worldName = _worldName;

  ///  准备服务调用：6个参数
  std::string service_create = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req_create;
  gz::msgs::Boolean rep_create;
  bool result;
  unsigned int timeout = 20000; // 2秒超时
  bool executed = false;

  /// 设置req_create参数
  req_create.set_name(_instanceName); // 实例名称（可以与模型名称不同）
  req_create.set_allow_renaming(false);

  auto pose = req_create.mutable_pose(); // 模型初始位姿
  pose->mutable_position()->set_x(_pos[0]);
  pose->mutable_position()->set_y(_pos[1]);
  pose->mutable_position()->set_z(_pos[2]);

  /// 欧拉角 → 四元数转换（_pos[3..5] = roll, pitch, yaw）
  gz::math::Quaterniond q(_pos[3], _pos[4], _pos[5]);
  pose->mutable_orientation()->set_x(q.X());
  pose->mutable_orientation()->set_y(q.Y());
  pose->mutable_orientation()->set_z(q.Z());
  pose->mutable_orientation()->set_w(q.W());

  std::string sdfContent;  // 缓存用

  /// 根据文件后缀判断加载方式
  // 首先获取后缀
  std::string fileExtension = _filePath.substr(_filePath.find_last_of(".") + 1);
  // 然后根据后缀判断加载方式
  fileExtension = boost::to_lower_copy(fileExtension); // 后缀改为小写字母
  if (fileExtension == "sdf")                          // sdf：直接使用文件路径
  {
    /// 读取 SDF 文件内容作为 base SDF 缓存
    std::ifstream ifs(_filePath);
    if (ifs)
    {
      std::ostringstream oss;
      oss << ifs.rdbuf();
      sdfContent = oss.str();
    }
    /// 统一坐标系：mesh uri → 规范化缓存（与 dae/obj/stl 分支一致）
    /// 相对路径优先相对 SDF 文件所在目录解析
    {
      std::filesystem::path sdfPath(_filePath);
      std::string sdfBaseDir = sdfPath.has_parent_path()
          ? sdfPath.parent_path().string() : "";
      sdfContent = this->NormalizeMeshUrisInSDF(sdfContent, sdfBaseDir);
    }
    /// SDF 内模型名统一为实例名（allow_renaming=false 依赖 name 一致；兼容双/单引号）
    {
      const char *needle = "model name=\"";
      char quote = '"';
      size_t pos = sdfContent.find(needle);
      if (pos == std::string::npos)
      {
        needle = "model name='";
        quote = '\'';
        pos = sdfContent.find(needle);
      }
      if (pos != std::string::npos)
      {
        const size_t nameStart = pos + 12;
        const size_t nameEnd = sdfContent.find(quote, nameStart);
        if (nameEnd != std::string::npos)
          sdfContent.replace(nameStart, nameEnd - nameStart, _instanceName);
      }
    }
    req_create.set_sdf(sdfContent);
  }
  else if (fileExtension == "dae") // dae：制作sdf字符串
  {
    sdfContent = this->MakeSDFStringFromValidFileType(_instanceName, _filePath, _isStatic, _isAddBuoyancy);
    req_create.set_sdf(sdfContent); // 使用SDF字符串
  }
  else if (fileExtension == "glb" || fileExtension == "gltf") // glb：制作sdf字符串
  {
    sdfContent = this->MakeSDFStringFromValidFileType(_instanceName, _filePath, _isStatic, _isAddBuoyancy);
    req_create.set_sdf(sdfContent); // 使用SDF字符串
  }
  else if (fileExtension == "stl") // stl：制作sdf字符串
  {
    sdfContent = this->MakeSDFStringFromValidFileType(_instanceName, _filePath, _isStatic, _isAddBuoyancy);
    req_create.set_sdf(sdfContent); // 使用SDF字符串
  }
  // else if (fileExtension == "ply") // ply：制作sdf字符串 // 不支持ply文件，需要先转换为stl文件，或者pcl库存的时候就是stl格式
  // {
  //   std::string sdfContent = this->MakeSDFStringFromValidFileType(_instanceName, _filePath, _isStatic, _isAddBuoyancy);
  //   req_create.set_sdf(sdfContent); // 使用SDF字符串
  // }
  else
  {
    std::cout << "不支持的文件类型: " << fileExtension << std::endl;
    return false;
  }

  /// 调用服务（同步方式）
  std::cout << "正在调用服务create: " << service_create << std::endl;
  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / **结果检查**只有一处实现）
  executed = CreateEntityViaService(req_create);
  ///
  if (executed)
  {
    std::cout << "服务调用成功create" << std::endl;
    /// 等待模型ID返回
    uint32_t _id = 0;
    bool found = false;
    uint32_t maxTryTimes = 100;
    /// ★ 2026-09-25 性能修复（⑩）：先查再睡（原为"先睡 500ms 再查"，
    /// 实体已就绪时白等半秒 —— 实测 keship3 单次加载里就有这 0.5s）
    found = this->GetModelIDByInstanceNameFromGazebo(_instanceName, _id);
    while ((!found) && (maxTryTimes-- > 0))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      found = this->GetModelIDByInstanceNameFromGazebo(_instanceName, _id);
    }
    if (!found)
    {
      std::cout << "服务调用scene：未找到模型" << _instanceName << std::endl;
      return false;
    }
    // 添加到模型列表
    this->modelList[_instanceName] = _id;
    {
      SensorConfig sc;
      GetCachedSensorConfig(_instanceName, sc);
      NotifyModelSpawned(_instanceName, sc);
    }

    /// 缓存 base SDF + 空浮力体配置（供浮力体编辑器使用）
    this->CacheNormalizedSDF(_instanceName, sdfContent);
    this->staticCache_[_instanceName] = _isStatic;
    BuoyancyConfig cfg;
    cfg.linkName = "base_link";
    this->buoyancyConfigCache_[_instanceName] = cfg;

    CacheModelFootprint(_instanceName);
  }
  else
  {
    std::cout << "服务调用失败create" << std::endl;
  }
  std::cout << "服务调用create，执行状态" << executed << "，结果状态" << result << std::endl;

  return executed;
}

bool ModelManager::GetModelIDByInstanceNameFromGazebo(const std::string &_instanceName, uint32_t &_id)
{
  /// 获取模型Entity ID
  bool result;
  unsigned int timeout = 2000; // 2秒超时
  bool executed = false;
  std::string service_scene = "/world/" + this->worldName + "/scene/info";
  gz::msgs::Empty req_scene;
  gz::msgs::Scene rep_scene;

  executed = this->node->Request(service_scene, req_scene, timeout, rep_scene, result);
  if (executed)
  {
    /// ★ 2026-09-25 性能修复（⑩）：去掉两处**常态**打印。
    /// 原实现每次查询都打印"模型数量" + **逐个模型名**，而 spawn 后的等待循环
    /// 会反复调用本函数、且每个 spawn 都调 ⇒ 输出量随模型数呈 **O(N²)**；
    /// 又因每行都用 std::endl（每次都 flush）而实打实地拖慢加载。
    /// 失败信息（未找到/服务失败）仍然照打 —— 那才是需要看的。
    /// 需要逐模型细节时用 `keship_ctl model list`。
    for (int i = 0; i < rep_scene.model_size(); i++)
    {
      if (rep_scene.model(i).name() == _instanceName)
      {
        _id = rep_scene.model(i).id();
        return true;
      }
    }
  }
  else
  {
    std::cout << "服务调用失败scene" << std::endl;
  }
  std::cout << "服务调用scene：未找到模型" << _instanceName << std::endl;
  return false;
}

bool ModelManager::GetModelPoseByIDFromGazebo(uint32_t _id, double _pose[6])
{
  /// 获取模型Entity ID
  bool result;
  unsigned int timeout = 2000; // 2秒超时
  bool executed = false;
  std::string service_state = "/world/" + this->worldName + "/state";
  gz::msgs::Empty req_state;
  gz::msgs::SerializedStepMap rep_state;

  executed = this->node->Request(service_state, req_state, timeout, rep_state, result);
  if (!executed || !result)
  {
    std::cout << "服务调用失败state" << std::endl;
    return false;
  }
  auto it = rep_state.state().entities().find(_id);
  if (it == rep_state.state().entities().end())
  {
    std::cout << "服务调用state：未找到模型" << _id << std::endl;
    return false;
  }
  double x, y, z, roll, pitch, yaw;
  for (const auto &[compType, compMsg] : it->second.components())
  {
    std::cout << "服务调用state，组件类型" << compType << std::endl;
    if (compType == 10918813941671183356ULL)
    {
      std::cout << "服务调用state，模型位姿" << compMsg.component().c_str() << std::endl;
      std::sscanf(compMsg.component().c_str(), "%lf %lf %lf %lf %lf %lf",
                  _pose + 0,
                  _pose + 1,
                  _pose + 2,
                  _pose + 3,
                  _pose + 4,
                  _pose + 5);
      return true;
    }
  }
  return false;
}

/// @brief  设置模型位姿
/// @param _id 模型ID
/// @param _pose 模型位姿，格式为[x, y, z, roll, pitch, yaw]
/// @return 是否设置成功
bool ModelManager::SetModelPoseByIDInGazebo(uint32_t _id, double _pose[6])
{
  /// 1. 设置模型位姿
  bool result;
  unsigned int timeout = 2000; // 2秒超时
  bool executed = false;

  std::string service_set_pose = "/world/" + this->worldName + "/set_pose/blocking"; // 带blocking的是同步执行，不带blocking是异步执行
  gz::msgs::Pose req_set_pose;
  gz::msgs::Boolean rep_set_pose;
  ///
  req_set_pose.set_id(_id);
  gz::math::Pose3d pose(_pose[0], _pose[1], _pose[2], _pose[3], _pose[4], _pose[5]);
  auto q = pose.Rot();
  req_set_pose.mutable_position()->set_x(pose.Pos().X());
  req_set_pose.mutable_position()->set_y(pose.Pos().Y());
  req_set_pose.mutable_position()->set_z(pose.Pos().Z());
  req_set_pose.mutable_orientation()->set_w(q.W());
  req_set_pose.mutable_orientation()->set_x(q.X());
  req_set_pose.mutable_orientation()->set_y(q.Y());
  req_set_pose.mutable_orientation()->set_z(q.Z());
  ///
  executed = this->node->Request(service_set_pose, req_set_pose, timeout, rep_set_pose, result);
  if (!executed || !result || !rep_set_pose.data())
  {
    std::cout << "服务调用失败set_pose" << std::endl;
    return false;
  }
  else
  {
    std::cout << "服务调用成功set_pose" << std::endl;
  }

  /// 2. 清零线性速度和角速度

  return true;
}

/////////////////////////////////////////////////
/// 统一加载层坐标系：SDF 中文件 mesh uri → 规范化缓存 OBJ
std::string ModelManager::NormalizeMeshUrisInSDF(const std::string &_sdf,
                                                 const std::string &_sdfBaseDir)
{
  static const char *kMeshExts[] = {
      ".dae", ".obj", ".stl", ".gltf", ".glb", ".fbx"};

  std::string out = _sdf;
  /// ★ 2026-09-25 性能修复（③）：**同一 SDF 内同一源文件只规范化一次**。
  /// 船的 base SDF 里 visual 与 collision 各有一个 `<uri>` 且指向同一 mesh
  /// （例：ke0812_1.ksm 两处都是 `robot_model/keship3/keship3.dae`），
  /// 原实现对每个 `<uri>` 各调一次 NormalizeMeshToSDFCache ⇒ **同一文件被全量
  /// 解析两次**（wamv 125MB 时实测第二次多花 4.3s）。
  /// 去重后语义**逐字不变**：命中的 uri 仍照原样写 meshCacheToSrcUri_
  /// （保持"后出现的 uri 覆盖"这一原语义）。
  std::map<std::string, std::string> pathToCachePath;  // 解析后源路径 → 规范化缓存路径
  size_t pos = 0;
  while (true)
  {
    size_t start = out.find("<uri>", pos);
    if (start == std::string::npos)
      break;
    start += 5;
    size_t end = out.find("</uri>", start);
    if (end == std::string::npos)
      break;

    std::string uri = out.substr(start, end - start);
    std::string repl;

    // 只处理网格扩展名
    bool isMeshExt = false;
    {
      std::string low = boost::to_lower_copy(uri);
      for (const char *e : kMeshExts)
      {
        size_t elen = std::strlen(e);
        if (low.size() >= elen && low.compare(low.size() - elen, elen, e) == 0)
        {
          isMeshExt = true;
          break;
        }
      }
    }
    if (!isMeshExt)
    {
      pos = end;
      continue;
    }

    std::string path = ResolveMeshUriToPath(uri, _sdfBaseDir);
    if (path.empty())
    {
      std::cerr << "NormalizeMeshUrisInSDF: 无法解析 mesh uri: "
                << uri << std::endl;
      pos = end;
      continue;
    }

    /// ★ ③：本 SDF 内该源文件已规范化过则复用，不再解析
    std::string cachePath;
    auto cached = pathToCachePath.find(path);
    if (cached != pathToCachePath.end())
    {
      cachePath = cached->second;
    }
    else
    {
      cachePath = NormalizeMeshToSDFCache(path);
      /// 只缓存**成功**结果 —— 失败仍逐次报错，与改前行为一致
      if (!cachePath.empty())
        pathToCachePath[path] = cachePath;
    }

    if (!cachePath.empty())
    {
      meshCacheToSrcUri_[cachePath] = uri;
      repl = "file://" + cachePath;
    }
    else
    {
      // 规范化失败：若原始文件存在则直接使用原始路径兜底
      std::error_code ec;
      if (std::filesystem::exists(path, ec))
      {
        std::cerr << "NormalizeMeshUrisInSDF: 规范化失败，使用原始文件: "
                  << path << std::endl;
        repl = "file://" + path;
      }
      else
      {
        std::cerr << "NormalizeMeshUrisInSDF: 模型文件不存在: "
                  << path << " (uri=" << uri << ")" << std::endl;
      }
    }

    if (!repl.empty() && repl != uri)
      out.replace(start, end - start, repl);
    pos = start + (repl.empty() ? (end - start) : repl.size());
  }
  return out;
}

/////////////////////////////////////////////////
std::string ModelManager::MakeSDFStringFromValidFileType(const std::string &_instanceName, const std::string &_filePath, bool isStatic, bool isBuoyancy,
    bool _addInertia, const InertiaParams &_inertia)
{
  std::ostringstream sdf;
  std::string isStaticStr = isStatic ? "true" : "false";

  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.10\">\n"
      << "  <model name=\"" << _instanceName << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>" << isStaticStr << "</static>\n"
      << "    <link name=\"base_link\">\n";
  if (_addInertia)
  {
    sdf << "      <inertial>\n";
    if (_inertia.cogX != 0 || _inertia.cogY != 0 || _inertia.cogZ != 0)
      sdf << "        <pose>" << _inertia.cogX << " " << _inertia.cogY << " " << _inertia.cogZ << " 0 0 0</pose>\n";
    sdf << "        <mass>" << _inertia.mass << "</mass>\n"
        << "        <inertia>\n"
        << "          <ixx>" << _inertia.ixx << "</ixx><ixy>" << _inertia.ixy << "</ixy><ixz>" << _inertia.ixz << "</ixz>\n"
        << "          <iyy>" << _inertia.iyy << "</iyy><iyz>" << _inertia.iyz << "</iyz>\n"
        << "          <izz>" << _inertia.izz << "</izz>\n"
        << "        </inertia>\n"
        << "      </inertial>\n";
  }
  std::string meshUri = NormalizeMeshToSDFCache(_filePath);
  if (meshUri.empty())
  {
    std::cerr << "MakeSDFStringFromValidFileType: 规范化失败，回退原始文件: "
              << _filePath << std::endl;
    meshUri = _filePath;
  }

  sdf << "      <visual name=\"visual\">\n"
      << "        <geometry>\n"
      << "          <mesh><uri>file://" << meshUri << "</uri></mesh>\n"
      << "        </geometry>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <mesh><uri>file://" << meshUri << "</uri></mesh>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n";
  if (isBuoyancy)
  {
    sdf << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
        << "        name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <enable_buoyancy>true</enable_buoyancy>\n"
      << "      <enable_hydro>false</enable_hydro>\n"
      << "      <fluid_density>1000</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <link_name>base_link</link_name>\n"
      << "    </plugin>\n";
  }
  sdf << "  </model>\n"
      << "</sdf>\n";
  std::cout << "MakeSDFStringFromDAE: 生成的SDF字符串: " << std::endl
            << sdf.str() << std::endl;
  return sdf.str();
}

/////////////////////////////////////////////////
std::string ModelManager::MakeSDFStringFromSTL(const std::string &_instanceName, const std::string &_stlFilePath)
{
  // TODO: 实现从STL文件制作SDF字符串的功能
  return "";
}
/////////////////////////////////////////////////
std::string ModelManager::MakeSDFString_TestShape(const TestShapeType &_shapeType, const std::vector<double>& _shapeParams)
{
  std::string shapeType="";
  /// 转换为字符串
  switch (_shapeType)
  {
  case TestShapeType::Cube:
    shapeType="Cube";
    break;
  case TestShapeType::Sphere:
    shapeType="Sphere";
    break;
  case TestShapeType::Cylinder:
    shapeType="Cylinder";
    break;
  case TestShapeType::Cone:
    shapeType="Cone";
    break;
  
  default:
    break;
  }
  
  ///
  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.10\">\n"
      << "  <model name=\"" << "TestShape" << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>true</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry>\n"
      << "          <box size=\"1 1 1\"/>\n"
      << "        </geometry>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <box size=\"1 1 1\"/>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "      <isBuoyancy>true</isBuoyancy>\n"
      << "      <fromShapeType>" << shapeType << "</fromShapeType>\n"
      << "      <params>"
      ;
  for(int i=0;i<_shapeParams.size();i++)
  {
    sdf << _shapeParams[i] << " ";
  }
  sdf << "      </params>\n";

  sdf << "    </plugin>\n"
      << "  </model>\n"
      << "</sdf>\n";
  std::cout << "MakeSDFString_TestShape: 生成的SDF字符串: " << std::endl
            << sdf.str() << std::endl;
  return sdf.str();
}

/////////////////////////////////////////////////
bool ModelManager::SpawnCylinderInGazebo(
    const std::string &_instanceName,
    double _diameter,
    double _length,
    double _density,
    double _pos[6],
    bool _skipSpawn)
{
  double radius = _diameter * 0.5;
  double volume = M_PI * radius * radius * _length;
  double mass = _density * volume;

  /// 计算圆柱体惯性（绕质心，Z轴为圆柱轴线）
  double ixx = (1.0 / 12.0) * mass * (3.0 * radius * radius + _length * _length);
  double iyy = ixx;
  double izz = 0.5 * mass * radius * radius;

  int segments = 32;

  /// 生成基础 SDF（不含浮力插件，用于缓存和重建）
  std::ostringstream baseSDF;
  baseSDF << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.10\">\n"
      << "  <model name=\"" << _instanceName << "\">\n"
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
      << "          <cylinder>\n"
      << "            <radius>" << radius << "</radius>\n"
      << "            <length>" << _length << "</length>\n"
      << "          </cylinder>\n"
      << "        </geometry>\n"
      << "        <material>\n"
      << "          <ambient>1.0 0.4 0.1 1.0</ambient>\n"
      << "          <diffuse>1.0 0.5 0.2 1.0</diffuse>\n"
      << "          <specular>0.2 0.2 0.2 1.0</specular>\n"
      << "        </material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <cylinder>\n"
      << "            <radius>" << radius << "</radius>\n"
      << "            <length>" << _length << "</length>\n"
      << "          </cylinder>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n";

  /// 缓存基础 SDF（供重建时复用）
  this->CacheNormalizedSDF(_instanceName, baseSDF.str());
  this->staticCache_[_instanceName] = false;

  /// 缓存默认浮力体配置（供编辑器还原）
  {
    BuoyancyConfig cfg;
    cfg.linkName = "base_link";
    BuoyancyBodyConfig body;
    body.name = "cylinder_hull";
    body.type = PrimitiveType::Cylinder;
    body.cyl.radius = radius;
    body.cyl.length = _length;
    body.pose[0] = 0; body.pose[1] = 0; body.pose[2] = 0;
    body.pose[3] = 0; body.pose[4] = 0; body.pose[5] = 0;
    cfg.bodies.push_back(body);
    this->buoyancyConfigCache_[_instanceName] = cfg;
  }

  if (_skipSpawn)
  {
    std::cout << "SpawnCylinderInGazebo: 仅缓存 SDF+配置，延迟 spawn" << std::endl;
    return true;
  }

  /// 拼接完整 SDF（基础 + 浮力插件 + 控制器插件）
  std::ostringstream sdf;
  sdf << baseSDF.str()
      << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
      << "        name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <link_name>base_link</link_name>\n"
      << "      <enable_buoyancy>true</enable_buoyancy>\n"
      << "      <enable_hydro>false</enable_hydro>\n"
      << "      <fluid_density>1000</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <buoyancy name=\"cylinder_hull\">\n"
      << "        <geometry>\n"
      << "          <cylinder>\n"
      << "            <radius>" << radius << "</radius>\n"
      << "            <length>" << _length << "</length>\n"
      << "          </cylinder>\n"
      << "        </geometry>\n"
      << "        <pose>0 0 0 0 0 0</pose>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "  </model>\n"
      << "</sdf>\n";

  std::cout << "SpawnCylinderInGazebo: 直径=" << _diameter
            << ", 长度=" << _length << ", 密度=" << _density
            << ", 质量=" << mass << std::endl;

  /// 通过实体工厂服务生成模型
  std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 20000;

  req.set_name(_instanceName);
  req.set_allow_renaming(true);

  auto pose = req.mutable_pose();
  pose->mutable_position()->set_x(_pos[0]);
  pose->mutable_position()->set_y(_pos[1]);
  pose->mutable_position()->set_z(_pos[2]);

  /// 欧拉角 → 四元数转换（_pos[3..5] = roll, pitch, yaw）
  gz::math::Quaterniond q(_pos[3], _pos[4], _pos[5]);
  pose->mutable_orientation()->set_x(q.X());
  pose->mutable_orientation()->set_y(q.Y());
  pose->mutable_orientation()->set_z(q.Z());
  pose->mutable_orientation()->set_w(q.W());

  req.set_sdf(sdf.str());

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / **结果检查**只有一处实现）
  bool executed = CreateEntityViaService(req);

  if (executed)
  {
    std::cout << "服务调用成功create" << std::endl;
    uint32_t id = 0;
    bool found = false;
    uint32_t maxTryTimes = 100;
    /// ★ 2026-09-25（⑩）：先查再睡
    found = this->GetModelIDByInstanceNameFromGazebo(_instanceName, id);
    while ((!found) && (maxTryTimes-- > 0))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      found = this->GetModelIDByInstanceNameFromGazebo(_instanceName, id);
    }
    if (found)
    {
      this->modelList[_instanceName] = id;
      this->CacheNormalizedSDF(_instanceName, sdf.str());
      {
        SensorConfig sc;
        GetCachedSensorConfig(_instanceName, sc);
        NotifyModelSpawned(_instanceName, sc);
      }
      CacheModelFootprint(_instanceName);
      return true;
    }
    std::cout << "SpawnCylinderInGazebo: 未找到模型ID" << std::endl;
    return false;
  }

  std::cout << "服务调用失败create" << std::endl;
  return false;
}

/////////////////////////////////////////////////

namespace
{
/// 水动力标签的唯一生成处（2026-10-25）：SdfBuilder 与 SpawnMathHull 共用，
/// 提取 SDF 里所有 `<plugin filename="...">` 的文件名（2026-10-25）
/// 用于重建后的"插件不丢失"不变量检查。
std::vector<std::string> PluginFileNamesInSdf(const std::string &_sdf)
{
  std::vector<std::string> names;
  const std::string key = "<plugin filename=\"";
  size_t pos = 0;
  while ((pos = _sdf.find(key, pos)) != std::string::npos)
  {
    pos += key.size();
    const size_t end = _sdf.find('"', pos);
    if (end == std::string::npos) break;
    names.push_back(_sdf.substr(pos, end - pos));
    pos = end;
  }
  return names;
}

/// 避免"同一套标签写两遍、改一处漏一处"。
std::string BuildHydroSdfTags(const keship::HydroConfig &_hc)
{
  std::ostringstream hs;
  if (_hc.dampingMode == "pose_based")
  {
    hs << "      <damping_mode>pose_based</damping_mode>\n";
    return hs.str();
  }
  if (!_hc.dampingTerms.empty())
  {
    hs << "      <damping>\n";
    for (const auto &t : _hc.dampingTerms)
    {
      hs << "        <term target=\"" << t.target
         << "\" coefficient=\"" << t.coefficient << "\">\n";
      for (const auto &f : t.factors)
        hs << "          <factor dof=\"" << f.dof
           << "\" order=\"" << f.order << "\"/>\n";
      hs << "        </term>\n";
    }
    hs << "      </damping>\n";
  }
  const char *dNames[] = {"Xu","Yv","Zw","Kp","Mq","Nr","Yr","Nv"};
  hs << "      <linear_damping>\n";
  for (int i = 0; i < 8; i++)
    hs << "        <" << dNames[i] << ">" << _hc.damping[i] << "</" << dNames[i] << ">\n";
  hs << "      </linear_damping>\n";
  return hs.str();
}
}  // namespace

/////////////////////////////////////////////////
/// 数学船型默认水动力系数（2026-10-25）
///
/// 轻量档（_full=false）只给线性 8 项：
///   Zw/Kp/Mq 用**静水恢复刚度法** c = 2ζ√(k·I)：
///     A_wp 取自本船型体积曲线 d∇/dT（与 DisplacementVerifyDialog 同源，非矩形粗估）
///     垂荡 k = ρ·g·A_wp                     → Zw = 2·ζ_h·√(k·m)
///     横摇 k = ρ·g·(A_wp·B²/12)，I = ixx    → Kp = 2·ζ_r·√(k·ixx)
///     纵摇 k = ρ·g·(A_wp·L²/12)，I = iyy    → Mq = 2·ζ_r·√(k·iyy)
///   ζ 取 0.15 / 0.10（与插件 pose_based 同值，全项目一致）
///   Xu/Yv/Nr 用 m 的比例（与 CylinderTestDialog 的 0.05·m 同风格；无解析式，属工程近似）
///
/// 完整档（_full=true）追加：
///   Morison 二次阻力 0.5·ρ·Cd·A（船体投影面积），以及势流附加质量 6×6 对角阵
///   （附加质量按排水体积比例，量级参照细长体/圆柱理论，见每行注释）
///
/// 所有线性系数下限 1e-3：插件在 enable_hydro=true 而 fossen 无效时会**整体 return**
/// （连浮力都不施加），退化参数不能把浮力一起弄没。
HydroConfig ModelManager::EstimateMathHullHydro(const ShipHullSpec &_spec, bool _full)
{
  const double rho = 1000.0;
  const double g = 9.81;

  ShipHullGenerator hull(_spec);
  const auto &hp = hull.hydroProps();

  const double L = _spec.L, B = _spec.B, D = _spec.D;
  /// 质量口径与 SpawnMathHull 的 <mass> 一致（rho × 均质船体体积）
  const double m = _spec.rho * hp.volume;
  const double ixx = m / 12.0 * (B * B + D * D);
  const double iyy = m / 12.0 * (L * L + D * D);
  const double izz = m / 12.0 * (L * L + B * B);

  /// 水线面面积：优先用体积曲线的中央差分（= d∇/dT，与船型自洽）
  double Awp = (hp.AwD > 1e-9) ? hp.AwD : L * B * 0.8;
  {
    const auto &dc = hp.draftCurve;
    const auto &vc = hp.volumeCurve;
    if (dc.size() >= 2 && vc.size() == dc.size())
    {
      const size_t k = dc.size() / 2;
      double a = 0.0;
      if (k > 0 && k + 1 < dc.size() && (dc[k + 1] - dc[k - 1]) > 1e-12)
        a = (vc[k + 1] - vc[k - 1]) / (dc[k + 1] - dc[k - 1]);
      else if ((dc[1] - dc[0]) > 1e-12)
        a = (vc[1] - vc[0]) / (dc[1] - dc[0]);
      if (a > 1e-6)
        Awp = a;
    }
  }

  const double zetaHeave = 0.15;
  const double zetaRot = 0.10;

  HydroConfig cfg;
  cfg.dampingMode = "fossen";
  cfg.damping[0] = std::max(1e-3, 0.05 * m);                 // Xu：漂移线性摩擦（近似）
  cfg.damping[1] = std::max(1e-3, 0.05 * m);                 // Yv：同上
  cfg.damping[2] = std::max(1e-3, 2.0 * zetaHeave *
      std::sqrt(rho * g * Awp * m));                         // Zw：垂荡
  cfg.damping[3] = std::max(1e-3, 2.0 * zetaRot *
      std::sqrt(rho * g * (Awp * B * B / 12.0) * ixx));      // Kp：横摇
  cfg.damping[4] = std::max(1e-3, 2.0 * zetaRot *
      std::sqrt(rho * g * (Awp * L * L / 12.0) * iyy));      // Mq：纵摇
  cfg.damping[5] = std::max(1e-3, 0.05 * izz);               // Nr：首摇（近似）
  cfg.damping[6] = 0.0;                                      // Yr：耦合项不给
  cfg.damping[7] = 0.0;                                      // Nv：耦合项不给

  if (_full)
  {
    const double T = (hp.draft > 1e-6) ? hp.draft : 0.5 * D;
    const double A_surge = B * T;   ///< 迎流投影（船体横剖面）
    const double A_sway = L * T;    ///< 侧向投影（船体侧面）

    auto addQuad = [&cfg](int _dof, double _coeff) {
      if (_coeff <= 0.0)
        return;
      DampingTerm t;
      t.target = _dof;
      t.coefficient = _coeff;
      DampingFactor f;
      f.dof = _dof;
      f.order = 2;
      t.factors.push_back(f);
      cfg.dampingTerms.push_back(t);
    };

    /// Morison 型二次阻力：0.5·ρ·Cd·A（Cd 取钝体经验值）
    addQuad(0, 0.5 * rho * 0.80 * A_surge);                 // 纵荡：V 形首尾，Cd≈0.8
    addQuad(1, 0.5 * rho * 1.00 * A_sway);                  // 横荡：侧面平板，Cd≈1.0
    addQuad(2, 0.5 * rho * 1.00 * Awp);                     // 垂荡：水线面，Cd≈1.0
    /// 转动项按"力 × 力臂²"估：横摇力臂 B/2 的等效矩 ≈ A_side·B²/12
    addQuad(3, 0.5 * rho * 1.00 * A_sway * (B * B / 12.0)); // 横摇
    addQuad(4, 0.5 * rho * 1.00 * Awp * (L * L / 12.0));    // 纵摇
    addQuad(5, 0.5 * rho * 1.00 * A_sway * (L * L / 12.0)); // 首摇

    /// 附加质量（势流，6×6 对角；量级参照细长体/圆柱理论）
    const double rhoV = rho * hp.volume;
    cfg.addedMass[0 * 6 + 0] = 0.10 * rhoV;                   // 纵荡：细长体轴向小
    cfg.addedMass[1 * 6 + 1] = 0.90 * rhoV;                   // 横荡：二维绕流 C_a≈1
    cfg.addedMass[2 * 6 + 2] = 0.90 * rhoV;                   // 垂荡：同上
    cfg.addedMass[3 * 6 + 3] = 0.20 * rhoV * (B * B / 12.0);  // 横摇
    cfg.addedMass[4 * 6 + 4] = 0.25 * rhoV * (L * L / 12.0);  // 纵摇
    cfg.addedMass[5 * 6 + 5] = 0.25 * rhoV * (L * L / 12.0);  // 首摇
  }

  return cfg;
}

/////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/// 数学船型 SDF 组装（纯函数，2026-10-25）
/// 不写文件、不触 gz：给定 (名称/规格/插件选项/两个网格路径) 即得确定 SDF，
/// 便于离线单测覆盖"名称/质量/惯性/浮力网格/水动力标签/附加质量"这些易错点。
std::string ModelManager::MakeMathHullSDF(
    const std::string &_instanceName, const ShipHullSpec &_spec,
    const MathHullPluginOpts &_opts,
    const std::string &visPath, const std::string &buoyPath)
{
  ShipHullGenerator hull(_spec);
  const double L = _spec.L, B = _spec.B, D = _spec.D;
  const double m = _spec.rho * hull.hydroProps().volume;
  const double ixx = m / 12.0 * (B * B + D * D);
  const double iyy = m / 12.0 * (L * L + D * D);
  const double izz = m / 12.0 * (L * L + B * B);

  /// 水动力标签（2026-10-25）：优先用完整 HydroConfig（与圆柱体/网格模型同一套标签），
  /// 否则退回旧的 3 项 <linear_damping>（keship_ctl --damping 兼容路径）
  std::string hydroTags;
  if (_opts.hydro)
    hydroTags = BuildHydroSdfTags(*_opts.hydro);
  else if (_opts.hasDamping())
  {
    std::ostringstream hs;
    hs << "      <linear_damping>\n"
       << "        <Zw>" << _opts.damping[0] << "</Zw>\n"
       << "        <Kp>" << _opts.damping[1] << "</Kp>\n"
       << "        <Mq>" << _opts.damping[2] << "</Mq>\n"
       << "      </linear_damping>\n";
    hydroTags = hs.str();
  }

  // SDF (对齐 MakeSDFStringFromValidFileType 和 HullStabilityDialog)
  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n<sdf version=\"1.10\">\n"
      << "  <model name=\"" << _instanceName << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <inertial>\n"
      << "        <pose>0 0 " << (-D / 2.0) << " 0 0 0</pose>\n"
      << "        <mass>" << m << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>" << ixx << "</ixx><ixy>0</ixy><ixz>0</ixz>\n"
      << "          <iyy>" << iyy << "</iyy><iyz>0</iyz>\n"
      << "          <izz>" << izz << "</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry><mesh><uri>file://" << visPath << "</uri></mesh></geometry>\n"
      << "        <material><ambient>0.6 0.5 0.4 1</ambient><diffuse>0.7 0.6 0.5 1</diffuse></material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry><mesh><uri>file://" << visPath << "</uri></mesh></geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\" name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <link_name>base_link</link_name>\n"
      << "      <enable_buoyancy>true</enable_buoyancy>\n"
      << "      <enable_hydro>" << (_opts.enableHydro ? "true" : "false") << "</enable_hydro>\n"
      /// 以下两段为 opt-in 插入：_opts 全默认时产出与改动前**逐字节一致**
      /// 浸水体积发布（排水量实验取"实测 ∇"用）
      << (_opts.publishSubVolume
            ? std::string("      <publish_subvolume>true</publish_subvolume>\n")
            : std::string())
      /// 线性阻尼（fossen 路径）：网格船体走不了 pose_based（需 hasCylinderGeometry_），
      /// 不给系数就是零阻尼 → 长时间垂荡
      << hydroTags
      << "      <fluid_density>1000</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <buoyancy name=\"hull\">\n"
      << "        <geometry>\n"
      << "          <mesh><uri>" << buoyPath << "</uri></mesh>\n"
      << "        </geometry>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "  </model>\n</sdf>\n";

  std::string out = sdf.str();
  if (_opts.hydro)
  {
    bool anyAm = false;
    for (double v : _opts.hydro->addedMass)
      if (v != 0.0) { anyAm = true; break; }
    if (anyAm)
      InjectFluidAddedMass(out, _opts.hydro->addedMass.data());
  }
  return out;
}

bool ModelManager::SpawnMathHull(
    const std::string &_instanceName,
    const ShipHullSpec &_spec,
    double _pos[6],
    const MathHullPluginOpts &_opts)
{
  // 生成 OBJ 文件 (Phase B: 模型加载菜单)
  std::string dir = keship::KeshipModelsDir();
  if (std::system(("mkdir -p " + dir).c_str()) != 0)
    std::cerr << "SpawnMathHull: 创建目录失败 " << dir << std::endl;

  ShipHullGenerator hull(_spec);
  /// ★ 文件名带**网格内容指纹**（2026-09-19 修，与实验对话框同一处错）：
  /// gz 的 `gz::common::MeshManager` **按文件路径**缓存网格 —— 同路径换内容仍返回旧网格。
  /// 原先只按实例名命名（`<名>_vis.obj`）→ 同名实例换参数重新生成时，
  /// 界面里显示的仍是旧船（GUI 进程常驻、缓存永不失效）。
  /// 典型触发：`keship_ctl model spawn-hull hullA --L 6` → 删除 → `--L 9` 同名重生。
  /// 加指纹后几何一变 URI 就变；几何相同则命中缓存是正确的（内容本就一致）。
  const std::string meshTag = hull.meshTag();
  std::string visPath = dir + _instanceName + "_vis_" + meshTag + ".obj";
  std::string buoyPath = dir + _instanceName + "_buoy_" + meshTag + ".obj";
  hull.writeVisualOBJ(visPath);
  hull.writeBuoyancyOBJ(buoyPath);

  double L = _spec.L, B = _spec.B, D = _spec.D;
  double actualVol = hull.hydroProps().volume;
  double m = _spec.rho * actualVol;
  double ixx = m / 12.0 * (B * B + D * D);
  double iyy = m / 12.0 * (L * L + D * D);
  double izz = m / 12.0 * (L * L + B * B);

  const std::string sdfStr = MakeMathHullSDF(_instanceName, _spec, _opts, visPath, buoyPath);

  // Spawn via EntityFactory
  std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  req.set_name(_instanceName);
  req.set_allow_renaming(true);
  auto *pose = req.mutable_pose();
  pose->mutable_position()->set_x(_pos[0]);
  pose->mutable_position()->set_y(_pos[1]);
  pose->mutable_position()->set_z(_pos[2]);
  gz::math::Quaterniond q(_pos[3], _pos[4], _pos[5]);
  pose->mutable_orientation()->set_x(q.X());
  pose->mutable_orientation()->set_y(q.Y());
  pose->mutable_orientation()->set_z(q.Z());
  pose->mutable_orientation()->set_w(q.W());
  req.set_sdf(sdfStr);

  gz::msgs::Boolean rep;
  bool result = false;
  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  // 解析 entity ID 并加入模型列表
  uint32_t id = 0;
  bool found = false;
  int maxTry = 40;
  /// ★ 2026-09-25（⑩）：先查再睡
  found = this->GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && maxTry-- > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = this->GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  }
  if (found) {
    this->modelList[_instanceName] = id;
    this->CacheNormalizedSDF(_instanceName, sdfStr);
    SensorConfig scEmpty;
    NotifyModelSpawned(_instanceName, scEmpty);
    this->staticCache_[_instanceName] = false;
    CacheModelFootprint(_instanceName);
    std::cout << "SpawnMathHull: " << _instanceName << " (id=" << id << ") 已生成" << std::endl;
    return true;
  }
  std::cerr << "SpawnMathHull: 无法解析 entity ID" << std::endl;
  return false;
}

/////////////////////////////////////////////////
bool ModelManager::LoadMeshDataFromDae(const std::string &_daePath, MeshData &_meshData)
{
  std::cout << "LoadMeshDataFromDae: Assimp: 加载DAE文件: " << _daePath << std::endl;
  Assimp::Importer importer;
  /// 导入选项： triangulate 三角化，flipUVs 翻转UV坐标，等等
  const aiScene *scene = importer.ReadFile(_daePath,
                                           aiProcess_Triangulate |               // 三角化
                                               aiProcess_FlipUVs |               // 翻转UV坐标
                                               aiProcess_JoinIdenticalVertices | // 合并相同顶点
                                               aiProcess_GenSmoothNormals |      // 生成平滑法线
                                               aiProcess_CalcTangentSpace |      // 计算切线空间
                                               aiProcess_PreTransformVertices);  // 烘焙节点矩阵，与 LoadMeshFromFile 一致
  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
  {
    std::cerr << "LoadMeshDataFromDae: Assimp: 加载DAE文件失败: " << importer.GetErrorString() << std::endl;
    return false;
  }
  std::cout << "LoadMeshDataFromDae: Assimp: 网格数量scene->mNumMeshes: " << scene->mNumMeshes << std::endl;

  _meshData.vertices.clear();
  _meshData.indices.clear();
  _meshData.normals.clear();
  _meshData.texCoords.clear();

  /// 遍历所有网格（通常一个模型只有一个网格，但为了健壮性我们合并它们）
  for (unsigned m = 0; m < scene->mNumMeshes; m++)
  {
    aiMesh *mesh = scene->mMeshes[m];
    std::cout << "LoadMeshDataFromDae: Assimp: 顶点个数mesh->mNumVertices: " << mesh->mNumVertices << std::endl;
    std::cout << "LoadMeshDataFromDae: Assimp: 面索引数量mesh->mNumFaces: " << mesh->mNumFaces << std::endl;
    /// 记录当前顶点起始索引（用于索引偏移）
    unsigned vertexOffset = _meshData.vertices.size() / 3;
    /// 提取顶点位置
    for (unsigned v = 0; v < mesh->mNumVertices; v++)
    {
      _meshData.vertices.push_back(mesh->mVertices[v]);
    }
    /// 提取面索引（已经三角化）
    for (unsigned f = 0; f < mesh->mNumFaces; f++)
    {
      aiFace face = mesh->mFaces[f];
      /// 确保是三角形（索引数量为3）
      if (face.mNumIndices != 3)
      {
        std::cerr << "LoadMeshDataFromDae: Assimp: 索引数量mNumIndices不是3，跳过: " << face.mNumIndices << std::endl;
        continue;
      }
      _meshData.indices.push_back(face.mIndices[0] + vertexOffset);
      _meshData.indices.push_back(face.mIndices[1] + vertexOffset);
      _meshData.indices.push_back(face.mIndices[2] + vertexOffset);
    }
    /// 提取顶点法线（如果存在）
    if (mesh->HasNormals())
    {
      for (unsigned v = 0; v < mesh->mNumVertices; v++)
      {
        _meshData.normals.push_back(mesh->mNormals[v].x);
        _meshData.normals.push_back(mesh->mNormals[v].y);
        _meshData.normals.push_back(mesh->mNormals[v].z);
      }
    }
  }

  /// 生成四面体
  // this->GenerateTetrahedronsFromMesh(_meshData);

  ///
  return true;
}

/////////////////////////////////////////////////
/**
 * @brief 从 SDF 字符串加载模型到 Gazebo 中
 * @param _instanceName 实例名称（可以与模型名称不同）
 * @param _sdfContent SDF 字符串内容
 * @return true 成功加载模型
 * @return false 加载模型失败
 */
bool ModelManager::SpawnModelInGazeboFromSDFString(
    // const std::string &_worldName,
    const std::string &_instanceName,
    const std::string &_sdfContent)
{
  // this->worldName = _worldName;
  // 统一坐标系：mesh uri → 规范化缓存 OBJ（spawn 与缓存一致）
  std::string sdfNormalized = this->NormalizeMeshUrisInSDF(_sdfContent);
  // 准备服务调用
  std::string service = "/world/" + this->worldName + "/create/blocking";
  // 创建请求消息
  gz::msgs::EntityFactory msg;
  // 设置实例名称
  msg.set_name(_instanceName); // 实例名称（可以与模型名称不同）
  msg.set_allow_renaming(true);
  //
  if (sdfNormalized.empty())
  {
    std::string sdfContent = "<?xml version=\"1.0\"?><sdf version=\"1.9\"><model name=\"HHHHH\"><pose>0 0 1 0 0 0</pose><link name=\"link\"><inertial><mass>1.0</mass></inertial><collision name=\"collision\"><geometry><box><size>1 1 1</size></box></geometry></collision><visual name=\"visual\"><geometry><box><size>100 100 100</size></box></geometry></visual></link></model></sdf>";
    msg.set_sdf(sdfContent); // 使用SDF字符串
  }
  else
  {
    msg.set_sdf(sdfNormalized); // 使用SDF字符串
  }

  // 调用服务（同步方式）
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 20000; // 20秒超时

  std::cout << "正在调用服务: " << service << std::endl;

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(msg)) return false;

  // 等待模型 ID
  uint32_t id = 0;
  bool found = false;
  uint32_t tries = 100;
  /// ★ 2026-09-25 性能修复（⑩）：**先查再睡**。
  /// 原实现先睡一个周期再查 —— 实体若已就绪（常态）就白等 200ms，而每次
  /// spawn/rebuild 都付这个代价。
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  }

  if (found)
  {
    this->modelList[_instanceName] = id;
    this->CacheNormalizedSDF(_instanceName, sdfNormalized);
    {
      bool isSt = _sdfContent.find("<static>true</static>") != std::string::npos
               || _sdfContent.find("<static>1</static>") != std::string::npos;
      this->staticCache_[_instanceName] = isSt;
    }
    {
      SensorConfig sc;
      GetCachedSensorConfig(_instanceName, sc);
      NotifyModelSpawned(_instanceName, sc);
    }
    CacheModelFootprint(_instanceName);
    return true;
  }

  std::cerr << "SpawnModelInGazeboFromSDFString: 未找到模型ID" << std::endl;
  return false;
}

/////////////////////////////////////////////////
bool ModelManager::DeleteModelInGazeboByName(
    const std::string &_instanceName, bool _preserveSource)
{
  // TODO: 实现删除模型功能
  // this->worldName = _worldName;
  // 准备服务调用
  std::string service = "/world/" + this->worldName + "/remove";
  // 创建请求消息
  gz::msgs::Entity msg;
  // 设置实例名称
  msg.set_name(_instanceName); // 实例名称（可以与模型名称不同）
  msg.set_type(gz::msgs::Entity::MODEL);

  // 调用服务（同步方式）
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 2000; // 2秒超时

  std::cout << "正在调用服务: " << service << ";" << _instanceName << std::endl;

  bool executed = this->node->Request(service, msg, timeout, rep, result);
  if (executed && result && rep.data())
  {
    this->modelList.erase(_instanceName);
    this->modelBBoxCache_.erase(_instanceName);
    // _preserveSource=true（Rebuild 系列）：保留来源元数据，重建后树分组不丢失
    if (!_preserveSource)
      this->modelSource_.erase(_instanceName);
    NotifyModelDeleted(_instanceName);
    std::cout << "服务调用成功，rep.data()=" << rep.data() << std::endl;
  }
  else
  {
    std::cout << "服务调用失败，rep.data()=" << rep.data() << std::endl;
  }

  return executed;
}

/////////////////////////////////////////////////
bool ModelManager::DeleteModelInGazeboByIndex(int _index)
{
  // if (_index < 0 || _index >= this->modelList.size())
  // {
  //   std::cout << "keship 模型索引无效" << _index << std::endl;
  //   return false;
  // }
  // auto it = this->modelList.begin();
  // std::advance(it, _index);
  auto modelNameAndID = this->GetModelNameAndIDByIndex(_index);
  if (modelNameAndID.first == "")
    return false;
  bool success = this->DeleteModelInGazeboByName(modelNameAndID.first);
  return success;
}

std::pair<std::string, uint32_t> ModelManager::GetModelNameAndIDByIndex(int _index)
{
  if (_index < 0 || _index >= this->modelList.size())
  {
    std::cout << "keship 模型索引无效" << _index << std::endl;
    return {"", 0};
  }
  auto it = this->modelList.begin();
  std::advance(it, _index);
  return *it;
}

bool ModelManager::GetModelPoseByIndex(int _index, gz::msgs::Pose &_pose)
{
  auto modelNameAndID = this->GetModelNameAndIDByIndex(_index);
  if (modelNameAndID.first == "")
    return false;
  return this->GetModelPoseByName(modelNameAndID.first, _pose);
}
/**
 * @brief 获取模型位姿
 *
 * @param _instanceName 模型实例名称
 * @param _pose 得到的模型位姿，包含位置和旋转，格式为 [x, y, z, roll, pitch, yaw]
 */
bool ModelManager::GetModelPoseByName(const std::string &_instanceName, gz::msgs::Pose &_pose)
{
  /// 准备服务调用 - 使用Gazebo的状态查询服务
  std::string service = "/world/" + this->worldName + "/scene/info";
  /// 创建请求消息
  gz::msgs::Empty req;
  /// 创建响应消息
  gz::msgs::Scene rep;
  bool result = false;
  unsigned int timeout = 500; // 500毫秒超时

  ///
  bool executed = this->node->Request(service, req, timeout, rep, result);
  if (!executed || !result)
  {
    std::cout << "keship 模型位姿查询服务调用失败" << std::endl;
    return false;
  }
  // 解析响应消息，提取模型位姿
  for (int i = 0; i < rep.model_size(); ++i)
  {
    const auto &model = rep.model(i);
    if (model.name() == _instanceName)
    {
      _pose = model.pose();
      return true;
    }
  }
  return false;
}

/// ★★ 2026-09-28 重写（张老师 2026-09-25 拍板「delete-all 应当删除所有模型、水域、子世界」）：
///   **改为枚举活动场景**（`/world/<w>/scene/info`），不再遍历本进程的 `modelList`。
///
/// 动因（旧实现的两个缺陷）：
///   ① `keship_ctl` 每次调用都是**新进程** ⇒ `modelList` 恒空
///      ⇒ 旧实现**什么都不删，却 `return true`**（"假成功"，与 09-24 修的 P1-7 同族）；
///   ② 旧实现无条件 `return true`，即使部分删除失败也报成功。
///
/// ⚠ **必须排除的三类实体**（否则会误删 world 基础设施）：
///   · `sun` / `ground_plane` —— world 自带，删了会破坏渲染；
///   · `waves_*` —— **水面视觉**，由水域机制（`WaterAreaVisualManager`）管理，
///     由调用方在"清空水域"那一步统一删除（见 `MenuPluginKeship::OnBtn_DeleteAllModels`）。
bool ModelManager::DeleteAllModelsInGazebo()
{
  /// 取一帧场景（与 GetModelIDByInstanceNameFromGazebo 同一服务）
  gz::msgs::Empty req;
  gz::msgs::Scene scene;
  bool result = false;
  const std::string service = "/world/" + this->worldName + "/scene/info";
  if (!this->node->Request(service, req, 2000, scene, result) || !result)
  {
    std::cerr << "DeleteAllModelsInGazebo: 取场景失败（" << service
              << "）—— 仿真没在运行？" << std::endl;
    return false;
  }

  /// 收集待删名单（排除 world 自带与水面视觉）
  std::vector<std::string> names;
  names.reserve(scene.model_size());
  for (int i = 0; i < scene.model_size(); ++i)
  {
    const std::string &n = scene.model(i).name();
    if (n == "sun" || n == "ground_plane") continue;   /// world 自带
    if (n.rfind("waves_", 0) == 0) continue;           /// 水面视觉（由水域机制删）
    names.push_back(n);
  }

  bool allSuccess = true;
  for (const auto &name : names)
  {
    if (!this->DeleteModelInGazeboByName(name))
    {
      std::cerr << "DeleteAllModelsInGazebo: 删除失败 " << name << std::endl;
      allSuccess = false;
    }
  }

  /// 场景已变 ⇒ 本进程缓存一律清空（不论成败）
  this->modelList.clear();

  std::cout << "DeleteAllModelsInGazebo: 共 " << names.size() << " 个模型，"
            << (allSuccess ? "全部删除成功" : "**有删除失败**") << std::endl;

  /// ★ 返回**真实**成败（旧实现恒 true ⇒ 上层无法感知失败）
  return allSuccess;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GenerateTetrahedronsFromMesh(MeshData &_meshData)
{
  // 清空现有的四面体列表
  _meshData.tetrahedrons.clear();

  // 检查是否有足够的顶点和索引
  if (_meshData.vertices.size() < 9 || _meshData.indices.size() < 3)
  {
    std::cerr << "GenerateTetrahedronsFromMesh: 网格数据不足，无法生成四面体" << std::endl;
    return false;
  }

  // 计算模型的中心点，作为四面体的第四个顶点
  aiVector3D center(0, 0, 0);
  int vertexCount = _meshData.vertices.size() / 3;
  for (int i = 0; i < vertexCount; i++)
  {
    center = center + _meshData.vertices[i * 3];
  }

  center /= vertexCount;

  // 为每个三角形生成一个四面体
  for (int i = 0; i < _meshData.indices.size(); i += 3)
  {
    // 获取三角形的三个顶点
    unsigned int i0 = _meshData.indices[i];
    unsigned int i1 = _meshData.indices[i + 1];
    unsigned int i2 = _meshData.indices[i + 2];

    // 检查索引是否有效
    if (i0 * 3 + 2 >= _meshData.vertices.size() ||
        i1 * 3 + 2 >= _meshData.vertices.size() ||
        i2 * 3 + 2 >= _meshData.vertices.size())
    {
      std::cerr << "GenerateTetrahedronsFromMesh: 无效的顶点索引" << std::endl;
      continue;
    }
  }

  std::cout << "GenerateTetrahedronsFromMesh: 成功生成 " << _meshData.tetrahedrons.size() << " 个四面体" << std::endl;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GetCachedBuoyancyConfig(
    const std::string &_name, BuoyancyConfig &_out) const
{
  auto it = buoyancyConfigCache_.find(_name);
  if (it == buoyancyConfigCache_.end())
    return false;
  _out = it->second;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
std::map<std::string, uint32_t> ModelManager::GetModelList() const
{
  // TODO: 实现获取模型列表功能
  // std::cout << "TODO: 获取模型列表在 " << this->worldName << std::endl;
  return this->modelList;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
std::string ModelManager::MakeBuoyancyPluginSDF(
    const BuoyancyConfig &_config, const HydroConfig *_hydro)
{
  bool hasHydro = (_hydro != nullptr);
  std::ostringstream sdf;
  sdf << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
      << " name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <link_name>" << _config.linkName << "</link_name>\n"
      /// ★ 2026-09-30 优化计划 F3：**没有浮力体时不要启用浮力**。
      ///   原先恒为 true ⇒ 插件会走 `LoadSingleMeshBuoyancy`（拿**整船碰撞网格**当浮力体）
      ///   ⇒ 而 `loadMeshData` 的顶点索引查找是 O(三角数 × 顶点数)，实测会**挂死**（>900 s）✗
      ///   现在：bodies 非空才 true；为空（如"只开水动力"）⇒ false，浮力块整体跳过，水动力不受影响 ✓
      << "      <enable_buoyancy>" << (_config.bodies.empty() ? "false" : "true")
      << "</enable_buoyancy>\n"
      << "      <enable_hydro>" << (hasHydro ? "true" : "false") << "</enable_hydro>\n"
      << "      <fluid_density>" << _config.fluidDensity << "</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n";


  for (const auto &body : _config.bodies)
  {
    sdf << "      <buoyancy name=\"" << body.name << "\">\n"
        << "        <geometry>\n";

    if (body.type == PrimitiveType::Box)
    {
      sdf << "          <box><size>"
          << body.box.dx << " " << body.box.dy << " " << body.box.dz
          << "</size></box>\n";
    }
    else
    {
      sdf << "          <cylinder><radius>" << body.cyl.radius
          << "</radius><length>" << body.cyl.length
          << "</length></cylinder>\n";
    }

    sdf << "        </geometry>\n"
        << "        <pose>"
        << body.pose[0] << " " << body.pose[1] << " " << body.pose[2] << " "
        << body.pose[3] << " " << body.pose[4] << " " << body.pose[5]
        << "</pose>\n"
        << "      </buoyancy>\n";
  }

    if (hasHydro)
      sdf << BuildHydroSdfTags(*_hydro);

  sdf << "    </plugin>\n";
  return sdf.str();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::RebuildModelWithBuoyancy(
    const std::string &_instanceName,
    const BuoyancyConfig &_config)
{
  BuoyancyConfig configCopy = _config;

  /// 1. 保存当前 pose（通过 /state 服务获取物理引擎真实位姿）
  double savedPose[6] = {0, 0, 0, 0, 0, 0};
  {
    auto idIt = this->modelList.find(_instanceName);
    if (idIt != this->modelList.end())
    {
      if (GetModelPoseByIDFromGazebo(idIt->second, savedPose))
      {
        std::cout << "RebuildModelWithBuoyancy: 保存pose "
                  << savedPose[0] << " " << savedPose[1] << " " << savedPose[2] << " "
                  << savedPose[3] << " " << savedPose[4] << " " << savedPose[5] << std::endl;
      }
      else
      {
        std::cerr << "RebuildModelWithBuoyancy: /state 获取pose失败，使用默认值" << std::endl;
      }
    }
  }

  /// ★ 2026-09-28 修（**成套**：5 个 Rebuild* 同族缺陷之一）：**先查缓存，再做破坏性删除**。
  ///   原实现是「先删模型 → 后查缓存」⇒ 缓存缺失时模型**已被删掉**，随后才 return false
  ///   ⇒ **场景凭空少一个模型**（数据丢失级；同族反模式「没做到却报成功」的破坏性版本）。
  ///   本函数会重建实体 ⇒ 一切「可能失败」的准备工作必须在删除**之前**做完。
  if (sdfCache_.find(_instanceName) == sdfCache_.end())
  {
    std::cerr << "RebuildModelWithBuoyancy: 本进程没有该模型的 SDF 缓存，**拒绝重建以免丢模型**"
                 "（请先在本进程 load-ksm / spawn）: "
              << _instanceName << std::endl;
    return false;   ///< **不删模型**
  }

  /// 2. 删除旧模型 + 等待确认删除完毕
  if (!DeleteModelInGazeboByName(_instanceName, true))
  {
    std::cerr << "RebuildModelWithBuoyancy: 删除模型失败" << std::endl;
    return false;
  }

  /// 等待 Gazebo 彻底清理（删除是同步返回但 ECM 更新可能有延迟）
  {
    int retries = 20;
    uint32_t staleId = 0;
    while (retries-- > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!GetModelIDByInstanceNameFromGazebo(_instanceName, staleId))
        break;
    }
    if (retries <= 0)
      std::cerr << "RebuildModelWithBuoyancy: 警告 — 模型删除后仍残留" << std::endl;
  }

  /// 3. 从缓存取基础 SDF 并插入新浮力插件
  /// ★ 2026-09-28：缓存在**删除之前**已校验存在（见上方检查）⇒ 此处必然命中。
  ///   原先「缓存缺失 ⇒ 悄悄换成 1 m×1 m×1 m 方块」的回退**已删除** ——
  ///   那是「悄悄把用户模型换掉」的静默错误结果，不是可接受的行为。
  auto cacheIt = this->sdfCache_.find(_instanceName);
  std::string baseSDF = cacheIt->second;

  /// M_A 通过 DART <fluid_added_mass> 注入（隐式积分）

  /// 基础 SDF 如含 </model></sdf>，从 </link> 截断（去掉 </link>，后续插入传感器后重新闭合）
  auto lpos = baseSDF.rfind("</link>");
  if (lpos != std::string::npos) baseSDF = baseSDF.substr(0, lpos);

  HydroConfig hydroCfg;
  bool hasHydro = GetCachedHydroConfig(_instanceName, hydroCfg);

  SensorConfig sensorCfg;
  bool hasSensor = GetCachedSensorConfig(_instanceName, sensorCfg);

  std::ostringstream sdf;
  sdf << baseSDF;

  /// 保留已有传感器配置
  if (hasSensor)
    sdf << MakeSensorSDF(sensorCfg, _instanceName, worldName);
  sdf << "    </link>\n";

  /// 保留已有推进器配置
  ThrusterConfig thrustCfg;
  if (GetCachedThrusterConfig(_instanceName, thrustCfg) && !thrustCfg.bodies.empty())
    sdf << MakeThrustPluginSDF(thrustCfg);

  /// 合并的浮力-水动力插件（由 HydroConfig 决定是否 enable_hydro）
  {
    if (hasHydro)
    {
      sdf << MakeBuoyancyPluginSDF(_config, &hydroCfg);
    }
    else
    {
      sdf << MakeBuoyancyPluginSDF(_config);
    }
  }

  sdf << "  </model>\n"
      << "</sdf>\n";

  /// ★ 2026-09-24 修 P0-1：本路径原先漏了 M_A 注入
  ApplyAddedMassToSdf(sdf, hasHydro, hydroCfg);

  /// 4. spawn 新模型（直接在保存的位姿生成，避免 spawn 在原点再移动的中间状态）
  
  
std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 20000;

  req.set_name(_instanceName);
  req.set_allow_renaming(false);
  req.set_sdf(sdf.str());

  /// 设置初始位姿为保存的物理位姿
  auto reqPose = req.mutable_pose();
  reqPose->mutable_position()->set_x(savedPose[0]);
  reqPose->mutable_position()->set_y(savedPose[1]);
  reqPose->mutable_position()->set_z(savedPose[2]);
  gz::math::Quaterniond q(savedPose[3], savedPose[4], savedPose[5]);
  reqPose->mutable_orientation()->set_x(q.X());
  reqPose->mutable_orientation()->set_y(q.Y());
  reqPose->mutable_orientation()->set_z(q.Z());
  reqPose->mutable_orientation()->set_w(q.W());

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  /// 5. 等待模型 ID 就绪（位姿已在 spawn 时设置，无需再移动）
  uint32_t id = 0;
  bool found = false;
  uint32_t maxTryTimes = 100;
  while ((!found) && (maxTryTimes-- > 0))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  }

  if (found)
  {
    this->modelList[_instanceName] = id;
    this->buoyancyConfigCache_[_instanceName] = configCopy;
    NotifyModelSpawned(_instanceName, sensorCfg);
    std::cout << "RebuildModelWithBuoyancy: 完成, id=" << id << std::endl;
    return true;
  }

  std::cerr << "RebuildModelWithBuoyancy: 未找到模型ID" << std::endl;
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GetCachedModelGeometry(const std::string &_name,
    ModelGeometryType &_type, double &_dx, double &_dy, double &_dz,
    double &_radius, double &_length, std::string &_meshPath) const
{
  auto it = this->sdfCache_.find(_name);
  if (it == this->sdfCache_.end())
    return false;

  _type = ModelGeometryType::Box;
  _dx = _dy = _dz = 0.0;
  _radius = _length = 0.0;
  _meshPath.clear();

  const std::string &sdf = it->second;

  /// 查找 <cylinder> 或 <box>
  auto hasTag = [&](const std::string &_tag) -> bool {
    return sdf.find("<" + _tag) != std::string::npos;
  };
  auto extract = [&](const std::string &_tag, const std::string &_sub,
                     double &_val) -> bool {
    auto start = sdf.find("<" + _tag);
    if (start == std::string::npos) return false;
    auto subStart = sdf.find("<" + _sub, start);
    if (subStart == std::string::npos) return false;
    auto valStart = sdf.find(">", subStart);
    auto valEnd = sdf.find("<", valStart + 1);
    if (valStart == std::string::npos) return false;
    std::string valStr = sdf.substr(valStart + 1, valEnd - valStart - 1);
    try { _val = std::stod(valStr); return true; }
    catch (...) { return false; }
  };

  if (hasTag("cylinder"))
  {
    _type = ModelGeometryType::Cylinder;
    extract("cylinder", "radius", _radius);
    extract("cylinder", "length", _length);
    return true;
  }
  else if (hasTag("box"))
  {
    _type = ModelGeometryType::Box;
    auto start = sdf.find("<box");
    if (start != std::string::npos)
    {
      auto sizeStart = sdf.find("<size", start);
      if (sizeStart != std::string::npos)
      {
        auto valStart = sdf.find(">", sizeStart);
        auto valEnd = sdf.find("<", valStart + 1);
        if (valStart != std::string::npos && valEnd != std::string::npos)
        {
          std::string sizeStr = sdf.substr(valStart + 1, valEnd - valStart - 1);
          std::sscanf(sizeStr.c_str(), "%lf %lf %lf", &_dx, &_dy, &_dz);
          return true;
        }
      }
    }
    return false;
  }
  else if (hasTag("mesh"))
  {
    _type = ModelGeometryType::Mesh;
    auto start = sdf.find("<uri>");
    if (start != std::string::npos)
    {
      auto valStart = sdf.find(">", start);
      auto valEnd = sdf.find("<", valStart + 1);
      if (valStart != std::string::npos && valEnd != std::string::npos)
      {
        std::string uri = sdf.substr(valStart + 1, valEnd - valStart - 1);

        /// 去掉 file:// 前缀
        if (uri.find("file://") == 0)
          _meshPath = uri.substr(7);
        else
          _meshPath = uri;

        /// ★★ 2026-09-23 修复（张老师反馈：GUI「加载模型」加载 yanhu.dae 后，
        ///    2D 地图上看不到该模型，而 Gazebo 3D 里正常）：
        ///
        /// SDF 里的 mesh uri 通常是 `model://yanhu/yanhu.dae` 或相对路径
        /// （如 `robot_model/keship3/keship3.dae`），**Assimp 无法直接解析** ⇒
        /// `LoadMeshFromFile` 失败返回空 ⇒ 足迹尺寸为 0 ⇒ 地图上画出来看不见。
        ///
        /// 这里改用 `gz::common::findFile` 解析成真实文件路径 ——
        /// 与 `KSPlugin_HydroBuoyancy` 的浮力 mesh 加载（:915 附近）保持一致。
        /// ⚠ 这是"地图足迹"与"浮力"两条 mesh 加载路径**行为不一致**导致的缺陷：
        ///   浮力侧一直是对的，所以船能正常浮起来，只有地图足迹悄悄为空。
        const std::string resolved = gz::common::findFile(_meshPath);
        if (!resolved.empty())
          _meshPath = resolved;

        return true;
      }
    }
    return false;
  }

  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// 计算函数：矩形包围盒。返回 {w, h, xMin, yMin, xMax, yMax}
struct BBoxResult { double w, h, xMin, yMin, xMax, yMax; };
static BBoxResult ComputeBBoxFromVertices(
    const std::vector<gz::math::Vector3d> &_verts)
{
  double xMin = INFINITY, xMax = -INFINITY;
  double yMin = INFINITY, yMax = -INFINITY;
  for (const auto &v : _verts)
  {
    if (v.X() < xMin) xMin = v.X();
    if (v.X() > xMax) xMax = v.X();
    if (v.Y() < yMin) yMin = v.Y();
    if (v.Y() > yMax) yMax = v.Y();
  }
  return {xMax - xMin, yMax - yMin, xMin, yMin, xMax, yMax};
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// 计算函数：Graham Scan 2D 凸包
static void ComputeConvexHull(
    const std::vector<gz::math::Vector3d> &_verts,
    std::vector<std::pair<double,double>> &_out)
{
  _out.clear();
  if (_verts.size() < 3) return;

  std::vector<std::pair<double,double>> pts;
  pts.reserve(_verts.size());
  for (const auto &v : _verts)
    pts.emplace_back(v.X(), v.Y());
  std::sort(pts.begin(), pts.end());
  pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
  if (pts.size() < 3) return;

  size_t pivot = 0;
  for (size_t i = 1; i < pts.size(); ++i)
    if (pts[i].second < pts[pivot].second ||
        (pts[i].second == pts[pivot].second && pts[i].first < pts[pivot].first))
      pivot = i;
  std::swap(pts[0], pts[pivot]);

  auto &p0 = pts[0];
  std::sort(pts.begin() + 1, pts.end(),
      [&p0](const auto &a, const auto &b) {
        double cross = (a.first - p0.first) * (b.second - p0.second)
                     - (a.second - p0.second) * (b.first - p0.first);
        if (std::abs(cross) < 1e-12)
          return std::hypot(a.first - p0.first, a.second - p0.second)
               < std::hypot(b.first - p0.first, b.second - p0.second);
        return cross > 0;
      });

  std::vector<std::pair<double,double>> hull;
  for (auto &p : pts)
  {
    while (hull.size() >= 2)
    {
      auto &a = hull[hull.size() - 2], &b = hull.back();
      double cross = (b.first - a.first) * (p.second - a.second)
                   - (b.second - a.second) * (p.first - a.first);
      if (cross > 0) break;
      hull.pop_back();
    }
    hull.push_back(p);
  }
  _out = std::move(hull);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// 计算函数：Alpha Shape 凹包（简化版：凸包 + 去长边）
static void ComputeAlphaShape(
    const std::vector<gz::math::Vector3d> &_verts,
    std::vector<std::pair<double,double>> &_out,
    double _alpha = 2.0)
{
  _out.clear();
  if (_verts.size() < 3) return;

  ComputeConvexHull(_verts, _out);
  if (_out.size() <= 3) return;

  double alphaSq = _alpha * _alpha;
  std::vector<std::pair<double,double>> result;
  for (size_t i = 0; i < _out.size(); ++i)
  {
    size_t j = (i + 1) % _out.size();
    double dx = _out[j].first - _out[i].first;
    double dy = _out[j].second - _out[i].second;
    if (dx * dx + dy * dy <= alphaSq)
      result.push_back(_out[i]);
  }
  if (result.size() >= 3)
    _out = std::move(result);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// 计算函数：分组件凸包（Assimp sub-mesh 分解）
static void ComputeSubMeshHulls(
    const std::string &_meshPath,
    std::vector<std::vector<std::pair<double,double>>> &_out)
{
  _out.clear();
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(_meshPath,
      aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
      aiProcess_PreTransformVertices);
  if (!scene || !scene->HasMeshes()) return;

  for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
  {
    const aiMesh *mesh = scene->mMeshes[m];
    if (!mesh || mesh->mNumVertices < 3) continue;

    std::vector<gz::math::Vector3d> verts;
    verts.reserve(mesh->mNumVertices);
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
      verts.emplace_back(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);

    std::vector<std::pair<double,double>> hull;
    ComputeConvexHull(verts, hull);
    if (hull.size() >= 3)
      _out.push_back(std::move(hull));
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// 计算函数：三角形投影填充 2D 俯视图像（纯 C++，边缘函数法）
static void ComputeProjectedImage(
    const std::string &_meshPath,
    std::vector<unsigned char> &_outData,
    int &_outW, int &_outH,
    int _imgW = 256, int _imgH = 256)
{
  _outData.clear(); _outW = 0; _outH = 0;

  MeshDataSimple mesh = LoadMeshFromFile(_meshPath);
  if (mesh.triangles.empty())
    return;

  double xMin = INFINITY, xMax = -INFINITY;
  double yMin = INFINITY, yMax = -INFINITY;
  for (const auto &v : mesh.vertices)
  {
    if (v.X() < xMin) xMin = v.X();
    if (v.X() > xMax) xMax = v.X();
    if (v.Y() < yMin) yMin = v.Y();
    if (v.Y() > yMax) yMax = v.Y();
  }
  double localW = xMax - xMin;
  double localH = yMax - yMin;
  if (localW < 1e-9 || localH < 1e-9) return;

  _outW = _imgW;
  _outH = _imgH;
  _outData.assign(_imgW * _imgH * 4, 0);

  /// ★★ 2026-09-23 修正（地图「北朝上」，NED：+X 北、+Y 东）：
  ///   投影图**生成时**的轴映射必须与 MapWidget 一致，否则位图本身朝向就错了 ——
  ///   在 paintModelImage 里再交换坐标是**无效的**（图像内容是旋转过的）。
  ///     · 图像 X（横向）← 世界 **Y（东）**
  ///     · 图像 Y（纵向）← 世界 **X（北）**，且 y 取负（图像 y 向下）
  ///   原先写成 图像X←世界X、图像Y←世界Y（旧映射）⇒
  ///   张老师看到"2D 预览与 3D 场景方向不一致"。
  /// ⚠ 注意方向：**图像 X（横向）← 世界 −Y（西）**、图像 Y（纵向）← 世界 −X（下→上为 +X）。
  /// 这与 MapWidget 的映射严格一致（见其 toX/toY 的说明）。
  /// 依据：3D 相机的"屏幕右"是世界 −Y（gz 世界系为 ENU，相机朝 +X 等价于朝东）。
  auto toX = [&](double _wy) { return (_imgW - 1) - (_wy - yMin) / localH * (_imgW - 1); };
  auto toY = [&](double _wx) { return (_imgH - 1) - (_wx - xMin) / localW * (_imgH - 1); };

  auto edgeFn = [](double _px, double _py,
                   double _ax, double _ay, double _bx, double _by) -> double {
    return (_bx - _ax) * (_py - _ay) - (_by - _ay) * (_px - _ax);
  };

  unsigned char *texData = nullptr;
  int texW = 0, texH = 0;
  if (!mesh.texturePath.empty() && !mesh.texCoords.empty())
  {
    std::string texPath = mesh.texturePath;
    if (texPath[0] == '*' || texPath.empty()) { }  // 嵌入纹理，跳过
    else if (texPath[0] != '/' && (texPath.size() < 2 || texPath[1] != ':'))
    {
      std::string dir = _meshPath.substr(0, _meshPath.find_last_of("/\\") + 1);
      texPath = dir + texPath;
    }
    texData = stbi_load(texPath.c_str(), &texW, &texH, nullptr, 4);
  }

  for (size_t t = 0; t < mesh.triangles.size(); ++t)
  {
    const auto &tri = mesh.triangles[t];
    // 注意：toX 吃世界 Y、toY 吃世界 X（见上方说明）
    double ix0 = toX(tri.v0.Y()), iy0 = toY(tri.v0.X());
    double ix1 = toX(tri.v1.Y()), iy1 = toY(tri.v1.X());
    double ix2 = toX(tri.v2.Y()), iy2 = toY(tri.v2.X());

    int minX = std::max(0, (int)std::floor(std::min({ix0, ix1, ix2})));
    int maxX = std::min(_imgW - 1, (int)std::ceil(std::max({ix0, ix1, ix2})));
    int minY = std::max(0, (int)std::floor(std::min({iy0, iy1, iy2})));
    int maxY = std::min(_imgH - 1, (int)std::ceil(std::max({iy0, iy1, iy2})));

    if (texData && t < mesh.triangleIndices.size())
    {
      const auto &idx = mesh.triangleIndices[t];
      double u0 = 0, v0 = 0, u1 = 0, v1 = 0, u2 = 0, v2 = 0;
      if (idx[0] < mesh.texCoords.size()) { u0 = mesh.texCoords[idx[0]].first;  v0 = mesh.texCoords[idx[0]].second; }
      if (idx[1] < mesh.texCoords.size()) { u1 = mesh.texCoords[idx[1]].first;  v1 = mesh.texCoords[idx[1]].second; }
      if (idx[2] < mesh.texCoords.size()) { u2 = mesh.texCoords[idx[2]].first;  v2 = mesh.texCoords[idx[2]].second; }

      for (int y = minY; y <= maxY; ++y)
      {
        for (int x = minX; x <= maxX; ++x)
        {
          double e0 = edgeFn(x + 0.5, y + 0.5, ix0, iy0, ix1, iy1);
          double e1 = edgeFn(x + 0.5, y + 0.5, ix1, iy1, ix2, iy2);
          double e2 = edgeFn(x + 0.5, y + 0.5, ix2, iy2, ix0, iy0);
          bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                        (e0 <= 0 && e1 <= 0 && e2 <= 0);
          if (!inside) continue;

          double area = e0 + e1 + e2;
          double alpha = e1 / area, beta = e2 / area, gamma = e0 / area;
          double u = alpha * u0 + beta * u1 + gamma * u2;
          double v = alpha * v0 + beta * v1 + gamma * v2;
          u = std::max(0.0, std::min(1.0, u));
          v = std::max(0.0, std::min(1.0, v));

          int tx = (int)(u * (texW - 1));
          int ty = texH - 1 - (int)(v * (texH - 1));
          size_t ti = ((size_t)ty * texW + tx) * 4;
          size_t pi = ((size_t)y * _imgW + x) * 4;
          _outData[pi + 0] = texData[ti + 0];
          _outData[pi + 1] = texData[ti + 1];
          _outData[pi + 2] = texData[ti + 2];
          _outData[pi + 3] = 200;
        }
      }
    }
    else
    {
      gz::math::Vector3d n = (tri.v1 - tri.v0).Cross(tri.v2 - tri.v1);
      n.Normalize();
      int gray = 128 + (int)(std::abs(n.Z()) * 127.0);
      gray = std::max(80, std::min(gray, 240));

      for (int y = minY; y <= maxY; ++y)
      {
        for (int x = minX; x <= maxX; ++x)
        {
          double e0 = edgeFn(x + 0.5, y + 0.5, ix0, iy0, ix1, iy1);
          double e1 = edgeFn(x + 0.5, y + 0.5, ix1, iy1, ix2, iy2);
          double e2 = edgeFn(x + 0.5, y + 0.5, ix2, iy2, ix0, iy0);
          bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                        (e0 <= 0 && e1 <= 0 && e2 <= 0);
          if (!inside) continue;

          size_t pi = ((size_t)y * _imgW + x) * 4;
          _outData[pi + 0] = gray;
          _outData[pi + 1] = gray;
          _outData[pi + 2] = gray;
          _outData[pi + 3] = 80;
        }
      }
    }
  }

  if (texData) stbi_image_free(texData);

  /// DEBUG: save preview image to data/preview/
  {
    keship::EnsureKeshipDataDirs();
    std::string fname = keship::KeshipPreviewDir() + "keship_2d_preview_";
    size_t slash = _meshPath.find_last_of("/\\");
    if (slash != std::string::npos)
      fname += _meshPath.substr(slash + 1);
    else
      fname += "unknown";
    fname += ".ppm";
    std::ofstream ofs(fname, std::ios::binary);
    if (ofs) {
      ofs << "P6\n" << _imgW << " " << _imgH << "\n255\n";
      for (int y = _imgH - 1; y >= 0; --y)
        for (int x = 0; x < _imgW; ++x) {
          size_t pi = ((size_t)y * _imgW + x) * 4;
          ofs.put(static_cast<char>(_outData[pi + 0]));
          ofs.put(static_cast<char>(_outData[pi + 1]));
          ofs.put(static_cast<char>(_outData[pi + 2]));
        }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
void ModelManager::CacheModelFootprint(const std::string &_name)
{
  ModelFootprint fp;
  fp.name = _name;

  ModelGeometryType type;
  double dx, dy, dz, radius, length;
  std::string meshPath;
  if (!GetCachedModelGeometry(_name, type, dx, dy, dz, radius, length, meshPath))
  {
    /// ★ 2026-09-23 诊断：模型未进地图缓存的原因之一（sdfCache_ 里没有该模型，
    ///   或 SDF 里既无 <mesh> 也无 <box>/<cylinder>）
    std::cout << "[足迹诊断] " << _name
              << " —— GetCachedModelGeometry 失败（不进地图缓存）" << std::endl;
    return;
  }

  switch (type)
  {
  case ModelGeometryType::Box:
    fp.localW = dx;
    fp.localH = dy;
    break;
  case ModelGeometryType::Cylinder:
    fp.localW = radius * 2.0;
    fp.localH = radius * 2.0;
    break;
  case ModelGeometryType::Mesh:
    if (!meshPath.empty())
    {
      /// ★ 2026-09-25 性能修复（⑤）：这里只需要**顶点位置**（包围盒 + 2D 凸包），
      /// 原实现对规范化 OBJ 再做一次 **Assimp 全量解析**（wamv 的 179MB OBJ
      /// 实测 3790ms）。改为流式只读 `v` 行 —— 规范化 OBJ 是"无变换"产物，
      /// 顶点行即最终坐标；包围盒取 min/max、凸包内部 sort+unique ⇒ 逐位等价。
      ///
      /// ⚠ 仅当 meshPath 确实是 `.obj` 才走流式：规范化失败时上层会兜底成
      ///   源文件路径（dae/stl），那种情况必须退回 Assimp，否则读 XML 当 OBJ 解析。
      std::vector<gz::math::Vector3d> verts;
      bool gotVerts = false;
      {
        std::string low = boost::to_lower_copy(meshPath);
        const bool isObj =
            low.size() >= 4 && low.compare(low.size() - 4, 4, ".obj") == 0;
        if (isObj)
          gotVerts = keship::LoadNormalizedObjVertices(meshPath, verts);
      }
      if (!gotVerts)
      {
        MeshDataSimple mesh = LoadMeshFromFile(meshPath);
        verts = mesh.vertices;
      }
      if (!verts.empty())
      {
        auto bb = ComputeBBoxFromVertices(verts);
        fp.localW = bb.w;
        fp.localH = bb.h;
        fp.localXMin = bb.xMin;
        fp.localYMin = bb.yMin;

        ComputeConvexHull(verts, fp.convexOutline);
      }
    }
    break;
  }

  bool isStatic;
  if (GetCachedModelStatic(_name, isStatic))
    fp.isStatic = isStatic;

  /// ★ 2026-09-23 诊断：确认该模型是否真的进了地图缓存、尺寸与轮廓是否有效
  std::cout << "[足迹诊断] " << _name
            << " type=" << static_cast<int>(type)
            << " meshPath=" << meshPath
            << " W=" << fp.localW << " H=" << fp.localH
            << " outline=" << fp.convexOutline.size()
            << std::endl;

  modelBBoxCache_[_name] = fp;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
void ModelManager::EnsureFootprintOutlines(ModelRenderScheme _scheme)
{
  for (auto &[name, fp] : modelBBoxCache_)
  {
    if (_scheme == ModelRenderScheme::AlphaShape && fp.alphaOutline.empty())
    {
      std::string meshPath;
      ModelGeometryType type; double dx, dy, dz, radius, length;
      if (GetCachedModelGeometry(name, type, dx, dy, dz, radius, length, meshPath) &&
          !meshPath.empty())
      {
        MeshDataSimple mesh = LoadMeshFromFile(meshPath);
        if (!mesh.vertices.empty())
          ComputeAlphaShape(mesh.vertices, fp.alphaOutline);
      }
    }
    else if (_scheme == ModelRenderScheme::SubMeshHulls && fp.subOutlines.empty())
    {
      std::string meshPath;
      ModelGeometryType type; double dx, dy, dz, radius, length;
      if (GetCachedModelGeometry(name, type, dx, dy, dz, radius, length, meshPath) &&
          !meshPath.empty())
      {
        ComputeSubMeshHulls(meshPath, fp.subOutlines);
      }
    }
    else if (_scheme == ModelRenderScheme::ProjectedImage && fp.previewData.empty())
    {
      std::string meshPath;
      ModelGeometryType type; double dx, dy, dz, radius, length;
      if (GetCachedModelGeometry(name, type, dx, dy, dz, radius, length, meshPath) &&
          !meshPath.empty())
      {
        ComputeProjectedImage(meshPath, fp.previewData, fp.previewW, fp.previewH);
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::ParseStaticFromSDF(const std::string &_sdf, bool &_out)
{
  auto start = _sdf.find("<static>");
  if (start == std::string::npos) { _out = false; return false; }
  start += 8;
  auto end = _sdf.find("</static>", start);
  if (end == std::string::npos) { _out = false; return false; }
  std::string val = _sdf.substr(start, end - start);
  _out = (val == "true" || val == "1");
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/// 切换模型 静止/动态（2026-10-25）
/// 做法：改缓存 SDF 的 <static> 后整段重放 —— 保留全部插件（含数学船型的网格浮力体），
/// 避免"按配置重拼"丢掉 SDF-only 插件。
bool ModelManager::SetModelStatic(const std::string &_name, bool _isStatic)
{
  auto it = sdfCache_.find(_name);
  if (it == sdfCache_.end())
  {
    std::cerr << "SetModelStatic: 无 SDF 缓存（请先在本进程 spawn）: " << _name << std::endl;
    return false;
  }

  double pose[6] = {0, 0, 0, 0, 0, 0};
  {
    auto idIt = modelList.find(_name);
    if (idIt != modelList.end())
      GetModelPoseByIDFromGazebo(idIt->second, pose);
  }

  std::string sdf = it->second;
  const std::string want = _isStatic ? "true" : "false";
  const std::string t = "<static>true</static>";
  const std::string f = "<static>false</static>";
  auto tp = sdf.find(t);
  auto fp = sdf.find(f);
  if (tp != std::string::npos)
    sdf.replace(tp, t.size(), "<static>" + want + "</static>");
  else if (fp != std::string::npos)
    sdf.replace(fp, f.size(), "<static>" + want + "</static>");
  else
  {
    auto mp = sdf.find("</model>");
    if (mp != std::string::npos)
      sdf.insert(mp, "    <static>" + want + "</static>\n");
  }
  it->second = sdf;

  if (!DeleteModelInGazeboByName(_name, true)) return false;
  { int retries = 20; uint32_t sid = 0;
    while (retries-- > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!GetModelIDByInstanceNameFromGazebo(_name, sid)) break; } }

  SensorConfig sc;
  GetCachedSensorConfig(_name, sc);
  const bool ok = RespawnSdf(_name, sdf, pose, sc);
  if (ok)
  {
    staticCache_[_name] = _isStatic;
    CacheModelFootprint(_name);
  }
  return ok;
}

bool ModelManager::GetCachedModelStatic(const std::string &_name,
    bool &_isStatic) const
{
  auto it = staticCache_.find(_name);
  if (it != staticCache_.end())
  {
    _isStatic = it->second;
    return true;
  }

  auto sdfIt = sdfCache_.find(_name);
  if (sdfIt == sdfCache_.end())
    return false;

  ParseStaticFromSDF(sdfIt->second, _isStatic);
  staticCache_[_name] = _isStatic;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::SpawnModelWithBuoyancy(
    const std::string &_instanceName,
    const BuoyancyConfig &_config,
    const double _pos[6])
{
  auto it = this->sdfCache_.find(_instanceName);
  if (it == this->sdfCache_.end())
  {
    std::cerr << "SpawnModelWithBuoyancy: 无缓存 SDF" << std::endl;
    return false;
  }

  /// 基础 SDF 含 </model></sdf> 及控制器插件，从 </link> 处截断（去掉 </link>，后续插入传感器后重新闭合）
  std::string base = it->second;

  /// M_A 通过 DART <fluid_added_mass> 注入（隐式积分）

  bool hasExternalForce = base.find("<external_force>") != std::string::npos
                     || base.find("<variable_load>") != std::string::npos;
  auto lpos = base.rfind("</link>");
  if (lpos != std::string::npos) base = base.substr(0, lpos);

  HydroConfig hydroCfg;
  bool hasHydro = GetCachedHydroConfig(_instanceName, hydroCfg);

  SensorConfig sensorCfg;
  bool hasSensor = GetCachedSensorConfig(_instanceName, sensorCfg);

  std::ostringstream sdf;
  sdf << base;

  if (hasSensor)
    sdf << MakeSensorSDF(sensorCfg, _instanceName, worldName);
  sdf << "    </link>\n";

  /// 保留已有推进器配置
  ThrusterConfig thrustCfg;
  if (GetCachedThrusterConfig(_instanceName, thrustCfg) && !thrustCfg.bodies.empty())
    sdf << MakeThrustPluginSDF(thrustCfg);

  /// 合并的浮力-水动力插件
  {
    if (hasHydro)
    {
      sdf << MakeBuoyancyPluginSDF(_config, &hydroCfg);
    }
    else
    {
      sdf << MakeBuoyancyPluginSDF(_config);
    }
  }

  sdf << "  </model>\n"
      << "</sdf>\n";

  
  /// M_A 注入（统一入口 —— 2026-09-24 起改用 ApplyAddedMassToSdf，行为等价）
  ApplyAddedMassToSdf(sdf, hasHydro, hydroCfg);

  std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 20000;

  req.set_name(_instanceName);
  req.set_allow_renaming(false);
  req.set_sdf(sdf.str());

  if (_pos)
  {
    auto *ps = req.mutable_pose();
    ps->mutable_position()->set_x(_pos[0]);
    ps->mutable_position()->set_y(_pos[1]);
    ps->mutable_position()->set_z(_pos[2]);
    gz::math::Quaterniond q(_pos[3], _pos[4], _pos[5]);
    ps->mutable_orientation()->set_x(q.X());
    ps->mutable_orientation()->set_y(q.Y());
    ps->mutable_orientation()->set_z(q.Z());
    ps->mutable_orientation()->set_w(q.W());
    std::cout << "SpawnModelWithBuoyancy: 请求 spawn 位置 = ["
              << _pos[0] << ", " << _pos[1] << ", " << _pos[2] << "]" << std::endl;
  }

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  uint32_t id = 0;
  bool found = false;
  uint32_t tries = 100;
  /// ★ 2026-09-25 性能修复（⑩）：**先查再睡**。
  /// 原实现先睡一个周期再查 —— 实体若已就绪（常态）就白等 200ms，而每次
  /// spawn/rebuild 都付这个代价。
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  }

  if (found)
  {
    this->modelList[_instanceName] = id;
    this->buoyancyConfigCache_[_instanceName] = _config;
    NotifyModelSpawned(_instanceName, sensorCfg);
    std::cout << "SpawnModelWithBuoyancy: 完成, id=" << id << std::endl;
    return true;
  }

  std::cerr << "SpawnModelWithBuoyancy: 未找到模型ID" << std::endl;
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
std::string ModelManager::MakeThrustPluginSDF(const ThrusterConfig &_config)
{
  if (_config.bodies.empty())
    return "";

  std::ostringstream sdf;
  sdf << "    <plugin filename=\"libKSPlugin_Thrust.so\""
      << " name=\"keship::KSPlugin_Thrust\">\n"
      << "      <link_name>" << _config.linkName << "</link_name>\n";

  for (const auto &body : _config.bodies)
  {
    sdf << "      <thrust name=\"" << body.name << "\">\n"
        << "        <pose>"
        << body.pose[0] << " " << body.pose[1] << " " << body.pose[2] << " "
        << body.pose[3] << " " << body.pose[4] << " " << body.pose[5]
        << "</pose>\n"
        << "        <max_force>" << body.maxForce << "</max_force>\n"
        << "      </thrust>\n";
  }

  sdf << "    </plugin>\n";
  return sdf.str();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::RebuildModelWithThrust(
    const std::string &_instanceName,
    const ThrusterConfig &_config)
{
  /// 1. 保存当前 pose
  double savedPose[6] = {0, 0, 0, 0, 0, 0};
  {
    auto idIt = this->modelList.find(_instanceName);
    if (idIt != this->modelList.end())
    {
      if (GetModelPoseByIDFromGazebo(idIt->second, savedPose))
      {
        std::cout << "RebuildModelWithThrust: 保存pose "
                  << savedPose[0] << " " << savedPose[1] << " " << savedPose[2] << " "
                  << savedPose[3] << " " << savedPose[4] << " " << savedPose[5] << std::endl;
      }
    }
  }

  /// ★ 2026-09-28 修（**成套**：5 个 Rebuild* 同族缺陷之一）：**先查缓存，再做破坏性删除**。
  ///   原实现是「先删模型 → 后查缓存」⇒ 缓存缺失时模型**已被删掉**，随后才 return false
  ///   ⇒ **场景凭空少一个模型**（数据丢失级；同族反模式「没做到却报成功」的破坏性版本）。
  ///   本函数会重建实体 ⇒ 一切「可能失败」的准备工作必须在删除**之前**做完。
  if (sdfCache_.find(_instanceName) == sdfCache_.end())
  {
    std::cerr << "RebuildModelWithThrust: 本进程没有该模型的 SDF 缓存，**拒绝重建以免丢模型**"
                 "（请先在本进程 load-ksm / spawn）: "
              << _instanceName << std::endl;
    return false;   ///< **不删模型**
  }

  /// 2. 删除旧模型
  if (!DeleteModelInGazeboByName(_instanceName, true))
  {
    std::cerr << "RebuildModelWithThrust: 删除模型失败" << std::endl;
    return false;
  }

  /// 等待清理
  {
    int retries = 20;
    uint32_t staleId = 0;
    while (retries-- > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!GetModelIDByInstanceNameFromGazebo(_instanceName, staleId))
        break;
    }
  }

  /// 3. 从缓存取基础 SDF，从 </link> 截断
  ///（缓存在删除前已校验存在 ⇒ 必然命中；原「删除后才查缓存」的写法已前置）
  auto cacheIt = this->sdfCache_.find(_instanceName);

  std::string base = cacheIt->second;
  /// ★ 2026-10-25：自包含插件模型 → 在原文里插入推进器段后重放（不丢浮力）
  if (base.find("libKSPlugin_HydroBuoyancy.so") != std::string::npos)
  {
    std::string patched = base;
    auto m = patched.rfind("</model>");
    if (m != std::string::npos)
      patched = patched.substr(0, m) + MakeThrustPluginSDF(_config) + patched.substr(m);
    SensorConfig sc; GetCachedSensorConfig(_instanceName, sc);
    bool ok = RespawnSdf(_instanceName, patched, savedPose, sc);
    if (ok) thrusterConfigCache_[_instanceName] = _config;
    return ok;
  }


  /// [removed] M_A 现在完全在插件中显式计算，不再注入 DART

  auto lpos = base.rfind("</link>");
  if (lpos != std::string::npos) base = base.substr(0, lpos);

  /// 4. 组装完整 SDF：base + 传感器 + </link> + 浮力体插件(如有) + 推进器插件 + 控制器插件
  HydroConfig hydroCfg;
  bool hasHydro = GetCachedHydroConfig(_instanceName, hydroCfg);

  SensorConfig sensorCfg;
  bool hasSensor = GetCachedSensorConfig(_instanceName, sensorCfg);

  std::ostringstream sdf;
  sdf << base;

  if (hasSensor)
    sdf << MakeSensorSDF(sensorCfg, _instanceName, worldName);
  sdf << "    </link>\n";

  sdf << MakeThrustPluginSDF(_config);

  BuoyancyConfig buoyCfg;
  bool haveBuoy = GetCachedBuoyancyConfig(_instanceName, buoyCfg) && !buoyCfg.bodies.empty();

  if (hasHydro)
  {
    sdf << MakeBuoyancyPluginSDF(buoyCfg, &hydroCfg);
  }
  else if (haveBuoy)
  {
    sdf << MakeBuoyancyPluginSDF(buoyCfg);
  }

  sdf << "  </model>\n"
      << "</sdf>\n";

  /// ★ 2026-09-24 修 P0-1：本路径原先漏了 M_A 注入
  ApplyAddedMassToSdf(sdf, hasHydro, hydroCfg);

  /// 5. spawn 新模型
  std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 20000;

  req.set_name(_instanceName);
  req.set_allow_renaming(false);
  req.set_sdf(sdf.str());

  auto reqPose = req.mutable_pose();
  reqPose->mutable_position()->set_x(savedPose[0]);
  reqPose->mutable_position()->set_y(savedPose[1]);
  reqPose->mutable_position()->set_z(savedPose[2]);
  gz::math::Quaterniond q(savedPose[3], savedPose[4], savedPose[5]);
  reqPose->mutable_orientation()->set_x(q.X());
  reqPose->mutable_orientation()->set_y(q.Y());
  reqPose->mutable_orientation()->set_z(q.Z());
  reqPose->mutable_orientation()->set_w(q.W());

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  /// 6. 等待模型 ID
  uint32_t id = 0;
  bool found = false;
  uint32_t tries = 100;
  /// ★ 2026-09-25 性能修复（⑩）：**先查再睡**。
  /// 原实现先睡一个周期再查 —— 实体若已就绪（常态）就白等 200ms，而每次
  /// spawn/rebuild 都付这个代价。
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  }

  if (found)
  {
    this->modelList[_instanceName] = id;
    this->thrusterConfigCache_[_instanceName] = _config;
    NotifyModelSpawned(_instanceName, sensorCfg);
    std::cout << "RebuildModelWithThrust: 完成, id=" << id << std::endl;
    return true;
  }

  std::cerr << "RebuildModelWithThrust: 未找到模型ID" << std::endl;
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::SpawnModelWithThrust(
    const std::string &_instanceName,
    const ThrusterConfig &_config,
    const double _pos[6])
{
  auto it = this->sdfCache_.find(_instanceName);
  if (it == this->sdfCache_.end())
  {
    std::cerr << "SpawnModelWithThrust: 无缓存 SDF" << std::endl;
    return false;
  }

  std::string base = it->second;

  /// [removed] M_A 现在完全在插件中显式计算，不再注入 DART

  auto lpos = base.rfind("</link>");
  if (lpos != std::string::npos) base = base.substr(0, lpos);

  SensorConfig sensorCfg;
  bool hasSensor = GetCachedSensorConfig(_instanceName, sensorCfg);

  std::ostringstream sdf;
  sdf << base;

  if (hasSensor)
    sdf << MakeSensorSDF(sensorCfg, _instanceName, worldName);
  sdf << "    </link>\n";

  HydroConfig hydroCfg;
  bool hasHydro = GetCachedHydroConfig(_instanceName, hydroCfg);

  sdf << MakeThrustPluginSDF(_config);

  BuoyancyConfig buoyCfg;
  bool haveBuoy = GetCachedBuoyancyConfig(_instanceName, buoyCfg) && !buoyCfg.bodies.empty();

  if (hasHydro)
  {
    sdf << MakeBuoyancyPluginSDF(buoyCfg, &hydroCfg);
  }
  else if (haveBuoy)
  {
    sdf << MakeBuoyancyPluginSDF(buoyCfg);
  }

  sdf << "  </model>\n"
      << "</sdf>\n";

  /// ★ 2026-09-24 修 P0-1：**本路径是影响面最大的一处** ——
  /// 4 个 ksm 中 3 个含 THRUST 段、都走这里，原先全部丢失 M_A。
  ApplyAddedMassToSdf(sdf, hasHydro, hydroCfg);

  std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  gz::msgs::Boolean rep;
  bool result;
  unsigned int timeout = 20000;

  req.set_name(_instanceName);
  req.set_allow_renaming(false);
  req.set_sdf(sdf.str());

  if (_pos)
  {
    auto *ps = req.mutable_pose();
    ps->mutable_position()->set_x(_pos[0]);
    ps->mutable_position()->set_y(_pos[1]);
    ps->mutable_position()->set_z(_pos[2]);
    gz::math::Quaterniond q(_pos[3], _pos[4], _pos[5]);
    ps->mutable_orientation()->set_x(q.X());
    ps->mutable_orientation()->set_y(q.Y());
    ps->mutable_orientation()->set_z(q.Z());
    ps->mutable_orientation()->set_w(q.W());
  }

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  uint32_t id = 0;
  bool found = false;
  uint32_t tries = 100;
  /// ★ 2026-09-25 性能修复（⑩）：**先查再睡**。
  /// 原实现先睡一个周期再查 —— 实体若已就绪（常态）就白等 200ms，而每次
  /// spawn/rebuild 都付这个代价。
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  }

  if (found)
  {
    this->modelList[_instanceName] = id;
    this->thrusterConfigCache_[_instanceName] = _config;
    NotifyModelSpawned(_instanceName, sensorCfg);
    std::cout << "SpawnModelWithThrust: 完成, id=" << id << std::endl;
    return true;
  }

  std::cerr << "SpawnModelWithThrust: 未找到模型ID" << std::endl;
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GetCachedThrusterConfig(
    const std::string &_name, ThrusterConfig &_out) const
{
  auto it = thrusterConfigCache_.find(_name);
  if (it == thrusterConfigCache_.end())
    return false;
  _out = it->second;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief 通过 `create/blocking` 生成实体 —— **11 个 spawn/rebuild 的统一入口**
///
/// ★ 2026-09-24 收敛（张老师："不是应该只调用已有函数吗？"）：
///   原先每个 spawn/rebuild 都自己写一遍「拼服务名 → Request → 查结果」，
///   而且查法不一（10 处只查 `executed`、1 处连 `result` 都查）⇒ 同名冲突时"假成功"。
///   我一开始逐处补 `|| !result`，补了 8 处 —— **机械核对发现漏了 2 处**
///   （`SpawnModelInGazeboFromFile` / `SpawnCylinderInGazebo`）。
///   **这就是"平行路径 + 逐处打补丁"必然漏补的实证。**
///
/// 现在只此一处实现；新增路径调它即可，口径天然一致。
/// ★★ 2026-09-28 碰撞体积"计算方式"的**施加点**（张老师要求三选一：
///   结构网格 / 凸包 / 无）。
///
/// 设计要点：
///   · **Mesh ⇒ 原样返回**（零成本、零风险；也是默认，保持既有行为）；
///   · **None ⇒ 删掉 `<collision>` 段**；
///   · **ConvexHull ⇒ 用 qhull 生成凸包 OBJ**（缓存到源目录 normalized/），
///     并把 `<collision>` 指向它；**失败则回退原样**（安全优先，`_outOk=false`）。
///   · 源网格 uri 从 collision 段内取（取不到就用全 SDF 的第一个 `<uri>`）。
std::string ModelManager::ApplyCollisionModeToSdf(const std::string &_sdf,
                                                  CollisionMode _mode,
                                                  bool *_outOk) const
{
  if (_outOk) *_outOk = true;

  /// Mesh ⇒ 不动（默认路径，零开销）
  if (_mode == CollisionMode::Mesh)
    return _sdf;

  /// 定位第一个 <collision ...> ... </collision>
  static const std::string kOpen = "<collision";
  static const std::string kClose = "</collision>";
  const size_t c0 = _sdf.find(kOpen);
  if (c0 == std::string::npos)
    return _sdf;                       ///< 本来就没有 collision ⇒ 无需处理
  const size_t c1 = _sdf.find(kClose, c0);
  if (c1 == std::string::npos)
    return _sdf;
  const size_t cEnd = c1 + kClose.size();

  /// None ⇒ 删除该段
  if (_mode == CollisionMode::None)
    return _sdf.substr(0, c0) + _sdf.substr(cEnd);

  /// ConvexHull ⇒ 先取源网格 uri
  auto pickUri = [&](size_t _from, size_t _to) -> std::string {
    const size_t u0 = _sdf.find("<uri>", _from);
    if (u0 == std::string::npos || u0 >= _to) return "";
    const size_t u1 = _sdf.find("</uri>", u0);
    if (u1 == std::string::npos) return "";
    return _sdf.substr(u0 + 5, u1 - u0 - 5);
  };
  std::string uri = pickUri(c0, cEnd);
  if (uri.empty())
    uri = pickUri(0, _sdf.size());
  if (uri.empty())
  {
    std::cerr << "ApplyCollisionModeToSdf: 找不到网格 uri，保持原 collision" << std::endl;
    if (_outOk) *_outOk = false;
    return _sdf;
  }

  /// uri → 真实路径（支持 `file://` 与 `model://`/相对形式）
  std::string path = uri;
  if (path.rfind("file://", 0) == 0)
  {
    path = path.substr(7);
  }
  else
  {
    const std::string resolved = gz::common::findFile(path);
    if (!resolved.empty())
      path = resolved;
  }

  const std::string hullPath = keship::GenerateConvexHullCache(path);
  if (hullPath.empty())
  {
    std::cerr << "ApplyCollisionModeToSdf: 凸包生成失败，**回退原 collision**: "
              << path << std::endl;
    if (_outOk) *_outOk = false;
    return _sdf;                       ///< 失败 ⇒ 回退（绝不把模型弄成没碰撞）
  }

  const std::string repl =
      "<collision name=\"collision\"><geometry><mesh><uri>file://" + hullPath +
      "</uri></mesh></geometry></collision>";
  std::cout << "ApplyCollisionModeToSdf: collision → 凸包 " << hullPath << std::endl;
  return _sdf.substr(0, c0) + repl + _sdf.substr(cEnd);
}

bool ModelManager::CreateEntityViaService(const gz::msgs::EntityFactory &_req)
{
  /// ★★ 2026-09-28：**统一在此施加"碰撞体积计算方式"**（张老师要求三选一）。
  /// 本函数是 **11 条 spawn/rebuild 路径的必经之路**，且能从 `_req.name()` 取实例名
  /// ⇒ **一处生效，不必逐处补**（09-24 的教训：横向关注点逐处补必漏）。
  /// ⚠ Mesh 模式直接原样返回 ⇒ 默认路径零开销、零行为变化。
  gz::msgs::EntityFactory req = _req;
  {
    const CollisionMode cm = GetCollisionMode(req.name());
    if (cm != CollisionMode::Mesh && !req.sdf().empty())
      req.set_sdf(ApplyCollisionModeToSdf(req.sdf(), cm));
  }

  const std::string service = "/world/" + this->worldName + "/create/blocking";
  gz::msgs::Boolean rep;
  bool result = false;
  const bool executed = this->node->Request(service, req, 20000, rep, result);
  if (!executed || !result)
  {
    std::cerr << "CreateEntityViaService: spawn 失败 name=" << req.name()
              << "（executed=" << executed << ", result=" << result << "）" << std::endl;
    return false;
  }
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::InjectFluidAddedMass(std::string &_sdf,
    const double _addedMass[36])
{
  /// 2026-10-25：全零矩阵直接跳过 —— 避免生成无意义的全零 <fluid_added_mass> 块
  {
    bool anyNonZero = false;
    for (int i = 0; i < 36; ++i)
      if (std::abs(_addedMass[i]) > 1e-12) { anyNonZero = true; break; }
    if (!anyNonZero) return true;
  }
  // M_A 注入 DART <fluid_added_mass>（隐式积分，无条件稳定）
  //
  // ⚠ 2026-09-24 审核更正（原文写「空中由 FossenModel::computeAirCorrection 抵消」
  //    并引用 program/notes/ma_dart_removal_fix.md）：
  //    · `computeAirCorrection` **在代码库中不存在**（全库仅此注释提及）；
  //    · 被引用的设计文档 `ma_dart_removal_fix.md` 亦**不存在**；
  //    ⇒ **模型出水（空中）时 M_A 未做任何抵消** —— DART 的 `<fluid_added_mass>`
  //      是 link 惯性的组成部分、不区分介质，故跃出水面后仍带附加惯性。
  //      **待验证 / 待修**，见 doc/audit/model_loading_audit_2026-09-24.md 的 P1-4。

  auto vpos = _sdf.find("sdf version=\"1.6\"");
  if (vpos != std::string::npos)
    _sdf.replace(vpos, 18, "sdf version=\"1.10\"");

  // 在 </inertial> 前插入 <fluid_added_mass>
  auto ipos = _sdf.find("</inertial>");
  if (ipos == std::string::npos)
    return false;

  // 21 个上三角标签映射（行主序 6×6 → SDF 标签）
  static const char* tags[6][6] = {
    {"xx","xy","xz","xp","xq","xr"},
    {nullptr,"yy","yz","yp","yq","yr"},
    {nullptr,nullptr,"zz","zp","zq","zr"},
    {nullptr,nullptr,nullptr,"pp","pq","pr"},
    {nullptr,nullptr,nullptr,nullptr,"qq","qr"},
    {nullptr,nullptr,nullptr,nullptr,nullptr,"rr"}
  };

  std::ostringstream fam;
  fam << "      <fluid_added_mass>\n";
  for (int i = 0; i < 6; ++i)
  {
    for (int j = i; j < 6; ++j)
    {
      double val = _addedMass[i * 6 + j];
      if (std::abs(val) < 1e-12)
        continue;

      /// ★★ 本体坐标系符号修正（2026-09-23）
      ///
      /// 用户的 M_A 按 **Fossen/NED 本体**导出（x 前、**y 右**、**z 下**），
      /// 而 DART 的 `<fluid_added_mass>` 工作在 **gz link 系**（x 前、**y 左**、**z 上**）
      /// ⇒ y/z 轴族（sway/heave/roll/pitch/yaw，索引 1..5）的"力分量"与
      ///   "速度分量"各自反号。
      ///
      /// 对矩阵项 m_ij，净修正系数 = (-1)^([i∈S] + [j∈S])，S = {1,2,3,4,5}：
      ///   · i、j 都在 S 内，或都不在（即 i=j=0）      ⇒ +1（不变）
      ///   · 恰有一个在 S 内（即 **x 与 y/z 轴的耦合项**）⇒ -1（取反）
      ///
      /// 实测（`models_all/ks_model/ke0812_1.ksm`）：非零项为对角 6 项
      /// + `m15 = m51 = -405`（sway↔yaw），**全部落在"不变"一侧** ⇒
      /// 加此修正后该模型的 SDF 输出**逐字不变**（可作回归判据）。
      /// 唯一受影响的是 `m01..m05` 类耦合项（该模型全为 0）——
      /// 这正是本次要堵住的隐患：将来若出现非零的 x↔y/z 耦合，符号自动正确。
      const bool iInS = (i >= 1);
      const bool jInS = (j >= 1);
      if (iInS != jInS)
        val = -val;

      fam << "        <" << tags[i][j] << ">"
          << val << "</" << tags[i][j] << ">\n";
    }
  }
  fam << "      </fluid_added_mass>\n";

  _sdf.insert(ipos, fam.str());
  return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/// ★ 2026-09-25 性能修复（①）：**Camera 传感器规格集中定义在此**（唯一生成点）。
///
/// 背景（取证见 doc/audit/model_loading_performance_2026-09-25.md）：
///   每个模型一路 Camera 会按 `update_rate` **逐帧渲染**（headless 下走软件渲染），
///   实测**单路约占 0.72~0.75 倍实时预算**：
///     · 无头 4 路 → 实时率 0.258（把 Camera 行删掉 → 1.146）
///     · GUI  2 路 → real time factor 0.40（同场景 nocam → 0.99）
///   成本 ∝ 像素数 × 帧率，原规格 640×480 @ 30 Hz 是"多模型就卡"的**唯一主因**。
///
/// 现值 320×240 @ 10 Hz ⇒ 成本约为原来的 **1/12**（4 路 ≈ 0.25 倍预算，实时率回到 ~1.0）。
///
/// ⚠ **扩展点**（张老师 2026-09-25 拍板"以后可加用户自选分辨率"）：
///   将来把下面三个常量换成 `SensorConfig` 的字段（并在传感器编辑对话框暴露）即可。
///   **本函数必须保持为 Camera SDF 的唯一生成点** —— 不要在别处另开一份拼装，
///   那正是 09-24 审核里"多条平行 spawn 路径各自实现 ⇒ 漏补成常态"的老毛病。
/// ⚠ 2026-09-26 起：默认档与合法区间已上移到 ModelManager.hh
///   （`kCameraDefault*` / `kCameraMin*` / `kCameraCostRef*`），
///   因为「用户自选」需要在**每个实例**上覆盖规格，默认值成为公共契约。
///   本函数仍是 Camera SDF 的**唯一生成点**。
///
/// ⚠ 必须包在 `namespace keship` 里：本文件顶部只有 `using namespace keship;`，
///   文件其余部分是**全局作用域**，而声明在 keship 内 ⇒ 不包会链接期 undefined。
namespace keship
{
CameraSpec ClampCameraSpec(int _w, int _h, double _rate, bool *_outClamped)
{
  CameraSpec s;
  /// 0 / 负值 ⇒ "未指定"，用默认档
  s.width  = (_w > 0)     ? _w    : kCameraDefaultWidth;
  s.height = (_h > 0)     ? _h    : kCameraDefaultHeight;
  s.rate   = (_rate > 0.0) ? _rate : kCameraDefaultRate;

  const CameraSpec requested = s;   ///< 夹紧前的用户诉求（判定"是否被改过"）

  /// ★ 只做**物理合法性**夹紧：防手滑输入 0 / 负数 / 荒谬值把 gz 打死。
  ///   **不吸附档位、不因"成本"降级** —— 按用户设定原样执行。
  ///
  /// ⚠ 2026-09-26 张老师纠正（**已采纳**）：这是**仿真**软件，Camera 是**纯传感器**，
  ///   `update_rate` / 分辨率**不参与物理计算** ⇒ RTF 低只是**慢**、**不影响正确性**，
  ///   且可靠更强的机器改善。**拿用户的设定换 RTF 是错的**（还悄悄改了设定）。
  ///   ⇒ 代价只在 GUI 里**提示**（"预计成本 ×N"），不在这里干预。
  ///
  /// ⚠ 仍**建议**（不强制）`update_rate` 取 250 Hz 步长的整除值（5/10/25/50 Hz）：
  ///   非整除值实测帧间隔抖动更大（30 Hz → std 28.1 ms vs 10 Hz → 7.6 ms）。
  s.width  = std::clamp(s.width,  kCameraMinWidth,  kCameraMaxWidth);
  s.height = std::clamp(s.height, kCameraMinHeight, kCameraMaxHeight);
  s.rate   = std::clamp(s.rate,   kCameraMinRate,   kCameraMaxRate);

  if (_outClamped)
    *_outClamped = (s.width != requested.width || s.height != requested.height ||
                    s.rate  != requested.rate);
  return s;
}
}  // namespace keship

std::string ModelManager::MakeSensorSDF(const SensorConfig &_config,
                                         const std::string &_modelName,
                                         const std::string &_worldName)
{
  std::ostringstream sdf;

  for (const auto &inst : _config.instances)
  {
    std::string sensorPath = keship::SanitizeRosTopic(
        "/world/" + _worldName + "/model/" + _modelName
        + "/link/" + _config.linkName + "/sensor/" + inst.name);

    switch (inst.type)
    {
    case SensorType::IMU:
      sdf << "      <sensor name=\"" << inst.name << "\" type=\"imu\">\n"
          << "        <topic>" << sensorPath + "/imu" << "</topic>\n"
          << "        <update_rate>100</update_rate>\n"
          << "        <imu>\n"
          << "          <angular_velocity><x><noise type=\"gaussian\"><mean>0</mean><stddev>0.01</stddev></noise></x></angular_velocity>\n"
          << "          <linear_acceleration><x><noise type=\"gaussian\"><mean>0</mean><stddev>0.1</stddev></noise></x></linear_acceleration>\n"
          << "        </imu>\n"
          << "        <pose>" << inst.pose[0] << " " << inst.pose[1] << " " << inst.pose[2]
          << " " << inst.pose[3] << " " << inst.pose[4] << " " << inst.pose[5] << "</pose>\n"
          << "      </sensor>\n";
      break;

    case SensorType::NavSat:
      sdf << "      <sensor name=\"" << inst.name << "\" type=\"navsat\">\n"
          << "        <always_on>1</always_on>\n"
          << "        <update_rate>10</update_rate>\n"
          << "        <topic>" << sensorPath + "/navsat" << "</topic>\n"
          << "        <pose>" << inst.pose[0] << " " << inst.pose[1] << " " << inst.pose[2]
          << " " << inst.pose[3] << " " << inst.pose[4] << " " << inst.pose[5] << "</pose>\n"
          << "      </sensor>\n";
      break;

    case SensorType::Magnetometer:
      sdf << "      <sensor name=\"" << inst.name << "\" type=\"magnetometer\">\n"
          << "        <topic>" << sensorPath + "/magnetometer" << "</topic>\n"
          << "        <update_rate>10</update_rate>\n"
          << "        <magnetometer><x><noise type=\"gaussian\"><mean>0</mean><stddev>0.0001</stddev></noise></x></magnetometer>\n"
          << "        <pose>" << inst.pose[0] << " " << inst.pose[1] << " " << inst.pose[2]
          << " " << inst.pose[3] << " " << inst.pose[4] << " " << inst.pose[5] << "</pose>\n"
          << "      </sensor>\n";
      break;

    case SensorType::Camera:
    {
      /// ★ 2026-09-26「用户自选」：规格来自**实例**（0 = 用默认档 320×240@10），
      /// 并统一经 ClampCameraSpec() 夹紧（含"不得比原规格 640×480@30 更重"的上限）。
      const CameraSpec camSpec = ClampCameraSpec(inst.camWidth, inst.camHeight,
                                                 inst.camRate);
      sdf << "      <sensor name=\"" << inst.name << "\" type=\"camera\">\n"
          << "        <topic>" << sensorPath + "/image" << "</topic>\n"
          << "        <update_rate>" << camSpec.rate << "</update_rate>\n"
          << "        <camera>\n"
          << "          <horizontal_fov>1.047</horizontal_fov>\n"
          << "          <image><width>" << camSpec.width << "</width><height>"
          << camSpec.height << "</height></image>\n"
          << "          <clip><near>0.1</near><far>100</far></clip>\n"
          << "        </camera>\n"
          << "        <pose>" << inst.pose[0] << " " << inst.pose[1] << " " << inst.pose[2]
          << " " << inst.pose[3] << " " << inst.pose[4] << " " << inst.pose[5] << "</pose>\n"
          << "      </sensor>\n";
      break;
    }

    case SensorType::Lidar:
      sdf << "      <sensor name=\"" << inst.name << "\" type=\"gpu_lidar\">\n"
          << "        <topic>" << sensorPath + "/scan" << "</topic>\n"
          << "        <update_rate>10</update_rate>\n"
          << "        <ray>\n"
          << "          <scan><horizontal><samples>360</samples><resolution>1</resolution><min_angle>-3.14159</min_angle><max_angle>3.14159</max_angle></horizontal></scan>\n"
          << "          <range><min>0.1</min><max>50</max></range>\n"
          << "        </ray>\n"
          << "        <pose>" << inst.pose[0] << " " << inst.pose[1] << " " << inst.pose[2]
          << " " << inst.pose[3] << " " << inst.pose[4] << " " << inst.pose[5] << "</pose>\n"
          << "      </sensor>\n";
      break;
    }
  }

  return sdf.str();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::RebuildModelWithHydro(
    const std::string &_instanceName,
    const HydroConfig &_config)
{
  double savedPose[6] = {0, 0, 0, 0, 0, 0};
  {
    auto idIt = modelList.find(_instanceName);
    if (idIt != modelList.end())
      GetModelPoseByIDFromGazebo(idIt->second, savedPose);
  }

  /// ★ 2026-09-28 修（**成套**：5 个 Rebuild* 同族缺陷之一）：**先查缓存，再做破坏性删除**。
  ///   原实现是「先删模型 → 后查缓存」⇒ 缓存缺失时模型**已被删掉**，随后才 return false
  ///   ⇒ **场景凭空少一个模型**（数据丢失级；同族反模式「没做到却报成功」的破坏性版本）。
  ///   本函数会重建实体 ⇒ 一切「可能失败」的准备工作必须在删除**之前**做完。
  if (sdfCache_.find(_instanceName) == sdfCache_.end())
  {
    std::cerr << "RebuildModelWithHydro: 本进程没有该模型的 SDF 缓存，**拒绝重建以免丢模型**"
                 "（请先在本进程 load-ksm / spawn）: "
              << _instanceName << std::endl;
    return false;   ///< **不删模型**
  }

  if (!DeleteModelInGazeboByName(_instanceName, true))
    return false;

  { int retries = 20; uint32_t sid = 0;
    while (retries-- > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!GetModelIDByInstanceNameFromGazebo(_instanceName, sid)) break; } }

  ///（缓存在删除前已校验存在 ⇒ 必然命中；原「删除后才查缓存」的写法已前置）
  auto cacheIt = sdfCache_.find(_instanceName);

  std::string base = cacheIt->second;
  /// ★ 2026-10-25：自包含插件模型（数学船型，网格浮力体）不能走"截断重拼"，
  ///   否则浮力插件丢失。改为在原文上替换水动力标签后整段重放。
  if (base.find("libKSPlugin_HydroBuoyancy.so") != std::string::npos)
  {
    std::string patched = base;
    const std::string tags = BuildHydroSdfTags(_config);
    auto p = patched.find("<linear_damping>");
    auto q = patched.find("</linear_damping>");
    if (p != std::string::npos && q != std::string::npos && q > p)
      patched = patched.substr(0, p) + tags +
                patched.substr(q + std::string("</linear_damping>").size());
    else
    {
      auto fd = patched.find("<fluid_density>");
      if (fd != std::string::npos) patched.insert(fd, tags);
    }
    const std::string off = "<enable_hydro>false</enable_hydro>";
    const std::string on = "<enable_hydro>true</enable_hydro>";
    auto e = patched.find(off);
    if (e != std::string::npos) patched.replace(e, off.size(), on);
    bool anyAm = false;
    for (double v : _config.addedMass) if (v != 0.0) { anyAm = true; break; }
    if (anyAm) InjectFluidAddedMass(patched, _config.addedMass.data());

    SensorConfig sc; GetCachedSensorConfig(_instanceName, sc);
    bool ok = RespawnSdf(_instanceName, patched, savedPose, sc);
    if (ok) hydroConfigCache_[_instanceName] = _config;
    return ok;
  }


  /// [removed] M_A 现在完全在插件中显式计算，不再注入 DART

  auto lpos = base.rfind("</link>");
  if (lpos != std::string::npos) base = base.substr(0, lpos);

  SensorConfig sensorCfg;
  bool hasSensor = GetCachedSensorConfig(_instanceName, sensorCfg);

  std::ostringstream sdf;
  sdf << base;

  if (hasSensor)
    sdf << MakeSensorSDF(sensorCfg, _instanceName, worldName);
  sdf << "    </link>\n";

  BuoyancyConfig bCfg;
  bool haveBuoy = GetCachedBuoyancyConfig(_instanceName, bCfg) && !bCfg.bodies.empty();

  ThrusterConfig tCfg;
  if (GetCachedThrusterConfig(_instanceName, tCfg) && !tCfg.bodies.empty())
    sdf << MakeThrustPluginSDF(tCfg);

  {
    HydroConfig hc = _config;
    sdf << MakeBuoyancyPluginSDF(bCfg, &hc);
  }

  sdf << "  </model>\n"
      << "</sdf>\n";

  /// ★ 2026-09-24 修 P0-1：本路径原先漏了 M_A 注入。
  /// 本函数语义即"用水动力配置重建" ⇒ hydro 恒为启用，故 _hasHydro 传 true。
  ApplyAddedMassToSdf(sdf, true, _config);

  std::string service = "/world/" + worldName + "/create/blocking";
  gz::msgs::EntityFactory req; gz::msgs::Boolean rep; bool result;
  req.set_name(_instanceName); req.set_allow_renaming(false); req.set_sdf(sdf.str());
  auto rp = req.mutable_pose();
  rp->mutable_position()->set_x(savedPose[0]);
  rp->mutable_position()->set_y(savedPose[1]);
  rp->mutable_position()->set_z(savedPose[2]);
  gz::math::Quaterniond q(savedPose[3], savedPose[4], savedPose[5]);
  rp->mutable_orientation()->set_x(q.X()); rp->mutable_orientation()->set_y(q.Y());
  rp->mutable_orientation()->set_z(q.Z()); rp->mutable_orientation()->set_w(q.W());

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  uint32_t id = 0; bool found = false; uint32_t tries = 100;
  /// ★ 2026-09-25（⑩）：先查再睡
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id); }

  if (found) { modelList[_instanceName] = id; hydroConfigCache_[_instanceName] = _config; NotifyModelSpawned(_instanceName, sensorCfg); return true; }
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::RebuildModelWithSensors(
    const std::string &_instanceName,
    const SensorConfig &_config)
{
  /// 缓存或清除传感器配置（后续组装方法会自动从缓存读取）
  if (!_config.instances.empty())
    sensorConfigCache_[_instanceName] = _config;
  else
    sensorConfigCache_.erase(_instanceName);

  double savedPose[6] = {0, 0, 0, 0, 0, 0};
  {
    auto idIt = modelList.find(_instanceName);
    if (idIt != modelList.end())
      GetModelPoseByIDFromGazebo(idIt->second, savedPose);
  }

  /// ★ 2026-09-28 修（**成套**：5 个 Rebuild* 同族缺陷之一）：**先查缓存，再做破坏性删除**。
  ///   原实现是「先删模型 → 后查缓存」⇒ 缓存缺失时模型**已被删掉**，随后才 return false
  ///   ⇒ **场景凭空少一个模型**（数据丢失级；同族反模式「没做到却报成功」的破坏性版本）。
  ///   本函数会重建实体 ⇒ 一切「可能失败」的准备工作必须在删除**之前**做完。
  if (sdfCache_.find(_instanceName) == sdfCache_.end())
  {
    std::cerr << "RebuildModelWithSensors: 本进程没有该模型的 SDF 缓存，**拒绝重建以免丢模型**"
                 "（请先在本进程 load-ksm / spawn）: "
              << _instanceName << std::endl;
    return false;   ///< **不删模型**
  }

  if (!DeleteModelInGazeboByName(_instanceName, true))
    return false;

  { int retries = 20; uint32_t sid = 0;
    while (retries-- > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!GetModelIDByInstanceNameFromGazebo(_instanceName, sid)) break; } }

  ///（缓存在删除前已校验存在 ⇒ 必然命中；原「删除后才查缓存」的写法已前置）
  auto cacheIt = sdfCache_.find(_instanceName);

  std::string base = cacheIt->second;
  /// ★ 2026-10-25：自包含插件模型 → 在原文 <link> 内插入传感器段后重放（不丢浮力）
  if (base.find("libKSPlugin_HydroBuoyancy.so") != std::string::npos)
  {
    std::string patched = base;
    auto l = patched.rfind("</link>");
    if (l != std::string::npos)
      patched = patched.substr(0, l) + MakeSensorSDF(_config, _instanceName, worldName) + patched.substr(l);
    bool ok = RespawnSdf(_instanceName, patched, savedPose, _config);
    if (ok) sensorConfigCache_[_instanceName] = _config;
    return ok;
  }


  /// [removed] M_A 现在完全在插件中显式计算，不再注入 DART

  auto lpos = base.rfind("</link>");
  if (lpos != std::string::npos) base = base.substr(0, lpos);

  std::ostringstream sdf;
  sdf << base;

  sdf << MakeSensorSDF(_config, _instanceName, worldName);
  sdf << "    </link>\n";

  HydroConfig hydroCfg;
  bool hasHydro = GetCachedHydroConfig(_instanceName, hydroCfg);

  BuoyancyConfig bCfg;
  bool haveBuoy = GetCachedBuoyancyConfig(_instanceName, bCfg) && !bCfg.bodies.empty();

  ThrusterConfig tCfg;
  if (GetCachedThrusterConfig(_instanceName, tCfg) && !tCfg.bodies.empty())
    sdf << MakeThrustPluginSDF(tCfg);

  if (hasHydro)
  {
    sdf << MakeBuoyancyPluginSDF(bCfg, &hydroCfg);
  }
  else if (haveBuoy)
  {
    sdf << MakeBuoyancyPluginSDF(bCfg);
  }

  sdf << "  </model>\n"
      << "</sdf>\n";

  /// ★ 2026-09-24 修 P0-1：本路径原先漏了 M_A 注入
  ApplyAddedMassToSdf(sdf, hasHydro, hydroCfg);

  std::string service = "/world/" + worldName + "/create/blocking";
  gz::msgs::EntityFactory req; gz::msgs::Boolean rep; bool result;
  req.set_name(_instanceName); req.set_allow_renaming(false); req.set_sdf(sdf.str());
  auto rp = req.mutable_pose();
  rp->mutable_position()->set_x(savedPose[0]);
  rp->mutable_position()->set_y(savedPose[1]);
  rp->mutable_position()->set_z(savedPose[2]);
  gz::math::Quaterniond q(savedPose[3], savedPose[4], savedPose[5]);
  rp->mutable_orientation()->set_x(q.X()); rp->mutable_orientation()->set_y(q.Y());
  rp->mutable_orientation()->set_z(q.Z()); rp->mutable_orientation()->set_w(q.W());

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req)) return false;

  uint32_t id = 0; bool found = false; uint32_t tries = 100;
  /// ★ 2026-09-25（⑩）：先查再睡
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id); }

  if (found) { modelList[_instanceName] = id; NotifyModelSpawned(_instanceName, _config); return true; }
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
void ModelManager::NotifyModelSpawned(const std::string &_name,
                                       const SensorConfig &_sensorCfg)
{
  auto it = thrusterConfigCache_.find(_name);
  std::string thrusters = "[]";
  if (it != thrusterConfigCache_.end())
  {
    std::ostringstream ts;
    ts << "[";
    bool first = true;
    for (const auto &b : it->second.bodies)
    {
      if (!first) ts << ",";
      ts << "\"" << b.name << "\"";
      first = false;
    }
    ts << "]";
    thrusters = ts.str();
  }

  std::ostringstream ss;
  ss << "[";
  bool firstSensor = true;
  for (const auto &inst : _sensorCfg.instances)
  {
    if (!firstSensor) ss << ",";
    ss << "{\"type\":\"" << sensorTypeName(inst.type) << "\""
       << ",\"name\":\"" << inst.name << "\""
       << ",\"link\":\"" << _sensorCfg.linkName << "\"}";
    firstSensor = false;
  }
  ss << "]";

  std::string msg = "{\"event\":\"spawn\",\"name\":\"" + _name
                  + "\",\"thrusters\":" + thrusters
                  + ",\"sensors\":" + ss.str() + "}";
  std::cout << "NotifyModelSpawned: " << msg << std::endl;

  gz::msgs::StringMsg gzMsg;
  gzMsg.set_data(msg);
  auto pub = node->Advertise<gz::msgs::StringMsg>("/keship/model_ready");
  pub.Publish(gzMsg);
}

void ModelManager::NotifyModelDeleted(const std::string &_name)
{
  std::string msg = "{\"event\":\"delete\",\"name\":\"" + _name + "\"}";
  std::cout << "NotifyModelDeleted: " << msg << std::endl;

  gz::msgs::StringMsg gzMsg;
  gzMsg.set_data(msg);
  auto delPub = node->Advertise<gz::msgs::StringMsg>("/keship/model_ready");
  delPub.Publish(gzMsg);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GetCachedHydroConfig(const std::string &_name, HydroConfig &_out) const
{
  auto it = hydroConfigCache_.find(_name);
  if (it == hydroConfigCache_.end()) return false;
  _out = it->second;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GetCachedSensorConfig(const std::string &_name, SensorConfig &_out) const
{
  auto it = sensorConfigCache_.find(_name);
  if (it == sensorConfigCache_.end()) return false;
  _out = it->second;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::ParseInertiaFromSDF(const std::string &_sdf, InertiaParams &_out)
{
  const std::string &sdf = _sdf;

  auto extract = [&](const std::string &_tag, double &_val) -> bool {
    auto start = sdf.find("<" + _tag + ">");
    if (start == std::string::npos) return false;
    auto valStart = start + _tag.length() + 2;
    auto valEnd = sdf.find("<", valStart);
    if (valEnd == std::string::npos) return false;
    try { _val = std::stod(sdf.substr(valStart, valEnd - valStart)); return true; }
    catch (...) { return false; }
  };

  extract("mass", _out.mass);
  extract("ixx", _out.ixx); extract("ixy", _out.ixy); extract("ixz", _out.ixz);
  extract("iyy", _out.iyy); extract("iyz", _out.iyz); extract("izz", _out.izz);

  auto inertialStart = sdf.find("<inertial>");
  auto inertialEnd = sdf.find("</inertial>");
  if (inertialStart != std::string::npos && inertialEnd != std::string::npos)
  {
    auto poseStart = sdf.find("<pose>", inertialStart);
    if (poseStart != std::string::npos && poseStart < inertialEnd)
    {
      auto valStart = poseStart + 6;
      auto valEnd = sdf.find("<", valStart);
      if (valEnd != std::string::npos)
      {
        std::string poseStr = sdf.substr(valStart, valEnd - valStart);
        double vals[6] = {};
        std::istringstream iss(poseStr);
        for (int i = 0; i < 6 && iss >> vals[i]; ++i) {}
        _out.cogX = vals[0]; _out.cogY = vals[1]; _out.cogZ = vals[2];
      }
    }
  }
  return _out.mass > 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::GetCachedModelInertia(const std::string &_name,
    InertiaParams &_out) const
{
  auto it = sdfCache_.find(_name);
  if (it == sdfCache_.end()) return false;

  return ParseInertiaFromSDF(it->second, _out);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::UpdateCachedSDFInertia(const std::string &_name,
    const InertiaParams &_inertia)
{
  auto it = sdfCache_.find(_name);
  if (it == sdfCache_.end()) return false;

  std::string &sdf = it->second;

  /// 查找 <inertial>...</inertial> 块并替换
  auto start = sdf.find("<inertial>");
  auto end = sdf.find("</inertial>");
  if (start == std::string::npos || end == std::string::npos) return false;
  end += 12; // length of "</inertial>"

  std::ostringstream oss;
  oss << "<inertial>\n";
  if (_inertia.cogX != 0 || _inertia.cogY != 0 || _inertia.cogZ != 0)
    oss << "        <pose>" << _inertia.cogX << " " << _inertia.cogY << " " << _inertia.cogZ << " 0 0 0</pose>\n";
  oss << "        <mass>" << _inertia.mass << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>" << _inertia.ixx << "</ixx><ixy>" << _inertia.ixy << "</ixy><ixz>" << _inertia.ixz << "</ixz>\n"
      << "          <iyy>" << _inertia.iyy << "</iyy><iyz>" << _inertia.iyz << "</iyz>\n"
      << "          <izz>" << _inertia.izz << "</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>";

  sdf.replace(start, end - start, oss.str());
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/// 重建用 SDF 组装（纯函数，2026-10-25）
///
/// 为什么不放在 RebuildModelWithAllPlugins 里：那段代码是"有损重建"的高发区
/// （按配置重拼插件，配置表达力 < SDF），必须能被**离线单测**直接覆盖。
/// 本函数不触网络/文件/单例：给定输入即确定输出。
///
/// 不变量：产物必须保留 `_baseSdf` 里的**全部** `<plugin filename=...>`；
/// 少一个就整段返回 `_baseSdf`（调用方配合 UpdateCachedSDFInertia 已改好 inertia）。
std::string ModelManager::ComposeRebuiltSDF(
    const std::string &_baseSdf, const std::string &_instanceName,
    const std::string &_worldName,
    bool hasSensor, const SensorConfig &sensorCfg,
    bool hasThrust, const ThrusterConfig &tCfg,
    bool hasHydro, const HydroConfig &hydroCfg,
    bool haveBuoy, const BuoyancyConfig &bCfg)
{
  std::string base = _baseSdf;

  /// [removed] M_A 现在完全在插件中显式计算，不再注入 DART

  bool hasExternalForce = base.find("<external_force>") != std::string::npos
                     || base.find("<variable_load>") != std::string::npos;
  auto lpos = base.rfind("</link>");
  if (lpos != std::string::npos) base = base.substr(0, lpos);

  std::ostringstream sdf;
  sdf << base;

  if (hasSensor)
    sdf << MakeSensorSDF(sensorCfg, _instanceName, _worldName);
  sdf << "    </link>\n";

  if (hasThrust)
    sdf << MakeThrustPluginSDF(tCfg);

  if (hasHydro)
  {
    sdf << MakeBuoyancyPluginSDF(bCfg, &hydroCfg);
  }
  else if (haveBuoy)
  {
    sdf << MakeBuoyancyPluginSDF(bCfg);
  }

  sdf << "    <plugin filename=\"libKSPlugin_VariableMass.so\""
      << " name=\"keship::KSPlugin_VariableMass\">\n"
      << "      <link_name>base_link</link_name>\n";
  if (hasExternalForce)
    sdf << "      <external_force>\n"
        << "        <link_name>base_link</link_name>\n"
        << "      </external_force>\n";
  sdf << "    </plugin>\n  </model>\n</sdf>\n";

  /// ★ 2026-09-24 修 P0-1：本路径原先漏了 M_A 注入
  ApplyAddedMassToSdf(sdf, hasHydro, hydroCfg);

  /// ★ 2026-10-25 通用防护：**重建不得丢插件**
  ///   重建按 BuoyancyConfig/SensorConfig 等**键值配置**重拼插件，
  ///   而配置能表达的东西比 SDF 少（典型：数学船型的网格浮力体）。
  ///   对比原缓存 SDF 与重建产物的 plugin 集合；少一个就整段重放 ——
  ///   宁可放弃"按配置刷新"，也不让模型丢功能（2026-10-25 自由落体事故）。
  std::string finalSdf = sdf.str();
  {
    const std::vector<std::string> before = PluginFileNamesInSdf(_baseSdf);
    const std::vector<std::string> after = PluginFileNamesInSdf(finalSdf);
    std::vector<std::string> missing;
    for (const auto &pname : before)
      if (std::find(after.begin(), after.end(), pname) == after.end())
        missing.push_back(pname);
    if (!missing.empty())
    {
      std::cerr << "RebuildModelWithAllPlugins: 重建会丢失插件";
      for (const auto &mname : missing) std::cerr << " [" << mname << "]";
      std::cerr << " —— 改用整段重放（缓存 SDF 已含更新后的 inertia）" << std::endl;
      finalSdf = _baseSdf;   // 注意：base 已被截到 </link>，必须回到完整原文
    }
  }

  return finalSdf;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// 用给定 SDF 原文重放一个模型（保留原 pose），成功后登记并通知
/// 供"自包含插件模型"的编辑重建使用（2026-10-25）
bool ModelManager::RespawnSdf(const std::string &_name, const std::string &_sdf,
                              const double _pose[6], const SensorConfig &_sc)
{
  std::string service = "/world/" + worldName + "/create/blocking";
  gz::msgs::EntityFactory req;
  gz::msgs::Boolean rep;
  bool result = false;
  req.set_name(_name);
  req.set_allow_renaming(false);
  req.set_sdf(_sdf);
  auto *rp = req.mutable_pose();
  rp->mutable_position()->set_x(_pose[0]);
  rp->mutable_position()->set_y(_pose[1]);
  rp->mutable_position()->set_z(_pose[2]);
  gz::math::Quaterniond q(_pose[3], _pose[4], _pose[5]);
  rp->mutable_orientation()->set_x(q.X());
  rp->mutable_orientation()->set_y(q.Y());
  rp->mutable_orientation()->set_z(q.Z());
  rp->mutable_orientation()->set_w(q.W());
  if (!CreateEntityViaService(req)) return false;

  uint32_t id = 0;
  bool found = GetModelIDByInstanceNameFromGazebo(_name, id);
  uint32_t tries = 100;
  while (!found && tries-- > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_name, id);
  }
  if (found)
  {
    modelList[_name] = id;
    NotifyModelSpawned(_name, _sc);
    return true;
  }
  return false;
}

bool ModelManager::RebuildModelWithAllPlugins(const std::string &_instanceName)
{
  double savedPose[6] = {0, 0, 0, 0, 0, 0};
  {
    auto idIt = modelList.find(_instanceName);
    if (idIt != modelList.end())
      GetModelPoseByIDFromGazebo(idIt->second, savedPose);
  }

  /// ★ 2026-09-28 修（**成套**：5 个 Rebuild* 同族缺陷之一）：**先查缓存，再做破坏性删除**。
  ///   原实现是「先删模型 → 后查缓存」⇒ 缓存缺失时模型**已被删掉**，随后才 return false
  ///   ⇒ **场景凭空少一个模型**（数据丢失级；同族反模式「没做到却报成功」的破坏性版本）。
  ///   本函数会重建实体 ⇒ 一切「可能失败」的准备工作必须在删除**之前**做完。
  if (sdfCache_.find(_instanceName) == sdfCache_.end())
  {
    std::cerr << "RebuildModelWithAllPlugins: 本进程没有该模型的 SDF 缓存，**拒绝重建以免丢模型**"
                 "（请先在本进程 load-ksm / spawn）: "
              << _instanceName << std::endl;
    return false;   ///< **不删模型**
  }

  /// 供「创建失败时尽力还原」用（见下方创建失败分支）
  const std::string origBaseSdf = sdfCache_[_instanceName];

  if (!DeleteModelInGazeboByName(_instanceName, true)) return false;

  { int retries = 20; uint32_t sid = 0;
    while (retries-- > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!GetModelIDByInstanceNameFromGazebo(_instanceName, sid)) break; } }

  ///（缓存在删除前已校验存在 ⇒ 必然命中）
  auto it = sdfCache_.find(_instanceName);
  std::string base = it->second;

  /// ★ 2026-10-25 修（张老师实测：数学船型改质量 +1kg 后自由落体）：
  ///   本函数随后会把 SDF 截到最后一个 `</link>`，再按 BuoyancyConfig 重拼插件；
  ///   而 BuoyancyConfig 只能表达 Box/Cylinder —— 数学船型的 `<buoyancy>` 是**网格**，
  ///   无法表达 ⇒ 浮力插件整段丢失 ⇒ 模型失去浮力。
  ///   对「SDF 自含 HydroBuoyancy 插件 **且** 没有缓存浮力配置」的模型改为**整段重放**：
  ///   调用方（改质量那条路）已用 UpdateCachedSDFInertia 改好 `<inertial>`，
  ///   重放即可让新质量生效，同时保住网格浮力体。
  {
    BuoyancyConfig probeCfg;
    const bool haveCfgBuoy = GetCachedBuoyancyConfig(_instanceName, probeCfg)
                             && !probeCfg.bodies.empty();
    if (!haveCfgBuoy && base.find("libKSPlugin_HydroBuoyancy.so") != std::string::npos)
    {
      std::cout << "RebuildModelWithAllPlugins: " << _instanceName
                << " 为自含插件的模型（网格浮力体无法用 BuoyancyConfig 表达），整段重放"
                << std::endl;
      gz::msgs::EntityFactory req;
      req.set_name(_instanceName);
      req.set_allow_renaming(false);
      req.set_sdf(base);
      auto *rp = req.mutable_pose();
      rp->mutable_position()->set_x(savedPose[0]);
      rp->mutable_position()->set_y(savedPose[1]);
      rp->mutable_position()->set_z(savedPose[2]);
      gz::math::Quaterniond rq(savedPose[3], savedPose[4], savedPose[5]);
      rp->mutable_orientation()->set_x(rq.X());
      rp->mutable_orientation()->set_y(rq.Y());
      rp->mutable_orientation()->set_z(rq.Z());
      rp->mutable_orientation()->set_w(rq.W());

      if (!CreateEntityViaService(req))
      {
        std::cerr << "RebuildModelWithAllPlugins: 重放失败，用原 SDF 还原: "
                  << _instanceName << std::endl;
        gz::msgs::EntityFactory req2;
        req2.set_name(_instanceName);
        req2.set_allow_renaming(false);
        req2.set_sdf(origBaseSdf);
        auto *rp2 = req2.mutable_pose();
        rp2->mutable_position()->set_x(savedPose[0]);
        rp2->mutable_position()->set_y(savedPose[1]);
        rp2->mutable_position()->set_z(savedPose[2]);
        rp2->mutable_orientation()->set_x(rq.X());
        rp2->mutable_orientation()->set_y(rq.Y());
        rp2->mutable_orientation()->set_z(rq.Z());
        rp2->mutable_orientation()->set_w(rq.W());
        CreateEntityViaService(req2);
        uint32_t rid = 0;
        if (GetModelIDByInstanceNameFromGazebo(_instanceName, rid))
          modelList[_instanceName] = rid;
        return false;
      }

      uint32_t rid = 0;
      bool found2 = GetModelIDByInstanceNameFromGazebo(_instanceName, rid);
      uint32_t tries2 = 100;
      while (!found2 && tries2-- > 0)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        found2 = GetModelIDByInstanceNameFromGazebo(_instanceName, rid);
      }
      if (found2)
      {
        modelList[_instanceName] = rid;
        SensorConfig scEmpty;
        NotifyModelSpawned(_instanceName, scEmpty);
        return true;
      }
      return false;
    }
  }

  SensorConfig sensorCfg;
  const bool hasSensor = GetCachedSensorConfig(_instanceName, sensorCfg);
  ThrusterConfig tCfg;
  const bool hasThrust = GetCachedThrusterConfig(_instanceName, tCfg) && !tCfg.bodies.empty();
  HydroConfig hydroCfg;
  const bool hasHydro = GetCachedHydroConfig(_instanceName, hydroCfg);
  BuoyancyConfig bCfg;
  const bool haveBuoy = GetCachedBuoyancyConfig(_instanceName, bCfg) && !bCfg.bodies.empty();

  const std::string finalSdf = ComposeRebuiltSDF(
      origBaseSdf, _instanceName, worldName,
      hasSensor, sensorCfg, hasThrust, tCfg,
      hasHydro, hydroCfg, haveBuoy, bCfg);

  std::string service = "/world/" + worldName + "/create/blocking";
  gz::msgs::EntityFactory req; gz::msgs::Boolean rep; bool result;
  req.set_name(_instanceName); req.set_allow_renaming(false); req.set_sdf(finalSdf);
  auto rp = req.mutable_pose();
  rp->mutable_position()->set_x(savedPose[0]);
  rp->mutable_position()->set_y(savedPose[1]);
  rp->mutable_position()->set_z(savedPose[2]);
  gz::math::Quaterniond q(savedPose[3], savedPose[4], savedPose[5]);
  rp->mutable_orientation()->set_x(q.X()); rp->mutable_orientation()->set_y(q.Y());
  rp->mutable_orientation()->set_z(q.Z()); rp->mutable_orientation()->set_w(q.W());

  /// 统一入口（2026-09-24 收敛：服务名 / 超时 / 结果检查只有一处实现）
  if (!CreateEntityViaService(req))
  {
    /// ★ 2026-09-28：**创建失败也不能「删了不重建」** —— 用缓存里的原始 base SDF
    ///   + 原保存位姿**尽力还原**（插件配置可能未生效，但**模型不会凭空消失**），
    ///   并把实际发生了什么**明确报出来**（绝不静默）。
    std::cerr << "RebuildModelWithAllPlugins: 重建创建失败，尽力还原原模型: "
              << _instanceName << std::endl;
    gz::msgs::EntityFactory req2;
    req2.set_name(_instanceName);
    req2.set_allow_renaming(false);
    req2.set_sdf(origBaseSdf);
    auto rp2 = req2.mutable_pose();
    rp2->mutable_position()->set_x(savedPose[0]);
    rp2->mutable_position()->set_y(savedPose[1]);
    rp2->mutable_position()->set_z(savedPose[2]);
    rp2->mutable_orientation()->set_x(q.X());
    rp2->mutable_orientation()->set_y(q.Y());
    rp2->mutable_orientation()->set_z(q.Z());
    rp2->mutable_orientation()->set_w(q.W());
    if (CreateEntityViaService(req2))
    {
      uint32_t rid = 0;
      if (GetModelIDByInstanceNameFromGazebo(_instanceName, rid))
        modelList[_instanceName] = rid;
      std::cerr << "RebuildModelWithAllPlugins: 已用原 SDF 还原（插件配置可能未生效）"
                << std::endl;
    }
    else
    {
      std::cerr << "RebuildModelWithAllPlugins: **还原也失败，模型已丢失**: "
                << _instanceName << std::endl;
    }
    return false;
  }

  uint32_t id = 0; bool found = false; uint32_t tries = 100;
  /// ★ 2026-09-25（⑩）：先查再睡
  found = GetModelIDByInstanceNameFromGazebo(_instanceName, id);
  while (!found && tries-- > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    found = GetModelIDByInstanceNameFromGazebo(_instanceName, id); }

  if (found) { modelList[_instanceName] = id; NotifyModelSpawned(_instanceName, sensorCfg); return true; }
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::SaveModelToKSM(const std::string &_name, const std::string &_filepath,
                                   const std::string &_sdfName)
{
  /// 查找缓存
  auto sdfIt = sdfCache_.find(_name);
  if (sdfIt == sdfCache_.end())
  {
    std::cerr << "SaveModelToKSM: 模型 '" << _name << "' 无缓存 SDF" << std::endl;
    return false;
  }

  std::ofstream ofs(_filepath);
  if (!ofs)
  {
    std::cerr << "SaveModelToKSM: 无法创建文件 " << _filepath << std::endl;
    return false;
  }

  /// BASE_SDF（保存时把 SDF 内模型名还原为原名，避免实例名前缀写入文件）
  std::string baseSdf = sdfIt->second;

  /// 相对路径化：mesh uri 反查回源路径（robot_model/... 相对 models_all），
  /// 保证 ksm 持久化源路径而非缓存路径；未命中（数学船模 data/models/ 等）
  /// 则剥 file:// + keship_ws 前缀，保持相对路径
  {
    for (const auto &kv : meshCacheToSrcUri_)
    {
      const std::string cacheUri = "file://" + kv.first;
      size_t p = 0;
      while ((p = baseSdf.find(cacheUri, p)) != std::string::npos)
      {
        baseSdf.replace(p, cacheUri.size(), kv.second);
        p += kv.second.size();
      }
    }
    const std::string wsPrefix = "file://" + keship::KeshipHomeWsDir() + "/";
    size_t p = 0;
    while ((p = baseSdf.find(wsPrefix, p)) != std::string::npos)
      baseSdf.replace(p, wsPrefix.size(), "");
  }

  if (!_sdfName.empty() && _sdfName != _name)
  {
    auto ns = baseSdf.find("model name=\"");
    if (ns != std::string::npos)
    {
      ns += 12;
      auto ne = baseSdf.find("\"", ns);
      if (ne != std::string::npos)
        baseSdf.replace(ns, ne - ns, _sdfName);
    }
  }
  ofs << "[keship_model v1]\n\n";

  /// BASE_SDF
  ofs << "===BASE_SDF===\n"
      << baseSdf << "\n"
      << "===END===\n\n";

  /// BUOYANCY
  ofs << "===BUOYANCY===\n";
  auto buoyIt = buoyancyConfigCache_.find(_name);
  if (buoyIt != buoyancyConfigCache_.end() && !buoyIt->second.bodies.empty())
  {
    const auto &cfg = buoyIt->second;
    ofs << "link_name=" << cfg.linkName << "\n"
        << "fluid_density=" << cfg.fluidDensity << "\n";
    for (const auto &b : cfg.bodies)
    {
      ofs << "body=" << b.name << ",";
      if (b.type == PrimitiveType::Box)
        ofs << "Box," << b.box.dx << "," << b.box.dy << "," << b.box.dz << ",0,";
      else
        ofs << "Cyl," << b.cyl.radius << "," << b.cyl.length << ",0,0,";
      ofs << b.pose[0] << "," << b.pose[1] << "," << b.pose[2] << ","
          << b.pose[3] << "," << b.pose[4] << "," << b.pose[5] << "\n";
    }
  }
  ofs << "===END===\n\n";

  /// THRUST
  ofs << "===THRUST===\n";
  auto thrustIt = thrusterConfigCache_.find(_name);
  if (thrustIt != thrusterConfigCache_.end() && !thrustIt->second.bodies.empty())
  {
    const auto &cfg = thrustIt->second;
    ofs << "link_name=" << cfg.linkName << "\n";
    for (const auto &b : cfg.bodies)
    {
      ofs << "body=" << b.name << ","
          << b.pose[0] << "," << b.pose[1] << "," << b.pose[2] << ","
          << b.pose[3] << "," << b.pose[4] << "," << b.pose[5] << ","
          << b.maxForce << "\n";
    }
  }
  ofs << "===END===\n\n";

  /// HYDRO
  ofs << "===HYDRO===\n";
  auto hydroIt = hydroConfigCache_.find(_name);
   if (hydroIt != hydroConfigCache_.end())
  {
    const auto &cfg = hydroIt->second;
    ofs << "link_name=" << cfg.linkName << "\n";
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 6; ++j)
        ofs << "am=m" << i << j << "," << cfg.addedMass[i*6 + j] << "\n";
    const char *dNames[] = {"Xu","Yv","Zw","Kp","Mq","Nr","Yr","Nv"};
    for (int i = 0; i < 8; i++)
      ofs << "damp=" << dNames[i] << "," << cfg.damping[i] << "\n";
    for (const auto &t : cfg.dampingTerms)
    {
      ofs << "dampt=" << t.target << "," << t.coefficient;
      for (const auto &f : t.factors)
        ofs << "," << f.dof << "," << f.order;
      ofs << "\n";
    }
    ofs << "damping_mode=" << cfg.dampingMode << "\n"
        << "damping_ratio=" << cfg.dampingRatio << "\n";
  }
  ofs << "===END===\n\n";

  /// SENSORS
  auto sensorIt = sensorConfigCache_.find(_name);
  if (sensorIt != sensorConfigCache_.end() && !sensorIt->second.instances.empty())
  {
    const auto &sc = sensorIt->second;
    ofs << "===SENSORS===\n"
        << "link_name=" << sc.linkName << "\n";
    for (const auto &inst : sc.instances)
    {
      ofs << "instance=" << sensorTypeName(inst.type) << "," << inst.name << ","
          << inst.pose[0] << "," << inst.pose[1] << "," << inst.pose[2] << ","
          << inst.pose[3] << "," << inst.pose[4] << "," << inst.pose[5];
      /// ★ 2026-09-26「Camera 规格用户自选」：**非默认档**才追加 `,宽,高,帧率`。
      /// 默认档保持 8 字段 ⇒ 与老格式逐字一致（老工具/老资产零影响）。
      /// 写的是 `ClampCameraSpec()` 夹紧后的**实际生效值**，不是原始输入。
      if (inst.type == SensorType::Camera)
      {
        const CameraSpec cs = ClampCameraSpec(inst.camWidth, inst.camHeight, inst.camRate);
        /// 只有**与默认档不同**才追加 ⇒ GUI 存具体值（320/240/10）时也不会多出冗余字段
        if (cs.width != kCameraDefaultWidth || cs.height != kCameraDefaultHeight ||
            cs.rate != kCameraDefaultRate)
          ofs << "," << cs.width << "," << cs.height << "," << cs.rate;
      }
      ofs << "\n";
    }
    ofs << "===END===\n\n";
  }

  /// ★ 2026-09-28（D2）：碰撞体积计算方式
  ///   **只在非默认档（Mesh）时才写** ⇒ 默认档与老 ksm 与改动前**逐字节一致**，
  ///   老工具与老资产零影响（与 SENSORS 段「非默认档才追加字段」同一策略）。
  {
    const CollisionMode cm = GetCollisionMode(_name);
    if (cm != CollisionMode::Mesh)
    {
      ofs << "===COLLISION===\n"
          << collisionModeName(cm) << "\n"
          << "===END===\n\n";
    }
  }

  std::cout << "SaveModelToKSM: 已保存 " << _name << " -> " << _filepath << std::endl;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief 模型来源元数据访问
void ModelManager::SetModelSource(const std::string &_instanceName,
    const WorldModelSource &_src)
{
  modelSource_[_instanceName] = _src;
}

bool ModelManager::GetModelSource(const std::string &_instanceName,
    WorldModelSource &_out) const
{
  auto it = modelSource_.find(_instanceName);
  if (it == modelSource_.end()) return false;
  _out = it->second;
  return true;
}

void ModelManager::ClearModelSources()
{
  modelSource_.clear();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief 水域来源元数据访问（0.0.4）
void ModelManager::SetWaterSource(const std::string &_instanceName,
    const WaterWorldSource &_src)
{
  waterSource_[_instanceName] = _src;
}

bool ModelManager::GetWaterSource(const std::string &_instanceName,
    WaterWorldSource &_out) const
{
  auto it = waterSource_.find(_instanceName);
  if (it == waterSource_.end()) return false;
  _out = it->second;
  return true;
}

void ModelManager::ClearWaterSources()
{
  waterSource_.clear();
}

void ModelManager::RemoveWaterSource(const std::string &_instanceName)
{
  waterSource_.erase(_instanceName);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::SaveWorldManifest(const std::string &_dirpath,
    const std::string &_worldName,
    const std::vector<std::string> &_selectedModels,
    const std::string &_waterAreasJSON,
    int *_failCount,
    const std::string &_rootChain)
{
  if (_failCount) *_failCount = 0;
  if (_worldName.empty())
  {
    std::cerr << "SaveWorldManifest: 世界名为空" << std::endl;
    return false;
  }

  // 覆盖保存：清空世界目录全部内容（旧清单 / 旧 ksm / 旧子世界文件夹），
  // 防止未勾选模型残留；先收集条目再统一删除（删除当前项后继续迭代不安全）
  {
    std::error_code ec;
    std::vector<std::filesystem::path> toRemove;
    std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(_dirpath, ec);
         !ec && it != end; it.increment(ec))
    {
      toRemove.push_back(it->path());
    }
    ec.clear();
    for (const auto &p : toRemove)
    {
      std::filesystem::remove_all(p, ec);
      ec.clear();
    }
  }

  // 子 World 链前缀（子 ksw 独立导出：剥离前缀 + 过滤不在链内实体）
  std::vector<std::string> rootSegs;
  {
    std::string cur;
    for (const char c : _rootChain)
    {
      if (c == '/') { if (!cur.empty()) { rootSegs.push_back(cur); cur.clear(); } }
      else cur += c;
    }
    if (!cur.empty()) rootSegs.push_back(cur);
  }
  auto chainMatches = [&rootSegs](const std::vector<std::string> &_chain) -> bool
  {
    if (rootSegs.empty()) return true;
    if (_chain.size() < rootSegs.size()) return false;
    for (size_t i = 0; i < rootSegs.size(); ++i)
      if (_chain[i] != rootSegs[i]) return false;
    return true;
  };
  auto stripPrefix = [&rootSegs](std::vector<std::string> &_chain)
  {
    if (!rootSegs.empty() && _chain.size() >= rootSegs.size())
      _chain.erase(_chain.begin(), _chain.begin() + rootSegs.size());
  };

  // 确定要保存的模型集合
  // （空 = 全部场景模型；但子 ksw 导出（_rootChain）时空 = 仅导出水域不导出模型）
  std::vector<std::string> names;
  if (_rootChain.empty() || !_selectedModels.empty())
  {
    for (const auto &[name, id] : modelList)
    {
      if (_selectedModels.empty() ||
          std::find(_selectedModels.begin(), _selectedModels.end(), name) != _selectedModels.end())
        names.push_back(name);
    }
  }

  // 0.0.4 根：version + attributes + models + waters + subWorlds（顶层四段）
  JsonValue root = JvMakeEmptyWorldNode(_worldName);
  root.obj.insert(root.obj.begin(), {"version", JvMakeStr("0.0.4")});

  int failCount = 0;

  for (const auto &name : names)
  {
    double pose[6] = {};
    if (!GetModelPoseByIDFromGazebo(modelList.at(name), pose))
    {
      std::cerr << "SaveWorldManifest: 获取 " << name << " 位姿失败，跳过" << std::endl;
      ++failCount;
      continue;
    }

    // 来源元数据：path = 子 World 链（空 = 顶层）；baseName = JSON 原键名
    WorldModelSource src;
    std::vector<std::string> subChain;
    if (modelSource_.count(name))
    {
      src = modelSource_.at(name);
      subChain = src.path;
    }
    // 子 ksw 导出：链前缀不匹配 → 跳过；匹配 → 剥离前缀
    if (!chainMatches(subChain))
      continue;
    stripPrefix(subChain);
    std::string leafName = src.baseName.empty() ? name : src.baseName;

    // 按子 World 链建立多层 .ksw 文件夹：dir/world1.ksw/subA.ksw/boat.ksm
    std::string relDir;
    for (const auto &seg : subChain)
      relDir += seg + ".ksw/";
    if (!relDir.empty())
    {
      std::error_code ec;
      std::filesystem::create_directories(_dirpath + "/" + relDir, ec);
    }

    std::string ksmFile = relDir + leafName + ".ksm";
    std::string ksmPath = _dirpath + "/" + ksmFile;
    if (!SaveModelToKSM(name, ksmPath, leafName))
    {
      std::cerr << "SaveWorldManifest: 保存 " << name << " 模型失败" << std::endl;
      ++failCount;
      continue;
    }

    JsonValue leaf; leaf.kind = JsonValue::Kind::Object;
    JsonValue leafAttr; leafAttr.kind = JsonValue::Kind::Object;
    leafAttr.obj.emplace_back("file", JvMakeStr(ksmFile));
    leafAttr.obj.emplace_back("pose", JvMakePose(pose));
    bool isSt = false; GetCachedModelStatic(name, isSt);
    leafAttr.obj.emplace_back("isStatic", JvMakeBool(isSt));
    leaf.obj.emplace_back("attributes", std::move(leafAttr));

    JsonValue *target = JvGetOrCreateSubWorld(root, subChain);
    JsonValue *models = JvWorldSegment(*target, "models");
    if (models) models->obj.emplace_back(leafName, std::move(leaf));
  }

  // 水区（按 waterSource_ 还原子 World 嵌套；无来源 → 顶层 waters）
  if (!_waterAreasJSON.empty())
  {
    auto areas = DeserializeWaterAreas(_waterAreasJSON);
    for (const auto &a : areas)
    {
      JsonValue leaf; leaf.kind = JsonValue::Kind::Object;

      JsonValue attr2; attr2.kind = JsonValue::Kind::Object;
      JsonValue bounds; bounds.kind = JsonValue::Kind::Object;
      bounds.obj.emplace_back("xMin", JvMakeNum(a.xMin));
      bounds.obj.emplace_back("xMax", JvMakeNum(a.xMax));
      bounds.obj.emplace_back("yMin", JvMakeNum(a.yMin));
      bounds.obj.emplace_back("yMax", JvMakeNum(a.yMax));
      attr2.obj.emplace_back("bounds", std::move(bounds));
      attr2.obj.emplace_back("baseHeight", JvMakeNum(a.baseHeight));

      JsonValue verts; verts.kind = JsonValue::Kind::Array;
      for (const auto &p : a.vertices)
      {
        JsonValue pt; pt.kind = JsonValue::Kind::Array;
        pt.arr.push_back(JvMakeNum(p.first));
        pt.arr.push_back(JvMakeNum(p.second));
        verts.arr.push_back(std::move(pt));
      }
      attr2.obj.emplace_back("vertices", std::move(verts));
      leaf.obj.emplace_back("attributes", std::move(attr2));

      JsonValue fluids; fluids.kind = JsonValue::Kind::Object;
      JsonValue wave; wave.kind = JsonValue::Kind::Object;
      wave.obj.emplace_back("model", JvMakeStr(a.waveParams.model));
      wave.obj.emplace_back("period", JvMakeNum(a.waveParams.period));
      wave.obj.emplace_back("number", JvMakeNum(a.waveParams.number));
      wave.obj.emplace_back("amplitude", JvMakeNum(a.waveParams.amplitude));
      wave.obj.emplace_back("scale", JvMakeNum(a.waveParams.scale));
      wave.obj.emplace_back("gain", JvMakeNum(a.waveParams.gain));
      /// ★ 2026-10-24（张老师拍板方案 A）：directionAuto 为真时：
      ///   浪向 = 「风速与流速中较大者」的方向；两者都为 0 时保持手动值。
      ///   **生成期决定**（不依赖运行时联动）⇒ 改风/流方向后需重新生成 world。
      double waveDirEff = a.waveParams.direction;
      if (a.waveDirectionAuto)
      {
        const double vWind = a.windParams.speed;
        const double vCur  = a.currentParams.speed;
        if (vWind > 1e-9 || vCur > 1e-9)
          waveDirEff = (vWind >= vCur) ? a.windParams.direction : a.currentParams.direction;
      }
      wave.obj.emplace_back("direction", JvMakeNum(waveDirEff));
      wave.obj.emplace_back("angle", JvMakeNum(a.waveParams.angle));
      wave.obj.emplace_back("steepness", JvMakeNum(a.waveParams.steepness));
      wave.obj.emplace_back("tau", JvMakeNum(a.waveParams.tau));
      fluids.obj.emplace_back("wave", std::move(wave));

      JsonValue wind; wind.kind = JsonValue::Kind::Object;
      wind.obj.emplace_back("speed", JvMakeNum(a.windParams.speed));
      wind.obj.emplace_back("direction", JvMakeNum(a.windParams.direction));
      fluids.obj.emplace_back("wind", std::move(wind));

      JsonValue cur; cur.kind = JsonValue::Kind::Object;
      cur.obj.emplace_back("speed", JvMakeNum(a.currentParams.speed));
      cur.obj.emplace_back("direction", JvMakeNum(a.currentParams.direction));
      fluids.obj.emplace_back("current", std::move(cur));

      fluids.obj.emplace_back("directionAuto", JvMakeBool(a.waveDirectionAuto));
      leaf.obj.emplace_back("fluids", std::move(fluids));

      // 还原位置：waterSource_ 登记 → 子容器嵌套；未登记 → 顶层 waters
      std::vector<std::string> subChain;
      std::string jsonName = a.name;
      auto wsrc = waterSource_.find(a.name);
      if (wsrc != waterSource_.end())
      {
        subChain = wsrc->second.path;
        if (!wsrc->second.baseName.empty()) jsonName = wsrc->second.baseName;
        // 子 ksw 导出：链前缀不匹配 → 跳过；匹配 → 剥离前缀
        if (!chainMatches(subChain))
          continue;
        stripPrefix(subChain);
      }
      JsonValue *target = JvGetOrCreateSubWorld(root, subChain);
      JsonValue *waters = JvWorldSegment(*target, "waters");
      if (waters) waters->obj.emplace_back(jsonName.empty() ? "未命名水域" : jsonName,
                                           std::move(leaf));
    }
  }

  std::string jsonPath = _dirpath + "/keship_world.kswf";
  std::ofstream ofs(jsonPath);
  if (!ofs) return false;

  WriteTree(ofs, root, 1);
  ofs << "\n";
  std::cout << "SaveWorldManifest: " << jsonPath << "（0.0.4 结构）" << std::endl;

  if (_failCount) *_failCount = failCount;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::LoadWorldManifest(const std::string &_filepath,
    std::vector<WorldManifestItem> &_out,
    std::vector<WorldWaterItem> &_waters,
    std::string *_worldName,
    std::array<double, 6> *_worldPose)
{
  std::ifstream ifs(_filepath);
  if (!ifs) return false;
  std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());

  JsonValue doc;
  JsonReader rd(content);
  if (!rd.Parse(doc) || doc.kind != JsonValue::Kind::Object)
  {
    std::cerr << "LoadWorldManifest: 无法解析 " << _filepath << std::endl;
    return false;
  }

  // 版本校验：仅支持 0.0.4（字符串），其余一律拒绝（不兼容旧版）
  std::string version;
  for (const auto &[k, v] : doc.obj)
    if (k == "version" && v.kind == JsonValue::Kind::String) version = v.str;
  if (version.empty())
  {
    std::cerr << "LoadWorldManifest: 缺少 version 字段（可能为旧版格式，不兼容）" << std::endl;
    return false;
  }
  if (version != "0.0.4")
  {
    std::cerr << "LoadWorldManifest: 版本 " << version
              << " 不受支持（仅支持 0.0.4）" << std::endl;
    return false;
  }

  // 顶层 attributes：name / pose
  if (_worldName)
  {
    for (const auto &[k, v] : doc.obj)
      if (k == "attributes" && v.kind == JsonValue::Kind::Object)
        for (const auto &[kk, vv] : v.obj)
          if (kk == "name" && vv.kind == JsonValue::Kind::String) { *_worldName = vv.str; break; }
  }
  if (_worldPose)
  {
    _worldPose->fill(0.0);
    for (const auto &[k, v] : doc.obj)
      if (k == "attributes" && v.kind == JsonValue::Kind::Object)
        for (const auto &[kk, vv] : v.obj)
          if (kk == "pose" && vv.kind == JsonValue::Kind::Array)
          {
            int idx = 0;
            for (const auto &el : vv.arr)
              if (idx < 6 && el.kind == JsonValue::Kind::Number) (*_worldPose)[idx++] = el.num;
          }
  }

  std::string dir = _filepath.substr(0, _filepath.find_last_of('/'));
  if (dir.empty()) dir = ".";

  // 递归展平 models / waters / subWorlds
  std::function<void(const JsonValue &, const std::vector<std::string> &)> walk =
      [&](const JsonValue &node, const std::vector<std::string> &pathSoFar)
  {
    const JsonValue *models = nullptr, *waters = nullptr, *subs = nullptr;
    for (const auto &kv : node.obj)
    {
      if (kv.second.kind != JsonValue::Kind::Object) continue;
      if (kv.first == "models") models = &kv.second;
      else if (kv.first == "waters") waters = &kv.second;
      else if (kv.first == "subWorlds") subs = &kv.second;
    }

    if (models)
    {
      for (const auto &[key, val] : models->obj)
      {
        if (val.kind != JsonValue::Kind::Object) continue;
        WorldManifestItem item;
        item.path = pathSoFar;
        item.baseName = key;
        for (const auto &[kk, vv] : val.obj)
        {
          if (kk != "attributes" || vv.kind != JsonValue::Kind::Object) continue;
          for (const auto &[kk2, vv2] : vv.obj)
          {
            if (kk2 == "file" && vv2.kind == JsonValue::Kind::String)
            {
              std::string file = vv2.str;
              // 相对路径（不以 / 或 \ 开头，含多段如 world1.ksw/boat.ksm）统一补全到清单目录
              if (!file.empty() && file[0] != '/' && file[0] != '\\')
                file = dir + "/" + file;
              item.ksmPath = file;
            }
            else if (kk2 == "pose" && vv2.kind == JsonValue::Kind::Array)
            {
              int k = 0;
              for (const auto &el : vv2.arr)
                if (k < 6 && el.kind == JsonValue::Kind::Number) item.pose[k++] = el.num;
            }
            else if (kk2 == "isStatic" && vv2.kind == JsonValue::Kind::Bool)
              item.isStatic = vv2.boolean;
          }
        }
        _out.push_back(std::move(item));
      }
    }

    if (waters)
    {
      for (const auto &[key, val] : waters->obj)
      {
        if (val.kind != JsonValue::Kind::Object) continue;
        WorldWaterItem item;
        item.path = pathSoFar;
        item.baseName = key;
        item.area = BuildWaterAreaFromLeaf(val);
        item.area.name = key;                     // 键名即原水域名
        _waters.push_back(std::move(item));
      }
    }

    if (subs)
    {
      std::vector<std::string> subPath = pathSoFar;
      for (const auto &[key, val] : subs->obj)
      {
        if (val.kind != JsonValue::Kind::Object) continue;
        subPath.push_back(key);
        walk(val, subPath);
        subPath.pop_back();
      }
    }
  };

  std::vector<std::string> rootPath;
  walk(doc, rootPath);

  for (const auto &it : _out)
    std::cout << "LoadWorldManifest: " << it.baseName << " <" << it.ksmPath << ">" << std::endl;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
bool ModelManager::ParseKSMFile(const std::string &_filepath,
                                 std::string &_baseSDF,
                                 BuoyancyConfig &_buoyCfg,
                                 ThrusterConfig &_thrustCfg,
                                 HydroConfig &_hydroCfg,
                                 SensorConfig &_sensorCfg,
                                 CollisionMode *_outCollisionMode)
{
  std::ifstream ifs(_filepath);
  if (!ifs)
  {
    std::cerr << "ParseKSMFile: 无法打开 " << _filepath << std::endl;
    return false;
  }

  enum { NONE, BASE_SDF, BUOYANCY, THRUST, HYDRO, SENSORS, COLLISION } section = NONE;
  std::string line;
  std::ostringstream sdfBuf;

  try {
  while (std::getline(ifs, line))
  {
    /// 跳过 CR
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line == "===BASE_SDF===")      { section = BASE_SDF;  continue; }
    if (line == "===BUOYANCY===")      { section = BUOYANCY;  continue; }
    if (line == "===THRUST===")        { section = THRUST;    continue; }
    if (line == "===HYDRO===")         { section = HYDRO;     continue; }
    if (line == "===SENSORS===")       { section = SENSORS;   continue; }
    /// ★ 2026-09-28（D2）：碰撞体积计算方式（缺段 ⇒ 调用方保持初值 ⇒ 向后兼容）
    if (line == "===COLLISION===")     { section = COLLISION; continue; }
    if (line == "===END===")           { section = NONE;      continue; }
    if (line.find("[keship_model") == 0) continue;

    if (section == BASE_SDF)
    {
      sdfBuf << line << "\n";
    }
    else if (section == BUOYANCY)
    {
      if (line.find("link_name=") == 0)
        _buoyCfg.linkName = line.substr(10);
      else if (line.find("fluid_density=") == 0)
        _buoyCfg.fluidDensity = std::stod(line.substr(14));
      else if (line.find("body=") == 0)
      {
        std::vector<std::string> parts;
        boost::split(parts, line.substr(5), boost::is_any_of(","));
        if (parts.size() >= 12)
        {
          BuoyancyBodyConfig b;
          b.name = parts[0];
          b.type = (parts[1] == "Box") ? PrimitiveType::Box : PrimitiveType::Cylinder;
          if (b.type == PrimitiveType::Box)
          { b.box.dx = std::stod(parts[2]); b.box.dy = std::stod(parts[3]); b.box.dz = std::stod(parts[4]); }
          else
          { b.cyl.radius = std::stod(parts[2]); b.cyl.length = std::stod(parts[3]); }
          /// 跳过 parts[5]（保存格式中的占位 0）
          b.pose[0]=std::stod(parts[6]); b.pose[1]=std::stod(parts[7]); b.pose[2]=std::stod(parts[8]);
          b.pose[3]=std::stod(parts[9]); b.pose[4]=std::stod(parts[10]); b.pose[5]=std::stod(parts[11]);
          _buoyCfg.bodies.push_back(b);
        }
      }
    }
    else if (section == THRUST)
    {
      if (line.find("link_name=") == 0)
        _thrustCfg.linkName = line.substr(10);
      else if (line.find("body=") == 0)
      {
        std::vector<std::string> parts;
        boost::split(parts, line.substr(5), boost::is_any_of(","));
        if (parts.size() >= 8)
        {
          ThrusterBodyConfig b;
          b.name = parts[0];
          b.pose[0]=std::stod(parts[1]); b.pose[1]=std::stod(parts[2]); b.pose[2]=std::stod(parts[3]);
          b.pose[3]=std::stod(parts[4]); b.pose[4]=std::stod(parts[5]); b.pose[5]=std::stod(parts[6]);
          b.maxForce = std::stod(parts[7]);
          _thrustCfg.bodies.push_back(b);
        }
      }
    }
    else if (section == HYDRO)
    {
      if (line.find("link_name=") == 0)
        _hydroCfg.linkName = line.substr(10);
      else if (line.find("am=") == 0)
      {
        auto comma = line.find(',');
        if (comma != std::string::npos)
        {
          std::string name = line.substr(3, comma - 3);
          double val = std::stod(line.substr(comma + 1));

          if (name.size() == 3 && name[0] == 'm')
          {
            int i = name[1] - '0';
            int j = name[2] - '0';
            if (i >= 0 && i < 6 && j >= 0 && j < 6)
              _hydroCfg.addedMass[i * 6 + j] = val;
          }
          else
          {
            const char *amNames[] = {"X_udot","Y_vdot","Z_wdot","K_pdot","M_qdot","N_rdot",
                                     "Y_pdot","Y_rdot","K_vdot","K_rdot","M_wdot","N_vdot"};
            int oldToNew[12][2] = {{0,0},{1,1},{2,2},{3,3},{4,4},{5,5},
                                   {1,3},{1,5},{3,1},{3,5},{2,4},{5,1}};
            for (int k = 0; k < 12; k++)
              if (name == amNames[k]) {
                _hydroCfg.addedMass[oldToNew[k][0]*6 + oldToNew[k][1]] = val;
                break;
              }
          }
        }
      }
      else if (line.find("dampt=") == 0)
      {
        std::vector<std::string> parts;
        boost::split(parts, line.substr(6), boost::is_any_of(","));
        if (parts.size() >= 3)
        {
          DampingTerm t;
          t.target = std::stoi(parts[0]);
          t.coefficient = std::stod(parts[1]);
          for (size_t k = 2; k + 1 < parts.size(); k += 2)
            t.factors.push_back({std::stoi(parts[k]), std::stoi(parts[k+1])});
          _hydroCfg.dampingTerms.push_back(t);
        }
      }
        else if (line.find("damp=") == 0)
        {
          auto comma = line.find(',');
          if (comma != std::string::npos)
          {
            std::string name = line.substr(5, comma - 5);
            double val = std::stod(line.substr(comma + 1));
            const char *dNames[] = {"Xu","Yv","Zw","Kp","Mq","Nr","Yr","Nv"};
            for (int i = 0; i < 8; i++)
              if (name == dNames[i]) { _hydroCfg.damping[i] = val; break; }
          }
        }
        else if (line.find("damping_mode=") == 0)
          _hydroCfg.dampingMode = line.substr(13);
        else if (line.find("damping_ratio=") == 0)
          _hydroCfg.dampingRatio = std::stod(line.substr(14));
      }
      else if (section == COLLISION)
    {
      /// ★ 2026-09-28（D2）：一行 `collisionModeName()`（Mesh / ConvexHull / None）
      if (_outCollisionMode && !line.empty())
        *_outCollisionMode = collisionModeFromName(line);
    }
      else if (section == SENSORS)
    {
      if (line.find("link_name=") == 0)
        _sensorCfg.linkName = line.substr(10);
      else if (line.find("instance=") == 0)
      {
        std::vector<std::string> parts;
        boost::split(parts, line.substr(9), boost::is_any_of(","));
        if (parts.size() >= 8)
        {
          SensorInstance inst;
          inst.type = sensorTypeFromName(parts[0]);
          inst.name = parts[1];
          inst.pose[0]=std::stod(parts[2]); inst.pose[1]=std::stod(parts[3]);
          inst.pose[2]=std::stod(parts[4]); inst.pose[3]=std::stod(parts[5]);
          inst.pose[4]=std::stod(parts[6]); inst.pose[5]=std::stod(parts[7]);
          /// ★ 2026-09-26「Camera 规格用户自选」：**可选**追加 3 个字段
          ///   `,<宽>,<高>,<帧率>`。老 ksm 只有 8 个字段 ⇒ 走默认档（0 = 未指定）
          ///   ⇒ **向后兼容**，老资产无需迁移。
          if (parts.size() >= 11)
          {
            inst.camWidth  = std::stoi(parts[8]);
            inst.camHeight = std::stoi(parts[9]);
            inst.camRate   = std::stod(parts[10]);
          }
          _sensorCfg.instances.push_back(inst);
        }
      }
    }
  }
  } catch (const std::exception &) {
    std::cerr << "ParseKSMFile: 文件格式错误" << std::endl;
    return false;
  }

  _baseSDF = sdfBuf.str();
  if (!_baseSDF.empty() && _baseSDF.back() == '\n')
    _baseSDF.pop_back();

  std::cout << "ParseKSMFile: 解析完成 — buoyancy bodies=" << _buoyCfg.bodies.size()
            << " thrust bodies=" << _thrustCfg.bodies.size()
            << " hydro=" << (_hydroCfg.addedMass[0] != 0 ? "yes" : "empty")
            << " sensors=" << _sensorCfg.instances.size() << std::endl;
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/// ★ 2026-09-24 新增：KSM 加载流程下沉（GUI 与无头入口共用同一份实现）
///
/// 原先这套流程写在 `MenuPluginKeship::OnBtn_LoadModel`（GUI 槽）里，
/// 导致 **ksm 加载路径无法被无头入口驱动** ⇒ 只存在于该路径的缺陷
/// （如 P0-1 M_A 丢失、P0-2 足迹缓存缺失）进不了可回归范围。
/// 下沉后：GUI 与 `keship_ctl model load-ksm` 调的是同一个函数。
///
/// ⚠ 与 KSW 加载（`MenuPluginKeship::OnBtn_LoadWorld`）的分派规则**必须一致**：
///   THRUST 非空 → WithThrust；否则 BUOYANCY 非空 → WithBuoyancy；否则 → SDFString。
///   两处若漂移，同一模型经"单模型加载"与"世界加载"会得到不同物理 —— 这正是
///   2026-09-24 审核里 P0-1 的成因模式（平行路径各自实现 ⇒ 漏补）。
bool ModelManager::LoadModelFromKsmFile(
    const std::string &_instanceName,
    const std::string &_ksmPath,
    const double _pos[6],
    const std::string &_sourceBaseName,
    const CollisionMode *_overrideCollision)
{
  if (_instanceName.empty() || _ksmPath.empty())
  {
    std::cerr << "LoadModelFromKsmFile: 实例名或路径为空" << std::endl;
    return false;
  }

  /// 0. 同名检查（★ 2026-09-24 修"假成功"）
  ///
  /// 实测：`allow_renaming=false` 且场景已有同名时，gz 的 create/blocking
  /// **返回成功但不创建实体**；随后按名字取 ID 会取到**已存在的旧实体**，
  /// 于是函数以为 spawn 成功 ⇒ 上层（含世界加载的失败计数）被误导。
  /// 故这里先显式查一次**活动场景** —— 走 gz 服务，不依赖本进程缓存，跨进程可靠。
  {
    uint32_t existingId = 0;
    if (GetModelIDByInstanceNameFromGazebo(_instanceName, existingId))
    {
      std::cerr << "LoadModelFromKsmFile: 场景已有同名模型 " << _instanceName
                << "（id=" << existingId << "），拒绝重复加载" << std::endl;
      return false;
    }
  }

  /// 1. 解析五段
  std::string baseSDF;
  BuoyancyConfig buoyCfg;
  ThrusterConfig thrustCfg;
  HydroConfig hydroCfg;
  hydroCfg.linkName = "";          /// 哨兵：仅当 HYDRO 段有 link_name 时才视为"启用了 Fossen"
  SensorConfig sensorCfg;
  CollisionMode ksmCollision = CollisionMode::Mesh;   ///< 缺段 ⇒ 默认（向后兼容）
  if (!ParseKSMFile(_ksmPath, baseSDF, buoyCfg, thrustCfg, hydroCfg, sensorCfg,
                    &ksmCollision))
  {
    std::cerr << "LoadModelFromKsmFile: 解析失败 " << _ksmPath << std::endl;
    return false;
  }

  /// 2. 模型名统一为实例名（spawn 依赖名字一致以取回 entity ID）
  if (!ReplaceFirstModelName(baseSDF, _instanceName))
    std::cerr << "LoadModelFromKsmFile: 警告 — base SDF 中未找到 model name，"
                 "实例名与文件内名可能不一致（spawn 取 ID 会失败）" << std::endl;

  /// 3. 缓存配置（与 GUI 原实现逐条一致）
  CacheModelSDF(_instanceName, baseSDF);

  /// ★ 2026-09-28（D2）：碰撞体积计算方式的**生效口径（只此一处）**
  ///   · 调用方显式指定（`_overrideCollision` 非空）⇒ 用它（用户刚选的就该赢）；
  ///   · 否则用 ksm 里持久化的（缺段 ⇒ Mesh）。
  ///   写进进程缓存后，**编辑对话框就能显示事实**（此前恒显示缺省的「结构网格」）。
  SetCollisionMode(_instanceName,
                   _overrideCollision ? *_overrideCollision : ksmCollision);
  buoyCfg.linkName = "base_link";
  thrustCfg.linkName = "base_link";
  CacheBuoyancyConfig(_instanceName, buoyCfg);
  CacheThrusterConfig(_instanceName, thrustCfg);
  if (!hydroCfg.linkName.empty())
    CacheHydroConfig(_instanceName, hydroCfg);
  if (!sensorCfg.instances.empty())
    CacheSensorConfig(_instanceName, sensorCfg);

  /// 4. 按段分派 spawn（规则与 KSW 加载一致，见函数头注释）
  bool ok = false;
  const char *route = "";
  if (!thrustCfg.bodies.empty())
  {
    route = "SpawnModelWithThrust";
    ok = SpawnModelWithThrust(_instanceName, thrustCfg, _pos);
  }
  else if (!buoyCfg.bodies.empty())
  {
    route = "SpawnModelWithBuoyancy";
    ok = SpawnModelWithBuoyancy(_instanceName, buoyCfg, _pos);
  }
  else
  {
    route = "SpawnModelInGazeboFromSDFString";
    ok = SpawnModelInGazeboFromSDFString(_instanceName, baseSDF);
  }

  if (!ok)
  {
    std::cerr << "LoadModelFromKsmFile: spawn 失败（路径 " << route << "）: "
              << _instanceName << std::endl;
    return false;
  }

  /// 5. 登记来源元数据（保存 ksw 时按原名还原）
  {
    WorldModelSource src;
    src.baseName = _sourceBaseName.empty() ? _instanceName : _sourceBaseName;
    SetModelSource(_instanceName, src);
  }

  std::cout << "LoadModelFromKsmFile: 完成 " << _instanceName
            << " <- " << _ksmPath << "（路径 " << route
            << "；浮力体 " << buoyCfg.bodies.size()
            << " / 推进器 " << thrustCfg.bodies.size()
            << " / 水动力 " << (hydroCfg.linkName.empty() ? "无" : "有")
            << " / 传感器 " << sensorCfg.instances.size() << "）" << std::endl;
  return true;
}
