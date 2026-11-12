"""Contracts and bounded synthetic ROS checks using only the relocated installation."""

import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import signal
import subprocess
import time
import xml.etree.ElementTree as ET


PACKAGES = ('universal_gnss_msgs', 'universal_gnss_ros2', 'universal_gnss_mavros')
LAUNCHES = ('receiver_and_ntrip.launch.py', 'receiver_tcp.launch.py',
            'receiver_serial.launch.py', 'ntrip.launch.py', 'replay.launch.py')


def command(args):
    result = subprocess.run([str(a) for a in args], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=30)
    print(result.stdout, end='', flush=True)
    result.check_returncode()
    return result.stdout


def contracts(prefix):
    from ament_index_python.packages import get_package_share_directory
    from rclpy.serialization import deserialize_message, serialize_message
    from universal_gnss_msgs.msg import GnssStatus, RtcmFrame
    from universal_gnss_msgs.srv import GetReceiverSnapshot

    metadata = json.loads(Path('source.json').read_text())
    assert len(metadata['source_commit']) == 40
    assert metadata['distribution'] == 'ros2-lyrical' and 'mavros' in metadata['options']
    print('CANDIDATE', metadata['source_commit'], flush=True)
    for name in PACKAGES:
        expected = prefix / name
        assert Path(command(['ros2', 'pkg', 'prefix', name]).strip()).resolve() == expected.resolve()
        share = Path(get_package_share_directory(name))
        assert share.resolve() == (expected / 'share' / name).resolve()
        assert ET.parse(share / 'package.xml').findtext('name') == name
        assert (share / 'cmake' / (name + 'Config.cmake')).is_file(), 'missing CMake export'
    for interface in ('msg/GnssStatus', 'msg/RtcmFrame', 'srv/GetReceiverSnapshot'):
        command(['ros2', 'interface', 'show', 'universal_gnss_msgs/' + interface])
    for message_type in (GnssStatus, RtcmFrame, GetReceiverSnapshot.Request, GetReceiverSnapshot.Response):
        assert message_type.__module__.startswith('universal_gnss_msgs.')
        message = message_type()
        assert deserialize_message(serialize_message(message), message_type) == message
    print('PASS canonical ROSIDL, type support and serialization', flush=True)

    for path in sorted(prefix.rglob('*')):
        if not path.is_file():
            continue
        with path.open('rb') as data:
            elf = data.read(4) == b'\x7fELF'
        if not elf:
            continue
        dependencies = command(['ldd', path])
        assert 'not found' not in dependencies, str(path)
        for line in dependencies.splitlines():
            if 'universal_gnss' in line and '=>' in line:
                resolved = Path(line.split('=>', 1)[1].split('(', 1)[0].strip()).resolve()
                assert resolved.is_relative_to(prefix.resolve()), 'UG dependency outside relocated prefix'
        dynamic = command(['readelf', '-d', path])
        assert '/tmp/ug-work/' not in dynamic, 'functional producer RPATH/RUNPATH'
    share = prefix / 'universal_gnss_ros2/share/universal_gnss_ros2'
    assert {p.name for p in (share / 'launch').glob('*.launch.py')} == set(LAUNCHES)
    parameters = share / 'config/empty_parameters.yaml'
    assert parameters.is_file()
    for launch in LAUNCHES:
        command(['ros2', 'launch', 'universal_gnss_ros2', launch, '--show-args'])
    # launch_ros uses the installed parameter file without creating a receiver.
    from launch import LaunchContext
    from launch_ros.parameter_descriptions import ParameterFile
    assert Path(ParameterFile(str(parameters)).evaluate(LaunchContext())).resolve() == parameters.resolve()
    print('PASS ELF dependencies, five installed launch files and parameters', flush=True)


@contextmanager
def running(args, name):
    """Always reap the process group, including failed assertions and timeout paths."""
    log = Path('/tmp') / ('ug-consumer-' + name + '.log')
    with log.open('w') as output:
        process = subprocess.Popen([str(a) for a in args], stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            yield process
        finally:
            try:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                import os
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=2)
            finally:
                print(f'PROCESS_LOG {name}\n{log.read_text()}', flush=True)
            assert process.returncode == 0, (name, process.returncode)


def smoke(prefix, fixture):
    import rclpy
    from std_srvs.srv import Trigger
    from universal_gnss_msgs.msg import GnssStatus

    executables = prefix / 'universal_gnss_ros2/lib/universal_gnss_ros2'
    cases = {'receiver_node': ['transport:=tcp', 'tcp_host:=127.0.0.1', 'tcp_port:=9'],
             'ntrip_node': ['caster_host:=127.0.0.1', 'caster_port:=9', 'mountpoint:=RTCM3']}
    for name, parameters in cases.items():
        args = [executables / name, '--ros-args']
        for parameter in parameters:
            args.extend(['-p', parameter])
        with running(args, name) as process:
            time.sleep(1)
            assert process.poll() is None, 'node exited before controlled shutdown'
        print('PASS installed start/stop', name, flush=True)

    rclpy.init()
    node = rclpy.create_node('ug_installed_consumer')
    received = []
    subscription = node.create_subscription(GnssStatus, '/ci02/status', received.append, 10)
    client = node.create_client(Trigger, '/ci02/replay/step')
    args = [executables / 'replay_node', '--ros-args', '-r', '__node:=replay', '-r', '__ns:=/ci02',
            '-p', 'input_path:=' + str(fixture.resolve()), '-p', 'replay_mode:=stepped']
    try:
        with running(args, 'replay_node') as process:
            assert client.wait_for_service(timeout_sec=10), 'replay service not discovered'
            # Let the subscription discover the publisher before stepping a single observation.
            deadline = time.monotonic() + 5
            while not node.count_publishers('/ci02/status') and time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.1)
            assert node.count_publishers('/ci02/status'), 'status publisher not discovered'
            future = client.call_async(Trigger.Request())
            rclpy.spin_until_future_complete(node, future, timeout_sec=10)
            assert future.done() and future.result().success, 'replay step failed'
            deadline = time.monotonic() + 5
            while not received and time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.1)
            assert received and isinstance(received[0], GnssStatus), 'no canonical GnssStatus received'
            assert process.poll() is None
            print('PASS installed replay Trigger and canonical GnssStatus receipt', flush=True)
    finally:
        node.destroy_subscription(subscription)
        node.destroy_node()
        rclpy.shutdown()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--contracts-only', action='store_true')
    args = parser.parse_args()
    if args.contracts_only:
        contracts(args.prefix)
    else:
        smoke(args.prefix, args.input)


if __name__ == '__main__':
    main()
