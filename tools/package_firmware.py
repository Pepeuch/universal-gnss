#!/usr/bin/env python3
"""Prepare a *local* static ESP Web Tools bundle from an already verified IDF build.

This tool does not compile, publish, upload or flash anything. A trusted CI job
must validate these inputs and authorize any deployment separately.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

VARIANTS = {
    's3-gateway': ('ESP32-S3', 'waveshare-s3-eth', 'gateway'),
    'c3-rover-usb': ('ESP32-C3', 'supermini-c3', 'rover'),
    'c3-rover-uart': ('ESP32-C3', 'supermini-c3', 'rover'),
}

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True, help='existing IDF build folder')
    p.add_argument('--verified', type=Path, required=True, help='native-verification.json from verified build')
    p.add_argument('--profile-json', type=Path, required=True, help='normalized profile exported by UI')
    p.add_argument('--source-sha', required=True, help='trusted 40-character source Git SHA')
    p.add_argument('--output', type=Path, required=True, help='new output folder, e.g. docs/flasher/')
    args = p.parse_args(argv)
    if not re.fullmatch(r'[a-fA-F0-9]{40}', args.source_sha):
        p.error('source-sha must be a full Git SHA')
    profile = json.loads(args.profile_json.read_text())
    verified = json.loads(args.verified.read_text())
    if profile.get('schema') != 1 or profile.get('variant') not in VARIANTS:
        p.error('unsupported or outdated profile')
    chip, board, role = VARIANTS[profile['variant']]
    if (profile.get('chip'), profile.get('board'), profile.get('profile')) != (chip, board, role):
        p.error('invalid board/chip/role selection')
    if verified.get('profile') != role:
        p.error('verified native role does not match profile')
    if role == 'gateway' and not verified.get('local_gnss'):
        p.error('S3 gate must have compiled local GNSS')
    if role == 'rover' and verified.get('local_gnss'):
        p.error('rover must exclude GNSS')
    if profile['variant'].endswith('usb') and profile.get('output') != 'usb':
        p.error('wrong USB output')
    if profile['variant'].endswith('uart') and profile.get('output') != 'uart':
        p.error('wrong UART output')
    mac = profile.get('peer_mac', '')
    if not isinstance(mac, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', mac):
        p.error('invalid ESP-NOW peer MAC')
    raw_mac = bytes.fromhex(mac.replace(':', ''))
    if raw_mac[0] & 1 or not any(raw_mac):
        p.error('not a unicast peer MAC')
    if not isinstance(profile.get('channel'), int) or not 1 <= profile['channel'] <= 11:
        p.error('invalid ESP-NOW channel')
    if profile['variant'].endswith('uart'):
        if profile.get('uart_tx') not in (4, 5, 6, 7, 10) or profile.get('uart_baud') not in (115200, 230400, 460800, 921600):
            p.error('invalid UART selection')
    else:
        if 'uart_tx' in profile or 'uart_baud' in profile:
            p.error('unexpected UART override')
    # Trusted CI must also independently compare actual sdkconfig values for
    # channel, peer, TX pin and baud: a JSON file alone cannot attest the binary.
    build = args.build.resolve(strict=True)
    flash_args = json.loads((build / 'flasher_args.json').read_text())
    files = flash_args.get('flash_files')
    if not isinstance(files, dict) or len(files) < 3:
        p.error('IDF flash_files missing, unsafe to fabricate offsets')
    result = []
    spans = []
    app_path = build / 'universal-gnss-esp32.bin'
    if sha256(app_path) != verified.get('sha256'):
        p.error('app SHA256 differs from native verifier')
    if app_path.stat().st_size != verified.get('app_bytes'):
        p.error('app size differs from native verifier')
    for raw_offset, raw_path in files.items():
        if not re.fullmatch(r'0x[0-9a-fA-F]+|[0-9]+', raw_offset):
            p.error('invalid flash offset')
        offset = int(raw_offset, 0)
        rel = Path(raw_path)
        if rel.is_absolute() or '..' in rel.parts or rel.suffix != '.bin':
            p.error('invalid flash filename')
        source = (build / rel).resolve(strict=True)
        if source != build and build not in source.parents:
            p.error('flash filename escapes IDF build')
        size = source.stat().st_size
        if not size or offset < 0 or offset + size > (16 if chip == 'ESP32-S3' else 4) * 1024 * 1024:
            p.error('flash part outside target flash')
        result.append((offset, source, size))
        spans.append((offset, offset + size))
    spans.sort()
    if any(a[1] > b[0] for a,b in zip(spans,spans[1:])):
        p.error('overlapping flash parts')
    if app_path not in [x[1] for x in result]:
        p.error('verified app is missing from IDF flash arguments')
    # The output catalog path depends only on trusted source SHA and normalized
    # profile. Never allow arbitrary strings to become filesystem paths.
    canonical = json.dumps(profile, sort_keys=True, separators=(',', ':'), ensure_ascii=True)
    key = hashlib.sha256((args.source_sha.lower()+'\n'+canonical).encode()).hexdigest()[:20]
    dest = args.output.resolve() / 'published' / key
    if dest.exists():
        p.error('bundle already exists; refuse silent overwrite')
    dest.mkdir(parents=True)
    parts = []
    sha_files = {}
    for offset, source, _ in sorted(result):
        name = f'{offset:06x}-{source.name}'
        shutil.copyfile(source, dest / name)
        sha_files[name] = sha256(dest / name)
        parts.append({'path':name,'offset':offset})
    manifest = {'name':f'Universal GNSS {profile["variant"]}',
                'version':args.source_sha[:12]+'-'+key,
                'new_install_prompt_erase':True,
                'builds':[{'chipFamily':chip,'parts':parts}]}
    (dest / 'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    entry = {'profile':profile,'manifest':f'flasher/published/{key}/manifest.json',
             'sha256':sha256(app_path),'source_sha':args.source_sha.lower()}
    (dest / 'provenance.json').write_text(json.dumps({**entry,'part_sha256':sha_files},indent=2)+'\n')
    print(json.dumps({'catalog_entry':entry,'provenance':str(dest/'provenance.json')},indent=2))
    return 0

if __name__ == '__main__':
    sys.exit(main())
