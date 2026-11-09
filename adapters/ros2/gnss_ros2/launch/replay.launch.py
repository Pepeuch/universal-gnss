from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    input_path = LaunchConfiguration("input_path")
    replay_mode = LaunchConfiguration("replay_mode")
    publish_rtcm = LaunchConfiguration("publish_rtcm")
    wall_time_scale = LaunchConfiguration("wall_time_scale")
    fallback_step_ms = LaunchConfiguration("fallback_step_ms")
    timer_poll_ms = LaunchConfiguration("timer_poll_ms")
    frame_id = LaunchConfiguration("frame_id")

    return LaunchDescription(
        [
            DeclareLaunchArgument("input_path"),
            DeclareLaunchArgument("replay_mode", default_value="wall_time"),
            DeclareLaunchArgument("publish_rtcm", default_value="true"),
            DeclareLaunchArgument("wall_time_scale", default_value="1.0"),
            DeclareLaunchArgument("fallback_step_ms", default_value="100"),
            DeclareLaunchArgument("timer_poll_ms", default_value="1"),
            DeclareLaunchArgument("frame_id", default_value="gnss_link"),
            Node(
                package="universal_gnss_ros2",
                executable="replay_node",
                name="universal_gnss_replay",
                output="screen",
                parameters=[
                    {
                        "input_path": input_path,
                        "replay_mode": replay_mode,
                        "publish_rtcm": publish_rtcm,
                        "wall_time_scale": wall_time_scale,
                        "fallback_step_ms": fallback_step_ms,
                        "timer_poll_ms": timer_poll_ms,
                        "frame_id": frame_id,
                    }
                ],
            ),
        ]
    )
