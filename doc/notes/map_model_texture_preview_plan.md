# 2D 地图模型纹理投影 — 实施方案

**日期**：2026-06-22 | **状态**：✅ 已实施 | **依赖**：[map_model_2d_preview_plan.md](archive/map_model_2d_preview_plan.md) ProjectedImage

> **代码验证日期**：2026-06-22，Assimp UV/纹理 API 全部核对，`ConvertToZUp` 行为确认。

---

## 背景

当前 `ProjectedImage` 方案用纯灰色填充三角形投影，模型只有轮廓剪影。需要还原模型的表面颜色（纹理/材质）。

---

## 总体策略

**纹理映射（主） + 法向着色（兜底）**

```
ComputeProjectedImage(meshPath)
  ├─ LoadMeshFromFile → vertices + triangles + UV + texturePath
  ├─ ConvertToZUp(Y_UP)
  ├─ if texturePath 有效 && stbi_load 成功:
  │    └─ 三角形填充: 重心坐标插值 UV → 纹理采样 → 写入像素
  └─ else:
       └─ 三角形填充: 面法线 Z 分量 → 灰度着色 (80~240)
```

---

## 实施步骤

### Step 1：新增 stb_image 纹理加载库

**文件**：`extern/stb/stb_image.h` — 单头文件库（public domain/MIT），负责 PNG/JPG/BMP/TGA 加载。

**文件**：`src_cpp/utils/StbImageLoader.cc`：
```cpp
#define STB_IMAGE_IMPLEMENTATION
#include "../extern/stb/stb_image.h"
```

**CMakeLists.txt**：
```cmake
target_include_directories(keship_utils PRIVATE extern/stb)
target_sources(keship_utils PRIVATE src_cpp/utils/StbImageLoader.cc)
```

### Step 2：MeshDataSimple 扩展三角索引

**问题**：当前 `Triangle3D` 存储完整顶点坐标，纹理采样需要知道每个顶点对应的 UV 坐标。`texCoords` 数组与 `vertices` 数组平行，但 `triangles` 中存的是坐标而非索引，无法从 `texCoords[i]` 通过顶点位置匹配找到 UV。

**方案**：新增 `triangleIndices` 字段，存储每个三角形的 3 个顶点索引。

```cpp
// MeshUtils.hh
struct MeshDataSimple {
    std::vector<gz::math::Vector3d> vertices;
    std::vector<Triangle3D> triangles;
    std::vector<gz::math::Vector3d> normals;
    std::vector<std::pair<double,double>> texCoords;           // 每顶点 UV
    std::string texturePath;                                   // diffuse 纹理路径
    std::vector<std::array<unsigned, 3>> triangleIndices;     // 三角形顶点索引
};
```

**MeshUtils.cc** — `LoadMeshFromFile` 追加：

```cpp
// 提取 UV（与顶点顺序对齐，vertexOffset 后位置一致）
if (mesh->HasTextureCoords(0)) {
    for (unsigned v = 0; v < mesh->mNumVertices; v++)
        result.texCoords.emplace_back(
            mesh->mTextureCoords[0][v].x,
            mesh->mTextureCoords[0][v].y);
}

// 提取 diffuse 纹理路径（仅取第一个 mesh 的材质）
if (m == 0 && result.texturePath.empty()) {
    const aiMaterial *mat = scene->mMaterials[mesh->mMaterialIndex];
    aiString path;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
        result.texturePath = path.C_Str();
}

// 存储三角形索引
result.triangles.push_back({...});
result.triangleIndices.push_back({i0, i1, i2});
```

### Step 3：ComputeProjectedImage 纹理采样

**纹理分支**（有纹理路径 + stbi_load 成功）：

```cpp
int texW, texH;
unsigned char *texData = stbi_load(texturePath, &texW, &texH, nullptr, 4);
if (texData) {
    for (size_t t = 0; t < mesh.triangles.size(); ++t) {
        const auto &tri = mesh.triangles[t];
        const auto &idx = mesh.triangleIndices[t];
        
        double ix0 = toImgX(tri.v0.X()), iy0 = toImgY(tri.v0.Y());
        double ix1 = toImgX(tri.v1.X()), iy1 = toImgY(tri.v1.Y());
        double ix2 = toImgX(tri.v2.X()), iy2 = toImgY(tri.v2.Y());
        
        double u0 = texCoords[idx[0]].first,  v0 = texCoords[idx[0]].second;
        double u1 = texCoords[idx[1]].first,  v1 = texCoords[idx[1]].second;
        double u2 = texCoords[idx[2]].first,  v2 = texCoords[idx[2]].second;
        
        // 包围盒 + 像素填充
        for each pixel (px, py) in bbox:
            e0 = edgeFn(px, py, ix0, iy0, ix1, iy1);
            e1 = edgeFn(px, py, ix1, iy1, ix2, iy2);
            e2 = edgeFn(px, py, ix2, iy2, ix0, iy0);
            if (e0>=0 && e1>=0 && e2>=0) || (e0<=0 && e1<=0 && e2<=0):
                area = e0 + e1 + e2;
                alpha = e1 / area; beta = e2 / area; gamma = e0 / area;
                
                u = alpha*u0 + beta*u1 + gamma*u2;
                v = alpha*v0 + beta*v1 + gamma*v2;
                u = clamp(u, 0, 1); v = clamp(v, 0, 1);
                
                tx = u * (texW - 1);
                ty = texH - 1 - v * (texH - 1);  // flip V
                ti = (ty * texW + tx) * 4;
                
                _outData[pixelIdx+0] = texData[ti+0];  // R
                _outData[pixelIdx+1] = texData[ti+1];  // G
                _outData[pixelIdx+2] = texData[ti+2];  // B
                _outData[pixelIdx+3] = 200;             // A
    }
    stbi_image_free(texData);
}
```

**法向着色分支**（兜底）：

```cpp
for (const auto &tri : mesh.triangles) {
    gz::math::Vector3d n = (tri.v1 - tri.v0).Cross(tri.v2 - tri.v1);
    n.Normalize();
    int gray = 128 + (int)(std::abs(n.Z()) * 127);  // 朝上→亮，朝下→暗
    gray = std::clamp(gray, 80, 240);
    
    // 像素填充（与现有逻辑相同，用 gray 代替硬编码 160）
    for each pixel in triangle:
        _outData[...] = {gray, gray, gray, 80};
}
```

**CMakeLists.txt** 追加：
```cmake
target_include_directories(keship_utils PRIVATE extern/stb)
target_sources(keship_utils PRIVATE
    src_cpp/utils/StbImageLoader.cc
)
```

---

## 纹理路径处理

Assimp 返回的路径可能是：
- 相对路径：`textures/building.png` → 需要基于 mesh 文件的目录拼接
- 绝对路径：`/path/to/texture.png` → 直接使用
- 嵌入纹理：`*0` → stbi_load 无法加载，回退法向着色

拼接逻辑：
```cpp
std::string textureBaseDir = meshDir.substr(0, meshDir.find_last_of("/\\") + 1);
std::string fullTexPath = textureBaseDir + texturePath;
```

---

## 改动汇总

| 文件 | 改动 |
|------|------|
| `extern/stb/stb_image.h` | **新建** — 纹理加载库 |
| `src_cpp/utils/StbImageLoader.cc` | **新建** — stb_image 编译单元 |
| `MeshUtils.hh` | `MeshDataSimple` 新增 `texCoords` + `texturePath` + `triangleIndices` |
| `MeshUtils.cc` | `LoadMeshFromFile` 提取 UV 坐标、纹理路径、三角形索引 |
| `ModelManager.cc` | `ComputeProjectedImage` 纹理采样 + 法向兜底 |
| `CMakeLists.txt` | 新增 include 路径 + StbImageLoader 源文件 |

---

## 代码验证结果 (2026-06-22)

### Assimp API 验证

| API | 验证结果 | 源码位置 |
|-----|---------|---------|
| `aiMesh::HasTextureCoords(0)` → `bool` | ✅ 正确 | `assimp/mesh.h:586` |
| `aiMesh::mTextureCoords[0][v]` → `aiVector3D(x=U, y=V)` | ✅ 2D UV 时 z=0 | `assimp/mesh.h:733` |
| `aiMaterial::GetTexture(DIFFUSE, 0, &path)` → `aiReturn` | ✅ | `assimp/material.h:804` |
| `aiString::C_Str()` → `const char*` | ✅ | `assimp/types.h:381` |

### 现有代码状态

| 检查项 | 状态 |
|--------|------|
| `extern/stb/stb_image.h` 是否存在 | ❌ 不存在，需新建 |
| `MeshDataSimple` 是否已有 `texCoords` / `triangleIndices` | ❌ 无，需新增 |
| `LoadMeshFromFile` 是否已提取 UV | ❌ 未提取，需新增 |
| `LoadMeshFromFile` 是否已提取纹理路径 | ❌ 未提取，需新增 |
| `ConvertToZUp` 是否变换三角形坐标 | ✅ 行 135-145，每个三角形顶点 in-place 变换 |
| `ConvertToZUp` 后 triangleIndices 有效性 | ✅ 索引不变，顶点已 swizzle |
| `texCoords` 与 `vertices` 索引对齐 | ✅ 在同一 mesh 循环中顺序追加，索引一致 |

### 纹理路径处理

Assimp 返回的路径需分情况处理：

```cpp
std::string resolveTexturePath(const std::string &_meshPath,
                                const std::string &_texPath)
{
  if (_texPath.empty()) return "";
  if (_texPath[0] == '*') return "";  // 嵌入纹理，stbi 无法加载
  if (_texPath[0] == '/' || (_texPath.size() > 1 && _texPath[1] == ':'))
    return _texPath;  // 绝对路径
  std::string dir = _meshPath.substr(0, _meshPath.find_last_of("/\\") + 1);
  return dir + _texPath;  // 相对路径拼接
}
```

---

## 设计要点

### 1. 为什么用重心坐标插值 UV

边缘函数 `e0, e1, e2` 的值正比于像素到各边的距离。归一化后 `(e1/area, e2/area, e0/area)` 即为重心坐标，可直接用于 UV 插值。无需额外计算。

### 2. 为什么纹理 V 轴需要翻转

纹理坐标系 V=0 在顶部（OpenGL 约定），图像坐标 Y=0 也在顶部，两者一致。但 `stbi_load` 返回的像素数据第一行对应 V=0（顶部），所以 `ty = texH - 1 - v*(texH-1)` 将 V 范围 [0,1] 映射到像素行 [H-1, 0]。

### 3. 为什么 triangleIndices 与 triangles 平行存储

两者在同一个 for 循环中 `push_back`，确保索引数组的第 t 个元素对应三角形的第 t 个三角形。每个三角形存储 3 个全局顶点索引，用于从 `texCoords` 数组中提取对应 UV。

### 4. 无纹理时的渐变

`gray = 128 + abs(n.Z) * 127`：面朝上（n.Z≈1）→ `gray≈255`（白/亮）；面朝侧（n.Z≈0）→ `gray≈128`（中等灰）。`clamp` 防止极端值，确保渲染可辨识。
