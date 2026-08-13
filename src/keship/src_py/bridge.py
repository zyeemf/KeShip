"""Keship Bridge 工具"""
from dataclasses import dataclass
from enum import Enum


class BridgeDirection(Enum):
    BIDIRECTIONAL = 0
    GZ_TO_ROS = 1
    ROS_TO_GZ = 2


@dataclass
class Bridge:
    gz_topic: str
    ros_topic: str
    gz_type: str
    ros_type: str
    direction: BridgeDirection

    def argument(self) -> str:
        if self.direction == BridgeDirection.GZ_TO_ROS:
            return f'{self.gz_topic}@{self.ros_type}[{self.gz_type}'
        elif self.direction == BridgeDirection.ROS_TO_GZ:
            return f'{self.gz_topic}@{self.ros_type}]{self.gz_type}'
        else:  # BIDIRECTIONAL
            return f'{self.gz_topic}@{self.ros_type}@{self.gz_type}'

    def remapping(self) -> tuple:
        return (self.gz_topic, self.ros_topic)
