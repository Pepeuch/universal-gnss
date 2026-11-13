"""Run immutable historical host tests with only CRC24Q delegated to UG, in /tmp."""
import argparse
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[3]
EMBEDDED = ROOT / 'embedded/esp32'


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--historical-repo', required=True)
    args = parser.parse_args()
    sha = json.loads((EMBEDDED / 'historical-reference.json').read_text())['commit']
    archive = subprocess.check_output([
        'git', '-C', args.historical_repo, 'archive', sha,
        'components/espnow_rtcm', 'components/lora_transport', 'components/rtcm_filter',
        'tests/host', 'scripts/check_rtcm_usb.py', 'scripts/check_rtcm_capture.py'])
    with tempfile.TemporaryDirectory(prefix='ug-espnow-crc-') as directory:
        work = Path(directory)
        with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
            stream.extractall(work, filter='data')
        source = work / 'components/rtcm_filter/src/rtcm_filter.c'
        text = source.read_text()
        start = text.index('uint32_t rtcm3_crc24q(')
        end = text.index('void rtcm3_stream_parser_init(', start)
        source.write_text('#include "ug_rtcm.h"\n' + text[:start] + '''
uint32_t rtcm3_crc24q(const uint8_t *data, size_t len)
{
    uint32_t crc = 0;
    (void)ug_rtcm_crc24q(data, len, &crc);
    return crc;
}

''' + text[end:])
        flags = ['-g', '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror']
        includes = ['-I' + str(EMBEDDED / 'components/ug_rtcm/include'),
                    '-I' + str(ROOT / 'protocols/gnss_protocols/include')]
        objects = []
        for index, cpp in enumerate((EMBEDDED / 'components/ug_rtcm/src/rtcm.cpp',
                                     ROOT / 'protocols/gnss_protocols/src/rtcm_crc24q.cpp')):
            obj = work / f'ug{index}.o'
            run('c++', '-std=c++17', *flags, *includes, '-c', str(cpp), '-o', str(obj))
            objects.append(str(obj))
        includes += ['-I' + str(work / path) for path in (
            'tests/host/stubs', 'components/espnow_rtcm/include',
            'components/lora_transport/include', 'components/rtcm_filter/include')]
        sources = [str(work / path) for path in (
            'components/espnow_rtcm/src/espnow_rtcm_codec.c',
            'components/lora_transport/src/lora_transport.c',
            'components/rtcm_filter/src/rtcm_filter.c',
            'components/rtcm_filter/src/rtcm_profiles.c')]
        for test in ('test_espnow_rtcm', 'bridge_pipe'):
            run('cc', '-std=c11', *flags, *includes, *sources,
                str(work / f'tests/host/{test}.c'), *objects, '-lstdc++', '-o', str(work / test))
        run(str(work / 'test_espnow_rtcm'))
        env = dict(os.environ, ESPNOW_BRIDGE_TEST_EXE=str(work / 'bridge_pipe'),
                   PYTHONDONTWRITEBYTECODE='1')
        run('python3', '-m', 'unittest', 'discover', '-s', str(work / 'tests/host'),
            '-p', 'test_rtcm_usb.py', '-v', env=env)


if __name__ == '__main__':
    main()
