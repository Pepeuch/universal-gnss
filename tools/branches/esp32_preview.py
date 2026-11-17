#!/usr/bin/env python3
"""Expose successful repository-branch CI firmware through the existing Pages site.

Producer jobs have no write token. This trusted-main controller handles data only:
qualified CI bundles -> one durable development Release -> Pages catalogue.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preview_alpha', ROOT / 'tools/branches/esp32_alpha.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)
r = a.v
spec = importlib.util.spec_from_file_location('preview_history', ROOT / 'tools/branches/esp32_catalog_history.py')
history = importlib.util.module_from_spec(spec)
spec.loader.exec_module(history)
TAG = 'esp32-ci-builds'
VARIANTS = ('s3-gateway', 'c3-rover-usb', 'c3-rover-uart')


def write(path, value):
    token = os.environ.get('GITHUB_TOKEN')
    if not token:
        raise ValueError('trusted publisher token required')
    request = urllib.request.Request('https://api.github.com/repos/' + a.REPO + '/' + path,
        data=json.dumps(value).encode(), method='POST', headers={
            'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
            'Content-Type': 'application/json', 'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.build_opener(r.NoRedirect()).open(request, timeout=60) as response:
        return json.load(response)


def authorize(repo, run_id):
    if type(run_id) is not int or run_id <= 0:
        raise ValueError('exact native CI run required')
    run = r.api('actions/runs/' + str(run_id))
    if (run.get('path') != '.github/workflows/esp32.yml' or run.get('event') != 'push'
            or run.get('status') != 'completed' or run.get('conclusion') != 'success'
            or run.get('head_repository', {}).get('full_name') != a.REPO):
        raise ValueError('successful same-repository native push CI required')
    branch = run.get('head_branch', '')
    subprocess.run(['git', 'check-ref-format', '--branch', branch], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ref = r.api('branches/' + urllib.parse.quote(branch, safe=''))
    head=ref['commit']['sha']
    if not re.fullmatch('[a-f0-9]{40}',head):raise ValueError('invalid branch head')
    if subprocess.run(['git','-C',str(repo),'cat-file','-e',head+'^{commit}'],env=a.ENV,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
        subprocess.run(['git','-C',str(repo),'fetch','--no-tags','origin',head],check=True,env=a.ENV,timeout=90)
    if subprocess.run(['git','-C',str(repo),'merge-base','--is-ancestor',run['head_sha'],ref['commit']['sha']],env=a.ENV).returncode:
        raise ValueError('built source is no longer on this branch')
    identity = a.source(repo, run['head_sha'], development=True)
    return {**identity, 'branch': branch, 'run_id': run_id, 'channel': 'DEV',
            'finished_at': run['updated_at']}


def available_run(run_id):
    run=r.api('actions/runs/'+str(run_id))
    if run.get('event')!='push' or run.get('conclusion')!='success':return False
    names={item['name'] for item in r.pages(lambda path:r.api(path)['artifacts'],'actions/runs/'+str(run_id)+'/artifacts') if not item.get('expired')}
    expected={'native-'+variant for variant in VARIANTS}
    if not names.intersection(expected):return False  # Intentional docs-only skip, or old producer without bundles.
    if not expected<=names:raise ValueError('incomplete native artifact matrix')
    try:r.api('branches/'+urllib.parse.quote(run['head_branch'],safe=''))
    except urllib.error.HTTPError as error:
        if error.code==404:return False  # Tag push or deleted branch, not a branch preview.
        raise
    return True

def storage(repo):
    try:
        release = r.api('releases/tags/' + TAG)
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        # One technical archive tag, at the trusted publisher, not branch tags.
        sha = os.environ.get('GITHUB_SHA') or a.git(repo, 'rev-parse', 'HEAD')
        a.s.require_main(repo, sha, a.git(repo, 'rev-parse', sha + '^{tree}'))
        try:
            obj = r.api('git/ref/tags/' + TAG)['object']
        except urllib.error.HTTPError as tag_error:
            if tag_error.code != 404:
                raise
            write('git/refs', {'ref': 'refs/tags/' + TAG, 'sha': sha})
        else:
            a.s.require_main(repo, obj['sha'], a.git(repo, 'rev-parse', obj['sha'] + '^{tree}'))
        release = write('releases', {'tag_name': TAG, 'name': 'ESP32 CI development builds',
            'body': 'Development firmware from successful branch CI. Not a UG version or a hardware qualification. Each snapshot records its branch and firmware SHA; download the matching esp32-ci-source-<SHA>.tar.gz. The automatic GitHub source ZIP for this technical tag refers to the publisher, not the branch firmware.',
            'prerelease': True, 'make_latest': 'false'})
    if release.get('draft') or release.get('tag_name') != TAG or type(release.get('id')) is not int:
        raise ValueError('invalid development archive Release')
    return release


def assets(release):
    return r.asset_map({**release, 'assets': r.pages(r.api, 'releases/' + str(release['id']) + '/assets')})


def verify_snapshot(release, marker, files):
    meta = r.decode(r.download(marker, 65536))
    if (meta.get('schema') != 1 or meta.get('release_id') != release['id']
            or meta.get('repository') != a.REPO or type(meta.get('run_id')) is not int
            or not re.fullmatch('[a-f0-9]{40}', meta.get('source_sha', ''))):
        raise ValueError('invalid CI archive identity')
    names = {'esp32-ci-site-' + str(meta['run_id']) + '.zip',
             'esp32-ci-source-' + meta['source_sha'] + '.tar.gz'}
    if set(meta.get('assets', {})) != names:
        raise ValueError('incomplete CI archive manifest')
    archive = None
    for name, digest in meta['assets'].items():
        if not re.fullmatch('[a-f0-9]{64}', digest):
            raise ValueError('invalid CI archive checksum')
        data = r.download(files[name])
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('CI durable archive checksum mismatch')
        if name.endswith('.zip'):
            archive = data
    return archive, meta


def latest(release):
    files = assets(release)
    markers = [item for name, item in files.items() if re.fullmatch(r'esp32-ci-[1-9][0-9]*\.json', name)]
    if not markers:
        return None  # No pending upload may replace a complete catalogue.
    marker = max(markers, key=lambda item: item['id'])
    data,meta=verify_snapshot(release, marker, files)
    meta['archived_at']=marker.get('created_at',meta['archived_at'])
    return data,meta


def publish(repo, run_id, bundles, output):
    identity = authorize(repo, run_id)
    release = storage(repo)
    files = assets(release)
    name = 'esp32-ci-' + str(run_id) + '.json'
    if name in files:
        data, meta = verify_snapshot(release, files[name], files)
        if meta['source_sha'] != identity['source_sha']:
            raise ValueError('CI run identity changed')
        spec = importlib.util.spec_from_file_location('preview_refresh', ROOT / 'tools/branches/refresh_esp32_site.py')
        refresh = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(refresh)
        return refresh.refresh(data, TAG, output, repo)
    tag_sha, _ = r.tag_identity(r.api, TAG)
    identity.update(release_tag=TAG, release_url=release['html_url'], technical_tag_sha=tag_sha,
                    release_id=release['id'], publisher_source=a.git(repo, 'rev-parse', 'HEAD'))
    approvals = {'schema': 1, 'sources': [{'source_sha': identity['source_sha'],
        'source_tree': identity['source_tree'], 'review': 'Native CI run ' + str(run_id),
        'publication': 'ci_preview'}]}
    site_name = 'esp32-ci-site-' + str(run_id) + '.zip'
    if site_name not in files:
        if len(bundles) != len(VARIANTS):
            raise ValueError('all three native CI bundles required')
        seen = set()
        for bundle in bundles:
            entry, directory, _ = a.s.inspect_bundle(bundle, approvals)
            variant = entry['profile']['variant']
            if entry['source_sha'] != identity['source_sha'] or variant not in VARIANTS or variant in seen:
                raise ValueError('wrong/duplicate development firmware')
            a.require_s3_memory(entry, directory)
            seen.add(variant)
    # A partial attempt resumes its exact initial snapshot, without restamping it.
    if site_name in files:
        if not files[site_name].get('digest', '').startswith('sha256:'):
            raise ValueError('partial CI snapshot requires GitHub SHA256')
        output = Path(output)
        if output.exists():
            raise ValueError('new output required')
        output.mkdir()
        history.w.unpack(r.download(files[site_name]), output)
        meta = a.s.p.load(output / 'alpha-publication.json')
        if any(meta.get(key) != identity[key] for key in ('source_sha', 'source_tree', 'branch', 'run_id', 'release_id')):
            raise ValueError('partial CI snapshot identity mismatch')
    else:
        output = a.s.prepare(output, approvals, bundles, repo)
        identity['archived_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (output / 'alpha-publication.json').write_bytes(a.s.p.json_bytes(identity))
        (output / 'index.html').write_text('<meta http-equiv="refresh" content="0;url=flasher.html">')
        history.merge(output)
        path = output / 'flasher/branches.json'
        mapping = a.s.p.load(path) if path.exists() else {'schema': 1, 'branches': {}}
        mapping['branches'][identity['branch']] = {key: identity[key] for key in
            ('source_sha', 'source_tree', 'run_id', 'finished_at')}
        encoded=a.s.p.json_bytes(mapping)
        if len(encoded)>65536:raise ValueError('branch catalogue capacity exceeded; no truncation')
        path.write_bytes(encoded)
    # Reuse the full refresh validator for historical and new firmware alike.
    with tempfile.TemporaryDirectory(prefix='ug-ci-publish-') as temp:
        temp = Path(temp)
        archive = temp / site_name
        a.deterministic_zip(output, archive)
        # refresh uses the immutable technical tag, while source is the branch SHA.
        history.w.refresh(archive.read_bytes(), TAG, temp / 'verified', repo)
        source = temp / ('esp32-ci-source-' + identity['source_sha'] + '.tar.gz')
        subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar.gz',
                        '--output=' + str(source), identity['source_sha']], check=True, env=a.ENV)
        hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (archive, source)}
        meta = a.s.p.load(output / 'alpha-publication.json')
        marker = temp / name
        marker.write_bytes(a.s.p.json_bytes({**meta, 'schema': 1, 'repository': a.REPO, 'assets': hashes}))
        for path in (archive, source, marker):
            if path.name in files and r.download(files[path.name]) != path.read_bytes():
                raise ValueError('immutable CI asset conflict')
        authorize(repo, run_id)
        for path in (archive, source):
            if path.name not in files:
                r.upload(release['id'], path, os.environ)
        current = assets(release)
        for path in (archive, source):
            if r.download(current[path.name]) != path.read_bytes():
                raise ValueError('CI archive readback failed')
        authorize(repo, run_id)
        r.upload(release['id'], marker, os.environ)
        current = assets(release)
        verify_snapshot(release, current[name], current)
    return output


def collect(repo, output):
    """Drain completed branch builds, including callbacks replaced in the queue.

    GitHub concurrency can replace pending callbacks. Scanning retained native
    artifacts avoids losing a branch when another branch or a docs-only run finishes.
    Existing immutable markers are reused; branch source is never executed here.
    """
    artifacts=r.pages(lambda path:r.api(path)['artifacts'],'actions/artifacts?name=native-s3-gateway')
    run_ids={item['workflow_run']['id'] for item in artifacts if not item.get('expired')}
    try:
        release=r.api('releases/tags/'+TAG)
    except urllib.error.HTTPError as error:
        if error.code!=404:raise
        release=None
    recorded=assets(release) if release else {}
    for name in recorded:
        match=re.fullmatch(r'esp32-ci-site-([1-9][0-9]*)\.zip',name)
        if match and 'esp32-ci-'+match[1]+'.json' not in recorded:run_ids.add(int(match[1]))
    candidates, seen = [], set()
    for run_id in sorted(run_ids,reverse=True):
        run=r.api('actions/runs/'+str(run_id))
        if run.get('path')!='.github/workflows/esp32.yml' or run.get('event')!='push' or run.get('conclusion')!='success':continue
        branch = run.get('head_branch')
        if branch in seen or run.get('head_repository', {}).get('full_name') != a.REPO:
            continue
        if ('esp32-ci-'+str(run['id'])+'.json' in recorded or
                'esp32-ci-site-'+str(run['id'])+'.zip' in recorded or available_run(run['id'])):
            candidates.append(run['id'])
            seen.add(branch)
    if not candidates:
        return False
    release = release or storage(repo)
    pending = [run_id for run_id in reversed(candidates) if 'esp32-ci-' + str(run_id) + '.json' not in recorded]
    if not pending:
        return False
    output = Path(output).resolve()
    if output.exists() or output.is_relative_to(Path(repo).resolve()):
        raise ValueError('new output outside source required')
    with tempfile.TemporaryDirectory(prefix='ug-branch-builds-') as temp:
        temp = Path(temp)
        for run_id in pending:
            root = temp / str(run_id)
            bundles = []
            for variant in (() if 'esp32-ci-site-'+str(run_id)+'.zip' in recorded else VARIANTS):
                directory = root / variant
                subprocess.run(['gh', 'run', 'download', str(run_id), '--repo', a.REPO,
                    '--name', 'native-' + variant, '--dir', str(directory)], check=True, timeout=180,
                    env={**os.environ, 'GH_TOKEN': os.environ.get('GITHUB_TOKEN', '')})
                bundles.append(directory)
            result = publish(repo, run_id, bundles, root / 'site')
        shutil.copytree(result, output)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-id', type=int)
    parser.add_argument('--probe',action='store_true')
    parser.add_argument('--collect',action='store_true')
    parser.add_argument('--bundle', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if (os.getuid() == 0 or os.environ.get('GITHUB_REPOSITORY') != a.REPO
            or os.environ.get('GITHUB_EVENT_NAME') not in ('workflow_run','workflow_dispatch','release')
            or (os.environ.get('GITHUB_EVENT_NAME')!='release' and os.environ.get('GITHUB_REF')!='refs/heads/main')):
        parser.error('trusted non-root main publication controller required')
    a.s.require_main(ROOT,a.git(ROOT,'rev-parse','HEAD'),a.git(ROOT,'rev-parse','HEAD^{tree}'))
    try:
        if args.collect:
            if args.output is None:raise ValueError('output required')
            ready=collect(ROOT,args.output)
            with open(os.environ['GITHUB_OUTPUT'],'a') as output:output.write('ready='+str(ready).lower()+'\n')
            return
        if args.run_id is None:raise ValueError('run id required')
        if args.probe:
            ready=available_run(args.run_id)
            with open(os.environ['GITHUB_OUTPUT'],'a') as output:output.write('ready='+str(ready).lower()+'\n')
            return
        if args.output is None:raise ValueError('output required')
        publish(ROOT, args.run_id, args.bundle, args.output)
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    print('Verified branch firmware archived; ready for Pages deployment.')


if __name__ == '__main__':
    main()
