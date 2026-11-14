"""Prepare a pinned, auditable historical application overlay; never fetch or flash."""
import argparse
import difflib
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile

import yaml

UG = Path(__file__).resolve().parents[3]
REFERENCE = UG / 'embedded/esp32/historical-reference.json'
PATHS = ['LICENSE', 'README.md', 'CMakeLists.txt', 'components', 'main',
         'managed_components', 'configs/templates', 'firmware/espnow_rover',
         'partitions.csv', 'dependencies.lock', 'www', 'tests/host',
         'scripts/test_espnow_host.sh', 'scripts/check_rtcm_usb.py',
         'scripts/check_rtcm_capture.py']


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *args):
    return subprocess.check_output(['git', '--no-replace-objects', '-c', 'protocol.allow=never', '-C', str(repo), *args])


def extract(repo, sha, target, paths=()):
    if git(repo, 'rev-parse', sha + '^{commit}').decode().strip() != sha:
        raise ValueError('commit mismatch')
    target.mkdir(parents=True, exist_ok=True)
    archive = git(repo, 'archive', sha, *paths)
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        # No archive symlinks/devices: this overlay needs regular build inputs only.
        if any(not (m.isfile() or m.isdir()) for m in stream.getmembers()):
            raise ValueError('non-regular archive member')
        stream.extractall(target, filter='data')


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p.read_bytes())
            for p in sorted(root.rglob('*')) if p.is_file()}


def modes(root):
    return {p.relative_to(root).as_posix(): oct(p.stat().st_mode & 0o777)
            for p in sorted(root.rglob('*')) if p.is_file()}


def verify_mdns(root):
    component = root / 'managed_components/espressif__mdns'
    lock = yaml.safe_load((root / 'dependencies.lock').read_text())
    entry = lock['dependencies']['espressif/mdns']
    if entry['version'] != '1.5.0':
        raise ValueError('unexpected mDNS version')
    if (component / '.component_hash').read_text().strip() != entry['component_hash']:
        raise ValueError('mDNS component hash mismatch')
    checks = json.loads((component / 'CHECKSUMS.json').read_text())
    if checks['algorithm'] != 'sha256':
        raise ValueError('unsupported checksum algorithm')
    missing = []
    for item in checks['files']:
        path = (component / item['path']).resolve()
        if component.resolve() not in path.parents:
            raise ValueError('unsafe checksum path')
        if not path.exists() and path.parent == component / 'tests/test_afl_fuzz_host/in' and path.suffix == '.bin':
            # These upstream fuzz corpus binaries are absent from the pinned Git tree.
            missing.append(item['path'])
            continue
        data = path.read_bytes()
        if len(data) != item['size'] or digest(data) != item['hash']:
            raise ValueError('mDNS checksum mismatch: ' + item['path'])
    listed = {i['path'] for i in checks['files']} | {'.component_hash', 'CHECKSUMS.json'}
    if set(inventory(component)) - listed:
        raise ValueError('unlisted mDNS source file')
    return missing


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('pinned source anchor mismatch: ' + old[:60])
    return text.replace(old, new, 1)


def replace_function(text, start, end, replacement):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ValueError('pinned function boundary mismatch')
    a, b = text.index(start), text.index(end)
    return text[:a] + replacement + '\n\n' + text[b:]


def adapt(root):
    changes = {}
    def edit(path, transform):
        p = root / path
        before = p.read_text()
        after = transform(before)
        p.write_text(after)
        changes[path] = {'before': digest(before.encode()), 'after': digest(after.encode()),
                         'diff': ''.join(difflib.unified_diff(before.splitlines(True),
                                  after.splitlines(True), fromfile='a/' + path, tofile='b/' + path))}
    edit('components/rtcm_filter/src/rtcm_filter.c', lambda s:
         '#include "ug_rtcm.h"\n' + replace_function(s, 'uint32_t rtcm3_crc24q(',
         'void rtcm3_stream_parser_init(', '''uint32_t rtcm3_crc24q(const uint8_t *data, size_t len)
{
    uint32_t crc = 0;
    (void)ug_rtcm_crc24q(data, len, &crc);
    return crc;
}'''))
    edit('components/espnow_rtcm/src/espnow_rtcm_codec.c', lambda s:
         '#include "ug_rtcm.h"\n' + replace_function(s, 'bool espnow_rtcm_frame_valid(',
         'esp_err_t espnow_rtcm_stream_push(', '''bool espnow_rtcm_frame_valid(const uint8_t *frame, size_t len)
{
    return ug_rtcm_frame_valid(frame, len);
}'''))
    edit('components/rtcm_filter/CMakeLists.txt', lambda s:
         replace_once(s, '        esp_timer', '        esp_timer\n        ug_rtcm'))
    edit('components/espnow_rtcm/CMakeLists.txt', lambda s:
         replace_once(s, 'REQUIRES rtcm_filter', 'REQUIRES ug_rtcm rtcm_filter'))
    for path in ('CMakeLists.txt', 'firmware/espnow_rover/CMakeLists.txt'):
        edit(path, lambda s: replace_once(s, 'include($ENV{IDF_PATH}/tools/cmake/project.cmake)',
             'list(APPEND EXTRA_COMPONENT_DIRS "${UG_RTCM_COMPONENT}")\n'
             'include($ENV{IDF_PATH}/tools/cmake/project.cmake)'))
    return changes


def prepare(repo, output, sx_repo=None):
    repo, output = Path(repo).resolve(), Path(output).resolve()
    # Never overwrite or generate inside tracked source roots.
    if output.exists() or output == UG or UG in output.parents:
        raise ValueError('output must be new and outside the UG source tree')
    if output == repo or repo in output.parents:
        raise ValueError('output must be outside the historical checkout')
    ref = json.loads(REFERENCE.read_text())
    nested = ref['nested_submodule']
    expected = f"160000 commit {nested['commit']}\t{nested['path']}"
    if git(repo, 'ls-tree', ref['commit'], nested['path']).decode().strip() != expected:
        raise ValueError('historical nested gitlink mismatch')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.ug-overlay-', dir=output.parent) as temporary:
        stage = Path(temporary)
        root = stage / 'source'
        extract(repo, ref['commit'], root, PATHS)
        extract(sx_repo or repo / nested['path'], nested['commit'], root / nested['path'])
        absent_mdns_vectors = verify_mdns(root)
        before = inventory(root)
        before_modes = modes(root)
        changes = adapt(root)
        after = inventory(root)
        if {p for p in before if before[p] != after[p]} != set(changes):
            raise ValueError('unexpected source mutation')
        if before_modes != modes(root):
            raise ValueError('unexpected source mode change')
        if set(before) != set(after):
            raise ValueError('unexpected source addition/deletion')
        # The remaining historical RTCM production source must not contain a CRC24Q loop.
        for p in root.rglob('*'):
            if p.suffix in ('.c', '.cpp', '.h', '.hpp') and '1864cfb' in p.read_text().lower():
                raise ValueError('second production CRC24Q implementation: ' + str(p))
        ug_files = ['protocols/gnss_protocols/src/rtcm_crc24q.cpp',
                    'protocols/gnss_protocols/include/universal_gnss_protocols/rtcm_crc24q.hpp',
                    'embedded/esp32/components/ug_rtcm/src/rtcm.cpp',
                    'embedded/esp32/components/ug_rtcm/include/ug_rtcm.h',
                    'embedded/esp32/components/ug_rtcm/CMakeLists.txt']
        report = {'historical_commit': ref['commit'], 'nested_commit': nested['commit'],
                  'ug_commit': git(UG, 'rev-parse', 'HEAD').decode().strip(),
                  'ug_inputs': {p: digest((UG / p).read_bytes()) for p in ug_files},
                  'source_before': before, 'source_after': after, 'source_modes': before_modes,
                  'overlay': changes,
                  'licenses': {p: h for p, h in after.items()
                               if Path(p).name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE'))},
                  'absent_mdns_upstream_fuzz_vectors': absent_mdns_vectors,
                  'reference_kind': 'transitional-build-overlay-not-final-esp32-architecture'}
        (stage / 'overlay.json').write_text(json.dumps(report, indent=2) + '\n')
        (stage / 'overlay.patch').write_text(''.join(v['diff'] for v in changes.values()))
        stage.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--historical-repo', required=True)
    parser.add_argument('--sx126x-repo')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    print(prepare(args.historical_repo, args.output, args.sx126x_repo))


if __name__ == '__main__':
    main()
