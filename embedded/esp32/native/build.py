"""Build native UG outside source roots; dependency downloads require --provision. Never flash."""
import argparse
import json
import yaml
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'embedded/esp32/integration'))
from build import merged_defaults, C3_PARTITIONS


def network_sdk_defaults(profile, board):
    if profile == 'rover':
        return ''
    if profile not in ('gateway', 'relay-ntrip') or board not in ('waveshare-s3-eth', 'supermini-c3'):
        raise ValueError('unsupported native network profile/board')
    return f'CONFIG_LWIP_MAX_SOCKETS={20 if board == "waveshare-s3-eth" else 10}\n'


def validate_rover_output(profile, board, tx, baud):
    if tx is None:
        if baud != 115200:
            raise ValueError('UART baud without an explicit rover TX pin')
        return
    if profile != 'rover' or board != 'supermini-c3' or tx not in (4, 5, 6, 7, 10):
        raise ValueError('TX-only UART currently requires a reviewed C3 rover GPIO: 4,5,6,7,10')
    if baud not in (115200, 230400, 460800, 921600):
        raise ValueError('unsupported rover UART baud')


def validate_dependency_lock(lock, target):
    expected = json.loads((ROOT / 'embedded/esp32/migration/dependencies.json').read_text())['managed']
    if (not isinstance(lock, dict) or not isinstance(lock.get('dependencies'), dict) or
            lock.get('target') != target or set(lock['dependencies']) != set(expected)):
        raise ValueError('dependency cache target or inventory mismatch')
    for name, pin in expected.items():
        actual = lock['dependencies'][name]
        if (actual.get('version') != pin['version'] or
                actual.get('component_hash') != pin['component_hash']):
            raise ValueError('dependency version/hash mismatch: ' + name)
        source = actual.get('source', {})
        if (source.get('type') != pin['source']['type'] or
                source.get('registry_url', '').rstrip('/') != pin['source'].get('registry_url', '').rstrip('/')):
            raise ValueError('dependency source mismatch: ' + name)


def copy_main(source, destination, network):
    shutil.copytree(source, destination)
    if network:
        # Archives may be deliberately read-only. Only this new generated copy
        # needs a writable directory and a replacement network manifest.
        destination.chmod(destination.stat().st_mode | stat.S_IWUSR)
        manifest = destination / 'idf_component.yml'
        manifest.unlink(missing_ok=True)
        shutil.copyfile(destination / 'idf_component.network.yml', manifest)


def firmware_identity(profile, board, transport, source_sha=None, source_tree=None, ug_version=None, source_modified=False):
    modified = source_modified
    if source_sha is None:
        source_sha = subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
        source_tree = subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD^{tree}'],text=True).strip()
        modified = subprocess.run(['git','-C',str(ROOT),'diff','--quiet','HEAD']).returncode != 0
    if ug_version is None:
        version_result = subprocess.run(['git','-C',str(ROOT),'describe','--tags','--match','v[0-9]*','--abbrev=0'],capture_output=True,text=True)
        ug_version = version_result.stdout.strip() if version_result.returncode==0 else 'unversioned'
    if (not re.fullmatch('[a-f0-9]{40}', source_sha or '') or
            not re.fullmatch('[a-f0-9]{40}', source_tree or '') or
            not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?|unversioned',ug_version or '')):
        raise ValueError('exact source SHA/tree and versioned UG reference required')
    return dict(ug_version=ug_version,source_sha=source_sha,source_tree=source_tree,
                hardware_variant=board,profile=profile,web_transport=transport if profile!='rover' else 'none',
                esp_idf='6.1',modified=modified)


def build(output, profile, board, gnss, provision=False, dependency_cache=None, peer_mac=None, channel=None,
          rover_uart_tx=None, rover_uart_baud=115200, prepare_only=False, web_transport='https',
          source_sha=None, source_tree=None, ug_version=None, source_modified=False):
    if web_transport not in ('http', 'https') or (web_transport == 'http' and
            (profile != 'gateway' or board != 'waveshare-s3-eth')):
        raise ValueError('HTTP experiment requires the S3 gateway; HTTPS remains the default')
    validate_rover_output(profile, board, rover_uart_tx, rover_uart_baud)
    if peer_mac is not None or channel is not None:
        raise ValueError('peer and channel are runtime settings, never firmware variants')
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
    identity = firmware_identity(profile, board, web_transport, source_sha, source_tree, ug_version, source_modified)
    (project/'firmware-identity.json').write_text(json.dumps(identity,sort_keys=True,indent=2)+'\n')
    definitions = {'UG_FIRMWARE_VERSION':identity['ug_version'], 'UG_SOURCE_SHA':identity['source_sha'],
                   'UG_HARDWARE_VARIANT':identity['hardware_variant'], 'UG_WEB_TRANSPORT':identity['web_transport']}
    cmake = ''.join('add_compile_definitions('+key+'=\\\"'+value+'\\\")\n' for key,value in definitions.items())
    cmake += 'add_compile_definitions(UG_SOURCE_MODIFIED='+str(int(identity['modified']))+')\n'
    cmake += 'set(PROJECT_VER \"'+identity['ug_version']+'+'+identity['source_sha'][:8]+'\")\n'
    (project/'firmware-identity.cmake').write_text(cmake)
    source = ROOT / 'embedded/esp32/native'
    shutil.copyfile(source / 'CMakeLists.txt', project / 'CMakeLists.txt')
    rover = profile == 'rover'
    s3 = board == 'waveshare-s3-eth'
    copy_main(source / 'main', project / 'main', not rover)
    if not rover:
        if dependency_cache:
            cache = Path(dependency_cache).resolve()
            lock = yaml.safe_load((cache / 'dependencies.lock').read_text())
            validate_dependency_lock(lock, 'esp32s3' if s3 else 'esp32c3')
            shutil.copytree(cache / 'managed_components', project / 'managed_components')
            shutil.copyfile(cache / 'dependencies.lock', project / 'dependencies.lock')
    defaults = ['''CONFIG_APP_REPRODUCIBLE_BUILD=y
CONFIG_COMPILER_HIDE_PATHS_MACROS=y
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
''', 'CONFIG_ESPNOW_RTCM_ROVER=y' if rover else 'CONFIG_ESPNOW_RTCM_BASE=y',
        # Network boot decodes both persistent banks through nested Settings
        # snapshots. The former 8 KiB main stack overflowed on the physical S3.
        # IDF releases this boot-only stack when app_main returns.
        f'CONFIG_ESP_MAIN_TASK_STACK_SIZE={8192 if rover else 32768}\n']
    if s3:
        defaults += ['CONFIG_UG_WEB_HTTP=y\n' if web_transport == 'http' else '# CONFIG_UG_WEB_HTTP is not set\n']
        # Dynamic S3 owners must recover from nested C++ allocation failures;
        # with exceptions disabled, libstdc++ aborts instead. Keep a bounded
        # emergency reserve and qualify it under full heap exhaustion in QEMU.
        defaults += ['''CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_ESPPSRAM64=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024
CONFIG_COMPILER_CXX_EXCEPTIONS=y
CONFIG_COMPILER_CXX_EXCEPTIONS_EMG_POOL_SIZE=2048
CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y
CONFIG_UART_ISR_IN_IRAM=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_HEAP_TRACING_STANDALONE=y
CONFIG_HEAP_TRACING_STACK_DEPTH=4
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64=y
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
               '-D', 'SDKCONFIG_DEFAULTS=' + str(work / 'sdkconfig.defaults')]
    command += ['reconfigure'] if prepare_only else ['build', 'size']
    env = dict(os.environ, IDF_COMPONENT_MANAGER='0' if rover else '1',
               IDF_COMPONENT_CHECK_NEW_VERSION='0')
    if not rover:
        defaults_path = work / 'sdkconfig.defaults'
        defaults_path.write_text(defaults_path.read_text() +
                                 network_sdk_defaults(profile, board))
        defaults_path.write_text(defaults_path.read_text() + 'CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y\n' + ('CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y\n' if s3 else 'CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y\n') + 'CONFIG_ESP_HTTPS_SERVER_ENABLE=y\nCONFIG_MBEDTLS_PEM_WRITE_C=y\n# CONFIG_MDNS_ENABLE_CONSOLE_CLI is not set\nCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\n')
        if s3 and web_transport == 'https':
            defaults_path.write_text(defaults_path.read_text() +
                                    'CONFIG_MBEDTLS_X509_CREATE_C=y\nCONFIG_MBEDTLS_X509_CRT_WRITE_C=y\n')
    subprocess.run(command, env=env, check=True)
    if not rover:
        validate_dependency_lock(yaml.safe_load((project / 'dependencies.lock').read_text()),
                                 'esp32s3' if s3 else 'esp32c3')
    print('Native prepare only;' if prepare_only else 'Native build only;',
          'hardware qualification pending:', profile, board)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', required=True)
    p.add_argument('--profile', required=True, choices=('gateway', 'relay-ntrip', 'rover'))
    p.add_argument('--board', required=True, choices=('waveshare-s3-eth', 'supermini-c3'))
    p.add_argument('--local-gnss', action='store_true')
    p.add_argument('--web-transport', choices=('https', 'http'), default='https',
                   help='explicit S3 LAN HTTP experiment; never a TLS fallback')
    p.add_argument('--peer-mac', help='obsolete; rejected: provision the peer locally at runtime')
    p.add_argument('--channel', type=int, help='obsolete override; channels are provisioned at runtime')
    p.add_argument('--rover-uart-tx', type=int, help='explicit reviewed C3 TX-only GPIO; omitted keeps USB')
    p.add_argument('--rover-uart-baud', type=int, default=115200, help='UART 8N1 baud (only with explicit TX)')
    p.add_argument('--provision', action='store_true', help='explicitly allow IDF to download pinned dependencies')
    p.add_argument('--prepare-only', action='store_true', help='configure/download only; do not compile firmware')
    p.add_argument('--dependency-cache', help='prepared project containing same-target lock and managed_components')
    p.add_argument('--source-modified', action='store_true')
    p.add_argument('--source-sha')
    p.add_argument('--source-tree')
    p.add_argument('--ug-version')
    a = p.parse_args()
    build(a.output, a.profile, a.board, a.local_gnss, a.provision, a.dependency_cache, a.peer_mac, a.channel,
          a.rover_uart_tx, a.rover_uart_baud, a.prepare_only, a.web_transport,
          a.source_sha, a.source_tree, a.ug_version, a.source_modified)
