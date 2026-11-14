"""Cross-language portable schema/privacy and software-only PTY provisioning."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('portable',ROOT/'embedded/esp32/tools/portable_config.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class Portable(unittest.TestCase):
    def check_c(self,value,valid=True):
        executable=os.environ.get('UG_PORTABLE_VALIDATOR')
        if not executable:return
        text=value if isinstance(value,str) else json.dumps(value)
        result=subprocess.run([executable,text],capture_output=True,text=True)
        self.assertEqual(result.returncode==0,valid,(text,result.stderr))
        if valid:self.assertEqual(json.loads(result.stdout),p.parse(text))
    def test_c_python_schema_match_and_supported_pins(self):
        for board in p.PINS:
            value=p.defaults(board);self.check_c(value)
            for tx in range(-1,49):
                candidate={**value,'uart_tx':tx,'uart_rx':16 if board=='waveshare-s3-eth' else -1,'output':'gnss' if board=='waveshare-s3-eth' else 'uart'}
                try:p.validate(candidate);valid=True
                except ValueError:valid=False
                self.check_c(candidate,valid)
            for tx in p.PINS[board]:
                for baud in (115200,230400,460800,921600):
                    candidate={**value,'uart_tx':tx,'uart_rx':17 if tx!=17 else 16,'uart_baud':baud}
                    if board=='supermini-c3':candidate.update(output='uart',uart_rx=-1)
                    self.assertEqual(p.parse(json.dumps(candidate)),candidate);self.check_c(candidate)
    def test_schema_document_matches_strict_implementations(self):
        schema=json.loads((ROOT/'embedded/esp32/config/portable-v1.schema.json').read_text())
        self.assertFalse(schema['additionalProperties']);self.assertEqual(set(schema['required']),set(p.defaults('supermini-c3')))
        s3=schema['oneOf'][0]['oneOf'][1:]
        self.assertEqual([v['properties']['uart_tx']['const'] for v in s3],list(p.PINS['waveshare-s3-eth']))
        self.assertEqual(schema['oneOf'][1]['oneOf'][1]['properties']['uart_tx']['enum'],list(p.PINS['supermini-c3']))
    def test_private_fields_absent_and_rejected_not_redacted(self):
        base=p.defaults('waveshare-s3-eth')
        for key in ('password','ssid','host','peer','key','mac','device_name','latitude_deg','longitude_deg','token','certificate','diagnostics','history','username','mountpoint'):
            candidate={**base,key:'PRIVATE_SENTINEL'}
            with self.assertRaises(ValueError):p.parse(json.dumps(candidate))
            self.check_c(candidate,False)
        self.assertNotIn('PRIVATE',json.dumps(base))
    def test_duplicate_unknown_version_nested_noninteger_and_overflow_rejected(self):
        text=json.dumps(p.defaults('supermini-c3'))
        for bad in ('{}',text.replace('"schema": 1','"schema": 2'),text.replace('"schema": 1','"schema": true'),text.replace('"schema": 1','"schema": 1.0'),text.replace('"schema": 1','"schema": 1e0'),text.replace('"schema": 1','"schema": {"a":1}'),text.replace('"schema": 1','"schema": 1,"schema":1'),text.replace('"schema": 1','"schema": 99999999999999999'),text+'garbage',text.replace('"schema"','"sche\\u006da"'),text.replace('"schema": 1','"schema": -0'),' '*513):
            with self.assertRaises(ValueError):p.parse(bad)
            self.check_c(bad,False)
    def test_wrong_board_and_peripheral_conflicts(self):
        value=p.defaults('waveshare-s3-eth')
        with self.assertRaises(ValueError):p.validate(value,'supermini-c3')
        for pin in (0,3,4,5,6,7,8,9,10,11,12,13,14,19,20,21,26,33,34,35,36,37,45,46):
            with self.assertRaises(ValueError):p.validate({**value,'uart_tx':pin,'uart_rx':17})
        for changes in ({'uart_tx':17,'uart_rx':17},{'ethernet':'custom'},{'profile':'relay-ntrip'},{'board':'c6'}):
            with self.assertRaises(ValueError):p.validate({**value,**changes})
if __name__=='__main__':unittest.main()
