"""Fail-closed inspection of actual IDF objects, linked ELF and build budgets."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

from prepare import UG, digest


def output(*args):
    return subprocess.check_output([str(a) for a in args], text=True)


def verify(prepared, profile, compare_with=None):
    root = Path(prepared).resolve()
    work = root / profile
    build = work / 'build'
    provenance = json.loads((root / 'overlay.json').read_text())
    for path, expected in provenance['ug_inputs'].items():
        if digest((UG / path).read_bytes()) != expected:
            raise ValueError('UG input changed since preparation: ' + path)
    for path, expected in provenance['source_after'].items():
        p = root / 'source' / path
        if digest(p.read_bytes()) != expected:
            raise ValueError('historical input changed: ' + path)
        if 'source_modes' in provenance and oct(p.stat().st_mode & 0o777) != provenance['source_modes'][path]:
            raise ValueError('historical input mode changed: ' + path)
    entries = json.loads((build / 'compile_commands.json').read_text())
    crc = [c for c in entries if c['file'] == str(UG / 'protocols/gnss_protocols/src/rtcm_crc24q.cpp')]
    if len(crc) != 1:
        raise ValueError('must compile exactly the existing UG CRC source once')
    compiler = shlex.split(crc[0]['command'])[0]
    prefix = compiler.removesuffix('g++')
    nm, objdump = prefix + 'nm', prefix + 'objdump'
    elf, = build.glob('*.elf')
    symbols = output(nm, '-C', '--defined-only', elf)
    names = ('ug_rtcm_crc24q', 'ug_rtcm_frame_valid', 'rtcm3_crc24q',
             'espnow_rtcm_frame_valid', 'lora_transport_crc16',
             'universal_gnss_protocols::ComputeRtcmCrc24Q(',
             'universal_gnss_protocols::ValidateRtcmCrc24Q(')
    selected = {}
    for name in names:
        lines = [line for line in symbols.splitlines()
                 if line.split(' ', 2)[-1] == name or
                 (name.endswith('(') and line.split(' ', 2)[-1].startswith(name))]
        if not lines and profile == 'c3-rover' and name in ('ug_rtcm_crc24q', 'rtcm3_crc24q'):
            # Rover receives complete frames; raw-stream CRC entry points are GC'd.
            selected[name] = 'not linked: unused raw-stream entry point'
            continue
        if len(lines) != 1:
            raise ValueError('missing/duplicate linked symbol: ' + name)
        selected[name] = lines[0]
    proof = {}
    for suffix, callee in (('/rtcm_filter.c', 'ug_rtcm_crc24q'),
                           ('/espnow_rtcm_codec.c', 'ug_rtcm_frame_valid'),
                           ('/ug_rtcm/src/rtcm.cpp', 'ComputeRtcmCrc24Q')):
        command, = [c for c in entries if c['file'].endswith(suffix)]
        obj = build / command['output']
        undefined = output(nm, '-C', '-u', obj)
        relocations = output(objdump, '-dr', obj)
        if callee not in undefined or callee not in relocations:
            raise ValueError('missing object call relocation: ' + callee)
        proof[callee] = undefined.splitlines()
        (work / (obj.name + '.relocations.txt')).write_text(relocations)
    ug_undefined = output(nm, '-u', build / 'esp-idf/ug_rtcm/libug_rtcm.a')
    if any(name in ug_undefined for name in ('malloc', 'calloc', 'realloc', ' free', '_Zn')):
        raise ValueError('unexpected UG allocation dependency')
    map_file = elf.with_suffix('.map')
    mapping = map_file.read_text()
    for token in ('libug_rtcm.a(rtcm_crc24q.cpp.obj)', 'libug_rtcm.a(rtcm.cpp.obj)'):
        if token not in mapping:
            raise ValueError('missing linked UG object in map: ' + token)
    config = (work / 'sdkconfig').read_text().splitlines()
    expected = ['CONFIG_ESPNOW_RTCM_CHANNEL=1', 'CONFIG_APP_REPRODUCIBLE_BUILD=y']
    if profile == 'c3-rover':
        expected += ['CONFIG_ESPNOW_RTCM_ROVER=y', 'CONFIG_ESP_CONSOLE_NONE=y',
                     'CONFIG_ESP_CONSOLE_SECONDARY_NONE=y', 'CONFIG_LOG_DEFAULT_LEVEL=0',
                     'CONFIG_BOOTLOADER_LOG_LEVEL=0', 'CONFIG_LOG_MAXIMUM_LEVEL=0',
                     'CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:01"',
                     'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y']
        if any(c['file'].endswith('/ntrip_client.c') for c in entries):
            raise ValueError('NTRIP unexpectedly compiled into rover')
    else:
        expected += ['CONFIG_ESPNOW_RTCM_BASE=y', 'CONFIG_BOARD_WAVESHARE_ESP32S3_ETH=y',
                     '# CONFIG_LORA_FEATURE_ENABLED is not set', 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
                     'CONFIG_SPIRAM=y', 'CONFIG_SPIRAM_MODE_OCT=y',
                     'CONFIG_SPIRAM_TYPE_ESPPSRAM64=y',
                     'CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:02"']
    if not set(expected) <= set(config):
        raise ValueError('unexpected effective configuration: ' + repr(set(expected) - set(config)))
    sizes = json.loads(output('python', '-m', 'esp_idf_size', '--format', 'json', map_file))
    archives = json.loads(output('python', '-m', 'esp_idf_size', '--format', 'json', '--archives', map_file))
    if archives.get('libug_rtcm.a', {}).get('ram_st_total') != 0:
        raise ValueError('unexpected UG static RAM allocation')
    artifacts = {p.relative_to(build).as_posix(): {'bytes': p.stat().st_size, 'sha256': digest(p.read_bytes())}
                 for p in sorted(build.rglob('*.bin'))}
    application = elf.with_suffix('.bin')
    app_budget = (3 * 1024 * 1024) if profile == 's3-gateway' else (1280 * 1024)
    if application.stat().st_size > app_budget:
        raise ValueError('application exceeds pinned OTA partition')
    if compare_with is not None:
        other = Path(compare_with).resolve()
        other_provenance = json.loads((other / 'overlay.json').read_text())
        for key in ('historical_commit', 'nested_commit', 'ug_inputs', 'source_after'):
            if provenance[key] != other_provenance[key]:
                raise ValueError('reproduction input mismatch: ' + key)
        other_build = other / profile / 'build'
        other_artifacts = {p.relative_to(other_build).as_posix():
                           {'bytes': p.stat().st_size, 'sha256': digest(p.read_bytes())}
                           for p in sorted(other_build.rglob('*.bin'))}
        if artifacts != other_artifacts or elf.read_bytes() != (other_build / elf.name).read_bytes():
            raise ValueError('reproduction mismatch: image or ELF bytes')
    result = {'profile': profile, 'symbols': selected, 'object_calls': proof,
              'sizes': sizes, 'archive_sizes': archives, 'artifacts': artifacts,
              'app_partition_bytes': app_budget, 'ug_undefined': ug_undefined.splitlines(),
              'reproduced': compare_with is not None}
    (work / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'profile': profile, 'symbols': 'PASS', 'config': 'PASS',
                      'app_bytes': application.stat().st_size, 'app_partition_bytes': app_budget,
                      'memory': sizes}, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', required=True)
    parser.add_argument('--profile', required=True, choices=('s3-gateway', 'c3-rover'))
    parser.add_argument('--compare-with', help='second independently prepared/built root')
    args = parser.parse_args()
    verify(args.prepared, args.profile, args.compare_with)
