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
            'tools/gnss_tools/src/tool.cpp': '#include "universal_gnss/types.hpp"\n',
            'gnss_ros2/CMakeLists.txt': 'add_subdirectory("${UNIVERSAL_GNSS_SOURCE_ROOT}/tools/gnss_tools" out)\n',
            'gnss_ros2/package.xml': '<package><name>universal_gnss_ros2</name><version>0.7.0</version><depend>universal_gnss_msgs</depend></package>\n',
            'universal_gnss_msgs/package.xml': '<package><name>universal_gnss_msgs</name><version>0.7.0</version></package>\n',
            'universal_gnss_msgs/msg/GnssStatus.msg': 'bool valid\n',
            'adapters/mavros/gnss_mavros/src/adapter.cpp': '#include "universal_gnss/types.hpp"\n',
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
                'tools': component(['tools/gnss_tools/'], ['protocols']),
                'messages': component(['universal_gnss_msgs/']),
                'ros2': component(['gnss_ros2/'], ['tools', 'messages', 'legal']),
                'mavros': component(['adapters/mavros/gnss_mavros/'], ['ros2', 'core']),
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
        self.snapshot.cache['tools/gnss_tools/src/tool.cpp'] = b'#include "universal_gnss/missing.hpp"\n'
        self.reject('internal include missing')

    def test_audited_computed_python_reference(self):
        self.manifest['references'] = [{'from': 'gnss_ros2/CMakeLists.txt',
                                       'to': 'docs/history.md', 'reason': 'Computed test path'}]
        self.reject('internal reference not distributed')

    def test_forbidden_component_file(self):
        self.manifest['distributions']['ros2-lyrical']['forbidden'].append('tools/gnss_tools/')
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
        self.assertFalse((self.destination / 'adapters/mavros/gnss_mavros').exists())
        (other / 'core/gnss_core/include/universal_gnss/types.hpp').write_text('changed')
        d.verify(self.destination, first)
        self.assertNotEqual(first[d.METADATA], second[d.METADATA])
        self.assertEqual(git(self.repo, 'status', '--porcelain'), '')

    def test_missing_required_file_detected(self):
        expected = self.plan()
        d.generate(self.snapshot, expected, self.destination)
        (self.destination / 'tools/gnss_tools/src/tool.cpp').unlink()
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
        path = self.destination / 'tools/gnss_tools/src/tool.cpp'
        path.unlink()
        path.symlink_to(self.repo / 'tools/gnss_tools/src/tool.cpp')
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
        (self.destination / 'tools/gnss_tools/src/tool.cpp').chmod(0o755)
        with self.assertRaisesRegex(d.DistributionError, 'content/mode'):
            d.verify(self.destination, expected)


class RepositoryIntegrationTests(unittest.TestCase):
    mavros_root = 'adapters/mavros/gnss_mavros/'

    def test_legacy_mavros_root_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['gnss_mavros/package.xml'] = snapshot.tree[self.mavros_root + 'package.xml']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file: gnss_mavros/'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical', ['mavros'])

    def test_missing_mavros_package_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree.pop(self.mavros_root + 'package.xml')
        with self.assertRaisesRegex(d.DistributionError, 'path does not exist: mavros'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical', ['mavros'])

    def test_duplicate_mavros_package_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree[self.mavros_root + 'src/duplicate/package.xml'] = snapshot.tree[self.mavros_root + 'package.xml']
        with self.assertRaisesRegex(d.DistributionError, 'duplicate ROS package: universal_gnss_mavros'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical', ['mavros'])

    def test_unclassified_adapter_sibling_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['adapters/unclassified_neighbor/new.cpp'] = snapshot.tree[self.mavros_root + 'src/mavlink_gnss_adapter.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file: adapters/unclassified_neighbor/'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_mavros_injected_without_option_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        manifest = (TOOL / 'manifest.yaml').read_bytes()
        for profile in ('ros2-kilted', 'ros2-lyrical'):
            for options in ([], ['docker']):
                with self.subTest(profile=profile, options=options):
                    expected = d.plan(snapshot, manifest, profile, options)
                    self.assertFalse(any(p.startswith(self.mavros_root) for p in expected))
                    with tempfile.TemporaryDirectory(prefix='ug-no-mavros-') as workspace:
                        destination = Path(workspace) / 'distribution'
                        d.generate(snapshot, expected, destination)
                        injected = destination / self.mavros_root / 'package.xml'
                        injected.parent.mkdir(parents=True)
                        injected.write_bytes(snapshot.read(self.mavros_root + 'package.xml'))
                        with self.assertRaisesRegex(d.DistributionError, 'unexpected/forbidden'):
                            d.verify(destination, expected)

    def test_selected_mavros_omission_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        manifest = (TOOL / 'manifest.yaml').read_bytes()
        for profile in ('ros2-kilted', 'ros2-lyrical'):
            for options in (['mavros'], ['mavros', 'docker']):
                with self.subTest(profile=profile, options=options):
                    expected = d.plan(snapshot, manifest, profile, options)
                    with tempfile.TemporaryDirectory(prefix='ug-with-mavros-') as workspace:
                        destination = Path(workspace) / 'distribution'
                        d.generate(snapshot, expected, destination)
                        (destination / self.mavros_root / 'package.xml').unlink()
                        with self.assertRaisesRegex(d.DistributionError, 'inventory mismatch'):
                            d.verify(destination, expected)

    def test_invalid_mavros_plugin_paths_rejected(self):
        cases = (
            ('package.xml', b'${prefix}/universal_gnss_mavros_plugins.xml', b'${prefix}/missing.xml',
             'internal reference not distributed'),
            ('CMakeLists.txt', b'plugin_description_file(mavros universal_gnss_mavros_plugins.xml)',
             b'plugin_description_file(mavros missing.xml)', 'plugin descriptor not exported'),
            ('universal_gnss_mavros_plugins.xml', b'path="universal_gnss_mavros_plugin"',
             b'path="missing_plugin_target"', 'plugin library target missing'),
        )
        for filename, original, invalid, error in cases:
            with self.subTest(filename=filename):
                snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
                path = self.mavros_root + filename
                content = snapshot.read(path)
                self.assertIn(original, content)
                snapshot.cache[path] = content.replace(original, invalid)
                with self.assertRaisesRegex(d.DistributionError, error):
                    d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical', ['mavros'])

    def test_missing_mavros_external_declaration_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        manifest = d.decode((TOOL / 'manifest.yaml').read_bytes())
        manifest['components']['mavros']['external'].remove('mavros ==2.16.0')
        with self.assertRaisesRegex(d.DistributionError, 'undeclared external dependency: .*: mavros'):
            d.plan(snapshot, d.json_bytes(manifest), 'ros2-lyrical', ['mavros'])

    tools_fixtures = (
        'testdata/mixed/nmea_ubx_rtcm_unicore.bin',
        'testdata/nmea/basic_fix.nmea',
        'testdata/rtcm/basic_msm.rtcm',
        'testdata/comparison/receiver_a.nmea',
        'testdata/comparison/receiver_b.nmea',
        'testdata/ubx/nav_pvt_sat_monrf.ubx',
        'testdata/unicore/basic_ascii.log',
    )

    def test_missing_tools_component_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree = {p: v for p, v in snapshot.tree.items() if not p.startswith('tools/gnss_tools/')}
        with self.assertRaisesRegex(d.DistributionError, 'path does not exist: tools'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_legacy_tools_root_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['gnss_tools/src/gnss_replay.cpp'] = snapshot.tree['tools/gnss_tools/src/gnss_replay.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file: gnss_tools/'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_unclassified_tools_sibling_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['tools/unclassified_neighbor/new.cpp'] = snapshot.tree['tools/gnss_tools/src/gnss_replay.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file: tools/unclassified_neighbor/'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_generator_scripts_excluded_and_injected_script_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        manifest_bytes = (TOOL / 'manifest.yaml').read_bytes()
        manifest = d.decode(manifest_bytes)
        self.assertEqual(manifest['components']['tools']['paths'], ['tools/gnss_tools/'])
        owners = d.validate_manifest(manifest, snapshot)
        self.assertTrue(all(owner is None for p, owner in owners.items() if p.startswith('tools/branches/')))
        for profile in ('ros2-kilted', 'ros2-lyrical'):
            for options in ([], ['mavros'], ['docker'], ['mavros', 'docker']):
                with self.subTest(profile=profile, options=options):
                    expected = d.plan(snapshot, manifest_bytes, profile, options)
                    self.assertFalse(any(p.startswith('tools/branches/') for p in expected))
                    with tempfile.TemporaryDirectory(prefix='ug-generator-exclusion-') as workspace:
                        destination = Path(workspace) / 'distribution'
                        d.generate(snapshot, expected, destination)
                        injected = destination / 'tools/branches/distribution.py'
                        injected.parent.mkdir(parents=True)
                        injected.write_bytes(snapshot.read('tools/branches/distribution.py'))
                        with self.assertRaisesRegex(d.DistributionError, 'unexpected/forbidden'):
                            d.verify(destination, expected)

    def test_missing_tools_fixture_rejected(self):
        for path in self.tools_fixtures:
            with self.subTest(path=path):
                snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
                snapshot.tree.pop(path)
                with self.assertRaisesRegex(d.DistributionError, 'reference path does not exist') as failure:
                    d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')
                self.assertIn(path, str(failure.exception))

    ntrip_tls_inputs = (
        'core/gnss_transport/tests/tls_loopback_server.hpp',
        'core/gnss_transport/tests/fixtures/tls/ca.crt',
        'core/gnss_transport/tests/fixtures/tls/server.crt',
        'core/gnss_transport/tests/fixtures/tls/server.key',
    )

    def test_runtime_forbidden_if_accidentally_selected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        manifest = d.decode((TOOL / 'manifest.yaml').read_bytes())
        manifest['exclude'] = [e for e in manifest['exclude'] if e['path'] != 'core/gnss_runtime/']
        manifest['components']['core']['paths'].append('core/gnss_runtime/')
        for profile in ('ros2-kilted', 'ros2-lyrical'):
            for options in ([], ['mavros'], ['docker'], ['mavros', 'docker']):
                with self.subTest(profile=profile, options=options):
                    with self.assertRaisesRegex(d.DistributionError, 'forbidden file: core/gnss_runtime/'):
                        d.plan(snapshot, d.json_bytes(manifest), profile, options)

    def test_injected_runtime_source_rejected_from_generated_output(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        manifest = (TOOL / 'manifest.yaml').read_bytes()
        content = snapshot.read('core/gnss_runtime/src/receiver_supervisor.cpp')
        for profile in ('ros2-kilted', 'ros2-lyrical'):
            expected = d.plan(snapshot, manifest, profile)
            for prefix in ('gnss_runtime', 'core/gnss_runtime'):
                with self.subTest(profile=profile, prefix=prefix):
                    with tempfile.TemporaryDirectory(prefix='ug-runtime-exclusion-') as workspace:
                        destination = Path(workspace) / 'distribution'
                        d.generate(snapshot, expected, destination)
                        injected = destination / prefix / 'src/receiver_supervisor.cpp'
                        injected.parent.mkdir(parents=True)
                        injected.write_bytes(content)
                        with self.assertRaisesRegex(d.DistributionError, 'unexpected/forbidden'):
                            d.verify(destination, expected)

    def test_real_unclassified_core_sibling_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['core/unclassified_runtime_neighbor/new.cpp'] = snapshot.tree['core/gnss_runtime/src/receiver_supervisor.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_legacy_runtime_source_root_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['gnss_runtime/src/receiver_supervisor.cpp'] = snapshot.tree['core/gnss_runtime/src/receiver_supervisor.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_missing_transport_component_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree = {p: v for p, v in snapshot.tree.items() if not p.startswith('core/gnss_transport/')}
        with self.assertRaisesRegex(d.DistributionError, 'path does not exist'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_legacy_transport_root_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['gnss_transport/src/memory_stream.cpp'] = snapshot.tree['core/gnss_transport/src/memory_stream.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_missing_silent_tls_helper_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree.pop('core/gnss_transport/tests/silent_tls_loopback_server.hpp')
        with self.assertRaisesRegex(d.DistributionError, 'internal include missing.*silent_tls_loopback_server.hpp'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_missing_ntrip_component_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree = {p: v for p, v in snapshot.tree.items() if not p.startswith('core/gnss_ntrip/')}
        with self.assertRaisesRegex(d.DistributionError, 'path does not exist'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_legacy_ntrip_root_rejected(self):
        snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        snapshot.tree['gnss_ntrip/src/ntrip_client.cpp'] = snapshot.tree['core/gnss_ntrip/src/ntrip_client.cpp']
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file'):
            d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')

    def test_missing_ntrip_tls_input_rejected(self):
        for path in self.ntrip_tls_inputs:
            with self.subTest(path=path):
                snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
                snapshot.tree.pop(path)
                with self.assertRaises(d.DistributionError) as failure:
                    d.plan(snapshot, (TOOL / 'manifest.yaml').read_bytes(), 'ros2-lyrical')
                message = str(failure.exception)
                self.assertIn(Path(path).name, message)
                if path.endswith('.hpp'):
                    self.assertIn('internal include missing', message)
                else:
                    self.assertIn('reference path does not exist', message)

    def test_real_manifest_and_both_pilot_closures(self):
        repo = TOOL.parents[1]
        before = git(repo, 'status', '--porcelain=v1', '--untracked-files=all')
        snapshot = d.Snapshot(repo, 'HEAD')
        manifest = (TOOL / 'manifest.yaml').read_bytes()
        for profile in ['ros2-kilted', 'ros2-lyrical']:
            for options in [[], ['mavros'], ['docker'], ['mavros', 'docker']]:
                with self.subTest(profile=profile, options=options):
                    expected = d.plan(snapshot, manifest, profile, options)
                    self.assertIn('tools/gnss_tools/src/gnss_replay.cpp', expected)
                    self.assertFalse(any(p.startswith('gnss_tools/') for p in expected))
                    self.assertFalse(any(p.startswith('tools/branches/') for p in expected))
                    allowed_scripts = {'scripts/tests/test_docker_healthcheck.py'}
                    if 'docker' in options:
                        allowed_scripts.add('scripts/collect_support_snapshot.py')
                    self.assertEqual({p for p in expected if p.startswith('scripts/')}, allowed_scripts)
                    self.assertIn('core/gnss_core/include/universal_gnss/gnss_runtime_state.hpp', expected)
                    self.assertFalse(any(p.startswith('gnss_core/') for p in expected))
                    self.assertIn('core/gnss_driver/include/universal_gnss_driver/receiver_session.hpp', expected)
                    self.assertIn('core/gnss_driver/src/receiver_discovery.cpp', expected)
                    self.assertFalse(any(p.startswith('gnss_driver/') for p in expected))
                    self.assertIn('core/gnss_ntrip/include/universal_gnss_ntrip/ntrip_client.hpp', expected)
                    self.assertIn('core/gnss_ntrip/src/ntrip_client.cpp', expected)
                    self.assertFalse(any(p.startswith('gnss_ntrip/') for p in expected))
                    self.assertIn('core/gnss_transport/include/universal_gnss_transport/byte_stream.hpp', expected)
                    self.assertIn('core/gnss_transport/src/tcp_client_transport.cpp', expected)
                    self.assertIn('core/gnss_transport/tests/silent_tls_loopback_server.hpp', expected)
                    self.assertFalse(any(p.startswith('gnss_transport/') for p in expected))
                    for path in self.ntrip_tls_inputs:
                        self.assertIn(path, expected)
                    self.assertIn('protocols/gnss_protocols/src/nmea_parser.cpp', expected)
                    self.assertIn('protocols/gnss_protocols/include/universal_gnss_protocols/rtcm_parser.hpp', expected)
                    self.assertFalse(any(p.startswith('gnss_protocols/') for p in expected))
                    self.assertIn('scripts/tests/test_docker_healthcheck.py', expected)
                    self.assertIn('docker/healthcheck.py', expected)
                    self.assertIn('testdata/mixed/nmea_ubx_rtcm_unicore.bin', expected)
                    self.assertFalse(any(p.startswith('gnss_runtime/') for p in expected))
                    self.assertFalse(any(p.startswith('core/gnss_runtime/') for p in expected))
                    self.assertNotIn('runtime', d.decode(expected[d.METADATA][0])['components'])
                    self.assertEqual('adapters/mavros/gnss_mavros/package.xml' in expected, 'mavros' in options)
                    self.assertFalse(any(p.startswith('gnss_mavros/') for p in expected))
                    metadata = d.decode(expected[d.METADATA][0])
                    self.assertEqual('mavros' in metadata['components'], 'mavros' in options)
                    if 'mavros' in options:
                        self.assertIn('--base-paths universal_gnss_msgs gnss_ros2 adapters/mavros/gnss_mavros',
                                      expected[d.README][0].decode())
                    self.assertEqual('Dockerfile' in expected, 'docker' in options)
        self.assertEqual(d.plan(snapshot, manifest, 'ros2-lyrical', ['mavros', 'docker']),
                         d.plan(snapshot, manifest, 'ros2-lyrical', ['docker', 'mavros']))
        self.assertEqual(git(repo, 'status', '--porcelain=v1', '--untracked-files=all'), before)


class MavrosGuardRegressionTests(unittest.TestCase):
    root = 'adapters/mavros/gnss_mavros/'
    target = 'universal_gnss_mavros_plugin'
    descriptor = 'universal_gnss_mavros_plugins.xml'

    def setUp(self):
        self.snapshot = d.Snapshot(TOOL.parents[1], 'HEAD')
        self.manifest = d.decode((TOOL / 'manifest.yaml').read_bytes())
        self.cmake_path = self.root + 'CMakeLists.txt'
        self.cmake = self.snapshot.read(self.cmake_path)
        self.baseline = self.plan()

    def plan(self, options=('mavros',), profile='ros2-lyrical'):
        return d.plan(self.snapshot, d.json_bytes(self.manifest), profile, list(options))

    def replace_cmake(self, before, after):
        self.assertIn(before, self.cmake)
        self.snapshot.cache[self.cmake_path] = self.cmake.replace(before, after)

    def assert_valid_plugin(self):
        output = self.plan()
        self.assertIn(self.root + self.descriptor, output)
        self.assertEqual(output[self.cmake_path][0], self.snapshot.read(self.cmake_path))
        metadata = d.decode(output[d.METADATA][0])
        self.assertIn('mavros', metadata['components'])
        self.assertEqual(metadata['package_versions']['universal_gnss_mavros'], '0.7.0')
        return output

    def add_duplicate(self, path):
        self.snapshot.tree[path] = self.snapshot.tree[self.root + 'package.xml']
        self.snapshot.cache[path] = b'<package><name>universal_gnss_mavros</name><version>0.7.0</version></package>'

    def test_selected_duplicate_is_rejected(self):
        path = self.root + 'src/example/package.xml'
        self.add_duplicate(path)
        with self.assertRaises(d.DistributionError):
            self.plan()
        # The same classified file with a distinct identity must be accepted.
        self.snapshot.cache[path] = self.snapshot.cache[path].replace(b'<name>universal_gnss_mavros</name>', b'<name>distinct_example</name>')
        self.assertIn('distinct_example', d.decode(self.plan()[d.METADATA][0])['package_versions'])

    def test_selected_duplicate_without_mavros_is_rejected(self):
        path = 'tools/gnss_tools/examples/package.xml'
        self.snapshot.tree[path] = self.snapshot.tree['gnss_ros2/package.xml']
        self.snapshot.cache[path] = b'<package><name>universal_gnss_ros2</name><version>0.7.0</version></package>'
        with self.assertRaises(d.DistributionError):
            self.plan(())

    def test_excluded_duplicate_does_not_change_eight_distributions(self):
        before = {(p, tuple(o)): self.plan(o, p) for p in ('ros2-kilted', 'ros2-lyrical')
                  for o in ([], ['mavros'], ['docker'], ['mavros', 'docker'])}
        self.add_duplicate('docs/example/package.xml')
        for (profile, options), expected in before.items():
            with self.subTest(profile=profile, options=options):
                self.assertEqual(self.plan(options, profile), expected)

    def test_classification_exclusion_is_not_specific_to_docs(self):
        path = 'tools/gnss_tools/examples/package.xml'
        self.manifest['exclude'].append({'path': path, 'reason': 'Excluded test example'})
        before = self.plan()
        self.add_duplicate(path)
        self.assertEqual(self.plan(), before)

    def test_excluded_package_xml_is_not_parsed(self):
        self.add_duplicate('docs/example/package.xml')
        self.snapshot.cache['docs/example/package.xml'] = b'not XML, deliberately excluded'
        self.assertEqual(self.plan(), self.baseline)

    def test_unselected_duplicate_respects_mavros_option(self):
        before = self.plan(())
        self.add_duplicate(self.root + 'src/example/package.xml')
        self.assertEqual(self.plan(()), before)
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_unquoted_library_is_accepted(self):
        self.assert_valid_plugin()

    def test_quoted_library_and_export_are_accepted(self):
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED',
                           b'add_library("universal_gnss_mavros_plugin" "SHARED"')
        self.snapshot.cache[self.cmake_path] = self.snapshot.cache[self.cmake_path].replace(
            b'pluginlib_export_plugin_description_file(mavros universal_gnss_mavros_plugins.xml)',
            b'pluginlib_export_plugin_description_file("mavros" "universal_gnss_mavros_plugins.xml")')
        self.assert_valid_plugin()

    def test_bracket_literal_arguments_are_accepted(self):
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED',
                           b'add_library([=[\nuniversal_gnss_mavros_plugin]=] [=[SHARED]=]')
        self.assert_valid_plugin()

    def test_line_commented_library_is_rejected(self):
        self.replace_cmake(b'  add_library(universal_gnss_mavros_plugin SHARED',
                           b'  # add_library(universal_gnss_mavros_plugin SHARED')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_bracket_commented_library_is_rejected(self):
        original = b'add_library(universal_gnss_mavros_plugin SHARED src/universal_gnss_plugin.cpp)'
        for equals in (b'', b'=', b'=='):
            with self.subTest(equals=equals):
                self.replace_cmake(original, b'#[' + equals + b'[\n' + original + b'\n]' + equals + b']')
                with self.assertRaises(d.DistributionError):
                    self.plan()

    def test_strings_do_not_manufacture_library_declarations(self):
        original = b'add_library(universal_gnss_mavros_plugin SHARED src/universal_gnss_plugin.cpp)'
        for decoy in (b'set(DECOY "' + original + b'")', b'set(DECOY [=[' + original + b']=])'):
            with self.subTest(decoy=decoy):
                self.replace_cmake(original, decoy)
                with self.assertRaises(d.DistributionError):
                    self.plan()

    def test_comments_between_literal_arguments_are_accepted(self):
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED',
                           b'add_library(\n # line comment\n "universal_gnss_mavros_plugin" #[=[block comment]=] SHARED')
        self.assert_valid_plugin()

    def test_wrong_plugin_category_is_rejected(self):
        self.replace_cmake(b'pluginlib_export_plugin_description_file(mavros ',
                           b'pluginlib_export_plugin_description_file(other_category ')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_commented_plugin_export_is_rejected(self):
        original = b'pluginlib_export_plugin_description_file(mavros universal_gnss_mavros_plugins.xml)'
        self.replace_cmake(original, b'#[[' + original + b']]')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_static_library_is_rejected(self):
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED',
                           b'add_library(universal_gnss_mavros_plugin STATIC')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_module_library_is_accepted(self):
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED',
                           b'add_library(universal_gnss_mavros_plugin MODULE')
        self.assert_valid_plugin()

    def test_imported_library_is_not_claimed_as_built(self):
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED src/universal_gnss_plugin.cpp)',
                           b'add_library(universal_gnss_mavros_plugin SHARED IMPORTED)')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_empty_library_descriptor_is_rejected(self):
        path = self.root + self.descriptor
        for invalid in (b'<class_libraries/>', b'<library/>'):
            with self.subTest(invalid=invalid):
                self.snapshot.cache[path] = invalid
                with self.assertRaises(d.DistributionError):
                    self.plan()

    def test_unsupported_plugin_prefix_is_rejected(self):
        path = self.root + 'package.xml'
        self.snapshot.cache[path] = self.snapshot.read(path).replace(b'${prefix}/', b'/opt/untrusted/')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_missing_descriptor_is_rejected(self):
        self.snapshot.tree.pop(self.root + self.descriptor)
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_inconsistent_export_is_rejected(self):
        self.replace_cmake(b'pluginlib_export_plugin_description_file(mavros universal_gnss_mavros_plugins.xml)',
                           b'pluginlib_export_plugin_description_file(mavros missing.xml)')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_unknown_library_is_rejected(self):
        path = self.root + self.descriptor
        self.snapshot.cache[path] = self.snapshot.read(path).replace(b'path="universal_gnss_mavros_plugin"', b'path="unknown_library"')
        with self.assertRaises(d.DistributionError):
            self.plan()

    def test_dynamic_essential_arguments_are_explicitly_unproven(self):
        cases = (
            (b'add_library(universal_gnss_mavros_plugin SHARED', b'add_library(${PLUGIN_TARGET} SHARED'),
            (b'add_library(universal_gnss_mavros_plugin SHARED', b'add_library(universal_gnss_mavros_plugin ${PLUGIN_TYPE}'),
            (b'pluginlib_export_plugin_description_file(mavros ', b'pluginlib_export_plugin_description_file(${PLUGIN_CATEGORY} '),
        )
        for original, dynamic in cases:
            with self.subTest(dynamic=dynamic):
                self.replace_cmake(original, dynamic)
                with self.assertRaises(d.DistributionError) as failure:
                    self.plan()
                self.assertIn('cannot statically prove', str(failure.exception))

    def test_default_distributions_do_not_validate_mavros_plugin(self):
        before = self.plan(())
        self.replace_cmake(b'add_library(universal_gnss_mavros_plugin SHARED',
                           b'add_library(universal_gnss_mavros_plugin STATIC')
        self.assertEqual(self.plan(()), before)


if __name__ == '__main__':
    unittest.main()
