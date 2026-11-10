"""Installed-consumer boundary, identity and failure propagation regressions."""

import io
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest import mock

import test_distribution as fixtures

TOOL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL))
import distribution
import consumer_validation as consumer
import validate_ci as ci


class ConsumerPipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.DistributionTests.setUpClass()
        cls.repo = fixtures.DistributionTests.repo
        cls.sha = fixtures.DistributionTests.sha

    @classmethod
    def tearDownClass(cls):
        fixtures.DistributionTests.tearDownClass()

    def validate(self, **kwargs):
        return ci.validate(self.repo, self.sha, 'ros2-lyrical', ['mavros'], 'cached',
                           manifest_bytes=distribution.json_bytes(fixtures.DistributionTests.manifest),
                           installed_consumer=True, **kwargs)

    def test_consumer_failure_propagates(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.validate(consumer_builder=mock.Mock(side_effect=subprocess.CalledProcessError(19, 'consumer')))

    def test_consumer_timeout_propagates(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            self.validate(consumer_builder=mock.Mock(side_effect=subprocess.TimeoutExpired('consumer', 300)))

    def test_success_reuses_one_build_and_binds_source(self):
        def builder(metadata, source, image):
            self.assertEqual(metadata['source_commit'], self.sha)
            self.assertEqual(metadata['options'], ['mavros'])
            self.assertEqual(image, 'cached')
            return {'result': 'pass', 'source_commit': self.sha}
        with mock.patch.object(ci, 'isolated_build', side_effect=AssertionError('duplicate build')):
            result = self.validate(consumer_builder=builder)
        self.assertEqual(result['installed_consumer']['source_commit'], self.sha)
        self.assertEqual(result['isolated_build_and_tests'], 'pass')

    def test_consumer_requires_lyrical_mavros_and_image(self):
        for profile, options, image in [('ros2-kilted', ['mavros'], 'image'),
                                         ('ros2-lyrical', [], 'image'),
                                         ('ros2-lyrical', ['mavros'], None)]:
            with self.subTest(profile=profile, options=options), self.assertRaisesRegex(distribution.DistributionError, 'requires'):
                ci.validate(self.repo, self.sha, profile, options, image, installed_consumer=True)


class ConsumerIsolationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.export = Path(self.directory.name)
        self.metadata = {'source_commit': 'a' * 40}
        (self.export / 'source.json').write_text(json.dumps(self.metadata))
        (self.export / 'install.tar').write_bytes(b'archive')
        self.fixture = self.export / 'synthetic.nmea'
        self.fixture.write_text('synthetic test input')

    def test_missing_export_fails_without_starting_container(self):
        (self.export / 'install.tar').unlink()
        with mock.patch.object(subprocess, 'run') as run, self.assertRaises(FileNotFoundError):
            consumer.consume(self.export, 'image', self.metadata, self.fixture)
        run.assert_not_called()

    def test_wrong_candidate_fails_without_starting_container(self):
        with mock.patch.object(subprocess, 'run') as run, self.assertRaisesRegex(ValueError, 'metadata'):
            consumer.consume(self.export, 'image', {'source_commit': 'b' * 40}, self.fixture)
        run.assert_not_called()

    def test_container_boundary_and_payload(self):
        def execute(command, **kwargs):
            self.assertEqual(command[:2], ['docker', 'run'])
            self.assertEqual(command[command.index('--network') + 1], 'none')
            self.assertEqual(command[command.index('--user') + 1], f'{os.getuid()}:{os.getgid()}')
            self.assertIn('-i', command)
            for forbidden in ('--volume', '-v', '--mount', '--device', '--privileged'):
                self.assertNotIn(forbidden, command)
            self.assertNotIn('/var/run/docker.sock', ' '.join(command))
            self.assertEqual(kwargs['timeout'], 300)
            with tarfile.open(fileobj=io.BytesIO(kwargs['stdin'].read())) as bundle:
                names = bundle.getnames()
                self.assertEqual({n.split('/')[0] for n in names},
                                 {'install.tar', 'source.json', 'test', 'basic_fix.nmea'})
                self.assertFalse(any('/source/' in n or '/build/' in n for n in names))
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(subprocess, 'run', side_effect=execute):
            result = consumer.consume(self.export, 'sha256:dependency', self.metadata, self.fixture)
        self.assertEqual(result['source_commit'], 'a' * 40)

    def test_consumer_failure_removes_created_container(self):
        calls = []
        def execute(command, **kwargs):
            calls.append(command)
            if command[1] == 'run':
                Path(command[command.index('--cidfile') + 1]).write_text('our-container')
                raise subprocess.CalledProcessError(5, command)
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(subprocess, 'run', side_effect=execute), self.assertRaises(subprocess.CalledProcessError):
            consumer.consume(self.export, 'image', self.metadata, self.fixture)
        self.assertEqual(calls[-1], ['docker', 'rm', '-f', 'our-container'])

    def test_producer_failure_prevents_consumer_and_removes_container(self):
        calls = []
        def execute(command, **kwargs):
            calls.append(command)
            if command[0] == 'bash':
                (Path(command[-1]) / 'producer-container-id').write_text('producer')
                raise subprocess.TimeoutExpired(command, 1800)
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(subprocess, 'check_output', return_value='sha256:fixed\n'), \
                mock.patch.object(subprocess, 'run', side_effect=execute), \
                mock.patch.object(consumer, 'consume') as consume, \
                self.assertRaises(subprocess.TimeoutExpired):
            consumer.validate(self.metadata, Path('/generated'), 'mutable-tag')
        consume.assert_not_called()
        self.assertEqual(calls[0][-2], 'sha256:fixed')
        self.assertEqual(calls[-1], ['docker', 'rm', '-f', 'producer'])

    def test_unexpected_negative_failure_does_not_count_as_rejection(self):
        for result in (subprocess.CompletedProcess([], 0, 'missing CMake export'),
                       subprocess.CompletedProcess([], 1, 'unrelated error')):
            with self.subTest(result=result), mock.patch.object(subprocess, 'run', return_value=result), \
                    self.assertRaisesRegex(ValueError, 'did not reject'):
                consumer.consume(self.export, 'image', self.metadata, self.fixture,
                                 expected_error='missing CMake export')

    def test_negative_controls_remove_real_contracts(self):
        with tarfile.open(self.export / 'install.tar', 'w') as archive:
            for name in ('./local_setup.bash',
                         './universal_gnss_ros2/share/universal_gnss_ros2/cmake/universal_gnss_ros2Config.cmake',
                         './universal_gnss_mavros/lib/libuniversal_gnss_mavros_plugin.so'):
                entry = tarfile.TarInfo(name)
                entry.size = 4
                archive.addfile(entry, io.BytesIO(b'test'))
        def check(export, image, metadata, fixture, **kwargs):
            with tarfile.open(Path(export) / 'install.tar') as archive:
                names = archive.getnames()
            if kwargs['expected_error'] == 'missing relocated install prefix':
                self.assertEqual(names, [])
            elif kwargs['expected_error'] == 'missing CMake export':
                self.assertFalse(any(n.endswith('Config.cmake') for n in names))
            else:
                self.assertFalse(any(n.endswith('.so') for n in names))
            return {'result': 'expected_rejection'}
        with mock.patch.object(consumer, 'consume', side_effect=check) as call:
            result = consumer.negative_exports(self.export, 'image', self.metadata, self.fixture)
        self.assertEqual(call.call_count, 3)
        self.assertEqual(set(result), {'prefix', 'export', 'plugin'})

    def test_negative_control_requires_target_present(self):
        with tarfile.open(self.export / 'install.tar', 'w') as archive:
            archive.addfile(tarfile.TarInfo('./local_setup.bash'))
        with mock.patch.object(consumer, 'consume', return_value={'result': 'expected_rejection'}), \
                self.assertRaisesRegex(ValueError, 'did not remove'):
            consumer.negative_exports(self.export, 'image', self.metadata, self.fixture)

    def test_actual_process_cleanup_runs_when_assertion_fails(self):
        spec = importlib.util.spec_from_file_location('installed_checks', TOOL / 'consumer/check.py')
        checks = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(checks)
        code = ('import signal,sys,time; '
                'signal.signal(signal.SIGINT,lambda *_:sys.exit(0)); '
                'print("READY",flush=True); time.sleep(60)')
        name = 'cleanup-' + self.export.name
        with self.assertRaisesRegex(ValueError, 'synthetic failed check'):
            with checks.running([sys.executable, '-c', code], name) as process:
                deadline = time.monotonic() + 3
                log = Path('/tmp') / ('ug-consumer-' + name + '.log')
                while 'READY' not in log.read_text() and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertIn('READY', log.read_text())
                raise ValueError('synthetic failed check')
        self.assertEqual(process.poll(), 0)


if __name__ == '__main__':
    unittest.main()
