import importlib.util
from pathlib import Path
import queue
import re
import socket
import sys
import threading

from launch import LaunchContext, LaunchDescription, LaunchService
from launch.actions import (
    DeclareLaunchArgument, EmitEvent, ExecuteProcess, RegisterEventHandler,
    SetLaunchConfiguration, TimerAction,
)
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessStart
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch.utilities import perform_substitutions
from launch_ros.actions import Node
from launch_ros.descriptions import ParameterFile
import pytest


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
        and action.name in {"fix_topic", "status_topic", "rtcm_topic", "ntrip_enabled"}
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
    assert defaults["ntrip_enabled"] == "true"
    assert "parameters_file" in launch_argument_names()


def test_ntrip_node_is_conditioned_by_the_explicit_launch_argument():
    _, ntrip = combined_nodes()

    assert isinstance(ntrip.condition, IfCondition)
    predicate = ntrip.condition._IfCondition__predicate_expression
    assert len(predicate) == 1
    assert isinstance(predicate[0], LaunchConfiguration)
    assert perform_substitutions(LaunchContext(), predicate[0].variable_name) == "ntrip_enabled"

    disabled_context = LaunchContext()
    disabled_context.launch_configurations["ntrip_enabled"] = "false"
    assert ntrip.condition.evaluate(disabled_context) is False

    enabled_context = LaunchContext()
    enabled_context.launch_configurations["ntrip_enabled"] = "true"
    assert ntrip.condition.evaluate(enabled_context) is True


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


@pytest.fixture(scope='module')
def launched_incarnations():
    return set()


@pytest.mark.parametrize('parameter_scope', [None, '/**', '/universal_gnss_receiver'])
def test_live_launch_pins_incarnation_over_parameter_files(
    tmp_path, parameter_scope, launched_incarnations
):
    """Observe launch's actual process command, then query the running ROS nodes."""
    import rclpy
    import yaml
    from rcl_interfaces.srv import GetParameters
    from rclpy.executors import SingleThreadedExecutor
    from rclpy.signals import SignalHandlerOptions
    from universal_gnss_msgs.srv import GetReceiverSnapshot

    parameters = {}
    if parameter_scope is not None:
        parameters[parameter_scope] = {
            'ros__parameters': {'source_incarnation': 'conflicting-file-value'}
        }
        ntrip_scope = (
            '/**' if parameter_scope == '/**' else '/universal_gnss_ntrip'
        )
        parameters.setdefault(ntrip_scope, {'ros__parameters': {}})[
            'ros__parameters'
        ]['expected_source_incarnation'] = 'conflicting-file-value'
    parameter_file = tmp_path / 'parameters.yaml'
    parameter_file.write_text(yaml.safe_dump(parameters))

    description = load_combined_launch().generate_launch_description()
    receiver, ntrip = [a for a in description.entities if isinstance(a, Node)]
    expected = yaml.safe_load(static_parameter(receiver, 'source_incarnation'))
    assert expected == yaml.safe_load(static_parameter(ntrip, 'expected_source_incarnation'))
    assert expected not in launched_incarnations

    started = {}
    both_started = threading.Event()
    outcome = queue.Queue()
    service = LaunchService()

    def record_start(event, context):
        started[event.action] = event.cmd
        if receiver in started and ntrip in started:
            both_started.set()

    def check_live_nodes():
        context = rclpy.Context()
        client_node = None
        executor = None
        try:
            assert both_started.wait(5), 'both ROS processes must actually start'
            rclpy.init(context=context, signal_handler_options=SignalHandlerOptions.NO)
            executor = SingleThreadedExecutor(context=context)
            client_node = rclpy.create_node('combined_launch_contract', context=context)

            def call(service_type, name, request):
                client = client_node.create_client(service_type, name)
                try:
                    assert client.wait_for_service(timeout_sec=5), name
                    future = client.call_async(request)
                    rclpy.spin_until_future_complete(
                        client_node, future, executor=executor, timeout_sec=5
                    )
                    assert future.done(), name
                    assert future.exception() is None
                    return future.result()
                finally:
                    client_node.destroy_client(client)

            for node_name, parameter_name in (
                ('universal_gnss_receiver', 'source_incarnation'),
                ('universal_gnss_ntrip', 'expected_source_incarnation'),
            ):
                response = call(
                    GetParameters, f'/{node_name}/get_parameters',
                    GetParameters.Request(names=[parameter_name]),
                )
                assert response.values[0].string_value == expected
            snapshot = call(
                GetReceiverSnapshot, '/universal_gnss_receiver/get_snapshot',
                GetReceiverSnapshot.Request(),
            )
            assert snapshot.status.source_incarnation == expected
            for action, name in (
                (receiver, 'universal_gnss_receiver:source_incarnation'),
                (ntrip, 'universal_gnss_ntrip:expected_source_incarnation'),
            ):
                command = started[action]
                inline = [
                    command[i + 1]
                    for i, arg in enumerate(command[:-1])
                    if arg == '-p'
                    and command[i + 1].split(':=', 1)[0] == name
                ]
                assert len(inline) == 1, command
                assert yaml.safe_load(inline[0].split(':=', 1)[1]) == expected
                assert '--params-file' in command
            print(f'live incarnation: scope={parameter_scope}, token={expected}')
            outcome.put(None)
        except BaseException as error:
            outcome.put(error)
        finally:
            if executor is not None:
                executor.shutdown()
            if client_node is not None:
                client_node.destroy_node()
            if context.ok():
                context.shutdown()
            service.shutdown()

    # Only test-owned loopback listeners: no receiver, caster or hardware access.
    with socket.socket() as receiver_socket, socket.socket() as caster_socket:
        receiver_socket.bind(('127.0.0.1', 0))
        receiver_socket.listen()
        caster_socket.bind(('127.0.0.1', 0))
        caster_socket.listen()
        settings = {
            'parameters_file': str(parameter_file), 'receiver_family': 'ublox',
            'transport': 'tcp', 'tcp_host': '127.0.0.1',
            'tcp_port': str(receiver_socket.getsockname()[1]),
            'caster_host': '127.0.0.1',
            'caster_port': str(caster_socket.getsockname()[1]),
        }
        service.include_launch_description(LaunchDescription([
            *(SetLaunchConfiguration(key, value) for key, value in settings.items()),
            RegisterEventHandler(OnProcessStart(on_start=record_start)),
            *description.entities,
        ]))
        worker = threading.Thread(target=check_live_nodes)
        worker.start()
        try:
            assert service.run() == 0
        finally:
            service.shutdown(force_sync=True)
            worker.join(timeout=1)
        assert not worker.is_alive()
        error = outcome.get_nowait()
        if error is not None:
            raise error
        launched_incarnations.add(expected)
