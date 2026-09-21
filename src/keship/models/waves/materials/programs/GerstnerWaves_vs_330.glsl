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
// limitations under the License.s


// Copyright (c) 2019 Rhys Mainwaring.
//
// Modified to accept vector parameters and use the form
// for Gerstner waves published in:
//
// Jerry Tessendorf, "Simulating Ocean Water", 1999-2004
//
// theta = k * dir . x - omega * t + phase
//
// px = x - dir.x * q * a * sin(theta)
// py = y - dir.y * q * a * sin(theta)
// pz =         a * cos(theta)
//
// k is the wavenumber
// omega is the angular frequency
// q is the steepness (0 = sine, 1 = full Gerstner)
//
// The derivative terms (Tangent, Binormal, Normal) have been
// updated to be consistent with this convention.
//
// KeShip: 支持最多 8 组分波（命名 uniform），修复 P.y 位移使用 dy
// 的 bug，theta 加入 phase（与 Wavefield::ComputeDepthSimply 一致）。

#version 330

in vec4 vertex;
in vec4 uv0;
uniform mat4 worldviewproj_matrix;

/////////// Input parameters //////////
// Waves
uniform int Nwaves;
uniform vec3 camera_position_object_space;
uniform float rescale;
uniform vec2 bumpScale;
uniform vec2 bumpSpeed;
uniform float t;
uniform float tau;
// 法线细节的**定向漂移**：由「流速」驱动（米/秒 + 世界方向）。
// ★ 2026-09-21：流速默认 0 ⇒ 漂移为 0 ⇒ 水面**不整体移动**。
//   波浪本身不产生纹理平移（波浪的运动由几何波体现）；
//   "波光粼粼"的动态由 fs 里的**原地振荡扰动**提供（不产生净位移）。
uniform float flowSpeed;
uniform vec2  flowDir;
// 细节活跃度 0~1（由波幅决定）：控制原地扰动的幅度 —— 浪大时细节更"活"。
// 下限不为 0，保证 0 级海况仍有轻微起伏（否则画面像卡死）。
uniform float detailActivity;

// 8 组分波参数（命名 uniform，由 WaveVisual 按 Number() 动态填充）
uniform float amp0;   uniform float amp1;   uniform float amp2;   uniform float amp3;
uniform float amp4;   uniform float amp5;   uniform float amp6;   uniform float amp7;
uniform float k0;     uniform float k1;     uniform float k2;     uniform float k3;
uniform float k4;     uniform float k5;     uniform float k6;     uniform float k7;
uniform float om0;    uniform float om1;    uniform float om2;    uniform float om3;
uniform float om4;    uniform float om5;    uniform float om6;    uniform float om7;
uniform float st0;    uniform float st1;    uniform float st2;    uniform float st3;
uniform float st4;    uniform float st5;    uniform float st6;    uniform float st7;
uniform float ph0;    uniform float ph1;    uniform float ph2;    uniform float ph3;
uniform float ph4;    uniform float ph5;    uniform float ph6;    uniform float ph7;
uniform vec2 dir0;    uniform vec2 dir1;    uniform vec2 dir2;    uniform vec2 dir3;
uniform vec2 dir4;    uniform vec2 dir5;    uniform vec2 dir6;    uniform vec2 dir7;

/////////// Output variables to fragment shader //////////
out block
{
  mat3 rotMatrix;
  vec3 eyeVec;
  vec2 worldXY;   // 顶点原始平面坐标（米）—— 线性量，插值到片元是精确的
  vec2 flowXY;    // 主波流动位移（米）—— 对整帧是常量
} outVs;

// Compute linear combination of Gerstner waves as described in
// GPU Gems, chapter 01: "Effective Water Simulation from Physical Models"
// http://http.developer.nvidia.com/GPUGems/gpugems_ch01.html

// Information regarding a single wave
struct WaveParameters {
  float k;      // wavenumber
  float a;      // amplitude
  float omega;  // phase constant of speed
  vec2 d;       // horizontal direction of wave
  float q;      // steepness for Gerstner wave (q=0: rolling sine waves)
  float phase;  // initial phase offset
};



out gl_PerVertex
{
  vec4 gl_Position;
};

void main()
{
  WaveParameters waves[8];

  waves[0] = WaveParameters(k0, amp0, om0, dir0.xy, st0, ph0);
  waves[1] = WaveParameters(k1, amp1, om1, dir1.xy, st1, ph1);
  waves[2] = WaveParameters(k2, amp2, om2, dir2.xy, st2, ph2);
  waves[3] = WaveParameters(k3, amp3, om3, dir3.xy, st3, ph3);
  waves[4] = WaveParameters(k4, amp4, om4, dir4.xy, st4, ph4);
  waves[5] = WaveParameters(k5, amp5, om5, dir5.xy, st5, ph5);
  waves[6] = WaveParameters(k6, amp6, om6, dir6.xy, st6, ph6);
  waves[7] = WaveParameters(k7, amp7, om7, dir7.xy, st7, ph7);

  vec4 P = vertex;

  // Iteratively compute binormal, tangent, and normal vectors:
  vec3 B = vec3(1.0, 0.0, 0.0);
  vec3 T = vec3(0.0, 1.0, 0.0);
  vec3 N = vec3(0.0, 0.0, 1.0);

  // Wave synthesis using linear combination of Gerstner waves
  for(int i = 0; i < Nwaves; ++i)
  {
    // Evaluate wave equation:
    float k = waves[i].k;
    float a = waves[i].a * (1.0 - exp(-1.0*t/tau));
    float q = waves[i].q;
    float dx = waves[i].d.x;
    float dy = waves[i].d.y;
    float theta = dot(waves[i].d, P.xy)*k - t*waves[i].omega + waves[i].phase;
    float c = cos(theta);
    float s = sin(theta);

    // Displacement of point due to wave (Eq. 9)
    P.x -= q*a*dx*s;
    P.y -= q*a*dy*s;
    P.z += a*c;

    // Modify normals due to wave displacement (Eq. 10-12)
    float ka = a*k;
    float qkac = q*ka*c;
    float kas = ka*s;
    float dxy = dx*dy;

    B += vec3(-qkac*dx*dx, -qkac*dxy, -kas*dx);
    T += vec3(-qkac*dxy, -qkac*dy*dy, -kas*dy);
    N += vec3(dx*kas, dy*kas, -qkac);
  }

  // Compute (Surf2World * Rescale) matrix
  B = normalize(B)*rescale;
  T = normalize(T)*rescale;
  N = normalize(N);
  outVs.rotMatrix = mat3(B, T, N);

  gl_Position = worldviewproj_matrix * P;

  // ★ 2026-09-21（S2 修正）：法线贴图的采样坐标改到**片段着色器**里逐像素计算。
  //
  // 原先在顶点着色器算 bumpCoord：细节层纹理周期 ≈2.9 m，而 LOD 网格顶点间距
  // 中心区 2.56 m、外围最大 15.4 m —— **纹理频率高于顶点采样率（违反 Nyquist）**
  // ⇒ 相邻三角形之间纹理相位跳变，且随 t 变化 ⇒ 肉眼看到"细纹一直在抖动"
  //（张老师 2026-09-21 实测反馈）。逐像素算即把采样率提到像素级，抖动消失。
  //
  // 这里只往下传两个量：
  //   worldXY —— 原始平面坐标（**线性** ⇒ 插值到片元是精确的，不会引入误差）；
  //   flowXY  —— 流动位移（**对整帧是常量** ⇒ 插值同样无害）。
  // 两者相加后才在 fs 里乘 bumpScale / 加 t*bumpSpeed，因此高频纹理不再被顶点插值破坏。
  //
  // 另：两层改为**同向**（都沿主波方向 dir0），速度比 1.0 : 0.55、尺度比 1.0 : 0.43。
  // 原先第二层反向 —— 反向会在两层之间产生拍频（约 1 s 一拍），观感即"抖"。
  // 同向异速同样能打散单层平铺的重复纹，但没有拍频。
  // ★ 2026-09-21（第三轮 GUI 反馈修正）：纹理漂移**只由「流速」驱动**。
  //   之前几版用波浪相速度驱动 → 观感是"贴图整体在平移"，且速度与海况挂钩
  //   （张老师反馈："0 级有移动而无变化"、"不论多少级都恒定速度移动"）。
  //   现在：波浪**不产生任何纹理平移**（波浪运动由几何波体现），
  //   漂移只来自环境参数里的「流速」（默认 0 ⇒ 完全不移动）。
  outVs.worldXY = vertex.xy;
  outVs.flowXY  = flowDir * (flowSpeed * t);

  outVs.eyeVec = P.xyz - camera_position_object_space; // eye position in vertex space
}