"""Request admission and manual workflow; never dispatches GitHub Actions."""
import copy
import importlib.util
from pathlib import Path
import unittest
import yaml
ROOT=Path(__file__).resolve().parents[3]
SPEC=importlib.util.spec_from_file_location('firmware_request',ROOT/'tools/branches/firmware_request.py')
r=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(r)
class Requests(unittest.TestCase):
    def setUp(self):
        self.approvals={'schema':1,'sources':[{'source_sha':'a'*40,'source_tree':'b'*40,'review':'synthetic reviewed fixture','publication':'not_authorized'}]}
    def test_request_normalizes_approved_source_without_runtime_settings(self):
        out=r.validate('a'*40,'c3-rover-uart',5,230400,self.approvals)
        self.assertEqual(out['profile']['uart_tx'],5);self.assertEqual(out['source_tree'],'b'*40)
        self.assertNotIn('channel',out['profile']);self.assertEqual(out['publication'],'not_authorized')
    def test_unknown_unapproved_and_malformed_sources_rejected(self):
        for sha in ('main','a'*39,'b'*40,'a'*40+'; command'):
            with self.assertRaises(ValueError):r.validate(sha,'s3-gateway',4,115200,self.approvals)
        with self.assertRaises(ValueError):r.validate('a'*40,'s3-gateway',4,115200,{'schema':1,'sources':[]})
    def test_approval_register_cannot_implicitly_expand(self):
        for name,value in [('source_tree','main'),('publication','public'),('review','')]:
            bad=copy.deepcopy(self.approvals);bad['sources'][0][name]=value
            with self.assertRaises(ValueError):r.validate('a'*40,'s3-gateway',4,115200,bad)
        bad=copy.deepcopy(self.approvals);bad['sources']*=2
        with self.assertRaises(ValueError):r.validate('a'*40,'s3-gateway',4,115200,bad)
    def test_unsupported_or_irrelevant_uart_options_rejected(self):
        for variant,tx,baud in [('c6-rover',4,115200),('c3-rover-uart',9,115200),('s3-gateway',5,115200),('c3-rover-usb',4,921600)]:
            with self.assertRaises((ValueError,KeyError)):r.validate('a'*40,variant,tx,baud,self.approvals)
    def test_workflow_requires_default_branch_manual_authority(self):
        flow=yaml.load((ROOT/'.github/workflows/esp32-request.yml').read_text(),Loader=yaml.BaseLoader)
        self.assertEqual(set(flow['on']),{'workflow_dispatch'})
        self.assertEqual(flow['permissions'],{'contents':'read'})
        self.assertEqual(flow['concurrency']['cancel-in-progress'],'false')
        job=flow['jobs']['validate'];self.assertNotIn('if',job)
        self.assertIn('test "$WORKFLOW_REF" = "refs/heads/$DEFAULT_BRANCH"',job['steps'][0]['run'])
        checkout=job['steps'][1]['with'];self.assertIn('repository.default_branch',checkout['ref'])
        self.assertEqual(checkout['persist-credentials'],'false')
        script=job['steps'][2]['run'];self.assertIn('--execute',script)
        self.assertNotIn('${{',script);self.assertNotIn('issues',str(flow['on']))
        inputs=flow['on']['workflow_dispatch']['inputs']
        self.assertEqual(inputs['variant']['options'],['s3-gateway','c3-rover-usb','c3-rover-uart'])
        self.assertEqual(job['steps'][3]['with']['retention-days'],'7')
    def test_ci_uart_overrides_are_bounded(self):
        spec=importlib.util.spec_from_file_location('request_ci',ROOT/'embedded/esp32/native/ci.py')
        ci=importlib.util.module_from_spec(spec);spec.loader.exec_module(ci)
        self.assertIn('--rover-uart-tx 7 --rover-uart-baud 460800',ci.build_arguments('c3-rover-uart',7,460800))
        for variant,tx,baud in [('s3-gateway',4,115200),('c3-rover-uart',9,115200),('c3-rover-uart',4,9600)]:
            with self.assertRaises(ValueError):ci.uart_options(variant,tx,baud)
if __name__=='__main__':unittest.main()
