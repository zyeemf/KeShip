#!/usr/bin/env python3
"""监控 Gazebo 进程，退出时触发 ROS 2 launch 关闭"""

import subprocess
import time


def monitor():
    time.sleep(5)
    while True:
        time.sleep(1)
        proc = subprocess.run(['pgrep', '-f', 'gz sim -v 4'],
                              capture_output=True, text=True)
        if not proc.stdout.strip():
            break


if __name__ == '__main__':
    monitor()
