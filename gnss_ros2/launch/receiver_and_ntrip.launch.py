import uuid

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.descriptions import ParameterFile
from launch_ros.substitutions import FindPackageShare


def atomic_shutdown_actions(component: str):
    """Terminate the combined launch when either required child exits."""
    return [
        EmitEvent(
            event=Shutdown(
                reason=f"required Universal GNSS component exited: {component}"
            )
        )
    ]


def generate_launch_description() -> LaunchDescription:
    source_incarnation = uuid.uuid4().hex
    receiver_family = LaunchConfiguration("receiver_family")
    transport = LaunchConfiguration("transport")
    serial_device = LaunchConfiguration("serial_device")
    serial_baud = LaunchConfiguration("serial_baud")
    tcp_host = LaunchConfiguration("tcp_host")
    tcp_port = LaunchConfiguration("tcp_port")
    publish_rate_hz = LaunchConfiguration("publish_rate_hz")
    frame_id = LaunchConfiguration("frame_id")

    caster_host = LaunchConfiguration("caster_host")
    caster_port = LaunchConfiguration("caster_port")
    mountpoint = LaunchConfiguration("mountpoint")
    username = LaunchConfiguration("username")
    password = LaunchConfiguration("password")
    gga_enabled = LaunchConfiguration("gga_enabled")
    gga_interval_s = LaunchConfiguration("gga_interval_s")
    tls_enabled = LaunchConfiguration("tls_enabled")
    parameters_file = LaunchConfiguration("parameters_file")

    return LaunchDescription(
        [
            DeclareLaunchArgument("receiver_family", default_value="auto"),
            DeclareLaunchArgument("transport", default_value="serial"),
            DeclareLaunchArgument("serial_device", default_value="/dev/ttyACM0"),
            DeclareLaunchArgument("serial_baud", default_value="921600"),
            DeclareLaunchArgument("tcp_host", default_value="127.0.0.1"),
            DeclareLaunchArgument("tcp_port", default_value="2101"),
            DeclareLaunchArgument("publish_rate_hz", default_value="5.0"),
            DeclareLaunchArgument("frame_id", default_value="gnss_link"),
            DeclareLaunchArgument("caster_host", default_value="127.0.0.1"),
            DeclareLaunchArgument("caster_port", default_value="2101"),
            DeclareLaunchArgument("mountpoint", default_value="RTCM3"),
            DeclareLaunchArgument("username", default_value=""),
            DeclareLaunchArgument("password", default_value=""),
            DeclareLaunchArgument("gga_enabled", default_value="false"),
            DeclareLaunchArgument("gga_interval_s", default_value="10"),
            DeclareLaunchArgument("tls_enabled", default_value="false"),
            DeclareLaunchArgument(
                "parameters_file",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("universal_gnss_ros2"), "config", "empty_parameters.yaml"]
                ),
            ),
            Node(
                package="universal_gnss_ros2",
                executable="receiver_node",
                name="universal_gnss_receiver",
                output="screen",
                parameters=[
                    {
                        "receiver_family": receiver_family,
                        "transport": transport,
                        "serial_device": serial_device,
                        "serial_baud": serial_baud,
                        "tcp_host": tcp_host,
                        "tcp_port": tcp_port,
                        "publish_rate_hz": publish_rate_hz,
                        "frame_id": frame_id,
                        "source_incarnation": source_incarnation,
                    },
                    ParameterFile(parameters_file, allow_substs=False),
                ],
                on_exit=atomic_shutdown_actions("receiver_node"),
            ),
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
                        "expected_source_incarnation": source_incarnation,
                    },
                    ParameterFile(parameters_file, allow_substs=False),
                ],
                on_exit=atomic_shutdown_actions("ntrip_node"),
            ),
        ]
    )
