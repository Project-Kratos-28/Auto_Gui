"""Decompress the Orin's Cloudini topics for the laptop (see kratos_cloudini/README.md).

Topic contract with the Orin's encoder (auto_fastlio2, jazzy-nvblox, cloudini:=true):
  /glim_ros/points/compressed  ->  /kratos_gui/glim_points   live scan
  /glim_ros/map/compressed     ->  /kratos_gui/glim_map      GLIM map

ros2 launch kratos_cloudini decoder.launch.py
ros2 launch kratos_cloudini decoder.launch.py points_in:=/other/topic
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    args = [
        DeclareLaunchArgument('points_in', default_value='/glim_ros/points/compressed'),
        DeclareLaunchArgument('points_out', default_value='/kratos_gui/glim_points'),
        DeclareLaunchArgument('map_in', default_value='/glim_ros/map/compressed'),
        DeclareLaunchArgument('map_out', default_value='/kratos_gui/glim_map'),
    ]

    # Live scan: best effort, so a lost radio packet drops one frame instead
    # of stalling the stream behind retransmissions.
    points = Node(
        package='kratos_cloudini',
        executable='cloudini_decoder',
        name='cloudini_decoder_points',
        output='screen',
        parameters=[{
            'input_topic': LaunchConfiguration('points_in'),
            'output_topic': LaunchConfiguration('points_out'),
            'reliable': False,
            'latch_output': False,
        }],
    )

    # Map: reliable (it's big and only sent when a submap is added, so losing
    # one hurts), and latched so RViz gets the last map whenever it subscribes.
    map_ = Node(
        package='kratos_cloudini',
        executable='cloudini_decoder',
        name='cloudini_decoder_map',
        output='screen',
        parameters=[{
            'input_topic': LaunchConfiguration('map_in'),
            'output_topic': LaunchConfiguration('map_out'),
            'reliable': True,
            'latch_output': True,
        }],
    )

    return LaunchDescription(args + [points, map_])
