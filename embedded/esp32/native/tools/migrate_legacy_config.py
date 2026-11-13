# SPDX-License-Identifier: LGPL-3.0-only
"""Prepare a reviewed native UG configuration from pinned legacy JSON exports.

Offline only: no serial/network/NVS access, no automatic application, flash or
reboot. All services in the candidate are disabled. Original exports are read-only;
redacted credentials and hardware/BASE settings require explicit reconfiguration.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]
REFERENCE = '7a1ec69f5e9d07522c0284ec2dbae380f72c7e85'
MASKS = {'\x1a'*8, '********'}


def load_export(path):
    with Path(path).open('rb') as stream:
        raw = stream.read(65537)
    if len(raw) > 65536:
        raise ValueError('legacy export exceeds input budget')

    def unique(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ValueError('duplicate JSON key')
            out[key] = value
        return out

    def constant(_):
        raise ValueError('nonfinite JSON value')

    root = json.loads(raw.decode('utf-8'), object_pairs_hook=unique, parse_constant=constant)
    if not isinstance(root, dict):
        raise ValueError('legacy export must be an object')
    nodes = 0

    def budget(value, depth=0):
        nonlocal nodes
        nodes += 1
        if depth > 12 or nodes > 2048:
            raise ValueError('legacy export structure exceeds budget')
        if isinstance(value, dict):
            if any(len(key) > 128 or any(ord(c) < 32 for c in key) for key in value):
                raise ValueError('invalid legacy field name')
            for child in value.values():
                budget(child, depth+1)
        elif isinstance(value, float) and not math.isfinite(value):
            raise ValueError('nonfinite legacy number')
        elif isinstance(value, list):
            for child in value:
                budget(child, depth+1)
    budget(root)
    return root, hashlib.sha256(raw).hexdigest()


def integer(value, maximum):
    if isinstance(value, str) and re.fullmatch(r'[0-9]+', value):
        value = int(value)
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError('invalid legacy integer')
    return value


def boolean(value):
    if type(value) is bool:
        return value
    return bool(integer(value, 1))


def text(value):
    if not isinstance(value, str):
        raise ValueError('invalid legacy text')
    return value


def convert(config, slots=None):
    # Do not infer a model, pins, radio peer/channel, BASE datum or secrets from
    # family enums, masked exports or default values in the old firmware.
    if not isinstance(config.get('version'), str) or 'schema' in config or not any(
            key in config for key in ('ntr_cli_host', 'w_sta_ssid', 'uart_baud_rate')):
        raise ValueError('expected a pinned legacy /config export, not a native patch')
    missing, copied, requested, used = [], [], [], set()
    slot_metadata = set()

    def get(key, default=''):
        if key in config:
            used.add(key)
            copied.append(key)
        return config.get(key, default)

    def secret(value, name):
        value = text(value)
        if value in MASKS:
            missing.append(name)
            return ''
        return value

    def endpoint(prefix, name):
        return {'host': text(get(prefix+'host')), 'port': integer(get(prefix+'port', '2101'), 65535),
                'mountpoint': text(get(prefix+'mp')), 'username': text(get(prefix+'user')),
                'password': secret(get(prefix+'pass'), name+'.password'), 'password_set': False,
                'tls': False, 'ca_file': '', 'client_cert_file': '', 'client_key_file': '',
                'send_gga': False}

    upstream = endpoint('ntr_cli_', 'upstream')
    if boolean(get('ntr_cli_active', '0')):
        requested.append('upstream')
    caster = {'enabled': False, 'port': integer(get('ntr_cst_port', '2101'), 65535),
              'mountpoint': text(get('ntr_cst_mp', 'RTCM3')),
              'username': text(get('ntr_cst_user')),
              'password': secret(get('ntr_cst_pass'), 'caster.password')}
    if bool(caster['username']) != bool(caster['password']):
        missing.extend(['caster.username', 'caster.password'])
        caster['username'] = caster['password'] = ''
    if boolean(get('ntr_cst_active', '0')):
        requested.append('caster')
    wifi = {'sta': False, 'ap': False,
            'ssid': text(get('w_sta_ssid')), 'password': secret(get('w_sta_pass'), 'wifi.password'),
            'ap_ssid': text(get('w_ap_ssid', 'UG-RTK')), 'ap_password': secret(get('w_ap_pass'), 'wifi.ap_password')}
    for field in ('sta', 'ap'):
        if boolean(get('w_'+field+'_active', '0')):
            requested.append('wifi.'+field)
    publishers = [{'slot': i, 'enabled': False, 'endpoint': {'host': '', 'port': 2101,
                  'mountpoint': '', 'username': '', 'password': '', 'password_set': False,
                  'tls': False, 'ca_file': '', 'client_cert_file': '', 'client_key_file': '',
                  'send_gga': False}} for i in range(5)]
    if slots is not None:
        records = slots.get('slots')
        if not isinstance(records, list) or len(records) != 5:
            raise ValueError('expected all five authoritative NTRIP slot records')
        seen = set()
        for slot in records:
            required = {'index', 'role', 'enabled', 'host', 'port', 'mountpoint', 'username',
                        'password', 'has_password', 'use_tls'}
            if not isinstance(slot, dict) or not required.issubset(slot):
                raise ValueError('incomplete authoritative slot record')
            slot_metadata.update(set(slot)-required)
            index = integer(slot.get('index'), 4)
            if index in seen or slot.get('role') != 'server':
                raise ValueError('duplicate slot or unsupported role')
            seen.add(index)
            target = publishers[index]['endpoint']
            for key in ('host', 'mountpoint', 'username'):
                target[key] = text(slot.get(key, ''))
            target['port'] = integer(slot.get('port', 2101), 65535)
            target['tls'] = boolean(slot.get('use_tls', False))
            target['password'] = secret(slot.get('password', ''), f'publisher.{index}.password')
            if boolean(slot.get('has_password', False)) and not target['password']:
                missing.append(f'publisher.{index}.password')
            if boolean(slot.get('enabled', False)):
                requested.append(f'publisher.{index}')
    patch = {'schema': 1, 'input': 'ntrip',
             'gnss': {'enabled': False, 'tx_pin': -1, 'rx_pin': -1, 'baud': 115200, 'model': 'auto'},
             'upstream': {'enabled': False, 'endpoint': upstream}, 'publishers': publishers,
             'caster': caster, 'wifi': wifi, 'radio': {'enabled': False, 'channel': 1, 'peer': ''}}
    report = {'contract_reference': REFERENCE,
              'source_identity': 'operator-supplied exports; firmware identity not cryptographically authenticated',
              'copied_flat_fields': sorted(copied), 'preserved_only_in_original_export': sorted(set(config)-used),
              'credentials_to_reenter': sorted(set(missing)), 'previously_enabled_requires_review': requested,
              'publishers_from_authoritative_export': slots is not None,
              'slot_metadata_preserved_only_in_original_export': sorted(slot_metadata),
              'ntrip_metadata_preserved_only_in_original_export': sorted(set(slots)-{'slots'}) if slots is not None else [],
              'all_services_disabled': True, 'hardware_base_admin_tls_identity_not_imported': True,
              'upstream_tls_gga_require_explicit_review': True,
              'legacy_nvs_untouched': True, 'no_device_access': True}
    return patch, report


def prepare(config_path, output, validator, board, role, ntrip_path=None):
    if (board, role) not in {('waveshare-s3-eth', 'gateway'), ('waveshare-s3-eth', 'relay-ntrip'),
                            ('supermini-c3', 'relay-ntrip')}:
        raise ValueError('unsupported configuration migration profile')
    output = Path(output).resolve()
    if output == ROOT or ROOT in output.parents or output.exists():
        raise ValueError('output must be new and outside UG source roots')
    config, digest = load_export(config_path)
    slots, slot_digest = load_export(ntrip_path) if ntrip_path else (None, None)
    patch, report = convert(config, slots)
    encoded = json.dumps(patch, ensure_ascii=True, sort_keys=True, separators=(',', ':'))+'\n'
    if len(encoded.encode()) > 8192:
        raise ValueError('native candidate exceeds budget')
    checked = subprocess.run([str(validator), board, role], input=encoded, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    if checked.returncode != 0 or checked.stdout.strip() != 'UG_SCHEMA_VALID':
        raise ValueError('actual native UG schema rejected the candidate')
    report.update({'config_sha256': digest, 'ntrip_sha256': slot_digest,
                   'board': board, 'role': role, 'native_schema_validation': 'PASS',
                   'validator_sha256': hashlib.sha256(Path(validator).read_bytes()).hexdigest()})
    output.mkdir(mode=0o700, parents=True)
    # No global umask change, overwrite, source copy, console script or auto-apply.
    for name, contents in {'candidate.json': encoded, 'review.json': json.dumps(report, sort_keys=True, indent=2)+'\n'}.items():
        with os.fdopen(os.open(output/name, os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600), 'w') as stream:
            stream.write(contents)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy-config', required=True)
    parser.add_argument('--legacy-ntrip', help='authoritative /api/ntrip JSON; old flat publisher keys are not authoritative')
    parser.add_argument('--validator', required=True, help='ug_validate_settings built from this UG checkout')
    parser.add_argument('--output', required=True)
    parser.add_argument('--board', required=True, choices=('waveshare-s3-eth', 'supermini-c3'))
    parser.add_argument('--role', required=True, choices=('gateway', 'relay-ntrip'))
    args = parser.parse_args()
    try:
        prepare(args.legacy_config, args.output, args.validator, args.board, args.role, args.legacy_ntrip)
    except (OSError, ValueError, RecursionError, subprocess.SubprocessError):
        parser.exit(1, 'Migration refused; inspect the exports and native schema without publishing their contents.\n')
    print('Private candidate and review prepared. No configuration applied or device accessed.')
