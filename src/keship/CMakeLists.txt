# 定义CMake最低版本要求：
# 3.22.1：表示最低要求的 CMake 版本为 3.22.1
# FATAL_ERROR：如果 CMake 版本低于 3.22.1，会导致 CMake 报错并停止构建
cmake_minimum_required(VERSION 3.22.1 FATAL_ERROR)
# 项目名称
project(keship VERSION 0.0.1)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态库以 PIC 编译（链接到动态库时需要）
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 启用编译数据库生成（用于VSCode智能感知）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置QML2导入路径
# set(QML2_IMPORT_PATH /usr/lib/x86_64-linux-gnu/qt5/qml CACHE STRING "" FORCE)

############### 查找外部依赖 ###############
# 查找Assimp
find_package(assimp REQUIRED)


############### 查找所有依赖 ###############
# REQUIRED的意思：如果未找到该包，CMake 会报错并停止构建

# 添加ament_cmake支持：
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)

# 查找ROS 2依赖
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(ros_gz_sim REQUIRED)

# 查找Gazebo依赖
find_package(gz-sim8 REQUIRED) 
set(GZ_SIM_VER ${gz-sim8_VERSION_MAJOR})

# ── 能力探测（2026-10-05）：gz-sim 是否支持**运行时**设置流体附加质量 ──────────
#   KeShip 的「完全出水时取消 M_A」(S2) 需要 `gz::sim::Link::SetFluidAddedMass()`。
#   该接口**不是上游 8.11.0 自带的** —— 本工程给 gz-sim 打了补丁才有
#   （见 doc/plans/ma_air_upstream_setter_feasibility.md）。
#   为让**未打补丁**的环境也能正常编译，这里做能力探测：
#     具备  ⇒ 定义 KESHIP_HAS_SET_FLUID_ADDED_MASS，启用 S2
#     不具备 ⇒ 自动降级（S2 段不参与编译），其余功能完全不受影响
include(CheckCXXSourceCompiles)
set(_keship_saved_libs "${CMAKE_REQUIRED_LIBRARIES}")
set(_keship_saved_incs "${CMAKE_REQUIRED_INCLUDES}")
set(CMAKE_REQUIRED_LIBRARIES gz-sim8::core)
get_target_property(_keship_gzs_incs gz-sim8::core INTERFACE_INCLUDE_DIRECTORIES)
if(_keship_gzs_incs)
  set(CMAKE_REQUIRED_INCLUDES ${_keship_gzs_incs})
endif()
check_cxx_source_compiles("
  #include <gz/sim/Link.hh>
  #include <gz/math/Matrix6.hh>
  int main()
  {
    gz::sim::Link link;
    gz::math::Matrix6d m = gz::math::Matrix6d::Zero;
    link.SetFluidAddedMass(*(gz::sim::EntityComponentManager *)nullptr, m);
    return 0;
  }
" KESHIP_HAS_SET_FLUID_ADDED_MASS)
set(CMAKE_REQUIRED_LIBRARIES "${_keship_saved_libs}")
set(CMAKE_REQUIRED_INCLUDES "${_keship_saved_incs}")

if(KESHIP_HAS_SET_FLUID_ADDED_MASS)
  add_definitions(-DKESHIP_HAS_SET_FLUID_ADDED_MASS)
  message(STATUS "KeShip: gz-sim 具备 SetFluidAddedMass ⇒ 启用「完全出水取消 M_A」(S2)")
else()
  message(STATUS "KeShip: gz-sim **不具备** SetFluidAddedMass ⇒ 跳过 S2（自动降级，其余功能不变）")
endif()
find_package(gz-common5 REQUIRED) 
set(GZ_COMMON_VER ${gz-common5_VERSION_MAJOR})
find_package(Eigen3 REQUIRED)
find_package(gz-rendering8 REQUIRED)

# find_package()


# 查找Gazebo GUI插件依赖
set(CMAKE_AUTOMOC ON)
find_package(Qt5 5.15
  COMPONENTS
    Core
    Quick
    QuickControls2
  REQUIRED
)
find_package(gz-gui8 REQUIRED)

# ##
# # 创建资源路径配置文件：
# # ${CMAKE_CURRENT_SOURCE_DIR}：CMake 变量，表示当前 CMakeLists.txt 文件所在的目录（比如 /home/user/keship_ws/src/keship）
# # hooks/resource_paths.dsv.in：模板文件的相对路径
# # .in 后缀：表示这是一个 模板文件，需要经过 configure_file 处理后才能使用
# # @ONLY 选项：表示只处理模板中的变量，不处理其他 CMake 命令
# # ${CMAKE_CURRENT_BINARY_DIR}：CMake 变量，表示当前的 构建目录（通常是 build 目录）
# # ament_cmake_environment_hooks/：目标目录，ament_cmake 会自动读取这个目录下的环境钩子文件
# # resource_paths.dsv：生成的目标文件名（注意没有 .in 后缀）
# # .dsv 扩展名：表示 Delimiter-Separated Values（分隔符分隔值）文件，ament_cmake 用于定义环境变量
# configure_file(
#   ${CMAKE_CURRENT_SOURCE_DIR}/hooks/resource_paths.dsv.in
#   ${CMAKE_CURRENT_BINARY_DIR}/ament_cmake_environment_hooks/resource_paths.dsv
#   @ONLY
# )
# # 安装资源路径配置文件：
# # ${CMAKE_CURRENT_BINARY_DIR}/ament_cmake_environment_hooks/resource_paths.dsv：生成的资源路径配置文件路径
# # DESTINATION share/${PROJECT_NAME}/environment：安装目标目录，ament_cmake 会自动读取这个目录下的环境钩子文件
# install(FILES
#   ${CMAKE_CURRENT_BINARY_DIR}/ament_cmake_environment_hooks/resource_paths.dsv
#   DESTINATION share/${PROJECT_NAME}/environment
# )

############### hooks ###############
ament_environment_hooks("hooks/resource_paths.dsv.in")


############### Gazebo GUI 插件 - 编译 ###############
# 添加资源文件：
# src_cpp/gui/keship/menu.qrc：Qt 资源文件，包含插件的 UI 资源
# src_cpp/gui/keship/icons.qrc：自绘 SVG 图标（prefix KeShipIcons/，见 gui/keship/icons/）
qt_add_resources(menu_plugin_resources
  src_cpp/gui/keship/menu.qrc
  src_cpp/gui/keship/icons.qrc)
add_library(MenuPluginKeship SHARED
  src_cpp/gui/keship/MenuPluginKeship.cc
  src_cpp/gui/keship/editors/ModelSettingsDialog.cc
  src_cpp/gui/keship/experiments/CylinderTestDialog.cc
  src_cpp/gui/keship/editors/BuoyancyEditorDialog.cc
  src_cpp/gui/keship/editors/ThrusterEditorDialog.cc
  src_cpp/gui/keship/editors/HydrodynamicsEditorDialog.cc
  src_cpp/gui/keship/editors/SensorEditorDialog.cc
  src_cpp/gui/keship/editors/WorldSettingsDialog.cc
  src_cpp/gui/keship/shared/Buoyancy3DView.cc
  src_cpp/gui/keship/shared/FlowLayout.cc
  src_cpp/gui/keship/shared/UiTheme.cc
  src_cpp/gui/keship/shared/BargeFactory.cc
  src_cpp/gui/keship/shared/WaveInertiaIO.cc
  src_cpp/gui/keship/shared/WaveInertiaIO.cc
  src_cpp/gui/keship/viewers/CameraViewerDialog.cc
  src_cpp/gui/keship/viewers/LidarViewerDialog.cc
  src_cpp/gui/keship/experiments/StabilityExperimentDialog.cc
  src_cpp/gui/keship/experiments/IncliningExperimentDialog.cc
  src_cpp/gui/keship/experiments/HullStabilityDialog.cc
  src_cpp/gui/keship/shared/ShipHullDialog.cc
  src_cpp/gui/keship/shared/LinesPlanDialog.cc
  src_cpp/gui/keship/experiments/DisplacementVerifyDialog.cc
  src_cpp/gui/keship/tasks/MapWidget.cc
  src_cpp/gui/keship/tasks/TaskSetDialog.cc
  src_cpp/gui/keship/experiments/CodeRunnerDialog.cc
  src_cpp/gui/keship/experiments/TurningCircleDialog.cc
  src_cpp/gui/keship/experiments/ZigzagDialog.cc
  src_cpp/gui/keship/experiments/WaveResponseDialog.cc
  src_cpp/gui/keship/shared/StudentAlgorithmPanel.cc
  src_cpp/gui/keship/shared/RaoCurveWidget.cc
  src_cpp/gui/keship/shared/StudentCodeRunner.cc
  src_cpp/gui/keship/experiments/SensorEstimationDialog.cc
  src_cpp/gui/keship/tasks/TaskMonitorDialog.cc
  src_cpp/gui/keship/water/WaterAreaDrawDialog.cc
  src_cpp/gui/keship/editors/EnvironmentParamsDialog.cc
  src_cpp/gui/keship/editors/InertiaCalculatorDialog.cc
  src_cpp/gui/keship/editors/ProxyGeometryDialog.cc
  src_cpp/gui/keship/editors/LoadWorldDialog.cc
  src_cpp/gui/keship/editors/SaveWorldDialog.cc
  src_cpp/gui/keship/editors/SelectWorldDialog.cc
  src_cpp/gui/keship/editors/SelectKsmDialog.cc
  src_cpp/gui/keship/editors/SubWorldSettingsDialog.cc
  src_cpp/gui/keship/editors/MoveToChainDialog.cc
  src_cpp/gui/keship/editors/DuplicateWaterDialog.cc
  src_cpp/utils/TaskTomlParser.cc
  src_cpp/utils/TaskScoring.cc
  ${menu_plugin_resources}
)
target_link_libraries(MenuPluginKeship
  gz-gui8::gz-gui8
  Qt5::Core
  Qt5::Quick
  Qt5::QuickControls2
  assimp::assimp
  keship_utils
)
ament_target_dependencies(MenuPluginKeship
  rclcpp
  sensor_msgs
  ament_index_cpp
)
# Phase 10: toml++ header-only library
target_include_directories(MenuPluginKeship PRIVATE extern/toml++)
# 安装插件配置文件
install(FILES
  config/MenuPluginKeship.Config
  DESTINATION share/${PROJECT_NAME}/plugins_gui
)


############### 物理插件 - 批量进行编译/链接/安装 ###############
# 工具类静态库（只编译一次，链接到所有物理插件）
add_library(keship_utils STATIC
  src_cpp/utils/Buoyancy.cc
  src_cpp/utils/MeshUtils.cc
  src_cpp/utils/Tetrahedron.cc
  src_cpp/utils/CylinderMeshGenerator.cc
  src_cpp/utils/PrimitiveMeshGenerator.cc
  src_cpp/utils/FossenModel.cc
  src_cpp/utils/DiagnosticLog.cc
  src_cpp/utils/ShipHullGenerator.cc
  src_cpp/utils/WaterAreaManager.cc
  src_cpp/utils/WaterAreaVisualManager.cc
  src_cpp/utils/WaterAreaSerializer.cc
  src_cpp/utils/StbImageLoader.cc
  src_cpp/utils/MeshInertiaCalculator.cc
  src_cpp/utils/ModelManager.cc
  src_cpp/utils/ForceAccumulator.cc
  src_cpp/utils/TurningExperimentCore.cc
  src_cpp/utils/ZigzagExperimentCore.cc
  src_cpp/utils/WaveResponseCore.cc
  src_cpp/utils/HiddenCaseGenerator.cc
  src_cpp/utils/SensorEstimationCore.cc
)
# ★ 2026-09-28：碰撞体积"凸包"模式需要 3D 凸包 ⇒ 用系统自带的 qhull（重入版）
find_package(PkgConfig REQUIRED)
pkg_check_modules(QHULL_R REQUIRED qhull_r)

target_link_libraries(keship_utils PUBLIC
  gz-math7
  gz-transport13::gz-transport13
  # ★ 2026-09-23：ModelManager 解析 mesh uri（model:// 与相对路径）用 gz::common::findFile
  gz-common5::gz-common5
  assimp::assimp
  Eigen3::Eigen
  ${QHULL_R_LIBRARIES}          # ★ 2026-09-28 qhull（凸包）
)
target_include_directories(keship_utils PUBLIC ${QHULL_R_INCLUDE_DIRS})
target_include_directories(keship_utils PUBLIC
  src_cpp/utils
  extern/toml++
  extern
)

list(
  APPEND  KS_plugins_physics
  KSPlugin_HydroBuoyancy   # 浮力-水动力合并插件
  KSPlugin_Thrust          # 推进器插件
  KSPlugin_VariableMass    # 外部力施加器
)
foreach(PLUGIN ${KS_plugins_physics})
  add_library(${PLUGIN} SHARED
    src_cpp/physics/${PLUGIN}.cc
  )
  target_link_libraries(${PLUGIN} PUBLIC
    gz-sim8::core
    gz-common5::gz-common5
    gz-math7
    keship_utils
  )
  install(TARGETS ${PLUGIN} DESTINATION lib)
endforeach()

# KSPlugin_HydroBuoyancy also needs Wavefield (shared with WaveVisual)
target_sources(KSPlugin_HydroBuoyancy PRIVATE
  src_cpp/physics/Wavefield.cc
)

# WaveVisual 波浪视觉效果插件（依赖 gz-rendering8）
add_library(WaveVisual SHARED
  src_cpp/physics/WaveVisual.cc
  src_cpp/physics/Wavefield.cc
)
target_link_libraries(WaveVisual PUBLIC
  gz-sim8::core
  gz-rendering8::gz-rendering8
  gz-common5::gz-common5
  gz-math7
  Eigen3::Eigen
  keship_utils
)
install(TARGETS WaveVisual DESTINATION lib)

# KSPlugin_WavesModel 波浪场全局管理系统（替代 WavefieldPublisher）
add_library(KSPlugin_WavesModel SHARED
  src_cpp/physics/KSPlugin_WavesModel.cc
  src_cpp/physics/Wavefield.cc
)
target_link_libraries(KSPlugin_WavesModel PUBLIC
  gz-sim8::core
  gz-common5::gz-common5
  gz-math7
  keship_utils
)
install(TARGETS KSPlugin_WavesModel DESTINATION lib)


############### 回归测试（2026-10-24 入库）###############
# 背景：此前测试源码不入库（.gitignore 排除 src_cpp/test/），仅"本地存在时编译"。
#   2026-10-24 核查发现：该目录当时并不存在，块内 5 个目标的源码也已丢失
#   ⇒ 干净 clone 无法构建任何测试，历史验证结论不可复现。
# 现状：甲类回归测试源码随仓库分发（src_cpp/test/），默认不构建，不影响生产。
# 用法：colcon build --cmake-args -DKESHIP_BUILD_TESTS=ON && ctest --output-on-failure
option(KESHIP_BUILD_TESTS "构建 KeShip 回归测试（默认 OFF，不影响生产构建）" OFF)

if(KESHIP_BUILD_TESTS)
  enable_testing()
  find_package(Qt5 REQUIRED COMPONENTS Widgets)

  # ---- 甲-1：无 Qt 依赖（只链 keship_utils）----
  add_executable(hidden_cases_test src_cpp/test/hidden_cases_test.cc)
  target_include_directories(hidden_cases_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(hidden_cases_test gz-gui8::gz-gui8 keship_utils)
  ament_target_dependencies(hidden_cases_test rclcpp ament_index_cpp)
  add_test(NAME hidden_cases COMMAND hidden_cases_test)

  add_executable(verify_f3_enable_buoy src_cpp/test/verify_f3_enable_buoy.cc)
  target_include_directories(verify_f3_enable_buoy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(verify_f3_enable_buoy gz-gui8::gz-gui8 keship_utils)
  ament_target_dependencies(verify_f3_enable_buoy rclcpp ament_index_cpp)
  add_test(NAME f3_enable_buoy COMMAND verify_f3_enable_buoy)

  # 需命令行参数，不进 ctest（用法：batch1_verify_probe <源文件>...）
  add_executable(batch1_verify_probe src_cpp/test/batch1_verify_probe.cc)
  target_include_directories(batch1_verify_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(batch1_verify_probe gz-gui8::gz-gui8 keship_utils)
  ament_target_dependencies(batch1_verify_probe rclcpp ament_index_cpp)

  # ---- 甲-2：Qt Widgets（离屏 GUI 测试）----
  # 直接编译所需的 GUI 源文件，不链接整个 MenuPluginKeship 插件。
  set(KS_GUI_TEST_SRCS
    src_cpp/gui/keship/experiments/WaveResponseDialog.cc
    src_cpp/gui/keship/shared/StudentAlgorithmPanel.cc
    src_cpp/gui/keship/shared/RaoCurveWidget.cc
    src_cpp/gui/keship/shared/StudentCodeRunner.cc
    src_cpp/gui/keship/shared/UiTheme.cc
    src_cpp/gui/keship/shared/BargeFactory.cc
    src_cpp/gui/keship/shared/WaveInertiaIO.cc)

  # 注：ms06_ui_offscreen_test.cc 未纳入 —— 它引用 MS06CodePracticeDialog（v1，已删除），
  #     属过时测试；原件仍在 data/test/，但测的类已不存在，故降为丙类。
  foreach(_t ms06_v2_offscreen_test ms06_v2_extra_test ms06_p3_modal_test ms06_layout_shot)
    add_executable(${_t} src_cpp/test/${_t}.cc ${KS_GUI_TEST_SRCS})
    target_include_directories(${_t} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/gui/keship
      ${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/utils
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${_t} gz-gui8::gz-gui8 Qt5::Widgets keship_utils)
    ament_target_dependencies(${_t} rclcpp ament_index_cpp)
  endforeach()
  add_test(NAME ms06_v2 COMMAND ms06_v2_offscreen_test)
  add_test(NAME ms06_v2_extra COMMAND ms06_v2_extra_test)
  add_test(NAME ms06_p3_modal COMMAND ms06_p3_modal_test)
  add_test(NAME ms06_layout_shot COMMAND ms06_layout_shot)

  # 回归判据：ctest -L regression
  # 注：enable_testing() 同时会让 ament 的 linter（copyright/cpplint/uncrustify…）进入 ctest，
  #     那些是既有的代码风格检查，与回归结果无关，故用 label 隔离。
  set_tests_properties(hidden_cases f3_enable_buoy
                       PROPERTIES LABELS regression)
  # GUI 测试强制离屏：否则无 DISPLAY 的环境（CI / 纯 headless）会 SIGABRT。
  set_tests_properties(ms06_v2 ms06_v2_extra ms06_p3_modal ms06_layout_shot
                       PROPERTIES LABELS regression
                                  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  message(STATUS "KESHIP_BUILD_TESTS=ON：已启用回归测试目标（7 个可执行 / 6 个 ctest 用例）")
endif()

############### 无头指令入口 keship_ctl（2026-09-19）###############
# 让仿真在没有 GUI（MenuPluginKeship 不加载）时也能被命令行驱动。
# 直接链接 keship_utils 复用同一个 ModelManager —— 不复制任何模型/水域/SDF 组装逻辑。
# 注意：必须放在上面的 if(KESHIP_BUILD_TESTS) 之外——测试默认不构建，
# 而本目标是生产工具，必须始终编译。
add_executable(keship_ctl
  src_cpp/headless/keship_ctl.cc
)
target_link_libraries(keship_ctl PUBLIC
  gz-math7
  gz-transport13::gz-transport13
  keship_utils
)
install(TARGETS keship_ctl DESTINATION lib/${PROJECT_NAME})

############### 安装 - Python模块 ###############
ament_python_install_package(src_py)

# 安装 ROS 2 节点脚本
install(PROGRAMS
  scripts/keship_world_bridge
  scripts/monitor_sim.py
  scripts/keship_code_runner.py
  DESTINATION lib/${PROJECT_NAME}
)

# Phase D: 安装 runner 脚本到 share 目录，便于 ament_index_cpp 查找
install(PROGRAMS scripts/keship_code_runner.py
  DESTINATION share/${PROJECT_NAME}/scripts
)

############### 安装 - Gazebo GUI插件 ###############
# MenuPluginKeship：插件目标名称
# ARCHIVE DESTINATION lib：安装静态库到 lib 目录
# LIBRARY DESTINATION lib：安装共享库到 lib 目录
# RUNTIME DESTINATION bin：安装可执行文件到 bin 目录
install(TARGETS
  MenuPluginKeship
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)




############### 设置包含目录 ###############
include_directories(include)


############### 安装启动文件、配置、模型等资源 ###############
# install(DIRECTORY launch/ config/ models/ worlds/ urdf/
#     DESTINATION share/${PROJECT_NAME}
# )
install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)
install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)
install(DIRECTORY models/
  DESTINATION share/${PROJECT_NAME}/models
)
install(DIRECTORY worlds/
  DESTINATION share/${PROJECT_NAME}/worlds
)



############### 安装测试相关资源 ###############
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()


ament_package()
