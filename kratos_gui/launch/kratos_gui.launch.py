"""Start the Kratos GUI, optionally with the Cloudini decoders.

ros2 launch kratos_gui kratos_gui.launch.py                 # same as ros2 run kratos_gui kratos_gui
ros2 launch kratos_gui kratos_gui.launch.py cloudini:=true  # decoders + RViz on the decoded clouds

cloudini:=true only helps when the Orin is compressing too (TUI tab: tick
Cloudini before START). Closing the GUI stops the decoders.
"""
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, EmitEvent, IncludeLaunchDescription,
                            OpaqueFunction, RegisterEventHandler)
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def gui_and_decoders(context):
    cloudini = LaunchConfiguration('cloudini').perform(context).lower() == 'true'
    actions = []
    env = {}

    if cloudini:
        actions.append(IncludeLaunchDescription(PythonLaunchDescriptionSource(
            os.path.join(get_package_share_directory('kratos_cloudini'),
                         'launch', 'decoder.launch.py'))))
        env['KRATOS_RVIZ_CONFIG'] = os.path.join(
            get_package_share_directory('kratos_gui'), 'config', 'kratos_cloudini.rviz')

    gui = Node(package='kratos_gui', executable='kratos_gui', output='screen',
               additional_env=env)
    actions.append(gui)
    # The decoders only exist for the GUI's RViz
    actions.append(RegisterEventHandler(OnProcessExit(
        target_action=gui, on_exit=[EmitEvent(event=Shutdown(reason='GUI closed'))])))
    return actions


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('cloudini', default_value='false',
                              description='true: decompress the Orin\'s Cloudini topics for RViz'),
        OpaqueFunction(function=gui_and_decoders),
    ])
