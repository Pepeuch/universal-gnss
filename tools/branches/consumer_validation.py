"""Export one CI-01 installation and test it in a fresh, offline container."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tarfile
import tempfile
import time

TOOL = Path(__file__).resolve().parent


def consume(export, image, expected_metadata, fixture, *, expected_error=None):
    """Only installation, provenance, synthetic input and test code cross this boundary."""
    export = Path(export)
    if json.loads((export / 'source.json').read_text()) != expected_metadata:
        raise ValueError('installation source metadata does not match the generated candidate')
    archive_hash = hashlib.sha256((export / 'install.tar').read_bytes()).hexdigest()
    print(json.dumps({'consumer_candidate': expected_metadata['source_commit'],
                      'install_archive_sha256': archive_hash, 'dependency_image': image}), flush=True)
    with tempfile.TemporaryDirectory(prefix='ug-installed-consumer-') as directory:
        root = Path(directory)
        bundle = root / 'input.tar'
        with tarfile.open(bundle, 'w') as output:
            output.add(export / 'install.tar', arcname='install.tar')
            output.add(export / 'source.json', arcname='source.json')
            output.add(TOOL / 'consumer', arcname='test', filter=lambda item: None
                       if '__pycache__' in Path(item.name).parts else item)
            output.add(fixture, arcname='basic_fix.nmea')
        cidfile = root / 'container-id'
        command = ['docker', 'run', '--cidfile', str(cidfile), '--interactive', '--pull=never',
                   '--network', 'none', '--user', f'{os.getuid()}:{os.getgid()}', '--workdir', '/tmp',
                   '--tmpfs', '/workspaces:rw,nosuid,nodev', '--entrypoint', '/usr/bin/env', image,
                   '-i', 'PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
                   'HOME=/tmp/ug-consumer-home', 'ROS_LOG_DIR=/tmp/ug-consumer-log',
                   'ROS_LOCALHOST_ONLY=1', 'ROS_DOMAIN_ID=73', 'PYTHONDONTWRITEBYTECODE=1',
                   'bash', '--noprofile', '--norc', '-c',
                   'set -eo pipefail; mkdir -p /tmp/ug-consumer/input; '
                   'cd /tmp/ug-consumer/input; tar -xf -; bash test/run.sh']
        try:
            with bundle.open('rb') as data:
                if expected_error is None:
                    subprocess.run(command, stdin=data, check=True, timeout=300)
                else:
                    result = subprocess.run(command, stdin=data, timeout=300,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                    print(result.stdout, end='', flush=True)
                    if result.returncode == 0 or expected_error not in result.stdout:
                        raise ValueError('negative consumer did not reject the missing installation contract')
        finally:
            if cidfile.is_file():
                subprocess.run(['docker', 'rm', '-f', cidfile.read_text().strip()],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    return {'source_commit': expected_metadata['source_commit'],
            'install_archive_sha256': archive_hash, 'dependency_image': image,
            'result': 'expected_rejection' if expected_error else 'pass'}


def negative_exports(export, image, metadata, fixture):
    """Positive consumption must succeed first; each mutation has a specific failure oracle."""
    export = Path(export)
    cases = [('prefix', None, 'missing relocated install prefix'),
             ('export', '/universal_gnss_ros2Config.cmake', 'missing CMake export'),
             ('plugin', '/libuniversal_gnss_mavros_plugin.so', 'Could not find library')]
    results = {}
    for name, suffix, expected_error in cases:
        with tempfile.TemporaryDirectory(prefix='ug-missing-' + name + '-') as directory:
            destination = Path(directory)
            shutil.copyfile(export / 'source.json', destination / 'source.json')
            removed = 0
            with tarfile.open(export / 'install.tar') as original, \
                    tarfile.open(destination / 'install.tar', 'w') as mutated:
                for entry in original:
                    if suffix is None or entry.name.endswith(suffix):
                        removed += 1
                    else:
                        mutated.addfile(entry, original.extractfile(entry) if entry.isfile() else None)
            if not removed:
                raise ValueError('negative fixture did not remove the expected contract: ' + name)
            results[name] = consume(destination, image, metadata, fixture,
                                    expected_error=expected_error)['result']
            print('PASS expected rejection of missing ' + name, flush=True)
    return results


def validate(metadata, source, image):
    """Reuse the isolated row's build, then discard its container before consumption."""
    if os.getuid() == 0:
        raise ValueError('installed validation requires the normal project user')
    image_id = subprocess.check_output(['docker', 'image', 'inspect', '--format', '{{.Id}}', image],
                                       text=True).strip()
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='ug-install-export-') as directory:
        try:
            subprocess.run(['bash', str(TOOL / 'test_isolated.sh'), 'lyrical', str(source), image_id,
                            directory], check=True, timeout=1800)
        finally:
            # Also handles a host-side timeout that killed Bash before its EXIT trap.
            cidfile = Path(directory) / 'producer-container-id'
            if cidfile.is_file():
                subprocess.run(['docker', 'rm', '-f', cidfile.read_text().strip()],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        result = consume(directory, image_id, metadata, Path(source) / 'testdata/nmea/basic_fix.nmea')
        result['negative_controls'] = negative_exports(
            directory, image_id, metadata, Path(source) / 'testdata/nmea/basic_fix.nmea')
    result['producer_and_consumer_seconds'] = round(time.monotonic() - started, 2)
    print(json.dumps({'installed_consumer': result}), flush=True)
    return result
