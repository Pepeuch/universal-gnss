"""Real Compose parsing and production-image health transitions, Lyrical only."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
REQUIRED = ('UNIVERSAL_GNSS_IMAGE', 'GNSS_DEVICE', 'GNSS_GROUP',
            'UNIVERSAL_GNSS_PARAMETERS_FILE', 'UNIVERSAL_GNSS_LOG_DIR', 'UNIVERSAL_GNSS_EXPORT_DIR')


def run(args, **kwargs):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30, **kwargs)
    return result


def checked(args, **kwargs):
    result = run(args, **kwargs)
    if result.returncode:
        print(result.stdout, flush=True)
        result.check_returncode()
    return result.stdout


def validate_resolved(config, values):
    service = config['services']['universal-gnss']
    assert service['image'] == values['UNIVERSAL_GNSS_IMAGE']
    assert service['devices'] == [{'source': values['GNSS_DEVICE'], 'target': '/dev/gnss-receiver',
                                   'permissions': 'rwm'}]
    assert service['group_add'] == [values['GNSS_GROUP']]
    volumes = {item['target']: item for item in service['volumes']}
    expected = {'/etc/universal_gnss/parameters.yaml': ('UNIVERSAL_GNSS_PARAMETERS_FILE', True),
                '/var/log/universal_gnss': ('UNIVERSAL_GNSS_LOG_DIR', False),
                '/var/lib/universal_gnss/export': ('UNIVERSAL_GNSS_EXPORT_DIR', False)}
    assert set(volumes) == set(expected)
    for target, (variable, readonly) in expected.items():
        assert volumes[target]['type'] == 'bind'
        assert volumes[target]['source'] == values[variable]
        assert volumes[target].get('read_only', False) == readonly
    assert service['environment'] == {'ROS_DOMAIN_ID': values.get('ROS_DOMAIN_ID') or '42',
                                      'UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION':
                                      values.get('UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION') or '1',
                                      'UNIVERSAL_GNSS_EXPORT_DIR': '/var/lib/universal_gnss/export'}
    assert service['restart'] == 'unless-stopped'
    assert service['logging'] == {'driver': 'local', 'options': {'max-size': '10m', 'max-file': '5'}}


def compose(repo):
    version = checked(['docker', 'compose', 'version']).strip()
    print(version, flush=True)
    # No real operator environment can satisfy a deliberately omitted test input.
    environment = {k: v for k, v in os.environ.items()
                   if k in ('PATH', 'HOME', 'DOCKER_CONFIG', 'DOCKER_HOST', 'DOCKER_CONTEXT')}
    with tempfile.TemporaryDirectory(prefix='ug-compose-') as directory:
        root = Path(directory)
        values = {'UNIVERSAL_GNSS_IMAGE': 'universal-gnss:ci03-synthetic',
                  'GNSS_DEVICE': '/dev/ug-ci03-does-not-exist', 'GNSS_GROUP': '4242',
                  'UNIVERSAL_GNSS_PARAMETERS_FILE': str(root / 'parameters.yaml'),
                  'UNIVERSAL_GNSS_LOG_DIR': str(root / 'logs'),
                  'UNIVERSAL_GNSS_EXPORT_DIR': str(root / 'export'),
                  'ROS_DOMAIN_ID': '73', 'UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION': '1'}
        env_file = root / 'test.env'
        command = ['docker', 'compose', '--env-file', str(env_file), '-f', str(repo / 'docker/compose.yaml'),
                   'config', '--format', 'json']

        def resolve(inputs):
            env_file.write_text(''.join(k + '=' + v + '\n' for k, v in inputs.items()))
            return run(command, env=environment)

        for defaults in (False, True):
            inputs = values.copy()
            if defaults:
                del inputs['ROS_DOMAIN_ID'], inputs['UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION']
            result = resolve(inputs)
            if result.returncode:
                print(result.stdout, flush=True)
            result.check_returncode()
            config = json.loads(result.stdout)
            validate_resolved(config, inputs)
            print('PASS resolved Compose configuration', json.dumps(config['services']), flush=True)
        for variable in REQUIRED:
            for empty in (False, True):
                inputs = values.copy()
                if empty:
                    inputs[variable] = ''
                else:
                    del inputs[variable]
                result = resolve(inputs)
                assert result.returncode != 0 and variable in result.stdout, (variable, result.stdout)
                print('PASS required Compose input rejected', variable, 'empty' if empty else 'missing')
        for variable, value, diagnostic in [('GNSS_DEVICE', 'bad:target:rw:extra', 'device'),
                                            ('UNIVERSAL_GNSS_PARAMETERS_FILE', 'bad:rw:extra', 'spec')]:
            inputs = {**values, variable: value}
            result = resolve(inputs)
            assert result.returncode != 0 and diagnostic in result.stdout.lower(), result.stdout
            print('PASS invalid Compose mapping rejected', variable, result.stdout.strip(), flush=True)
    return {'compose': 'pass', 'version': version, 'positive_cases': 2, 'negative_cases': 14,
            'compose_startup': 'not_requested'}


PROBE = '''
import importlib.util,json,sys
spec=importlib.util.spec_from_file_location('installed_health','/usr/local/libexec/universal-gnss-healthcheck.py')
health=importlib.util.module_from_spec(spec);spec.loader.exec_module(health)
assert health.DEFAULT_SERVICES==('/universal_gnss_receiver/get_health','/universal_gnss_ntrip/get_health')
health.rclpy.init();node=health.rclpy.create_node('ci03_direct_service_probe')
try:
    actual=[health.service_is_responsive(node,name,2.0) for name in health.DEFAULT_SERVICES]
    print(json.dumps(dict(zip(health.DEFAULT_SERVICES,actual))),flush=True)
    assert actual==([True,True] if sys.argv[1]=='healthy' else [True,False])
finally:
    node.destroy_node();health.rclpy.shutdown()
'''

STOP_NTRIP = '''
import os,pathlib,signal
expected=b'/opt/universal_gnss/install/lib/universal_gnss_ros2/ntrip_node'
pids=[]
for proc in pathlib.Path('/proc').glob('[0-9]*'):
    try:
        if (proc/'cmdline').read_bytes().split(b'\\0')[0]==expected:pids.append(int(proc.name))
    except FileNotFoundError:pass
assert len(pids)==1,pids
os.kill(pids[0],signal.SIGSTOP)
print('NTRIP service process suspended',pids[0],flush=True)
'''


def inspect_container(cid):
    return json.loads(checked(['docker', 'inspect', cid]))[0]


def wait_health(cid, desired, timeout=90):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        snapshot = inspect_container(cid)
        assert snapshot['State']['Running'], 'container exited instead of changing health'
        health = snapshot['State'].get('Health')
        assert health is not None, 'Docker healthcheck was not configured/executed'
        if health['Status'] == desired:
            assert health.get('Log'), 'health status without an executed probe'
            return snapshot
        time.sleep(1)
    raise TimeoutError('Docker health did not reach ' + desired)


def verify_failures(snapshot, last_success):
    health = snapshot['State']['Health']
    retries = snapshot['Config']['Healthcheck']['Retries']
    assert retries == 3
    assert health['FailingStreak'] >= retries
    logs = health['Log'][-retries:]
    # The installed probe must finish with its ordinary negative verdict;
    # a Docker timeout (-1) or command execution failure is not evidence of this scenario.
    assert len(logs) == retries and all(item['ExitCode'] == 1 for item in logs)
    assert all(item['Start'] > last_success for item in logs), 'failures precede controlled interruption'


def healthcheck(image, source_sha):
    started = time.monotonic()
    metadata = json.loads(checked(['docker', 'image', 'inspect', image]))[0]
    config = metadata['Config']
    assert config['Labels']['org.opencontainers.image.revision'] == source_sha
    assert 'ROS_DISTRO=lyrical' in config['Env'] and config['User'] == '1000:1000'
    expected_probe = ['CMD', '/usr/local/bin/universal-gnss-healthcheck', '--timeout', '2']
    assert config['Healthcheck']['Test'] == expected_probe and config['Healthcheck']['Retries'] == 3
    with tempfile.TemporaryDirectory(prefix='ug-health-') as directory:
        root = Path(directory)
        parameters = root / 'universal_gnss'
        parameters.mkdir()
        (parameters / 'parameters.yaml').write_text('''
/universal_gnss_receiver:
  ros__parameters:
    transport: tcp
    tcp_host: 127.0.0.1
    tcp_port: 9
/universal_gnss_ntrip:
  ros__parameters:
    caster_host: 127.0.0.1
    caster_port: 9
    mountpoint: RTCM3
    username: ""
    password: ""
    gga_enabled: false
    tls_enabled: false
''')
        cid = checked(['docker', 'create', '--pull=never', '--network', 'none',
                       '--env', 'ROS_LOCALHOST_ONLY=1', '--env', 'ROS_DOMAIN_ID=74',
                       '--health-interval=2s', '--health-start-period=1s', metadata['Id']]).strip()
        try:
            # Docker API copy avoids host-source/device mounts, including with remote daemons.
            checked(['docker', 'cp', str(parameters), cid + ':/etc/'])
            checked(['docker', 'start', cid])
            healthy = wait_health(cid, 'healthy')
            actual_config = healthy['Config']['Healthcheck']
            assert actual_config['Test'] == expected_probe
            assert actual_config['Retries'] == 3 and actual_config['Interval'] == 2_000_000_000
            assert healthy['State']['Health']['FailingStreak'] == 0
            last = healthy['State']['Health']['Log'][-1]
            assert last['ExitCode'] == 0
            print('PASS Docker healthy', json.dumps(healthy['State']['Health']), flush=True)
            service_command = ['docker', 'exec', '-i', cid, '/usr/local/bin/universal-gnss-entrypoint',
                               'python3', '-', 'healthy']
            print(checked(service_command, input=PROBE), flush=True)
            print(checked(['docker', 'exec', '-i', cid, 'python3', '-'], input=STOP_NTRIP), flush=True)
            unhealthy = wait_health(cid, 'unhealthy')
            verify_failures(unhealthy, last['End'])
            print(checked(service_command[:-1] + ['unhealthy'], input=PROBE), flush=True)
            print('PASS Docker unhealthy', json.dumps(unhealthy['State']['Health']), flush=True)
        finally:
            # Diagnostics are best effort; cleanup failure itself must still fail validation.
            try:
                diagnostic = run(['docker', 'logs', '--tail', '80', cid])
                print(diagnostic.stdout, flush=True)
                print(json.dumps(inspect_container(cid)['State']), flush=True)
            finally:
                checked(['docker', 'rm', '-f', cid])
    return {'docker_health': 'pass', 'source_sha': source_sha, 'image_id': metadata['Id'],
            'transitions': ['healthy', 'unhealthy'], 'retries': 3,
            'seconds': round(time.monotonic() - started, 2), 'gnss_quality': 'not_qualified'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    sub.add_parser('compose').add_argument('--repo', type=Path, default=ROOT)
    health = sub.add_parser('healthcheck')
    health.add_argument('--image', required=True)
    health.add_argument('--source-sha', required=True)
    args = parser.parse_args(argv)
    if os.getuid() == 0:
        raise ValueError('run qualification as the normal project user')
    result = compose(args.repo) if args.mode == 'compose' else healthcheck(args.image, args.source_sha)
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
