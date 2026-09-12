import importlib.util
import re
import sys
from pathlib import Path

import pytest
from launch import LaunchContext, LaunchDescription, LaunchService
from launch.actions import DeclareLaunchArgument, EmitEvent, ExecuteProcess, TimerAction
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch.utilities import perform_substitutions
from launch_ros.actions import Node
from launch_ros.descriptions import ParameterFile


LAUNCH_PATH = Path(__file__).resolve().parents[1] / "launch" / "receiver_and_ntrip.launch.py"


def load_combined_launch():
    spec = importlib.util.spec_from_file_location("receiver_and_ntrip_launch", LAUNCH_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def required_child(component: str, exit_code: int | None) -> ExecuteProcess:
    if exit_code is None:
        program = "import time; time.sleep(30)"
    else:
        program = f"raise SystemExit({exit_code})"
    module = load_combined_launch()
    return ExecuteProcess(
        cmd=[sys.executable, "-c", program],
        on_exit=module.atomic_shutdown_actions(component),
    )


def test_both_combined_nodes_are_atomic_and_never_independently_respawned():
    module = load_combined_launch()
    nodes = [action for action in module.generate_launch_description().entities if isinstance(action, Node)]

    assert len(nodes) == 2
    for node in nodes:
        assert node._ExecuteLocal__respawn is False
        on_exit = node._ExecuteLocal__on_exit
        assert len(on_exit) == 1
        assert isinstance(on_exit[0], EmitEvent)
        assert isinstance(on_exit[0]._EmitEvent__event, Shutdown)


def static_parameter(node: Node, name: str) -> str:
    parameters = node._Node__parameters[0]
    for key, value in parameters.items():
        if "".join(substitution.text for substitution in key) == name:
            return "".join(substitution.text for substitution in value).splitlines()[0]
    raise AssertionError(f"missing static parameter: {name}")


def test_combined_children_share_one_fresh_source_incarnation():
    first_nodes = [
        action
        for action in load_combined_launch().generate_launch_description().entities
        if isinstance(action, Node)
    ]
    second_nodes = [
        action
        for action in load_combined_launch().generate_launch_description().entities
        if isinstance(action, Node)
    ]

    receiver_incarnation = static_parameter(first_nodes[0], "source_incarnation")
    ntrip_incarnation = static_parameter(first_nodes[1], "expected_source_incarnation")
    next_incarnation = static_parameter(second_nodes[0], "source_incarnation")
    assert receiver_incarnation == ntrip_incarnation
    assert re.fullmatch(r"[0-9a-f]{32}", receiver_incarnation)
    assert next_incarnation != receiver_incarnation


def launch_entities():
    return load_combined_launch().generate_launch_description().entities


def combined_nodes():
    return [action for action in launch_entities() if isinstance(action, Node)]


def launch_argument_defaults() -> dict[str, str]:
    context = LaunchContext()
    return {
        action.name: perform_substitutions(context, action.default_value)
        for action in launch_entities()
        if isinstance(action, DeclareLaunchArgument)
        and action.name in {"fix_topic", "status_topic", "rtcm_topic"}
    }


def launch_argument_names() -> set[str]:
    return {
        action.name
        for action in launch_entities()
        if isinstance(action, DeclareLaunchArgument)
    }


def node_remappings(node: Node, values: dict[str, str]) -> dict[str, str]:
    context = LaunchContext()
    context.launch_configurations.update(values)
    return {
        perform_substitutions(context, source): perform_substitutions(context, target)
        for source, target in node._Node__remappings
    }


def test_combined_launch_declares_topic_arguments_with_compatible_defaults():
    defaults = launch_argument_defaults()

    assert defaults["fix_topic"] == "/fix"
    assert defaults["status_topic"] == "/status"
    assert defaults["rtcm_topic"] == "/rtcm"
    assert "parameters_file" in launch_argument_names()


def test_combined_launch_keeps_parameters_file_on_both_nodes():
    nodes = combined_nodes()

    for node in nodes:
        parameter_file = node._Node__parameters[1]
        assert isinstance(parameter_file, ParameterFile)
        parameter_file_substitutions = parameter_file._ParameterFile__param_file
        assert len(parameter_file_substitutions) == 1
        assert isinstance(parameter_file_substitutions[0], LaunchConfiguration)
        assert (
            perform_substitutions(
                LaunchContext(), parameter_file_substitutions[0].variable_name
            )
            == "parameters_file"
        )
        assert parameter_file._ParameterFile__allow_substs is False


def test_combined_launch_remaps_default_topics_for_both_nodes():
    receiver, ntrip = combined_nodes()
    defaults = launch_argument_defaults()

    assert node_remappings(receiver, defaults) == {
        "fix": "/fix",
        "status": "/status",
        "rtcm": "/rtcm",
    }
    assert node_remappings(ntrip, defaults) == {
        "status": "/status",
        "rtcm": "/rtcm",
    }


def test_combined_launch_connects_mowglinext_topic_graph():
    receiver, ntrip = combined_nodes()
    mowglinext_topics = {
        "fix_topic": "/gps/fix",
        "status_topic": "/universal_gnss_receiver/status",
        "rtcm_topic": "/universal_gnss_receiver/rtcm",
    }

    assert node_remappings(receiver, mowglinext_topics) == {
        "fix": "/gps/fix",
        "status": "/universal_gnss_receiver/status",
        "rtcm": "/universal_gnss_receiver/rtcm",
    }
    assert node_remappings(ntrip, mowglinext_topics) == {
        "status": "/universal_gnss_receiver/status",
        "rtcm": "/universal_gnss_receiver/rtcm",
    }


@pytest.mark.parametrize("component", ["receiver_node", "ntrip_node"])
def test_required_child_exit_stops_its_peer(component):
    launch_service = LaunchService()
    launch_service.include_launch_description(
        LaunchDescription([required_child(component, 7), required_child("peer", None)])
    )

    assert launch_service.run() == 0


def test_external_clean_shutdown_remains_successful():
    launch_service = LaunchService()
    launch_service.include_launch_description(
        LaunchDescription(
            [
                required_child("receiver_node", None),
                required_child("ntrip_node", None),
                TimerAction(
                    period=0.2,
                    actions=[EmitEvent(event=Shutdown(reason="test clean shutdown"))],
                ),
            ]
        )
    )

    assert launch_service.run() == 0
