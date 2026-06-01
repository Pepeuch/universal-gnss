from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    caster_host = LaunchConfiguration("caster_host")
    caster_port = LaunchConfiguration("caster_port")
    mountpoint = LaunchConfiguration("mountpoint")
    username = LaunchConfiguration("username")
    password = LaunchConfiguration("password")
    gga_enabled = LaunchConfiguration("gga_enabled")
    gga_interval_s = LaunchConfiguration("gga_interval_s")
    tls_enabled = LaunchConfiguration("tls_enabled")

    return LaunchDescription(
        [
            DeclareLaunchArgument("caster_host", default_value="127.0.0.1"),
            DeclareLaunchArgument("caster_port", default_value="2101"),
            DeclareLaunchArgument("mountpoint", default_value="RTCM3"),
            DeclareLaunchArgument("username", default_value=""),
            DeclareLaunchArgument("password", default_value=""),
            DeclareLaunchArgument("gga_enabled", default_value="false"),
            DeclareLaunchArgument("gga_interval_s", default_value="10"),
            DeclareLaunchArgument("tls_enabled", default_value="false"),
            Node(
                package="universal_gnss_ros2",
                executable="ntrip_node",
                name="universal_gnss_ntrip",
                output="screen",
                parameters=[
                    {
                        "caster_host": caster_host,
                        "caster_port": caster_port,
                        "mountpoint": mountpoint,
                        "username": username,
                        "password": password,
                        "gga_enabled": gga_enabled,
                        "gga_interval_s": gga_interval_s,
                        "tls_enabled": tls_enabled,
                    }
                ],
            ),
        ]
    )
