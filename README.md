# KeShip

KeShip 船舶运动仿真系统（ROS2 Jazzy + Gazebo Harmonic + Qt6）。

## 目录结构

```
keship_ws/
├── src/keship/              # 程序源码（ROS2 功能包）
│   └── models/              # 部分源模型（大模型不入库，本地保留）
├── models_all/              # 统一模型资源根目录
│   ├── ks_model/            # 模型清单库（.ksm 散文件，入库）
│   ├── ks_world/            # 世界清单目录（.ksw 目录 + .kswf 清单，入库）
│   └── robot_model/         # 原始模型（.dae/.stl，不入库，见下）
├── data/                    # 运行时数据（部分入库）
│   ├── models/              # 缓存模型 obj（小文件，入库）
│   ├── models_normalized/   # 网格规范化缓存（自动生成，不入库）
│   └── experiments/         # 实验临时目录（不入库）
├── task/                    # 实验任务（.toml，入库）
└── scripts/                 # 运维脚本（含目录迁移脚本）
```

## 大模型放置指引

以下目录只入库了目录结构（.gitkeep），**模型文件需自行放入**：

- `models_all/robot_model/keship3/`：keship3.dae + ship.png + keship3.assxml
- `models_all/robot_model/wamv/`：wamv.dae
- `models_all/robot_model/yanhu/`：yanhu.dae + dirt.png
- `models_all/robot_model/` 其他子目录：按需新建即可

`data/models_normalized/` 为网格规范化缓存，由程序自动生成，**无需手动放置**（删除后会自动重建）。

## 构建与运行

```bash
source /opt/ros/jazzy/setup.bash
source /usr/share/gz/setup.sh
colcon build --packages-select keship --merge-install
```

运行入口见 `start_keship.sh`（GUI 插件加载于 Gazebo）。

## Git 使用指引

### 分支结构
- `main`：现行新历史（日常操作此分支）
- `master` / `legacy`：旧历史存档（仅查阅，勿修改）

### 日常操作
```bash
git pull                       # 更新
git status / git diff          # 查看改动
git add <文件> && git commit -m "描述"
git push                       # 推送（凭据已配置，无需手动输入）
git log --oneline -5           # 查看历史
```

### 注意事项
- 凭据自动认证已配置，推送/拉取无需输入账号密码
- 大模型目录（`models_all/robot_model/` 等）已被 .gitignore 忽略，模型文件放入不会误提交
- 仓库含中文文件名（如"新世界"目录），如需显示中文可执行 `git config --global core.quotepath false`
- 克隆时若远端默认分支未切换，需显式 `-b main`
