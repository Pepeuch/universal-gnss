#!/usr/bin/env python3
"""Compile immutable native ESP32 sources in a disposable SDK; never flash or publish."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
VARIANTS = {
    's3-gateway': ('gateway', 'waveshare-s3-eth', True, None),
    'c3-relay': ('relay-ntrip', 'supermini-c3', False, None),
    'c3-rover-usb': ('rover', 'supermini-c3', False, None),
    'c3-rover-uart': ('rover', 'supermini-c3', False, 4),
}
SOURCE = '/tmp/ug-source'
NATIVE = SOURCE + '/embedded/esp32/native'
SDK_SETUP = 'set -euo pipefail; source /opt/esp/idf/export.sh >/dev/null 2>&1; '


def run(command, *, capture=False, data=None, timeout=1200):
    return subprocess.run(command, check=True, input=data, stdout=subprocess.PIPE if capture else None,
                          timeout=timeout)


def source_identity(repo, ref):
    object_id = run(['git', '-C', str(repo), 'rev-parse', '--verify', '--end-of-options', ref + '^{object}'],
                    capture=True).stdout.decode().strip()
    if not re.fullmatch(r'[0-9a-f]{40}', object_id):
        raise ValueError('source must resolve to an immutable SHA-1 Git object')
    kind = run(['git', '-C', str(repo), 'cat-file', '-t', object_id], capture=True).stdout.decode().strip()
    if kind not in ('commit', 'tree'):
        raise ValueError('source must be a commit or an explicitly prepared local tree')
    tree = run(['git', '-C', str(repo), 'rev-parse', object_id + '^{tree}'], capture=True).stdout.decode().strip()
    return {'source_object': object_id, 'source_type': kind, 'source_tree': tree,
            'source_commit': object_id if kind == 'commit' else None}


def sdk_image(inventory):
    image = inventory['sdk']['image']
    if inventory['sdk']['version'] != '6.1' or not re.fullmatch(r'espressif/idf@sha256:[0-9a-f]{64}', image):
        raise ValueError('official SDK 6.1 image must be pinned by digest')
    return image


def require_isolation(info, uid, gid):
    if (info['Config']['User'] != f'{uid}:{gid}' or info['Mounts'] or
            info['HostConfig'].get('Devices') or info['HostConfig'].get('Privileged') or
            info['NetworkSettings']['Networks']):
        raise ValueError('SDK isolation boundary violated')


@contextmanager
def host_dependency_image(base, dockerfile):
    tag = 'ug-native-host-deps:' + uuid.uuid4().hex
    succeeded = False
    try:
        # A Dockerfile on stdin is an empty context: no project source enters
        # the root dependency-install layer. The official base is digest-pinned.
        run(['docker', 'build', '--build-arg', 'SDK_IMAGE=' + base, '--tag', tag, '-'],
            data=dockerfile)
        yield tag
        succeeded = True
    finally:
        try:
            removed = subprocess.run(['docker', 'image', 'rm', tag], timeout=30, check=False)
            cleanup_error = removed.returncode != 0
        except (OSError, subprocess.SubprocessError):
            cleanup_error = True
        if cleanup_error:
            if succeeded:
                raise RuntimeError('host dependency image cleanup failed')
            print('ERROR: host dependency image cleanup failed after an earlier error', file=sys.stderr)


@contextmanager
def sdk_container(image):
    uid, gid = os.getuid(), os.getgid()
    if uid == 0:
        raise ValueError('native CI must run as the normal project user')
    # Only these public settings enter the SDK; no host credentials or source binds.
    cid = run(['docker', 'create', '--pull=never', '--network', 'bridge',
               '--user', f'{uid}:{gid}', '--cap-drop', 'ALL',
               '--security-opt', 'no-new-privileges',
               '--env', 'HOME=/tmp/ug-home', '--env', 'PYTHONDONTWRITEBYTECODE=1',
               '--env', 'IDF_COMPONENT_CHECK_NEW_VERSION=0',
               '--env', 'CMAKE_BUILD_PARALLEL_LEVEL=3', '--env', 'IDF_CCACHE_ENABLE=1',
               '--env', 'CCACHE_DIR=/tmp/ug-ccache',
               '--entrypoint', '/bin/bash', image, '-c', 'exec sleep infinity'], capture=True).stdout.decode().strip()
    succeeded = False
    try:
        run(['docker', 'start', cid])
        yield cid
        succeeded = True
    finally:
        try:
            cleaned = subprocess.run(['docker', 'rm', '-f', cid], timeout=30, check=False)
            cleanup_error = cleaned.returncode != 0
        except (OSError, subprocess.SubprocessError):
            cleanup_error = True
        if cleanup_error:
            if succeeded:
                raise RuntimeError('SDK cleanup failed')
            print('ERROR: SDK cleanup failed after an earlier error', file=sys.stderr)


def sdk(cid, script, *, capture=False, timeout=1200):
    return run(['docker', 'exec', cid, 'bash', '-c', SDK_SETUP + script], capture=capture, timeout=timeout)


def build_arguments(variant):
    profile, board, gnss, tx = VARIANTS[variant]
    args = f'--profile {profile} --board {board}'
    if gnss:
        args += ' --local-gnss'
    if tx is not None:
        args += f' --rover-uart-tx {tx}'
    return args


def verify_arguments(variant):
    profile, _, gnss, tx = VARIANTS[variant]
    args = f'--profile {profile}'
    if gnss:
        args += ' --local-gnss'
    if tx is not None:
        args += f' --rover-uart-tx {tx}'
    return args


def validate(repo, ref, variant):
    if os.getuid() == 0 or variant not in VARIANTS:
        raise ValueError('normal project user and supported variant required')
    identity = source_identity(repo, ref)
    inventory = json.loads(run(['git', '-C', str(repo), 'show',
                                identity['source_object'] + ':embedded/esp32/migration/dependencies.json'],
                               capture=True).stdout)
    image = sdk_image(inventory)
    run(['docker', 'pull', image])
    if variant == 's3-gateway':
        recipe = run(['git', '-C', str(repo), 'show',
                      identity['source_object'] + ':embedded/esp32/native/Dockerfile.ci'], capture=True).stdout
        with host_dependency_image(image, recipe) as dependency_image:
            return validate_container(repo, identity, inventory, image, dependency_image, variant)
    return validate_container(repo, identity, inventory, image, image, variant)


def validate_container(repo, identity, inventory, base_image, image, variant):
    with sdk_container(image) as cid:
        sdk(cid, 'test "$(id -u)" != 0; test ! -e /ug; test ! -e /workspaces/universal-gnss; '
                 f'mkdir -p {SOURCE}')
        # Git archive excludes the index/worktree, oldrtkesp and local checkpoints.
        archive = run(['git', '-C', str(repo), 'archive', '--format=tar', identity['source_object']], capture=True).stdout
        run(['docker', 'exec', '-i', cid, 'tar', '--no-same-owner', '-xf', '-', '-C', SOURCE], data=archive)
        sdk(cid, f'chmod -R a-w {SOURCE}')
        args = build_arguments(variant)
        profile = VARIANTS[variant][0]
        provision = ' --provision' if profile != 'rover' else ''
        sdk(cid, f'python {NATIVE}/build.py --output /tmp/ug-ci-a {args}{provision} --prepare-only')
        sdk(cid, 'test ! -e /tmp/ug-ci-a/build/universal-gnss-esp32.bin; '
                 'test ! -e /tmp/ug-ci-a/build/universal-gnss-esp32.elf')
        if variant == 's3-gateway':
            version = inventory['host_test_only']['quickjs']['version']
            if version != '1.19.4':
                raise ValueError('unreviewed browser-test dependency')
            sdk(cid, f'python -m pip install --disable-pip-version-check --no-cache-dir '
                     f'--target /tmp/ug-host-deps quickjs=={version}')
        # Mandatory boundary: dependency provisioning ends before any project build/test.
        run(['docker', 'network', 'disconnect', 'bridge', cid])
        info, = json.loads(run(['docker', 'inspect', cid], capture=True).stdout)
        require_isolation(info, os.getuid(), os.getgid())
        host_tests = 'not_selected'
        if variant == 's3-gateway':
            sdk(cid, f'cmake -S {NATIVE}/tests -B /tmp/ug-host-build '
                     '-DUG_CJSON_SOURCE=/tmp/ug-ci-a/project/managed_components/espressif__cjson/cJSON '
                     '-DCMAKE_C_FLAGS="-fsanitize=address,undefined -g" '
                     '-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"; '
                     'cmake --build /tmp/ug-host-build -j4; '
                     'ctest --test-dir /tmp/ug-host-build --output-on-failure; '
                     f'PYTHONPATH=/tmp/ug-host-deps python {NATIVE}/tests/test_web_assets.py; '
                     f'python {NATIVE}/tests/test_capture_tools.py')
            host_tests = 'pass'
        sdk(cid, 'idf.py -C /tmp/ug-ci-a/project -B /tmp/ug-ci-a/build build size')
        first = json.loads(sdk(cid, f'python {NATIVE}/verify.py --work /tmp/ug-ci-a '
                                  + verify_arguments(variant), capture=True).stdout)
        cache = ' --dependency-cache /tmp/ug-ci-a/project' if profile != 'rover' else ''
        sdk(cid, f'python {NATIVE}/build.py --output /tmp/ug-ci-b {args}{cache}')
        second = json.loads(sdk(cid, f'python {NATIVE}/verify.py --work /tmp/ug-ci-b '
                                   + verify_arguments(variant), capture=True).stdout)
        if first['sha256'] != second['sha256']:
            raise ValueError('independent app reconstruction differs')
        sdk(cid, 'for f in universal-gnss-esp32.bin bootloader/bootloader.bin '
                 'partition_table/partition-table.bin; do '
                 'cmp "/tmp/ug-ci-a/build/$f" "/tmp/ug-ci-b/build/$f"; done')
        # Assert isolation again after all project/test commands.
        info, = json.loads(run(['docker', 'inspect', cid], capture=True).stdout)
        require_isolation(info, os.getuid(), os.getgid())
        host_dependencies = sdk(cid, 'cat /opt/ug-native-host-dependencies.txt', capture=True).stdout.decode().strip() if variant == 's3-gateway' else 'not_selected'
        return {**identity, 'variant': variant, 'sdk_image': base_image,
                'host_dependencies': host_dependencies, 'build_and_verify': 'pass',
                'reproducibility': 'pass', 'host_tests': host_tests,
                'network_during_build_and_tests': 'disconnected_no_interfaces', 'source_mounts': False,
                'flash_or_publication': False, 'binary': first}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--source-ref', required=True)
    parser.add_argument('--variant', choices=VARIANTS, required=True)
    args = parser.parse_args()
    if os.getuid() == 0:
        parser.error('run as the normal project user')
    # Give cleanup a chance on runner cancellation. SIGKILL cannot be recovered.
    def cancelled(*_):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, cancelled)
    try:
        report = validate(args.repo, args.source_ref, args.variant)
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        print(f'ERROR: native ESP32 qualification failed: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('ERROR: native ESP32 qualification cancelled', file=sys.stderr)
        return 130
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
