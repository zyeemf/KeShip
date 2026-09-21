// Copyright (c) 2016 The UUV Simulator Authors.
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#version 330

////////// Input parameters //////////
// Textures
uniform sampler2D bumpMap;
uniform samplerCube cubeMap;

// Colors
uniform vec4 deepColor;
uniform vec4 shallowColor;
uniform float fresnelPower;
uniform float hdrMultiplier;

// 法线细节的采样参数（2026-09-21：纹理坐标改到本阶段逐像素计算）
uniform vec2 bumpScale;
uniform vec2 bumpSpeed;
uniform float t;
// 细节活跃度 0~1（由波幅决定，下限不为 0）—— 控制**原地扰动**的幅度
uniform float detailActivity;

////////// Input computed in vertex shader //////////
in block
{
  mat3 rotMatrix;
  vec3 eyeVec;
  vec2 worldXY;
  vec2 flowXY;
} inPs;

out vec4 fragColor;

void main()
{
  // Apply bump mapping to normal vector to make waves look more detailed:
  // ★ 2026-09-21（S2 修正）：**逐像素**计算纹理坐标。
  //   顶点着色器逐顶点算时，细节层纹理周期（≈2.9 m）小于 LOD 网格顶点间距
  //   （中心 2.56 m、外围最大 15.4 m）⇒ 违反 Nyquist ⇒ 相邻三角形相位跳变、
  //   随时间变化 ⇒ 肉眼看到"细纹一直在抖动"。改到本阶段算即把采样率提到像素级。
  //   两层**同向异速**（尺度 1.0 : 0.43、速度 1.0 : 0.55）—— 同向避免拍频，
  //   异速/异尺度仍能打散单层平铺的重复纹。
  // ★ 2026-09-21（第三轮 GUI 反馈修正）—— 把"平移"与"起伏"彻底分开：
  //
  //   ① **定向漂移**：只来自 `flowXY`（=「流速」方向 × 速度 × t）。
  //      流速默认 0 ⇒ 完全不移动。波浪**不再**产生任何纹理平移 ——
  //      原先用波浪相速度驱动会显得"贴图整体在平移"，且速度与海况挂钩
  //      （张老师反馈："0 级有移动而无变化"、"不论多少级都恒定速度移动"）。
  //
  //   ② **原地振荡扰动**（无净位移）：这才是"波光粼粼"该有的样子 ——
  //      sin/cos 有界且均值为 0 ⇒ 纹理在原地微微荡漾、不会整体漂走；
  //      空间项让不同位置相位不同 ⇒ 呈波纹状起伏，而非整片一起抖。
  //      幅度 × `detailActivity`（由波幅决定，下限 0.35）⇒
  //      0 级海况仍有轻微起伏（画面不"卡死"），浪大时更活跃。
  vec2 uvBase = inPs.worldXY + inPs.flowXY;

  float tw  = t * 0.9;
  float act = detailActivity;
  vec2 w1 = vec2(sin(tw        + uvBase.y * 0.55),
                 cos(tw * 0.83 + uvBase.x * 0.47)) * (0.050 * act);
  vec2 w2 = vec2(sin(tw * 1.27 + uvBase.x * 0.31),
                 cos(tw * 0.61 + uvBase.y * 0.43)) * (0.080 * act);

  vec3 bump1 = texture(bumpMap, uvBase * bumpScale          + w1).xyz * 2.0 - 1.0;
  vec3 bump2 = texture(bumpMap, uvBase * (bumpScale * 0.43) + w2).xyz * 2.0 - 1.0;
  vec3 bumpN = normalize(vec3(bump1.xy + bump2.xy, 1.0));
  vec3 N = normalize(inPs.rotMatrix * bumpN);

  // Reflected ray:
  vec3 E = normalize(inPs.eyeVec);
  vec3 R = reflect(E, N);

  // negate z for use with the skybox texture that comes with gz-rendering
  R = vec3(R.x, R.y, -R.z);

  // uncomment this line if using other textures that are Y up
  // Gazebo requires rotated cube map lookup.
  // R = vec3(R.x, R.z, R.y);

  // Get environment color of reflected ray:
  vec4 envColor = texture(cubeMap, R, 0.0);

  // Cheap hdr effect:
  envColor.rgb *= (envColor.r+envColor.g+envColor.b)*hdrMultiplier;

  // Compute refraction ratio (Fresnel):
  float facing = 1.0 - dot(-E, N);
  float waterEnvRatio = clamp(pow(facing, fresnelPower), 0.05, 1.0);

  // Refracted ray only considers deep and shallow water colors:
  vec4 waterColor = mix(shallowColor, deepColor, facing);

  // Perform linear interpolation between reflection and refraction.
  vec4 color = mix(waterColor, envColor, waterEnvRatio);

  fragColor = vec4(color.xyz, 0.9);
}
