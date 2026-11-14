"""Compile the two pinned reference applications; no serial, flash or network action."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from prepare import UG, digest, inventory

S3_TEMPLATES = ('common', 'esp32s3', 'flash_16mb', 'psram_8mb',
                'board_waveshare_esp32s3_eth', 'role_base', 'eth_w5500', 'gnss_unicore',
                'lora_sx1262', 'lora_driver_sx126x', 'region_eu868', 'espnow_base')
C3_PARTITIONS = '''# Compile-only copy of the reference 4MB OTA layout; never flash automatically.
nvs,data,nvs,0x9000,20K,
otadata,data,ota,0xe000,8K,
app0,app,ota_0,0x10000,1280K,
app1,app,ota_1,0x150000,1280K,
spiffs,data,spiffs,0x290000,1408K,
coredump,data,coredump,0x3f0000,64K,
'''


def merged_defaults(parts):
    """Resolve overrides before Kconfig; no dependence on duplicate-assignment policy."""
    values = {}
    for part in parts:
        for line in part.splitlines():
            match = re.match(r'(?:# )?(CONFIG_[A-Z0-9_]+)(?:=| is not set)', line)
            if match:
                values[match[1]] = line
    return '\n'.join(values.values()) + '\n'


def run(root, profile):
    root = Path(root).resolve()
    report = json.loads((root / 'overlay.json').read_text())
    source = root / 'source'
    if inventory(source) != report['source_after']:
        raise ValueError('prepared source changed before build')
    for path, expected in report['ug_inputs'].items():
        if digest((UG / path).read_bytes()) != expected:
            raise ValueError('UG build input changed: ' + path)
    version = subprocess.check_output(['idf.py', '--version'], text=True).strip()
    if version != 'ESP-IDF v5.5.3':
        raise ValueError('requires ESP-IDF v5.5.3, got ' + version)
    work = root / profile
    work.mkdir(exist_ok=False)
    if profile == 's3-gateway':
        project, target = source, 'esp32s3'
        parts = [(source / f'configs/templates/sdkconfig.{p}.defaults').read_text()
                 for p in S3_TEMPLATES]
        parts += ['''CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:02"
# CONFIG_LORA_FEATURE_ENABLED is not set
# CONFIG_RTK_LORA_TX_ENABLED is not set
''']
    else:
        project, target = source / 'firmware/espnow_rover', 'esp32c3'
        partition = work / 'partitions.csv'
        partition.write_text(C3_PARTITIONS)
        parts = [(project / 'sdkconfig.defaults').read_text(), f'''CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:01"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{partition}"
''']
    parts += ['''CONFIG_ESPNOW_RTCM_CHANNEL=1
CONFIG_APP_REPRODUCIBLE_BUILD=y
CONFIG_COMPILER_HIDE_PATHS_MACROS=y
''']
    defaults = work / 'sdkconfig.defaults'
    defaults.write_text(merged_defaults(parts))
    command = ['idf.py', '-C', str(project), '-B', str(work / 'build'),
               '-D', 'IDF_TARGET=' + target, '-D', 'SDKCONFIG=' + str(work / 'sdkconfig'),
               '-D', 'SDKCONFIG_DEFAULTS=' + str(defaults),
               '-D', 'UG_RTCM_COMPONENT=' + str(UG / 'embedded/esp32/components/ug_rtcm'),
               'build', 'size']
    subprocess.run(command, check=True)
    # IDF may generate lock/cache files, but every archived source must retain its bytes.
    for path, expected in report['source_after'].items():
        if digest((source / path).read_bytes()) != expected:
            raise ValueError('archived source changed during build: ' + path)
    print('Compiled only; no flash or hardware qualification:', profile)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', required=True)
    parser.add_argument('--profile', required=True, choices=('s3-gateway', 'c3-rover'))
    args = parser.parse_args()
    run(args.prepared, args.profile)
