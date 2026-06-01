from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    examples_dir = Path(__file__).resolve().parent
    ekf_config = str(examples_dir / "ekf.yaml")
    navsat_config = str(examples_dir / "navsat_transform.yaml")

    return LaunchDescription(
        [
            DeclareLaunchArgument("receiver_family", default_value="auto"),
            DeclareLaunchArgument("serial_device", default_value="/dev/ttyACM0"),
            DeclareLaunchArgument("serial_baud", default_value="921600"),
            DeclareLaunchArgument("publish_rate_hz", default_value="5.0"),
            DeclareLaunchArgument("frame_id", default_value="gnss_link"),
            DeclareLaunchArgument(
                "magnetic_declination_radians", default_value="0.0"
            ),
            DeclareLaunchArgument("use_odometry_yaw", default_value="false"),
            Node(
                package="universal_gnss_ros2",
                executable="receiver_node",
                name="universal_gnss_receiver",
                output="screen",
                parameters=[
                    {
                        "receiver_family": LaunchConfiguration("receiver_family"),
                        "transport": "serial",
                        "serial_device": LaunchConfiguration("serial_device"),
                        "serial_baud": LaunchConfiguration("serial_baud"),
                        "publish_rate_hz": LaunchConfiguration("publish_rate_hz"),
                        "frame_id": LaunchConfiguration("frame_id"),
                    }
                ],
            ),
            Node(
                package="robot_localization",
                executable="navsat_transform_node",
                name="navsat_transform",
                output="screen",
                parameters=[
                    navsat_config,
                    {
                        "magnetic_declination_radians": LaunchConfiguration(
                            "magnetic_declination_radians"
                        ),
                        "use_odometry_yaw": LaunchConfiguration("use_odometry_yaw"),
                    },
                ],
                remappings=[
                    ("gps/fix", "fix"),
                    ("imu", "/imu/data"),
                    ("odometry/filtered", "/odometry/filtered"),
                ],
            ),
            Node(
                package="robot_localization",
                executable="ekf_node",
                name="ekf_filter",
                output="screen",
                parameters=[ekf_config],
                remappings=[
                    ("odometry/filtered", "/odometry/filtered"),
                    ("odometry/gps", "/odometry/gps"),
                    ("odometry/filtered_map", "/odometry/global"),
                ],
            ),
        ]
    )
