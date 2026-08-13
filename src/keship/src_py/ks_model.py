
# ZhangYu 创建 2026.1.15


import yaml
from io import TextIOWrapper


from typing import Optional


KeShip11 = [
    'Keship',
    ]

class Model_Keship:
    ''' 模型类 '''
    def __init__(self, robot_name:str, pose:list):
        ''' 构造函数 '''
        self.robot_name = robot_name # 机器人名称
        self.pose = pose # 位置姿态 [x, y, z, roll, pitch, yaw]



    def SetRobotUrdf(self, robot_urdf:str):
        ''' 设置机器人 URDF 文件路径 '''
        self.robot_urdf = robot_urdf


    def spawn_args(self, model_sdf: Optional[str] = None) -> list:
        ''' 生成 Gazebo 生成模型的参数 '''
        ### 检查是否提供了模型 SDF 文件路径
        # if model_sdf is None:
        #     _, model_sdf = self.generate()
        ###
        return [
            '-string', model_sdf,
            '-name', self.robot_name,
            '-allow_rename', 'false',
            '-x', str(self.pose[0]), '-y', str(self.pose[1]), '-z', str(self.pose[2]),
            '-R', str(self.pose[3]), '-P', str(self.pose[4]), '-Y', str(self.pose[5]),
            ]



    
    @classmethod
    def GetModelsFromConfigFile(cls, stream:TextIOWrapper) -> list:
        '''
        从配置文件中加载模型参数，并生成模型
        '''
        # 加载配置文件
        configs = yaml.safe_load(stream)
        
        models: list[Model_Keship] = []
        model: Model_Keship

        if type(configs) == dict:
            model = cls.GetOneModelFromConfig(configs)
            models.append(model)
        elif type(configs) == list:
            for model_config in configs:
                model = cls.GetOneModelFromConfig(model_config)
                models.append(model)

        return models
    
    @classmethod
    def GetOneModelFromConfig(cls, config:dict) -> 'Model_Keship':
        '''
        根据配置生成单个模型
        '''
        # 检查配置是否完整规范
        if 'model_name' not in config:
            raise ValueError(f"配置文件缺少模型名称")
        if 'model_type' not in config:
            raise ValueError(f"配置文件缺少模型类型")
        model_name:str = config['model_name']
        model_type:str = config['model_type']

        # 位置姿态
        xyz = [0.0,0.0,0.0]
        rpy = [0.0,0.0,0.0]
        if 'position' in config:
            xyz = config['position']['xyz']
            rpy = config['position']['rpy']
        pose: list[float] = [*xyz,*rpy]
        #
        model: Model_Keship = cls(model_name, pose)
        

        return model

