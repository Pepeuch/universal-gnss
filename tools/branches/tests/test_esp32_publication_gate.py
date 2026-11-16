"""Automatic publication never implies permission from a green workflow alone."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('publication_gate',ROOT/'tools/branches/esp32_publication_gate.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)

class Gate(unittest.TestCase):
    sha='a'*40
    def runs(self):
        return [{'id':i,'run_attempt':1,'head_sha':self.sha,'head_branch':'main','event':'push','path':'.github/workflows/'+name,'status':'completed','conclusion':'success'} for i,name in enumerate(g.REQUIRED)]
    def test_every_mandatory_main_workflow_required(self):
        g.require_runs(self.runs(),self.sha)
        for i in range(7):
            runs=self.runs();del runs[i]
            with self.assertRaises(ValueError):g.require_runs(runs,self.sha)
    def test_failed_cancelled_skipped_pending_and_pr_not_authority(self):
        for i in range(7):
            for field,value in [('conclusion','failure'),('conclusion','cancelled'),('conclusion','skipped'),('status','in_progress'),('event','pull_request'),('head_branch','dev'),('head_sha','b'*40)]:
                runs=self.runs();runs[i][field]=value
                with self.assertRaises(ValueError):g.require_runs(runs,self.sha)
    def test_newer_retry_supersedes_old_success(self):
        runs=self.runs();retry=dict(runs[0],id=100,status='queued',conclusion=None)
        with self.assertRaises(ValueError):g.require_runs(runs+[retry],self.sha)
    def test_empty_register_never_authorizes_even_green_main(self):
        with patch.object(g.a.s.p,'load',return_value={'schema':1,'sources':[]}),patch.object(g,'api') as api:
            with self.assertRaises(ValueError):g.authorize(self.sha)
            api.assert_not_called()
    def test_approved_tree_mismatch_refused(self):
        row={'source_sha':self.sha,'source_tree':'b'*40,'publication':'main_approved','review':'synthetic'}
        with patch.object(g.a.s.p,'load',return_value={'schema':1,'sources':[row]}),patch.object(g.a,'source',return_value={'source_tree':'c'*40}):
            with self.assertRaises(ValueError):g.authorize(self.sha)
    def test_approved_validated_source_allowed(self):
        row={'source_sha':self.sha,'source_tree':'b'*40,'publication':'main_approved','review':'synthetic'}
        with patch.object(g.a.s.p,'load',return_value={'schema':1,'sources':[row]}),patch.object(g.a,'source',return_value={'source_tree':'b'*40}),patch.object(g.a,'git',return_value=self.sha),patch.object(g,'api',return_value={'workflow_runs':self.runs()}):
            self.assertEqual(g.authorize(self.sha),{'source_tree':'b'*40})

    def test_current_publication_implementation_must_also_be_green(self):
        row={'source_sha':self.sha,'source_tree':'b'*40,'publication':'main_approved','review':'synthetic'}
        with patch.object(g.a.s.p,'load',return_value={'schema':1,'sources':[row]}),patch.object(g.a,'source',return_value={'source_tree':'b'*40}),patch.object(g.a,'git',return_value='c'*40),patch.object(g,'api',return_value={'workflow_runs':self.runs()}):
            with self.assertRaises(g.NotReady):g.authorize(self.sha)
    def test_existing_complete_release_uses_recovery_instead_of_new_firmware(self):
        row={'source_sha':self.sha,'source_tree':'b'*40,'publication':'main_approved','review':'synthetic'}
        release={'draft':False,'prerelease':True,'assets':[{'name':n} for n in ('esp32-alpha-site.zip','SHA256SUMS','s3-gateway.zip','c3-rover-usb.zip')]}
        with patch.object(g.a.s.p,'load',return_value={'schema':1,'sources':[row]}),patch.object(g.a,'git',return_value=self.sha),patch.object(g,'authorize'),patch.object(g,'api',return_value=release):
            g.ALREADY_PUBLISHED=False
            self.assertEqual(g.select(),self.sha);self.assertTrue(g.ALREADY_PUBLISHED)
    def test_partial_existing_release_cannot_be_treated_as_success(self):
        row={'source_sha':self.sha,'source_tree':'b'*40,'publication':'main_approved','review':'synthetic'}
        with patch.object(g.a.s.p,'load',return_value={'schema':1,'sources':[row]}),patch.object(g.a,'git',return_value=self.sha),patch.object(g,'authorize'),patch.object(g,'api',return_value={'draft':False,'prerelease':True,'assets':[]}):
            with self.assertRaises(ValueError):g.select()

if __name__=='__main__':unittest.main()
