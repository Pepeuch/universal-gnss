"""Release publication contract using synthetic bundles and an in-memory GitHub.

No network writes, firmware build, flash or publication during these tests.
"""
import argparse
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import yaml

ROOT = Path(__file__).resolve().parents[3]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


g = load('release_gate_test', 'tools/branches/esp32_publication_gate.py')
a, r = g.a, g.a.v
f = load('release_package_fixture', 'tools/branches/tests/test_firmware_package.py')
w = load('release_refresh_test', 'tools/branches/refresh_esp32_site.py')


class Authority(unittest.TestCase):
    sha, tree, tag = 'a' * 40, 'b' * 40, 'v0.7.5-RC1'

    def setUp(self):
        self.release = {'id': 42, 'tag_name': self.tag, 'draft': False, 'prerelease': True,
                        'published_at': '2026-11-16T22:06:51Z', 'author': {'login': 'Maintainer'},
                        'body': 'Original description', 'assets': []}
        self.rulesets = []
        self.permission = 'maintain'
        self.approvals = {'schema': 1, 'sources': []}
        self.identity = {'source_sha': self.sha, 'source_tree': self.tree,
                         'release_tag': self.tag, 'release_url': 'https://example.invalid/release'}

    def api(self, path):
        if path == 'releases/tags/' + self.tag:
            return copy.deepcopy(self.release)
        if path.startswith('releases/42/assets?'):
            return copy.deepcopy(self.release['assets'])
        if path == 'git/ref/tags/' + self.tag:
            return {'object': {'type': 'commit', 'sha': self.sha}}
        if path.startswith('collaborators/'):
            return {'permission': self.permission}
        if path.startswith('collaborators?'):
            return [{'id': 7, 'login': 'Maintainer'}]
        if path.startswith('rulesets?'):
            return [{'id': n} for n in range(len(self.rulesets))]
        if path.startswith('rulesets/'):
            return self.rulesets[int(path.split('/')[1])]
        raise AssertionError('unexpected API request: ' + path)

    def authorize(self, **kwargs):
        with patch.object(a, 'source', return_value=self.identity), \
                patch.object(a.s.p, 'load', return_value=self.approvals), \
                patch.object(a, 'git', return_value=self.sha), patch.object(g, 'require_ci') as ci:
            result = r.authorize(a, self.api, ci, ROOT, self.sha, self.tag, **kwargs)
            ci.assert_called_once_with(self.sha)
            return result

    def approve(self):
        self.approvals['sources'] = [{'source_sha': self.sha, 'source_tree': self.tree,
                                      'publication': 'main_approved', 'review': 'explicit review'}]

    def test_trusted_published_release_is_approval_without_register_or_ruleset(self):
        identity, _ = self.authorize(actor='Maintainer')
        self.assertEqual(identity['authority'], 'maintainer_release')
        self.assertEqual(self.approvals['sources'], [])

    def test_untrusted_release_author_or_dispatch_actor_rejected_even_with_approval(self):
        self.approve()
        for permission in ('write', 'read', 'none'):
            self.permission = permission
            with self.assertRaisesRegex(ValueError, 'trusted'):
                self.authorize(actor='Contributor')
        self.permission = 'maintain'
        def api(path):
            return {'permission': 'read'} if path == 'collaborators/Contributor/permission' else self.api(path)
        with patch.object(a, 'source', return_value=self.identity):
            with self.assertRaisesRegex(ValueError, 'trusted'):
                r.authorize(a, api, lambda _: None, ROOT, self.sha, self.tag, actor='Contributor')

    def test_revoked_or_mismatched_record_is_not_silently_approved(self):
        self.approve()
        for field, value in (('publication', 'not_authorized'), ('source_tree', 'c' * 40)):
            self.approve()
            self.approvals['sources'][0][field] = value
            with self.assertRaisesRegex(ValueError, 'revoked|mismatch'):
                self.authorize()

    def test_changed_release_id_tag_object_or_commit_is_rejected(self):
        self.approve()
        for kwargs in ({'release_id': 43}, {'tag_object_sha': 'c' * 40}):
            with self.assertRaises(ValueError):
                self.authorize(**kwargs)
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            r.authorize(a, self.api, lambda _: None, ROOT, 'd' * 40, self.tag)

    def test_canonical_rc_format_only_and_existing_published_release(self):
        for tag in ('v0.7.5-RC0', 'v00.7.5-RC1', 'v0.7.5-rc1', 'v0.7.5-RC01',
                    'v0.7.5', 'v0.7.5-RC1/evil', 'v0.7.5-RC1\n', '--bad'):
            with self.assertRaises(ValueError):
                r.get_release(self.api, tag)
        self.release['draft'] = True
        with self.assertRaises(ValueError):
            r.get_release(self.api, self.tag)
        self.release['draft'] = False
        self.release['published_at'] = None
        with self.assertRaises((ValueError, AttributeError)):
            r.get_release(self.api, self.tag)

    def test_annotated_tag_peels_to_exact_commit_and_keeps_tag_object(self):
        def api(path):
            if path.startswith('git/ref/'):
                return {'object': {'type': 'tag', 'sha': 'c' * 40}}
            return {'object': {'type': 'commit', 'sha': self.sha}}
        self.assertEqual(r.tag_identity(api, self.tag), (self.sha, 'c' * 40))
        with self.assertRaises(ValueError):
            r.tag_identity(lambda _: {'object': {'type': 'tag', 'sha': 'c' * 40}}, self.tag)

    def test_source_ci_and_controller_ci_both_mandatory(self):
        self.approve()
        with patch.object(a, 'source', return_value=self.identity), \
                patch.object(a.s.p, 'load', return_value=self.approvals), \
                patch.object(a, 'git', return_value='c' * 40), patch.object(g, 'require_ci') as ci:
            r.authorize(a, self.api, ci, ROOT, self.sha, self.tag)
            self.assertEqual([call.args[0] for call in ci.call_args_list], [self.sha, 'c' * 40])
            ci.side_effect = g.NotReady('required workflow missing')
            with self.assertRaises(g.NotReady):
                r.authorize(a, self.api, ci, ROOT, self.sha, self.tag)

    def test_published_event_and_manual_catchup_resolve_same_tag_sha(self):
        with tempfile.TemporaryDirectory() as temp:
            payload = Path(temp) / 'event.json'
            payload.write_text(json.dumps({'action': 'published', 'release': self.release}))
            env = {'GITHUB_EVENT_NAME': 'release', 'GITHUB_REPOSITORY': r.REPO,
                   'GITHUB_EVENT_PATH': str(payload), 'GITHUB_SHA': self.sha,
                   'GITHUB_REF': 'refs/tags/' + self.tag, 'GITHUB_ACTOR': 'Maintainer'}
            args = argparse.Namespace(source_sha=None, release_tag='')
            with patch.object(g, 'release_candidate', return_value=(self.identity, False, False)) as candidate:
                self.assertEqual(g.choose(args, env)[0], self.identity)
                self.assertEqual(candidate.call_args.kwargs['event_sha'], self.sha)
                env.update(GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/heads/main')
                args.source_sha, args.release_tag = self.sha, self.tag
                self.assertEqual(g.choose(args, env)[0], self.identity)
                env['GITHUB_REF'] = 'refs/heads/dev'
                with self.assertRaises(ValueError):
                    g.choose(args, env)

    def test_workflow_run_defers_pending_rc_without_fallback_or_approval(self):
        env = {'GITHUB_EVENT_NAME': 'workflow_run', 'GITHUB_REF': 'refs/heads/main',
               'GITHUB_REPOSITORY': r.REPO}
        for error in (g.NotReady('CI pending'), g.NotReady('CI failed')):
            with patch.object(g.a.v, 'pages', return_value=[self.release]), \
                    patch.object(g, 'release_candidate', side_effect=error), patch.object(g, 'select') as legacy:
                self.assertEqual(g.choose(argparse.Namespace(source_sha=None, release_tag=''), env),
                                 ({}, False, False))
                legacy.assert_not_called()


class Assets(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.identity = {'source_sha': 'a' * 40, 'source_tree': 'b' * 40, 'release_tag': 'v0.7.5-RC1',
                         'release_id': 42, 'tag_object_sha': 'a' * 40,
                         'published_at': '2026-11-16T22:06:51Z', 'channel': 'RC'}
        self.release = {'id': 42, 'tag_name': self.identity['release_tag'], 'draft': False,
                        'published_at': self.identity['published_at'], 'body': 'Do not edit',
                        'assets': [{'name': 'unrelated.txt'}]}
        self.bytes = {'unrelated.txt': b'untouched'}
        self.uploads = []
        self.directory = self.root / 'assets'
        self.directory.mkdir()
        names = set(r.BASE_ASSETS) - {r.SUMS}
        for variant in r.VARIANTS:
            names.update('esp32-' + variant + '-000000-' + name + '.bin' for name in
                         ('bootloader', 'partition-table', 'ota_data_initial', 'universal-gnss-esp32'))
        for name in names:
            (self.directory / name).write_bytes(('synthetic-' + name).encode())
        self.write_marker()

    def write_marker(self):
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.directory.iterdir()
                  if p.name not in (r.MARKER, r.SUMS)}
        (self.directory / r.SUMS).write_text(''.join(sha + '  ' + name + '\n' for name, sha in sorted(hashes.items())))
        hashes[r.SUMS] = hashlib.sha256((self.directory / r.SUMS).read_bytes()).hexdigest()
        marker = {key: self.identity[key] for key in ('source_sha', 'source_tree', 'release_tag', 'release_id', 'tag_object_sha')}
        marker.update(schema=1, repository=r.REPO, assets=hashes)
        (self.directory / r.MARKER).write_bytes(a.s.p.json_bytes(marker))

    def api(self, path):
        if path.startswith('releases/tags/'):
            return copy.deepcopy(self.release)
        if path.startswith('releases/42/assets?'):
            return copy.deepcopy(self.release['assets'])
        raise AssertionError(path)

    def upload(self, release_id, path, env):
        self.assertEqual(release_id, 42)
        self.assertNotIn(path.name, self.bytes)
        self.uploads.append(path.name)
        self.bytes[path.name] = path.read_bytes()
        self.release['assets'].append({'name': path.name, 'state': 'uploaded',
            'size': path.stat().st_size, 'digest': 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest(),
            'browser_download_url': 'https://github.com/' + r.REPO + '/releases/download/' +
                                    self.identity['release_tag'] + '/' + path.name})

    def append(self, upload=None, reauthorize=lambda: None):
        with patch.object(r, 'download', side_effect=lambda asset, *args: self.bytes[asset['name']]), \
                patch.object(r, 'upload', side_effect=upload or self.upload):
            return r.append_assets(self.release, self.identity, self.directory, self.api, reauthorize, {})

    def test_append_existing_release_preserves_description_and_other_assets_marker_last(self):
        self.append()
        self.assertEqual(self.release['body'], 'Do not edit')
        self.assertEqual(self.bytes['unrelated.txt'], b'untouched')
        self.assertEqual(self.uploads[0], r.ARCHIVE)
        self.assertEqual(self.uploads[-1], r.MARKER)
        self.assertEqual(len(self.uploads), 22)

    def test_complete_rerun_has_no_write(self):
        self.append()
        self.uploads.clear()
        self.append()
        self.assertEqual(self.uploads, [])

    def test_mid_upload_failure_leaves_no_complete_marker_and_resumes_missing_only(self):
        def fail(release_id, path, env):
            if len(self.uploads) == 4:
                raise OSError('simulated connection loss')
            self.upload(release_id, path, env)
        with self.assertRaises(OSError):
            self.append(upload=fail)
        self.assertNotIn(r.MARKER, self.bytes)
        first = list(self.uploads)
        self.append()
        self.assertEqual(len(self.uploads), len(set(self.uploads)))
        self.assertEqual(self.uploads[:4], first)
        self.assertEqual(self.uploads[-1], r.MARKER)

    def test_conflicting_existing_bytes_fail_before_any_upload(self):
        name = 'esp32-catalog.json'
        self.release['assets'].append({'name': name})
        self.bytes[name] = b'different existing asset'
        with self.assertRaisesRegex(ValueError, 'conflict'):
            self.append()
        self.assertEqual(self.uploads, [])

    def test_revoked_authority_before_marker_cannot_complete_publication(self):
        calls = []
        def revoke():
            calls.append(1)
            if len(calls) == 2:
                raise ValueError('authority revoked during build')
        with self.assertRaisesRegex(ValueError, 'revoked'):
            self.append(reauthorize=revoke)
        self.assertNotIn(r.MARKER, self.bytes)

    def test_corrupted_raw_binary_prevents_catalogue_completion(self):
        self.append()
        part = next(name for name in self.bytes if name.endswith('.bin'))
        self.bytes[part] += b'corrupt'
        with patch.object(r, 'download', side_effect=lambda asset, *args: self.bytes[asset['name']]):
            with self.assertRaisesRegex(ValueError, 'mismatch'):
                r.verify_marker(self.release, self.identity)

    def test_duplicate_assets_or_manifest_keys_rejected(self):
        self.release['assets'].append({'name': 'unrelated.txt'})
        with self.assertRaises(ValueError):
            r.asset_map(self.release)
        with self.assertRaises(ValueError):
            r.decode('{"schema":1,"schema":2}')

    def test_untrusted_asset_url_and_missing_digest_for_partial_recovery(self):
        with self.assertRaisesRegex(ValueError, 'untrusted'):
            r.download({'browser_download_url': 'https://attacker.invalid/binary'})
        self.release['assets'].append({'name': r.ARCHIVE})
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            r.resume_site(a, self.release, self.identity, self.root / 'recovered')

    def test_upload_uses_exact_existing_id_and_rejects_redirects(self):
        path = self.directory / r.ARCHIVE
        seen = []
        class Opener:
            def open(self, request, timeout):
                seen.append(request)
                return io.BytesIO(b'{"id":123}')
        with patch.object(r.urllib.request, 'build_opener', return_value=Opener()) as opener:
            r.upload(42, path, {'GITHUB_TOKEN': 'synthetic-test-only'})
            self.assertIsInstance(opener.call_args.args[0], r.NoRedirect)
        self.assertIn('/releases/42/assets?name=', seen[0].full_url)
        self.assertEqual(seen[0].method, 'POST')
        with self.assertRaises(ValueError):
            r.upload('../bad', path, {'GITHUB_TOKEN': 'synthetic'})

    def test_web_only_refresh_ignores_partial_rc_archive(self):
        partial = dict(self.release, assets=[{'name': r.ARCHIVE}])
        with patch.object(w.urllib.request, 'urlopen'), patch.object(w.json, 'load', return_value=[partial]):
            with self.assertRaisesRegex(ValueError, 'no durable'):
                w.durable_archive()


class Workflow(unittest.TestCase):
    def test_release_published_trigger_and_maintenance_preserved(self):
        flow = yaml.load((ROOT / '.github/workflows/esp32-alpha.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(flow['on']['release']['types'], ['published'])
        self.assertEqual(set(flow['on']), {'release', 'workflow_dispatch', 'workflow_run'})
        self.assertEqual(flow['permissions'], {'contents': 'read'})
        self.assertEqual(flow['jobs']['build']['strategy']['matrix']['variant'], list(r.VARIANTS))
        self.assertNotIn('GITHUB_TOKEN', str(flow['jobs']['build']))
        for job in ('build', 'publish', 'restore'):
            checkout = next(s for s in flow['jobs'][job]['steps'] if s.get('uses', '').startswith('actions/checkout@'))
            self.assertEqual(checkout['with']['ref'], '${{ needs.authorize.outputs.publisher_sha }}')

    def test_resume_gate_is_fail_closed_for_failed_cancelled_or_skipped_build(self):
        flow = yaml.load((ROOT / '.github/workflows/esp32-alpha.yml').read_text(), Loader=yaml.BaseLoader)
        expression = flow['jobs']['publish']['if']
        self.assertIn('always()', expression)
        # Evaluate the actual bounded GitHub expression across all result combinations.
        for authorized in ('success', 'failure', 'cancelled', 'skipped'):
            for build in ('success', 'failure', 'cancelled', 'skipped'):
                for resume in ('true', 'false'):
                    expr = expression.replace('always()', 'True')
                    for key, value in {'needs.authorize.result': authorized, 'needs.build.result': build,
                                       'needs.authorize.outputs.source_sha': 'a' * 40,
                                       'needs.authorize.outputs.already_published': 'false',
                                       'needs.authorize.outputs.resume_archive': resume}.items():
                        expr = expr.replace(key, repr(value))
                    actual = eval(expr.replace('&&', ' and ').replace('||', ' or '), {'__builtins__': {}})
                    self.assertEqual(actual, authorized == 'success' and
                                     (build == 'success' or (build == 'skipped' and resume == 'true')))

    def test_docker_release_and_c3_uart_contracts_preserved(self):
        flow = yaml.load((ROOT / '.github/workflows/docker-release.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(flow['on']['push']['tags'], ['v*'])
        job = flow['jobs']['build-release-index']
        self.assertEqual(job['strategy']['matrix']['ros_distro'], ['kilted', 'lyrical'])
        build = next(step for step in job['steps'] if step.get('uses', '').startswith('docker/build-push-action@'))
        self.assertEqual(build['with']['platforms'], 'linux/amd64,linux/arm64')
        request = yaml.load((ROOT / '.github/workflows/esp32-request.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertIn('workflow_dispatch', request['on'])


class Pipeline(unittest.TestCase):
    """Real packaging/validation/site/archive path, synthetic tiny IDF binaries."""
    setUp = Assets.setUp
    write_marker = Assets.write_marker
    api = Assets.api
    upload = Assets.upload
    append = Assets.append

    def site(self):
        repo = self.root / 'repository'
        repo.mkdir()
        def git(*args):
            return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
        git('init', '-q', '-b', 'main')
        git('config', 'user.name', 'Synthetic Release')
        git('config', 'user.email', 'test@example.invalid')
        for path in ('embedded/esp32/native/ci.py', 'embedded/esp32/migration/dependencies.json',
                     'embedded/esp32/native/main/gateway.cpp', 'embedded/esp32/native/main/rover.c'):
            file = repo / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('synthetic fixture, not firmware')
        git('add', 'embedded')
        git('commit', '-qm', 'synthetic source')
        self.identity.update(source_sha=git('rev-parse', 'HEAD'), source_tree=git('rev-parse', 'HEAD^{tree}'))
        self.identity['tag_object_sha'] = self.identity['source_sha']
        git('update-ref', 'refs/remotes/origin/main', self.identity['source_sha'])
        git('tag', self.identity['release_tag'])
        self.identity.update(a.source(repo, self.identity['source_sha'], self.identity['release_tag']))
        bundles = []
        for variant in r.VARIANTS:
            fixture = f.Package()
            fixture.setUp()
            self.addCleanup(fixture.doCleanups)
            fixture.qualification.update(source_commit=self.identity['source_sha'],
                source_object=self.identity['source_sha'], source_tree=self.identity['source_tree'], variant=variant)
            fixture.profile = f.p.profiles.from_variant(variant)
            if variant == 's3-gateway':
                fixture.verified.update(profile='gateway', local_gnss=True,
                                        boot_memory={'main_stack_bytes': 32768, 'tls_external': True})
                fixture.qualification['host_tests'] = 'pass'
                fixture.flash['extra_esptool_args']['chip'] = 'esp32s3'
                fixture.flash['flash_settings']['flash_size'] = '16MB'
                (fixture.root / 'sdkconfig').write_text('CONFIG_IDF_TARGET="esp32s3"\n'
                    'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"\nCONFIG_PARTITION_TABLE_OFFSET=0x8000\n'
                    'CONFIG_SPIRAM=y\nCONFIG_SPIRAM_MODE_OCT=y\nCONFIG_SPIRAM_TYPE_ESPPSRAM64=y\n')
                fixture.write_metadata()
                desc = fixture.build / 'project_description.json'
                data = json.loads(desc.read_bytes())
                data['target'] = 'esp32s3'
                desc.write_text(json.dumps(data))
            folder = fixture.root / 'qualified'
            entry = f.p.package(fixture.build, fixture.verified, fixture.profile,
                                self.identity['source_sha'], folder, fixture.qualification)
            (folder / 'catalog-entry.json').write_bytes(a.s.p.json_bytes(entry))
            bundles.append(folder)
        site = self.root / 'actual-site'
        a.stage(repo, self.identity['source_sha'], bundles, site, self.identity['release_tag'],
                release_identity=self.identity)
        (site / 'flasher/publications.json').write_bytes(a.s.p.json_bytes({'schema': 1, 'publications': [
            {'source_sha': self.identity['source_sha'], 'release_tag': self.identity['release_tag'],
             'published_at': self.identity['published_at'], 'channel': 'RC', 'status': 'validated_authorized_rc'}]}))
        return repo, site

    def test_end_to_end_qualified_bundles_release_archive_catalogue_and_online_bytes(self):
        repo, site = self.site()
        self.directory = r.materialize(a, repo, site, self.root / 'real-assets', self.identity)
        self.append()
        with patch.object(w.urllib.request, 'urlopen'), \
                patch.object(w.json, 'load', return_value=[self.release]), \
                patch.object(w.a.v, 'api', side_effect=self.api), \
                patch.object(w.a.v, 'download', side_effect=lambda asset, *args: self.bytes[asset['name']]), \
                patch.object(w.a.v, 'tag_identity', return_value=(self.identity['source_sha'], self.identity['tag_object_sha'])), \
                patch.object(w, 'download', side_effect=lambda url, limit: self.bytes[url.rsplit('/', 1)[1]]):
            self.assertEqual(w.durable_archive(), (self.bytes[r.ARCHIVE], self.identity['release_tag']))
        snapshots = dict(self.bytes)
        self.uploads.clear()
        # On restart reuse the original verified site archive, even if publisher
        # assets changed. The firmware and archived metadata remain byte-identical.
        archive_asset = next(item for item in self.release['assets'] if item['name'] == r.ARCHIVE)
        archive_asset['digest'] = 'sha256:' + hashlib.sha256(self.bytes[r.ARCHIVE]).hexdigest()
        with patch.object(r, 'download', side_effect=lambda asset, *args: self.bytes[asset['name']]), \
                patch.object(a, 'ROOT', repo):
            recovered = r.resume_site(a, self.release, self.identity, self.root / 'resumed-site')
        resumed_assets = r.materialize(a, repo, recovered, self.root / 'resumed-assets', self.identity)
        self.directory = resumed_assets
        self.append()
        self.assertEqual(self.uploads, [])
        self.assertEqual(self.bytes, snapshots)
        with patch('urllib.request.urlopen', side_effect=lambda url, timeout:
                   io.BytesIO((site / url.split('/universal-gnss/')[1]).read_bytes())):
            result = a.verify_online(site, 'https://pepeuch.github.io/universal-gnss/')
        self.assertGreater(result['verified_public_files'], 20)
        for entry in a.s.p.load(site / 'flasher/catalog.json')['images']:
            self.assertEqual(entry['source_sha'], self.identity['source_sha'])
            self.assertEqual(entry['nvs_preservation'], 'not_qualified')

    def test_changed_qualification_blocks_archival_before_upload(self):
        repo, site = self.site()
        entry = a.s.p.load(site / 'flasher/catalog.json')['images'][0]
        (site / entry['manifest']).write_text('{}')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            r.materialize(a, repo, site, self.root / 'invalid-assets', self.identity)
        self.assertEqual(self.uploads, [])


if __name__ == '__main__':
    unittest.main()
