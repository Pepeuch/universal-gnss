#!/usr/bin/env python3
"""Fail-closed automatic alpha authority: main ancestry, explicit approval, CI."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('gate_alpha', ROOT/'tools/branches/esp32_alpha.py')
a = importlib.util.module_from_spec(spec); spec.loader.exec_module(a)
ALREADY_PUBLISHED = False
REQUIRED = ('backlog-status.yml', 'cmake.yml', 'distributions.yml', 'docker.yml',
            'format.yml', 'ros2-lyrical.yml', 'esp32.yml')


def api(path):
    headers = {'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
    if os.environ.get('GITHUB_TOKEN'):
        headers['Authorization'] = 'Bearer '+os.environ['GITHUB_TOKEN']
    request = urllib.request.Request('https://api.github.com/repos/'+a.REPO+'/'+path, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


class NotReady(ValueError):
    """A required validation has not succeeded yet."""


def require_runs(runs, sha):
    for filename in REQUIRED:
        matches = [r for r in runs if r.get('path') == '.github/workflows/'+filename and
                   r.get('head_sha') == sha and r.get('event') == 'push' and r.get('head_branch') == 'main']
        if not matches:
            raise NotReady('mandatory main validation missing: '+filename)
        newest = max(matches, key=lambda r: (r['id'], r.get('run_attempt', 1)))
        if newest.get('status') != 'completed' or newest.get('conclusion') != 'success':
            raise NotReady('mandatory main validation not successful: '+filename)


def require_ci(sha):
    runs=[]
    for page in range(1,11):
        batch=api('actions/runs?head_sha='+sha+'&event=push&per_page=100&page='+str(page))['workflow_runs']
        runs.extend(batch)
        if len(batch)<100:break
    else:raise ValueError('validation history exceeds bounded query')
    require_runs(runs,sha)


def authorize(sha, repo=ROOT):
    records = a.s.r.approval_register(a.s.p.load(Path(repo)/'docs/flasher/approved-sources.json'))
    row = records.get(sha)
    if not row or row['publication'] != 'main_approved':
        raise ValueError('explicit main publication approval required')
    identity = a.source(repo, sha)
    if identity['source_tree'] != row['source_tree']:
        raise ValueError('approved source tree mismatch')
    require_ci(sha)
    publisher = a.git(repo, 'rev-parse', 'origin/main')
    if publisher != sha:
        require_ci(publisher)  # The trusted publication implementation must be green too.
    return identity


def select(repo=ROOT):
    global ALREADY_PUBLISHED
    records = a.s.r.approval_register(a.s.p.load(Path(repo)/'docs/flasher/approved-sources.json'))
    for sha in a.git(repo, 'rev-list', 'origin/main').splitlines():
        if sha in records and records[sha]['publication'] == 'main_approved':
            # Never fall back to an older approved revision if the newest fails.
            try:
                authorize(sha, repo)
            except NotReady:
                return ''
            try:
                release=api('releases/tags/esp32-alpha-'+sha[:12])
            except urllib.error.HTTPError as error:
                if error.code!=404:raise
            else:
                names={item['name'] for item in release['assets']}
                if release.get('draft') or not release.get('prerelease') or not {'esp32-alpha-site.zip','SHA256SUMS','s3-gateway.zip','c3-rover-usb.zip'}<=names:
                    raise ValueError('existing publication incomplete; explicit recovery required')
                ALREADY_PUBLISHED = True
                return sha  # Re-deploy verified archive without rebuilding firmware.
            return sha
    return ''


def release_candidate(tag, sha='', repo=ROOT, actor=None, release_id=None, event_sha=None):
    release = a.v.get_release(api, tag)
    target, _ = a.v.tag_identity(api, tag)
    if (sha and sha != target) or (event_sha and event_sha != target):
        raise ValueError('requested/event SHA differs from Release tag')
    identity, release = a.v.authorize(a, api, require_ci, repo, target, tag,
                                     actor=actor, release_id=release_id)
    assets = a.v.asset_map(release)
    complete = a.v.MARKER in assets
    if complete:
        a.v.verify_marker(release, identity)
    return identity, complete, a.v.ARCHIVE in assets and not complete


def choose(args, env=os.environ):
    """Release and maintenance entry points share all immutable authority checks."""
    event = env.get('GITHUB_EVENT_NAME')
    if env.get('GITHUB_REPOSITORY') != a.REPO:
        raise ValueError('publication repository mismatch')
    if event not in ('release', 'workflow_dispatch', 'workflow_run'):
        raise ValueError('unsupported publication event')
    if event != 'release' and env.get('GITHUB_REF') != 'refs/heads/main':
        raise ValueError('publication controller must run from main')
    tag, sha, release_id, event_sha = args.release_tag, args.source_sha, None, None
    if event == 'release':
        payload = a.s.p.load(Path(env['GITHUB_EVENT_PATH']))
        if payload.get('action') != 'published':
            raise ValueError('published Release event required')
        tag = payload['release']['tag_name']
        if not a.v.RC_TAG.fullmatch(tag):
            return {}, False, False
        release_id = payload['release']['id']
        event_sha = env.get('GITHUB_SHA')
        if env.get('GITHUB_REF') != 'refs/tags/' + tag or not event_sha:
            raise ValueError('Release event ref/SHA mismatch')
    elif event == 'workflow_run':
        releases = a.v.pages(api, 'releases')
        candidates = [r for r in releases if not r.get('draft') and
                      a.v.RC_TAG.fullmatch(r.get('tag_name', ''))]
        if candidates:
            # CI may complete after Release publication. Never use an older RC
            # to bypass the newest candidate's missing validation/authority.
            tag = max(candidates, key=lambda r: r['published_at'])['tag_name']
    if tag and a.v.RC_TAG.fullmatch(tag):
        if event != 'workflow_run' and not env.get('GITHUB_ACTOR'):
            raise ValueError('publication actor required')
        try:
            return release_candidate(tag, sha, actor=env.get('GITHUB_ACTOR')
                                     if event != 'workflow_run' else None,
                                     release_id=release_id, event_sha=event_sha)
        except NotReady as error:
            if event == 'workflow_dispatch':
                raise
            print('Publication deferred: ' + str(error))
            return {}, False, False
    if event == 'workflow_dispatch':
        identity = authorize(sha)
        return a.source(ROOT, sha, tag), False, False
    selected = select()
    return (a.source(ROOT, selected) if selected else {}), ALREADY_PUBLISHED, False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-sha')
    parser.add_argument('--release-tag', default='')
    args = parser.parse_args()
    identity, complete, resume = choose(args)
    sha = identity.get('source_sha', '')
    print(sha)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as out:
            out.write('source_sha='+sha+'\n')
            out.write('already_published='+str(complete).lower()+'\n')
            out.write('resume_archive='+str(resume).lower()+'\n')
            out.write('release_tag='+identity.get('release_tag', '')+'\n')
            out.write('identity='+json.dumps(identity, sort_keys=True, separators=(',', ':'))+'\n')
            out.write('publisher_sha='+a.git(ROOT, 'rev-parse', 'HEAD')+'\n')


if __name__ == '__main__':
    main()
