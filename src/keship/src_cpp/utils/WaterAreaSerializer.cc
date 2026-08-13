/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水域序列化器实现 — JSON 格式 */

#include "WaterAreaSerializer.hh"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

namespace keship
{

// ─── 序列化 ───────────────────────────────────────────────

static void WriteJsonString(std::ostream &_os, const std::string &_s)
{
  _os << '"';
  for (char c : _s)
  {
    if (c == '"')  _os << "\\\"";
    else if (c == '\\') _os << "\\\\";
    else _os << c;
  }
  _os << '"';
}

std::string SerializeWaterAreas(const std::vector<WaterArea> &_areas)
{
  std::ostringstream os;
  os << "{\"version\":2,\"areas\":[";
  for (size_t ai = 0; ai < _areas.size(); ++ai)
  {
    if (ai > 0) os << ",";
    const auto &a = _areas[ai];
    os << "{";
    WriteJsonString(os, "name");       os << ":"; WriteJsonString(os, a.name);
    os << ",\"xMin\":" << a.xMin;
    os << ",\"xMax\":" << a.xMax;
    os << ",\"yMin\":" << a.yMin;
    os << ",\"yMax\":" << a.yMax;
    os << ",\"baseHeight\":" << a.baseHeight;

    // vertices
    os << ",\"vertices\":[";
    for (size_t vi = 0; vi < a.vertices.size(); ++vi)
    {
      if (vi > 0) os << ",";
      os << "[" << a.vertices[vi].first << "," << a.vertices[vi].second << "]";
    }
    os << "]";

    // waveParams
    os << ",\"waveModel\":";     WriteJsonString(os, a.waveParams.model);
    os << ",\"wavePeriod\":"     << a.waveParams.period;
    os << ",\"waveNumber\":"     << a.waveParams.number;
    os << ",\"waveScale\":"      << a.waveParams.scale;
    os << ",\"waveGain\":"       << a.waveParams.gain;
    os << ",\"waveDirection\":"  << a.waveParams.direction;
    os << ",\"waveAngle\":"      << a.waveParams.angle;
    os << ",\"waveSteepness\":"  << a.waveParams.steepness;
    os << ",\"waveAmplitude\":"  << a.waveParams.amplitude;
    os << ",\"waveTau\":"        << a.waveParams.tau;

    os << ",\"windSpeed\":"      << a.windParams.speed;
    os << ",\"windDirection\":"  << a.windParams.direction;
    os << ",\"currentSpeed\":"   << a.currentParams.speed;
    os << ",\"currentDirection\":" << a.currentParams.direction;
    os << ",\"waveDirectionAuto\":\"" << (a.waveDirectionAuto ? "true" : "false") << "\"";
    os << "}";
  }
  os << "]}";
  return os.str();
}

// ─── 反序列化 — 简易 JSON 解析器 ──────────────────────────

namespace {

class JsonParser
{
public:
  explicit JsonParser(const std::string &_json) : data_(_json), pos_(0) {}

  std::vector<WaterArea> ParseDocument()
  {
    skipWhitespace();
    // 顶层数组 = 旧版无版本格式，不兼容：丢弃
    if (peek() == '[')
    {
      std::cerr << "DeserializeWaterAreas: 旧版水域配置文件（无版本号）已忽略" << std::endl;
      return {};
    }
    expect('{');
    std::vector<WaterArea> areas;
    while (peek() != '}')
    {
      std::string key = parseString();
      expect(':');
      if (key == "version")           parseNumber();
      else if (key == "areas")        areas = ParseAreasValue();
      else
      {
        // unknown key — skip value
        if (peek() == '"') parseString();
        else if (peek() == '[') { int depth = 0; while (pos_ < data_.size()) { char c = advance(); if (c == '[') depth++; if (c == ']') { depth--; if (depth == 0) break; } } }
        else parseNumber();
      }
      tryConsume(',');
    }
    expect('}');
    return areas;
  }

  std::vector<WaterArea> ParseAreasValue()
  {
    expect('[');
    std::vector<WaterArea> areas;
    if (peek() != ']')
    {
      do {
        areas.push_back(ParseArea());
      } while (tryConsume(','));
    }
    expect(']');
    return areas;
  }

private:
  const std::string &data_;
  size_t pos_;

  char peek() const { return pos_ < data_.size() ? data_[pos_] : '\0'; }
  char advance() { return pos_ < data_.size() ? data_[pos_++] : '\0'; }

  void skipWhitespace()
  {
    while (pos_ < data_.size() && (data_[pos_] == ' ' || data_[pos_] == '\t'
        || data_[pos_] == '\n' || data_[pos_] == '\r'))
      ++pos_;
  }

  void expect(char _c)
  {
    skipWhitespace();
    if (advance() != _c)
      throw std::runtime_error(std::string("JSON parse error: expected '") + _c + "'");
  }

  bool tryConsume(char _c)
  {
    skipWhitespace();
    if (peek() == _c) { advance(); return true; }
    return false;
  }

  std::string parseString()
  {
    skipWhitespace();
    expect('"');
    std::string s;
    while (peek() != '"' && peek() != '\0')
    {
      if (peek() == '\\') { advance(); advance(); }
      else s += advance();
    }
    expect('"');
    return s;
  }

  double parseNumber()
  {
    skipWhitespace();
    size_t start = pos_;
    if (peek() == '-') advance();
    while ((peek() >= '0' && peek() <= '9') || peek() == '.'
           || peek() == 'e' || peek() == 'E' || peek() == '+' || peek() == '-')
    {
      if (peek() == '+' || peek() == '-')
      {
        if (pos_ > start && data_[pos_-1] != 'e' && data_[pos_-1] != 'E')
          break;
      }
      advance();
    }
    return std::strtod(data_.c_str() + start, nullptr);
  }

  std::vector<std::pair<double, double>> parseVertices()
  {
    expect('[');
    std::vector<std::pair<double, double>> verts;
    if (peek() != ']')
    {
      do {
        expect('[');
        double x = parseNumber();
        expect(',');
        double y = parseNumber();
        expect(']');
        verts.emplace_back(x, y);
      } while (tryConsume(','));
    }
    expect(']');
    return verts;
  }

  WaterArea ParseArea()
  {
    WaterArea a;
    skipWhitespace();
    expect('{');
    while (peek() != '}')
    {
      std::string key = parseString();
      expect(':');
      if (key == "name")           a.name = parseString();
      else if (key == "xMin")      a.xMin = parseNumber();
      else if (key == "xMax")      a.xMax = parseNumber();
      else if (key == "yMin")      a.yMin = parseNumber();
      else if (key == "yMax")      a.yMax = parseNumber();
      else if (key == "baseHeight") a.baseHeight = parseNumber();
      else if (key == "vertices")   a.vertices = parseVertices();
      else if (key == "waveModel")      a.waveParams.model = parseString();
      else if (key == "wavePeriod")     a.waveParams.period = parseNumber();
      else if (key == "waveNumber")     a.waveParams.number = static_cast<int>(parseNumber());
      else if (key == "waveScale")      a.waveParams.scale = parseNumber();
      else if (key == "waveGain")       a.waveParams.gain = parseNumber();
      else if (key == "waveDirection")  a.waveParams.direction = parseNumber();
      else if (key == "waveAngle")      a.waveParams.angle = parseNumber();
      else if (key == "waveSteepness")  a.waveParams.steepness = parseNumber();
      else if (key == "waveAmplitude")  a.waveParams.amplitude = parseNumber();
      else if (key == "waveTau")        a.waveParams.tau = parseNumber();
      else if (key == "windSpeed")      a.windParams.speed = parseNumber();
      else if (key == "windDirection")  a.windParams.direction = parseNumber();
      else if (key == "currentSpeed")   a.currentParams.speed = parseNumber();
      else if (key == "currentDirection") a.currentParams.direction = parseNumber();
      else if (key == "waveDirectionAuto") a.waveDirectionAuto = (parseString() == "true");
      else { /* unknown key — skip value */
        if (peek() == '"') parseString();
        else if (peek() == '[') { /* skip array */ int depth=0; while(pos_<data_.size()){char c=advance();if(c=='[')depth++;if(c==']'){depth--;if(depth==0)break;}} }
        else parseNumber();
      }
      tryConsume(',');
    }
    expect('}');
    return a;
  }
};

} // anonymous namespace

std::vector<WaterArea> DeserializeWaterAreas(const std::string &_json)
{
  if (_json.empty()) return {};
  try
  {
    JsonParser parser(_json);
    return parser.ParseDocument();
  }
  catch (const std::exception &e)
  {
    std::cerr << "DeserializeWaterAreas: " << e.what() << std::endl;
    return {};
  }
}

} // namespace keship
