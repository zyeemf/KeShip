/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 菜单插件： keship */
/// Zhangyu 创建 2026.1.16

#include "MenuPluginKeship.hh"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <set>
#include <map>
#include <functional>
#include <algorithm>
#include <boost/algorithm/string.hpp>

#include <gz/plugin/Register.hh>
#include <gz/gui/Application.hh>
#include <gz/gui/MainWindow.hh>

#include <QFileDialog>
#include <QMessageBox>
#include <QUrl>
#include <QTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <gz/msgs/stringmsg.pb.h>
#include <gz/msgs/world_control.pb.h>
#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/param.pb.h>
#include <gz/msgs/spherical_coordinates.pb.h>
#include <gz/math/Pose3.hh>
#include <gz/math/Quaternion.hh>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "editors/ModelSettingsDialog.hh"
#include "experiments/CylinderTestDialog.hh"
#include "experiments/StabilityExperimentDialog.hh"
#include "experiments/IncliningExperimentDialog.hh"
#include "experiments/HullStabilityDialog.hh"
#include "shared/ShipHullDialog.hh"
#include "shared/UiTheme.hh"
#include "experiments/DisplacementVerifyDialog.hh"
#include "experiments/CodeRunnerDialog.hh"
#include "tasks/TaskSetDialog.hh"
#include "tasks/TaskMonitorDialog.hh"
#include "editors/BuoyancyEditorDialog.hh"
#include "editors/ThrusterEditorDialog.hh"
#include "editors/HydrodynamicsEditorDialog.hh"
#include "editors/SensorEditorDialog.hh"
#include "editors/WorldSettingsDialog.hh"
#include "viewers/CameraViewerDialog.hh"
#include "viewers/LidarViewerDialog.hh"
#include "shared/Buoyancy3DView.hh"
#include "water/WaterAreaDrawDialog.hh"
#include "editors/EnvironmentParamsDialog.hh"
#include "editors/InertiaCalculatorDialog.hh"
#include "editors/LoadWorldDialog.hh"
#include "editors/SaveWorldDialog.hh"
#include "editors/SelectWorldDialog.hh"
#include "editors/SelectKsmDialog.hh"
#include "editors/SubWorldSettingsDialog.hh"
#include "editors/MoveToChainDialog.hh"
#include "editors/DuplicateWaterDialog.hh"
#include "WaterAreaSerializer.hh"
#include "keship/utils/SeaState.hh"
#include "MeshUtils.hh"

//////////////////////////////////////////////////////////////////////////////////////////////////
namespace
{
  /// \brief 收集侧边栏快捷路径（Windows 盘符 + 用户自定义）
  QList<QUrl> SidebarUrls()
  {
    QList<QUrl> urls;

    /// 自动检测 /mnt/ 下的 Windows 盘符
    QDir mntDir("/mnt");
    for (const auto &entry : mntDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
      if (entry == "wsl" || entry == "wslg")
        continue;
      urls << QUrl::fromLocalFile("/mnt/" + entry);
    }

    return urls;
  }

  /// \brief 为文件对话框设置侧边栏快捷路径
  void SetupSidebarUrls(QFileDialog &_dialog)
  {
    _dialog.setSidebarUrls(SidebarUrls());
  }
} // namespace

//////////////////////////////////////////////////////////////////////////////////////////////////
MenuPluginKeship::MenuPluginKeship()
    : gz::gui::Plugin()
{
  /// 说明（2026-09-18）：此处曾全局应用"对话框统一主题"，实测该做法会把原生
  /// 按钮 / 边框的细节绘制拉平（调色板覆盖 style 的细节绘制），观感并不合适，
  /// 已撤销 —— 改为**逐个对话框**按需调整（见 doc/plans/ui_improvement_plan.md）。
  /// UiTheme::statusColor() 供逐个修改时统一状态语义色；applyToDialog() 备用。

  /// 设置主窗口标题
  auto topLevelWindows = QApplication::topLevelWindows();
  for (auto window : topLevelWindows)
  {
    std::cout << "window->objectName(): " << window->objectName().toStdString() << std::endl;
    if (window->objectName() == "window")
    {
      window->setTitle("KeShip虚拟仿真教学平台");

      // 设置窗口无边框
      // window->setFlags(window->flags() | Qt::FramelessWindowHint);
      // window->setFlags(Qt::Window | Qt::FramelessWindowHint);
      // window->show();

      // window->setCursor(Qt::BusyCursor);

      
      // window->showFullScreen(); // 全屏显示
      window->showMaximized(); // 最大化窗口
      
      auto childWindows = window->children();
      for (auto child : childWindows)
      {
        std::cout << "child->objectName(): " << child->objectName().toStdString() << std::endl;
        if(child->inherits("QMenuBar"))
        {
          std::cout<<"QMenuBar"<<std::endl;
        }
        
      }
      // break; //无用，因为只有一个窗口
    }
  }
  /// 初始化模型管理器
  this->modelManager = std::make_unique<keship::ModelManager>("keship_world");

  /// 初始化 rclcpp（在 gz-gui 进程中只需初始化一次）
  if (!rclcpp::ok()) rclcpp::init(0, nullptr);
  rosNode_ = std::make_shared<rclcpp::Node>("keship_gui_sensors");
  spinThread_ = std::thread([this]() { rclcpp::spin(rosNode_); });

  /// 模型变更时刷新传感器列表
  QObject::connect(this, &MenuPluginKeship::ModelNamesChanged,
                   this, &MenuPluginKeship::RefreshSensorList);

}

MenuPluginKeship::~MenuPluginKeship()
{
  modelManager->GetNode().Unsubscribe(
      "/world/" + modelManager->GetWorldName() + "/pose/info");
  rclcpp::shutdown();
  if (spinThread_.joinable())
    spinThread_.join();
}

void MenuPluginKeship::LoadConfig(const tinyxml2::XMLElement *_pluginElement)
{
  {
    try {
      std::string shareDir = ament_index_cpp::get_package_share_directory("keship");
      waterVisualMgr_.SetShaderDir(
          shareDir + "/models/waves/materials/programs/");
    } catch (...) {}
  }

  /// 启动即空场景：不恢复上次会话的水域配置（需求：关闭时的水域
  /// 不得跨重启继承），改为启动写空 JSON —— 清掉旧数据的同时
  /// 通知物理插件（WavesModel/HydroBuoyancy 按文件 mtime 热更新）
  /// 清空其波浪场/水区配置，数据、视觉、物理三者一致
  WriteWaterAreasJson();

  // 启动时不生成任何水面（Areas 恒空）；水面一律由
  // "添加默认水域"/自定义绘制/载入 ksw 触发产生
  NotifyModelListChanged();

  this->modelManager->GetNode().Subscribe(
      "/world/" + this->modelManager->GetWorldName() + "/pose/info",
      std::function<void(const gz::msgs::Pose_V&)>(
          [this](const gz::msgs::Pose_V &_msg) {
            std::lock_guard<std::mutex> lock(modelPoseCacheMutex_);
            modelPoseCache_.clear();
            for (int i = 0; i < _msg.pose_size(); ++i)
            {
              const auto &p = _msg.pose(i);
              const auto &ori = p.orientation();
              double yaw = gz::math::Quaterniond(
                  ori.w(), ori.x(), ori.y(), ori.z()).Yaw();
              modelPoseCache_[p.name()] = {
                  p.position().x(), p.position().y(), yaw
              };
            }
          }));

  /// 若上次退出时卡片处于收起状态（高度被保存为 50），启动后恢复展开，
  /// 与 QML 内部 collapsed 状态保持一致（卡片创建完成后再执行）
  QTimer::singleShot(500, this, [this]()
  {
    auto *card = this->CardItem();
    if (!card)
      return;
    if (card->height() <= 50.5)
    {
      card->setProperty("lastHeight", 600);
      card->setProperty("state", "floating_collapsed");
      card->setProperty("state", "floating");
    }
  });
}

//////////////////////////////////////////////////////////////////////////////////////////////////
QStringList MenuPluginKeship::GetModelNames() const
{
  auto modelList = this->modelManager->GetModelList();
  QStringList modelNames;
  for (const auto &model : modelList)
  {
    modelNames << QString::fromStdString(model.first);
  }
  return modelNames;
}

QStringList MenuPluginKeship::GetWaterAreaNames() const
{
  QStringList names;
  for (const auto &a : customWaterAreas_)
    names << QString::fromStdString(a.name);
  return names;
}

QStringList MenuPluginKeship::GetWaterAreaSeaStates() const
{
  // 按 Areas() 全量生成，与树水域行的 widx（Areas 索引）对齐
  QStringList levels;
  for (const auto &a : waterAreaMgr_.Areas())
  {
    const double hs = keship::SeaState::HsFromParams(
        a.waveParams.gain, a.waveParams.period);
    levels << QString("%1级").arg(keship::SeaState::LevelFromHs(hs));
  }
  return levels;
}

QVariantList MenuPluginKeship::GetModelStaticFlags() const
{
  auto modelList = this->modelManager->GetModelList();
  QVariantList flags;
  for (const auto &model : modelList)
  {
    bool isStatic = false;
    this->modelManager->GetCachedModelStatic(model.first, isStatic);
    flags << isStatic;
  }
  return flags;
}

QVariantList MenuPluginKeship::GetModelMathHullFlags() const
{
  auto modelList = this->modelManager->GetModelList();
  QVariantList flags;
  for (const auto &model : modelList)
    flags << (mathHullNames_.count(model.first) > 0);
  return flags;
}

QVariantList MenuPluginKeship::GetWorldHierarchy() const
{
  /// 树节点：模型/水域列表 + 直接子链（键 = 子 World 链，空 = 顶层）
  struct Node
  {
    std::vector<std::pair<QString, QString>> models; ///< <实例名, 原名>
    std::vector<int> midxs;
    std::vector<std::pair<QString, QString>> waters; ///< <水域名, 显示名>
    std::vector<int> widxs;
    std::set<QString> subs;
  };
  std::map<QString, Node> nodes;
  auto ensureChain = [&nodes](const QString &_chain)
  {
    QString cur;
    for (const auto &seg : _chain.split('/', Qt::SkipEmptyParts))
    {
      cur = cur.isEmpty() ? seg : cur + "/" + seg;
      nodes[cur];
    }
    nodes[_chain];
  };

  // 模型：来源链（无来源 = 顶层）；原名 = 来源 baseName
  const auto &modelList = this->modelManager->GetModelList();
  int midx = 0;
  for (const auto &[name, id] : modelList)
  {
    QString chain;
    keship::ModelManager::WorldModelSource src;
    if (this->modelManager->GetModelSource(name, src))
    {
      for (const auto &s : src.path)
      {
        if (!chain.isEmpty()) chain += "/";
        chain += QString::fromStdString(s);
      }
    }
    ensureChain(chain);
    const QString disp = src.baseName.empty()
        ? QString::fromStdString(name) : QString::fromStdString(src.baseName);
    nodes[chain].models.emplace_back(QString::fromStdString(name), disp);
    nodes[chain].midxs.push_back(midx++);
  }

  // 水域：来源链（无来源 → 顶层）
  int widx = 0;
  for (const auto &a : waterAreaMgr_.Areas())
  {
    QString chain;
    keship::ModelManager::WaterWorldSource wsrc;
    if (this->modelManager->GetWaterSource(a.name, wsrc))
    {
      for (const auto &s : wsrc.path)
      {
        if (!chain.isEmpty()) chain += "/";
        chain += QString::fromStdString(s);
      }
    }
    ensureChain(chain);
    // 显示原名（来源 baseName），实例名仅作内部键
    const QString disp = wsrc.baseName.empty()
        ? QString::fromStdString(a.name) : QString::fromStdString(wsrc.baseName);
    nodes[chain].waters.emplace_back(disp, disp);
    nodes[chain].widxs.push_back(widx++);
  }

  // 链父子引用（父=去掉末段，空=顶层）
  for (const auto &kv : nodes)
  {
    const QString chain = kv.first;
    if (chain.isEmpty()) continue;
    const int p = chain.lastIndexOf('/');
    nodes[p < 0 ? QString() : chain.left(p)].subs.insert(chain);
  }

  // 输出扁平行：模型组 → 水域组 → 子链组（递归）；深度随层级
  QVariantList rows;
  std::function<void(const QString &, int)> emitChain = [&](const QString &_chain, int _depth)
  {
    const Node &nd = nodes[_chain];

    if (!nd.models.empty())
    {
      QVariantMap g;
      g["kind"] = "group"; g["gtype"] = "models";
      g["text"] = "模型"; g["chain"] = _chain; g["depth"] = _depth;
      rows.push_back(g);
      for (size_t i = 0; i < nd.models.size(); ++i)
      {
        bool isSt = false;
        this->modelManager->GetCachedModelStatic(nd.models[i].first.toStdString(), isSt);
        QVariantMap m;
        m["kind"] = "model"; m["text"] = nd.models[i].second;
        m["chain"] = _chain; m["depth"] = _depth + 1;
        m["midx"] = nd.midxs[i]; m["widx"] = -1;
        m["isStatic"] = isSt;
        m["mathHull"] = mathHullNames_.count(nd.models[i].first.toStdString()) > 0;
        rows.push_back(m);
      }
    }

    if (!nd.waters.empty())
    {
      QVariantMap g;
      g["kind"] = "group"; g["gtype"] = "waters";
      g["text"] = "水域"; g["chain"] = _chain; g["depth"] = _depth;
      rows.push_back(g);
      for (size_t i = 0; i < nd.waters.size(); ++i)
      {
        QVariantMap w;
        w["kind"] = "water"; w["text"] = nd.waters[i].second;
        w["chain"] = _chain; w["depth"] = _depth + 1;
        w["midx"] = -1; w["widx"] = nd.widxs[i];
        rows.push_back(w);
      }
    }

    for (const auto &sub : nd.subs)
    {
      QVariantMap g;
      g["kind"] = "group"; g["gtype"] = "sub";
      g["text"] = sub.section('/', -1); g["chain"] = sub; g["depth"] = _depth;
      rows.push_back(g);
      emitChain(sub, _depth + 1);
    }
  };
  emitChain(QString(), 0);
  return rows;
}

void MenuPluginKeship::NotifyModelListChanged()
{
  emit ModelStaticFlagsChanged();
  emit ModelMathHullFlagsChanged();
  emit ModelNamesChanged();
}

void MenuPluginKeship::OnBtn_LoadWorld()
{
  // 选择世界文件夹（<世界名>.ksw，内含 keship_world.kswf 清单与 *.ksm；双击即加载）
  SelectWorldDialog dlg(QString::fromStdString(keship::KeshipWorldsDir()),
                        SidebarUrls());
  if (dlg.exec() != QDialog::Accepted) return;
  const QString dirPath = dlg.selectedDir();
  if (dirPath.isEmpty()) return;
  QString filePath = dirPath + "/keship_world.kswf";

  // 解析世界清单（0.0.4 结构 → 展平模型与水域条目）
  std::vector<keship::ModelManager::WorldManifestItem> items;
  std::vector<keship::ModelManager::WorldWaterItem> waters;
  std::string worldName;
  std::array<double, 6> worldPose{};
  if (!this->modelManager->LoadWorldManifest(filePath.toStdString(), items, waters,
                                             &worldName, &worldPose))
  {
    QMessageBox::warning(nullptr, "错误",
        "无法解析世界清单文件（仅支持 0.0.4 格式，旧版不兼容）");
    return;
  }

  // 模型树行（子链文本 TAB 勾选 TAB 全链文本；子链可为空=顶层，全链文本用于匹配过滤）
  auto chainText = [](const std::vector<std::string> &path)
  {
    QString line;
    for (const auto &seg : path)
    {
      if (!line.isEmpty()) line += "/";
      line += QString::fromStdString(seg);
    }
    return line;
  };
  auto fullText = [&chainText](const std::vector<std::string> &path, const std::string &base)
  {
    QString line = chainText(path);
    if (!line.isEmpty()) line += "/";
    line += QString::fromStdString(base);
    return line;
  };

  QStringList modelLines;
  for (const auto &item : items)
  {
    QString full = fullText(item.path, item.baseName);
    modelLines << chainText(item.path) + "\t1\t" + full;
  }
  QStringList waterLines;
  for (const auto &wi : waters)
  {
    QString full = fullText(wi.path, wi.baseName);
    waterLines << chainText(wi.path) + "\t1\t" + full;
  }

  // 命名空间 + 六向整体偏移 + 模型/水域勾选对话框
  LoadWorldDialog dlg2(QString::fromStdString(worldName), modelLines, waterLines);
  if (dlg2.exec() != QDialog::Accepted) return;

  const QString ns = dlg2.namespaceText();
  const double off[6] = {
      dlg2.offsetX(), dlg2.offsetY(), dlg2.offsetZ(),
      dlg2.offsetRoll() * 3.14159265358979323846 / 180.0,
      dlg2.offsetPitch() * 3.14159265358979323846 / 180.0,
      dlg2.offsetYaw() * 3.14159265358979323846 / 180.0};
  const QStringList selModels = dlg2.selectedModelNames();
  const QStringList selWaters = dlg2.selectedWaterNames();

  /// 载入前确认：合并进当前场景（同名模型/水域将被跳过）
  if (QMessageBox::question(nullptr, "载入世界",
      "载入世界时将把勾选的模型与水区合并进当前场景，\n"
      "同名模型/水域将被跳过，是否继续？",
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No) != QMessageBox::Yes)
  {
    return;
  }

  // 暂停仿真（加载完成后按打开前状态还原，不影响用户手动暂停/继续状态）
  const bool wasPaused = paused_;
  {
    gz::msgs::WorldControl req;
    gz::msgs::Boolean rep; bool result;
    req.set_pause(true);
    this->modelManager->GetNode().Request("/world/keship_world/control", req, 500, rep, result);
  }

  // 解析命名空间层级（可 / 分隔多层）
  std::vector<std::string> nsSeg;
  for (const QString &s : ns.split('/', Qt::SkipEmptyParts))
    nsSeg.push_back(s.toStdString());

  // 命名空间与子 World 链合并（重叠段去重）：
  // ns=[world1] 与 链=[world1,subA] → [world1,subA]，避免 world1_world1_subA 式重复前缀
  auto mergeSegs = [](std::vector<std::string> _ns,
                      const std::vector<std::string> &_path)
  {
    std::vector<std::string> rest = _path;
    while (!rest.empty() && !_ns.empty() && rest.front() == _ns.back())
      rest.erase(rest.begin());
    _ns.insert(_ns.end(), rest.begin(), rest.end());
    return _ns;
  };

  // 加载勾选的水域：实例名 = 子链前缀 + 原键名（统一前缀规则），登记来源
  int skippedWaters = 0;
  std::vector<std::string> skippedWaterNames;
  for (const auto &wi : waters)
  {
    QString full = fullText(wi.path, wi.baseName);
    if (!selWaters.contains(full)) continue;

    std::string instName;
    const auto mergedW = mergeSegs(nsSeg, wi.path);
    for (const auto &s : mergedW) { if (!instName.empty()) instName += "_"; instName += s; }
    if (!instName.empty()) instName += "_";
    instName += wi.baseName;

    // 合并模式：同名水域跳过（保留场景已有水区）
    bool dup = false;
    for (const auto &a : waterAreaMgr_.Areas())
      if (a.name == instName) { dup = true; break; }
    if (dup) { ++skippedWaters; skippedWaterNames.push_back(instName); continue; }

    keship::WaterArea a = wi.area;
    a.name = instName;
    waterAreaMgr_.AddArea(a);
    customWaterAreas_.push_back(a);

    keship::ModelManager::WaterWorldSource wsrc;
    wsrc.path = mergeSegs(nsSeg, wi.path);
    wsrc.baseName = wi.baseName;
    this->modelManager->SetWaterSource(instName, wsrc);
  }
  std::cout << "LoadWorld: 加载 " << waterAreaMgr_.Areas().size() << " 个水域" << std::endl;

  /// 水面先于任何模型生成：先写水域 JSON，再生成水面覆盖并下发波浪参数
  /// （水面早于模型存在，动态模型载入后不再因迟到的水面碰撞而跳起）
  WriteWaterAreasJson();

  /// 按载入的水区刷新水面视觉（SpawnAll 内部会先删旧表面并等待消失）
  waterVisualMgr_.SpawnAll(waterAreaMgr_.Areas(), modelManager->GetNode());

  // 载入世界的波浪参数立即下发，保证与界面配置一致
  {
    auto areas = waterAreaMgr_.Areas();
    if (!areas.empty())
      PublishWaveParams(areas[0].waveParams, areas[0].name);
  }

  /// 先静止后动态：所有 static 模型先并入位，再生成浮力/推进模型（此时水面已就绪）
  std::stable_partition(items.begin(), items.end(),
      [](const keship::ModelManager::WorldManifestItem &it) { return it.isStatic; });

  int skippedModels = 0;
  std::vector<std::string> skippedModelNames;
  /// ★ 2026-09-24 修 K1：加载失败必须**可见** —— 原先 spawn 的返回值根本没接收，
  /// 失败既不报错、不计数、不弹窗，用户以为加载成功（只有控制台一行 stderr）。
  /// 这里对齐"重名跳过"早已有的提示机制。
  int failedModels = 0;
  std::vector<std::string> failedModelNames;
  for (const auto &item : items)
  {
    QString full = fullText(item.path, item.baseName);
    if (!selModels.contains(full)) continue;

    // 实例名：命名空间 + 子 World 链 + 原键名（下划线连接，重叠段去重保证唯一）
    std::string instName;
    const auto mergedM = mergeSegs(nsSeg, item.path);
    for (const auto &s : mergedM) { if (!instName.empty()) instName += "_"; instName += s; }
    if (!instName.empty()) instName += "_";
    instName += item.baseName;

    // 合并模式：同名模型跳过（保留场景已有实例与来源元数据）
    if (this->modelManager->GetModelList().count(instName) > 0)
    {
      std::cerr << "LoadWorld: 跳过重名模型 " << instName << std::endl;
      skippedModelNames.push_back(instName);
      ++skippedModels;
      continue;
    }

    // 位姿 = 世界整体位姿(attributes.pose) + 模型自身位姿 + 对话框偏移（角度已换算弧度）
    double pos[6] = {
        item.pose[0] + worldPose[0] + off[0],
        item.pose[1] + worldPose[1] + off[1],
        item.pose[2] + worldPose[2] + off[2],
        item.pose[3] + worldPose[3] + off[3],
        item.pose[4] + worldPose[4] + off[4],
        item.pose[5] + worldPose[5] + off[5]};

    /// ★★ 2026-09-24：**改调与"单模型加载"同一个下沉函数**（`LoadModelFromKsmFile`）。
    /// 原先此处内联了「解析 → 模型名替换 → 缓存 4 类配置 → 分派 spawn → 等 ID」整套逻辑，
    /// 与 `OnBtn_LoadModel` 的 .ksm 分支构成**两份实现** ⇒ 任何改动都要改两处，
    /// 漏一处即漂移（这正是审核中 P0-1 的成因模式）。
    ///
    /// 保留在世界槽外层的、属世界语义的三件事：
    ///   · 实例名由「命名空间 + 子世界链 + 原名」合成（上方 mergeSegs）；
    ///   · 位姿叠加世界整体位姿与对话框偏移（上方 pos）；
    ///   · 重名先跳过（上方），来源登记**完整链信息**（下方）。
    if (!this->modelManager->LoadModelFromKsmFile(instName, item.ksmPath, pos, item.baseName))
    {
      /// ★ 修 K1 + K2：计入失败并**不登记来源元数据** ——
      /// 原先无论成败都 `SetModelSource`，会在 modelSource_ 里留下实际不存在的模型。
      std::cerr << "LoadWorld: 加载失败 " << instName
                << " <- " << item.ksmPath << std::endl;
      failedModelNames.push_back(instName);
      ++failedModels;
      continue;
    }

    // 登记来源元数据（后续保存时按子 World 链还原嵌套位置；链与实例名同用 mergeSegs 去重）
    keship::ModelManager::WorldModelSource src;
    src.kswPath = filePath.toStdString();
    src.ns = nsSeg;
    src.path = mergeSegs(nsSeg, item.path);
    src.baseName = item.baseName;
    this->modelManager->SetModelSource(instName, src);
  }

  // 恢复仿真（水面与模型全部就位后再启动，避免加载期自由落体与水面碰撞弹跳；
  // 若加载前用户已手动暂停则保持暂停）
  SetSimulationPaused(wasPaused);

    if (skippedModels > 0 || skippedWaters > 0 || failedModels > 0)
  {
    QStringList names;
    for (size_t i = 0; i < skippedModelNames.size() && i < 4; ++i)
      names << QString::fromStdString(skippedModelNames[i]);
    for (size_t i = 0; i < skippedWaterNames.size() && i < 4; ++i)
      names << QString::fromStdString(skippedWaterNames[i]);
    QStringList failNames;
    for (size_t i = 0; i < failedModelNames.size() && i < 4; ++i)
      failNames << QString::fromStdString(failedModelNames[i]);

    QString detail;
    if (skippedModels > 0)
      detail += QString("重名模型 %1 个").arg(skippedModels);
    if (skippedWaters > 0)
    {
      if (!detail.isEmpty()) detail += "、";
      detail += QString("重名水域 %1 个").arg(skippedWaters);
    }
    if (!detail.isEmpty()) detail += "（保留场景已有）";
    if (!names.isEmpty()) detail += "\n被跳过：" + names.join("、");

    /// ★ 2026-09-24 修 K1：失败必须显式告知用户（原先完全静默）
    if (failedModels > 0)
    {
      if (!detail.isEmpty()) detail += "\n";
      detail += QString("⚠ 加载失败 %1 个").arg(failedModels);
      if (!failNames.isEmpty()) detail += "：" + failNames.join("、");
      if (failedModelNames.size() > 4)
        detail += QString(" 等共 %1 个").arg(failedModelNames.size());
      detail += "\n（失败原因见控制台输出）";
    }

    QMessageBox::information(nullptr, "载入世界", detail);
  }

  NotifyModelListChanged();
  std::cout << "keship: 载入世界完成" << std::endl;
}

void MenuPluginKeship::OnBtn_SaveWorld()
{
  // 从场景模型 + 来源元数据构建树（行格式：子World链 TAB 勾选 TAB 原名 TAB 实例名）
  // 子 World 链来自来源登记；无来源/空链 → segs 为空，模型落在顶层“模型”分组
  QStringList treeLines;
  const auto &modelList = this->modelManager->GetModelList();
  for (const auto &[name, id] : modelList)
  {
    keship::ModelManager::WorldModelSource src;
    QString segs;
    if (this->modelManager->GetModelSource(name, src))
    {
      for (const auto &s : src.path) { if (!segs.isEmpty()) segs += "/"; segs += QString::fromStdString(s); }
    }
    // 显示原名（来源 baseName），实例名仅作内部提交键（缓存/SDF 查找）
    const QString instName = QString::fromStdString(name);
    const QString displayName = src.baseName.empty()
        ? instName
        : QString::fromStdString(src.baseName);
    treeLines << segs + "\t1\t" + displayName + "\t" + instName;
  }

    // 水区行：子链 TAB 勾选 TAB 全链/原名（叶子显示末段=原名，勾选键含链可区分跨链同名）
    QStringList waterNames;
    for (const auto &a : waterAreaMgr_.Areas())
    {
      keship::ModelManager::WaterWorldSource wsrc;
      QString segs;
      if (this->modelManager->GetWaterSource(a.name, wsrc))
      {
        for (const auto &s : wsrc.path) { if (!segs.isEmpty()) segs += "/"; segs += QString::fromStdString(s); }
      }
      const QString disp = wsrc.baseName.empty()
          ? QString::fromStdString(a.name) : QString::fromStdString(wsrc.baseName);
      const QString fullKey = segs.isEmpty() ? disp : segs + "/" + disp;
      waterNames << segs + "\t1\t" + fullKey;
    }

  // 世界名：优先取首模型来源的子 World 首段（“顶层”非有效世界名则保持默认）
  QString suggestedName = "新世界";
  if (!treeLines.isEmpty())
  {
    const QString firstRoot = treeLines.first().section('\t', 0, 0).section('/', 0, 0);
    if (firstRoot != "顶层" && !firstRoot.isEmpty())
      suggestedName = firstRoot;
  }

  SaveWorldDialog dlg(suggestedName, treeLines, waterNames,
                      QString::fromStdString(keship::KeshipWorldsDir()),
                      SidebarUrls());
  if (dlg.exec() != QDialog::Accepted) return;

  // 按勾选过滤水区（一个不选 = 不保存水区；对话框叶子键 = 全链/原名，映射回实例名）
  const QStringList selWaters = dlg.selectedWaterNames();
  std::string json;
  if (!selWaters.isEmpty())
  {
    std::vector<keship::WaterArea> picked;
    for (const auto &w : waterAreaMgr_.Areas())
    {
      keship::ModelManager::WaterWorldSource wsrc;
      QString key = QString::fromStdString(w.name);
      if (this->modelManager->GetWaterSource(w.name, wsrc) &&
          !wsrc.baseName.empty())
      {
        QString chain;
        for (const auto &s : wsrc.path)
        {
          if (!chain.isEmpty()) chain += "/";
          chain += QString::fromStdString(s);
        }
        key = chain.isEmpty() ? QString::fromStdString(wsrc.baseName)
                              : chain + "/" + QString::fromStdString(wsrc.baseName);
      }
      if (selWaters.contains(key))
        picked.push_back(w);
    }
    json = keship::SerializeWaterAreas(picked);
  }

  QStringList sel = dlg.selectedModels();
  std::vector<std::string> selected;
  selected.reserve(sel.size());
  for (const auto &s : sel) selected.push_back(s.toStdString());

  int failCount = 0;
  bool ok = this->modelManager->SaveWorldManifest(
      dlg.saveDir().toStdString(), dlg.worldName().toStdString(),
      selected, json, &failCount);
  if (!ok && failCount == 0)
  {
    QMessageBox::warning(nullptr, "保存世界", "无法写入世界清单文件");
    return;
  }
  if (failCount > 0)
  {
    QMessageBox::warning(nullptr, "保存世界",
        QString("%1 个模型保存失败，请查看控制台输出").arg(failCount));
    return;
  }
  std::cout << "keship: 保存世界完成 -> " << dlg.saveDir().toStdString()
            << "（世界名 " << dlg.worldName().toStdString() << "）" << std::endl;
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_SaveSubWorld(const QString &_chain)
{
  // 子 World 链前缀（用于过滤 + 剥离）
  const QStringList chainSegs =
      _chain.split('/', Qt::SkipEmptyParts);
  auto relChain = [&chainSegs](const std::vector<std::string> &_path,
                               bool &_match) -> QString
  {
    _match = false;
    if (static_cast<int>(_path.size()) < chainSegs.size()) return QString();
    for (int i = 0; i < chainSegs.size(); ++i)
    {
      if (QString::fromStdString(_path[i]) != chainSegs[i])
        return QString();
    }
    _match = true;
    QString rel;
    for (int i = chainSegs.size(); i < static_cast<int>(_path.size()); ++i)
    {
      if (!rel.isEmpty()) rel += "/";
      rel += QString::fromStdString(_path[i]);
    }
    return rel;
  };

  // 链内模型行（子链剥离；行格式与保存世界一致）
  QStringList treeLines;
  const auto &modelList = this->modelManager->GetModelList();
  for (const auto &[name, id] : modelList)
  {
    keship::ModelManager::WorldModelSource src;
    bool match = false;
    QString rel;
    if (this->modelManager->GetModelSource(name, src))
      rel = relChain(src.path, match);
    if (!match) continue;
    const QString instName = QString::fromStdString(name);
    const QString displayName = src.baseName.empty()
        ? instName : QString::fromStdString(src.baseName);
    treeLines << rel + "\t1\t" + displayName + "\t" + instName;
  }

  // 链内水域行
  QStringList waterNames;
  for (const auto &a : waterAreaMgr_.Areas())
  {
    keship::ModelManager::WaterWorldSource wsrc;
    bool match = false;
    QString rel;
    if (this->modelManager->GetWaterSource(a.name, wsrc))
      rel = relChain(wsrc.path, match);
    if (!match) continue;
    // 显示原名（来源 baseName）；勾选键 = 相对链/原名（可区分跨链同名）
    const QString disp = wsrc.baseName.empty()
        ? QString::fromStdString(a.name) : QString::fromStdString(wsrc.baseName);
    const QString fullKey = rel.isEmpty() ? disp : rel + "/" + disp;
    waterNames << rel + "\t1\t" + fullKey;
  }

  if (treeLines.isEmpty() && waterNames.isEmpty())
  {
    QMessageBox::warning(nullptr, "保存子世界",
        "该子世界内没有模型或水域");
    return;
  }

  // 默认世界名 = 链末段（净化非法字符）；目录 = models_ksw/<世界名>/ 由对话框拼接
  QString suggest = _chain.section('/', -1);
  QString cleaned;
  for (const QChar &c : suggest)
    cleaned += (c.isLetterOrNumber() || c == '_') ? c : '_';
  if (cleaned.isEmpty()) cleaned = "子世界";

  SaveWorldDialog dlg(cleaned, treeLines, waterNames,
      QString::fromStdString(keship::KeshipWorldsDir()),
      SidebarUrls());
  if (dlg.exec() != QDialog::Accepted) return;

  // 勾选水区 → JSON（子世界仅链内自定义水区；对话框叶子键 = 相对链/原名，映射回实例名）
  const QStringList selWaters = dlg.selectedWaterNames();
  std::string json;
  if (!selWaters.isEmpty())
  {
    std::vector<keship::WaterArea> picked;
    for (const auto &w : waterAreaMgr_.Areas())
    {
      keship::ModelManager::WaterWorldSource wsrc;
      QString key = QString::fromStdString(w.name);
      if (this->modelManager->GetWaterSource(w.name, wsrc) &&
          !wsrc.baseName.empty())
      {
        bool match = false;
        QString rel = relChain(wsrc.path, match);
        if (match)
          key = rel.isEmpty() ? QString::fromStdString(wsrc.baseName)
                              : rel + "/" + QString::fromStdString(wsrc.baseName);
        else
          key.clear();
      }
      if (!key.isEmpty() && selWaters.contains(key))
        picked.push_back(w);
    }
    json = keship::SerializeWaterAreas(picked);
  }

  QStringList sel = dlg.selectedModels();
  std::vector<std::string> selected;
  selected.reserve(sel.size());
  for (const auto &s : sel) selected.push_back(s.toStdString());

  int failCount = 0;
  bool ok = this->modelManager->SaveWorldManifest(
      dlg.saveDir().toStdString(), dlg.worldName().toStdString(),
      selected, json, &failCount, _chain.toStdString());
  if (!ok && failCount == 0)
  {
    QMessageBox::warning(nullptr, "保存子世界", "无法写入世界清单文件");
    return;
  }
  if (failCount > 0)
  {
    QMessageBox::warning(nullptr, "保存子世界",
        QString("%1 个模型保存失败，请查看控制台输出").arg(failCount));
    return;
  }
  std::cout << "keship: 保存子世界完成 " << _chain.toStdString()
            << " -> " << dlg.saveDir().toStdString()
            << "（世界名 " << dlg.worldName().toStdString() << "）"
            << std::endl;
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_WorldSettings()
{
  WorldSettingsDialog dlg;
  if (dlg.exec() != QDialog::Accepted)
    return;

  gz::msgs::SphericalCoordinates req;
  req.set_latitude_deg(dlg.latitudeDeg());
  req.set_longitude_deg(dlg.longitudeDeg());
  req.set_elevation(dlg.elevation());
  req.set_heading_deg(dlg.headingDeg());

  gz::msgs::Boolean rep;
  bool result = false;
  bool ok = this->modelManager->GetNode().Request(
      "/world/keship_world/set_spherical_coordinates", req, 1000, rep, result);

  std::cout << "keship: 世界原点设置 (lat=" << req.latitude_deg()
            << ", lon=" << req.longitude_deg()
            << ", elev=" << req.elevation()
            << ", heading=" << req.heading_deg()
            << ") " << ((ok && result) ? "成功" : "失败") << std::endl;
}

void MenuPluginKeship::OnPauseToggle()
{
  SetSimulationPaused(!paused_);
}

bool MenuPluginKeship::SetSimulationPaused(bool _pause)
{
  std::string service = "/world/keship_world/control";
  gz::msgs::WorldControl req;
  gz::msgs::Boolean rep;
  bool result;
  req.set_pause(_pause);
  bool ok = this->modelManager->GetNode().Request(service, req, 500, rep, result);
  ok = ok && result && rep.data();
  if (ok)
  {
    paused_ = _pause;
    emit PausedChanged();
  }
  return ok;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_LoadModel()
{
  ///--- 第一步：选择模型文件（ks_model 列表 + 原始模型浏览双通道）
  SelectKsmDialog dlg(QString::fromStdString(keship::KeshipKsmDir()),
                      SidebarUrls());
  if (dlg.exec() != QDialog::Accepted)
  {
    std::cout << "keship未选择模型文件" << std::endl;
    return;
  }
  QString filePath;
  if (!dlg.selectedRawFile().isEmpty())
    filePath = dlg.selectedRawFile();
  else
    filePath = dlg.selectedFile();
  if (filePath.isEmpty())
  {
    std::cout << "keship未选择模型文件" << std::endl;
    return;
  }
  /// 检查模型文件名是否符合要求：文件名只能包含字母、数字、下划线
  QFileInfo fileInfo(filePath);                   // 获取文件信息
  QString fileNameBaseName = fileInfo.baseName(); // 提取文件名（不包含路径和后缀）
  QRegExp regex("[a-zA-Z0-9_]+$");
  if (!regex.exactMatch(fileNameBaseName))
  {
    QMessageBox::warning(nullptr, "错误", "文件名只能包含字母、数字、下划线");
    return;
  }

  /// 检测是否为 .ksm 文件
  std::string fileExt = boost::to_lower_copy(filePath.toStdString().substr(filePath.toStdString().find_last_of(".") + 1));

  if (fileExt == "ksm")
  {
    std::string baseSDF;
    keship::BuoyancyConfig buoyCfg;
    keship::ThrusterConfig thrustCfg;
    keship::HydroConfig hydroCfg;
    hydroCfg.linkName = "";
    keship::SensorConfig sensorCfg;
    /// ★ 2026-09-28（D2）：一并取出 ksm 里持久化的碰撞方式，用于**回填对话框**
    keship::CollisionMode ksmCollision = keship::CollisionMode::Mesh;
    if (!this->modelManager->ParseKSMFile(filePath.toStdString(), baseSDF, buoyCfg, thrustCfg, hydroCfg, sensorCfg,
                                          &ksmCollision))
    {
      QMessageBox::warning(nullptr, "错误", "无法解析 .ksm 文件");
      return;
    }

    ModelSettingsDialog dialog(this->GetModelNames(), ModelSettingsDialog::Ksm);

    /// 从解析数据填入对话框（仅展示，不可编辑）
    {
      bool isStatic = false;
      keship::ModelManager::ParseStaticFromSDF(baseSDF, isStatic);
      dialog.setStatic(isStatic);

      dialog.setBuoyancy(!buoyCfg.bodies.empty());

      keship::InertiaParams ip;
      if (keship::ModelManager::ParseInertiaFromSDF(baseSDF, ip))
        dialog.setInertiaValues(ip.mass, ip.ixx, ip.ixy, ip.ixz, ip.iyy, ip.iyz, ip.izz);

      dialog.setCogValues(ip.cogX, ip.cogY, ip.cogZ);
      /// ★ 2026-09-28（D2）：把 ksm 里存的碰撞方式**回填**，让用户看到的就是事实
      dialog.setCollisionMode(ksmCollision);
    }

    if (dialog.exec() != QDialog::Accepted)
    {
      std::cout << "keship 取消加载模型" << std::endl;
      return;
    }

    std::string instanceName = dialog.getModelName().toStdString();
    double ksmPos[6];
    ksmPos[0] = dialog.getPosX(); ksmPos[1] = dialog.getPosY(); ksmPos[2] = dialog.getPosZ();
    ksmPos[3] = dialog.getRoll() * M_PI / 180.0;
    ksmPos[4] = dialog.getPitch() * M_PI / 180.0;
    ksmPos[5] = dialog.getYaw() * M_PI / 180.0;

    std::cout << "keship: KSM 载入位置 ksmPos = ["
              << ksmPos[0] << ", " << ksmPos[1] << ", " << ksmPos[2] << ", "
              << ksmPos[3] << ", " << ksmPos[4] << ", " << ksmPos[5] << "]" << std::endl;

    /// ★ 2026-09-24：加载流程**下沉到 ModelManager::LoadModelFromKsmFile**，
    /// 与无头入口 `keship_ctl model load-ksm` **共用同一份实现** ——
    /// 这样「无头验证」验的就是 GUI 真正走的那条路（本次改造的目的）。
    ///
    /// 取舍：baseSDF 会被解析两次（上面那次只供对话框展示）——
    /// 代价是重读一个几 KB 的文本文件（可忽略），换来的是两条入口**零漂移**。
    /// ⚠ 不要为了省这次解析而在这里重写加载逻辑 —— 那正是 P0-1 的成因模式。
    /// ★ 2026-09-28：碰撞体积计算方式的**生效口径**（D2 起）——
    ///   对话框的值**已从 ksm 回填**（见上）；用户改过就以用户为准，没改就是 ksm 里的值。
    ///   故把对话框的值作为**显式覆盖**传下去（口径唯一，不再分两处写）。
    const keship::CollisionMode chosenColl = dialog.getCollisionMode();
    if (!this->modelManager->LoadModelFromKsmFile(instanceName, filePath.toStdString(), ksmPos,
                                                  "", &chosenColl))
    {
      QMessageBox::warning(nullptr, "错误", "加载 .ksm 模型失败");
      return;
    }
    NotifyModelListChanged();
    std::cout << "keship: 成功加载 .ksm 模型: " << instanceName << std::endl;
    return;
  }

  ///
  std::cout << "keship选择的模型文件路径: " << filePath.toStdString() << std::endl;

  std::string instanceName = "";
  double pos[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  ///--- 第二步：弹出设置窗口
  ModelSettingsDialog dialog(this->GetModelNames());
  {
    Buoyancy3DModelInfo mi;
    mi.meshPath = filePath.toStdString();
    mi.isBox = false;
    dialog.setModelInfo(mi);
    dialog.setShowCoGPreview(true);
  }
  if (dialog.exec() != QDialog::Accepted)
  {
    std::cout << "keship 取消加载模型" << std::endl;
    return;
  }
  // 从文件路径中提取实例名称，并且去掉文件后缀
  // instanceName = filePath.toStdString().substr(filePath.toStdString().find_last_of("/") + 1, filePath.toStdString().find_last_of(".") - filePath.toStdString().find_last_of("/") - 1);
  instanceName = dialog.getModelName().toStdString();

  // 位姿
  pos[0] = dialog.getPosX();
  pos[1] = dialog.getPosY();
  pos[2] = dialog.getPosZ();
  pos[3] = dialog.getRoll() * M_PI / 180.0;
  pos[4] = dialog.getPitch() * M_PI / 180.0;
  pos[5] = dialog.getYaw() * M_PI / 180.0;

  bool isStatic = dialog.isStatic();
  bool isAddBuoyancy = dialog.isAddBuoyancy();

  /// ★ 2026-09-28：碰撞体积计算方式**加载时可选**（张老师要求）。
  ///   写在此处即可覆盖下面**两条** spawn 分支（勾浮力走编辑器链 / 不勾直接 spawn）——
  ///   因为 `CreateEntityViaService` 是按**实例名**读该缓存，与走哪条分支无关。
  this->modelManager->SetCollisionMode(instanceName, dialog.getCollisionMode());

  ///--- 第三步：勾选浮力时不 spawn，先打开编辑器配置代理几何体
  if (isAddBuoyancy)
  {
    /// 预缓存 base SDF + 空浮力体配置（模型尚未 spawn）
    bool addInertia = dialog.isAddInertia();
    double im = addInertia ? dialog.getInertiaMass() : 0;
    auto it = addInertia ? dialog.getInertiaTensor() : std::vector<double>{};
    bool valid = (it.size() >= 9);
    keship::InertiaParams ip;
    ip.mass = im;
    if (valid) {
      ip.ixx = it[0]; ip.ixy = it[1]; ip.ixz = it[2];
      ip.iyy = it[4]; ip.iyz = it[5]; ip.izz = it[8];
    }
    ip.cogX = dialog.getCogX();
    ip.cogY = dialog.getCogY();
    ip.cogZ = dialog.getCogZ();
    std::string baseSDF = this->modelManager->MakeSDFStringFromValidFileType(
        instanceName, filePath.toStdString(), isStatic, false,
        addInertia, ip);
    this->modelManager->CacheModelSDF(instanceName, baseSDF);
    keship::BuoyancyConfig cfg;
    cfg.linkName = "base_link";
    this->modelManager->CacheBuoyancyConfig(instanceName, cfg);

    /// 提取 mesh 路径给 3D 预览
    Buoyancy3DModelInfo mi;
    mi.meshPath = filePath.toStdString();

    /// 打开浮力体编辑器 → 流体动力编辑器 → 推进器编辑器 → spawn
    auto spawnPos = std::make_shared<std::array<double, 6>>();
    for (int i = 0; i < 6; i++) (*spawnPos)[i] = pos[i];

    auto *dialog = new BuoyancyEditorDialog(
        instanceName, "base_link", this->modelManager->GetNode(),
        mi, "keship_world");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setConfig(cfg);
    dialog->setCreateMode();

    QObject::connect(dialog, &QDialog::accepted, this,
        [this, dialog, name = instanceName, spawnPos]() {
      keship::BuoyancyConfig buoyCfg = dialog->getConfig();
      this->modelManager->CacheBuoyancyConfig(name, buoyCfg);

      auto *hydroDialog = new HydrodynamicsEditorDialog(name, "base_link");
      hydroDialog->setConfig(keship::HydroConfig{});
      hydroDialog->setAttribute(Qt::WA_DeleteOnClose);

      QObject::connect(hydroDialog, &QDialog::accepted, this,
          [this, hydroDialog, buoyCfg, name, spawnPos]() {
        keship::HydroConfig hydroCfg = hydroDialog->getConfig();
        this->modelManager->CacheHydroConfig(name, hydroCfg);
        this->modelManager->SpawnModelWithBuoyancy(name, buoyCfg,
            spawnPos->data());
        // 登记来源：原名 = 用户输入的实例名（保存时用用户命名）
        {
          keship::ModelManager::WorldModelSource src;
          src.baseName = name;
          this->modelManager->SetModelSource(name, src);
        }
        NotifyModelListChanged();
      });

      hydroDialog->show();
    });

    dialog->show();
    return;
  }

  ///--- 不勾选浮力：直接 spawn
  bool success = this->modelManager->SpawnModelInGazeboFromFile(
      instanceName, filePath.toStdString(), pos, isStatic, false);
  if (success)
  {
    std::cout << "keship成功加载模型：" << instanceName << std::endl;
    // 登记来源：原名 = 用户输入的实例名（保存时用用户命名）
    {
      keship::ModelManager::WorldModelSource src;
      src.baseName = instanceName;
      this->modelManager->SetModelSource(instanceName, src);
    }
    NotifyModelListChanged();
  }
  else
  {
    std::cout << "keship失败加载模型：" << instanceName << std::endl;
    std::cout.flush();
    return;
  }
  


}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_LoadShape(const QString &_shapeType)
{
  std::cout << "OnBtn_LoadShape: keship加载形状 " << _shapeType.toStdString() << std::endl;
  // TODO
}


/////////////////////////////////////////////////
/// 说明（2026-09-18）：搜索实际由 QML 侧完成（treeFilter → buildRows() 过滤），
/// 本槽**当前未被 QML 调用** —— 原先 QML 调用它却毫无作用，反而让人误以为
/// "搜索已实现"，故已移除该调用。此处保留本槽，作为将来需要跨层检索
/// （如按 KSM 属性搜索、命中结果跳转）时的接入点；若无此需求，
/// 可连同 .hh 中的声明一并删除。
void MenuPluginKeship::OnSearchModel(const QString &_searchText)
{
  std::cout << "keship搜索模型（预留，当前 QML 未调用）："
            << _searchText.toStdString() << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_DeleteModelByName(const QString &_instanceName)
{
  std::cout << "TODO: keship删除模型文件" << _instanceName.toStdString() << std::endl;
  /// 删除操作二次确认（2026-09-17 布局评审，下同）
  if (QMessageBox::question(nullptr, "删除模型",
      QString("确定删除模型「%1」吗？").arg(_instanceName),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    return;
  bool success = this->modelManager->DeleteModelInGazeboByName(_instanceName.toStdString());
  if (success)
  {
    std::cout << "keship成功删除模型：" << _instanceName.toStdString() << std::endl;
    NotifyModelListChanged(); // 发送信号通知QML更新
  }
  else
  {
    std::cout << "keship失败删除模型：" << _instanceName.toStdString() << std::endl;
  }
}

void MenuPluginKeship::OnBtn_DeleteModelByIndex(int _index)
{
  std::cout << "TODO: keship删除选中模型" << _index << std::endl;
  if (_index < 0 || _index >= this->modelManager->GetModelList().size())
  {
    std::cout << "keship删除选中模型索引无效" << _index << std::endl;
    return;
  }
  /// 删除操作二次确认：按索引取实例名提示
  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (QMessageBox::question(nullptr, "删除模型",
      QString("确定删除模型「%1」吗？").arg(QString::fromStdString(nameAndId.first)),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    return;
  bool success = this->modelManager->DeleteModelInGazeboByIndex(_index);
  if (success)
  {
    std::cout << "keship成功删除模型：" << _index << std::endl;
    NotifyModelListChanged();
  }
  else
  {
    std::cout << "keship失败删除模型：" << _index << std::endl;
  }
}

void MenuPluginKeship::OnBtn_DeleteAllModels()
{
  /// 删除操作二次确认：全部清场属高危操作
  /// ⚠ 文案必须与实际行为一致 —— 2026-09-28 起本操作**会**删水域与子世界结构，
  ///   旧文案写的是"不影响水域与子世界结构"（已失效）。
  if (QMessageBox::question(nullptr, "清空场景",
      "确定清空场景吗？将删除：\n"
      "  · 全部模型\n"
      "  · 全部水域（含其波浪/风/流参数与 3D 水面）\n"
      "  · 子世界结构（模型/水域的来源链）\n"
      "world 自带的太阳与地面不受影响。",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    return;

  /// ★ 2026-09-28 新语义（张老师 2026-09-25 拍板）：
  ///   「delete-all 应当删除所有模型、水域、子世界」
  ///
  /// ① **模型** —— `DeleteAllModelsInGazebo` 已改为**枚举活动场景**
  ///    （旧实现只遍历本进程 modelList ⇒ 会漏删/假成功），
  ///    并排除 world 自带实体 `sun` / `ground_plane` 与水面 `waves_*`
  this->modelManager->DeleteAllModelsInGazebo();

  /// ② **水域** —— 内存三件套（waterAreaMgr_ / customWaterAreas_）+ 3D 水面 + 水域 JSON。
  ///    `RebuildWaterAreas()` = 写空 JSON + `SpawnAll(空列表)`（⇒ 删掉全部 waves_*）+ 通知 QML
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    waterAreaMgr_.Clear();
  }
  customWaterAreas_.clear();
  RebuildWaterAreas();

  /// ③ **子世界** —— 模型/水域的**来源链**元数据（成员已删光，链已无意义）
  this->modelManager->ClearModelSources();
  this->modelManager->ClearWaterSources();

  NotifyModelListChanged(); // 发送信号通知QML更新
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_ShowModelSettingsDialogByIndex(int _index)
{
  std::cout << "TODO: keship显示模型属性对话框" << _index << std::endl;
  if (_index < 0 || _index >= this->modelManager->GetModelList().size())
  {
    std::cout << "keship显示模型属性对话框索引无效" << _index << std::endl;
    return;
  }
  /// 获取模型名称
  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (nameAndId.first == "")
    return;
  /// 获取模型位姿
  double pose_[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  bool success = this->modelManager->GetModelPoseByIDFromGazebo(nameAndId.second, pose_);
  if (!success)
  {
    std::cout << "keship获取模型位姿失败" << nameAndId.first << std::endl;
    return;
  }

  // double pose_[6] = {pose.position().x(), pose.position().y(), pose.position().z(), pose.orientation().x(), pose.orientation().y(), pose.orientation().z()};
  // QString a = QString::fromStdString(instanceName);
  ModelSettingsDialog dialog(QString::fromStdString(nameAndId.first), pose_);

  /// 从缓存还原静态标记
  bool isStatic = false;
  modelManager->GetCachedModelStatic(nameAndId.first, isStatic);
  dialog.setStatic(isStatic);

  /// ★ 2026-09-28：回填当前碰撞体积计算方式（加载后编辑）
  dialog.setCollisionMode(modelManager->GetCollisionMode(nameAndId.first));

  /// 从缓存 SDF 回填惯性到对话框
  keship::InertiaParams ip;
  this->modelManager->GetCachedModelInertia(nameAndId.first, ip);
  if (ip.mass > 0)
  {
    dialog.setInertiaValues(ip.mass, ip.ixx, ip.ixy, ip.ixz, ip.iyy, ip.iyz, ip.izz);
    dialog.setCogValues(ip.cogX, ip.cogY, ip.cogZ);
  }

  {
    keship::ModelManager::ModelGeometryType gt;
    double dx, dy, dz, r, l;
    std::string meshPath;
    if (modelManager->GetCachedModelGeometry(nameAndId.first, gt, dx, dy, dz, r, l, meshPath)
        && !meshPath.empty())
    {
      Buoyancy3DModelInfo mi;
      mi.meshPath = meshPath;
      mi.isBox = false;
      dialog.setModelInfo(mi);
    }
  }

    const bool wasPaused = paused_;

  {
    gz::msgs::WorldControl req;
    gz::msgs::Boolean rep; bool result;
    req.set_pause(true);
    modelManager->GetNode().Request("/world/keship_world/control", req, 500, rep, result);
  }

  if (dialog.exec() != QDialog::Accepted)
  {
    std::cout << "keship 取消修改模型位姿" << std::endl;
    SetSimulationPaused(wasPaused);
    return;
  }

  pose_[0] = dialog.getPosX();
  pose_[1] = dialog.getPosY();
  pose_[2] = dialog.getPosZ();
  pose_[3] = dialog.getRoll() * M_PI / 180.0;
  pose_[4] = dialog.getPitch() * M_PI / 180.0;
  pose_[5] = dialog.getYaw() * M_PI / 180.0;

  double newMass = dialog.getInertiaMass();
  auto newTensor = dialog.getInertiaTensor();
  double newCogX = dialog.getCogX(), newCogY = dialog.getCogY(), newCogZ = dialog.getCogZ();
  bool inertiaChanged = (std::abs(newMass - ip.mass) > 1e-6 ||
      (newTensor.size() >= 9 && (
       std::abs(newTensor[0]-ip.ixx)>1e-6 || std::abs(newTensor[1]-ip.ixy)>1e-6 ||
       std::abs(newTensor[2]-ip.ixz)>1e-6 || std::abs(newTensor[4]-ip.iyy)>1e-6 ||
       std::abs(newTensor[5]-ip.iyz)>1e-6 || std::abs(newTensor[8]-ip.izz)>1e-6)) ||
      std::abs(newCogX - ip.cogX) > 1e-6 ||
      std::abs(newCogY - ip.cogY) > 1e-6 ||
      std::abs(newCogZ - ip.cogZ) > 1e-6);

  /// ★ 2026-09-28：碰撞体积计算方式**加载后也可改**（张老师要求）。
  const keship::CollisionMode newColl = dialog.getCollisionMode();
  const bool collisionChanged =
      (newColl != modelManager->GetCollisionMode(nameAndId.first));

  if (inertiaChanged || collisionChanged)
  {
    if (collisionChanged)
      this->modelManager->SetCollisionMode(nameAndId.first, newColl);
    if (inertiaChanged)
    {
      keship::InertiaParams newIp;
      newIp.mass = newMass;
      newIp.ixx = newTensor[0]; newIp.ixy = newTensor[1]; newIp.ixz = newTensor[2];
      newIp.iyy = newTensor[4]; newIp.iyz = newTensor[5]; newIp.izz = newTensor[8];
      newIp.cogX = newCogX; newIp.cogY = newCogY; newIp.cogZ = newCogZ;
      this->modelManager->UpdateCachedSDFInertia(nameAndId.first, newIp);
    }
    /// 全插件重建（保留 buoyancy/thrust/hydro）。
    /// ⚠ 重建会**删除并重建实体** ⇒ 旧 id 失效，且重建内部只沿用**场景里的旧位姿**。
    ///   因此重建后必须**按名字重新取 id、并把对话框里的位姿再落一次** ——
    ///   否则"同时改了位姿与惯性/碰撞"时位姿编辑会被静默丢弃（既有缺陷，本次一并修）。
    this->modelManager->RebuildModelWithAllPlugins(nameAndId.first);
    uint32_t rebuiltId = 0;
    if (this->modelManager->GetModelIDByInstanceNameFromGazebo(nameAndId.first, rebuiltId))
    {
      if (!this->modelManager->SetModelPoseByIDInGazebo(rebuiltId, pose_))
        std::cout << "keship重建后设置模型位姿失败" << nameAndId.first << std::endl;
    }
    NotifyModelListChanged();
  }
  else
  {
    bool poseOk = this->modelManager->SetModelPoseByIDInGazebo(nameAndId.second, pose_);
    if (!poseOk)
    {
      std::cout << "keship设置模型位姿失败" << nameAndId.first << std::endl;
    }
  }

  SetSimulationPaused(wasPaused);

  std::cout << "keship成功处理模型属性：" << nameAndId.first << std::endl;
}

/**
 * @brief 获取模型列表
 *
 * @return QStringList 模型名称列表
 */
// QStringList MenuPluginKeship::GetModelNames() const
// {
//   std::cout << "TODO: keship获取模型列表" << std::endl;
//   return this->modelListModel;
// }
// void MenuPluginKeship::ModelNamesChanged()
// {
//   std::cout << "TODO: keship模型列表改变" << std::endl;
// }

//////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief 在Gazebo中生成模型
 *
 * @param _worldName 世界名称
 * @param _instanceName 实例名称
 * @param _filePath 模型文件路径
 * @return true 成功
 * @return false 失败
 */
// bool MenuPluginKeship::SpawnModelInGazeboFromFile(
//     const std::string &_worldName,
//     const std::string &_instanceName,
//     const std::string &_filePath,
//     double _pos[6])
// {
//   // 准备服务调用
//   std::string service = "/world/" + _worldName + "/create";

//   // 创建请求消息
//   gz::msgs::EntityFactory msg;

//   // 设置EntityFactory
//   msg.set_name(_instanceName); // 实例名称（可以与模型名称不同）
//   msg.set_allow_renaming(true);

//   auto pose = msg.mutable_pose();
//   pose->mutable_position()->set_x(_pos[0]);
//   pose->mutable_position()->set_y(_pos[1]);
//   pose->mutable_position()->set_z(_pos[2]);
//   pose->mutable_orientation()->set_x(_pos[3]);
//   pose->mutable_orientation()->set_y(_pos[4]);
//   pose->mutable_orientation()->set_z(_pos[5]);

//   msg.set_sdf_filename("file://" + _filePath); // 使用文件路径

//   // 调用服务（同步方式）
//   gz::msgs::Boolean rep;
//   bool result;
//   unsigned int timeout = 2000; // 2秒超时

//   std::cout << "正在调用服务: " << service << std::endl;

//   bool executed = this->node->Request(service, msg, timeout, rep, result);
//   if (executed)
//     std::cout << "服务调用成功" << std::endl;
//   else
//     std::cout << "服务调用失败" << std::endl;

//   return executed;
// }

// bool MenuPluginKeship::SpawnModelInGazeboFromSDF(
//     const std::string &_worldName,
//     const std::string &_instanceName,
//     const std::string &_sdfContent = "")
// {
//   // 准备服务调用
//   std::string service = "/world/" + _worldName + "/create";

//   // 创建请求消息
//   gz::msgs::EntityFactory msg;

//   // 设置实例名称
//   msg.set_name(_instanceName); // 实例名称（可以与模型名称不同）
//   msg.set_allow_renaming(true);
//   //
//   if (_sdfContent.empty())
//   {
//     std::string sdfContent = "<?xml version=\"1.0\"?><sdf version=\"1.9\"><model name=\"HHHHH\"><pose>0 0 1 0 0 0</pose><link name=\"link\"><inertial><mass>1.0</mass></inertial><collision name=\"collision\"><geometry><box><size>1 1 1</size></box></geometry></collision><visual name=\"visual\"><geometry><box><size>100 100 100</size></box></geometry></visual></link></model></sdf>";
//     msg.set_sdf(sdfContent); // 使用SDF字符串
//   }
//   else
//   {
//     msg.set_sdf(_sdfContent); // 使用SDF字符串
//   }

//   // 调用服务（同步方式）
//   gz::msgs::Boolean rep;
//   bool result;
//   unsigned int timeout = 2000; // 2秒超时

//   std::cout << "正在调用服务: " << service << std::endl;

//   bool executed = this->node->Request(service, msg, timeout, rep, result);
//   if (executed)
//     std::cout << "服务调用成功" << std::endl;
//   else
//     std::cout << "服务调用失败" << std::endl;

//   return executed;
// }

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_CylinderBuoyancyTest()
{
  std::cout << "keship: 圆柱体浮力测试" << std::endl;

  CylinderTestDialog dialog(this->GetModelNames());
  if (dialog.exec() != QDialog::Accepted)
  {
    std::cout << "keship: 取消圆柱体浮力测试" << std::endl;
    return;
  }

  std::string instanceName = dialog.getInstanceName().toStdString();
  double diameter = dialog.getDiameter();
  double length = dialog.getLength();
  double density = dialog.getDensity();

  double pos[6];
  pos[0] = dialog.getPosX();
  pos[1] = dialog.getPosY();
  pos[2] = dialog.getPosZ();
  pos[3] = dialog.getRoll() * M_PI / 180.0;
  pos[4] = dialog.getPitch() * M_PI / 180.0;
  pos[5] = dialog.getYaw() * M_PI / 180.0;

  /// 获取阻尼模式选择
  std::string dampingMode = dialog.getDampingMode().toStdString();

  /// 仅缓存 SDF+浮力体配置，不 spawn（等用户配完 hydro 再生成）
  bool cached = this->modelManager->SpawnCylinderInGazebo(
      instanceName, diameter, length, density, pos, /*_skipSpawn=*/true);

  if (!cached)
  {
    std::cout << "keship: 圆柱体 SDF 缓存失败" << std::endl;
    return;
  }

  if (dampingMode == "fossen")
  {
    /// 弹出流体动力编辑器，预填圆柱体经验系数
    auto *hydroDialog = new HydrodynamicsEditorDialog(instanceName, "base_link");
    hydroDialog->setAttribute(Qt::WA_DeleteOnClose);
    double cylMass = density * M_PI * (diameter*0.5) * (diameter*0.5) * length;
    hydroDialog->setConfig(
        HydrodynamicsEditorDialog::cylinderDefaults(cylMass, diameter*0.5, length));

    auto spawnPos = std::make_shared<std::array<double, 6>>();
    std::copy(pos, pos + 6, spawnPos->begin());

    QObject::connect(hydroDialog, &QDialog::accepted, this,
        [this, hydroDialog, name = instanceName, spawnPos]() {
      keship::HydroConfig hydroCfg = hydroDialog->getConfig();
      hydroCfg.dampingMode = "fossen";
      this->modelManager->CacheHydroConfig(name, hydroCfg);

      keship::BuoyancyConfig buoyCfg;
      this->modelManager->GetCachedBuoyancyConfig(name, buoyCfg);
      this->modelManager->SpawnModelWithBuoyancy(name, buoyCfg, spawnPos->data());
      NotifyModelListChanged();
    });

    hydroDialog->show();
  }
  else
  {
    /// Pose-Based 模式：跳过 HydroEditor，直接 spawn
    keship::HydroConfig hydroCfg;
    hydroCfg.linkName = "base_link";
    hydroCfg.dampingMode = "pose_based";
    /// 补 M_A（物理附加质量，与 fossen 一致）：DART <fluid_added_mass>
    /// 隐式注入，使 pose-based 瞬态（推力响应/倾倒转动）物理完整
    double cylMass = density * M_PI * (diameter*0.5) * (diameter*0.5) * length;
    hydroCfg.addedMass = HydrodynamicsEditorDialog::cylinderAddedMass(
        cylMass, diameter * 0.5, length, 1000.0);
    this->modelManager->CacheHydroConfig(instanceName, hydroCfg);

    keship::BuoyancyConfig buoyCfg;
    this->modelManager->GetCachedBuoyancyConfig(instanceName, buoyCfg);
    this->modelManager->SpawnModelWithBuoyancy(instanceName, buoyCfg, pos);
    NotifyModelListChanged();
  }
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_SP02_Stability()
{
  if (sp02Dialog_)
  {
    sp02Dialog_->raise();
    sp02Dialog_->activateWindow();
    return;
  }

  auto *dialog = new StabilityExperimentDialog(
      modelManager->GetNode(), "keship_world");
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  QObject::connect(dialog, &QDialog::destroyed, this, [this]() {
    sp02Dialog_ = nullptr;
  });

  sp02Dialog_ = dialog;
  dialog->show();
  std::cout << "SP-02: 初稳性实验对话框已打开" << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_SP03_Inclining()
{
  if (sp03Dialog_)
  {
    sp03Dialog_->raise();
    sp03Dialog_->activateWindow();
    return;
  }

  auto *dialog = new IncliningExperimentDialog(
      modelManager->GetNode(), "keship_world");
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  QObject::connect(dialog, &QDialog::destroyed, this, [this]() {
    sp03Dialog_ = nullptr;
  });

  sp03Dialog_ = dialog;
  dialog->show();
  std::cout << "SP-03: 倾斜实验对话框已打开" << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_SP04_HullStability()
{
  if (sp04Dialog_) { sp04Dialog_->raise(); sp04Dialog_->activateWindow(); return; }
  auto *dialog = new HullStabilityDialog(modelManager->GetNode(), "keship_world");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(dialog, &QDialog::destroyed, this, [this]() { sp04Dialog_ = nullptr; });
  sp04Dialog_ = dialog; dialog->show();
  std::cout << "SP-04: 数学船型对话框已打开" << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_PR01_CodeRunner()
{
  auto *dlg = new CodeRunnerDialog();
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->show();
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_TaskSet()
{
  if (taskSetDialog_)
  {
    taskSetDialog_->raise();
    taskSetDialog_->activateWindow();
    return;
  }

  auto *dlg = new TaskSetDialog(modelManager->GetNode(), "keship_world");
  dlg->setAttribute(Qt::WA_DeleteOnClose);

  /// ★ 2026-09-23：注入场景模型足迹 —— 原先本对话框的地图**看不到任何模型预览**
  /// （`GatherSceneFootprints` 只在水域对话框被调用）。张老师反馈后补齐。
  dlg->setSceneModels(GatherSceneFootprints());

  QObject::connect(dlg, &QDialog::destroyed, this, [this]() {
    taskSetDialog_ = nullptr;
  });

  taskSetDialog_ = dlg;
  dlg->show();
  std::cout << "Phase 10: 任务设定对话框已打开" << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_TaskMonitor()
{
  if (taskMonitorDialog_)
  {
    taskMonitorDialog_->raise();
    taskMonitorDialog_->activateWindow();
    return;
  }

  auto *dlg = new TaskMonitorDialog(modelManager->GetNode(), "keship_world", rosNode_);
  dlg->setAttribute(Qt::WA_DeleteOnClose);

  // 填充非静态模型列表 + 传递推进器信息
  for (const auto &m : modelManager->GetModelList()) {
    bool isStatic = false;
    modelManager->GetCachedModelStatic(m.first, isStatic);
    if (!isStatic) {
      dlg->addModel(QString::fromStdString(m.first));

      // 传递推进器信息（从 ModelManager 缓存读取）
      keship::ThrusterConfig thrCfg;
      if (modelManager->GetCachedThrusterConfig(m.first, thrCfg)) {
        std::vector<std::string> names;
        double totalMax = 0;
        for (auto &b : thrCfg.bodies) {
          names.push_back(b.name);
          totalMax += b.maxForce;
        }
        dlg->setThrusterInfo(m.first, names, totalMax);
      }
    }
  }

  QObject::connect(dlg, &QDialog::destroyed, this, [this]() {
    taskMonitorDialog_ = nullptr;
  });

  taskMonitorDialog_ = dlg;
  dlg->show();
  std::cout << "Phase 10: 任务监控面板已打开" << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_Displacement()
{
  auto *dlg = new DisplacementVerifyDialog(modelManager->GetNode(), "keship_world");
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->show();
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_NewMathHull()
{
  auto *dialog = new ShipHullDialog();
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(dialog, &ShipHullDialog::spawnRequested, this,
    [this](keship::ShipHullSpec spec, std::string name) {
      // 去重
      int suffix = 1;
      std::string finalName = name;
      for (const auto &m : modelManager->GetModelList())
        if (m.first == finalName) { finalName = name + "_" + std::to_string(++suffix); }

      double pos[6] = {0, 0, spec.D / 2.0 + 0.1, 0, 0, 0};
      if (modelManager->SpawnMathHull(finalName, spec, pos)) {
        std::cout << "Phase B: 数学船型 " << finalName << " 已生成" << std::endl;
        mathHullNames_.insert(finalName);
        NotifyModelListChanged();
      } else
        std::cerr << "Phase B: 数学船型生成失败" << std::endl;
    });
  dialog->show();
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_EditBuoyancy(int _index)
{
  std::cout << "keship: 编辑浮力体 index=" << _index << std::endl;

  if (_index < 0 || _index >= (int)this->modelManager->GetModelList().size())
  {
    std::cout << "keship: 编辑浮力体索引无效" << std::endl;
    return;
  }

  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (nameAndId.first.empty())
    return;

  QString key = "Buoyancy/" + QString::fromStdString(nameAndId.first);
  auto *dialog = this->acquireEditor(key, [this, nameAndId]() -> QDialog * {
    Buoyancy3DModelInfo modelInfo;
    {
      keship::ModelManager::ModelGeometryType type;
      this->modelManager->GetCachedModelGeometry(nameAndId.first,
          type, modelInfo.boxDx, modelInfo.boxDy, modelInfo.boxDz,
          modelInfo.cylRadius, modelInfo.cylLength, modelInfo.meshPath);
      modelInfo.isBox = (type == keship::ModelManager::ModelGeometryType::Box);
    }

    auto *d = new BuoyancyEditorDialog(
        nameAndId.first, "base_link", this->modelManager->GetNode(),
        modelInfo, "keship_world");

    /// 从缓存还原已有浮力体配置
    keship::BuoyancyConfig existingCfg;
    if (this->modelManager->GetCachedBuoyancyConfig(nameAndId.first, existingCfg))
      d->setConfig(existingCfg);

    QObject::connect(d, &QDialog::accepted, this,
        [this, d, name = nameAndId.first]() {
      keship::BuoyancyConfig config = d->getConfig();
      if (!this->modelManager->RebuildModelWithBuoyancy(name, config))
        QMessageBox::warning(nullptr, "重建失败", "模型重建失败，请重试");
      NotifyModelListChanged();
    });
    return d;
  });
  if (dialog)
    dialog->show();
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_EditThrust(int _index)
{
  std::cout << "keship: 编辑推进器 index=" << _index << std::endl;

  if (_index < 0 || _index >= (int)this->modelManager->GetModelList().size())
  {
    std::cout << "keship: 编辑推进器索引无效" << std::endl;
    return;
  }

  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (nameAndId.first.empty())
    return;

  QString key = "Thrust/" + QString::fromStdString(nameAndId.first);
  auto *dialog = this->acquireEditor(key, [this, nameAndId]() -> QDialog * {
    Buoyancy3DModelInfo modelInfo;
    {
      keship::ModelManager::ModelGeometryType type;
      this->modelManager->GetCachedModelGeometry(nameAndId.first,
          type, modelInfo.boxDx, modelInfo.boxDy, modelInfo.boxDz,
          modelInfo.cylRadius, modelInfo.cylLength, modelInfo.meshPath);
      modelInfo.isBox = (type == keship::ModelManager::ModelGeometryType::Box);
    }

    auto *d = new ThrusterEditorDialog(
        nameAndId.first, "base_link", this->modelManager->GetNode(),
        modelInfo, "keship_world");

    /// 从缓存还原已有推进器配置
    keship::ThrusterConfig existingCfg;
    if (this->modelManager->GetCachedThrusterConfig(nameAndId.first, existingCfg))
      d->setConfig(existingCfg);

    QObject::connect(d, &QDialog::accepted, this,
        [this, d, name = nameAndId.first]() {
      keship::ThrusterConfig config = d->getConfig();
      if (!this->modelManager->RebuildModelWithThrust(name, config))
        QMessageBox::warning(nullptr, "重建失败", "模型重建失败，请重试");
      NotifyModelListChanged();
    });
    return d;
  });
  if (dialog)
    dialog->show();
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_SaveModel(int _index)
{
  std::cout << "keship: 保存模型 index=" << _index << std::endl;

  if (_index < 0 || _index >= (int)this->modelManager->GetModelList().size())
  {
    std::cout << "keship: 保存模型索引无效" << std::endl;
    return;
  }

  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (nameAndId.first.empty())
    return;

  QString defaultName = QString::fromStdString(nameAndId.first) + ".ksm";
  QFileDialog dlg(nullptr, "保存 Keship 模型",
                  QString::fromStdString(keship::KeshipKsmDir()) + "/" + defaultName,
                  "Keship Model Files (*.ksm)");
  dlg.setFileMode(QFileDialog::AnyFile);
  dlg.setAcceptMode(QFileDialog::AcceptSave);
  SetupSidebarUrls(dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;
  QString filePath = dlg.selectedFiles().value(0);
  if (filePath.isEmpty())
    return;

  // 保存时把 SDF 内模型名还原为来源原名（无来源则原样）
  keship::ModelManager::WorldModelSource msrc;
  std::string sdfName;
  if (this->modelManager->GetModelSource(nameAndId.first, msrc))
    sdfName = msrc.baseName;
  bool ok = this->modelManager->SaveModelToKSM(
      nameAndId.first, filePath.toStdString(), sdfName);
  if (ok)
    std::cout << "keship: 模型已保存到 " << filePath.toStdString() << std::endl;
  else
    std::cerr << "keship: 保存模型失败" << std::endl;
}

/////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_EditHydro(int _index)
{
  std::cout << "keship: 编辑流体动力 index=" << _index << std::endl;

  if (_index < 0 || _index >= (int)this->modelManager->GetModelList().size())
  {
    std::cout << "keship: 编辑流体动力索引无效" << std::endl;
    return;
  }

  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (nameAndId.first.empty())
    return;

  QString key = "Hydro/" + QString::fromStdString(nameAndId.first);
  auto *dialog = this->acquireEditor(key, [this, nameAndId]() -> QDialog * {
    auto *d = new HydrodynamicsEditorDialog(
        nameAndId.first, "base_link");

    keship::HydroConfig existingCfg;
    if (this->modelManager->GetCachedHydroConfig(nameAndId.first, existingCfg))
      d->setConfig(existingCfg);
    else
      d->setConfig(keship::HydroConfig{});

    QObject::connect(d, &QDialog::accepted, this,
        [this, d, name = nameAndId.first]() {
      keship::HydroConfig config = d->getConfig();
      if (!this->modelManager->RebuildModelWithHydro(name, config))
        QMessageBox::warning(nullptr, "重建失败", "模型重建失败，请重试");
      NotifyModelListChanged();
    });
    return d;
  });
  if (dialog)
    dialog->show();
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::OnBtn_EditSensor(int _index)
{
  std::cout << "keship: 编辑传感器 index=" << _index << std::endl;

  if (_index < 0 || _index >= (int)this->modelManager->GetModelList().size())
  {
    std::cout << "keship: 编辑传感器索引无效" << std::endl;
    return;
  }

  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_index);
  if (nameAndId.first.empty())
    return;

  QString key = "Sensor/" + QString::fromStdString(nameAndId.first);
  auto *dialog = this->acquireEditor(key, [this, nameAndId]() -> QDialog * {
    Buoyancy3DModelInfo modelInfo;
    {
      keship::ModelManager::ModelGeometryType type;
      this->modelManager->GetCachedModelGeometry(nameAndId.first,
          type, modelInfo.boxDx, modelInfo.boxDy, modelInfo.boxDz,
          modelInfo.cylRadius, modelInfo.cylLength, modelInfo.meshPath);
      modelInfo.isBox = (type == keship::ModelManager::ModelGeometryType::Box);
    }

    auto *d = new SensorEditorDialog(
        QString::fromStdString(nameAndId.first), "base_link", modelInfo);

    keship::SensorConfig existingCfg;
    if (this->modelManager->GetCachedSensorConfig(nameAndId.first, existingCfg))
      d->setConfig(existingCfg);

    QObject::connect(d, &QDialog::accepted, this,
        [this, d, name = nameAndId.first]() {
      keship::SensorConfig config = d->getConfig();
      if (!this->modelManager->RebuildModelWithSensors(name, config))
        QMessageBox::warning(nullptr, "重建失败", "模型重建失败，请重试");
      NotifyModelListChanged();
    });
    return d;
  });
  if (dialog)
    dialog->show();
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::RefreshSensorList()
{
  QStringList list;
  for (auto &kv : this->modelManager->GetModelList())
  {
    keship::SensorConfig cfg;
    if (this->modelManager->GetCachedSensorConfig(kv.first, cfg))
    {
      for (const auto &inst : cfg.instances)
      {
        if (inst.type != keship::SensorType::Camera &&
            inst.type != keship::SensorType::Lidar)
          continue;
        QString typeLabel = (inst.type == keship::SensorType::Camera) ? "相机" : "激光雷达";
        list << QString::fromStdString(kv.first) + " / " + typeLabel
                  + " / " + QString::fromStdString(inst.name);
      }
    }
  }
  sensorList_ = list;
  emit SensorListChanged();
  CloseStaleViewers();
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::OnSensorSelected(const QString &_sensorId)
{
  if (_sensorId.isEmpty()) return;

  /// _sensorId 格式: "keship_01 / 相机 / front_camera"
  QStringList parts = _sensorId.split(" / ");
  if (parts.size() < 3) return;

  QString modelName = parts[0];
  QString typeLabel = parts[1];
  QString instName  = parts[2];

  openSensorView(modelName, typeLabel, instName);
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::openSensorView(const QString &_modelName,
                                       const QString &_typeLabel,
                                       const QString &_instName)
{
  QString key = _modelName + " / " + _typeLabel + " / " + _instName;

  /// 已有同传感器查看器打开则激活已有窗口
  auto it = openViewers_.find(key);
  if (it != openViewers_.end() && it->second)
  {
    it->second->raise();
    it->second->activateWindow();
    return;
  }

  QDialog *viewer = nullptr;
  if (_typeLabel == "相机")
    viewer = new CameraViewerDialog(rosNode_, _modelName, _instName, "camera");
  else if (_typeLabel == "激光雷达")
    viewer = new LidarViewerDialog(rosNode_, _modelName, _instName, "lidar");
  else
    return;

  viewer->setAttribute(Qt::WA_DeleteOnClose);
  openViewers_[key] = viewer;
  QObject::connect(viewer, &QDialog::destroyed, this, [this, key]() {
    openViewers_.erase(key);
  });
  viewer->show();
}

//////////////////////////////////////////////////////////////////////////////////
QDialog *MenuPluginKeship::acquireEditor(const QString &_key,
                                         std::function<QDialog *()> &&_factory)
{
  /// 已有同 key 窗口则置前，不重复创建
  auto it = editorDialogs_.find(_key);
  if (it != editorDialogs_.end() && it->second)
  {
    it->second->raise();
    it->second->activateWindow();
    return nullptr;
  }

  QDialog *dialog = _factory();
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  editorDialogs_[_key] = dialog;
  QObject::connect(dialog, &QDialog::destroyed, this, [this, _key]() {
    editorDialogs_.erase(_key);
  });
  return dialog;
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::CloseStaleViewers()
{
  /// 构建当前有效 sensor 的 key 集合
  std::set<QString> valid;
  for (const auto &s : sensorList_)
    valid.insert(s);

  /// 关闭不在有效集合中的查看器
  for (auto it = openViewers_.begin(); it != openViewers_.end(); )
  {
    if (valid.find(it->first) == valid.end())
    {
      if (it->second) it->second->close();
      it = openViewers_.erase(it);  // erase via iterator (destroyed signal may also fire)
    }
    else
    {
      ++it;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////
std::vector<keship::ModelFootprint> MenuPluginKeship::GatherSceneFootprints() const
{
  std::vector<keship::ModelFootprint> result;
  std::lock_guard<std::mutex> lock(modelPoseCacheMutex_);

  for (const auto &[name, bbox] : modelManager->GetBBoxCache())
  {
    if (name == "sun" || name == "ground_plane" ||
        name.rfind("waves_", 0) == 0)
      continue;

    /// ★ 2026-09-24 修 P0-2（张老师澄清的设计意图）：**2D 地图只预览静态模型** ——
    /// 动态模型在 2D 上互相遮挡，"不好确定谁在遮盖谁"，故一律不画。
    /// 注：`isStatic` 字段一直存在（include/keship/utils/WaterArea.hh:120），
    /// `CacheModelFootprint` 也一直在填，只是**从未被用于过滤**
    /// （`MapWidget` 的 4 处 `isStatic` 分支只用于配色）。
    if (!bbox.isStatic)
      continue;

    keship::ModelFootprint fp = bbox;
    auto it = modelPoseCache_.find(name);
    if (it != modelPoseCache_.end())
    {
      fp.worldX   = it->second.x;
      fp.worldY   = it->second.y;
      fp.worldYaw = it->second.yaw;
    }
    result.push_back(fp);
  }

  /// ★ 2026-09-23 诊断：地图实际会画哪些模型（张老师反馈 yanhu 不显示）
  {
    std::string names;
    for (const auto &f : result)
      names += f.name + "(" + std::to_string(f.localW) + "x" + std::to_string(f.localH) + ") ";
    std::cout << "[足迹诊断] 地图收到 " << result.size() << " 个模型: "
              << (names.empty() ? "(空)" : names) << std::endl;
  }
  return result;
}

//////////////////////////////////////////////////////////////////////////////////
void MenuPluginKeship::WriteWaterAreasJson(const std::string &_path)
{
  std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
  std::string json = keship::SerializeWaterAreas(waterAreaMgr_.Areas());
  std::ofstream f(_path);
  if (f.is_open()) f << json;
}

void MenuPluginKeship::RebuildWaterAreas()
{
  keship::EnsureKeshipDataDirs();
  WriteWaterAreasJson();
  waterVisualMgr_.SpawnAll(waterAreaMgr_.Areas(),
      modelManager->GetNode());
  NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_AddDefaultWaterArea()
{
  // 仅无水域时可用（UI 已禁用约束）；固定名"默认水域"，普通矩形水区。
  // 查重防御：预防快速双击/事件队列重复派发导致的同名多份（WaterArea 支持同名共存）
  for (const auto &a : waterAreaMgr_.Areas())
    if (a.name == "默认水域")
    {
      std::cerr << "OnBtn_AddDefaultWaterArea: 场景已有\"默认水域\"，拦截本次添加" << std::endl;
      return;
    }

  keship::WaterArea area;
  area.name = "默认水域";
  area.xMin = -250; area.xMax = 250;
  area.yMin = -250; area.yMax = 250;
  area.baseHeight = 0.0;
  /// 统一为点构成形状（矩形四角，与手动输入兜底顺序一致）：保证
  /// 编辑对话框可见可编辑、保存/载入 ksw 持久化后形状不退化
  area.vertices = {{-250,-250},{250,-250},{250,250},{-250,250}};
  /// ★ 2026-09-22 张老师要求：新增水域默认 **0 级海况（无风无浪无流）** ——
  /// 教学实验一上来就是干净的静水基线，不必再手动"设静水"。
  /// 画面上的"微微起伏"由 WaveVisual 的**纯渲染细节层活跃度**负责，不参与物理。
  /// ParamsFromLevel(0) 给出 gain=0 / 风速=0（周期取 TpFromHs(0)=5.0 占位）。
  keship::SeaState::ParamsFromLevel(0, &area.waveParams.period,
                                    &area.windParams.speed,
                                    &area.waveParams.gain);
  area.currentParams.speed = 0.0;      ///< 流速显式置 0（无流）
  customWaterAreas_.push_back(area);
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    waterAreaMgr_.AddArea(area);
    RebuildWaterAreas();
  }
  PublishWaveParams(area.waveParams, area.name);
}

void MenuPluginKeship::OnBtn_AddWaterArea()
{
  WaterAreaDrawDialog dlg;
  dlg.setDefaultName(
      QString::fromStdString("水域" + std::to_string(customWaterAreas_.size() + 1)));
  modelManager->EnsureFootprintOutlines(keship::ModelRenderScheme::ProjectedImage);
  dlg.setSceneModels(GatherSceneFootprints());
  // 新增时展示场景已有全部水域作为只读参考（便于定位/参考既有水域）
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    dlg.setReferenceWaterAreas(waterAreaMgr_.Areas());
  }
  if (dlg.exec() != QDialog::Accepted)
    return;

  keship::WaterArea area = dlg.getWaterArea();

  // 新添加水区与既有水区共存（不清空既有水区）
  customWaterAreas_.push_back(area);
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    waterAreaMgr_.AddArea(area);
    RebuildWaterAreas();
  }
  PublishWaveParams(area.waveParams, area.name);
}

void MenuPluginKeship::OnBtn_ShowWaterEnv(int _index)
{
  std::vector<keship::WaterArea> areas;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    areas = waterAreaMgr_.Areas();
  }
  if (_index < 0 || _index >= (int)areas.size())
    return;

  EnvironmentParamsDialog dlg(areas[_index]);
  if (dlg.exec() != QDialog::Accepted) return;
  auto updated = dlg.getWaterArea();
  const std::string victim = areas[_index].name;

  // customs 同名同步（保持与 Areas 一致）
  for (auto &ca : customWaterAreas_)
    if (ca.name == victim) ca = updated;

  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    auto all = waterAreaMgr_.Areas();
    waterAreaMgr_.Clear();
    for (auto &a : all)
      waterAreaMgr_.AddArea(a.name == victim ? updated : a);
    RebuildWaterAreas();
  }
  PublishWaveParams(updated.waveParams, updated.name);
}

void MenuPluginKeship::OnBtn_EditWaterArea(int _index)
{
  std::vector<keship::WaterArea> areas;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    areas = waterAreaMgr_.Areas();
  }
  if (_index < 0 || _index >= (int)areas.size())
    return;

  // 按名字映射回 customs（Areas/customs 索引可能错位）
  int ci = -1;
  for (size_t i = 0; i < customWaterAreas_.size(); ++i)
    if (customWaterAreas_[i].name == areas[_index].name) { ci = (int)i; break; }
  if (ci < 0) return;

  WaterAreaDrawDialog dlg;
  dlg.setDefaultName(QString::fromStdString(customWaterAreas_[ci].name));
  // 注入被编辑水域完整数据：几何可用/可编辑；风浪流参数保留不被重置信重置
  dlg.setInitialArea(customWaterAreas_[ci]);
  modelManager->EnsureFootprintOutlines(keship::ModelRenderScheme::ProjectedImage);
  dlg.setSceneModels(GatherSceneFootprints());
  // 统一以点构成形状编辑：vertices 为空（旧矩形水域）时兜底矩形四角
  // （先传多边形：其内部 fit 到本水域；随后传参考水域再统一 fitToAll 并集）
  {
    const auto &w = customWaterAreas_[ci];
    std::vector<std::pair<double,double>> verts = w.vertices;
    if (verts.empty())
      verts = {{w.xMin,w.yMin},{w.xMax,w.yMin},{w.xMax,w.yMax},{w.xMin,w.yMax}};
    dlg.setPolygonVertices(verts);
  }
  // 全部水域（排除自身）作为只读参考：其他水域灰框可见不可编辑，
  // 方便调整本水域时参考位置
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    std::vector<keship::WaterArea> refs;
    for (const auto &a : waterAreaMgr_.Areas())
      if (a.name != customWaterAreas_[ci].name)
        refs.push_back(a);
    dlg.setReferenceWaterAreas(refs);
  }
  if (dlg.exec() != QDialog::Accepted)
    return;

  auto updatedW = dlg.getWaterArea();
  const std::string victim = customWaterAreas_[ci].name;
  customWaterAreas_[ci] = updatedW;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    auto all = waterAreaMgr_.Areas();
    waterAreaMgr_.Clear();
    for (auto &a : all)
      waterAreaMgr_.AddArea(a.name == victim ? updatedW : a);
  }
  RebuildWaterAreas();
  PublishWaveParams(customWaterAreas_[ci].waveParams,
                    customWaterAreas_[ci].name);
}

void MenuPluginKeship::OnBtn_DeleteWaterArea(int _index)
{
  std::vector<keship::WaterArea> areas;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    areas = waterAreaMgr_.Areas();
  }
  if (_index < 0 || _index >= (int)areas.size())
    return;

  /// 删除操作二次确认
  if (QMessageBox::question(nullptr, "删除水域",
      QString::fromStdString("确定删除水域「" + areas[_index].name + "」吗？\n（其上的波浪/风/流参数将一并移除）"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    return;

  DeleteWaterAreaByName(areas[_index].name);
  RebuildWaterAreas();
  // 删除水区：波浪场由 WavesModel 依据 JSON 移除，无需 PMS（避免误更新其余水区）
}

void MenuPluginKeship::DeleteWaterAreaByName(const std::string &_name)
{
  // 按名字删除 customs 中全部同名（与 Areas 重建的"同名全去"保持一致，
  // 防止残留快照导致"添加默认水域"按钮错误置灰）
  customWaterAreas_.erase(
      std::remove_if(customWaterAreas_.begin(), customWaterAreas_.end(),
          [&](const keship::WaterArea &a) { return a.name == _name; }),
      customWaterAreas_.end());
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    auto all = waterAreaMgr_.Areas();
    waterAreaMgr_.Clear();
    for (const auto &a : all)
      if (a.name != _name)
        waterAreaMgr_.AddArea(a);
  }
  // 来源元数据一并清理（子世界删除/移动一致性）
  this->modelManager->RemoveWaterSource(_name);
}

//-----------------------------------------------------------------------------
// 结构树交互：复制 / 移动 / 子世界属性与删除
//-----------------------------------------------------------------------------

std::vector<std::string> MenuPluginKeship::SplitChain(const QString &_chain)
{
  std::vector<std::string> segs;
  for (const QString &s : _chain.split('/', Qt::SkipEmptyParts))
    segs.push_back(s.toStdString());
  return segs;
}

QString MenuPluginKeship::ModelChainOf(
    const keship::ModelManager::WorldModelSource &_src)
{
  QString chain;
  for (const auto &s : _src.path)
  {
    if (!chain.isEmpty()) chain += "/";
    chain += QString::fromStdString(s);
  }
  return chain;
}

QString MenuPluginKeship::WaterChainOf(
    const keship::ModelManager::WaterWorldSource &_src)
{
  QString chain;
  for (const auto &s : _src.path)
  {
    if (!chain.isEmpty()) chain += "/";
    chain += QString::fromStdString(s);
  }
  return chain;
}

std::vector<std::string> MenuPluginKeship::CollectChainModels(
    const QString &_chain) const
{
  const auto segs = SplitChain(_chain);
  std::vector<std::string> out;
  if (segs.empty()) return out;
  for (const auto &[name, src] : this->modelManager->GetModelSources())
  {
    if (src.path.size() < segs.size()) continue;
    bool match = true;
    for (size_t i = 0; i < segs.size(); ++i)
      if (src.path[i] != segs[i]) { match = false; break; }
    if (match) out.push_back(name);
  }
  return out;
}

std::vector<std::string> MenuPluginKeship::CollectChainWaters(
    const QString &_chain) const
{
  const auto segs = SplitChain(_chain);
  std::vector<std::string> out;
  if (segs.empty()) return out;
  for (const auto &[name, src] : this->modelManager->GetWaterSources())
  {
    if (src.path.size() < segs.size()) continue;
    bool match = true;
    for (size_t i = 0; i < segs.size(); ++i)
      if (src.path[i] != segs[i]) { match = false; break; }
    if (match) out.push_back(name);
  }
  return out;
}

QStringList MenuPluginKeship::AllExistingChains() const
{
  std::set<QString> chains;
  auto addChain = [&chains](const QString &_chain)
  {
    QString cur;
    for (const QString &seg : _chain.split('/', Qt::SkipEmptyParts))
    {
      cur = cur.isEmpty() ? seg : cur + "/" + seg;
      chains.insert(cur);
    }
  };
  for (const auto &[name, src] : this->modelManager->GetModelSources())
    addChain(ModelChainOf(src));
  for (const auto &[name, src] : this->modelManager->GetWaterSources())
    addChain(WaterChainOf(src));
  QStringList out;
  for (const QString &c : chains) out << c;
  return out;
}

bool MenuPluginKeship::ChainExists(const QString &_chain) const
{
  return AllExistingChains().contains(_chain);
}

bool MenuPluginKeship::RewriteChain(const QString &_oldChain,
                                    const QString &_newChain)
{
  if (_oldChain.isEmpty() || _oldChain == _newChain) return false;
  if (ChainExists(_newChain))
  {
    QMessageBox::warning(nullptr, "操作失败",
        "目标链「" + _newChain + "」已存在，无法移动/改名");
    return false;
  }
  const auto oldSegs = SplitChain(_oldChain);
  const auto newSegs = SplitChain(_newChain);
  bool any = false;
  auto modelSources = this->modelManager->GetModelSources();
  for (auto &[name, src] : modelSources)
  {
    if (src.path.size() < oldSegs.size()) continue;
    bool match = true;
    for (size_t i = 0; i < oldSegs.size(); ++i)
      if (src.path[i] != oldSegs[i]) { match = false; break; }
    if (!match) continue;
    std::vector<std::string> npath = newSegs;
    for (size_t i = oldSegs.size(); i < src.path.size(); ++i)
      npath.push_back(src.path[i]);
    src.path = npath;
    this->modelManager->SetModelSource(name, src);
    any = true;
  }
  auto waterSources = this->modelManager->GetWaterSources();
  for (auto &[name, wsrc] : waterSources)
  {
    if (wsrc.path.size() < oldSegs.size()) continue;
    bool match = true;
    for (size_t i = 0; i < oldSegs.size(); ++i)
      if (wsrc.path[i] != oldSegs[i]) { match = false; break; }
    if (!match) continue;
    std::vector<std::string> npath = newSegs;
    for (size_t i = oldSegs.size(); i < wsrc.path.size(); ++i)
      npath.push_back(wsrc.path[i]);
    wsrc.path = npath;
    this->modelManager->SetWaterSource(name, wsrc);
    any = true;
  }
  return any;
}

/// @brief 复制默认名的 ASCII 基名：ROS 2 话题名不允许中文字符（会失配/报错），
///        所以"复制"生成的默认模型名必须是纯 ASCII —— 只保留原名的 ASCII 可见字符，
///        其余（中文等）折叠为一个下划线；若没有任何可用字符则回退为 "model"。
static QString AsciiCopyBase(const QString &_orig)
{
  QString base;
  for (const QChar &ch : _orig)
  {
    const ushort u = ch.unicode();
    const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                    (u >= '0' && u <= '9') || u == '_' || u == '-';
    if (ok)
    {
      base += ch;
    }
    else if (!base.isEmpty() && !base.endsWith('_') && !base.endsWith('-'))
    {
      base += '_';
    }
  }
  while (!base.isEmpty() && (base.endsWith('_') || base.endsWith('-'))) base.chop(1);
  while (!base.isEmpty() && (base.startsWith('_') || base.startsWith('-'))) base.remove(0, 1);
  if (base.isEmpty()) base = QStringLiteral("model");
  return base;
}

QString MenuPluginKeship::UniqueCopyName(const QString &_base,
                                         const std::set<QString> &_existing)
{
  if (_existing.find(_base) == _existing.end()) return _base;
  int n = 2;
  while (true)
  {
    const QString cand = _base + QString::number(n);
    if (_existing.find(cand) == _existing.end()) return cand;
    ++n;
  }
}

bool MenuPluginKeship::DuplicateModelCore(const std::string &_name,
                                          const std::string &_newName,
                                          const QString &_chain,
                                          const double _pos[6])
{
  std::string sdf = this->modelManager->GetCachedModelSDF(_name);
  if (sdf.empty())
  {
    QMessageBox::warning(nullptr, "复制模型",
        "无法复制「" + QString::fromStdString(_name) + "」（缺少缓存 SDF）");
    return false;
  }
  keship::BuoyancyConfig bCfg;
  const bool haveBuoy =
      this->modelManager->GetCachedBuoyancyConfig(_name, bCfg) && !bCfg.bodies.empty();
  keship::ThrusterConfig tCfg;
  const bool haveThrust =
      this->modelManager->GetCachedThrusterConfig(_name, tCfg) && !tCfg.bodies.empty();
  keship::HydroConfig hCfg;
  const bool haveHydro = this->modelManager->GetCachedHydroConfig(_name, hCfg);
  keship::SensorConfig sCfg;
  const bool haveSensor = this->modelManager->GetCachedSensorConfig(_name, sCfg);

  // 缓存 SDF 的模型名统一为新实例名（编辑重建 allow_renaming=false 依赖 name 一致）
  {
    auto ns_ = sdf.find("model name=\"");
    if (ns_ != std::string::npos)
    {
      ns_ += 12;
      auto ne = sdf.find("\"", ns_);
      if (ne != std::string::npos)
        sdf.replace(ns_, ne - ns_, _newName);
    }
  }
  this->modelManager->CacheModelSDF(_newName, sdf);
  this->modelManager->CacheBuoyancyConfig(_newName, bCfg);
  this->modelManager->CacheThrusterConfig(_newName, tCfg);
  if (haveHydro)
    this->modelManager->CacheHydroConfig(_newName, hCfg);
  if (haveSensor)
    this->modelManager->CacheSensorConfig(_newName, sCfg);

  bool ok = false;
  if (haveThrust)
    ok = this->modelManager->SpawnModelWithThrust(_newName, tCfg, _pos);
  else if (haveBuoy)
    ok = this->modelManager->SpawnModelWithBuoyancy(_newName, bCfg, _pos);
  else
    ok = this->modelManager->SpawnModelInGazeboFromSDFString(_newName, sdf);
  if (!ok)
  {
    QMessageBox::warning(nullptr, "复制模型",
        "生成副本「" + QString::fromStdString(_newName) + "」失败");
    return false;
  }

  // 等待 spawn 完成（与载入世界一致的轮询）
  {
    uint32_t id = 0; int tries = 100;
    while (tries-- > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (this->modelManager->GetModelIDByInstanceNameFromGazebo(_newName, id)) break;
    }
  }

  // 来源登记：path = 目标链；baseName = 新名（防同链保存时 JSON 键冲突）
  keship::ModelManager::WorldModelSource src;
  keship::ModelManager::WorldModelSource orig;
  if (this->modelManager->GetModelSource(_name, orig))
  {
    src.ns = orig.ns;
    src.kswPath = orig.kswPath;
  }
  src.path = SplitChain(_chain);
  src.baseName = _newName;
  this->modelManager->SetModelSource(_newName, src);
  return true;
}

bool MenuPluginKeship::DuplicateWaterCore(const std::string &_name,
                                          const std::string &_newName,
                                          const QString &_chain,
                                          double _dx, double _dy,
                                          double _baseHeight)
{
  const auto &areas = waterAreaMgr_.Areas();
  const keship::WaterArea *src = nullptr;
  for (const auto &a : areas)
    if (a.name == _name) { src = &a; break; }
  if (!src) return false;

  keship::WaterArea copy = *src;
  copy.name = _newName;
  copy.xMin += _dx; copy.xMax += _dx;
  copy.yMin += _dy; copy.yMax += _dy;
  for (auto &v : copy.vertices)
  {
    v.first += _dx;
    v.second += _dy;
  }
  copy.baseHeight = src->baseHeight + _baseHeight;

  customWaterAreas_.push_back(copy);
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    waterAreaMgr_.AddArea(copy);
  }

  // 来源登记：path = 目标链；baseName = 新名（防同链保存时 JSON 键冲突）
  keship::ModelManager::WaterWorldSource wsrc;
  if (this->modelManager->GetWaterSource(_name, wsrc))
    wsrc.baseName = copy.name;
  wsrc.path = SplitChain(_chain);
  this->modelManager->SetWaterSource(copy.name, wsrc);
  return true;
}

bool MenuPluginKeship::MoveModelToChain(const std::string &_name,
                                        const QString &_chain)
{
  keship::ModelManager::WorldModelSource src;
  const bool hasSrc = this->modelManager->GetModelSource(_name, src);
  const std::string baseName =
      hasSrc && !src.baseName.empty() ? src.baseName : _name;
  // 目标链下同名 baseName 冲突检查（保存时 JSON 键冲突防御）
  for (const auto &[n, s] : this->modelManager->GetModelSources())
  {
    if (n == _name) continue;
    if (s.path == SplitChain(_chain) && s.baseName == baseName)
    {
      QMessageBox::warning(nullptr, "移动模型",
          "目标子世界下已存在同名模型「"
          + QString::fromStdString(baseName) + "」");
      return false;
    }
  }
  src.path = SplitChain(_chain);
  this->modelManager->SetModelSource(_name, src);
  return true;
}

bool MenuPluginKeship::MoveWaterToChain(const std::string &_name,
                                        const QString &_chain)
{
  keship::ModelManager::WaterWorldSource wsrc;
  const bool hasSrc = this->modelManager->GetWaterSource(_name, wsrc);
  const std::string baseName =
      hasSrc && !wsrc.baseName.empty() ? wsrc.baseName : _name;
  for (const auto &[n, s] : this->modelManager->GetWaterSources())
  {
    if (n == _name) continue;
    if (s.path == SplitChain(_chain) && s.baseName == baseName)
    {
      QMessageBox::warning(nullptr, "移动水域",
          "目标子世界下已存在同名水域「"
          + QString::fromStdString(baseName) + "」");
      return false;
    }
  }
  wsrc.path = SplitChain(_chain);
  this->modelManager->SetWaterSource(_name, wsrc);
  return true;
}

void MenuPluginKeship::OnBtn_DuplicateModelByIndex(int _midx)
{
  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_midx);
  if (nameAndId.first.empty()) return;
  const std::string orig = nameAndId.first;

  double pose[6] = {0, 0, 0, 0, 0, 0};
  if (!this->modelManager->GetModelPoseByIDFromGazebo(nameAndId.second, pose))
  {
    std::cerr << "复制模型: 获取位姿失败 " << orig << std::endl;
    return;
  }

  // 默认名去重（_copy、_copy2…）
  std::set<QString> existing;
  for (const auto &[n, id] : this->modelManager->GetModelList())
    existing.insert(QString::fromStdString(n));
  // 默认名必须全 ASCII：ROS 2 话题名不接受中文，复制名含中文会使传感器话题失配
  const QString defaultName = UniqueCopyName(
      AsciiCopyBase(QString::fromStdString(orig)) + "_copy", existing);

  // 弹属性对话框：可命名 + 改位姿（静态/浮力/惯性只读展示）
  ModelSettingsDialog dialog(this->GetModelNames(), ModelSettingsDialog::Ksm);
  dialog.setDefaultName(defaultName);
  dialog.setPose(pose);
  if (dialog.exec() != QDialog::Accepted) return;

  const std::string newName = dialog.getModelName().toStdString();
  double pos[6] = {
      dialog.getPosX(), dialog.getPosY(), dialog.getPosZ(),
      dialog.getRoll() * M_PI / 180.0,
      dialog.getPitch() * M_PI / 180.0,
      dialog.getYaw() * M_PI / 180.0};

  // 目标链 = 原件所在链（副本留在同一子世界内）
  QString chain;
  keship::ModelManager::WorldModelSource origSrc;
  if (this->modelManager->GetModelSource(orig, origSrc))
    chain = ModelChainOf(origSrc);

  if (DuplicateModelCore(orig, newName, chain, pos))
    NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_DuplicateWaterArea(int _widx)
{
  std::vector<keship::WaterArea> areas;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    areas = waterAreaMgr_.Areas();
  }
  if (_widx < 0 || _widx >= (int)areas.size()) return;
  const std::string orig = areas[_widx].name;

  std::set<QString> existing;
  for (const auto &a : areas) existing.insert(QString::fromStdString(a.name));
  // 后缀统一为 ASCII（_copy）；水域名本身保留（系统默认名即中文"默认水域"/"水域N"，
  // 强剥离会退化成无意义名，且水域名不参与 ROS 话题构造，无失配风险）
  const QString defaultName = UniqueCopyName(
      QString::fromStdString(orig) + "_copy", existing);

  DuplicateWaterDialog dlg(defaultName);
  if (dlg.exec() != QDialog::Accepted) return;
  const std::string newName = dlg.waterName().toStdString();
  if (existing.count(QString::fromStdString(newName)))
  {
    QMessageBox::warning(nullptr, "复制水域",
        "同名水域已存在：" + QString::fromStdString(newName));
    return;
  }

  // 目标链 = 原件所在链
  QString chain;
  keship::ModelManager::WaterWorldSource origSrc;
  if (this->modelManager->GetWaterSource(orig, origSrc))
    chain = WaterChainOf(origSrc);

  if (!DuplicateWaterCore(orig, newName, chain,
                          dlg.dx(), dlg.dy(), dlg.baseHeight()))
    return;

  RebuildWaterAreas();
  for (const auto &a : waterAreaMgr_.Areas())
    if (a.name == newName)
    {
      PublishWaveParams(a.waveParams, a.name);
      break;
    }
}

void MenuPluginKeship::OnBtn_DuplicateSubWorld(const QString &_chain)
{
  const auto models = CollectChainModels(_chain);
  const auto waters = CollectChainWaters(_chain);
  if (models.empty() && waters.empty())
  {
    QMessageBox::information(nullptr, "复制子世界", "该子世界内没有可复制的实体");
    return;
  }

  // 默认新链名 = 末段 + _copy（去重）；保持父链不变
  const QString parentChain = _chain.section('/', 0, -2);
  std::set<QString> existing;
  const auto chains = AllExistingChains();
  for (const QString &c : chains) existing.insert(c);
  // 链名末段同样 ASCII 化：SubWorldSettingsDialog 的校验只接受 [A-Za-z0-9_]，
  // 原先的「xxx_副本」默认值本身就不合法，用户被迫手动改
  const QString baseLeaf =
      AsciiCopyBase(_chain.section('/', -1)) + "_copy";
  const QString fullBase = parentChain.isEmpty()
      ? baseLeaf : parentChain + "/" + baseLeaf;
  const QString newChain = UniqueCopyName(fullBase, existing);

  // 实体清单（显示名 = baseName）
  QStringList mNames, wNames;
  for (const auto &m : models)
  {
    keship::ModelManager::WorldModelSource src;
    if (this->modelManager->GetModelSource(m, src))
      mNames << (src.baseName.empty() ? QString::fromStdString(m)
                                      : QString::fromStdString(src.baseName));
  }
  for (const auto &w : waters)
  {
    keship::ModelManager::WaterWorldSource wsrc;
    if (this->modelManager->GetWaterSource(w, wsrc))
      wNames << (wsrc.baseName.empty() ? QString::fromStdString(w)
                                       : QString::fromStdString(wsrc.baseName));
  }

  SubWorldSettingsDialog dlg(newChain.section('/', -1), mNames, wNames);
  if (dlg.exec() != QDialog::Accepted) return;
  const QString finalChain = parentChain.isEmpty()
      ? dlg.chainName() : parentChain + "/" + dlg.chainName();
  if (existing.count(finalChain))
  {
    QMessageBox::warning(nullptr, "复制子世界", "目标链已存在：" + finalChain);
    return;
  }

  const double off[6] = {
      dlg.offsetX(), dlg.offsetY(), dlg.offsetZ(),
      dlg.offsetRollDeg() * M_PI / 180.0,
      dlg.offsetPitchDeg() * M_PI / 180.0,
      dlg.offsetYawDeg() * M_PI / 180.0};
  const auto chainSegs = SplitChain(_chain);

  // 模型复制：新实例名 = 原名_copy（去重）；目标链 = 新链 + 相对子链
  std::set<QString> modelNames;
  for (const auto &[n, id] : this->modelManager->GetModelList())
    modelNames.insert(QString::fromStdString(n));
  for (const auto &m : models)
  {
    keship::ModelManager::WorldModelSource src;
    if (!this->modelManager->GetModelSource(m, src)) continue;
    QString relChain;
    for (size_t i = chainSegs.size(); i < src.path.size(); ++i)
    {
      if (!relChain.isEmpty()) relChain += "/";
      relChain += QString::fromStdString(src.path[i]);
    }
    const QString targetChain = relChain.isEmpty()
        ? finalChain : finalChain + "/" + relChain;
    // 同上：子世界内复制的模型名也必须全 ASCII
    const QString newName = UniqueCopyName(
        AsciiCopyBase(QString::fromStdString(m)) + "_copy", modelNames);
    modelNames.insert(newName);

    // 复制位姿 = 原件位姿 + 整体偏移
    double pose[6] = {0, 0, 0, 0, 0, 0};
    uint32_t id = 0;
    if (this->modelManager->GetModelIDByInstanceNameFromGazebo(m, id))
      this->modelManager->GetModelPoseByIDFromGazebo(id, pose);
    pose[0] += off[0]; pose[1] += off[1]; pose[2] += off[2];
    pose[3] += off[3]; pose[4] += off[4]; pose[5] += off[5];

    DuplicateModelCore(m, newName.toStdString(), targetChain, pose);
  }

  // 水域复制
  std::set<QString> waterNames;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    for (const auto &a : waterAreaMgr_.Areas())
      waterNames.insert(QString::fromStdString(a.name));
  }
  for (const auto &w : waters)
  {
    keship::ModelManager::WaterWorldSource wsrc;
    if (!this->modelManager->GetWaterSource(w, wsrc)) continue;
    QString relChain;
    for (size_t i = chainSegs.size(); i < wsrc.path.size(); ++i)
    {
      if (!relChain.isEmpty()) relChain += "/";
      relChain += QString::fromStdString(wsrc.path[i]);
    }
    const QString targetChain = relChain.isEmpty()
        ? finalChain : finalChain + "/" + relChain;
    // 水域名保留原名，仅后缀 ASCII 化（同单水域复制）
    const QString newName = UniqueCopyName(
        QString::fromStdString(w) + "_copy", waterNames);
    waterNames.insert(newName);

    DuplicateWaterCore(w, newName.toStdString(), targetChain,
                       off[0], off[1], off[2]);
  }

  RebuildWaterAreas();
  NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_DeleteSubWorld(const QString &_chain)
{
  const auto models = CollectChainModels(_chain);
  const auto waters = CollectChainWaters(_chain);
  if (models.empty() && waters.empty()) return;

  if (QMessageBox::question(nullptr, "删除子世界",
      QString("确定删除子世界「%1」吗？\n将删除其下 %2 个模型、%3 个水域。")
          .arg(_chain).arg((int)models.size()).arg((int)waters.size()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    return;

  for (const auto &m : models)
    this->modelManager->DeleteModelInGazeboByName(m);
  for (const auto &w : waters)
    DeleteWaterAreaByName(w);
  RebuildWaterAreas();
  NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_ShowSubWorldSettings(const QString &_chain)
{
  auto models = CollectChainModels(_chain);
  auto waters = CollectChainWaters(_chain);
  if (models.empty() && waters.empty()) return;

  QStringList mNames, wNames;
  for (const auto &m : models)
  {
    keship::ModelManager::WorldModelSource src;
    if (this->modelManager->GetModelSource(m, src))
      mNames << (src.baseName.empty() ? QString::fromStdString(m)
                                      : QString::fromStdString(src.baseName));
  }
  for (const auto &w : waters)
  {
    keship::ModelManager::WaterWorldSource wsrc;
    if (this->modelManager->GetWaterSource(w, wsrc))
      wNames << (wsrc.baseName.empty() ? QString::fromStdString(w)
                                       : QString::fromStdString(wsrc.baseName));
  }

  SubWorldSettingsDialog dlg(_chain.section('/', -1), mNames, wNames);
  if (dlg.exec() != QDialog::Accepted) return;

  // 1. 改名（纯元数据：遍历更新 path 前缀）
  const QString parentChain = _chain.section('/', 0, -2);
  const QString newChain = parentChain.isEmpty()
      ? dlg.chainName() : parentChain + "/" + dlg.chainName();
  if (newChain != _chain)
    RewriteChain(_chain, newChain);

  // 2. 整体位姿偏移（应用到链内全部模型）
  const double off[6] = {
      dlg.offsetX(), dlg.offsetY(), dlg.offsetZ(),
      dlg.offsetRollDeg() * M_PI / 180.0,
      dlg.offsetPitchDeg() * M_PI / 180.0,
      dlg.offsetYawDeg() * M_PI / 180.0};
  const bool hasOff = std::abs(off[0]) > 1e-9 || std::abs(off[1]) > 1e-9 ||
      std::abs(off[2]) > 1e-9 || std::abs(off[3]) > 1e-9 ||
      std::abs(off[4]) > 1e-9 || std::abs(off[5]) > 1e-9;
  if (hasOff)
  {
    const QString effChain = (newChain != _chain) ? newChain : _chain;
    for (const auto &m : CollectChainModels(effChain))
    {
      uint32_t id = 0;
      if (!this->modelManager->GetModelIDByInstanceNameFromGazebo(m, id)) continue;
      double pose[6] = {0, 0, 0, 0, 0, 0};
      if (!this->modelManager->GetModelPoseByIDFromGazebo(id, pose)) continue;
      pose[0] += off[0]; pose[1] += off[1]; pose[2] += off[2];
      pose[3] += off[3]; pose[4] += off[4]; pose[5] += off[5];
      this->modelManager->SetModelPoseByIDInGazebo(id, pose);
    }
  }

  NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_MoveModelByIndex(int _midx)
{
  auto nameAndId = this->modelManager->GetModelNameAndIDByIndex(_midx);
  if (nameAndId.first.empty()) return;

  MoveToChainDialog dlg(AllExistingChains(), "模型");
  if (dlg.exec() != QDialog::Accepted) return;
  if (MoveModelToChain(nameAndId.first, dlg.targetChain()))
    NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_MoveWaterByIndex(int _widx)
{
  std::vector<keship::WaterArea> areas;
  {
    std::lock_guard<std::recursive_mutex> lock(waterAreaMutex_);
    areas = waterAreaMgr_.Areas();
  }
  if (_widx < 0 || _widx >= (int)areas.size()) return;

  MoveToChainDialog dlg(AllExistingChains(), "水域");
  if (dlg.exec() != QDialog::Accepted) return;
  if (MoveWaterToChain(areas[_widx].name, dlg.targetChain()))
    NotifyModelListChanged();
}

void MenuPluginKeship::OnBtn_MoveSubWorld(const QString &_chain)
{
  // 循环保护：目标父链不能是自己或其子孙链
  MoveToChainDialog dlg(AllExistingChains(), "子世界");
  if (dlg.exec() != QDialog::Accepted) return;
  const QString targetParent = dlg.targetChain();
  if (targetParent == _chain || targetParent.startsWith(_chain + "/"))
  {
    QMessageBox::warning(nullptr, "移动子世界", "不能移动到自身或其子链下");
    return;
  }
  const QString leaf = _chain.section('/', -1);
  const QString newChain = targetParent.isEmpty()
      ? leaf : targetParent + "/" + leaf;
  if (newChain == _chain) return;
  if (RewriteChain(_chain, newChain))
    NotifyModelListChanged();
}

//-----------------------------------------------------------------------------
void MenuPluginKeship::PublishWaveParams(const keship::WaveParams &_wp,
                                         const std::string &_areaName)
{
  // 波浪参数通过 PMS 控制 topic 下发（KSPlugin_WavesModel 应用），
  // 带 area 标识只更新指定水区波浪场，使界面设置的波浪立即生效。
  // 实验对话框亦采用同一通道（无 area = 更新全部水区）。
  gz::msgs::Param msg;
  auto *p = msg.mutable_params();
  gz::msgs::Any v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
  v.set_string_value(_wp.model);  (*p)["model"] = v;
  if (!_areaName.empty())
  {
    v.set_string_value(_areaName); (*p)["area"] = v;
  }
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
  v.set_double_value(_wp.gain);      (*p)["gain"] = v;
  v.set_double_value(_wp.amplitude); (*p)["amplitude"] = v;
  v.set_double_value(_wp.period);    (*p)["period"] = v;
  v.set_double_value(_wp.direction); (*p)["direction"] = v;
  v.set_double_value(_wp.angle);     (*p)["angle"] = v;
  v.set_double_value(_wp.scale);     (*p)["scale"] = v;
  v.set_double_value(_wp.steepness); (*p)["steepness"] = v;
  v.set_double_value(_wp.tau);       (*p)["tau"] = v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_INT32);
  v.set_int_value(_wp.number);       (*p)["number"] = v;

  // 持久发送器：首次 Advertise 后复用，避免每次新建的握手竞态丢消息
  if (!this->waveParamsPub_)
  {
    this->waveParamsPub_ = this->modelManager->GetNode().Advertise<gz::msgs::Param>(
        this->waveCtlTopic_);
  }
  this->waveParamsPub_.Publish(msg);
  std::cout << "keship: 波浪参数已下发 (gain=" << _wp.gain << ")"
            << std::endl;

  // 300ms 后补发一次，覆盖握手窗口期的潜在丢失
  QTimer::singleShot(300, this, [this, msg]() {
    if (!this->waveParamsPub_)
      return;
    this->waveParamsPub_.Publish(msg);
    std::cout << "keship: 波浪参数已补发" << std::endl;
  });
}

//-----------------------------------------------------------------------------
void MenuPluginKeship::OnBtn_InertiaCalculator()
{
  auto *dlg = new InertiaCalculatorDialog(nullptr);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->show();
}

//-----------------------------------------------------------------------------
void MenuPluginKeship::OnBtn_CleanMeshCache()
{
  if (QMessageBox::question(nullptr, "清理网格缓存",
      "将清空所有源模型目录下的规范化网格缓存（robot_model/*/normalized），\n"
      "删除后如再次使用会自动重建。是否继续？",
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No) != QMessageBox::Yes)
  {
    return;
  }

  const keship::MeshCacheCleanupResult r = keship::CleanupUnusedMeshCache();
  const QString msg = QString("清理完成：删除 %1 个文件，释放 %2 MB")
      .arg(r.deleted)
      .arg(r.freedBytes / (1024.0 * 1024.0), 0, 'f', 1);
  std::cout << "CleanupUnusedMeshCache: " << msg.toStdString() << std::endl;
  QMessageBox::information(nullptr, "清理网格缓存", msg);
}

//////////////////////////////////////////////////////////////////////////////////
// Register this plugin
GZ_ADD_PLUGIN(MenuPluginKeship,
              gz::gui::Plugin);