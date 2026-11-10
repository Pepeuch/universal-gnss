"""CI-03 parser contract, health state evidence and failure propagation."""

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

import yaml

TOOL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL))
import docker_validation as validation


class ComposeContractTests(unittest.TestCase):
    def setUp(self):
        self.values = {'UNIVERSAL_GNSS_IMAGE': 'universal-gnss:test', 'GNSS_DEVICE': '/dev/fake',
                       'GNSS_GROUP': '4242', 'UNIVERSAL_GNSS_PARAMETERS_FILE': '/tmp/parameters.yaml',
                       'UNIVERSAL_GNSS_LOG_DIR': '/tmp/log', 'UNIVERSAL_GNSS_EXPORT_DIR': '/tmp/export'}
        self.service = {
            'image': 'universal-gnss:test', 'restart': 'unless-stopped', 'group_add': ['4242'],
            'devices': [{'source': '/dev/fake', 'target': '/dev/gnss-receiver', 'permissions': 'rwm'}],
            'environment': {'ROS_DOMAIN_ID': '42', 'UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION': '1',
                            'UNIVERSAL_GNSS_EXPORT_DIR': '/var/lib/universal_gnss/export'},
            'logging': {'driver': 'local', 'options': {'max-size': '10m', 'max-file': '5'}},
            'volumes': [
                {'type': 'bind', 'source': '/tmp/parameters.yaml', 'target': '/etc/universal_gnss/parameters.yaml', 'read_only': True},
                {'type': 'bind', 'source': '/tmp/log', 'target': '/var/log/universal_gnss'},
                {'type': 'bind', 'source': '/tmp/export', 'target': '/var/lib/universal_gnss/export'}]}

    def validate(self, service):
        validation.validate_resolved({'services': {'universal-gnss': service}}, self.values)

    def test_expected_mounts_environment_and_policy(self):
        self.validate(self.service)

    def test_wrong_image_device_group_restart_or_logging_rejected(self):
        for key, value in [('image', 'other:tag'), ('devices', []), ('group_add', ['0']),
                            ('restart', 'no'), ('logging', {'driver': 'none'})]:
            with self.subTest(key=key), self.assertRaises(AssertionError):
                self.validate({**self.service, key: value})

    def test_writable_parameters_missing_export_and_wrong_source_rejected(self):
        for mutation in ('rw', 'missing', 'source'):
            service = copy.deepcopy(self.service)
            if mutation == 'rw':
                service['volumes'][0]['read_only'] = False
            elif mutation == 'missing':
                service['volumes'].pop()
            else:
                service['volumes'][1]['source'] = '/unexpected'
            with self.subTest(mutation=mutation), self.assertRaises(AssertionError):
                self.validate(service)

    def test_schema_and_ros_environment_not_ignored(self):
        service = copy.deepcopy(self.service)
        service['environment']['ROS_DOMAIN_ID'] = 'wrong'
        with self.assertRaises(AssertionError):
            self.validate(service)

    def test_missing_compose_fails_instead_of_skipping(self):
        with mock.patch.object(validation, 'run', return_value=subprocess.CompletedProcess([], 1, 'missing plugin')), \
                self.assertRaises(subprocess.CalledProcessError):
            validation.compose(Path('/repo'))

    def test_operator_environment_cannot_supply_missing_required_variable(self):
        environments = []
        def execute(command, **kwargs):
            if command[-1] == 'version':
                return subprocess.CompletedProcess(command, 0, 'compose test')
            environments.append(kwargs['env'])
            # Deliberate early failure after inspecting the actual command/environment.
            self.assertIn('--env-file', command)
            self.assertIn('config', command)
            return subprocess.CompletedProcess(command, 1, 'stop after environment inspection')
        with mock.patch.dict(os.environ, dict.fromkeys(validation.REQUIRED, 'operator-value')), \
                mock.patch.object(validation, 'run', side_effect=execute), \
                self.assertRaises(subprocess.CalledProcessError):
            validation.compose(Path('/repo'))
        self.assertTrue(environments)
        self.assertTrue(all(not set(env) & set(validation.REQUIRED) for env in environments))


def snapshot(status, streak=0):
    logs = ([{'ExitCode': 0, 'Start': '01', 'End': '02'}] if status == 'healthy' else
            [{'ExitCode': 1, 'Start': t, 'End': t} for t in ('03', '04', '05')])
    return {'State': {'Running': True, 'Health': {'Status': status, 'FailingStreak': streak, 'Log': logs}},
            'Config': {'Healthcheck': {'Retries': 3,
                                      'Test': ['CMD', '/usr/local/bin/universal-gnss-healthcheck', '--timeout', '2'],
                                      'Interval': 2_000_000_000}}}


class DockerHealthTests(unittest.TestCase):
    def test_healthy_requires_an_executed_probe(self):
        data = snapshot('healthy')
        data['State']['Health']['Log'] = []
        with mock.patch.object(validation, 'inspect_container', return_value=data), self.assertRaisesRegex(AssertionError, 'executed'):
            validation.wait_health('container', 'healthy')

    def test_missing_healthcheck_and_exited_container_fail(self):
        for state in ({'Running': False}, {'Running': True}):
            with self.subTest(state=state), mock.patch.object(validation, 'inspect_container', return_value={'State': state}), \
                    self.assertRaises(AssertionError):
                validation.wait_health('container', 'healthy')

    def test_timeout_waiting_for_either_state_fails(self):
        for desired in ('healthy', 'unhealthy'):
            other = 'unhealthy' if desired == 'healthy' else 'healthy'
            with self.subTest(desired=desired), mock.patch.object(validation.time, 'monotonic', side_effect=[0, 1, 100]), \
                    mock.patch.object(validation.time, 'sleep'), \
                    mock.patch.object(validation, 'inspect_container', return_value=snapshot(other)), \
                    self.assertRaisesRegex(TimeoutError, desired):
                validation.wait_health('container', desired)

    def test_three_consecutive_failures_after_interruption_required(self):
        data = snapshot('unhealthy', 3)
        validation.verify_failures(data, '02')
        for mutation in ('streak', 'success', 'timeout', 'old', 'missing'):
            broken = copy.deepcopy(data)
            if mutation == 'streak':
                broken['State']['Health']['FailingStreak'] = 2
            elif mutation == 'success':
                broken['State']['Health']['Log'][-1]['ExitCode'] = 0
            elif mutation == 'timeout':
                broken['State']['Health']['Log'][-1]['ExitCode'] = -1
            elif mutation == 'old':
                broken['State']['Health']['Log'][0]['Start'] = '00'
            else:
                broken['State']['Health']['Log'].pop()
            with self.subTest(mutation=mutation), self.assertRaises(AssertionError):
                validation.verify_failures(broken, '02')

    def execute_flow(self, cleanup_failure=False, diagnostics_failure=False):
        calls = []
        health = snapshot('healthy')
        image = {'Id': 'sha256:production', 'Config': {
            'Env': ['ROS_DISTRO=lyrical'], 'User': '1000:1000',
            'Labels': {'org.opencontainers.image.revision': 'a' * 40},
            'Healthcheck': health['Config']['Healthcheck']}}
        def execute(args, **kwargs):
            calls.append(args)
            if args[:3] == ['docker', 'image', 'inspect']:
                return json.dumps([image])
            if args[:2] == ['docker', 'create']:
                self.assertEqual(args[args.index('--network') + 1], 'none')
                self.assertNotIn('--privileged', args)
                self.assertNotIn('--device', args)
                self.assertFalse(any(a.startswith('--health-cmd') for a in args))
                return 'our-container\n'
            if args[:3] == ['docker', 'rm', '-f'] and cleanup_failure:
                raise subprocess.CalledProcessError(1, args)
            return ''
        with mock.patch.object(validation, 'checked', side_effect=execute), \
                mock.patch.object(validation, 'wait_health', side_effect=[health, snapshot('unhealthy', 3)]), \
                mock.patch.object(validation, 'run', return_value=subprocess.CompletedProcess([], 0, 'logs')), \
                mock.patch.object(validation, 'inspect_container', side_effect=RuntimeError('diagnostics') if diagnostics_failure else None,
                                  return_value=snapshot('unhealthy', 3)):
            try:
                result = validation.healthcheck('production:tag', 'a' * 40)
            finally:
                self.assertEqual(calls[-1], ['docker', 'rm', '-f', 'our-container'])
        self.assertEqual(result['transitions'], ['healthy', 'unhealthy'])

    def test_actual_orchestration_preserves_probe_and_isolation(self):
        self.execute_flow()

    def test_cleanup_failure_is_not_masked(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.execute_flow(cleanup_failure=True)

    def test_diagnostics_failure_still_cleans_up(self):
        with self.assertRaisesRegex(RuntimeError, 'diagnostics'):
            self.execute_flow(diagnostics_failure=True)


class WorkflowTests(unittest.TestCase):
    def test_only_lyrical_steps_reuse_the_existing_image(self):
        text = (TOOL.parents[1] / '.github/workflows/docker.yml').read_text()
        jobs = yaml.load(text, Loader=yaml.BaseLoader)['jobs']
        self.assertEqual(set(jobs), {'docker-build-and-smoke'})
        job = jobs['docker-build-and-smoke']
        self.assertEqual(job['strategy']['matrix']['ros_distro'], ['kilted', 'lyrical'])
        steps = job['steps']
        added = [step for step in steps if 'docker_validation.py' in step.get('run', '')]
        self.assertEqual(len(added), 2)
        self.assertTrue(all(step['if'] == "matrix.ros_distro == 'lyrical'" for step in added))
        self.assertTrue(all('timeout-minutes' in step and 'continue-on-error' not in step for step in added))
        self.assertIn('--image "$IMAGE" --source-sha "$SOURCE_SHA"', added[-1]['run'])
        self.assertEqual(sum('docker build ' in step.get('run', '') for step in steps), 1)
        for name in ('Verify image contract', 'Verify external runtime persistence contract',
                     'Verify Docker DNS and NTRIP reconnect contract'):
            self.assertTrue(any(step['name'] == name for step in steps))
        self.assertNotIn('docker push', text)


if __name__ == '__main__':
    unittest.main()
