from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    receiver_family = LaunchConfiguration("receiver_family")
    serial_device = LaunchConfiguration("serial_device")
    serial_baud = LaunchConfiguration("serial_baud")
    publish_rate_hz = LaunchConfiguration("publish_rate_hz")
    frame_id = LaunchConfiguration("frame_id")

    return LaunchDescription(
        [
            DeclareLaunchArgument("receiver_family", default_value="auto"),
            DeclareLaunchArgument("serial_device", default_value="/dev/ttyACM0"),
            DeclareLaunchArgument("serial_baud", default_value="921600"),
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
                        "transport": "serial",
                        "serial_device": serial_device,
                        "serial_baud": serial_baud,
                        "publish_rate_hz": publish_rate_hz,
                        "frame_id": frame_id,
                    }
                ],
            ),
        ]
    )
