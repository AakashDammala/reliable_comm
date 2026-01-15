"""
standalone_pub.launch.py - Launch file for standalone reliable publisher

Launches standalone_pub with configurable parameters.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'topic_name',
            default_value='/observation',
            description='Base topic name for reliable communication'
        ),
        DeclareLaunchArgument(
            'message_type',
            default_value='cdcl_umd_msgs/Observation',
            description='ROS message type (sensor_msgs/Image, geometry_msgs/TwistStamped, cdcl_umd_msgs/Observation)'
        ),
        DeclareLaunchArgument(
            'qos_history_depth',
            default_value='1000',
            description='QoS history depth'
        ),
        DeclareLaunchArgument(
            'max_retries_per_loop',
            default_value='25',
            description='Maximum retries per republisher loop iteration'
        ),
        DeclareLaunchArgument(
            'retry_rate_hz',
            default_value='1.0',
            description='Rate (Hz) of the republisher loop'
        ),
        DeclareLaunchArgument(
            'max_storage_size',
            default_value='1000',
            description='Maximum messages to store for retransmission'
        ),
        Node(
            package='reliable_comm',
            executable='standalone_pub',
            name='standalone_pub',
            parameters=[{
                'topic_name': LaunchConfiguration('topic_name'),
                'message_type': LaunchConfiguration('message_type'),
                'qos_history_depth': LaunchConfiguration('qos_history_depth'),
                'max_retries_per_loop': LaunchConfiguration('max_retries_per_loop'),
                'retry_rate_hz': LaunchConfiguration('retry_rate_hz'),
                'max_storage_size': LaunchConfiguration('max_storage_size'),
            }],
            output='screen',
        ),
    ])
