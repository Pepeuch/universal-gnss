"""Inspect native linked objects/roles and budgets. Never opens devices or flashes."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def verify(work, profile, local_gnss=False):
    work = Path(work).resolve()
    build = work / 'build'
    commands = json.loads((build / 'compile_commands.json').read_text())
    source_main = Path(__file__).resolve().parent / 'main'
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
              'hardware': 'NOT_TESTED', 'rover_ROM_bytes': 'not qualified' if profile == 'rover' else 'not applicable'}
    (work / 'native-verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', required=True)
    parser.add_argument('--profile', required=True, choices=('gateway','relay-ntrip','rover'))
    parser.add_argument('--local-gnss', action='store_true')
    args = parser.parse_args()
    verify(args.work,args.profile,args.local_gnss)
