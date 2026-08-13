
# ZhangYu 创建 2026.1.15
# Keship 启动文件

import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import OpaqueFunction
from launch.actions import EmitEvent
from launch.actions import ExecuteProcess
from launch.actions import RegisterEventHandler
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration

from src_py import ks_launch
import src_py.ks_launch

from launch_ros.actions import Node


def launch(context, *args, **kwargs):
    ''' 启动 '''

    ### 要启动的进程列表
    launch_processes = []

    ### 建立仿真环境世界
    # 获取模型名称参数
    world_name:str = LaunchConfiguration('world_name').perform(context)
    world_name, _ = os.path.splitext(world_name) # 去掉文件扩展名
    print(f'世界名称: {world_name}')
    # gz仿真环境进程
    headless:bool = LaunchConfiguration('headless').perform(context).lower() == 'true'
    gz_paused:bool = LaunchConfiguration('gz_paused').perform(context).lower() == 'true'
    extra_gz_args:str = LaunchConfiguration('extra_gz_args').perform(context)
    launch_processes.extend(
        ks_launch.simulation(world_name, headless, gz_paused, extra_gz_args)
    )
                    
    
    ### 建立机器人模型

    # 机器人模型列表
    # models: list[Model_zhangyu2] = []
    #
    # config_file:str = LaunchConfiguration('config_file').perform(context)
    # if config_file and config_file != '':
    #     with open(config_file, 'r') as stream :
    #         models = Model_zhangyu2.GetModelsFromConfigFile(stream)
    # else:
    #     robot_name:str = LaunchConfiguration('robot_name').perform(context)
    #     robot_urdf:str = LaunchConfiguration('robot_urdf').perform(context)
    #     pose = [-532.0, 162.0, 0.0, 0.0, 0.0, 1.0]
    #     model = Model_zhangyu2(robot_name=robot_name, pose=pose)
    #     if robot_urdf and robot_urdf != '':
    #         model.SetRobotUrdf(robot_urdf)
    #     models.append(model)

    #
    world_name_base = os.path.basename(world_name) # 去掉路径前缀
    #
    sim_mode:str = LaunchConfiguration('sim_mode').perform(context)

    ### ROS 2 桥接与控制
    if sim_mode == 'full' or sim_mode == 'bridge':
        # 固定 bridge：桥接 /keship/model_ready（模型生命周期通知）
        from src_py.bridge import Bridge, BridgeDirection
        model_ready_bridge = Bridge(
            '/keship/model_ready', '/keship/model_ready',
            'gz.msgs.StringMsg', 'std_msgs/msg/String',
            BridgeDirection.GZ_TO_ROS)
        launch_processes.append(Node(
            package='ros_gz_bridge', executable='parameter_bridge',
            name='keship_model_ready_bridge',
            arguments=[model_ready_bridge.argument()],
            remappings=[model_ready_bridge.remapping()],
            output='screen'))

        # KeshipWorldBridge：动态管理每个模型的 bridge 子进程
        launch_processes.append(Node(
            package='keship', executable='keship_world_bridge',
            name='keship_world_bridge',
            output='screen'))

        print('ROS 2 桥接已配置')

    # Gazebo 退出时自动关闭整个 launch
    monitor_proc = ExecuteProcess(
        cmd=['ros2', 'run', 'keship', 'monitor_sim.py'],
        name='monitor_sim',
        output='screen')
    launch_processes.append(monitor_proc)
    launch_processes.append(RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=monitor_proc,
            on_exit=[EmitEvent(event=Shutdown(reason='Gazebo 已关闭'))])))

    print(f'launch配置完成')
    # 返回结果：进程列表
    return launch_processes



def generate_launch_description():
    print('zhangyu.launch.py')
    return LaunchDescription([
        
        ### 配置文件 参数
        DeclareLaunchArgument('config_file', 
                              default_value='',
                              description='Path to the model configuration file'),

        # 启动仿真环境 参数
        DeclareLaunchArgument('world_name', # 世界名称参数
                              default_value='keship_world.sdf',
                              description='World name'),
        DeclareLaunchArgument('headless', # 无头模式参数
                              default_value='false',
                              description='Run Gazebo in headless mode. No GUI.'),
        DeclareLaunchArgument('gz_paused', # Gazebo 暂停参数
                              default_value='false',
                              description='Start Gazebo in paused mode. Simulation will start paused.'),
        DeclareLaunchArgument('extra_gz_args', # 额外 Gazebo 参数
                              default_value='',
                              description='Extra arguments to pass to Gazebo.'),



        # 机器人模型 参数
        DeclareLaunchArgument('robot_name', # 机器人名称参数
                              default_value='keship_model',
                              description='Model name'),
        DeclareLaunchArgument('robot_urdf', # 机器人 URDF 文件路径参数
                              default_value='',
                              description='robot URDF file'),


        # 仿真模式
        DeclareLaunchArgument('sim_mode',
                              default_value='full',
                              description='Simulation mode: full, sim, or bridge.'
                              ),
        DeclareLaunchArgument('thruster_names',
                              default_value='thrust_0',
                              description='Comma-separated thruster names for bridge'),
                              
        
        # 调用 opaque function 加载模型
        OpaqueFunction(function=launch)
    ])