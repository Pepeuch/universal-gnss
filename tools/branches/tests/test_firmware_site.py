"""Local static staging is not a Pages deployment or a firmware publication."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
SPEC=importlib.util.spec_from_file_location('firmware_site',ROOT/'tools/branches/prepare_esp32_site.py')
s=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(s)
FIX=importlib.util.spec_from_file_location('site_package_fixture',ROOT/'tools/branches/tests/test_firmware_package.py')
f=importlib.util.module_from_spec(FIX);FIX.loader.exec_module(f)
class Site(unittest.TestCase):
    def test_empty_review_site_is_source_only_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as root:
            dest=s.prepare(Path(root)/'site',{'schema':1,'sources':[]})
            self.assertTrue((dest/'flasher.html').exists());self.assertTrue((dest/'.nojekyll').exists())
            self.assertEqual(s.p.load(dest/'flasher/catalog.json'),{'schema':2,'images':[]})
            self.assertFalse((dest/'flasher/published').exists())
            with self.assertRaises(ValueError):s.prepare(dest,{'schema':1,'sources':[]})
    def test_unapproved_bundle_and_archive_corruption_refused(self):
        fixture=f.Package();fixture.setUp();self.addCleanup(fixture.doCleanups)
        fixture.verified['rover_output']['physical_delivery']='NOT_TESTED'
        entry=fixture.package();root=fixture.root/'out';(root/'catalog-entry.json').write_text(json.dumps(entry))
        approval={'schema':1,'sources':[{'source_sha':'a'*40,'source_tree':'b'*40,'review':'synthetic','publication':'main_approved'}]}
        self.assertEqual(s.inspect_bundle(root,approval)[0],entry)
        bad=copy.deepcopy(approval);bad['sources'][0]['publication']='not_authorized'
        with self.assertRaises(ValueError):s.inspect_bundle(root,bad)
        archive=root/'published'/entry['build_key']/'bundle.zip';archive.write_bytes(b'invalid')
        with self.assertRaises(ValueError):s.inspect_bundle(root,approval)
    def test_main_ancestry_and_tree_are_real_git_checks(self):
        with tempfile.TemporaryDirectory() as root:
            repo=Path(root)
            def git(*args):return subprocess.check_output(['git','-C',root,*args],text=True).strip()
            git('init','-q','-b','main');git('config','user.name','Synthetic Site Test');git('config','user.email','test@example.invalid')
            (repo/'file').write_text('main');git('add','file');git('commit','-qm','fixture')
            main=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
            s.require_main(repo,main,tree,'main')
            git('checkout','-qb','feature');(repo/'file').write_text('feature');git('commit','-qam','feature')
            with self.assertRaises(ValueError):s.require_main(repo,git('rev-parse','HEAD'),git('rev-parse','HEAD^{tree}'),'main')
            with self.assertRaises(ValueError):s.require_main(repo,main,'a'*40,'main')
    def test_profile_sdk_and_qualified_sizes_cannot_be_relabelled(self):
        fixture=f.Package();fixture.setUp();self.addCleanup(fixture.doCleanups)
        entry=fixture.package();root=fixture.root/'out';(root/'catalog-entry.json').write_text(json.dumps(entry))
        approval={'schema':1,'sources':[{'source_sha':'a'*40,'source_tree':'b'*40,'review':'synthetic','publication':'main_approved'}]}
        path=root/'published'/entry['build_key']/'provenance.json';original=path.read_bytes()
        for field,value in [('sdk_image','local:unqualified'),('sha256','0'*64),('profile',s.p.profiles.from_variant('c3-rover-uart')),('part_sizes',{})]:
            data=json.loads(original);data[field]=value;path.write_text(json.dumps(data))
            with self.assertRaises(ValueError):s.inspect_bundle(root,approval)
            path.write_bytes(original)
    def test_malformed_approvals_never_stage_a_site(self):
        with tempfile.TemporaryDirectory() as root:
            for data in ({'schema':True,'sources':[]},{'schema':1,'sources':[{}]},{'schema':1,'sources':[],'other':1}):
                with self.assertRaises(ValueError):s.prepare(Path(root)/'site',data)
                self.assertFalse((Path(root)/'site').exists())
if __name__=='__main__':unittest.main()
