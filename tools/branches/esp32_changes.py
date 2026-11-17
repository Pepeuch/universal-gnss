#!/usr/bin/env python3
"""Select native firmware builds: stable pushes always, work branches by inputs."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def affects_firmware(name):
    path = Path(name)
    if name == '.github/workflows/esp32.yml':
        return True
    if not name.startswith(('embedded/esp32/', 'core/', 'protocols/', 'docs/flasher/', 'tools/branches/')):
        return name in ('docs/flasher.html', 'tools/package_firmware.py', '.gitmodules', 'CMakeLists.txt')
    if any(part in ('tests','fixtures') for part in path.parts) and path.suffix.lower() not in ('.md','.rst','.png','.svg','.jpg','.webp'):
        return True
    if path.name in ('CMakeLists.txt', 'Makefile', 'Kconfig', 'Kconfig.projbuild'):
        return True
    return (path.suffix.lower() in ('.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.hxx',
        '.cmake', '.py', '.sh', '.js', '.html', '.css', '.json', '.yaml', '.yml', '.csv',
        '.bin', '.pem', '.key', '.crt', '.txt', '.lock', '.patch', '.diff', '.in') and path.name not in ('README.txt',)) or path.name.startswith('sdkconfig')


def required(repo, event, ref, sha):
    if ref in ('refs/heads/main', 'refs/heads/dev') or ref.startswith('refs/tags/'):
        return True
    if not re.fullmatch('[a-f0-9]{40}', sha):
        raise ValueError('exact pushed/merge SHA required')
    base = event.get('pull_request', {}).get('base', {}).get('sha') or event.get('before')
    if base == '0' * 40 or not base:
        common=subprocess.run(['git','-C',str(repo),'merge-base','origin/dev',sha],capture_output=True,text=True)
        if common.returncode:
            names=subprocess.check_output(['git','-C',str(repo),'ls-tree','-r','--name-only','-z',sha]).decode().split('\0')
            return any(affects_firmware(name) for name in names if name)
        base=common.stdout.strip()
    if not re.fullmatch('[a-f0-9]{40}', base):
        raise ValueError('exact diff base required')
    names = subprocess.check_output(['git', '-C', str(repo), 'diff', '--no-renames', '--name-only', '-z', base, sha]).decode().split('\0')
    return any(affects_firmware(name) for name in names if name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--event', type=Path, required=True)
    parser.add_argument('--ref', required=True)
    parser.add_argument('--sha', required=True)
    args = parser.parse_args()
    decision = required(ROOT, json.loads(args.event.read_bytes()), args.ref, args.sha)
    print('Native firmware build required: ' + str(decision).lower())
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write('build_required=' + str(decision).lower() + '\n')


if __name__ == '__main__':
    main()
