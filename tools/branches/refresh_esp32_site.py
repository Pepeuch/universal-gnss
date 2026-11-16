#!/usr/bin/env python3
"""Refresh only Web Flasher assets from a verified durable alpha Release."""
import argparse
import hashlib
import importlib.util
import io
import json
import re
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('alpha_refresh', ROOT/'tools/branches/esp32_alpha.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def download(url, limit):
    if not url.startswith('https://github.com/Pepeuch/universal-gnss/releases/download/'):
        raise ValueError('untrusted durable asset URL')
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('release asset exceeds limit')
    return data


def durable_archive():
    # Do not use /latest: alpha prereleases are intentionally not latest releases.
    with urllib.request.urlopen('https://api.github.com/repos/'+a.REPO+'/releases?per_page=100', timeout=30) as response:
        releases = json.load(response)
    candidates = [r for r in releases if not r['draft'] and r['prerelease'] and
                  any(x['name'] == 'esp32-alpha-site.zip' for x in r['assets'])]
    if not candidates:
        raise ValueError('no durable alpha catalogue available')
    release = max(candidates, key=lambda r: r['published_at'])
    assets = {x['name']: x for x in release['assets']}
    archive = download(assets['esp32-alpha-site.zip']['browser_download_url'], 512*1024*1024)
    sums = download(assets['SHA256SUMS']['browser_download_url'], 65536).decode('ascii')
    checks = dict((name, sha) for sha, name in (line.split() for line in sums.splitlines()))
    if checks.get('esp32-alpha-site.zip') != hashlib.sha256(archive).hexdigest():
        raise ValueError('durable archive checksum mismatch')
    digest = assets['esp32-alpha-site.zip'].get('digest')
    if digest and digest != 'sha256:'+hashlib.sha256(archive).hexdigest():
        raise ValueError('GitHub asset digest mismatch')
    return archive, release['tag_name']


def unpack(data, root):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = set()
        total = 0
        for item in archive.infolist():
            name = item.filename
            path = Path(name)
            total += item.file_size
            if (name in names or path.is_absolute() or '..' in path.parts or '\\' in name or
                    item.is_dir() or (item.external_attr >> 16) & 0o170000 == 0o120000 or
                    total > 512*1024*1024):
                raise ValueError('unsafe archive member')
            names.add(name)
            dest = root/path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(archive.read(item))


cache_bust = a.s.cache_bust


def refresh(data, tag, output, repo=ROOT):
    output = Path(output).resolve()
    if output.exists() or output.is_relative_to(Path(repo).resolve()):
        raise ValueError('new output outside source required')
    with tempfile.TemporaryDirectory(prefix='ug-pages-refresh-') as temp:
        root = Path(temp); old = root/'old'; old.mkdir()
        unpack(data, old)
        meta = a.s.p.load(old/'alpha-publication.json')
        identity = a.source(repo, meta['source_sha'], tag)
        if meta['release_tag'] != tag or meta['source_tree'] != identity['source_tree']:
            raise ValueError('durable publication identity mismatch')
        catalog = a.s.p.load(old/'flasher/catalog.json')
        approvals = a.s.p.load(old/'flasher/approved-sources.json')
        if catalog.get('schema') != 2 or not catalog.get('images'):
            raise ValueError('nonempty existing catalogue required')
        bundles = []
        for i, entry in enumerate(catalog['images']):
            if entry['source_sha'] != meta['source_sha']:
                raise ValueError('unexpected unapproved firmware source')
            key = entry['build_key']
            if not re.fullmatch('[a-f0-9]{20}', key):
                raise ValueError('unsafe build identity')
            bundle = root/str(i); bundle.mkdir()
            dest = bundle/'published'/key
            dest.parent.mkdir()
            shutil.copytree(old/'flasher/published'/key, dest)
            (bundle/'catalog-entry.json').write_text(json.dumps(entry))
            checked, directory, _ = a.s.inspect_bundle(bundle, approvals)
            a.require_s3_memory(checked, directory)
            bundles.append(bundle)
        # Existing approval/qualification/main-ancestry checks remain authoritative.
        staged = root/'staged'
        a.s.prepare(staged, approvals, bundles, repo)
        # Preserve exact authoritative catalogue and every published firmware byte.
        for path in ('flasher/catalog.json', 'flasher/approved-sources.json', 'alpha-publication.json'):
            shutil.copyfile(old/path, staged/path)
        if (old/'index.html').exists():
            shutil.copyfile(old/'index.html', staged/'index.html')
        old_files = {p.relative_to(old/'flasher/published'): p.read_bytes()
                     for p in (old/'flasher/published').rglob('*') if p.is_file()}
        new_files = {p.relative_to(staged/'flasher/published'): p.read_bytes()
                     for p in (staged/'flasher/published').rglob('*') if p.is_file()}
        if old_files != new_files:
            raise ValueError('firmware payload changed')
        cache_bust(staged)
        shutil.copytree(staged, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data, tag = durable_archive()
    refresh(data, tag, args.output)
    print('Verified durable catalogue preserved; interface only refreshed from main.')


if __name__ == '__main__':
    main()
