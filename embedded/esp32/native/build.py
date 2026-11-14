"""Build native UG outside source roots; dependency downloads require --provision. Never flash."""
import argparse
import json
import yaml
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'embedded/esp32/integration'))
from build import merged_defaults, C3_PARTITIONS


def validate_rover_output(profile, board, tx, baud):
    if tx is None:
        if baud != 115200:
            raise ValueError('UART baud without an explicit rover TX pin')
        return
    if profile != 'rover' or board != 'supermini-c3' or tx not in (4, 5, 6, 7, 10):
        raise ValueError('TX-only UART currently requires a reviewed C3 rover GPIO: 4,5,6,7,10')
    if baud not in (115200, 230400, 460800, 921600):
        raise ValueError('unsupported rover UART baud')


def build(output, profile, board, gnss, provision=False, dependency_cache=None, peer_mac=None, channel=1,
          rover_uart_tx=None, rover_uart_baud=115200):
    validate_rover_output(profile, board, rover_uart_tx, rover_uart_baud)
    if peer_mac is not None:
        if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', peer_mac):
            raise ValueError('peer must be an explicit unicast STA MAC')
        octets = bytes.fromhex(peer_mac.replace(':',''))
        if octets[0] & 1 or not any(octets):
            raise ValueError('peer must be a nonzero unicast MAC')
    if not 1 <= channel <= 11:
        raise ValueError('fixed ESP-NOW channel must be between 1 and 11')
    work = Path(output).resolve()
    if work.exists() or ROOT == work or ROOT in work.parents:
        raise ValueError('build output must be new and outside source roots')
    if gnss and (profile != 'gateway' or board != 'waveshare-s3-eth'):
        raise ValueError('local GNSS currently requires the S3 gateway board')
    version = subprocess.check_output(['idf.py', '--version'], text=True).strip()
    if version != 'ESP-IDF v6.1':
        raise ValueError('native firmware requires ESP-IDF v6.1')
    if profile != 'rover' and not provision and dependency_cache is None:
        raise ValueError('network roles need --provision or a verified --dependency-cache project')
    work.mkdir(parents=True)
    project = work / 'project'
    project.mkdir()
    source = ROOT / 'embedded/esp32/native'
    shutil.copyfile(source / 'CMakeLists.txt', project / 'CMakeLists.txt')
    shutil.copytree(source / 'main', project / 'main')
    rover = profile == 'rover'
    s3 = board == 'waveshare-s3-eth'
    if not rover:
        shutil.copyfile(project / 'main/idf_component.network.yml', project / 'main/idf_component.yml')
        if dependency_cache:
            cache = Path(dependency_cache).resolve()
            lock = yaml.safe_load((cache / 'dependencies.lock').read_text())
            if lock['target'] != ('esp32s3' if s3 else 'esp32c3'):
                raise ValueError('dependency cache target mismatch')
            for name, version in {'espressif/w5500':'2.0.1', 'espressif/wiznet_common':'1.1.0',
                                  'espressif/mdns':'1.15.1', 'espressif/cjson':'1.7.19~2', 'idf':'6.1.0'}.items():
                if lock['dependencies'][name]['version'] != version:
                    raise ValueError('dependency version mismatch: ' + name)
            shutil.copytree(cache / 'managed_components', project / 'managed_components')
            shutil.copyfile(cache / 'dependencies.lock', project / 'dependencies.lock')
    defaults = ['''CONFIG_APP_REPRODUCIBLE_BUILD=y
CONFIG_COMPILER_HIDE_PATHS_MACROS=y
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
''', 'CONFIG_ESPNOW_RTCM_ROVER=y' if rover else 'CONFIG_ESPNOW_RTCM_BASE=y',
                f'CONFIG_ESPNOW_RTCM_CHANNEL={channel}',
                'CONFIG_ESPNOW_RTCM_PEER_MAC="' + (peer_mac or ('02:00:00:00:00:01' if rover else '02:00:00:00:00:02')) + '"']
    if s3:
        defaults += ['''CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_ESPPSRAM64=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
''']
        # Existing reference layout, not an authorization to repartition hardware.
        partitions = 'nvs,data,nvs,0x9000,24K,\notadata,data,ota,0xf000,8K,\nphy_init,data,phy,0x11000,4K,\nota_0,app,ota_0,0x20000,3M,\nota_1,app,ota_1,0x320000,3M,\nwww,data,spiffs,0x620000,9M,\ncoredump,data,coredump,0xf20000,256K,\n'
    else:
        defaults += ['CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y']
        partitions = C3_PARTITIONS
    if rover:
        defaults += ['''CONFIG_ESP_CONSOLE_NONE=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
CONFIG_LOG_DEFAULT_LEVEL_NONE=y
CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y
CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT=y
''']
    (project / 'partitions.csv').write_text(partitions)
    (work / 'sdkconfig.defaults').write_text(merged_defaults(defaults))
    output_defines = (['-D', 'UG_ROVER_UART_TX=' + str(rover_uart_tx if rover_uart_tx is not None else -1),
                       '-D', 'UG_ROVER_UART_BAUD=' + str(rover_uart_baud)] if rover else [])
    command = ['idf.py', '-C', str(project), '-B', str(work / 'build'),
               '-D', 'UG_ROOT=' + str(ROOT), '-D', 'UG_PROFILE=' + profile,
               '-D', 'UG_LOCAL_GNSS=' + ('ON' if gnss else 'OFF'),
               *output_defines,
               '-D', 'IDF_TARGET=' + ('esp32s3' if s3 else 'esp32c3'),
               '-D', 'SDKCONFIG=' + str(work / 'sdkconfig'),
               '-D', 'SDKCONFIG_DEFAULTS=' + str(work / 'sdkconfig.defaults'), 'build', 'size']
    env = dict(os.environ, IDF_COMPONENT_MANAGER='0' if rover else '1',
               IDF_COMPONENT_CHECK_NEW_VERSION='0')
    if not rover:
        defaults_path = work / 'sdkconfig.defaults'
        defaults_path.write_text(defaults_path.read_text() + 'CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y\n' + ('CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y\n' if s3 else 'CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y\n') + 'CONFIG_ESP_HTTPS_SERVER_ENABLE=y\nCONFIG_MBEDTLS_PEM_WRITE_C=y\n# CONFIG_MDNS_ENABLE_CONSOLE_CLI is not set\nCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\n')
    subprocess.run(command, env=env, check=True)
    print('Native build only; hardware qualification pending:', profile, board)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', required=True)
    p.add_argument('--profile', required=True, choices=('gateway', 'relay-ntrip', 'rover'))
    p.add_argument('--board', required=True, choices=('waveshare-s3-eth', 'supermini-c3'))
    p.add_argument('--local-gnss', action='store_true')
    p.add_argument('--peer-mac', help='explicit peer Wi-Fi STA MAC; omitted means a fictitious compile-only peer')
    p.add_argument('--channel', type=int, default=1, help='fixed common ESP-NOW channel; no automatic selection')
    p.add_argument('--rover-uart-tx', type=int, help='explicit reviewed C3 TX-only GPIO; omitted keeps USB')
    p.add_argument('--rover-uart-baud', type=int, default=115200, help='UART 8N1 baud (only with explicit TX)')
    p.add_argument('--provision', action='store_true', help='explicitly allow IDF to download pinned dependencies')
    p.add_argument('--dependency-cache', help='prepared project containing same-target lock and managed_components')
    a = p.parse_args()
    build(a.output, a.profile, a.board, a.local_gnss, a.provision, a.dependency_cache, a.peer_mac, a.channel,
          a.rover_uart_tx, a.rover_uart_baud)
