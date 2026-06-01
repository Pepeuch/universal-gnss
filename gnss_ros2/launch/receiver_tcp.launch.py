from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    receiver_family = LaunchConfiguration("receiver_family")
    tcp_host = LaunchConfiguration("tcp_host")
    tcp_port = LaunchConfiguration("tcp_port")
    publish_rate_hz = LaunchConfiguration("publish_rate_hz")
    frame_id = LaunchConfiguration("frame_id")

    return LaunchDescription(
        [
            DeclareLaunchArgument("receiver_family", default_value="auto"),
            DeclareLaunchArgument("tcp_host", default_value="127.0.0.1"),
            DeclareLaunchArgument("tcp_port", default_value="2101"),
            DeclareLaunchArgument("publish_rate_hz", default_value="5.0"),
            DeclareLaunchArgument("frame_id", default_value="gnss_link"),
            Node(
                package="universal_gnss_ros2",
                executable="receiver_node",
                name="universal_gnss_receiver",
                output="screen",
                parameters=[
                    {
                        "receiver_family": receiver_family,
                        "transport": "tcp",
                        "tcp_host": tcp_host,
                        "tcp_port": tcp_port,
                        "publish_rate_hz": publish_rate_hz,
                        "frame_id": frame_id,
                    }
                ],
            ),
        ]
    )
