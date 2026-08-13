/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 * Copyright (C) 2024 KeShip Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <gz/msgs/param.pb.h>

#include <chrono>
#include <list>
#include <mutex>
#include <string>

#include <gz/common/Profiler.hh>
#include <gz/plugin/Register.hh>
#include <gz/rendering/Material.hh>
#include <gz/rendering/RenderingIface.hh>
#include <gz/rendering/Scene.hh>
#include <gz/rendering/ShaderParams.hh>
#include <gz/rendering/Visual.hh>
#include <gz/transport/Node.hh>

#include <sdf/Element.hh>

#include "gz/sim/components/Name.hh"
#include "gz/sim/components/SourceFilePath.hh"
#include "gz/sim/rendering/Events.hh"
#include "gz/sim/rendering/RenderUtil.hh"
#include "gz/sim/Util.hh"

#include "Wavefield.hh"
#include "WaveVisual.hh"

using namespace gz;
using namespace keship;

namespace
{
  template<typename T>
  void TryReadSdfParam(sdf::ElementPtr _elem, const std::string &_key,
                       T &_member)
  {
    if (_elem->HasElement(_key))
      _member = _elem->GetElement(_key)->Get<T>();
  }
}

class keship::WaveVisualPrivate
{
  /// \brief Path to vertex shader
  public: std::string vertexShaderUri;

  /// \brief Path to fragment shader
  public: std::string fragmentShaderUri;

  /// \brief Mutex to protect sim time updates.
  public: std::mutex mutex;

  /// \brief Connection to pre-render event callback
  public: common::ConnectionPtr connection{nullptr};

  /// \brief Connection to render tear down event callback
  public: common::ConnectionPtr teardownConnection{nullptr};

  /// \brief Name of visual this plugin is attached to
  public: std::string visualName;

  /// \brief Pointer to visual
  public: rendering::VisualPtr visual;

  /// \brief Material used by this visual
  public: rendering::MaterialPtr material;

  /// \brief Pointer to scene
  public: rendering::ScenePtr scene;

  /// \brief Entity id of the visual
  public: sim::Entity entity = sim::kNullEntity;

  /// \brief Current sim time
  public: std::chrono::steady_clock::duration currentSimTime;

  /// \brief Path to model
  public: std::string modelPath;

  /// \brief Wavefield for computing wave params
  public: Wavefield wavefield;

  /// \brief Indicate whether the shader params have been set or not
  public: bool paramsSet = false;

  /// \brief Shader param. Recale x and y components of normals.
  public: float rescale = 0.5f;

  /// \brief Shader param. Bump map scale.
  public: math::Vector2d bumpScale = math::Vector2d(25.0, 25.0);

  /// \brief Shader param. Bump map speed in x and y.
  public: math::Vector2d bumpSpeed = math::Vector2d(0.01f, 0.01f);

  /// \brief Shader param. HDR multiplier.
  public: float hdrMultiplier = 0.4f;

  /// \brief Shader param. Fresnel power.
  public: float fresnelPower = 5.0f;

  /// \brief Shader param. Color of shallow water.
  public: math::Color shallowColor = math::Color(0.0f, 0.1f, 0.3f, 1.0f);

  /// \brief Shader param. Color of deep water.
  public: math::Color deepColor = math::Color(0.0f, 0.05f, 0.2f, 1.0f);

  /// \brief Optional SDF-specified texture paths (absolute).
  public: std::string bumpMapPath;
  public: std::string cubeMapPath;

  /// \brief Transport node.
  public: transport::Node node;

  /// \brief All rendering operations must happen within this call
  public: void OnUpdate();

  /// \brief BFS search for the visual node matching this->entity.
  public: bool FindVisual();

  /// \brief Create material and attach shaders to the visual.
  public: bool EnsureMaterial();

  /// \brief Upload shader parameters (one-time init).
  public: void UploadShaderParams();

  /// \brief Update the time uniform each frame.
  public: void UpdateTime();

  /// \brief Callback for receiving wave field updates.
  /// \param[in] _msg The message containing all the wave field parameters.
  public: void OnWavefield(const msgs::Param &_msg);

  /// \brief Callback to rendering engine tear down
  public: void OnRenderTeardown();

  public: ~WaveVisualPrivate();
  public: bool alive_ = true;
};

/////////////////////////////////////////////////
WaveVisual::WaveVisual()
    : System(), dataPtr(std::make_unique<WaveVisualPrivate>())
{
}

/////////////////////////////////////////////////
WaveVisual::~WaveVisual()
{
}

/////////////////////////////////////////////////
void WaveVisual::Configure(const sim::Entity &_entity,
               const std::shared_ptr<const sdf::Element> &_sdf,
               sim::EntityComponentManager &_ecm,
               sim::EventManager &_eventMgr)
{
  GZ_PROFILE("WaveVisual::Configure");
  // Ugly, but needed because the sdf::Element::GetElement is not a const
  // function and _sdf is a const shared pointer to a const sdf::Element.
  auto sdf = const_cast<sdf::Element *>(_sdf.get());

  if (!sdf->HasElement("wavefield"))
  {
    gzerr << "<wavefield> parameter is missing " << std::endl;
    return;
  }
  if (!sdf->HasElement("shader"))
  {
    gzerr << "<shader> parameter is missing " << std::endl;
    return;
  }

  this->dataPtr->wavefield.Load(_sdf);

  if (this->dataPtr->modelPath.empty())
  {
    auto modelEntity = topLevelModel(_entity, _ecm);
    this->dataPtr->modelPath =
      _ecm.ComponentData<sim::components::SourceFilePath>(modelEntity).value();
  }

  // parse path to shaders
  sdf::ElementPtr shaderElem = sdf->GetElement("shader");
  if (!shaderElem->HasElement("vertex") ||
      !shaderElem->HasElement("fragment"))
  {
    gzerr << "<shader> must have <vertex> and <fragment> sdf elements"
          << std::endl;
    return;
  }
  else
  {
    sdf::ElementPtr vertexElem = shaderElem->GetElement("vertex");
    this->dataPtr->vertexShaderUri = common::findFile(sim::asFullPath(
        vertexElem->Get<std::string>(), this->dataPtr->modelPath));
    sdf::ElementPtr fragmentElem = shaderElem->GetElement("fragment");
    this->dataPtr->fragmentShaderUri = common::findFile(sim::asFullPath(
        fragmentElem->Get<std::string>(), this->dataPtr->modelPath));
  }

  // parse shader params
  if (shaderElem->HasElement("parameters"))
  {
    sdf::ElementPtr p = shaderElem->GetElement("parameters");
    TryReadSdfParam(p, "rescale", this->dataPtr->rescale);
    TryReadSdfParam(p, "bumpScale", this->dataPtr->bumpScale);
    TryReadSdfParam(p, "hdrMultiplier", this->dataPtr->hdrMultiplier);
    TryReadSdfParam(p, "fresnelPower", this->dataPtr->fresnelPower);
    TryReadSdfParam(p, "shallowColor", this->dataPtr->shallowColor);
    TryReadSdfParam(p, "deepColor", this->dataPtr->deepColor);
    TryReadSdfParam(p, "bumpMap", this->dataPtr->bumpMapPath);
    TryReadSdfParam(p, "cubeMap", this->dataPtr->cubeMapPath);
  }

  this->dataPtr->entity = _entity;
  auto nameComp = _ecm.Component<sim::components::Name>(_entity);
  this->dataPtr->visualName = nameComp->Data();

  // connect to the SceneUpdate event
  // the callback is executed in the rendering thread so do all
  // rendering operations in that thread
  this->dataPtr->connection =
      _eventMgr.Connect<sim::events::SceneUpdate>(
      std::bind(&WaveVisualPrivate::OnUpdate, this->dataPtr.get()));

  this->dataPtr->teardownConnection =
      _eventMgr.Connect<sim::events::RenderTeardown>(
      std::bind(&WaveVisualPrivate::OnRenderTeardown, this->dataPtr.get()));

  // Subscribe to receive the wavefield parameters.
  this->dataPtr->node.Subscribe(this->dataPtr->wavefield.Topic(),
    &WaveVisualPrivate::OnWavefield, this->dataPtr.get());
}

//////////////////////////////////////////////////
void WaveVisual::PreUpdate(
  const sim::UpdateInfo &_info,
  sim::EntityComponentManager &)
{
  GZ_PROFILE("WaveVisual::PreUpdate");
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  this->dataPtr->currentSimTime = _info.simTime;
}

//////////////////////////////////////////////////
bool WaveVisualPrivate::FindVisual()
{
  if (this->visual)
    return true;

  auto entity = this->entity;

  auto rootVis = scene->RootVisual();
  std::list<rendering::NodePtr> nodes;
  nodes.push_back(rootVis);
  while (!nodes.empty())
  {
    auto n = nodes.front();
    nodes.pop_front();
    if (n && n->HasUserData("gazebo-entity"))
    {
      auto variant = n->UserData("gazebo-entity");
      const uint64_t *value = std::get_if<uint64_t>(&variant);
      if (value && *value == static_cast<uint64_t>(entity))
      {
        this->visual = std::dynamic_pointer_cast<rendering::Visual>(n);
        return true;
      }
    }
    for (unsigned int i = 0; i < n->ChildCount(); ++i)
      nodes.push_back(n->ChildByIndex(i));
  }
  return false;
}

//////////////////////////////////////////////////
bool WaveVisualPrivate::EnsureMaterial()
{
  if (this->material)
    return true;

  auto mat = scene->CreateMaterial();
  mat->SetVertexShader(this->vertexShaderUri);
  mat->SetFragmentShader(this->fragmentShaderUri);
  this->visual->SetMaterial(mat);
  scene->DestroyMaterial(mat);
  this->material = this->visual->Material();
  return this->material != nullptr;
}

//////////////////////////////////////////////////
void WaveVisualPrivate::UploadShaderParams()
{
  auto vsParams = this->material->VertexShaderParams();

  (*vsParams)["worldviewproj_matrix"] = 1;
  (*vsParams)["rescale"] = this->rescale;

  float bumpScaleV[2] = {
      static_cast<float>(this->bumpScale.X()),
      static_cast<float>(this->bumpScale.Y())};
  (*vsParams)["bumpScale"].InitializeBuffer(2);
  (*vsParams)["bumpScale"].UpdateBuffer(bumpScaleV);

  float bumpSpeedV[2] = {
      static_cast<float>(this->bumpSpeed.X()),
      static_cast<float>(this->bumpSpeed.Y())};
  (*vsParams)["bumpSpeed"].InitializeBuffer(2);
  (*vsParams)["bumpSpeed"].UpdateBuffer(bumpSpeedV);

  // 最多支持 8 组分波（命名 uniform，与 GerstnerWaves_vs_330.glsl 对应）
  const size_t kMaxWaves = 8;
  size_t n = this->wavefield.Number();
  if (n > kMaxWaves) n = kMaxWaves;
  (*vsParams)["Nwaves"] = static_cast<int>(n);

  for (size_t i = 0; i < kMaxWaves; ++i)
  {
    std::string is = std::to_string(i);
    float amp = 0.0f, wavenumber = 0.0f, omega = 0.0f;
    float steepness = 0.0f, phase = 0.0f;
    float dir[2] = {0.0f, 0.0f};
    if (i < n)
    {
      amp        = static_cast<float>(this->wavefield.Amplitude_V()[i]);
      wavenumber = static_cast<float>(this->wavefield.Wavenumber_V()[i]);
      omega      = static_cast<float>(this->wavefield.AngularFrequency_V()[i]);
      steepness  = static_cast<float>(this->wavefield.Steepness_V()[i]);
      phase      = static_cast<float>(this->wavefield.Phase_V()[i]);
      auto d = this->wavefield.Direction_V()[i];
      dir[0] = static_cast<float>(d.X());
      dir[1] = static_cast<float>(d.Y());
    }

    (*vsParams)["amp" + is] = amp;
    (*vsParams)["k" + is] = wavenumber;
    (*vsParams)["om" + is] = omega;
    (*vsParams)["st" + is] = steepness;
    (*vsParams)["ph" + is] = phase;
    (*vsParams)["dir" + is].InitializeBuffer(2);
    (*vsParams)["dir" + is].UpdateBuffer(dir);
  }

  (*vsParams)["tau"] = this->wavefield.Tau();
  (*vsParams)["camera_position_object_space"] = 1;

  auto fsParams = this->material->FragmentShaderParams();
  (*fsParams)["hdrMultiplier"] = this->hdrMultiplier;
  (*fsParams)["fresnelPower"] = this->fresnelPower;

  float shallowColorV[4] = {
      static_cast<float>(this->shallowColor.R()),
      static_cast<float>(this->shallowColor.G()),
      static_cast<float>(this->shallowColor.B()),
      static_cast<float>(this->shallowColor.A())};
  (*fsParams)["shallowColor"].InitializeBuffer(4);
  (*fsParams)["shallowColor"].UpdateBuffer(shallowColorV);

  float deepColorV[4] = {
      static_cast<float>(this->deepColor.R()),
      static_cast<float>(this->deepColor.G()),
      static_cast<float>(this->deepColor.B()),
      static_cast<float>(this->deepColor.A())};
  (*fsParams)["deepColor"].InitializeBuffer(4);
  (*fsParams)["deepColor"].UpdateBuffer(deepColorV);

  {
    std::string path = this->bumpMapPath.empty()
        ? common::findFile(sim::asFullPath(
            "materials/textures/wave_normals.dds", this->modelPath))
        : this->bumpMapPath;
    (*fsParams)["bumpMap"].SetTexture(path,
        rendering::ShaderParam::ParamType::PARAM_TEXTURE);
  }

  {
    std::string path = this->cubeMapPath.empty()
        ? common::findFile(sim::asFullPath(
            "materials/textures/skybox_lowres.dds", this->modelPath))
        : this->cubeMapPath;
    (*fsParams)["cubeMap"].SetTexture(path,
        rendering::ShaderParam::ParamType::PARAM_TEXTURE_CUBE, 1u);
  }

  this->paramsSet = true;
}

//////////////////////////////////////////////////
void WaveVisualPrivate::UpdateTime()
{
  float t = static_cast<float>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          this->currentSimTime).count()) * 1e-9f;
  auto params = this->material->VertexShaderParams();
  (*params)["t"] = t;
}

//////////////////////////////////////////////////
void WaveVisualPrivate::OnUpdate()
{
  if (this->visualName.empty())
    return;

  if (!this->scene)
    this->scene = rendering::sceneFromFirstRenderEngine();
  if (!this->scene)
    return;

  if (!this->FindVisual())
    return;

  if (!this->EnsureMaterial())
    return;

  std::lock_guard<std::mutex> lock(this->mutex);

  if (!this->paramsSet)
    this->UploadShaderParams();

  this->UpdateTime();
}

//////////////////////////////////////////////////
void WaveVisualPrivate::OnRenderTeardown()
{
  this->visual.reset();
  this->scene.reset();
  this->material.reset();
}

//////////////////////////////////////////////////
void WaveVisualPrivate::OnWavefield(const msgs::Param &_msg)
{
  std::lock_guard<std::mutex> lock(this->mutex);
  if (!this->alive_) return;

  const auto &p = _msg.params();
  bool waveChanged = false;

  auto checkDouble = [&](const char *key, double current) {
    auto it = p.find(key);
    if (it != p.end() && std::abs(it->second.double_value() - current) > 1e-9)
      waveChanged = true;
  };
  auto checkInt = [&](const char *key, int64_t current) {
    auto it = p.find(key);
    if (it != p.end() && it->second.int_value() != current)
      waveChanged = true;
  };

  checkDouble("amplitude", this->wavefield.Amplitude());
  checkDouble("period", this->wavefield.Period());
  checkDouble("direction", this->wavefield.Direction());
  checkDouble("steepness", this->wavefield.Steepness());
  checkDouble("scale", this->wavefield.Scale());
  checkDouble("angle", this->wavefield.Angle());
  checkInt("number", static_cast<int64_t>(this->wavefield.Number()));

  this->wavefield.Load(_msg);
  if (waveChanged)
    this->paramsSet = false;
}

WaveVisualPrivate::~WaveVisualPrivate()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    alive_ = false;
  }
  if (!wavefield.Topic().empty())
    node.Unsubscribe(wavefield.Topic());
  if (connection)
    connection.reset();
  if (teardownConnection)
    teardownConnection.reset();
}

GZ_ADD_PLUGIN(keship::WaveVisual,
              sim::System,
              WaveVisual::ISystemConfigure,
              WaveVisual::ISystemPreUpdate)

GZ_ADD_PLUGIN_ALIAS(WaveVisual, "keship::WaveVisual")
