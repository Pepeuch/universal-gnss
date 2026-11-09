"""Behavioral tests for closure, immutable inputs, safety and distribution integrity."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ug_distribution', TOOL / 'distribution.py')
d = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(d)


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True, stderr=subprocess.PIPE).strip()


class DistributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix='ug-generator-tests-')
        cls.repo = Path(cls.workspace.name) / 'source'
        cls.repo.mkdir()
        git(cls.repo, 'init', '-q')
        files = {
            'LICENSE': 'Test fixture license\n',
            'core/gnss_core/include/universal_gnss/types.hpp': '#pragma once\n',
            'protocols/gnss_protocols/src/parser.cpp': '#include "universal_gnss/types.hpp"\n',
            'gnss_tools/src/tool.cpp': '#include "universal_gnss/types.hpp"\n',
            'gnss_ros2/CMakeLists.txt': 'add_subdirectory("${UNIVERSAL_GNSS_SOURCE_ROOT}/gnss_tools" out)\n',
            'gnss_ros2/package.xml': '<package><name>universal_gnss_ros2</name><version>0.7.0</version><depend>universal_gnss_msgs</depend></package>\n',
            'universal_gnss_msgs/package.xml': '<package><name>universal_gnss_msgs</name><version>0.7.0</version></package>\n',
            'universal_gnss_msgs/msg/GnssStatus.msg': 'bool valid\n',
            'gnss_mavros/src/adapter.cpp': '#include "universal_gnss/types.hpp"\n',
            'docs/history.md': 'Immutable historical fixture\n',
        }
        for name, text in files.items():
            path = cls.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        git(cls.repo, 'add', '.')
        git(cls.repo, '-c', 'user.name=Generator Test', '-c', 'user.email=test@example.invalid',
            'commit', '-qm', 'Synthetic source fixture')
        cls.sha = git(cls.repo, 'rev-parse', 'HEAD')
        profiles = ['ros2-kilted', 'ros2-lyrical']
        def component(paths, deps=()):
            return {'paths': paths, 'requires': list(deps), 'external': [], 'platforms': ['linux'],
                    'frameworks': ['ros2'], 'hardware_profiles': ['generic-posix'], 'distributions': profiles}
        cls.manifest = {
            'schema_version': 1, 'ug_version': 'v0.7.4',
            'components': {
                'legal': component(['LICENSE']),
                'core': component(['core/gnss_core/']),
                'protocols': component(['protocols/gnss_protocols/'], ['core']),
                'tools': component(['gnss_tools/'], ['protocols']),
                'messages': component(['universal_gnss_msgs/']),
                'ros2': component(['gnss_ros2/'], ['tools', 'messages', 'legal']),
                'mavros': component(['gnss_mavros/'], ['ros2', 'core']),
            },
            'distributions': {p: {'platform': 'linux', 'framework': 'ros2',
                                  'hardware_profile': 'generic-posix', 'ros_distro': p.split('-')[-1],
                                  'roots': ['ros2'], 'options': {'mavros': 'mavros'},
                                  'forbidden': ['docs/', '.git/']} for p in profiles},
            'exclude': [{'path': 'docs/', 'reason': 'Historical fixture excluded'}],
            'references': [],
        }

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.manifest = copy.deepcopy(type(self).manifest)
        self.snapshot = d.Snapshot(self.repo, self.sha)
        self.output_root = tempfile.TemporaryDirectory(prefix='ug-generator-output-')
        self.addCleanup(self.output_root.cleanup)
        self.destination = Path(self.output_root.name) / 'distribution'

    def plan(self, profile='ros2-lyrical', options=(), excluded=()):
        return d.plan(self.snapshot, d.json_bytes(self.manifest), profile, options, excluded)

    def reject(self, pattern, **kwargs):
        with self.assertRaisesRegex(d.DistributionError, pattern):
            self.plan(**kwargs)

    def test_transitive_diamond_closure(self):
        selected = d.closure(self.manifest, 'ros2-kilted', ['mavros'], [])
        self.assertEqual(selected, sorted(self.manifest['components']))
        self.assertEqual(selected.count('core'), 1)

    def test_unknown_dependency(self):
        self.manifest['components']['tools']['requires'].append('not_known')
        self.reject('unknown dependency')

    def test_cycle_even_in_unselected_component(self):
        self.manifest['components']['mavros']['requires'].append('mavros')
        self.reject('dependency cycle')

    def test_missing_path(self):
        self.manifest['components']['core']['paths'] = ['does-not-exist/']
        self.reject('path does not exist')

    def test_unsafe_paths(self):
        for path in ['../escape', '/absolute', 'a/../../b', 'a\\b', '.git/config']:
            with self.subTest(path=path):
                self.manifest['components']['core']['paths'] = [path]
                self.reject('unsafe path|invalid path')

    def test_unclassified_new_file(self):
        self.snapshot.tree['new_receiver/new.cpp'] = self.snapshot.tree['protocols/gnss_protocols/src/parser.cpp']
        self.reject('unclassified source file')

    def test_unclassified_sibling_protocol_is_not_silently_distributed(self):
        self.snapshot.tree['protocols/future_receiver/new.cpp'] = self.snapshot.tree['protocols/gnss_protocols/src/parser.cpp']
        self.reject('unclassified source file')

    def test_unclassified_sibling_core_is_not_silently_distributed(self):
        self.snapshot.tree['core/future_backend/new.cpp'] = self.snapshot.tree['protocols/gnss_protocols/src/parser.cpp']
        self.reject('unclassified source file')

    def test_ambiguous_owner(self):
        self.manifest['components']['tools']['paths'].append('core/gnss_core/')
        self.reject('ambiguous classification')

    def test_required_component_exclusion(self):
        self.reject('cannot exclude required dependency', excluded=['tools'])

    def test_missing_transitive_edge_exposes_source_reference(self):
        self.manifest['components']['ros2']['requires'].remove('tools')
        self.reject('internal reference not distributed')

    def test_missing_internal_ros_package(self):
        self.manifest['components']['ros2']['requires'].remove('messages')
        self.reject('internal reference not distributed')

    def test_incompatible_platform(self):
        self.manifest['components']['core']['platforms'] = ['esp32']
        self.reject('platform/framework/hardware incompatibility')

    def test_incompatible_framework(self):
        self.manifest['components']['core']['frameworks'] = ['esp-idf']
        self.reject('platform/framework/hardware incompatibility')

    def test_incompatible_hardware_profile(self):
        self.manifest['components']['core']['hardware_profiles'] = ['future-board']
        self.reject('platform/framework/hardware incompatibility')

    def test_unqualified_distribution(self):
        self.reject('unknown distribution', profile='esp32')
        self.manifest['distributions']['ros2-kilted']['ros_distro'] = 'humble'
        self.reject('unqualified ROS distro')

    def test_distribution_eligibility(self):
        self.manifest['components']['core']['distributions'] = ['ros2-kilted']
        self.reject('distribution incompatible')

    def test_unknown_option(self):
        self.reject('unknown option', options=['esp-now'])

    def test_declared_external_dependencies(self):
        self.snapshot.cache['gnss_ros2/CMakeLists.txt'] = b'find_package(ExternalUndeclared REQUIRED)\n'
        self.reject('undeclared external dependency')

    def test_internal_include_not_distributed(self):
        self.manifest['components']['protocols']['requires'] = []
        self.reject('include not distributed')

    def test_literal_cmake_file_missing(self):
        self.snapshot.cache['gnss_ros2/CMakeLists.txt'] = b'add_executable(node src/missing.cpp)\n'
        self.reject('internal reference not distributed')

    def test_missing_internal_header(self):
        self.snapshot.cache['gnss_tools/src/tool.cpp'] = b'#include "universal_gnss/missing.hpp"\n'
        self.reject('internal include missing')

    def test_audited_computed_python_reference(self):
        self.manifest['references'] = [{'from': 'gnss_ros2/CMakeLists.txt',
                                       'to': 'docs/history.md', 'reason': 'Computed test path'}]
        self.reject('internal reference not distributed')

    def test_forbidden_component_file(self):
        self.manifest['distributions']['ros2-lyrical']['forbidden'].append('gnss_tools/')
        self.reject('forbidden file')

    def test_symlink_in_selected_source_rejected(self):
        _, oid = self.snapshot.tree['protocols/gnss_protocols/src/parser.cpp']
        self.snapshot.tree['protocols/gnss_protocols/src/parser.cpp'] = ('120000', oid)
        self.reject('symlink/unsupported mode')

    def test_invalid_and_duplicate_manifest(self):
        for data in [b'not json', b'{"schema_version":1,"schema_version":2}']:
            with self.subTest(data=data), self.assertRaises(d.DistributionError):
                d.plan(self.snapshot, data, 'ros2-lyrical')

    def test_unknown_manifest_field(self):
        self.manifest['silent_fallback_copy_everything'] = True
        self.reject('unknown or missing manifest field')

    def test_deterministic_bytes_and_modes(self):
        first = self.plan(options=['mavros'])
        second = self.plan(options=['mavros'])
        self.assertEqual(first, second)
        d.generate(self.snapshot, first, self.destination)
        self.assertEqual(d.inspect_output(self.destination), first)
        d.verify(self.destination, second)
        records = d.decode(first[d.INVENTORY][0])
        self.assertEqual([r['path'] for r in records], sorted(r['path'] for r in records))
        self.assertNotIn(str(self.repo).encode(), first[d.METADATA][0])

    def test_source_dirty_content_is_not_copied_or_modified(self):
        path = self.repo / 'core/gnss_core/include/universal_gnss/types.hpp'
        original = path.read_bytes()
        try:
            path.write_bytes(b'uncommitted user work\n')
            before = git(self.repo, 'status', '--porcelain=v1', '--untracked-files=all')
            output = self.plan()
            d.generate(self.snapshot, output, self.destination)
            self.assertEqual(output['core/gnss_core/include/universal_gnss/types.hpp'][0], original)
            self.assertEqual(path.read_bytes(), b'uncommitted user work\n')
            self.assertEqual(git(self.repo, 'status', '--porcelain=v1', '--untracked-files=all'), before)
        finally:
            path.write_bytes(original)

    def test_replace_refs_cannot_change_source_identity(self):
        empty_tree = subprocess.check_output(['git', '-C', str(self.repo), 'mktree'],
                                             input=b'').decode().strip()
        replacement = git(self.repo, '-c', 'user.name=Generator Test',
                          '-c', 'user.email=test@example.invalid', 'commit-tree', empty_tree,
                          '-m', 'Unrelated synthetic replacement')
        git(self.repo, 'replace', self.sha, replacement)
        try:
            self.assertEqual(git(self.repo, 'ls-tree', '-r', self.sha), '')
            snapshot = d.Snapshot(self.repo, self.sha)
            self.assertIn('core/gnss_core/include/universal_gnss/types.hpp', snapshot.tree)
            self.assertEqual(snapshot.read('core/gnss_core/include/universal_gnss/types.hpp'), b'#pragma once\n')
        finally:
            git(self.repo, 'replace', '-d', self.sha)

    def test_missing_blob_fails_offline(self):
        path = 'core/gnss_core/include/universal_gnss/types.hpp'
        oid = self.snapshot.tree[path][1]
        object_file = self.repo / '.git/objects' / oid[:2] / oid[2:]
        original = object_file.read_bytes()
        object_file.unlink()
        try:
            with self.assertRaisesRegex(d.DistributionError, 'blob unavailable offline'):
                self.snapshot.read(path)
        finally:
            object_file.write_bytes(original)

    def test_destination_with_user_data_never_overwritten(self):
        self.destination.mkdir()
        (self.destination / 'user.txt').write_text('keep me')
        with self.assertRaisesRegex(d.DistributionError, 'already exists'):
            d.generate(self.snapshot, self.plan(), self.destination)
        self.assertEqual((self.destination / 'user.txt').read_text(), 'keep me')

    def test_generated_destination_not_implicitly_overwritten(self):
        output = self.plan()
        d.generate(self.snapshot, output, self.destination)
        with self.assertRaisesRegex(d.DistributionError, 'already exists'):
            d.generate(self.snapshot, output, self.destination)

    def test_destination_inside_source_rejected_including_subdirectory_repo(self):
        snapshot = d.Snapshot(self.repo / 'core/gnss_core', self.sha)
        with self.assertRaisesRegex(d.DistributionError, 'inside source'):
            d.generate(snapshot, self.plan(), self.repo / 'generated')
        self.assertFalse((self.repo / 'generated').exists())

    def test_symlink_destination_rejected(self):
        self.destination.symlink_to(self.repo, target_is_directory=True)
        with self.assertRaisesRegex(d.DistributionError, 'symlink'):
            d.generate(self.snapshot, self.plan(), self.destination)

    def test_distributions_are_isolated(self):
        first = self.plan('ros2-kilted')
        second = self.plan('ros2-lyrical', options=['mavros'])
        other = Path(self.output_root.name) / 'other'
        d.generate(self.snapshot, first, self.destination)
        d.generate(self.snapshot, second, other)
        self.assertFalse((self.destination / 'gnss_mavros').exists())
        (other / 'core/gnss_core/include/universal_gnss/types.hpp').write_text('changed')
        d.verify(self.destination, first)
        self.assertNotEqual(first[d.METADATA], second[d.METADATA])
        self.assertEqual(git(self.repo, 'status', '--porcelain'), '')

    def test_missing_required_file_detected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        (self.destination / 'gnss_tools/src/tool.cpp').unlink()
        with self.assertRaisesRegex(d.DistributionError, 'inventory mismatch'):
            d.verify(self.destination, expected)

    def test_forbidden_extra_file_and_directory_detected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        (self.destination / 'docs').mkdir()
        with self.assertRaisesRegex(d.DistributionError, 'directory'):
            d.verify(self.destination, expected)
        (self.destination / 'docs/leak').write_text('unexpected')
        with self.assertRaisesRegex(d.DistributionError, 'unexpected/forbidden'):
            d.verify(self.destination, expected)

    def test_public_contract_change_and_forged_inventory_detected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        path = 'core/gnss_core/include/universal_gnss/types.hpp'
        (self.destination / path).write_text('unauthorized API change')
        records = d.decode((self.destination / d.INVENTORY).read_bytes())
        for record in records:
            if record['path'] == path:
                self.assertTrue(record['public_contract'])
                record['sha256'] = d.digest((self.destination / path).read_bytes())
        (self.destination / d.INVENTORY).write_bytes(d.json_bytes(records))
        with self.assertRaisesRegex(d.DistributionError, 'mismatch'):
            d.verify(self.destination, expected)

    def test_output_symlink_rejected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        path = self.destination / 'gnss_tools/src/tool.cpp'
        path.unlink()
        path.symlink_to(self.repo / 'gnss_tools/src/tool.cpp')
        with self.assertRaisesRegex(d.DistributionError, 'symlink'):
            d.verify(self.destination, expected)

    def test_manifest_metadata_tampering_detected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        (self.destination / d.MANIFEST).write_bytes(b'{}\n')
        with self.assertRaisesRegex(d.DistributionError, 'mismatch'):
            d.verify(self.destination, expected)

    def test_default_output_outside_source(self):
        result = d.generate(self.snapshot, self.plan())
        self.addCleanup(d.shutil.rmtree, result)
        self.assertFalse(result.is_relative_to(self.repo))
        d.verify(result, self.plan())

    def test_cli_requires_source_identity(self):
        result = subprocess.run(['bash', str(TOOL / 'make_backend.sh'), '--distribution', 'ros2-kilted'],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires --source-ref', result.stderr)

    def test_cli_roundtrip_and_forged_embedded_manifest(self):
        manifest = Path(self.output_root.name) / 'trusted.yaml'
        manifest.write_bytes(d.json_bytes(self.manifest))
        common = ['--repo', str(self.repo), '--source-ref', self.sha, '--manifest', str(manifest),
                  '--distribution', 'ros2-lyrical', '--output', str(self.destination)]
        result = subprocess.run(['bash', str(TOOL / 'make_backend.sh'), *common],
                                text=True, capture_output=True, check=True)
        self.assertEqual(result.stdout.strip(), str(self.destination))
        subprocess.run(['bash', str(TOOL / 'verify_backend.sh'), *common],
                       text=True, capture_output=True, check=True)
        (self.destination / d.MANIFEST).write_text('{}\n')
        result = subprocess.run(['bash', str(TOOL / 'verify_backend.sh'), *common],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('mismatch', result.stderr)

    def test_executable_mode_change_detected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        (self.destination / 'gnss_tools/src/tool.cpp').chmod(0o755)
        with self.assertRaisesRegex(d.DistributionError, 'content/mode'):
            d.verify(self.destination, expected)


class RepositoryIntegrationTests(unittest.TestCase):
    def test_real_manifest_and_both_pilot_closures(self):
        repo = TOOL.parents[1]
        before = git(repo, 'status', '--porcelain=v1', '--untracked-files=all')
        snapshot = d.Snapshot(repo, 'HEAD')
        manifest = (TOOL / 'manifest.yaml').read_bytes()
        for profile in ['ros2-kilted', 'ros2-lyrical']:
            for options in [[], ['mavros'], ['docker'], ['mavros', 'docker']]:
                with self.subTest(profile=profile, options=options):
                    expected = d.plan(snapshot, manifest, profile, options)
                    self.assertIn('gnss_tools/src/gnss_replay.cpp', expected)
                    self.assertIn('core/gnss_core/include/universal_gnss/gnss_runtime_state.hpp', expected)
                    self.assertFalse(any(p.startswith('gnss_core/') for p in expected))
                    self.assertIn('protocols/gnss_protocols/src/nmea_parser.cpp', expected)
                    self.assertIn('protocols/gnss_protocols/include/universal_gnss_protocols/rtcm_parser.hpp', expected)
                    self.assertFalse(any(p.startswith('gnss_protocols/') for p in expected))
                    self.assertIn('scripts/tests/test_docker_healthcheck.py', expected)
                    self.assertIn('docker/healthcheck.py', expected)
                    self.assertIn('testdata/mixed/nmea_ubx_rtcm_unicore.bin', expected)
                    self.assertFalse(any(p.startswith('gnss_runtime/') for p in expected))
                    self.assertEqual('gnss_mavros/package.xml' in expected, 'mavros' in options)
                    self.assertEqual('Dockerfile' in expected, 'docker' in options)
        self.assertEqual(d.plan(snapshot, manifest, 'ros2-lyrical', ['mavros', 'docker']),
                         d.plan(snapshot, manifest, 'ros2-lyrical', ['docker', 'mavros']))
        self.assertEqual(git(repo, 'status', '--porcelain=v1', '--untracked-files=all'), before)


if __name__ == '__main__':
    unittest.main()
