/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/// keship_ctl — KeShip 无头指令入口（2026-09-19 新增）
///
/// ── 为什么要它 ────────────────────────────────────────────────
/// GUI 本体是 gz GUI 插件 `MenuPluginKeship`（挂在 world SDF 的 GUI 段），
/// 所以 `headless:=true`（`gz sim -s`）时它**整个不加载** —— 无头模式缺的不是
/// 渲染，而是"操作逻辑的宿主"。但所有操作最终都落到 gz-transport 服务/话题上，
/// 因此可以把同一批操作暴露成命令行，让仿真在没有 GUI 时也能被完整驱动。
///
/// ── 三条设计要点（均经代码核实，见 doc/plans/headless_ctl_plan.md）────
///  1) **不复制任何业务逻辑**：直接链接 `keship_utils` 库、复用同一个 `ModelManager`
///     （模型 SDF 组装 / 数学船型 / 水域 / KSM·KSW）。`ModelManager` 零 Qt 依赖，
///     只需一个 `gz::transport::Node` 即可构造（ModelManager.cc:581-589）。
///  2) **能驱动任意正在运行的 keship 仿真**：gz 服务与"有没有 GUI"无关，
///     故既能驱动 `gz sim -s`（无头），也能驱动带 GUI 的正常启动实例 ——
///     张老师点按钮、指令侧发命令，操作的是同一个实例。
///  3) **无状态优先**：查询一律走活动场景（`/scene/info`、`/pose/info`），
///     不依赖本进程的 `modelList` 缓存；依赖缓存的操作（Rebuild / 存 KSM）
///     只对本进程自己 spawn 的模型可用，否则**明确报错**，绝不静默给出错误结果。
///
/// ── 用法 ────────────────────────────────────────────────────
///   source /opt/ros/jazzy/setup.bash && source ~/keship_ws/install/setup.bash
///   keship_ctl [--world keship_world] [--json] <命令> [参数...]
///   keship_ctl help
///
/// 运行前必须 source install/setup.bash —— 否则找不到模型 SDF 里的插件 .so
/// （`GZ_SIM_SYSTEM_PLUGIN_PATH` 由 hooks/resource_paths.dsv.in 注入）。

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/double.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/param.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/scene.pb.h>
#include <gz/msgs/world_control.pb.h>
#include <gz/msgs/world_stats.pb.h>
#include <gz/transport/Node.hh>

#include "ModelManager.hh"
#include "ShipHullGenerator.hh"
#include "WaterAreaSerializer.hh"
#include "keship/utils/KeshipPaths.hh"
#include "keship/utils/SeaState.hh"

namespace {

// ═══════════════════════════════════════════════════════════
// 全局选项
// ═══════════════════════════════════════════════════════════

struct Options {
  std::string world = "keship_world";
  bool json = false;
  bool force = false;      ///< 跳过"GUI 会话共存"守卫
};

Options g_opt;

std::string WorldTopic(const std::string &_suffix) {
  return "/world/" + g_opt.world + "/" + _suffix;
}

// ── 输出：人读 / JSON 双模 ────────────────────────────────
void Out(const std::string &_key, const std::string &_val, bool _jsonValue = false) {
  if (g_opt.json) {
    std::string v = _val;
    if (!_jsonValue) {
      // 转义成 JSON 字符串
      std::string esc;
      for (char c : v) {
        if (c == '"' || c == '\\') esc += '\\';
        if (c == '\n') { esc += "\\n"; continue; }
        esc += c;
      }
      v = "\"" + esc + "\"";
    }
    std::cout << "{\"key\":\"" << _key << "\",\"value\":" << v << "}" << std::endl;
  } else {
    std::cout << _val << std::endl;
  }
}

void Info(const std::string &_msg) {
  if (!g_opt.json) std::cout << _msg << std::endl;
}

/// 命令失败异常。
/// 不用 std::exit()：那样 `script` 批处理无法"报出第几行失败再中止" —— 进程会直接死掉。
/// 抛出后由 main（单命令）或 CmdScript（批处理，带行号）捕获。
struct CtlError : public std::runtime_error {
  int code;
  CtlError(const std::string &_msg, int _code)
      : std::runtime_error(_msg), code(_code) {}
};

[[noreturn]] void Fail(const std::string &_msg, int _code = 1) {
  throw CtlError(_msg, _code);
}

/// 统一的错误打印（供 catch 处调用）
void PrintError(const std::string &_msg) {
  if (g_opt.json) {
    std::string esc;
    for (char c : _msg) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
    std::cout << "{\"error\":\"" << esc << "\"}" << std::endl;
  } else {
    std::cerr << "[错误] " << _msg << std::endl;
  }
}

// ═══════════════════════════════════════════════════════════
// GUI 会话共存守卫（张老师 2026-09-19 要求：无头入口不得影响主程序与 GUI 操作）
// ═══════════════════════════════════════════════════════════
//
// 两类命令：
//   ① 只读 / 自包含 —— world status、model list/pose、watch、subvol、hull info、
//      model spawn/delete 单个：与 GUI 并行无碍（GUI 的树不会显示指令侧建的模型，
//      但不会错乱；`/keship/model_ready` 只被桥到 ROS，无 C++ 订阅者）。
//   ② **改全局共享状态** —— 水域 JSON、`model delete-all`：物理侧靠 mtime 热更新会跟上，
//      但 **GUI 的内存水域列表与 3D 水面不会更新** → 界面与实际脱节；
//      delete-all 更会删掉 GUI 正在管理的模型。
// 第 ② 类在检测到 GUI 会话时**默认拒绝**，须显式 `--force`。

/// 纯函数：一条 `gz sim` 命令行是否"带 GUI"（本项目世界，且**不含独立 token `-s`**）。
/// 独立出来是为了能做**注入回归** —— 否则"检测到 GUI 就拒绝"这条分支
/// 只能靠真开一个 GUI 来验证，而那会把窗口弹到使用者屏幕上。
bool CmdlineIsGuiGzSim(const std::string &_cmd) {
  if (_cmd.find("gz sim") == std::string::npos) return false;
  if (_cmd.find("keship_world") == std::string::npos) return false;   // 只看本项目
  std::istringstream ss(_cmd);
  std::string tok;
  while (ss >> tok)
    if (tok == "-s") return false;      // 服务端-only → 无 GUI
  return true;
}

/// 启发式：是否存在"带 GUI"的 gz sim
bool GuiSessionLikely(std::string &_detail) {
  /// 测试钩子：KESHIP_CTL_FAKE_GUI=1 时假装有 GUI。
  /// 方向安全 —— 它只会让守卫**更严**（多拒绝几条），不会放松；`--force` 仍可覆盖。
  if (std::getenv("KESHIP_CTL_FAKE_GUI") != nullptr) {
    _detail = "(由 KESHIP_CTL_FAKE_GUI 注入，仅用于守卫回归测试)";
    return true;
  }
  DIR *d = opendir("/proc");
  if (d == nullptr) return false;
  bool found = false;
  while (dirent *e = readdir(d)) {
    if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
    std::ifstream f(std::string("/proc/") + e->d_name + "/cmdline", std::ios::binary);
    if (!f) continue;
    std::string cmd((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    for (auto &c : cmd) if (c == '\0') c = ' ';
    if (CmdlineIsGuiGzSim(cmd)) { found = true; _detail = cmd; break; }
  }
  closedir(d);
  return found;
}

/// 改全局共享状态前的守卫
void GuardSharedState(const char *_what, const char *_why) {
  if (g_opt.force) return;
  std::string detail;
  if (!GuiSessionLikely(detail)) return;
  Fail(std::string("检测到带 GUI 的仿真正在运行，拒绝执行「") + _what + "」。\n"
       "        原因：" + _why + "\n"
       "        GUI 进程：" + detail + "\n"
       "        → 请先关闭 GUI 再执行；确需并行请加 --force（GUI 界面会与实际脱节）");
}

// ═══════════════════════════════════════════════════════════
// 小工具
// ═══════════════════════════════════════════════════════════

/// 从 pose_v 里按名字取一条位姿；找不到返回 false
bool FindPose(const gz::msgs::Pose_V &_msg, const std::string &_name,
              gz::msgs::Pose &_out) {
  for (int i = 0; i < _msg.pose_size(); ++i) {
    const auto &p = _msg.pose(i);
    std::string n = p.name();
    // pose/info 的名字可能是 "model::link" 形式，取前段匹配
    auto pos = n.find("::");
    if (pos != std::string::npos) n = n.substr(0, pos);
    if (n == _name) { _out = p; return true; }
  }
  return false;
}

/// 等待一帧 pose/info（最多 _timeoutMs），返回收到的消息
///
/// ★ 2026-09-19 修（悬垂引用崩溃）：回调**必须**按值捕获一个 `shared_ptr` 状态，
/// 不能 `[&]` 捕获局部变量。gz-transport 可能在订阅对象析构**之后**才派发
/// 已在队列里的消息；此时按引用捕获的 mutex / condition_variable / bool 已被销毁
/// → 悬垂引用 → `terminate called after throwing an instance of 'std::system_error'`
/// 或 glibc robust-mutex 断言 → SIGABRT。
/// 实测：`keship_ctl script`（内含 `model list` 后某行失败）**10 次崩 8 次**（rc=134），
/// 导致验收判据「script 某行失败 → 中止并报行号」形同虚设。
/// 用 shared_ptr 按值捕获后：只要 gz 还持有该 callable 的副本，状态就一直存活。
struct PoseWaitState {
  std::mutex mutex;
  std::condition_variable cv;
  bool got = false;
  gz::msgs::Pose_V msg;
};

bool WaitPoseInfo(gz::transport::Node &_node, const std::string &_topic,
                  gz::msgs::Pose_V &_out, int _timeoutMs = 3000) {
  auto st = std::make_shared<PoseWaitState>();
  auto sub = _node.Subscribe<gz::msgs::Pose_V>(_topic,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->got) { st->msg = _msg; st->got = true; }
        st->cv.notify_all();
      });
  std::unique_lock<std::mutex> lk(st->mutex);
  const bool ok = st->cv.wait_for(
      lk, std::chrono::milliseconds(_timeoutMs), [&] { return st->got; });
  if (ok) _out = st->msg;      ///< 在锁内拷贝到调用方变量，避免与迟到回调竞争
  return ok;
}

/// 探活：能否问到 /world/<w>/scene/info
bool ProbeWorld(gz::transport::Node &_node, gz::msgs::Scene &_scene) {
  gz::msgs::Empty req;
  bool result = false;
  bool ok = _node.Request(WorldTopic("scene/info"), req, 3000, _scene, result);
  return ok && result;
}

/// 检查仿真在跑；不在就给出可操作的提示后退出
void RequireWorld(gz::transport::Node &_node) {
  gz::msgs::Scene scene;
  if (!ProbeWorld(_node, scene)) {
    Fail("连不上 " + WorldTopic("scene/info") +
         " —— 仿真没在运行？\n"
         "        先起仿真：  start_keship.sh --headless   （或正常带 GUI 启动）\n"
         "        并确保已 source /opt/ros/jazzy/setup.bash 与 ~/keship_ws/install/setup.bash");
  }
}

std::string PoseStr(const gz::msgs::Pose &_p) {
  std::ostringstream s;
  s << std::fixed << std::setprecision(5)
    << _p.position().x() << " " << _p.position().y() << " " << _p.position().z()
    << "  " << _p.orientation().x() << " " << _p.orientation().y() << " "
    << _p.orientation().z() << " " << _p.orientation().w();
  return s.str();
}

/// 四元数 → (roll,pitch,yaw)
void QuatToRpy(const gz::msgs::Quaternion &_q, double &_r, double &_p, double &_y) {
  double x = _q.x(), y = _q.y(), z = _q.z(), w = _q.w();
  double sinr = 2.0 * (w * x + y * z);
  double cosr = 1.0 - 2.0 * (x * x + y * y);
  _r = std::atan2(sinr, cosr);
  double sinp = 2.0 * (w * y - z * x);
  sinp = std::max(-1.0, std::min(1.0, sinp));
  _p = std::asin(sinp);
  double siny = 2.0 * (w * z + x * y);
  double cosy = 1.0 - 2.0 * (y * y + z * z);
  _y = std::atan2(siny, cosy);
}

// ── 极简参数解析：--key value / --flag / 位置参数 ─────────
struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opt;
  std::set<std::string> flag;

  bool Has(const std::string &_k) const {
    return opt.count(_k) || flag.count(_k);
  }
  std::string Get(const std::string &_k, const std::string &_def = "") const {
    auto it = opt.find(_k);
    return it == opt.end() ? _def : it->second;
  }
  double GetD(const std::string &_k, double _def) const {
    auto it = opt.find(_k);
    if (it == opt.end()) return _def;
    try { return std::stod(it->second); } catch (...) { return _def; }
  }
};

Args ParseArgs(const std::vector<std::string> &_tokens) {
  Args a;
  for (size_t i = 0; i < _tokens.size(); ++i) {
    const std::string &t = _tokens[i];
    if (t.size() > 2 && t[0] == '-' && t[1] == '-') {
      std::string k = t.substr(2);
      // 带 = 的写法
      auto eq = k.find('=');
      if (eq != std::string::npos) {
        a.opt[k.substr(0, eq)] = k.substr(eq + 1);
        continue;
      }
      // 下一 token 不是 --开头 → 当作值
      if (i + 1 < _tokens.size() && _tokens[i + 1].rfind("--", 0) != 0) {
        std::string val = _tokens[++i];
        /// `--pos` 允许两种写法：`--pos "0 0 0.5"`（单 token）或 `--pos 0 0 0.5`（多 token）。
        /// 2026-09-19 修：原先只吃紧邻的**一个** token，于是 `--pos 0 0 0.5` 静默退化成默认位姿
        /// —— 生成位置不对却报成功，属"没做到伪装成做到了"。
        if (k == "pos") {
          auto countNum = [](const std::string &s) {
            std::istringstream ss(s); std::string tmp; int n = 0;
            while (ss >> tmp) ++n;
            return n;
          };
          auto isNum = [](const std::string &s) {
            if (s.empty()) return false;
            char *end = nullptr;
            std::strtod(s.c_str(), &end);
            return end != nullptr && *end == '\0' && end != s.c_str();
          };
          while (i + 1 < _tokens.size() && _tokens[i + 1].rfind("--", 0) != 0) {
            if (countNum(val) >= 6) break;
            if (!isNum(_tokens[i + 1])) break;
            val += " " + _tokens[++i];
          }
        }
        a.opt[k] = val;
      } else {
        a.flag.insert(k);
      }
    } else {
      a.pos.push_back(t);
    }
  }
  return a;
}

/// 位姿：默认 (0,0,D/2+0.1) —— 与 DisplacementVerifyDialog::doSpawnHull 同口径
void PoseFromArgs(const Args &_a, double _D, double _out[6]) {
  for (int i = 0; i < 6; ++i) _out[i] = 0.0;
  _out[2] = _D / 2.0 + 0.1;
  if (_a.Has("pos")) {
    std::istringstream ss(_a.Get("pos"));
    for (int i = 0; i < 6; ++i) if (!(ss >> _out[i])) break;
  }
}

// ═══════════════════════════════════════════════════════════
// 命令实现
// ═══════════════════════════════════════════════════════════

// ── world ────────────────────────────────────────────────
int CmdWorldStatus(keship::ModelManager &_mm) {
  gz::transport::Node &node = _mm.GetNode();
  gz::msgs::Scene scene;
  if (!ProbeWorld(node, scene)) Fail("连不上 " + WorldTopic("scene/info") + " —— 仿真没在运行？");

  // 取一帧 stats（含 paused / simTime）
  /// ★ 同 WaitPoseInfo：回调按值捕获 shared_ptr 状态（防析构后迟到回调悬垂引用）
  struct StatsWaitState {
    std::mutex mutex;
    std::condition_variable cv;
    bool got = false;
    gz::msgs::WorldStatistics st;
  };
  auto ws = std::make_shared<StatsWaitState>();
  auto sub = node.Subscribe<gz::msgs::WorldStatistics>(WorldTopic("stats"),
      [ws](const gz::msgs::WorldStatistics &_s) {
        std::lock_guard<std::mutex> lk(ws->mutex);
        if (!ws->got) { ws->st = _s; ws->got = true; }
        ws->cv.notify_all();
      });
  bool got = false;
  gz::msgs::WorldStatistics st;
  {
    std::unique_lock<std::mutex> lk(ws->mutex);
    ws->cv.wait_for(lk, std::chrono::milliseconds(2000), [&] { return ws->got; });
    got = ws->got;
    if (got) st = ws->st;      ///< 锁内取副本，避免与迟到回调竞争
  }

  if (g_opt.json) {
    std::cout << "{\"world\":\"" << g_opt.world << "\","
              << "\"models\":" << scene.model_size() << ","
              << "\"paused\":" << (got ? (st.paused() ? "true" : "false") : "null") << ","
              << "\"sim_time\":" << (got ? std::to_string(st.sim_time().sec()) : "null")
              << "}" << std::endl;
  } else {
    std::cout << "world          : " << g_opt.world << std::endl;
    std::cout << "models         : " << scene.model_size() << std::endl;
    if (got) {
      std::cout << "paused         : " << (st.paused() ? "true" : "false") << std::endl;
      std::cout << "sim_time       : " << st.sim_time().sec() << "."
                << std::setw(9) << std::setfill('0') << st.sim_time().nsec()
                << std::setfill(' ') << " s" << std::endl;
      std::cout << "real_time_factor: " << std::fixed << std::setprecision(3)
                << st.real_time_factor() << std::endl;
    } else {
      std::cout << "paused         : (未收到 " << WorldTopic("stats") << "，无法判定)" << std::endl;
    }
  }
  return 0;
}

int CmdWorldPause(keship::ModelManager &_mm, bool _pause) {
  gz::msgs::WorldControl req; req.set_pause(_pause);
  gz::msgs::Boolean rep; bool result = false;
  bool ok = _mm.GetNode().Request(WorldTopic("control"), req, 1000, rep, result);
  if (!ok || !result || !rep.data()) Fail(std::string("暂停/恢复请求失败（") + (_pause ? "pause" : "resume") + "）");
  Info(_pause ? "已暂停" : "已恢复");
  if (g_opt.json) std::cout << "{\"paused\":" << (_pause ? "true" : "false") << "}" << std::endl;
  return 0;
}

// ── model ────────────────────────────────────────────────
int CmdModelList(keship::ModelManager &_mm) {
  gz::msgs::Pose_V poses;
  if (!WaitPoseInfo(_mm.GetNode(), WorldTopic("pose/info"), poses)) {
    Fail("未收到 " + WorldTopic("pose/info") + " —— 仿真在跑吗？");
  }
  // 只保留模型级条目（名字不含 "::"），并去重
  std::map<std::string, gz::msgs::Pose> models;
  for (int i = 0; i < poses.pose_size(); ++i) {
    const auto &p = poses.pose(i);
    if (p.name().find("::") != std::string::npos) continue;
    models[p.name()] = p;
  }
  if (g_opt.json) {
    std::cout << "{\"count\":" << models.size() << ",\"models\":[";
    bool first = true;
    for (auto &kv : models) {
      if (!first) std::cout << ",";
      first = false;
      double r, pi, y; QuatToRpy(kv.second.orientation(), r, pi, y);
      std::cout << "{\"name\":\"" << kv.first << "\",\"id\":" << kv.second.id()
                << ",\"pos\":[" << kv.second.position().x() << "," << kv.second.position().y()
                << "," << kv.second.position().z() << "],\"rpy\":[" << r << "," << pi << "," << y << "]}";
    }
    std::cout << "]}" << std::endl;
  } else {
    std::cout << std::left << std::setw(4) << "id" << std::setw(30) << "name"
              << "pos(x y z) / rpy(rad)" << std::endl;
    for (auto &kv : models) {
      double r, pi, y; QuatToRpy(kv.second.orientation(), r, pi, y);
      std::cout << std::left << std::setw(4) << kv.second.id()
                << std::setw(30) << kv.first
                << std::fixed << std::setprecision(4)
                << kv.second.position().x() << " " << kv.second.position().y() << " "
                << kv.second.position().z() << "  |  "
                << r << " " << pi << " " << y << std::endl;
    }
    std::cout << "共 " << models.size() << " 个模型" << std::endl;
  }
  return 0;
}

keship::ShipHullSpec SpecFromArgs(const Args &_a) {
  std::string t = _a.Get("type", "wigley-classic");
  double L = _a.GetD("L", 6.0), B = _a.GetD("B", 1.5), D = _a.GetD("D", 1.0);
  double rho = _a.GetD("rho", 500.0);
  if (t == "wigley-classic") return keship::ShipHullSpec::WigleyClassic(L, B, D, rho);
  if (t == "wigley-flat")    return keship::ShipHullSpec::WigleyFlat(L, B, D, rho);
  if (t == "wigley-sharp")   return keship::ShipHullSpec::WigleySharp(L, B, D, rho);
  if (t == "wigley-fine")    return keship::ShipHullSpec::WigleyFine(L, B, D, rho);
  if (t == "lewis-u")        return keship::ShipHullSpec::LewisU(L, B, D, rho);
  if (t == "lewis-v")        return keship::ShipHullSpec::LewisV(L, B, D, rho);
  Fail("未知 --type '" + t + "'（可选：wigley-classic|wigley-flat|wigley-sharp|wigley-fine|lewis-u|lewis-v）");
  return keship::ShipHullSpec();
}

int CmdModelSpawnHull(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty())
    Fail("用法：model spawn-hull <name> [--type ...] [--L --B --D --rho] [--pos x y z r p y]\n"
         "        可选插件参数：--hydro --publish-subvol --damping <Zw,Kp,Mq>");
  const std::string name = _a.pos[0];
  keship::ShipHullSpec spec = SpecFromArgs(_a);
  double pos[6]; PoseFromArgs(_a, spec.D, pos);

  keship::MathHullPluginOpts opts;
  opts.enableHydro = _a.Has("hydro");
  opts.publishSubVolume = _a.Has("publish-subvol");
  if (_a.Has("damping")) {
    std::istringstream ss(_a.Get("damping"));
    std::string tok;
    int i = 0;
    while (i < 3 && std::getline(ss, tok, ',')) {
      try { opts.damping[i++] = std::stod(tok); }
      catch (...) { Fail("--damping 需为逗号分隔的三个数字：Zw,Kp,Mq"); }
    }
    if (i != 3) Fail("--damping 需要 3 个逗号分隔的数字：Zw,Kp,Mq");
  }
  /// 守卫：插件 KSPlugin_HydroBuoyancy.cc:427 是 `if (inWater && enableHydro_)` ——
  /// enable_hydro=false 时阻尼整段跳过。不给这条守卫就会「写了阻尼却毫无效果」，
  /// 属于把"没做到"伪装成"做到了"。
  if (opts.hasDamping() && !opts.enableHydro)
    Fail("--damping 必须同时给 --hydro：插件在 enable_hydro=false 时不走 fossen 阻尼分支"
         "（KSPlugin_HydroBuoyancy.cc:427）");

  if (!_mm.SpawnMathHull(name, spec, pos, opts)) Fail("SpawnMathHull 失败：" + name);
  std::ostringstream ps;
  ps << std::fixed << std::setprecision(4)
     << pos[0] << " " << pos[1] << " " << pos[2] << " " << pos[3] << " " << pos[4] << " " << pos[5];
  Info("已生成数学船型 " + name + "  type=" + _a.Get("type", "wigley-classic") +
       "  L/B/D=" + std::to_string(spec.L) + "/" + std::to_string(spec.B) + "/" +
       std::to_string(spec.D) + "  rho=" + std::to_string(spec.rho) +
       "  pos=" + ps.str() +
       "  hydro=" + (opts.enableHydro ? "on" : "off") +
       "  publishSubVol=" + (opts.publishSubVolume ? "on" : "off") +
       (opts.hasDamping() ? ("  damping=" + _a.Get("damping")) : ""));
  return 0;
}

/// 通用逃生口：直接喂一个 SDF 文件（实验模型可由外部脚本生成 / 由对话框导出后复用）
int CmdModelSpawnSdf(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.size() < 2)
    Fail("用法：model spawn-sdf <name> <file.sdf>   （SDF 内容原样交给 /world/<w>/create/blocking）");
  const std::string name = _a.pos[0], path = _a.pos[1];
  std::ifstream in(path, std::ios::binary);
  if (!in) Fail("打不开 SDF 文件：" + path);
  std::ostringstream buf; buf << in.rdbuf();
  const std::string sdf = buf.str();
  if (sdf.find("<sdf") == std::string::npos) Fail("看起来不是 SDF：" + path);
  if (!_mm.SpawnModelInGazeboFromSDFString(name, sdf))
    Fail("SpawnModelInGazeboFromSDFString 失败：" + name);
  Info("已生成模型 " + name + " <- " + path + "（" + std::to_string(sdf.size()) + " 字节）");
  return 0;
}

int CmdModelSpawnCylinder(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty()) Fail("用法：model spawn-cylinder <name> [--d --len --rho] [--pos ...]");
  const std::string name = _a.pos[0];
  double d = _a.GetD("d", 0.5), len = _a.GetD("len", 1.0), rho = _a.GetD("rho", 500.0);
  double pos[6]; PoseFromArgs(_a, len, pos);
  if (!_mm.SpawnCylinderInGazebo(name, d, len, rho, pos))
    Fail("SpawnCylinderInGazebo 失败：" + name);
  Info("已生成圆柱体 " + name);
  return 0;
}

int CmdModelSpawnFile(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.size() < 2) Fail("用法：model spawn-file <name> <path> [--static] [--buoyancy] [--pos ...]");
  const std::string name = _a.pos[0], path = _a.pos[1];
  double pos[6]; PoseFromArgs(_a, 1.0, pos);
  bool isStatic = _a.Has("static"), addBuoy = _a.Has("buoyancy");
  if (!_mm.SpawnModelInGazeboFromFile(name, path, pos, isStatic, addBuoy))
    Fail("SpawnModelInGazeboFromFile 失败：" + name + " <- " + path);
  Info("已生成模型 " + name + " <- " + path);
  return 0;
}

/// ★ 2026-09-24 新增：加载 .ksm 模型（与 GUI「加载模型」走**同一份实现**）
///
/// 为什么需要它：在它之前，ksm 加载逻辑只存在于 GUI 槽里 ⇒ 无头入口无法驱动，
/// 导致只发生在该路径上的缺陷（P0-1 M_A 丢失 / P0-2 足迹缓存缺失）**进不了
/// 可回归范围**（张老师 2026-09-24：GUI 仅供用户使用，不便测试）。
///
/// 命令自带**路径回显**：输出里会写明实际走了哪条 spawn 分支
/// （WithThrust / WithBuoyancy / SDFString）—— 这本身就是判据，
/// 因为不同分支的横向关注点覆盖不同（见 doc/audit/model_loading_audit_2026-09-24.md）。
int CmdModelLoadKsm(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.size() < 2)
    Fail("用法：model load-ksm <name> <file.ksm> [--pos \"x y z r p y\"]\n"
         "        与 GUI「加载模型」选 .ksm 走**同一份实现**（ModelManager::LoadModelFromKsmFile）：\n"
         "        解析五段 → 缓存 SDF/浮力/推力/水动力/传感器 → 按有无推力·浮力分派 spawn。\n"
         "        默认位姿 z=0.1（可用 --pos 覆盖）。");
  const std::string name = _a.pos[0], path = _a.pos[1];
  double pos[6];
  PoseFromArgs(_a, 0.0, pos);   /// _D=0 ⇒ 默认 z = 0.1
  if (!_mm.LoadModelFromKsmFile(name, path, pos))
    Fail("LoadModelFromKsmFile 失败：" + name + " <- " + path);
  Info("已加载 ksm 模型 " + name + " <- " + path);
  return 0;
}

// ── 前向声明：水域读写辅助（定义见文件下方「水域：读写共用」一节）──
// 新增的 CmdWorldLoadKsw 在本节之前就要用它们
std::vector<keship::WaterArea> LoadWaterAreas();
void SaveWaterAreas(const std::vector<keship::WaterArea> &_areas);

/// 显示模型在 ModelManager 内的**配置与缓存状态** ——
/// 用于核对"这次加载走了哪条分派路径、缓存了哪些配置、M_A 是否非零、是否进了 2D 足迹缓存"。
/// 数据全部来自公有查询接口，不改变任何状态。
int CmdModelInfo(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty())
    Fail("用法：model info <name>");
  const std::string name = _a.pos[0];

  uint32_t id = 0;
  const bool inScene = _mm.GetModelIDByInstanceNameFromGazebo(name, id);

  keship::BuoyancyConfig bc;
  const bool hasB = _mm.GetCachedBuoyancyConfig(name, bc) && !bc.bodies.empty();
  keship::ThrusterConfig tc;
  const bool hasT = _mm.GetCachedThrusterConfig(name, tc) && !tc.bodies.empty();
  keship::HydroConfig hc;
  const bool hasH = _mm.GetCachedHydroConfig(name, hc);
  keship::SensorConfig sc;
  const bool hasS = _mm.GetCachedSensorConfig(name, sc) && !sc.instances.empty();
  bool isStatic = false;
  const bool hasSt = _mm.GetCachedModelStatic(name, isStatic);

  /// M_A 是否非零 —— 决定 `<fluid_added_mass>` 会不会被注入
  bool maNonZero = false;
  double maAbsSum = 0.0;
  if (hasH) {
    for (int i = 0; i < 36; ++i) {
      if (std::abs(hc.addedMass[i]) > 1e-12) maNonZero = true;
      maAbsSum += std::abs(hc.addedMass[i]);
    }
  }

  /// 会走哪条 spawn 分派路径（与 LoadModelFromKsmFile 的规则一致）
  const std::string route = hasT ? "SpawnModelWithThrust"
                           : hasB ? "SpawnModelWithBuoyancy"
                                  : "SpawnModelInGazeboFromSDFString";

  /// 2D 足迹缓存（注意：2D 地图现在只画静态模型）
  const bool inFootprint = _mm.GetBBoxCache().count(name) > 0;

  if (g_opt.json) {
    std::ostringstream js;
    js << "{\"name\":\"" << name << "\",\"inScene\":" << (inScene ? "true" : "false")
       << ",\"id\":" << id
       << ",\"route\":\"" << route << "\""
       << ",\"buoyancy\":" << (hasB ? "true" : "false")
       << ",\"thrust\":" << (hasT ? "true" : "false")
       << ",\"hydro\":" << (hasH ? "true" : "false")
       << ",\"addedMassNonZero\":" << (maNonZero ? "true" : "false")
       << ",\"addedMassAbsSum\":" << maAbsSum
       << ",\"sensors\":" << (hasS ? "true" : "false")
       << ",\"isStatic\":" << (isStatic ? "true" : "false")
       << ",\"inFootprintCache\":" << (inFootprint ? "true" : "false")
       << "}";
    Out("model_info", js.str(), true);
    return 0;
  }

  std::cout << "模型 " << name << std::endl;
  std::cout << "  在场景        : " << (inScene ? "是" : "否")
            << (inScene ? ("  (id=" + std::to_string(id) + ")") : "") << std::endl;
  std::cout << "  静态标记      : " << (hasSt ? (isStatic ? "true" : "false") : "(未知)") << std::endl;
  std::cout << "  分派路径      : " << route
            << "   ← 与 LoadModelFromKsmFile 的分派规则一致" << std::endl;
  std::cout << "  浮力配置      : "
            << (hasB ? ("有 (" + std::to_string(bc.bodies.size()) + " 个体)") : "无") << std::endl;
  std::cout << "  推力配置      : "
            << (hasT ? ("有 (" + std::to_string(tc.bodies.size()) + " 个)") : "无") << std::endl;
  std::cout << "  水动力配置    : " << (hasH ? "有" : "无");
  if (hasH)
    std::cout << "   M_A 非零=" << (maNonZero ? "是" : "否")
              << "   |M_A| 之和=" << maAbsSum
              << "   ⇒ <fluid_added_mass> " << (maNonZero ? "会" : "不会") << "注入";
  std::cout << std::endl;
  std::cout << "  传感器配置    : "
            << (hasS ? ("有 (" + std::to_string(sc.instances.size()) + " 个)") : "无") << std::endl;
  std::cout << "  2D 足迹缓存   : " << (inFootprint ? "有" : "无")
            << "   （2D 地图只画静态模型）" << std::endl;
  return 0;
}

/// 触发模型的插件重建 —— 覆盖 4 个 Rebuild 路径（GUI 里由编辑器改配置后触发）。
/// 配置一律**从缓存取**（与 GUI 的"编辑器改配置 → 缓存 → 重建"同源），
/// 故须先 `model load-ksm` 过该模型。
int CmdModelRebuild(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty())
    Fail("用法：model rebuild <name> [--kind all|buoyancy|thrust|hydro|sensors]\n"
         "        all = RebuildModelWithAllPlugins（默认）；其余各自对应一条 Rebuild 路径。\n"
         "        配置从缓存取 ⇒ 须先 load-ksm 过该模型。");
  const std::string name = _a.pos[0];
  const std::string kind = _a.Get("kind", "all");
  bool ok = false;

  if (kind == "all") {
    ok = _mm.RebuildModelWithAllPlugins(name);
  } else if (kind == "buoyancy") {
    keship::BuoyancyConfig c;
    if (!_mm.GetCachedBuoyancyConfig(name, c)) Fail("无缓存的浮力配置：" + name);
    ok = _mm.RebuildModelWithBuoyancy(name, c);
  } else if (kind == "thrust") {
    keship::ThrusterConfig c;
    if (!_mm.GetCachedThrusterConfig(name, c)) Fail("无缓存的推力配置：" + name);
    ok = _mm.RebuildModelWithThrust(name, c);
  } else if (kind == "hydro") {
    keship::HydroConfig c;
    if (!_mm.GetCachedHydroConfig(name, c)) Fail("无缓存的水动力配置：" + name);
    ok = _mm.RebuildModelWithHydro(name, c);
  } else if (kind == "sensors") {
    keship::SensorConfig c;
    if (!_mm.GetCachedSensorConfig(name, c)) Fail("无缓存的传感器配置：" + name);
    ok = _mm.RebuildModelWithSensors(name, c);
  } else {
    Fail("未知 --kind '" + kind + "'（可选：all|buoyancy|thrust|hydro|sensors）");
  }

  if (!ok) Fail("重建失败：" + name + "（kind=" + kind + "）");
  Info("已重建 " + name + "（kind=" + kind + "）");
  return 0;
}

/// 把当前模型保存为 .ksm（与 GUI「保存世界」用的是同一个 SaveModelToKSM）——
/// 用于"加载 → 保存 → 再加载"的往返验证。
int CmdModelSaveKsm(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.size() < 2)
    Fail("用法：model save-ksm <name> <file.ksm> [--sdf-name <模型名>]");
  const std::string name = _a.pos[0], path = _a.pos[1];
  if (!_mm.SaveModelToKSM(name, path, _a.Get("sdf-name", "")))
    Fail("保存失败：" + name + " -> " + path);
  Info("已保存 " + name + " -> " + path);
  return 0;
}

/// 合并"命名空间"与"子世界链"（与 GUI 的 mergeSegs 同逻辑：
/// 反复去掉链首与命名空间尾的重叠段，保证不出现 `w1_w1_sub` 式重复前缀）
std::vector<std::string> MergeNsAndChain(const std::vector<std::string> &_ns,
                                         const std::vector<std::string> &_path) {
  std::vector<std::string> ns = _ns;
  std::vector<std::string> rest = _path;
  while (!rest.empty() && !ns.empty() && rest.front() == ns.back())
    rest.erase(rest.begin());
  ns.insert(ns.end(), rest.begin(), rest.end());
  return ns;
}

/// 用下划线连接各段（可选再拼一个尾段）
std::string JoinSegs(const std::vector<std::string> &_segs, const std::string &_tail) {
  std::string s;
  for (const auto &seg : _segs) { if (!s.empty()) s += "_"; s += seg; }
  if (!_tail.empty()) { if (!s.empty()) s += "_"; s += _tail; }
  return s;
}

/// 加载 .ksw 世界（0.0.4）—— 与 GUI「载入世界」同源：
///   `LoadWorldManifest` 解析 → 写水域 JSON（物理侧 mtime 热更新）→
///   静态优先 → 逐个 `LoadModelFromKsmFile` → 登记完整来源元数据。
/// ⚠ 无头侧**不生成 3D 水面视觉**（那是 GUI 侧 WaterAreaVisualManager 的职责）
///   ⇒ 本命令验证的是"模型加载 + 水域数据"这一段。
int CmdWorldLoadKsw(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty())
    Fail("用法：world load-ksw <dir> [--ns <名>[/<子>]] [--offset \"x y z r p y\"] [--no-water]\n"
         "        <dir> 为 .ksw 目录（内含 keship_world.kswf）");
  const std::string dir = _a.pos[0];
  const std::string kswf = dir + "/keship_world.kswf";

  std::vector<keship::ModelManager::WorldManifestItem> items;
  std::vector<keship::ModelManager::WorldWaterItem> waters;
  std::string worldName;
  std::array<double, 6> worldPose{};
  if (!_mm.LoadWorldManifest(kswf, items, waters, &worldName, &worldPose))
    Fail("无法解析世界清单（仅支持 0.0.4）：" + kswf);
  Info("世界「" + worldName + "」：模型 " + std::to_string(items.size())
       + " 个 / 水域 " + std::to_string(waters.size()) + " 个");

  /// 命名空间（可 / 分隔多层）
  std::vector<std::string> nsSeg;
  {
    std::istringstream ss(_a.Get("ns"));
    std::string s;
    while (std::getline(ss, s, '/'))
      if (!s.empty()) nsSeg.push_back(s);
  }
  /// 六向整体偏移
  double off[6] = {0, 0, 0, 0, 0, 0};
  if (_a.Has("offset")) {
    std::istringstream ss(_a.Get("offset"));
    for (int i = 0; i < 6; ++i) if (!(ss >> off[i])) break;
  }

  /// 水域：合并进当前 JSON（与 GUI"合并进当前场景"的语义一致）；物理侧靠 mtime 热更新
  if (!_a.Has("no-water") && !waters.empty()) {
    GuardSharedState("world load-ksw（写水域）",
        "水域 JSON 被改写后物理侧会热更新，但 GUI 的内存水域列表与 3D 水面不会同步 → 界面与实际脱节");
    auto areas = LoadWaterAreas();
    int added = 0;
    for (const auto &w : waters) {
      const std::string inst = JoinSegs(MergeNsAndChain(nsSeg, w.path), w.baseName);
      bool dup = false;
      for (const auto &a : areas) if (a.name == inst) { dup = true; break; }
      if (dup) continue;
      keship::WaterArea a = w.area;
      a.name = inst;
      areas.push_back(a);
      ++added;
    }
    SaveWaterAreas(areas);
    Info("水域：新增 " + std::to_string(added) + " 个（当前共 " + std::to_string(areas.size()) + "）");
  }

  /// 模型：静态优先（与 GUI 的 stable_partition 同义）
  std::stable_partition(items.begin(), items.end(),
      [](const keship::ModelManager::WorldManifestItem &it) { return it.isStatic; });

  int ok = 0, skip = 0, fail = 0;
  std::vector<std::string> failNames;
  for (const auto &it : items) {
    const std::string inst = JoinSegs(MergeNsAndChain(nsSeg, it.path), it.baseName);
    if (_mm.GetModelList().count(inst) > 0) { ++skip; continue; }
    const double pos[6] = {
        it.pose[0] + worldPose[0] + off[0],
        it.pose[1] + worldPose[1] + off[1],
        it.pose[2] + worldPose[2] + off[2],
        it.pose[3] + worldPose[3] + off[3],
        it.pose[4] + worldPose[4] + off[4],
        it.pose[5] + worldPose[5] + off[5]};
    if (_mm.LoadModelFromKsmFile(inst, it.ksmPath, pos, it.baseName)) {
      ++ok;
      /// 完整来源元数据（与 GUI 一致，供后续保存时还原嵌套位置）
      keship::ModelManager::WorldModelSource src;
      src.kswPath = kswf;
      src.ns = nsSeg;
      src.path = MergeNsAndChain(nsSeg, it.path);
      src.baseName = it.baseName;
      _mm.SetModelSource(inst, src);
    } else {
      ++fail;
      failNames.push_back(inst);
    }
  }

  Info("模型：成功 " + std::to_string(ok) + " / 跳过重名 " + std::to_string(skip)
       + " / 失败 " + std::to_string(fail));
  if (fail > 0) {
    std::string s;
    for (const auto &n : failNames) s += " " + n;
    Fail("有 " + std::to_string(fail) + " 个模型加载失败：" + s);
  }
  return 0;
}

int CmdModelDelete(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty()) Fail("用法：model delete <name>");
  const std::string name = _a.pos[0];
  /// 先确认存在再删：gz 的 /world/<w>/remove 服务是**幂等**的，删不存在的模型
  /// 也会回 true —— 不先查就会「删了个不存在的模型却报成功」。
  uint32_t id = 0;
  if (!_mm.GetModelIDByInstanceNameFromGazebo(name, id))
    Fail("场景中没有这个模型，未删除：" + name);
  if (!_mm.DeleteModelInGazeboByName(name)) Fail("删除失败：" + name);
  Info("已删除 " + name + " (id=" + std::to_string(id) + ")");
  return 0;
}

int CmdModelDeleteAll(keship::ModelManager &_mm) {
  GuardSharedState("model delete-all",
      "会删掉 GUI 正在管理的全部模型与水域 —— GUI 的层次树/缓存不会同步，属破坏性操作");
  /// ★ 2026-09-28 新语义（张老师 2026-09-25 拍板）：
  ///   「delete-all 应当删除所有模型、水域、子世界」
  ///   · **模型**：`DeleteAllModelsInGazebo` 已改为**枚举活动场景**
  ///     （旧实现在新进程里 modelList 恒空 ⇒ 什么都不删却报成功）；
  ///   · **水域**：清空水域 JSON ⇒ 物理侧（WavesModel/HydroBuoyancy）靠 **mtime 热更新**跟上；
  ///     ⚠ 无头侧**没有 3D 水面**（`WaterAreaVisualManager` 是 GUI 侧类）⇒ 无需删 `waves_*`；
  ///     若并行驱动的是**张老师的 GUI 实例**，3D 水面需由他在界面上刷新（守卫已默认拒绝）；
  ///   · **子世界**：来源链元数据（`modelSource_`/`waterSource_`）本就是**本进程**的，
  ///     无头每次新进程 ⇒ 恒空 ⇒ 无需处理。
  if (!_mm.DeleteAllModelsInGazebo())
    Fail("DeleteAllModelsInGazebo 失败（可能部分模型没删掉，见上方 stderr）");
  SaveWaterAreas({});
  Info("已删除全部模型，并清空水域（水域 JSON 已置空，物理侧靠 mtime 热更新跟上）");
  return 0;
}

int CmdModelPose(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty()) Fail("用法：model pose <name>");
  const std::string name = _a.pos[0];
  gz::msgs::Pose_V poses;
  if (!WaitPoseInfo(_mm.GetNode(), WorldTopic("pose/info"), poses)) Fail("未收到 pose/info");
  gz::msgs::Pose p;
  if (!FindPose(poses, name, p)) Fail("场景中找不到模型：" + name);
  double r, pi, y; QuatToRpy(p.orientation(), r, pi, y);
  if (g_opt.json) {
    std::cout << "{\"name\":\"" << name << "\",\"id\":" << p.id()
              << ",\"pos\":[" << p.position().x() << "," << p.position().y() << "," << p.position().z()
              << "],\"rpy\":[" << r << "," << pi << "," << y << "]}" << std::endl;
  } else {
    std::cout << std::fixed << std::setprecision(5)
              << "xyz   : " << p.position().x() << " " << p.position().y() << " " << p.position().z() << std::endl
              << "rpy   : " << r << " " << pi << " " << y << "  (rad)" << std::endl
              << "quat  : " << PoseStr(p).substr(PoseStr(p).find("  ") + 2) << std::endl;
  }
  return 0;
}

int CmdModelSetPose(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.size() < 7) Fail("用法：model set-pose <name> x y z roll pitch yaw");
  const std::string name = _a.pos[0];
  double pose[6];
  for (int i = 0; i < 6; ++i) {
    try { pose[i] = std::stod(_a.pos[i + 1]); }
    catch (...) { Fail("位姿参数必须是数字"); }
  }
  uint32_t id = 0;
  if (!_mm.GetModelIDByInstanceNameFromGazebo(name, id)) Fail("场景中找不到模型：" + name);
  if (!_mm.SetModelPoseByIDInGazebo(id, pose)) Fail("SetModelPoseByIDInGazebo 失败：" + name);
  Info("已设置 " + name + " 位姿");
  return 0;
}

// ── hull（纯计算，不需要仿真在跑）─────────────────────────
/// 直接暴露 ShipHullGenerator 的静水力，供"用指令核对实验数字"。
/// 典型用途：SP-01 的"理论 ∇"就是 volumeAtDraft(d)，此前只能靠 GUI 里看，
/// 现在可以在无头侧独立算出来与实测对比。
int CmdHullInfo(const Args &_a) {
  keship::ShipHullSpec spec = SpecFromArgs(_a);

  /// ShipHullGenerator 构造时会往 stdout 打一行诊断 —— 会污染 --json 输出，先截住
  std::ostringstream captured;
  std::streambuf *old = std::cout.rdbuf(captured.rdbuf());
  auto hull = std::make_unique<keship::ShipHullGenerator>(spec);
  std::cout.rdbuf(old);

  const auto &hp = hull->hydroProps();
  const double draft = _a.GetD("draft", hp.draft);

  if (g_opt.json) {
    std::cout << std::setprecision(10)
              << "{\"type\":\"" << _a.Get("type", "wigley-classic") << "\""
              << ",\"L\":" << spec.L << ",\"B\":" << spec.B
              << ",\"D\":" << spec.D << ",\"rho\":" << spec.rho
              << ",\"volume_full\":" << hp.volume
              << ",\"catto_volume\":" << hull->cattoTotalVolume()
              << ",\"design_draft\":" << hp.draft
              << ",\"draft\":" << draft
              << ",\"volume_at_draft\":" << hull->volumeAtDraft(draft)
              << ",\"Aw\":" << hp.Aw << ",\"Ix\":" << hp.Ix
              << ",\"Cwp\":" << hp.Cwp << ",\"Ci\":" << hp.Ci
              << ",\"KB\":" << hp.KB << ",\"BM\":" << hp.BM << ",\"GM\":" << hp.GM
              /// ★ 缺陷 ⑩ 修复（2026-09-20）新增：**设计吃水处**的正确静水力
              /// （上面 Aw/Ix/KB/BM/GM 是历史口径：甲板水线面 + 全深度积分 + 坐标系混用；
              ///  volume 仍是"全船体积"、被 4 处 `m = rho*volume` 依赖，不能改）
              << ",\"volumeD\":" << hp.volumeD
              << ",\"KBup\":" << hp.KBup << ",\"BMd\":" << hp.BMd << ",\"GMd\":" << hp.GMd
              << ",\"AwD\":" << hp.AwD << ",\"IxD\":" << hp.IxD
              << "}" << std::endl;
  } else {
    std::cout << std::fixed << std::setprecision(6)
              << "船型            : " << _a.Get("type", "wigley-classic")
              << "  L/B/D=" << spec.L << "/" << spec.B << "/" << spec.D
              << "  rho=" << spec.rho << std::endl
              << "全船体积        : " << hp.volume << " m³"
              << "   (catto 校验 " << hull->cattoTotalVolume() << ")" << std::endl
              << "设计吃水        : " << hp.draft << " m" << std::endl
              << "查询吃水        : " << draft << " m" << std::endl
              << "volumeAtDraft(d): " << hull->volumeAtDraft(draft) << " m³" << std::endl
              << "Aw / Ix         : " << hp.Aw << " m² / " << hp.Ix << " m⁴" << std::endl
              << "Cwp / Ci        : " << hp.Cwp << " / " << hp.Ci << std::endl
              << "KB / BM / GM    : " << hp.KB << " / " << hp.BM << " / " << hp.GM << " m" << std::endl
              /// ★ 缺陷 ⑩ 修复（2026-09-20）新增：**设计吃水处**的正确静水力。
              /// 上面那三行是历史口径（甲板水线面 + 全深度积分 + 坐标系混用），
              /// 保留仅为兼容；**显示与实验测量应看下面这些**（见 plans §26）。
              << "── 设计吃水处（正确口径，缺陷 ⑩ 修复新增）──" << std::endl
              << "volumeD         : " << hp.volumeD << " m³（= 排水体积；volume 是全船体积）" << std::endl
              << "AwD / IxD       : " << hp.AwD << " m² / " << hp.IxD << " m⁴" << std::endl
              << "KBup / BMd / GMd: " << hp.KBup << " / " << hp.BMd << " / " << hp.GMd
              << " m（KBup 距龙骨）" << std::endl;
    if (!captured.str().empty())
      std::cout << "（生成器诊断：" << captured.str().substr(0, captured.str().find('\n'))
                << "）" << std::endl;
  }
  return 0;
}

// ── wave / water ─────────────────────────────────────────
/// 下发波浪/静水参数（补发若干次避开握手竞态；与 GUI PublishWaveParams 同款做法）
void PublishWaveParams(keship::ModelManager &_mm, const std::string &_model, double _gain) {
  gz::msgs::Param msg;
  auto *p = msg.mutable_params();
  gz::msgs::Any v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
  v.set_string_value(_model);
  (*p)["model"] = v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
  v.set_double_value(_gain);
  (*p)["gain"] = v;

  auto pub = _mm.GetNode().Advertise<gz::msgs::Param>("/keship/wavefield/parameters_set");
  /// 补发若干次：订阅端（KSPlugin_WavesModel）发现发布者需要时间，
  /// 只发一次会踩握手竞态（主插件 MenuPluginKeship.cc:2975 有同款 300ms 补发）
  for (int i = 0; i < 5; ++i) {
    pub.Publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
  }
}

// ── 水域：读写共用（与 GUI 同一套序列化器，不手写 JSON）──
std::vector<keship::WaterArea> LoadWaterAreas() {
  const std::string f = keship::KeshipWaterAreasFile();
  std::ifstream in(f, std::ios::binary);
  if (!in) return {};                       // 文件不存在 = 空场景（GUI 启动即写空 JSON）
  std::ostringstream buf; buf << in.rdbuf();
  return keship::DeserializeWaterAreas(buf.str());
}

void SaveWaterAreas(const std::vector<keship::WaterArea> &_areas) {
  const std::string f = keship::KeshipWaterAreasFile();
  std::ofstream out(f, std::ios::binary | std::ios::trunc);
  if (!out) Fail("写入失败：" + f);
  const std::string s = keship::SerializeWaterAreas(_areas);
  out << s;
  out.close();
  Info("已写入水域配置 " + f + "（" + std::to_string(_areas.size()) +
       " 片， " + std::to_string(s.size()) + " 字节）");
}

int CmdWaveSet(keship::ModelManager &_mm, const Args &_a) {
  const std::string model = _a.Get("model", "PMS");
  const double gain = _a.GetD("gain", 1.0);
  PublishWaveParams(_mm, model, gain);
  Info("已下发波浪参数 model=" + model + " gain=" + std::to_string(gain));
  if (g_opt.json)
    std::cout << "{\"model\":\"" << model << "\",\"gain\":" << gain << "}" << std::endl;
  return 0;
}

int CmdWaterSet(const Args &_a) {
  if (_a.pos.empty()) Fail("用法：water set <areas.json>   （经同一序列化器校验后写入 " +
                           keship::KeshipWaterAreasFile() + "，物理侧 mtime 热更新）");
  GuardSharedState("water set",
      "水域 JSON 被改写后物理侧会热更新，但 GUI 的内存水域列表与 3D 水面不会同步 → 界面与实际脱节");
  const std::string src = _a.pos[0];
  std::ifstream in(src, std::ios::binary);
  if (!in) Fail("打不开源文件：" + src);
  std::ostringstream buf; buf << in.rdbuf();
  const std::string content = buf.str();
  /// 用与 GUI 同一套反序列化器校验（比字符串包含判断严格）
  auto areas = keship::DeserializeWaterAreas(content);
  if (areas.empty() && content.find("areas") == std::string::npos)
    Fail("无法解析为 v2 水域 JSON：" + src);
  SaveWaterAreas(areas);
  return 0;
}

/// 添加「默认水域」——与 MenuPluginKeship::OnBtn_AddDefaultWaterArea 逐项一致
int CmdWaterAddDefault(keship::ModelManager &_mm) {
  GuardSharedState("water add-default",
      "水域 JSON 被改写后物理侧会热更新，但 GUI 的内存水域列表与 3D 水面不会同步 → 界面与实际脱节");
  auto areas = LoadWaterAreas();
  for (const auto &a : areas)
    if (a.name == "默认水域")
      Fail("场景已有「默认水域」，拒绝重复添加（与 GUI 的查重一致）");

  keship::WaterArea area;
  area.name = "默认水域";
  area.xMin = -250; area.xMax = 250;
  area.yMin = -250; area.yMax = 250;
  area.baseHeight = 0.0;
  area.vertices = {{-250, -250}, {250, -250}, {250, 250}, {-250, 250}};
  /// ★ 2026-09-22 张老师要求：新增水域默认 **0 级海况（无风无浪无流）** ——
  /// 教学实验一上来就是干净的静水基线，不必再手动"设静水"。
  /// 画面上的"微微起伏"由 WaveVisual 的**纯渲染细节层活跃度**负责，
  /// 不参与物理（见 doc/plans/ui_improvement_plan.md 的 S2 与判读纪律 25/26）。
  /// ParamsFromLevel(0) 给出 gain=0 / 风速=0（周期取 TpFromHs(0)=5.0 占位）。
  keship::SeaState::ParamsFromLevel(0, &area.waveParams.period,
                                    &area.windParams.speed,
                                    &area.waveParams.gain);
  area.currentParams.speed = 0.0;      ///< 流速显式置 0（无流）
  areas.push_back(area);
  SaveWaterAreas(areas);
  Info("已添加「默认水域」：±250 m 矩形，baseHeight=0，海况 0 级（无风无浪无流；周期 " +
       std::to_string(area.waveParams.period) + " s / 风速 " +
       std::to_string(area.windParams.speed) + " m/s / 流速 " +
       std::to_string(area.currentParams.speed) + " m/s / gain " +
       std::to_string(area.waveParams.gain) + "）");
  /// GUI 侧同时会 PublishWaveParams；这里保持一致
  PublishWaveParams(_mm, area.waveParams.model, area.waveParams.gain);
  return 0;
}

/// 清空水域（对齐 GUI 启动时的"空水域 JSON"）
int CmdWaterReset() {
  GuardSharedState("water reset",
      "水域 JSON 被改写后物理侧会热更新，但 GUI 的内存水域列表与 3D 水面不会同步 → 界面与实际脱节");
  SaveWaterAreas({});
  return 0;
}

int CmdWaterList() {
  auto areas = LoadWaterAreas();
  if (g_opt.json) {
    std::cout << "{\"count\":" << areas.size() << ",\"areas\":[";
    for (size_t i = 0; i < areas.size(); ++i) {
      if (i) std::cout << ",";
      std::cout << "{\"name\":\"" << areas[i].name << "\",\"bounds\":["
                << areas[i].xMin << "," << areas[i].xMax << ","
                << areas[i].yMin << "," << areas[i].yMax << "],\"baseHeight\":"
                << areas[i].baseHeight << ",\"gain\":" << areas[i].waveParams.gain << "}";
    }
    std::cout << "]}" << std::endl;
  } else {
    std::cout << "水域 " << areas.size() << " 片：" << std::endl;
    for (const auto &a : areas)
      std::cout << "  " << a.name << "  x[" << a.xMin << "," << a.xMax << "] y["
                << a.yMin << "," << a.yMax << "]  baseHeight=" << a.baseHeight
                << "  gain=" << a.waveParams.gain
                << "  顶点=" << a.vertices.size() << std::endl;
    if (areas.empty()) std::cout << "  （空场景 —— 教学实验前需先 water add-default）" << std::endl;
  }
  return 0;
}

int CmdWaterShow() {
  const std::string f = keship::KeshipWaterAreasFile();
  std::ifstream in(f, std::ios::binary);
  if (!in) Fail("读不到 " + f);
  std::ostringstream buf; buf << in.rdbuf();
  std::cout << f << ":" << std::endl << buf.str() << std::endl;
  return 0;
}

// ── 观测 ─────────────────────────────────────────────────
int CmdWatchPose(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty()) Fail("用法：watch pose <name> --duration <s> [--out <csv>]");
  const std::string name = _a.pos[0];
  const double duration = _a.GetD("duration", 10.0);
  const std::string outPath = _a.Get("out", "");

  /// ★ 同 WaitPoseInfo：回调按值捕获 shared_ptr 状态。
  /// csv 流也放进状态里 —— 否则函数返回后迟到回调仍会往已析构的 ofstream 写。
  struct WatchState {
    std::mutex mutex;
    std::atomic<bool> stop{false};
    int samples = 0;
    std::string name;
    std::chrono::steady_clock::time_point t0;
    std::ofstream csv;
  };
  auto wst = std::make_shared<WatchState>();
  wst->name = name;
  wst->t0 = std::chrono::steady_clock::now();
  if (!outPath.empty()) {
    wst->csv.open(outPath);
    if (!wst->csv) Fail("无法写 " + outPath);
    wst->csv << "t,x,y,z,roll,pitch,yaw\n";
  }

  auto sub = _mm.GetNode().Subscribe<gz::msgs::Pose_V>(WorldTopic("pose/info"),
      [wst](const gz::msgs::Pose_V &_msg) {
        if (wst->stop) return;
        gz::msgs::Pose p;
        if (!FindPose(_msg, wst->name, p)) return;
        double r, pi, y; QuatToRpy(p.orientation(), r, pi, y);
        double t = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - wst->t0).count();
        std::lock_guard<std::mutex> lk(wst->mutex);
        ++wst->samples;
        if (wst->csv) {
          wst->csv << std::fixed << std::setprecision(6) << t << ","
              << p.position().x() << "," << p.position().y() << "," << p.position().z() << ","
              << r << "," << pi << "," << y << "\n";
        }
      });

  std::this_thread::sleep_for(std::chrono::milliseconds(
      static_cast<int>(duration * 1000)));
  wst->stop = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  int samples = 0;
  { std::lock_guard<std::mutex> lk(wst->mutex); samples = wst->samples; }
  if (wst->csv.is_open()) { wst->csv.flush(); }

  if (g_opt.json) {
    std::cout << "{\"model\":\"" << name << "\",\"samples\":" << samples
              << ",\"duration\":" << duration;
    if (!outPath.empty()) std::cout << ",\"out\":\"" << outPath << "\"";
    std::cout << "}" << std::endl;
  } else {
    std::cout << "记录 " << name << " 位姿：" << samples << " 样本 / "
              << duration << " s";
    if (!outPath.empty()) std::cout << " → " << outPath;
    std::cout << std::endl;
  }
  if (samples == 0)
    Fail("0 样本 —— 模型名不对或仿真没在跑（**不要当成「模型没动」**）", 2);
  return 0;
}

/// 读浮力插件发布的浸水体积（SP-01 的"实测 ∇"来源，opt-in）
int CmdSubvol(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty()) Fail("用法：subvol <model> [--duration <s>]");
  const std::string name = _a.pos[0];
  const double duration = _a.GetD("duration", 3.0);
  const std::string topic = "/keship/sp01/subvol/" + name;

  /// ★ 同 WaitPoseInfo：回调按值捕获 shared_ptr 状态（防析构后迟到回调悬垂引用）
  struct SubvolState {
    std::mutex mutex;
    std::vector<double> vals;
  };
  auto svs = std::make_shared<SubvolState>();
  auto sub = _mm.GetNode().Subscribe<gz::msgs::Double>(topic,
      [svs](const gz::msgs::Double &_msg) {
        std::lock_guard<std::mutex> lk(svs->mutex);
        svs->vals.push_back(_msg.data());
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(
      static_cast<int>(duration * 1000)));

  size_t n;
  double last = 0.0;
  { std::lock_guard<std::mutex> lk(svs->mutex); n = svs->vals.size(); if (n) last = svs->vals.back(); }

  if (g_opt.json) {
    std::cout << "{\"model\":\"" << name << "\",\"topic\":\"" << topic
              << "\",\"samples\":" << n;
    if (n) std::cout << ",\"last\":" << last;
    std::cout << "}" << std::endl;
  } else if (n == 0) {
    std::cout << topic << " : 0 样本" << std::endl;
  } else {
    std::cout << topic << " : " << n << " 样本，末值 "
              << std::fixed << std::setprecision(5) << last << " m³" << std::endl;
  }
  if (n == 0) {
    Info("（0 样本 = 该模型没开 <publish_subvolume>，或未在算浮力 —— "
         "**不可当成实测 0**）");
    return 2;
  }
  return 0;
}

// ═══════════════════════════════════════════════════════════
// 分发
// ═══════════════════════════════════════════════════════════

void PrintHelp() {
  std::cout <<
R"(keship_ctl — KeShip 无头指令入口

用法:
  keship_ctl [--world <name>] [--json] <命令> [参数...]

全局:
  --world <name>   世界名（默认 keship_world）
  --json           机器可读输出
  --force          跳过"GUI 会话共存"守卫（见文末警告）
  help             本帮助

世界:
  world status                     世界状态（模型数 / 暂停 / 仿真时间 / real_time_factor）
                                   ⚠ real_time_factor **仅供参考，不作判据**：
                                     · 它是短窗滚动平均，受加载瞬态影响 —— 实测出现过 1.015 的
                                       跳变，以及 0.039 与实测 30.058 Hz 明显不自洽；
                                     · 语义上 **RTF 低只是"慢"，不是"错"** —— 仿真按固定步长推进，
                                       RTF 不参与物理计算、不影响结果正确性，且可靠更强机器改善；
                                     · ⇒ **只可用于多档性能对比**（谁比谁快），
                                       不得用于判定正确性/达标，更**不得据此去改用户的设定**
                                       （见 doc/plans/model_loading_perf_fix_plan.md §12.4）。
  world pause | world resume       暂停 / 恢复
  world load-ksw <dir> [--ns <名>[/<子>]] [--offset "x y z r p y"] [--no-water]
                                   ★ 加载 .ksw 世界（与 GUI「载入世界」同源：
                                     解析清单 → 写水域 JSON → 静态优先逐个 load-ksm）
                                     ⚠ 不生成 3D 水面视觉（那是 GUI 侧职责）

模型:
  model list                       列出活动场景中的模型（名字/ID/位姿）
  model spawn-hull <name> [--type wigley-classic|wigley-flat|wigley-sharp|wigley-fine|lewis-u|lewis-v]
                          [--L 6 --B 1.5 --D 1.0 --rho 500]
                          [--pos "x y z r p y"]   也接受 --pos x y z r p y（最多 6 个数）
                          [--hydro] [--publish-subvol] [--damping <Zw,Kp,Mq>]
                          注：--damping 必须同时给 --hydro（插件在 enable_hydro=false
                              时不走阻尼分支）；--publish-subvol 才发布 /keship/sp01/subvol/<名>
  model spawn-sdf <name> <file.sdf>   通用逃生口：直接喂 SDF 文件
  model spawn-cylinder <name> [--d 0.5 --len 1.0 --rho 500] [--pos ...]
  model spawn-file <name> <path> [--static] [--buoyancy] [--pos ...]
  model load-ksm <name> <file.ksm> [--pos ...]
                           ★ 与 GUI「加载模型」选 .ksm **同一份实现**（ModelManager::
                             LoadModelFromKsmFile）：解析五段 → 缓存 SDF/浮力/推力/
                             水动力/传感器 → 按有无推力·浮力分派 spawn
  model info <name>                ★ 暴露该模型的内部状态：分派路径 / 有无浮力·推力·
                                   水动力·传感器 / M_A 是否非零 / 是否进了 2D 足迹缓存
                                   （验证加载流程的"显微镜"，只读）
                                   ⚠ **配置与缓存类字段来自本进程内存** —— keship_ctl 每次
                                     调用都是新进程，故**跨调用时这些字段必然为空**。
                                     要看真实状态请用 `script` 把加载与查询写进同一个脚本，
                                     或紧跟在同一次 `load-ksm` 之后查看。
  model rebuild <name> [--kind all|buoyancy|thrust|hydro|sensors]
                                   ★ 触发插件重建（覆盖 4 条 Rebuild 路径；
                                     配置从缓存取 ⇒ 须先 load-ksm）
  model save-ksm <name> <file.ksm> [--sdf-name <模型名>]
                                   ★ 保存为 .ksm（与 GUI 同一实现）⇒ 可做往返验证
  model delete <name>
  model delete-all                 ★ 清空场景：删除**全部模型**并**清空水域**（水域 JSON 置空）。
                                   语义（张老师 2026-09-25 拍板）：「删除所有模型、水域、子世界」。
                                   · 枚举**活动场景**（不依赖本进程 modelList）⇒ 跨进程可靠；
                                   · **排除** world 自带的 sun / ground_plane（删了会破坏渲染）；
                                   · 无头侧没有 3D 水面 ⇒ 只清 JSON（物理侧靠 mtime 热更新跟上）；
                                   · 子世界来源链是本进程元数据，无头恒空 ⇒ 无需处理。
                                   ⚠ 检测到带 GUI 的仿真时默认拒绝（会破坏 GUI 状态），需 --force。
  model pose <name>
  model set-pose <name> x y z roll pitch yaw

环境:
  wave set [--gain 1.0] [--model PMS]      下发波浪/静水参数（实验前常设 --gain 0）
  water show                               打印水域 JSON 原文
  water list                               列出已解析的水域（空场景会提示需先 add-default）
  water add-default                        添加「默认水域」——**教学实验的前置步骤**
                                           （与 GUI 按钮逐项一致：±250 m 矩形 / baseHeight=0 /
                                            海况 1 级波与风参数）
  water reset                              清空水域（对齐 GUI 启动时的空场景）
  water set <areas.json>                   写入指定水域配置

⚠️ 与 GUI 共存（张老师要求：无头入口不得影响主程序与 GUI 操作）：
  `water *`（改文件）与 `model delete-all` 会**改全局共享状态** —— 物理侧靠 mtime 热更新会跟上，
  但 GUI 的内存水域列表与 3D 水面**不会同步**，界面会与实际脱节。
  因此检测到**带 GUI 的仿真**在跑时，这几条**默认拒绝**；确需并行请加 `--force`。
  其余命令（world/model list/pose/watch/subvol/hull info/单个 spawn·delete）与 GUI 并行无碍。

船型（纯计算，无需仿真在跑）:
  hull info [--type ...] [--L --B --D --rho] [--draft d]
                                           生成器静水力：全船体积 / Aw / Ix / KB / BM / GM /
                                           volumeAtDraft(d)（SP-01 的"理论 ∇"就是它）

观测:
  watch pose <name> --duration <s> [--out <csv>]   记录位姿
  subvol <model> [--duration <s>]                  读浮力插件发布的浸水体积

批处理:
  script <file>                    逐行执行命令（# 开头为注释，空行忽略）
)";
}

int Dispatch(keship::ModelManager &_mm, const std::vector<std::string> &_argv);

int CmdScript(keship::ModelManager &_mm, const Args &_a) {
  if (_a.pos.empty()) Fail("用法：script <file>");
  std::ifstream in(_a.pos[0]);
  if (!in) Fail("打不开脚本：" + _a.pos[0]);
  std::string line;
  int ln = 0, rc = 0;
  while (std::getline(in, line)) {
    ++ln;
    // 去注释与首尾空白
    auto h = line.find('#');
    if (h != std::string::npos) line = line.substr(0, h);
    auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    auto e = line.find_last_not_of(" \t\r\n");
    line = line.substr(b, e - b + 1);

    if (!g_opt.json) std::cout << "── [" << ln << "] " << line << std::endl;
    std::istringstream ss(line);
    std::vector<std::string> toks;
    std::string t;
    while (ss >> t) toks.push_back(t);
    if (toks.empty()) continue;
    int r = 0;
    try {
      r = Dispatch(_mm, toks);
    } catch (const CtlError &e) {
      std::cerr << "[错误] 脚本 " << _a.pos[0] << " 第 " << ln
                << " 行失败，已中止：" << e.what()
                << "（rc=" << e.code << "）" << std::endl;
      return e.code;
    }
    if (r != 0) {
      std::cerr << "[错误] 脚本 " << _a.pos[0] << " 第 " << ln
                << " 行返回非零，已中止（rc=" << r << "）" << std::endl;
      return r;
    }
  }
  Info("脚本执行完毕：" + _a.pos[0]);
  return rc;
}

int Dispatch(keship::ModelManager &_mm, const std::vector<std::string> &_argv) {
  if (_argv.empty()) { PrintHelp(); return 0; }

  const std::string cmd = _argv[0];
  std::vector<std::string> rest(_argv.begin() + 1, _argv.end());
  Args a = ParseArgs(rest);

  if (cmd == "help" || cmd == "--help" || cmd == "-h") { PrintHelp(); return 0; }

  // ── world ──
  if (cmd == "world") {
    if (a.pos.empty()) Fail("用法：world status|pause|resume|load-ksw");
    /// ⚠ 子命令名要摘掉再传给命令实现（与 model/water/watch 分支一致）——
    /// 2026-09-24 首版漏了这步，导致 `world load-ksw <dir>` 里 dir 收到的是 "load-ksw"。
    std::vector<std::string> wtail(a.pos.begin() + 1, a.pos.end());
    Args wsa = a; wsa.pos = wtail;
    if (a.pos[0] == "status") return CmdWorldStatus(_mm);
    if (a.pos[0] == "load-ksw") return CmdWorldLoadKsw(_mm, wsa);
    if (a.pos[0] == "pause")  return CmdWorldPause(_mm, true);
    if (a.pos[0] == "resume") return CmdWorldPause(_mm, false);
    Fail("未知 world 子命令：" + a.pos[0]);
  }

  // ── model ──
  if (cmd == "model") {
    if (a.pos.empty()) Fail("用法：model list|spawn-hull|spawn-cylinder|spawn-file|load-ksm|info|rebuild|save-ksm|delete|delete-all|pose|set-pose");
    const std::string sub = a.pos[0];
    std::vector<std::string> tail(a.pos.begin() + 1, a.pos.end());
    Args sa = a; sa.pos = tail;

    if (sub == "list")          return CmdModelList(_mm);
    if (sub == "spawn-hull")    return CmdModelSpawnHull(_mm, sa);
    if (sub == "spawn-sdf")     return CmdModelSpawnSdf(_mm, sa);
    if (sub == "spawn-cylinder")return CmdModelSpawnCylinder(_mm, sa);
    if (sub == "spawn-file")    return CmdModelSpawnFile(_mm, sa);
    if (sub == "load-ksm")      return CmdModelLoadKsm(_mm, sa);
    if (sub == "info")          return CmdModelInfo(_mm, sa);
    if (sub == "rebuild")       return CmdModelRebuild(_mm, sa);
    if (sub == "save-ksm")      return CmdModelSaveKsm(_mm, sa);
    if (sub == "delete")        return CmdModelDelete(_mm, sa);
    if (sub == "delete-all")    return CmdModelDeleteAll(_mm);
    if (sub == "pose")          return CmdModelPose(_mm, sa);
    if (sub == "set-pose")      return CmdModelSetPose(_mm, sa);
    Fail("未知 model 子命令：" + sub);
  }

  // ── hull（纯计算，不需要仿真在跑）──
  if (cmd == "hull") {
    if (a.pos.empty())
      Fail("用法：hull info [--type ...] [--L --B --D --rho] [--draft d]");
    if (a.pos[0] == "info") return CmdHullInfo(a);
    Fail("未知 hull 子命令：" + a.pos[0]);
  }

  // ── wave / water ──
  if (cmd == "wave") {
    if (a.pos.empty()) Fail("用法：wave set [--gain g] [--model PMS]");
    if (a.pos[0] == "set") return CmdWaveSet(_mm, a);
    Fail("未知 wave 子命令：" + a.pos[0]);
  }
  if (cmd == "water") {
    if (a.pos.empty())
      Fail("用法：water show | water list | water set <file> | water add-default | water reset");
    std::vector<std::string> tail(a.pos.begin() + 1, a.pos.end());
    Args sa = a; sa.pos = tail;
    if (a.pos[0] == "show")        return CmdWaterShow();
    if (a.pos[0] == "list")        return CmdWaterList();
    if (a.pos[0] == "set")         return CmdWaterSet(sa);
    if (a.pos[0] == "add-default") return CmdWaterAddDefault(_mm);
    if (a.pos[0] == "reset")       return CmdWaterReset();
    Fail("未知 water 子命令：" + a.pos[0]);
  }

  // ── 观测 ──
  if (cmd == "watch") {
    if (a.pos.empty()) Fail("用法：watch pose <name> --duration <s> [--out csv]");
    std::vector<std::string> tail(a.pos.begin() + 1, a.pos.end());
    Args sa = a; sa.pos = tail;
    if (a.pos[0] == "pose") return CmdWatchPose(_mm, sa);
    Fail("未知 watch 子命令：" + a.pos[0]);
  }
  if (cmd == "subvol") return CmdSubvol(_mm, a);

  // ── 批处理 ──
  if (cmd == "script") return CmdScript(_mm, a);

  Fail("未知命令：" + cmd + "（keship_ctl help 看用法）");
  return 1;
}

}  // namespace

int main(int argc, char **argv) {
  // ── 解析全局选项与命令 ──
  std::vector<std::string> tokens;
  for (int i = 1; i < argc; ++i) tokens.push_back(argv[i]);

  std::vector<std::string> cmdTokens;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "--world" && i + 1 < tokens.size()) {
      g_opt.world = tokens[++i];
    } else if (tokens[i].rfind("--world=", 0) == 0) {
      g_opt.world = tokens[i].substr(8);
    } else if (tokens[i] == "--json") {
      g_opt.json = true;
    } else if (tokens[i] == "--force") {
      g_opt.force = true;
    } else {
      cmdTokens.push_back(tokens[i]);
    }
  }

  if (cmdTokens.empty() || cmdTokens[0] == "help" ||
      cmdTokens[0] == "--help" || cmdTokens[0] == "-h") {
    PrintHelp();
    return 0;
  }

  /// 命令名本身可能需要先探活（help 不需要）
  keship::ModelManager mm(g_opt.world);
  try {
    return Dispatch(mm, cmdTokens);
  } catch (const CtlError &e) {
    PrintError(e.what());
    return e.code;
  } catch (const std::exception &e) {
    PrintError(std::string("未预期的异常：") + e.what());
    return 3;
  }
}
