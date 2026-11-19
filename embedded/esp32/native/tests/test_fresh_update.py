"""Offline update write boundaries, not hardware or browser qualification."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec=importlib.util.spec_from_file_location('update_plan',Path(__file__).resolve().parents[1]/'tools/prepare_update.py')
u=importlib.util.module_from_spec(spec)
spec.loader.exec_module(u)


class ApplicationUpdate(unittest.TestCase):
    def setUp(self):
        self.flash=bytearray(b'\xff'*(16*1024*1024))
        rows=b''
        for label,kind,subtype,offset,size in (('nvs',1,2,0x9000,0x6000),('otadata',1,0,0xf000,0x2000),
                ('ota_0',0,16,0x20000,0x300000),('ota_1',0,17,0x320000,0x300000)):
            rows+=struct.pack('<2sBBII16sI',b'\xaa\x50',kind,subtype,offset,size,label.encode(),0)
        self.table=(rows+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(rows,usedforsecurity=False).digest()).ljust(3072,b'\xff')
        self.flash[0x8000:0x8000+len(self.table)]=self.table
        self.boot=b'\xe9s3-bootloader'
        self.flash[:len(self.boot)]=self.boot
        image=bytearray(b'\0'*256)
        image[0]=0xe9
        struct.pack_into('<H',image,12,9)
        struct.pack_into('<I',image,32,0xabcd5432)
        self.current=bytes(image)
        image[-1]=1
        self.new=bytes(image)
        # Synthetic opaque namespaces: never real credentials or a real NVS dump.
        self.flash[0x9000:0x9060]=b'GNSS NTRIP Ethernet WiFi ESP-NOW admin TLS'.ljust(96,b'-')
        self.flash[0x20000:0x20100]=self.current

    def plan(self):
        return u.plan(self.flash,u.p.digest(self.flash),self.current,self.new,self.table,self.boot)

    def test_erased_ota_uses_first_slot_and_does_not_touch_configuration(self):
        result=self.plan()
        self.assertEqual(result['write'],{'offset':0x20000,'size':len(self.new)})
        before=bytes(self.flash[0x9000:0xf000])
        offset=result['write']['offset']
        self.flash[offset:offset+len(self.new)]=self.new
        self.assertEqual(bytes(self.flash[0x9000:0xf000]),before)
        self.assertFalse(result['browser_update_enabled'])
        self.assertIn('hardware_required',result['nvs_preservation'])

    def test_valid_second_slot_requires_matching_old_application(self):
        record=bytearray(b'\xff'*32)
        struct.pack_into('<I',record,0,2)
        # Independent SDK6.1 ROM CRC32_LE(UINT32_MAX) vector for ota_seq=2.
        struct.pack_into('<II',record,24,2,0x55f63774)
        self.flash[0xf000:0xf020]=record
        with self.assertRaisesRegex(ValueError,'archived reference'):self.plan()
        self.flash[0x320000:0x320100]=self.current
        self.assertEqual(self.plan()['write']['offset'],0x320000)

    def test_real_sdk_first_slot_crc_vector_and_wrong_seed_rejected(self):
        # Public OTA metadata read from the S3 (no credentials): seq=1, VALID.
        record=bytearray(b'\xff'*32)
        struct.pack_into('<I',record,0,1)
        struct.pack_into('<II',record,24,2,0x4743989a)
        self.flash[0xf000:0xf020]=record
        self.assertEqual(self.plan()['write']['offset'],0x20000)
        struct.pack_into('<I',record,28,zlib.crc32(record[:4])^0xffffffff)
        self.flash[0xf000:0xf020]=record
        with self.assertRaisesRegex(ValueError,'OTA state'):self.plan()

    def test_corrupt_pending_ota_unknown_chip_and_partition_are_refused(self):
        self.flash[0xf000]=1
        with self.assertRaisesRegex(ValueError,'OTA state'):self.plan()
        self.flash[0xf000]=0xff
        self.flash[0x8001]^=1
        with self.assertRaisesRegex(ValueError,'partition change'):self.plan()
        self.flash[0x8001]^=1
        self.new=self.new[:12]+b'\x05\x00'+self.new[14:]
        with self.assertRaisesRegex(ValueError,'ESP32-S3'):self.plan()

    def test_backup_reference_and_image_size_are_mandatory(self):
        with self.assertRaisesRegex(ValueError,'backup'):
            u.plan(self.flash,'0'*64,self.current,self.new,self.table,self.boot)
        self.new=b'\xe9'*(0x300000+1)
        with self.assertRaisesRegex(ValueError,'slot mismatch'):self.plan()


if __name__=='__main__':unittest.main()
