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
  vec2 bumpCoord;
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

  // Compute texture coordinates for bump map
  outVs.bumpCoord = uv0.xy*bumpScale + t*bumpSpeed;

  outVs.eyeVec = P.xyz - camera_position_object_space; // eye position in vertex space
}