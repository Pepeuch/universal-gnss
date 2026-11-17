"""History retains immutable payloads and public verification checks actual bytes."""
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('catalog_history',ROOT/'tools/branches/esp32_catalog_history.py')
h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
spec=importlib.util.spec_from_file_location('history_fixture',ROOT/'tools/branches/tests/test_firmware_package.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)

class History(unittest.TestCase):
    def setup_sites(self):
        fixture=f.Package();fixture.setUp();self.addCleanup(fixture.doCleanups)
        sites=[]
        for i,sha in enumerate(('a'*40,'c'*40)):
            q=dict(fixture.qualification,source_commit=sha,source_object=sha)
            entry=f.p.package(fixture.build,fixture.verified,fixture.profile,sha,fixture.root/('bundle'+str(i)),q)
            bundle=fixture.root/('bundle'+str(i));(bundle/'catalog-entry.json').write_text(json.dumps(entry))
            approval={'schema':1,'sources':[{'source_sha':sha,'source_tree':'b'*40,'publication':'main_approved','review':'synthetic'}]}
            with patch.object(h.w.a.s,'require_main'):
                site=h.w.a.s.prepare(fixture.root/('site'+str(i)),approval,[bundle])
            (site/'alpha-publication.json').write_text(json.dumps({'source_sha':sha,'source_tree':'b'*40,'release_tag':'synthetic-'+str(i)}))
            sites.append(site)
        (sites[0]/'flasher/publications.json').write_text(json.dumps({'schema':1,'publications':[{'source_sha':'a'*40,'published_at':'2026-01-01T00:00:00Z','status':'validated_authorized_alpha','release_tag':'synthetic-0'}]}))
        return sites
    def test_old_and_new_versions_and_binary_bytes_survive(self):
        old,new=self.setup_sites()
        snapshot={str(p.relative_to(old)):p.read_bytes() for p in (old/'flasher/published').rglob('*') if p.is_file()}
        with patch.object(h.w,'durable_archive',return_value=(b'validated-archive','synthetic-0')),patch.object(h.w,'refresh',return_value=old):h.merge(new)
        catalog=json.loads((new/'flasher/catalog.json').read_text())
        self.assertEqual([e['source_sha'] for e in catalog['images']],['c'*40,'a'*40])
        self.assertEqual(len(json.loads((new/'flasher/approved-sources.json').read_text())['sources']),2)
        for name,data in snapshot.items():self.assertEqual((new/name).read_bytes(),data)
        for entry in catalog['images']:
            self.assertEqual(entry['nvs_preservation'],'not_qualified')
            self.assertEqual(entry['install_mode'],'full')
    def test_invalid_durable_source_cannot_drop_old_catalogue(self):
        old,new=self.setup_sites();before=(new/'flasher/catalog.json').read_bytes()
        with patch.object(h.w,'durable_archive',side_effect=ValueError('bad checksum')):
            with self.assertRaises(ValueError):h.merge(new)
        self.assertEqual(before,(new/'flasher/catalog.json').read_bytes())
    def test_active_branch_rotates_preview_without_deleting_the_old_archive(self):
        old,new=self.setup_sites()
        for site,sha in ((old,'a'*40),(new,'c'*40)):
            path=site/'flasher/approved-sources.json';register=json.loads(path.read_bytes())
            register['sources'][0]['publication']='ci_preview';path.write_text(json.dumps(register))
            path=site/'alpha-publication.json';meta=json.loads(path.read_bytes())
            meta.update(channel='DEV',branch='feature/rtcm');path.write_text(json.dumps(meta))
        (old/'flasher/branches.json').write_text(json.dumps({'schema':1,'branches':{'feature/rtcm':{'source_sha':'a'*40}}}))
        archived={str(path.relative_to(old)):path.read_bytes() for path in old.rglob('*') if path.is_file()}
        with patch.object(h.w,'durable_archive',return_value=(b'verified','technical')),patch.object(h.w,'refresh',return_value=old):h.merge(new)
        images=json.loads((new/'flasher/catalog.json').read_bytes())['images']
        self.assertEqual({entry['source_sha'] for entry in images},{'c'*40})
        for name,data in archived.items():self.assertEqual((old/name).read_bytes(),data)
    def test_rc_of_an_existing_source_updates_label_and_fixed_date_without_payload_change(self):
        _,new=self.setup_sites();old=new.parent/'previous';shutil.copytree(new,old)
        (old/'flasher/publications.json').write_text(json.dumps({'schema':1,'publications':[
            {'source_sha':'c'*40,'published_at':'2026-01-01T00:00:00Z','channel':'ALPHA','status':'published','release_tag':'old-alpha'}]}))
        meta=json.loads((new/'alpha-publication.json').read_bytes())
        meta.update(channel='RC',release_tag='v0.7.5-RC1',published_at='2026-11-16T22:06:51Z')
        (new/'alpha-publication.json').write_text(json.dumps(meta))
        before={str(p.relative_to(new)):p.read_bytes() for p in (new/'flasher/published').rglob('*') if p.is_file()}
        with patch.object(h.w,'durable_archive',return_value=(b'verified','old-alpha')),patch.object(h.w,'refresh',return_value=old):h.merge(new)
        rows=json.loads((new/'flasher/publications.json').read_bytes())['publications']
        self.assertEqual(len(rows),1);self.assertEqual(rows[0]['channel'],'RC')
        self.assertEqual(rows[0]['published_at'],meta['published_at']);self.assertEqual(rows[0]['release_tag'],meta['release_tag'])
        for name,data in before.items():self.assertEqual((new/name).read_bytes(),data)
        # A Release can be published before native CI finishes. Its fixed Release
        # date may predate the CI archive, but formal RC metadata must still win.
        previous=json.loads((old/'flasher/publications.json').read_bytes())
        previous['publications'][0].update(channel='DEV',published_at='2026-11-17T00:00:00Z')
        (old/'flasher/publications.json').write_text(json.dumps(previous))
        with patch.object(h.w,'durable_archive',return_value=(b'verified','old-alpha')),patch.object(h.w,'refresh',return_value=old):h.merge(new)
        row=json.loads((new/'flasher/publications.json').read_bytes())['publications'][0]
        self.assertEqual(row['channel'],'RC');self.assertEqual(row['published_at'],meta['published_at'])
    def test_public_verification_fetches_and_compares_manifest_and_binary_bytes(self):
        old,new=self.setup_sites()
        with patch.object(h.w.a.s.p,'load',wraps=h.w.a.s.p.load),patch('urllib.request.urlopen',side_effect=lambda url,timeout:io.BytesIO((new/url.split('/universal-gnss/')[1]).read_bytes())):
            result=h.w.a.verify_online(new,'https://pepeuch.github.io/universal-gnss/')
            self.assertGreater(result['verified_public_files'],10)
        with patch('urllib.request.urlopen',return_value=io.BytesIO(b'wrong bytes')),patch('time.sleep'):
            with self.assertRaises(ValueError):h.w.a.verify_online(new,'https://pepeuch.github.io/universal-gnss/')

if __name__=='__main__':unittest.main()
