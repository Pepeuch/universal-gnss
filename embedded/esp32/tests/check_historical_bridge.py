"""Run immutable historical host tests with the application overlay, in /tmp."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
EMBEDDED = ROOT / 'embedded/esp32'
sys.path.insert(0, str(EMBEDDED / 'integration'))
from prepare import adapt, extract, REFERENCE


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--historical-repo', required=True)
    parser.add_argument('--native-components', action='store_true',
                        help='test the migrated UG radio/filter sources against pinned historical tests')
    args = parser.parse_args()
    sha = json.loads(REFERENCE.read_text())['commit']
    with tempfile.TemporaryDirectory(prefix='ug-espnow-crc-') as directory:
        work = Path(directory)
        extract(args.historical_repo, sha, work, (
            'components/espnow_rtcm', 'components/lora_transport', 'components/rtcm_filter',
            'tests/host', 'scripts/check_rtcm_usb.py', 'scripts/check_rtcm_capture.py',
            'CMakeLists.txt', 'firmware/espnow_rover/CMakeLists.txt'))
        adapt(work)
        flags = ['-g', '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror']
        includes = ['-I' + str(EMBEDDED / 'components/ug_rtcm/include'),
                    '-I' + str(ROOT / 'protocols/gnss_protocols/include')]
        objects = []
        for index, cpp in enumerate((EMBEDDED / 'components/ug_rtcm/src/rtcm.cpp',
                                     ROOT / 'protocols/gnss_protocols/src/rtcm_crc24q.cpp')):
            obj = work / f'ug{index}.o'
            run('c++', '-std=c++17', *flags, *includes, '-c', str(cpp), '-o', str(obj))
            objects.append(str(obj))
        component_root = EMBEDDED if args.native_components else work
        includes += ['-I' + str(work / 'tests/host/stubs')]
        includes += ['-I' + str(component_root / path) for path in ( 'components/espnow_rtcm/include',
            'components/lora_transport/include', 'components/rtcm_filter/include')]
        sources = [str(component_root / path) for path in (
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
