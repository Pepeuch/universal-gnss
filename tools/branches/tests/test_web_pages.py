"""Web-only refresh must never mint firmware or discard a catalogue."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import yaml

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('web_pages',ROOT/'tools/branches/refresh_esp32_site.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
spec=importlib.util.spec_from_file_location('web_fixture',ROOT/'tools/branches/tests/test_firmware_package.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)

class Pages(unittest.TestCase):
    def fixture(self):
        fixture=f.Package();fixture.setUp();self.addCleanup(fixture.doCleanups)
        entry=fixture.package(); bundle=fixture.root/'out'
        approval={'schema':1,'sources':[{'source_sha':'a'*40,'source_tree':'b'*40,'review':'synthetic','publication':'main_approved'}]}
        (bundle/'catalog-entry.json').write_text(json.dumps(entry))
        with patch.object(w.a.s,'require_main'):
            site=w.a.s.prepare(fixture.root/'site',approval,[bundle])
        (site/'alpha-publication.json').write_text(json.dumps({'source_sha':'a'*40,'source_tree':'b'*40,'release_tag':'synthetic'}))
        return fixture,site,entry
    def archive(self,site):
        data=io.BytesIO()
        with zipfile.ZipFile(data,'w') as z:
            for file in site.rglob('*'):
                if file.is_file():z.write(file,file.relative_to(site))
        return data.getvalue()
    def refresh(self,site,out):
        with patch.object(w.a,'source',return_value={'source_tree':'b'*40}),patch.object(w.a.s,'require_main'),patch.object(w.a,'remote_tag',return_value='a'*40):
            return w.refresh(self.archive(site),'synthetic',out)
    def test_web_only_preserves_every_firmware_byte_and_catalogue(self):
        fixture,site,entry=self.fixture()
        (site/'flasher/app.js').write_text('old-interface')
        out=self.refresh(site,fixture.root/'updated')
        for name in ('catalog.json','approved-sources.json'):
            self.assertEqual((out/'flasher'/name).read_bytes(),(site/'flasher'/name).read_bytes())
        for file in (site/'flasher/published').rglob('*'):
            if file.is_file():self.assertEqual(file.read_bytes(),(out/file.relative_to(site)).read_bytes())
        self.assertNotEqual((out/'flasher/app.js').read_bytes(),(site/'flasher/app.js').read_bytes())
        self.assertIn('app.js?v=',(out/'flasher.html').read_text())
    def test_missing_empty_or_unapproved_catalogue_rejected(self):
        fixture,site,entry=self.fixture()
        for value in ({'schema':2,'images':[]},{'schema':2,'images':[dict(entry,source_sha='c'*40)]}):
            (site/'flasher/catalog.json').write_text(json.dumps(value))
            with self.assertRaises(ValueError):self.refresh(site,fixture.root/'bad')
            self.assertFalse((fixture.root/'bad').exists())
        (site/'flasher/catalog.json').unlink()
        with self.assertRaises(FileNotFoundError):self.refresh(site,fixture.root/'bad')
    def test_corrupt_manifest_or_part_rejected(self):
        fixture,site,entry=self.fixture()
        path=site/entry['manifest'];path.write_text('{}')
        with self.assertRaises(ValueError):self.refresh(site,fixture.root/'bad')
    def test_archive_traversal_and_duplicates_rejected(self):
        for names in (['../escape'],['same','same']):
            b=io.BytesIO()
            with zipfile.ZipFile(b,'w') as z:
                for name in names:z.writestr(name,b'x')
            with tempfile.TemporaryDirectory() as d:
                with self.assertRaises(ValueError):w.unpack(b.getvalue(),Path(d))
    def test_workflow_main_only_shared_lock_no_build_or_release(self):
        flow=yaml.load((ROOT/'.github/workflows/esp32-web-pages.yml').read_text(),Loader=yaml.BaseLoader)
        alpha=yaml.load((ROOT/'.github/workflows/esp32-alpha.yml').read_text(),Loader=yaml.BaseLoader)
        self.assertEqual(flow['on']['push']['branches'],['main'])
        self.assertEqual(flow['concurrency'],alpha['concurrency'])
        self.assertEqual(flow['permissions'],{'contents':'read'})
        job=flow['jobs']['deploy'];self.assertIn("refs/heads/main",job['if'])
        self.assertEqual(job['permissions']['contents'],'read')
        commands=str(job['steps'])
        self.assertNotIn('native/ci.py',commands);self.assertNotIn('esp32_alpha.py release',commands)
        self.assertIn('refresh_esp32_site.py',commands)
    def test_absent_durable_source_fails_without_fallback(self):
        with patch.object(w.urllib.request,'urlopen'),patch.object(w.json,'load',return_value=[]):
            with self.assertRaisesRegex(ValueError,'no durable'):w.durable_archive()
    def test_durable_checksum_mismatch_fails(self):
        release={'draft':False,'prerelease':True,'published_at':'2026-11-16','tag_name':'synthetic',
                 'assets':[{'name':n,'browser_download_url':'https://github.com/Pepeuch/universal-gnss/releases/download/synthetic/'+n} for n in ('esp32-alpha-site.zip','SHA256SUMS')]}
        with patch.object(w.urllib.request,'urlopen'),patch.object(w.json,'load',return_value=[release]),patch.object(w,'download',side_effect=[b'archive',b'0'*64+b'  esp32-alpha-site.zip']):
            with self.assertRaisesRegex(ValueError,'checksum mismatch'):w.durable_archive()
    def test_durable_releases_include_alpha_beta_and_stable_without_channel_filter(self):
        import hashlib
        for prerelease,tag in [(True,'alpha-tag'),(True,'beta-tag'),(False,'v1.0.0')]:
            data=b'archive';digest=hashlib.sha256(data).hexdigest()
            release={'draft':False,'prerelease':prerelease,'published_at':'2026-11-16','tag_name':tag,
                     'assets':[{'name':n,'browser_download_url':'https://github.com/Pepeuch/universal-gnss/releases/download/'+tag+'/'+n} for n in ('esp32-site.zip','SHA256SUMS')]}
            with patch.object(w.urllib.request,'urlopen'),patch.object(w.json,'load',return_value=[release]),patch.object(w,'download',side_effect=[data,(digest+'  esp32-site.zip').encode()]):
                self.assertEqual(w.durable_archive(),(data,tag))
    def test_resource_cache_key_changes_with_content(self):
        with tempfile.TemporaryDirectory() as d:
            site=Path(d);(site/'flasher').mkdir()
            for name in ('app.js','portable.js','provision.js'):(site/'flasher'/name).write_text('one')
            html='<script src="flasher/app.js"></script>'
            (site/'flasher.html').write_text(html);w.cache_bust(site);one=(site/'flasher.html').read_text()
            (site/'flasher/app.js').write_text('two');(site/'flasher.html').write_text(html);w.cache_bust(site)
            self.assertNotEqual(one,(site/'flasher.html').read_text())

if __name__=='__main__':unittest.main()
