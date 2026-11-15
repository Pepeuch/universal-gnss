"""Manual alpha authority, real-build coupling and no release during unit tests."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import yaml
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('alpha',ROOT/'tools/branches/esp32_alpha.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
class Alpha(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        def git(*args):return subprocess.check_output(['git','-C',str(self.root),*args],text=True).strip()
        self.git=git;git('init','-q','-b','main');git('config','user.name','Alpha fixture');git('config','user.email','test@example.invalid')
        for path in ('embedded/esp32/native/ci.py','embedded/esp32/migration/dependencies.json','embedded/esp32/native/main/gateway.cpp','embedded/esp32/native/main/rover.c'):
            file=self.root/path;file.parent.mkdir(parents=True,exist_ok=True);file.write_text('synthetic test only')
        git('add','embedded');git('commit','-qm','fixture');self.sha=git('rev-parse','HEAD');git('update-ref','refs/remotes/origin/main',self.sha)
    def test_source_must_be_exact_main_commit_and_existing_tag_cannot_move(self):
        self.assertEqual(a.source(self.root,self.sha)['release_tag'],'esp32-alpha-'+self.sha[:12])
        for bad in ('main','a'*39,'--upload-pack=evil','A'*40):
            with self.assertRaises((ValueError,subprocess.SubprocessError)):a.source(self.root,bad)
        self.git('tag','0.7.5-a01');self.assertEqual(a.source(self.root,self.sha,'0.7.5-a01')['source_sha'],self.sha)
        self.git('checkout','-qb','feature');(self.root/'changed').write_text('x');self.git('add','changed');self.git('commit','-qm','feature')
        head=self.git('rev-parse','HEAD')
        with self.assertRaises(ValueError):a.source(self.root,head)
        self.git('update-ref','refs/remotes/origin/main',head)
        with self.assertRaisesRegex(ValueError,'another commit'):a.source(self.root,head,'0.7.5-a01')
        with self.assertRaises(ValueError):a.source(self.root,head,'v0.7.5')
    def test_stage_requires_both_profiles_and_generates_register_from_same_authority(self):
        roots=[self.root/'s3',self.root/'c3'];profiles=iter(a.VARIANTS);seen=[]
        roots[0].mkdir();(roots[0]/'qualification.json').write_text(json.dumps({'binary':{'boot_memory':{'main_stack_bytes':32768,'tls_external':True}}}))
        def inspect(root,register):
            seen.append(register);return {'source_sha':self.sha,'profile':{'variant':next(profiles)}},root,None
        def prepare(output,register,bundles,repo):
            output.mkdir();return output
        dest=self.root/'staged'
        with patch.object(a.s,'inspect_bundle',side_effect=inspect),patch.object(a.s,'prepare',side_effect=prepare):
            identity,output=a.stage(self.root,self.sha,roots,dest)
        self.assertEqual(identity['source_sha'],self.sha);self.assertTrue((output/'alpha-publication.json').exists())
        self.assertTrue(all(row['sources'][0]['source_sha']==self.sha and row['sources'][0]['publication']=='main_approved' for row in seen))
        with self.assertRaises(ValueError):a.stage(self.root,self.sha,[],self.root/'empty')
        with patch.object(a.s,'inspect_bundle',return_value=({'source_sha':self.sha,'profile':{'variant':'s3-gateway'}},roots[0],None)):
            with self.assertRaises(ValueError):a.stage(self.root,self.sha,roots,self.root/'duplicate')
        (roots[0]/'qualification.json').write_text(json.dumps({'binary':{}}))
        with patch.object(a.s,'inspect_bundle',return_value=({'source_sha':self.sha,'profile':{'variant':'s3-gateway'}},roots[0],None)):
            with self.assertRaisesRegex(ValueError,'memory corrections'):a.stage(self.root,self.sha,roots,self.root/'unsafe')
    def test_release_rejects_every_untrusted_event_before_io_or_publication(self):
        for env in ({},{'GITHUB_EVENT_NAME':'pull_request','GITHUB_REF':'refs/heads/main','GITHUB_REPOSITORY':a.REPO},{'GITHUB_EVENT_NAME':'workflow_dispatch','GITHUB_REF':'refs/heads/dev','GITHUB_REPOSITORY':a.REPO}):
            with patch.object(a.subprocess,'run') as execute:
                with self.assertRaises(ValueError):a.release(self.root,None,None,env)
                execute.assert_not_called()
    def test_workflow_is_manual_main_two_real_builds_and_durable_before_pages(self):
        flow=yaml.load((ROOT/'.github/workflows/esp32-alpha.yml').read_text(),Loader=yaml.BaseLoader)
        self.assertEqual(set(flow['on']),{'workflow_dispatch'});self.assertEqual(flow['permissions'],{'contents':'read'})
        self.assertEqual(flow['jobs']['build']['strategy']['matrix']['variant'],list(a.VARIANTS))
        build=flow['jobs']['build'];self.assertIn('refs/heads/main',build['steps'][0]['run']);self.assertNotIn('if',build)
        self.assertIn('native/ci.py',build['steps'][2]['run']);self.assertIn('--bundle-output',build['steps'][2]['run'])
        job=flow['jobs']['publish'];self.assertEqual(job['needs'],'build');self.assertNotIn('if',job)
        self.assertEqual(job['permissions'],{'contents':'write','pages':'write','id-token':'write'})
        self.assertEqual(job['environment']['name'],'github-pages')
        commands=str(job['steps']);self.assertLess(commands.index('esp32_alpha.py release'),commands.index('deploy-pages@'))
        self.assertIn('verify-online',commands);self.assertNotIn('issues',str(flow['on']))
        self.assertNotIn('HARDWARE_REQUIRED',commands);self.assertNotIn('firmware_request.py',commands)
        self.assertTrue(all('${{' not in step.get('run','') for step in build['steps']+job['steps']))
    def test_archive_is_deterministic_and_never_includes_a_symlink(self):
        root=self.root/'site';root.mkdir();(root/'file').write_bytes(b'real bytes')
        one=self.root/'one.zip';two=self.root/'two.zip';a.deterministic_zip(root,one);a.deterministic_zip(root,two)
        self.assertEqual(one.read_bytes(),two.read_bytes());(root/'link').symlink_to(root/'file')
        with self.assertRaises(ValueError):a.deterministic_zip(root,self.root/'bad.zip')
    def test_no_mutable_publication_or_shared_write_token_in_build(self):
        code=(ROOT/'tools/branches/esp32_alpha.py').read_text()
        self.assertNotIn('--clobber',code);self.assertNotIn('force-push',code);self.assertIn('immutable release asset conflict',code)
        self.assertIn("actual!=identity['source_sha']",code)
        self.assertIn("'--verify-tag'",code)
        flow=yaml.load((ROOT/'.github/workflows/esp32-alpha.yml').read_text(),Loader=yaml.BaseLoader)
        self.assertNotIn('GITHUB_TOKEN',str(flow['jobs']['build']))
if __name__=='__main__':unittest.main()
