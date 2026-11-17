#!/usr/bin/env python3
"""Versioned RC support for the existing ESP32 publication chain.

Trusted maintainer Releases and append-only assets. No tag/release creation,
retarget, asset deletion/replacement or release-description edit is supported.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request

REPO = 'Pepeuch/universal-gnss'
RC_TAG = re.compile(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-RC[1-9][0-9]*')
MARKER = 'esp32-publication.json'
ARCHIVE = 'esp32-site.zip'
SUMS = 'esp32-SHA256SUMS'
VARIANTS = ('s3-gateway', 'c3-rover-usb')
BASE_ASSETS = {ARCHIVE, 'esp32-firmware-source.tar.gz', 'esp32-catalog.json',
               'esp32-approved-sources.json', SUMS}
for variant in VARIANTS:
    BASE_ASSETS.update('esp32-' + variant + suffix for suffix in
                       ('.zip', '-manifest.json', '-provenance.json', '-qualification.json'))
PART = re.compile(r'esp32-(s3-gateway|c3-rover-usb)-[a-f0-9]{6}-'
                  r'(bootloader|partition-table|ota_data_initial|universal-gnss-esp32)\.bin')


def decode(data):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate publication metadata key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique)


def api(path):
    headers = {'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
    if os.environ.get('GITHUB_TOKEN'):
        headers['Authorization'] = 'Bearer ' + os.environ['GITHUB_TOKEN']
    with urllib.request.urlopen(urllib.request.Request(
            'https://api.github.com/repos/' + REPO + '/' + path, headers=headers), timeout=30) as response:
        return json.load(response)


def pages(api_call, path):
    result = []
    separator = '&' if '?' in path else '?'
    for page in range(1, 11):
        batch = api_call(path + separator + 'per_page=100&page=' + str(page))
        if not isinstance(batch, list):
            raise ValueError('invalid paginated GitHub response')
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise ValueError('GitHub history exceeds bounded query')


def asset_map(release):
    result = {}
    for item in release['assets']:
        name = item['name']
        if name in result:
            raise ValueError('duplicate Release asset name')
        result[name] = item
    return result


def get_release(api_call, tag):
    if not isinstance(tag, str) or not RC_TAG.fullmatch(tag):
        raise ValueError('canonical vX.Y.Z-RCn tag required')
    release = api_call('releases/tags/' + tag)
    if release.get('draft') is not False or release.get('tag_name') != tag:
        raise ValueError('existing published versioned Release required')
    if type(release.get('id')) is not int or release['id'] <= 0:
        raise ValueError('invalid Release identity')
    if not isinstance(release.get('published_at'), str):
        raise ValueError('published Release date required')
    date = datetime.datetime.fromisoformat(release['published_at'].replace('Z', '+00:00'))
    if date.tzinfo is None:
        raise ValueError('Release publication date must include timezone')
    release = dict(release)
    release['assets'] = pages(api_call, 'releases/' + str(release['id']) + '/assets')
    asset_map(release)
    return release


def tag_identity(api_call, tag):
    obj = api_call('git/ref/tags/' + tag)['object']
    original = obj['sha']
    seen = set()
    for _ in range(8):
        if not re.fullmatch('[a-f0-9]{40}', obj.get('sha', '')) or obj['sha'] in seen:
            raise ValueError('invalid/cyclic tag identity')
        seen.add(obj['sha'])
        if obj.get('type') == 'commit':
            return obj['sha'], original
        if obj.get('type') != 'tag':
            break
        obj = api_call('git/tags/' + obj['sha'])['object']
    raise ValueError('tag does not resolve to a bounded commit identity')


def maintainer(api_call, login):
    if not isinstance(login, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,38}', login):
        raise ValueError('trusted maintainer identity required')
    result = api_call('collaborators/' + login + '/permission')
    if result.get('permission') not in ('admin', 'maintain'):
        raise ValueError('publication requires a trusted admin/maintain account')
    return result


def authorize(alpha, api_call, require_ci, repo, sha, tag, *, actor=None,
              release_id=None, tag_object_sha=None):
    release = get_release(api_call, tag)
    target, tag_object = tag_identity(api_call, tag)
    if sha != target:
        raise ValueError('Release/tag/source commit mismatch')
    identity = alpha.source(repo, sha, tag)
    if release_id is not None and release['id'] != int(release_id):
        raise ValueError('Release identity changed')
    if tag_object_sha is not None and tag_object != tag_object_sha:
        raise ValueError('tag object changed; never retag')
    maintainer(api_call, release['author']['login'])
    if actor is not None:
        maintainer(api_call, actor)
    records = alpha.s.r.approval_register(alpha.s.p.load(Path(repo) / 'docs/flasher/approved-sources.json'))
    record = records.get(sha)
    if record and (record['publication'] != 'main_approved' or record['source_tree'] != identity['source_tree']):
        raise ValueError('source approval revoked or tree mismatch')
    # Current maintainer policy: publishing the versioned Release is approval.
    # Rulesets are deliberately deferred; never infer that tags are protected.
    require_ci(sha)
    publisher = alpha.git(repo, 'rev-parse', 'origin/main')
    if publisher != sha:
        require_ci(publisher)
    return {**identity, 'release_id': release['id'], 'tag_object_sha': tag_object,
            'published_at': release['published_at'], 'channel': 'RC',
            'authority': 'maintainer_release'}, release


def download(asset, limit=512 * 1024 * 1024):
    url = asset['browser_download_url']
    if not url.startswith('https://github.com/' + REPO + '/releases/download/'):
        raise ValueError('untrusted durable asset URL')
    if asset.get('state') != 'uploaded' or not 0 < asset.get('size', 0) <= limit:
        raise ValueError('Release asset is incomplete or oversized')
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read(limit + 1)
    if len(data) != asset['size'] or len(data) > limit:
        raise ValueError('Release asset size mismatch')
    digest = asset.get('digest')
    if digest and digest != 'sha256:' + hashlib.sha256(data).hexdigest():
        raise ValueError('GitHub asset digest mismatch')
    return data


def verify_marker(release, identity=None):
    assets = asset_map(release)
    marker = decode(download(assets[MARKER], 65536))
    if (marker.get('schema') != 1 or marker.get('repository') != REPO
            or marker.get('release_id') != release['id'] or marker.get('release_tag') != release['tag_name']):
        raise ValueError('publication marker Release identity mismatch')
    for field in ('source_sha', 'source_tree', 'tag_object_sha'):
        if not re.fullmatch('[a-f0-9]{40}', marker.get(field, '')):
            raise ValueError('invalid publication marker source')
        if identity and marker[field] != identity[field]:
            raise ValueError('publication marker source changed')
    sums = marker.get('assets', {})
    parts = {v: set() for v in VARIANTS}
    if not BASE_ASSETS <= set(sums):
        raise ValueError('publication marker incomplete')
    for name, sha in sums.items():
        if not re.fullmatch('[a-f0-9]{64}', sha):
            raise ValueError('invalid marker digest')
        match = PART.fullmatch(name)
        if name not in BASE_ASSETS and not match:
            raise ValueError('unexpected firmware asset')
        if match:
            parts[match[1]].add(match[2])
        if name not in assets or hashlib.sha256(download(assets[name])).hexdigest() != sha:
            raise ValueError('archived publication asset mismatch')
    expected = {'bootloader', 'partition-table', 'ota_data_initial', 'universal-gnss-esp32'}
    if any(labels != expected for labels in parts.values()) or len(sums) != len(BASE_ASSETS) + 8:
        raise ValueError('publication marker missing/duplicate firmware parts')
    checks = dict((name, sha) for sha, name in
                  (line.split() for line in download(assets[SUMS], 65536).decode('ascii').splitlines()))
    if checks != {name: sha for name, sha in sums.items() if name != SUMS}:
        raise ValueError('archived checksum manifest mismatch')
    return marker


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def upload(release_id, path, env):
    if type(release_id) is not int or release_id <= 0:
        raise ValueError('invalid Release id')
    token = env.get('GITHUB_TOKEN')
    if not token:
        raise ValueError('publication token required')
    name = path.name
    if not re.fullmatch(r'esp32-[A-Za-z0-9._-]+', name):
        raise ValueError('unsafe firmware asset name')
    url = ('https://uploads.github.com/repos/' + REPO + '/releases/' + str(release_id)
           + '/assets?name=' + urllib.parse.quote(name, safe=''))
    request = urllib.request.Request(url, method='POST', data=path.read_bytes(), headers={
        'Authorization': 'Bearer ' + token, 'Content-Type': 'application/octet-stream',
        'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'})
    # Upload destinations are constructed, never taken from untrusted metadata;
    # a redirect must not forward the write credential to another host.
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=180) as response:
        return json.load(response)


def append_assets(release, identity, directory, api_call, reauthorize, env):
    """Check every collision before writes, resume missing files, marker last."""
    directory = Path(directory)
    if any(p.is_symlink() or not p.is_file() for p in directory.iterdir()):
        raise ValueError('publication assets must be regular files')
    expected = {p.name: p for p in directory.iterdir()}
    if MARKER not in expected or not BASE_ASSETS <= set(expected):
        raise ValueError('incomplete prepared firmware publication')
    old = asset_map(release)
    for name, path in expected.items():
        if name in old and download(old[name]) != path.read_bytes():
            raise ValueError('immutable release asset conflict; no overwrite')
    reauthorize()
    for name in [ARCHIVE] + sorted(set(expected) - {MARKER, ARCHIVE}):
        if name not in old:
            upload(release['id'], expected[name], env)
    # Verify all bytes durably BEFORE writing the completion marker/catalogue.
    current = get_release(api_call, release['tag_name'])
    if current['id'] != release['id']:
        raise ValueError('Release changed during asset archival')
    uploaded = asset_map(current)
    for name, path in expected.items():
        if name != MARKER and (name not in uploaded or download(uploaded[name]) != path.read_bytes()):
            raise ValueError('durable asset verification failed')
    reauthorize()
    if MARKER not in uploaded:
        upload(release['id'], expected[MARKER], env)
    complete = get_release(api_call, release['tag_name'])
    verify_marker(complete, identity)
    return identity


def validate_site(alpha, site, identity, repo=None):
    """Revalidate all historical bundles too; only select the two new profiles."""
    import shutil
    import tempfile
    site = Path(site)
    meta = alpha.s.p.load(site / 'alpha-publication.json')
    for field in ('source_sha', 'source_tree', 'release_tag', 'release_id', 'tag_object_sha', 'published_at'):
        if meta.get(field) != identity[field]:
            raise ValueError('archived site identity changed: ' + field)
    catalog = alpha.s.p.load(site / 'flasher/catalog.json')
    approvals = alpha.s.p.load(site / 'flasher/approved-sources.json')
    selected = []
    with tempfile.TemporaryDirectory(prefix='ug-release-bundles-') as temp:
        for i, entry in enumerate(catalog['images']):
            key = entry['build_key']
            if not re.fullmatch('[a-f0-9]{20}', key):
                raise ValueError('unsafe firmware build identity')
            root = Path(temp) / str(i)
            directory = root / 'published' / key
            directory.parent.mkdir(parents=True)
            shutil.copytree(site / 'flasher/published' / key, directory)
            (root / 'catalog-entry.json').write_bytes(alpha.s.p.json_bytes(entry))
            checked, folder, _ = alpha.s.inspect_bundle(root, approvals)
            alpha.require_s3_memory(checked, folder)
            record = next(r for r in approvals['sources'] if r['source_sha'] == entry['source_sha'])
            alpha.s.require_source(repo or alpha.ROOT, entry['source_sha'], record['source_tree'], record['publication'])
            if entry['source_sha'] == identity['source_sha'] and entry['profile']['variant'] in VARIANTS:
                selected.append(entry)
    if len(selected) != 2 or {e['profile']['variant'] for e in selected} != set(VARIANTS):
        raise ValueError('Release requires exactly S3 Gateway and C3 USB for the same source')
    return selected


def materialize(alpha, repo, site, directory, identity):
    """Create stable namespaced assets; no notes/body or unrelated assets."""
    import shutil
    import subprocess
    selected = validate_site(alpha, site, identity, repo)
    site, directory = Path(site), Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    alpha.deterministic_zip(site, directory / ARCHIVE)
    subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar.gz',
                    '--output=' + str((directory / 'esp32-firmware-source.tar.gz').resolve()),
                    identity['source_sha']], check=True, env=alpha.ENV)
    for name in ('catalog.json', 'approved-sources.json'):
        shutil.copyfile(site / 'flasher' / name, directory / ('esp32-' + name))
    for entry in selected:
        prefix = 'esp32-' + entry['profile']['variant']
        folder = site / 'flasher/published' / entry['build_key']
        shutil.copyfile(folder / 'bundle.zip', directory / (prefix + '.zip'))
        for name in ('manifest.json', 'provenance.json', 'qualification.json'):
            shutil.copyfile(folder / name, directory / (prefix + '-' + name))
        for part in alpha.s.p.load(folder / 'manifest.json')['builds'][0]['parts']:
            shutil.copyfile(folder / part['path'], directory / (prefix + '-' + part['path']))
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(directory.iterdir())}
    (directory / SUMS).write_text(''.join(sha + '  ' + name + '\n' for name, sha in hashes.items()))
    hashes[SUMS] = hashlib.sha256((directory / SUMS).read_bytes()).hexdigest()
    marker = {key: identity[key] for key in ('source_sha', 'source_tree', 'release_tag', 'release_id', 'tag_object_sha')}
    marker.update(schema=1, repository=REPO, assets=hashes)
    (directory / MARKER).write_bytes(alpha.s.p.json_bytes(marker))
    return directory


def resume_site(alpha, release, identity, output):
    """Preserve exact first-attempt archive, rather than restamping/rebuilding it."""
    import importlib.util
    import tempfile
    import shutil
    spec = importlib.util.spec_from_file_location('release_refresh', Path(__file__).with_name('refresh_esp32_site.py'))
    refresh = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(refresh)
    asset = asset_map(release)[ARCHIVE]
    # Before a completion marker exists the API's own checksum anchors recovery.
    if not re.fullmatch(r'sha256:[a-f0-9]{64}', asset.get('digest', '')):
        raise ValueError('partial archive recovery requires GitHub asset SHA256')
    data = download(asset)
    output = Path(output).resolve()
    if output.exists() or output.is_relative_to(alpha.ROOT.resolve()):
        raise ValueError('new recovery output outside source required')
    with tempfile.TemporaryDirectory(prefix='ug-release-resume-') as temp:
        root = Path(temp)
        refresh.unpack(data, root)
        validate_site(alpha, root, identity)
        shutil.copytree(root, output)
    return output
