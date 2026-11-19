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
    's3-gateway-http': ('gateway', 'waveshare-s3-eth', True, None),
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


def uart_options(variant, tx=None, baud=115200):
    if variant != 'c3-rover-uart':
        if tx is not None or baud != 115200: raise ValueError('UART options require UART rover')
        return None, baud
    tx = 4 if tx is None else tx
    if type(tx) is not int or tx not in (4,5,6,7,10) or type(baud) is not int or baud not in (115200,230400,460800,921600):
        raise ValueError('unsupported UART option')
    return tx, baud


def build_arguments(variant, uart_tx=None, uart_baud=115200):
    profile, board, gnss, _ = VARIANTS[variant]
    tx, uart_baud = uart_options(variant, uart_tx, uart_baud)
    args = f'--profile {profile} --board {board}'
    if gnss:
        args += ' --local-gnss'
    if tx is not None:
        args += f' --rover-uart-tx {tx} --rover-uart-baud {uart_baud}'
    if variant == 's3-gateway-http':
        args += ' --web-transport http'
    return args


def verify_arguments(variant, uart_tx=None, uart_baud=115200):
    profile, _, gnss, _ = VARIANTS[variant]
    tx, uart_baud = uart_options(variant, uart_tx, uart_baud)
    args = f'--profile {profile}'
    if gnss:
        args += ' --local-gnss'
    if tx is not None:
        args += f' --rover-uart-tx {tx} --rover-uart-baud {uart_baud}'
    if variant == 's3-gateway-http':
        args += ' --web-transport http'
    return args


def source_has_identity(repo, source):
    # Compatibility with immutable Releases predating this bootstrap. Inspect
    # source only; do not execute branch code on the host or rewrite old tags.
    builder = run(['git', '-C', str(repo), 'show',
                   source + ':embedded/esp32/native/build.py'], capture=True).stdout
    return b'def firmware_identity(' in builder


def validate(repo, ref, variant, bundle_output=None, uart_tx=None, uart_baud=115200):
    if os.getuid() == 0 or variant not in VARIANTS:
        raise ValueError('normal project user and supported variant required')
    uart_options(variant,uart_tx,uart_baud)
    if bundle_output is not None:
        bundle_output=Path(bundle_output).resolve()
        if variant=='c3-relay' or bundle_output.exists() or bundle_output.is_relative_to(Path(repo).resolve()):
            raise ValueError('bundle requires a catalogue variant and new output outside source')
    identity = source_identity(repo, ref)
    if bundle_output is not None and identity['source_type']!='commit': raise ValueError('bundle requires a Git commit')
    inventory = json.loads(run(['git', '-C', str(repo), 'show',
                                identity['source_object'] + ':embedded/esp32/migration/dependencies.json'],
                               capture=True).stdout)
    image = sdk_image(inventory)
    run(['docker', 'pull', image])
    if variant.startswith('s3-gateway'):
        recipe = run(['git', '-C', str(repo), 'show',
                      identity['source_object'] + ':embedded/esp32/native/Dockerfile.ci'], capture=True).stdout
        with host_dependency_image(image, recipe) as dependency_image:
            return validate_container(repo, identity, inventory, image, dependency_image, variant, bundle_output, uart_tx, uart_baud)
    return validate_container(repo, identity, inventory, image, image, variant, bundle_output, uart_tx, uart_baud)


def validate_container(repo, identity, inventory, base_image, image, variant, bundle_output=None, uart_tx=None, uart_baud=115200):
    with sdk_container(image) as cid:
        sdk(cid, 'test "$(id -u)" != 0; test ! -e /ug; test ! -e /workspaces/universal-gnss; '
                 f'mkdir -p {SOURCE}')
        # Git archive excludes the index/worktree, oldrtkesp and local checkpoints.
        archive = run(['git', '-C', str(repo), 'archive', '--format=tar', identity['source_object']], capture=True).stdout
        run(['docker', 'exec', '-i', cid, 'tar', '--no-same-owner', '-xf', '-', '-C', SOURCE], data=archive)
        sdk(cid, f'chmod -R a-w {SOURCE}')
        args = build_arguments(variant, uart_tx, uart_baud)
        profile = VARIANTS[variant][0]
        import shlex
        identified = source_has_identity(repo, identity['source_object'])
        source_args = ''
        if identified:
            commit = identity['source_commit'] or run(['git','-C',str(repo),'rev-parse','HEAD'],capture=True).stdout.decode().strip()
            version_query = subprocess.run(['git','-C',str(repo),'describe','--tags','--match','v[0-9]*','--abbrev=0',commit],capture_output=True,text=True,check=False)
            version = version_query.stdout.strip() if version_query.returncode==0 else 'unversioned'
            source_args = (' --source-sha '+commit+' --source-tree '+identity['source_tree']+
                           ' --ug-version '+shlex.quote(version)+(' --source-modified' if identity['source_type']=='tree' else ''))
        elif variant == 's3-gateway-http':
            raise ValueError('explicit HTTP catalogue variant is unavailable on this historical source')
        provision = ' --provision' if profile != 'rover' else ''
        sdk(cid, f'python {NATIVE}/build.py --output /tmp/ug-ci-a {args}{provision}{source_args} --prepare-only')
        sdk(cid, 'test ! -e /tmp/ug-ci-a/build/universal-gnss-esp32.bin; '
                 'test ! -e /tmp/ug-ci-a/build/universal-gnss-esp32.elf')
        if variant.startswith('s3-gateway'):
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
        if variant.startswith('s3-gateway'):
            sdk(cid, f'cmake -S {NATIVE}/tests -B /tmp/ug-host-build '
                     '-DUG_CJSON_SOURCE=/tmp/ug-ci-a/project/managed_components/espressif__cjson/cJSON '
                     '-DCMAKE_C_FLAGS="-fsanitize=address,undefined -g" '
                     '-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"; '
                     'cmake --build /tmp/ug-host-build -j4; '
                     'ctest --test-dir /tmp/ug-host-build --output-on-failure; '
                     f'PYTHONPATH=/tmp/ug-host-deps python {NATIVE}/tests/test_web_assets.py; '
                     f'PYTHONPATH=/tmp/ug-host-deps python {NATIVE}/tests/test_flasher_browser.py; '
                     f'PYTHONPATH=/tmp/ug-host-deps python {NATIVE}/tests/test_browser_update.py; '
                     f'PYTHONPATH=/tmp/ug-host-deps python {NATIVE}/tests/test_portable_browser.py; '
                     f'python {NATIVE}/tests/test_capture_tools.py')
            host_tests = 'pass'
        if variant == 's3-gateway' and identified:
            # Real SDK crypto/NVS, on an emulated S3. No firmware variant,
            # physical device, network or publication side effect.
            sdk(cid, f'cp -R {NATIVE}/tests/tls_boot /tmp/ug-tls-test-project; '
                     'chmod -R u+w /tmp/ug-tls-test-project; '
                     'IDF_COMPONENT_MANAGER=0 idf.py -C /tmp/ug-tls-test-project '
                     '-B /tmp/ug-tls-test-build -DIDF_TARGET=esp32s3 '
                     f'-DUG_ROOT={SOURCE} '
                     '-DUG_CJSON_COMPONENT=/tmp/ug-ci-a/project/managed_components/espressif__cjson build; '
                     f'python {NATIVE}/tools/test_tls_boot.py --build /tmp/ug-tls-test-build '
                     '--output /tmp/ug-tls-qemu-report.json')
        sdk(cid, 'idf.py -C /tmp/ug-ci-a/project -B /tmp/ug-ci-a/build build size')
        first = json.loads(sdk(cid, f'python {NATIVE}/verify.py --work /tmp/ug-ci-a '
                                  + verify_arguments(variant, uart_tx, uart_baud), capture=True).stdout)
        cache = ' --dependency-cache /tmp/ug-ci-a/project' if profile != 'rover' else ''
        sdk(cid, f'python {NATIVE}/build.py --output /tmp/ug-ci-b {args}{cache}{source_args}')
        second = json.loads(sdk(cid, f'python {NATIVE}/verify.py --work /tmp/ug-ci-b '
                                   + verify_arguments(variant, uart_tx, uart_baud), capture=True).stdout)
        if first['sha256'] != second['sha256']:
            raise ValueError('independent app reconstruction differs')
        sdk(cid, 'for f in universal-gnss-esp32.bin bootloader/bootloader.bin '
                 'partition_table/partition-table.bin ota_data_initial.bin; do '
                 'cmp "/tmp/ug-ci-a/build/$f" "/tmp/ug-ci-b/build/$f"; done')
        # Assert isolation again after all project/test commands.
        info, = json.loads(run(['docker', 'inspect', cid], capture=True).stdout)
        require_isolation(info, os.getuid(), os.getgid())
        host_dependencies = sdk(cid, 'cat /opt/ug-native-host-dependencies.txt', capture=True).stdout.decode().strip() if variant.startswith('s3-gateway') else 'not_selected'
        report = {**identity, 'variant': variant, 'sdk_image': base_image,
                'host_dependencies': host_dependencies, 'build_and_verify': 'pass',
                'reproducibility': 'pass', 'host_tests': host_tests,
                'network_during_build_and_tests': 'disconnected_no_interfaces', 'source_mounts': False,
                'flash_or_publication': False, 'binary': first}
        part_script = "import json,hashlib;from pathlib import Path;root=Path('/tmp/ug-ci-a/build');paths=('bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin','universal-gnss-esp32.bin');print(json.dumps({'part_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'flash_offsets':{p:int(o,0) for o,p in json.loads((root/'flasher_args.json').read_text())['flash_files'].items()}}))"
        import shlex
        report.update(json.loads(sdk(cid,'python -c '+shlex.quote(part_script),capture=True).stdout))
        expected_parts={'bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin','universal-gnss-esp32.bin'}
        if set(report.get('part_sha256',{}))!=expected_parts or set(report.get('flash_offsets',{}))!=expected_parts:
            raise ValueError('qualification part/offset inventory mismatch')
        if bundle_output is not None:
            # Only explicit local output. No release, Pages, push or artifact service.
            import importlib.util
            spec=importlib.util.spec_from_file_location('bundle_profiles',ROOT/'embedded/esp32/native/profiles.py')
            profiles=importlib.util.module_from_spec(spec);spec.loader.exec_module(profiles)
            tx,baud=uart_options(variant,uart_tx,uart_baud)
            profile=profiles.from_variant(variant,4 if tx is None else tx,baud)
            for path,value in (('/tmp/ug-qualification.json',report),('/tmp/ug-profile.json',profile)):
                run(['docker','exec','-i',cid,'python','-c',
                     'import sys;from pathlib import Path;Path(sys.argv[1]).write_bytes(sys.stdin.buffer.read())',path],
                    data=json.dumps(value,sort_keys=True).encode())
            packaged=json.loads(sdk(cid,f'python {SOURCE}/tools/package_firmware.py --build /tmp/ug-ci-a/build '
                                   '--verified /tmp/ug-ci-a/native-verification.json --profile-json /tmp/ug-profile.json '
                                   '--qualification /tmp/ug-qualification.json --output /tmp/ug-bundle '
                                   + '--source-sha '+identity['source_commit'],capture=True).stdout)
            run(['docker','exec','-i',cid,'python','-c',
                 'import sys;from pathlib import Path;Path("/tmp/ug-bundle/catalog-entry.json").write_bytes(sys.stdin.buffer.read())'],
                data=json.dumps(packaged['catalog_entry'],sort_keys=True,indent=2).encode()+b'\n')
            run(['docker','cp',cid+':/tmp/ug-bundle/.',str(bundle_output)])
            report['bundle_created']=True
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--source-ref', required=True)
    parser.add_argument('--variant', choices=VARIANTS, required=True)
    parser.add_argument('--bundle-output',type=Path,help='new local directory outside source; no publication')
    parser.add_argument('--uart-tx',type=int,choices=(4,5,6,7,10))
    parser.add_argument('--uart-baud',type=int,choices=(115200,230400,460800,921600),default=115200)
    args = parser.parse_args()
    if os.getuid() == 0:
        parser.error('run as the normal project user')
    # Give cleanup a chance on runner cancellation. SIGKILL cannot be recovered.
    def cancelled(*_):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, cancelled)
    try:
        report = validate(args.repo, args.source_ref, args.variant,args.bundle_output,args.uart_tx,args.uart_baud)
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
