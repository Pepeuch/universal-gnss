"""Build a native UG firmware in a new external directory. Never fetch or flash."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'embedded/esp32/integration'))
from build import merged_defaults, C3_PARTITIONS


def build(output, profile, board, gnss):
    work = Path(output).resolve()
    if work.exists() or ROOT == work or ROOT in work.parents:
        raise ValueError('build output must be new and outside source roots')
    if gnss and (profile != 'gateway' or board != 'waveshare-s3-eth'):
        raise ValueError('local GNSS currently requires the S3 gateway board')
    version = subprocess.check_output(['idf.py', '--version'], text=True).strip()
    if version != 'ESP-IDF v6.1':
        raise ValueError('native firmware requires ESP-IDF v6.1')
    work.mkdir(parents=True)
    project = work / 'project'
    project.mkdir()
    source = ROOT / 'embedded/esp32/native'
    shutil.copyfile(source / 'CMakeLists.txt', project / 'CMakeLists.txt')
    shutil.copytree(source / 'main', project / 'main')
    rover = profile == 'rover'
    s3 = board == 'waveshare-s3-eth'
    defaults = ['''CONFIG_APP_REPRODUCIBLE_BUILD=y
CONFIG_COMPILER_HIDE_PATHS_MACROS=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPNOW_RTCM_CHANNEL=1
''', 'CONFIG_ESPNOW_RTCM_ROVER=y' if rover else 'CONFIG_ESPNOW_RTCM_BASE=y',
                'CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:01"' if rover else
                'CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:02"']
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
    command = ['idf.py', '-C', str(project), '-B', str(work / 'build'),
               '-D', 'UG_ROOT=' + str(ROOT), '-D', 'UG_PROFILE=' + profile,
               '-D', 'UG_LOCAL_GNSS=' + ('ON' if gnss else 'OFF'),
               '-D', 'IDF_TARGET=' + ('esp32s3' if s3 else 'esp32c3'),
               '-D', 'SDKCONFIG=' + str(work / 'sdkconfig'),
               '-D', 'SDKCONFIG_DEFAULTS=' + str(work / 'sdkconfig.defaults'), 'build', 'size']
    env = dict(os.environ, IDF_COMPONENT_MANAGER='0') # Core/radio require no downloads.
    subprocess.run(command, env=env, check=True)
    print('Native build only; hardware qualification pending:', profile, board)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', required=True)
    p.add_argument('--profile', required=True, choices=('gateway', 'relay-ntrip', 'rover'))
    p.add_argument('--board', required=True, choices=('waveshare-s3-eth', 'supermini-c3'))
    p.add_argument('--local-gnss', action='store_true')
    a = p.parse_args()
    build(a.output, a.profile, a.board, a.local_gnss)
