"""Generic firmware identities never include user pairing/receiver settings."""
import copy
import importlib.util
from pathlib import Path
import unittest
SPEC=importlib.util.spec_from_file_location('firmware_profiles',Path(__file__).resolve().parents[3]/'embedded/esp32/native/profiles.py')
p=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(p)
class Profiles(unittest.TestCase):
    def test_supported_catalogue_is_bounded(self):
        self.assertEqual(set(p.VARIANTS),{'s3-gateway','s3-gateway-http','c3-rover-usb','c3-rover-uart'})
        for variant in p.VARIANTS:
            self.assertEqual(p.normalize(p.from_variant(variant)),p.from_variant(variant))
    def test_runtime_and_unknown_options_rejected(self):
        for field in ('peer_mac','channel','key','receiver','source','lora','psram'):
            profile=p.from_variant('s3-gateway');profile[field]='untrusted'
            with self.assertRaises(ValueError):p.normalize(profile)
    def test_board_and_schema_mismatch_rejected(self):
        for field,value in [('schema',1),('schema',True),('flash_mb',4),('board','other'),('profile','relay-ntrip'),('psram_mb',True)]:
            profile=p.from_variant('s3-gateway');profile[field]=value
            with self.assertRaises(ValueError):p.normalize(profile)
        with self.assertRaises(ValueError):p.normalize(None)
    def test_uart_options_are_bounded_compile_variants(self):
        for pin in (4,5,6,7,10):
            for baud in (115200,230400,460800,921600):
                self.assertEqual(p.from_variant('c3-rover-uart',pin,baud)['uart_tx'],pin)
        for pin,baud in [(9,115200),(4,9600),(True,115200)]:
            with self.assertRaises(ValueError):p.from_variant('c3-rover-uart',pin,baud)
    def test_keys_are_immutable_source_and_option_specific(self):
        a=p.from_variant('s3-gateway')
        self.assertEqual(p.build_key('a'*40,a),p.build_key('a'*40,dict(reversed(list(a.items())))))
        self.assertNotEqual(p.build_key('a'*40,a),p.build_key('b'*40,a))
        self.assertNotEqual(p.build_key('a'*40,p.from_variant('c3-rover-uart',4)),p.build_key('a'*40,p.from_variant('c3-rover-uart',5)))
        for source in ('main','A'*40,'a'*39,'a'*40+';bad'):
            with self.assertRaises(ValueError):p.build_key(source,a)
    def test_missing_options_and_foreign_hardware_rejected(self):
        a=p.from_variant('c3-rover-usb');del a['ethernet']
        with self.assertRaises(ValueError):p.normalize(a)
        for variant in ('c6-rover','c3-relay','lora','gateway',[],{}):
            a=copy.deepcopy(p.from_variant('s3-gateway'));a['variant']=variant
            with self.assertRaises(ValueError):p.normalize(a)
if __name__=='__main__':unittest.main()
