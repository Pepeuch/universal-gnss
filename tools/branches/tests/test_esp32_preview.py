"""Branch preview authority and durable storage: no real GitHub writes or SDK."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import urllib.error
import yaml

ROOT = Path(__file__).resolve().parents[3]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


p = load('preview_test', 'tools/branches/esp32_preview.py')
f = load('preview_package', 'tools/branches/tests/test_firmware_package.py')
pipeline = load('preview_pipeline', 'tools/branches/tests/test_esp32_release.py')


class Preview(unittest.TestCase):
    def setUp(self):
        self.run = {'id': 23, 'path': '.github/workflows/esp32.yml', 'event': 'push',
                    'status': 'completed', 'conclusion': 'success', 'head_sha': 'a' * 40,
                    'head_branch': 'feature/rtcm', 'head_repository': {'full_name': p.a.REPO},
                    'updated_at': '2026-11-16T22:00:00Z'}

    def api(self, path):
        if path == 'actions/runs/23': return copy.deepcopy(self.run)
        if path.startswith('branches/'): return {'commit': {'sha': 'b' * 40}}
        if '/artifacts?' in path: return {'artifacts':[{'name': 'native-' + name} for name in p.VARIANTS]}
        raise AssertionError(path)

    def test_successful_feature_source_does_not_need_main_or_register_approval(self):
        with patch.object(p.r, 'api', side_effect=self.api), \
                patch.object(p.a, 'source', return_value={'source_sha': 'a' * 40, 'source_tree': 'c' * 40}) as source, \
                patch.object(p.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)):
            identity = p.authorize(ROOT, 23)
        self.assertEqual(identity['branch'], 'feature/rtcm')
        self.assertTrue(source.call_args.kwargs['development'])

    def test_fork_pr_failed_skipped_cancelled_or_wrong_workflow_cannot_publish(self):
        good = copy.deepcopy(self.run)
        for key, value in (('event', 'pull_request'), ('conclusion', 'failure'), ('conclusion', 'cancelled'),
                           ('conclusion', 'skipped'), ('status', 'in_progress'), ('path', '.github/workflows/other.yml'),
                           ('head_repository', {'full_name': 'fork/universal-gnss'})):
            self.run = dict(good, **{key: value})
            with patch.object(p.r, 'api', side_effect=self.api), self.assertRaises(ValueError):
                p.authorize(ROOT, 23)

    def test_force_replaced_source_is_rejected_but_documentation_descendant_is_allowed(self):
        with patch.object(p.r, 'api', side_effect=self.api), \
                patch.object(p.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)):
            with self.assertRaisesRegex((ValueError, subprocess.CalledProcessError), 'branch|Command'):
                p.authorize(ROOT, 23)

    def test_documentation_only_or_old_run_without_bundles_does_not_deploy(self):
        def api(path): return {'artifacts':[]} if '/artifacts?' in path else self.api(path)
        with patch.object(p.r, 'api', side_effect=api):
            self.assertFalse(p.available_run(23))
        with patch.object(p.r, 'api', side_effect=self.api):
            self.assertTrue(p.available_run(23))

    def test_partial_artifact_matrix_is_not_success(self):
        def api(path): return {'artifacts':[{'name': 'native-s3-gateway'}]} if '/artifacts?' in path else self.api(path)
        with patch.object(p.r, 'api', side_effect=api), self.assertRaisesRegex(ValueError, 'incomplete'):
            p.available_run(23)

    def test_collector_drains_another_branch_even_if_its_callback_was_replaced(self):
        runs=[{'id':n,'head_branch':branch,'head_repository':{'full_name':p.a.REPO},'path':'.github/workflows/esp32.yml','event':'push','conclusion':'success'}
              for n,branch in ((30,'docs-only'),(29,'feature/a'),(28,'dev'),(27,'feature/a'))]
        published=[]
        def publish(repo,run_id,bundles,output):
            published.append(run_id);output.mkdir(parents=True)
            (output/'verified').write_text(str(run_id));return output
        with tempfile.TemporaryDirectory() as temp, patch.object(p.r,'api',side_effect=lambda path:
                    {'artifacts':[{'workflow_run':{'id':run['id']},'expired':run['id']==30} for run in runs]} if path.startswith('actions/artifacts?') else
                    next(run for run in runs if run['id']==int(path.rsplit('/',1)[1])) if path.startswith('actions/runs/') else {'id':99}), \
                patch.object(p,'available_run',side_effect=lambda run_id:run_id!=30), \
                patch.object(p,'storage',return_value={'id':99}), \
                patch.object(p,'assets',return_value={'esp32-ci-28.json':{}}), \
                patch.object(p,'publish',side_effect=publish),patch.object(p.subprocess,'run') as download:
            output=Path(temp)/'site'
            self.assertTrue(p.collect(ROOT,output))
            self.assertEqual(published,[29]);self.assertEqual(download.call_count,3)
            self.assertEqual((output/'verified').read_text(),'29')

    def test_workflow_separates_unprivileged_builds_and_trusted_data_only_publisher(self):
        producer = yaml.load((ROOT / '.github/workflows/esp32.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(producer['permissions'], {'contents': 'read'})
        self.assertNotIn('secrets.', json.dumps(producer))
        self.assertIn('--bundle-output', producer['jobs']['firmware']['steps'][1]['run'])
        flow = yaml.load((ROOT / '.github/workflows/esp32-alpha.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertNotIn('branches', flow['on']['workflow_run'])
        self.assertIn("github.ref == 'refs/heads/main'", flow['jobs']['authorize']['if'])
        preview = flow['jobs']['preview']
        self.assertEqual(preview['needs'], ['authorize', 'publish', 'restore'])
        self.assertIn('always()', preview['if'])
        self.assertIn("github.repository == 'Pepeuch/universal-gnss'", preview['if'])
        self.assertEqual(preview['steps'][0]['with']['ref'], 'main')
        steps = str(preview['steps'])
        self.assertIn('--collect', steps)
        self.assertNotIn('native/ci.py', steps)
        self.assertNotIn('idf.py', steps)
        for step in preview['steps'][2:]:
            if step.get('id') != 'firmware':
                self.assertEqual(step['if'], "steps.firmware.outputs.ready == 'true'")


class EndToEnd(unittest.TestCase):
    def test_nonmain_bundles_archive_history_and_rerun(self):
        fixture = pipeline.Pipeline()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        repo, old = fixture.site()
        def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
        base = git('rev-parse', 'HEAD')
        origin = fixture.root / 'origin.git'
        subprocess.run(['git', 'init', '-q', '--bare', str(origin)], check=True)
        git('remote', 'add', 'origin', str(origin))
        git('tag', p.TAG)
        git('push', '-q', 'origin', 'main', '--tags')
        git('checkout', '-qb', 'feature/rtcm')
        (repo / 'code.cpp').write_text('int new_branch_code;')
        git('add', 'code.cpp'); git('commit', '-qm', 'feature code')
        sha, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
        with self.assertRaises(ValueError): p.a.source(repo, sha)
        identity = {**p.a.source(repo, sha, development=True), 'branch': 'feature/rtcm',
                    'run_id': 23, 'channel': 'DEV', 'finished_at': '2026-11-16T22:00:00Z'}
        bundles = []
        for variant in p.VARIANTS:
            item = f.Package(); item.setUp(); self.addCleanup(item.doCleanups)
            item.qualification.update(source_commit=sha, source_object=sha, source_tree=tree, variant=variant)
            item.profile = f.p.profiles.from_variant(variant)
            if variant == 's3-gateway':
                item.verified.update(profile='gateway', local_gnss=True,
                                     boot_memory={'main_stack_bytes': 32768, 'tls_external': True})
                item.qualification['host_tests'] = 'pass'
                item.flash['extra_esptool_args']['chip'] = 'esp32s3'
                item.flash['flash_settings']['flash_size'] = '16MB'
                (item.root / 'sdkconfig').write_text('CONFIG_IDF_TARGET="esp32s3"\nCONFIG_ESPTOOLPY_FLASHSIZE="16MB"\n'
                    'CONFIG_PARTITION_TABLE_OFFSET=0x8000\nCONFIG_SPIRAM=y\nCONFIG_SPIRAM_MODE_OCT=y\nCONFIG_SPIRAM_TYPE_ESPPSRAM64=y\n')
                item.write_metadata()
                path = item.build / 'project_description.json'; desc = json.loads(path.read_bytes())
                desc['target'] = 'esp32s3'; path.write_text(json.dumps(desc))
            elif variant == 'c3-rover-uart':
                item.verified['rover_output'] = {'transport': 'uart1-tx', 'tx_pin': 4, 'baud': 115200}
            out = item.root / 'qualified'
            entry = f.p.package(item.build, item.verified, item.profile, sha, out, item.qualification)
            (out / 'catalog-entry.json').write_bytes(p.a.s.p.json_bytes(entry))
            bundles.append(out)
        release = {'id': 99, 'tag_name': p.TAG, 'html_url': 'https://github.com/' + p.a.REPO + '/releases/tag/' + p.TAG}
        stored, uploads = {}, []
        fail_source_once=[True]
        def file_map():
            return {name: {'id': i + 1, 'name': name, 'digest': 'sha256:' + p.hashlib.sha256(data).hexdigest()}
                    for i, (name, data) in enumerate(stored.items())}
        def upload(release_id, path, env):
            self.assertEqual(release_id, 99); self.assertNotIn(path.name, stored)
            if path.name.endswith('.tar.gz') and fail_source_once[0]:
                fail_source_once[0]=False
                raise OSError('simulated interrupted source upload')
            uploads.append(path.name); stored[path.name] = path.read_bytes()
        real_refresh = p.history.w.refresh
        def refresh(data, tag, output, repo=repo):
            return old if data == b'existing-verified-site' else real_refresh(data, tag, output, repo)
        original = {str(path.relative_to(old)): path.read_bytes() for path in (old / 'flasher/published').rglob('*') if path.is_file()}
        with patch.object(p, 'authorize', return_value=identity), patch.object(p, 'storage', return_value=release), \
                patch.object(p, 'assets', side_effect=lambda _: file_map()), \
                patch.object(p.r, 'tag_identity', return_value=(base, base)), \
                patch.object(p.r, 'upload', side_effect=upload), \
                patch.object(p.r, 'download', side_effect=lambda item, *args: stored[item['name']]), \
                patch.object(p.history.w, 'durable_archive', return_value=(b'existing-verified-site', 'v0.7.5-RC1')), \
                patch.object(p.history.w, 'refresh', side_effect=refresh):
            with self.assertRaisesRegex(OSError,'interrupted'):
                p.publish(repo,23,bundles,fixture.root/'incomplete-site')
            self.assertEqual(len(stored),1);self.assertNotIn('esp32-ci-23.json',stored)
            output = p.publish(repo, 23, [], fixture.root / 'preview-site')
            self.assertEqual(uploads[-1], 'esp32-ci-23.json')
            self.assertEqual(len(uploads), 3)
            mapping = p.a.s.p.load(output / 'flasher/branches.json')
            self.assertEqual(mapping['branches']['feature/rtcm']['source_sha'], sha)
            self.assertEqual(len(p.a.s.p.load(output / 'flasher/catalog.json')['images']), 5)
            for name, data in original.items(): self.assertEqual((output / name).read_bytes(), data)
            again = p.publish(repo, 23, [], fixture.root / 'rerun')
            self.assertEqual(len(uploads), 3)
            self.assertEqual((again / 'flasher/branches.json').read_bytes(), (output / 'flasher/branches.json').read_bytes())
            self.site = again  # Optional real Chromium runner before doCleanups().
            with patch('urllib.request.urlopen', side_effect=lambda url, timeout:
                       io.BytesIO((again / url.split('/universal-gnss/')[1]).read_bytes())):
                result=p.a.verify_online(again,'https://pepeuch.github.io/universal-gnss/')
            self.assertGreater(result['verified_public_files'],40)
            # Corrupt durable data must be rejected, never silently truncate history.
            stored['esp32-ci-site-23.zip'] += b'corrupt'
            with self.assertRaisesRegex(ValueError, 'checksum'):
                p.publish(repo, 23, [], fixture.root / 'corrupted')


if __name__ == '__main__':
    unittest.main()
