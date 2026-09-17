#!/bin/bash
# KeShip 分组提交脚本（2026-09-17）
set -e
cd ~/keship_ws

echo "=== 0) 补充 .gitignore ==="
python3 - <<'PYEOF'
from pathlib import Path
p = Path('.gitignore')
t = p.read_text(encoding='utf-8')
add = []
if 'paper/' not in t:
    add.append('\n# ===== 私人研究资料（论文投稿，不入库）=====\npaper/\n')
if 'Zone.Identifier' not in t:
    add.append('\n# ===== Windows 下载标记（NTFS 数据流），不入库 =====\n*:Zone.Identifier\n')
if add:
    p.write_text(t.rstrip('\n') + '\n' + ''.join(add), encoding='utf-8')
    print(f'  已追加 {len(add)} 组规则')
else:
    print('  规则已存在')
PYEOF

echo
echo "=== 1) 子世界链管理源码 ==="
git add src/keship/src_cpp/gui/keship/editors/DuplicateWaterDialog.cc \
        src/keship/src_cpp/gui/keship/editors/DuplicateWaterDialog.hh \
        src/keship/src_cpp/gui/keship/editors/MoveToChainDialog.cc \
        src/keship/src_cpp/gui/keship/editors/MoveToChainDialog.hh \
        src/keship/src_cpp/gui/keship/editors/SubWorldSettingsDialog.cc \
        src/keship/src_cpp/gui/keship/editors/SubWorldSettingsDialog.hh \
        src/keship/src_cpp/gui/keship/editors/ModelSettingsDialog.cc \
        src/keship/src_cpp/gui/keship/editors/ModelSettingsDialog.hh \
        src/keship/src_cpp/utils/ModelManager.cc \
        src/keship/src_cpp/utils/ModelManager.hh \
        src/keship/src_cpp/gui/keship/MenuPluginKeship.hh \
        src/keship/CMakeLists.txt
git commit -q -m "feat(gui): 子世界链管理源码入库 — 三个新对话框与配套改动

- 新增 SubWorldSettingsDialog / MoveToChainDialog / DuplicateWaterDialog(.cc/.hh)
  此前源码未入库，而 CMakeLists 已引用，导致 clone 后编译失败
- ModelSettingsDialog、ModelManager、MenuPluginKeship.hh、CMakeLists.txt 同步改动
- 本批已通过 colcon build 验证（BUILD_EXIT=0）" && echo "  commit 1 完成"

echo
echo "=== 2) GUI 菜单布局改版 ==="
git add src/keship/src_cpp/gui/keship/MenuPluginKeship.qml src/keship/src_cpp/gui/keship/MenuPluginKeship.cc
git commit -q -m "feat(gui): 菜单布局改版 — 加载下拉、Tab 改名、菜单拆分与删除确认

1. Tab 改名：模型管理→自由任务、虚拟实验→教学实验
2. 新增「加载 ▾」下拉，归拢载入世界/载入模型/数学船型/圆柱体/添加两类水域六个入口
3. 按钮区重排三行：加载·保存世界·世界设置·暂停 ｜ 惯性计算·任务设定·任务执行 ｜ 清理网格缓存
4. 层次树行内「⋯」菜单拆分为三个（模型/水域/子世界），按实体类型弹出，统一样式
5. 删除二次确认：DeleteModelByName/ByIndex、DeleteWaterArea、DeleteAllModels 四处新增
   QMessageBox::question（默认 No）；与原 DeleteSubWorld/CleanMeshCache 合计 7 处

注：QML 导入版本为 2.1（MenuSeparator 自 QtQuick.Controls 2.1 引入，tools/check_qml_versions.sh 核查）
验证：colcon build 通过、qmllint 零警告、启动正常" && echo "  commit 2 完成"

echo
echo "=== 3) 文档体系精简重建 ==="
git add doc/
git commit -q -m "docs: 文档体系精简重建 — 移出规划类 40 份，只留配置必须

依据维护者 2026-09-17 指示：
- 移出 40 份：plans 12（保留 ksw 格式定义 2 份）、notes 活跃方案 3、notes/archive 24、subagent_routing 1
- 提升 4 份 archive 中有独有价值者至 notes 活跃区（ma_dart_removal_fix / sp03_bug_analysis /
  virtual_experiment_framework / wavevisual_sdf_warning_analysis）
- 新增 README.md（导航）、DOC_WORKFLOW.md（更新机制）、keship_issues_log.md
- 同步改写 architecture 专题表（删 22/改 3/增 1）、keship_plans.md、notes 内部断链
- 清理前完整留底在工作区 doc_old/（不在本仓库）
- 体检：29 笔记 / 92 链接 / 2 方案，全部通过" && echo "  commit 3 完成"

echo
echo "=== 4) 文档自动化工具与触发集成 ==="
git add tools/ start_keship.sh
git commit -q -m "chore(tools): 文档自动化检查工具与启动/提交触发

- tools/check_docs.py：七项体检（代码漂移/链接/结构/源码引用核对/笔记规范/导航登记/QML↔C++ 接口）
- tools/auto_check.py + auto_check.sh：检测漂移→自动刷新 code_map→体检→定位待复核文档→写审计日志
- tools/check_qml_versions.sh：QML 导入版本核查（防 MenuSeparator 类故障复发）
- tools/verify_ui_20260917.sh、switch_qml.sh、qml_backup/：GUI 验证与版本对照
- start_keship.sh：启动前自动检查（非阻塞，实测 0.2s）
- 均经注入回归验证（增/删/改/假链接/假路径/未登记文档/假槽名，八类可检出）" && echo "  commit 4 完成"

echo
echo "=== 5) 示例资产与 .gitignore ==="
git add models_all/ks_model/wam_0804_3_newhydro.ksm \
        models_all/ks_world/platform_3boat.ksw \
        task/task_circle.toml .gitignore
git commit -q -m "chore: 新增示例资产（模型/世界/任务）并排除私人资料

- models_all/ks_model/wam_0804_3_newhydro.ksm：WAM-V 模型（新水动力参数）
- models_all/ks_world/platform_3boat.ksw/：三船平台世界
- task/task_circle.toml：圆形航迹示例任务
- .gitignore：排除 paper/（论文投稿资料）与 *:Zone.Identifier（Windows 下载标记）" && echo "  commit 5 完成"

echo
echo "=== 提交结果 ==="
git log --oneline -6
echo
echo "=== 剩余未提交项 ==="
git status --short | head -10
echo "（共 $(git status --short | wc -l) 项）"
