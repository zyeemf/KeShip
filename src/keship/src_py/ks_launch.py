# ZhangYu 创建 2026.1.15
# 启动 keship 仿真环境
# 

from ament_index_python.packages import get_package_share_directory # 导入获取软件包共享目录的函数

from launch.actions import IncludeLaunchDescription # 导入包含启动描述动作
from launch.launch_description_sources import PythonLaunchDescriptionSource # 导入 Python 启动描述源

#from launch.substitutions import LaunchConfiguration # 导入获取软件包共享目录的子替换
from launch.actions import ExecuteProcess # 导入执行进程动作
from launch.actions import RegisterEventHandler # 导入注册事件处理动作
from launch.actions import EmitEvent # 导入触发事件动作
from launch.actions import DeclareLaunchArgument # 导入声明启动参数动作
from launch.event_handlers import OnProcessExit # 导入进程退出事件处理
from launch.events import Shutdown # 导入关闭事件

from launch_ros.actions import Node # 导入 ROS 节点动作



import os # 导入操作系统模块

# 导入 keship 模型类
from .ks_model import Model_Keship


##########################################################################################################
def simulation(world_name:str, headless:bool = False, gz_paused:bool = False, extra_gz_args:str = '', gz_verbose:int = 4):
    ''' 
    启动仿真 
    '''
    # 
    process = []
    # 设置Gazebo窗口标题
    os.environ['GZ_SIM_WINDOW_TITLE'] = 'keship - Gazebo Simulator'
    #
    # 日志等级：默认 4（与 2026-10-24 之前行为一致）；可用 launch 参数 gz_verbose 调低以减少刷屏
    gz_args = [f'-v {gz_verbose}']
    if headless: # 无头模式
        gz_args.append('-s')
    if not gz_paused: # 非暂停模式
        gz_args.append('-r')
    if extra_gz_args and extra_gz_args != '': # 额外参数
        gz_args.append(extra_gz_args)

    # 添加窗口标题设置（失败）
    # gz_args.append('--gui-config')
    # gz_args.append('{\"title\": \"keship - Gazebo Simulator\"}')

    #
    world_path = os.path.join(get_package_share_directory('keship'), 'worlds', f'{world_name}.sdf')
    gz_args.append(world_path) # 世界文件

    # 用于在当前launch文件中包含并执行另一个launch文件，使用IncludeLaunchDescription
    # 执行gz_sim.launch.py的效果：
    # 1 代码
    # {'gz_args': '-v 4 -r world.sdf'}.items()
    #     ↓
    # 2 变成：[( 'gz_args', '-v 4 -r world.sdf' )]
    #     ↓
    # 3 传给 IncludeLaunchDescription
    #     ↓
    # 4 IncludeLaunchDescription 执行时：
    #     - 找到 gz_sim.launch.py
    #     - 设置参数：gz_args = '-v 4 -r world.sdf'
    #     ↓
    # 5 gz_sim.launch.py 内部：
    #     - 通过 LaunchConfiguration('gz_args') 获取值
    #     - 执行：gz sim -v 4 -r world.sdf

    gz_sim = IncludeLaunchDescription(
                PythonLaunchDescriptionSource( # 另一个launch文件的文件源
                    [os.path.join(
                        get_package_share_directory('ros_gz_sim'),  # ros_gz_sim 软件包共享目录
                        'launch',                                   # launch 文件夹
                        'gz_sim.launch.py')]                        # launch文件
                ),
                launch_arguments={'gz_args':' '.join(gz_args)}.items()   # 传递参数
            )
    process.append(gz_sim)
    print(f'ros_gz_sim路径: {get_package_share_directory('ros_gz_sim')}')
    print(f'launch_zhangyu.py - gz_sim: {gz_sim.launch_arguments}')
    # # 注册仿真进程退出事件处理
    # sim_exit_event_handler = RegisterEventHandler(
    #     OnProcessExit(
    #         target_action=monitor_sim_process,
    #         on_exit=[EmitEvent(event=Shutdown(reason='仿真结束'))]
    #     )
    # )


    return process
    

##########################################################################################################
def spawn(models:list[Model_Keship], 
          sim_mode:str,
          robot_name = None,
          )->list[ExecuteProcess]:
    '''
    生成模型
    '''
    # use_sim_time = LaunchConfiguration(variable_name='use_sim_time', default='true')
    DeclareLaunchArgument(name='use_sim_time',
                          default_value='true',
                          description='Use simulation clock if true')

    ###
    launch_processes = []

    for model in models:
        if robot_name and model.robot_name!=robot_name:
            continue
        #
        if sim_mode == 'full' or sim_mode == 'sim':
            gz_spawn_entity = Node(package='ros_gz_sim',
                                   executable='create',
                                   output='screen',
                                   arguments=model.spawn_args(),
                                   )
            # launch_processes.append(gz_spawn_entity)

        #
        if sim_mode == 'full' or sim_mode == 'bridge':
            pass



    
    
    return launch_processes




