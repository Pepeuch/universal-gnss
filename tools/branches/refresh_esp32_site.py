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
    # Include published alpha, beta and stable releases; drafts are never public.
    releases = a.v.api('releases?per_page=100')
    candidates = [r for r in releases if not r['draft'] and
                  (not a.v.RC_TAG.fullmatch(r['tag_name']) or any(x['name']==a.v.MARKER for x in r['assets'])) and
                  any(x['name'] in ('esp32-site.zip','esp32-alpha-site.zip') for x in r['assets'])]
    spec=importlib.util.spec_from_file_location('durable_preview',ROOT/'tools/branches/esp32_preview.py')
    preview=importlib.util.module_from_spec(spec);spec.loader.exec_module(preview)
    ci_release=next((item for item in releases if not item['draft'] and item['tag_name']==preview.TAG),None)
    ci=preview.latest(ci_release) if ci_release else None
    def completion(item):
        marker=next((asset for asset in item['assets'] if asset['name']==a.v.MARKER),{})
        return marker.get('created_at') or item['published_at']
    release=max(candidates,key=completion) if candidates else None
    if ci and (not release or ci[1]['archived_at']>completion(release)):
        return ci[0],preview.TAG
    if not release:
        raise ValueError('no durable firmware catalogue available')
    assets = {x['name']: x for x in release['assets']}
    name='esp32-site.zip' if 'esp32-site.zip' in assets else 'esp32-alpha-site.zip'
    if a.v.RC_TAG.fullmatch(release['tag_name']):
        release = a.v.get_release(a.v.api, release['tag_name'])
        marker = a.v.verify_marker(release)
        target, obj = a.v.tag_identity(a.v.api, release['tag_name'])
        if marker['source_sha']!=target or marker['tag_object_sha']!=obj:
            raise ValueError('completed Release tag identity changed')
        assets = a.v.asset_map(release)
    archive = download(assets[name]['browser_download_url'], 512*1024*1024)
    sums = download(assets[a.v.SUMS if a.v.RC_TAG.fullmatch(release['tag_name']) else 'SHA256SUMS']['browser_download_url'], 65536).decode('ascii')
    checks = dict((name, sha) for sha, name in (line.split() for line in sums.splitlines()))
    if checks.get(name) != hashlib.sha256(archive).hexdigest():
        raise ValueError('durable archive checksum mismatch')
    digest = assets[name].get('digest')
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
        development=meta.get('channel')=='DEV'
        if development:a.s.require_source(repo,meta['source_sha'],meta['source_tree'],'ci_preview')
        identity = a.source(repo, meta['source_sha'],development=development)
        actual_tag=a.remote_tag(repo,tag)
        if development:
            if tag!='esp32-ci-builds' or actual_tag!=meta.get('technical_tag_sha'):raise ValueError('development archive origin mismatch')
            a.s.require_main(repo,actual_tag,a.git(repo,'rev-parse',actual_tag+'^{tree}'))
        if not development and actual_tag!=meta['source_sha']:
            raise ValueError('published release tag/source mismatch')
        if meta['release_tag'] != tag or meta['source_tree'] != identity['source_tree']:
            raise ValueError('durable publication identity mismatch')
        catalog = a.s.p.load(old/'flasher/catalog.json')
        approvals = a.s.p.load(old/'flasher/approved-sources.json')
        if catalog.get('schema') != 2 or not catalog.get('images'):
            raise ValueError('nonempty existing catalogue required')
        bundles = []
        for i, entry in enumerate(catalog['images']):
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
        for name in ('publications.json','branches.json'):
            if (old/'flasher'/name).exists():
                shutil.copyfile(old/'flasher'/name,staged/'flasher'/name)
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
    parser.add_argument('--require-source')
    args = parser.parse_args()
    data, tag = durable_archive()
    refresh(data, tag, args.output)
    if args.require_source and not any(e['source_sha']==args.require_source for e in a.s.p.load(args.output/'flasher/catalog.json')['images']):
        raise ValueError('authorized source missing from durable recovery catalogue')
    print('Verified durable catalogue preserved; interface only refreshed from main.')


if __name__ == '__main__':
    main()
