# SPDX-License-Identifier: LGPL-3.0-only
"""Synthetic legacy exports exercised against the actual UG settings validator."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1]/'tools/migrate_legacy_config.py'
spec = importlib.util.spec_from_file_location('legacy_migration', SCRIPT)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)
VALIDATOR = os.environ['UG_CONFIG_VALIDATOR']


class LegacyImport(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ug-legacy-import-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = {'version': 'synthetic', 'ntr_cli_host': 'caster.invalid', 'ntr_cli_port': '2101',
                       'ntr_cli_mp': 'TEST', 'ntr_cli_active': '1', 'ntr_cli_pass': '\x1a'*8,
                       'w_sta_ssid': 'SYNTHETIC', 'w_sta_active': '1', 'w_sta_pass': '\x1a'*8,
                       'uart_rx_pin': '37', 'base_lat_e7': '481234567',
                       'adm_pass': '\x1a'*8, 'ntr_srv_host': 'obsolete-flat.invalid'}
        self.input = self.root/'legacy.json'
        self.input.write_text(json.dumps(self.config))

    def prepare(self, name='output', **kwargs):
        return migration.prepare(self.input, self.root/name, VALIDATOR,
                                 kwargs.pop('board', 'waveshare-s3-eth'),
                                 kwargs.pop('role', 'gateway'), **kwargs)

    def test_masked_secrets_disabled_services_and_untouched_source(self):
        before = self.input.read_bytes()
        output = self.prepare()
        candidate = json.loads((output/'candidate.json').read_text())
        report = json.loads((output/'review.json').read_text())
        self.assertEqual(self.input.read_bytes(), before)
        self.assertFalse(candidate['upstream']['enabled'])
        self.assertFalse(candidate['gnss']['enabled'])
        self.assertFalse(candidate['radio']['enabled'])
        self.assertFalse(candidate['wifi']['sta'])
        self.assertFalse(candidate['caster']['enabled'])
        self.assertTrue(all(not slot['enabled'] for slot in candidate['publishers']))
        self.assertEqual(candidate['gnss']['rx_pin'], -1)
        self.assertEqual(candidate['upstream']['endpoint']['password'], '')
        self.assertIn('upstream.password', report['credentials_to_reenter'])
        self.assertIn('uart_rx_pin', report['preserved_only_in_original_export'])
        self.assertIn('ntr_srv_host', report['preserved_only_in_original_export'])
        self.assertNotIn('admin_token_sha256', candidate)
        self.assertEqual(report['native_schema_validation'], 'PASS')
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE((output/'candidate.json').stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE((output/'review.json').stat().st_mode), 0o600)
        second = self.prepare('second')
        self.assertEqual((output/'candidate.json').read_bytes(), (second/'candidate.json').read_bytes())
        self.assertEqual((output/'review.json').read_bytes(), (second/'review.json').read_bytes())

    def test_authoritative_slots_override_neither_by_flat_keys_nor_runtime_metadata(self):
        slots = {'slots': [{'index': i, 'role': 'server', 'enabled': True, 'host': f'caster{i}.invalid',
                           'port': 443, 'mountpoint': 'TEST', 'username': '', 'password': '********',
                           'has_password': True, 'use_tls': True, 'mock_mode': 'auth_fail',
                           'ntrip_version': '2', 'name': 'synthetic'} for i in range(5)]}
        path = self.root/'ntrip.json'
        path.write_text(json.dumps(slots))
        output = self.prepare(ntrip_path=path, board='supermini-c3', role='relay-ntrip')
        patch = json.loads((output/'candidate.json').read_text())
        report = json.loads((output/'review.json').read_text())
        self.assertEqual(patch['publishers'][0]['endpoint']['host'], 'caster0.invalid')
        self.assertTrue(patch['publishers'][0]['endpoint']['tls'])
        self.assertFalse(patch['publishers'][4]['enabled'])
        self.assertIn('publisher.4.password', report['credentials_to_reenter'])
        self.assertIn('ntrip_version', report['slot_metadata_preserved_only_in_original_export'])
        # No silent truncation: all five disabled configurations remain, but the
        # actual C3 schema forbids enabling a slot outside its supported capacity.
        patch['publishers'][4]['enabled'] = True
        patch['publishers'][4]['endpoint']['password'] = 'synthetic-only'
        checked = subprocess.run([VALIDATOR, 'supermini-c3', 'relay-ntrip'], input=json.dumps(patch),
                                 text=True, capture_output=True)
        self.assertNotEqual(checked.returncode, 0)
        bad = copy.deepcopy(slots)
        bad['slots'][4]['index'] = 0
        with self.assertRaises(ValueError):
            migration.convert(self.config, bad)
        del slots['slots'][0]['use_tls']
        with self.assertRaises(ValueError):
            migration.convert(self.config, slots)

    def test_invalid_native_candidate_is_rejected_before_any_output(self):
        self.config['ntr_cli_host'] = 'host\r\nSYNTHETIC-SECRET'
        self.input.write_text(json.dumps(self.config))
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse((self.root/'output').exists())

    def test_duplicate_nonfinite_and_oversized_exports(self):
        for bad in ('{"version":"x","version":"y"}', '{"x":NaN}', '{"x":1e400}', '['*20+'0'+']'*20, ' '*65537):
            self.input.write_text(bad)
            with self.assertRaises((ValueError, RecursionError)):
                migration.load_export(self.input)

    def test_unknown_format_numeric_suffix_and_mask_handling(self):
        for config in ({'schema': 1}, {}, {'version': 'x'}, {**self.config, 'ntr_cli_port': '2101suffix'}):
            with self.assertRaises(ValueError):
                migration.convert(config)
        config = {**self.config, 'ntr_cst_user': 'operator', 'ntr_cst_pass': '********'}
        patch, report = migration.convert(config)
        self.assertEqual(patch['caster']['username'], '')
        self.assertIn('caster.password', report['credentials_to_reenter'])

    def test_output_collision_and_repository_boundary(self):
        self.prepare()
        with self.assertRaises(ValueError):
            self.prepare()
        with self.assertRaises(ValueError):
            migration.prepare(self.input, migration.ROOT/'must-not-create-legacy-import', VALIDATOR,
                              'waveshare-s3-eth', 'gateway')
        self.assertFalse((migration.ROOT/'must-not-create-legacy-import').exists())

    def test_cli_failure_never_echoes_secret_and_validator_has_input_budget(self):
        self.config['ntr_cli_host'] = 'bad\r\nSYNTHETIC-SECRET'
        self.input.write_text(json.dumps(self.config))
        failed = subprocess.run(['python3', str(SCRIPT), '--legacy-config', str(self.input),
                                 '--output', str(self.root/'cli'), '--validator', VALIDATOR,
                                 '--board', 'waveshare-s3-eth', '--role', 'gateway'], capture_output=True, text=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertNotIn('SYNTHETIC-SECRET', failed.stdout+failed.stderr)
        checked = subprocess.run([VALIDATOR, 'waveshare-s3-eth', 'gateway'], input=' '*8193,
                                 capture_output=True, text=True)
        self.assertNotEqual(checked.returncode, 0)


if __name__ == '__main__':
    unittest.main()
