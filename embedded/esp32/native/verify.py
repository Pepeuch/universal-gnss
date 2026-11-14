"""Inspect native linked objects/roles and budgets. Never opens devices or flashes."""
import argparse
import hashlib
import gzip
import json
from pathlib import Path
import shlex
import subprocess


def project_standards(commands, source_root, main_root):
    source_root, main_root = Path(source_root).resolve(), Path(main_root).resolve()
    roots = [source_root / path for path in ('core', 'protocols', 'embedded/esp32/components')]
    found = {'c': set(), 'cxx': set()}
    for entry in commands:
        source = Path(entry['file']).resolve()
        if not (source.parent == main_root or any(source.is_relative_to(root) for root in roots)):
            continue
        language = 'c' if source.suffix == '.c' else 'cxx' if source.suffix in ('.cpp', '.cc', '.cxx') else None
        if language is None:
            continue
        flags = [arg.removeprefix('-std=') for arg in shlex.split(entry['command']) if arg.startswith('-std=')]
        allowed = ('gnu17', 'c17') if language == 'c' else ('gnu++17', 'c++17')
        if not flags or flags[-1] not in allowed:
            raise ValueError('project language contract is not C17/C++17: ' + str(source))
        found[language].add(flags[-1])
    return {key: sorted(values) for key, values in found.items()}


def check_web_assets(build, source_assets, application):
    reports = []
    for name in ('index.html', 'app.js', 'style.css'):
        packed = (Path(build) / 'esp-idf/ug_web/web_assets' / (name + '.gz')).read_bytes()
        source = Path(source_assets) / name
        if gzip.decompress(packed) != source.read_bytes() or application.count(packed) != 1:
            raise ValueError('stale or missing linked Web asset: ' + name)
        reports.append({'name': name, 'source_bytes': source.stat().st_size, 'gzip_bytes': len(packed)})
    return reports


def sdk_network_capacity(lines):
    values = dict(line.split('=', 1) for line in lines if line.startswith('CONFIG_') and '=' in line)
    names = {'sockets': 'LWIP_MAX_SOCKETS', 'tcp_active': 'LWIP_MAX_ACTIVE_TCP',
             'tcp_listeners': 'LWIP_MAX_LISTENING_TCP', 'udp_pcbs': 'LWIP_MAX_UDP_PCBS'}
    limits = {}
    for name, key in names.items():
        text = values.get('CONFIG_' + key, '')
        if not text.isdecimal() or int(text) <= 0:
            raise ValueError('missing or invalid SDK network limit: ' + key)
        limits[name] = int(text)
    limits['mdns_socket_mode'] = values.get('CONFIG_MDNS_NETWORKING_SOCKET') == 'y'
    return limits


def verify(work, profile, local_gnss=False, rover_uart_tx=None, rover_uart_baud=115200):
    if profile != 'rover' and (rover_uart_tx is not None or rover_uart_baud != 115200):
        raise ValueError('UART output expectation requires the rover profile')
    work = Path(work).resolve()
    build = work / 'build'
    commands = json.loads((build / 'compile_commands.json').read_text())
    source_main = Path(__file__).resolve().parent / 'main'
    if (work / 'project/CMakeLists.txt').read_bytes() != (source_main.parent / 'CMakeLists.txt').read_bytes():
        raise ValueError('stale copied native project CMake input')
    standards = project_standards(commands, Path(__file__).resolve().parents[3], work / 'project/main')
    generated_main = work / 'project/main'
    selected_main = [Path(entry['file']) for entry in commands
                     if Path(entry['file']).parent == generated_main]
    selected_main.append(generated_main / 'CMakeLists.txt')
    for actual in selected_main:
        original = source_main / actual.name
        if not original.is_file() or actual.read_bytes() != original.read_bytes():
            raise ValueError('stale copied main input: ' + str(actual))
    production = [entry['file'] for entry in commands]
    if any('/oldrtkesp/' in file for file in production):
        raise ValueError('historical checkout must never be a native build input')
    crc = [entry for entry in commands if entry['file'].endswith('/protocols/gnss_protocols/src/rtcm_crc24q.cpp')]
    if len(crc) != 1:
        raise ValueError('existing UG CRC must be compiled exactly once')
    command = shlex.split(crc[0]['command'])
    compiler = command[1] if Path(command[0]).name == 'ccache' else command[0]
    nm = compiler.removesuffix('g++') + 'nm'
    elf, = build.glob('*.elf')
    symbols = subprocess.check_output([nm, '-C', '--defined-only', str(elf)], text=True)
    crc_definitions = [line for line in symbols.splitlines()
                       if 'universal_gnss_protocols::ComputeRtcmCrc24Q(' in line]
    if len(crc_definitions) != 1 or 'ug_rtcm_frame_valid' not in symbols:
        raise ValueError('missing/duplicate linked UG CRC/frame validation')
    if 'lora_transport_crc16' not in symbols:
        raise ValueError('radio fragment CRC16 was lost')
    if profile == 'rover':
        forbidden = ('ug_network', 'ug_settings', 'ug_platform', 'ug_web', 'ug_supervisor', 'ug_receiver', '/core/gnss_ntrip/')
        if any(any(token in file for token in forbidden) for file in production):
            raise ValueError('rover unexpectedly embeds gateway/network/admin components')
        lines = (work / 'sdkconfig').read_text().splitlines()
        for value in ('CONFIG_ESP_CONSOLE_NONE=y', 'CONFIG_BOOTLOADER_LOG_LEVEL=0',
                      'CONFIG_LOG_DEFAULT_LEVEL=0', 'CONFIG_LOG_MAXIMUM_LEVEL=0'):
            if value not in lines:
                raise ValueError('binary USB log isolation missing: ' + value)
        main_c, = [entry for entry in commands if entry['file'].endswith('/main/rover.c')]
        expected_tx = str(rover_uart_tx if rover_uart_tx is not None else -1)
        if '-DUG_ROVER_UART_TX=' + expected_tx not in shlex.split(main_c['command']):
            raise ValueError('rover output selection does not match expected build')
        if rover_uart_baud not in (115200, 230400, 460800, 921600) or (
                '-DUG_ROVER_UART_BAUD=' + str(rover_uart_baud)) not in shlex.split(main_c['command']):
            raise ValueError('rover UART baud does not match expected build')
        if rover_uart_tx is None and rover_uart_baud != 115200:
            raise ValueError('UART baud expectation requires an explicit TX pin')
        uart_inputs = [file for file in production if file.endswith('/main/rover_uart.c')]
        if bool(uart_inputs) != (rover_uart_tx is not None):
            raise ValueError('rover UART backend input mismatch')
        if rover_uart_tx is not None and (rover_uart_tx not in (4, 5, 6, 7, 10) or
                                         'ug_rover_uart_start' not in symbols or 'uart_tx_chars' not in symbols):
            raise ValueError('reviewed nonblocking UART rover backend not linked')
        if 'espnow_rtcm_output_frame' not in symbols:
            raise ValueError('bounded rover frame writer not linked')
    else:
        for value in ('universal_gnss_ntrip::NtripClient::Read(',
                      'universal_gnss_transport::TcpClientTransport::Open(', 'ug::embedded::NtripPublisher::Send('):
            if value not in symbols:
                raise ValueError('actual UG network engine not linked: ' + value)
    driver_inputs = [file for file in production if '/core/gnss_driver/src/' in file]
    if local_gnss != bool(driver_inputs):
        raise ValueError('GNSS driver presence does not match local-GNSS capability')
    if local_gnss and 'universal_gnss_driver::ReceiverSession::FeedBytes(' not in symbols:
        raise ValueError('actual receiver/session engine not linked')
    binary = elf.with_suffix('.bin')
    web_assets = []
    if profile != 'rover':
        web_assets = check_web_assets(build, source_main.parent.parent / 'components/ug_web/assets', binary.read_bytes())
    # Partition table binary is the authoritative generated layout, using SDK's parser.
    import os
    import sys
    sys.path.insert(0, str(Path(os.environ['IDF_PATH']) / 'components/partition_table'))
    import gen_esp32part
    partitions = gen_esp32part.PartitionTable.from_binary((build / 'partition_table/partition-table.bin').read_bytes())
    slots = [part for part in partitions if part.type == 0]
    if not slots or any(binary.stat().st_size > part.size for part in slots):
        raise ValueError('application does not fit every app slot')
    result = {'profile': profile, 'local_gnss': local_gnss, 'app_bytes': binary.stat().st_size,
              'app_slots': [{'label': p.name, 'offset': p.offset, 'size': p.size} for p in slots],
              'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'ug_crc_definitions': crc_definitions,
              'driver_source_count': len(driver_inputs), 'compile_units': len(commands),
              'project_language_standards': standards, 'hardware': 'NOT_TESTED', 'rover_ROM_bytes': 'not qualified' if profile == 'rover' else 'not applicable'}
    result['web_assets'] = web_assets
    if profile != 'rover':
        result['network_capacity'] = sdk_network_capacity((work / 'sdkconfig').read_text().splitlines())
    if profile == 'rover':
        result['rover_output'] = {'transport': 'uart1-tx' if rover_uart_tx is not None else 'usb-serial-jtag',
                                  'tx_pin': rover_uart_tx,
                                  'baud': rover_uart_baud if rover_uart_tx is not None else None,
                                  'physical_delivery': 'NOT_TESTED'}
    (work / 'native-verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', required=True)
    parser.add_argument('--profile', required=True, choices=('gateway','relay-ntrip','rover'))
    parser.add_argument('--local-gnss', action='store_true')
    parser.add_argument('--rover-uart-tx', type=int)
    parser.add_argument('--rover-uart-baud', type=int, default=115200)
    args = parser.parse_args()
    verify(args.work,args.profile,args.local_gnss,args.rover_uart_tx,args.rover_uart_baud)
