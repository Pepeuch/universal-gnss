"""Synthetic IDF metadata tests, not a firmware build or qualification claim."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
SPEC=importlib.util.spec_from_file_location('firmware_package',ROOT/'tools/package_firmware.py')
p=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(p)
class Package(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.build=self.root/'build';self.build.mkdir()
        self.profile=p.profiles.from_variant('c3-rover-usb')
        table=b''
        for label,kind,subtype,offset,size in [('nvs',1,2,0x9000,0x5000),('otadata',1,0,0xe000,0x2000),('ota_0',0,16,0x10000,0x140000),('ota_1',0,17,0x150000,0x140000)]:
            table+=struct.pack('<2sBBII16sI',b'\xaa\x50',kind,subtype,offset,size,label.encode(),0)
        table+=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(table,usedforsecurity=False).digest()
        self.table=table.ljust(3072,b'\xff')
        files={p.PARTS[0]:b'\xe9boot',p.PARTS[1]:self.table,p.PARTS[2]:b'otadata',p.PARTS[3]:b'\xe9app'}
        for name,data in files.items():
            path=self.build/name;path.parent.mkdir(exist_ok=True);path.write_bytes(data)
        self.verified={'profile':'rover','local_gnss':False,'radio_configuration':'runtime_nvs_encrypted_unicast',
            'rover_output':{'transport':'usb-serial-jtag','tx_pin':None,'baud':None},
            'app_slots':[{'label':'ota_0','offset':0x10000,'size':0x140000},{'label':'ota_1','offset':0x150000,'size':0x140000}],
            'sha256':p.digest(files[p.PARTS[3]]),'app_bytes':len(files[p.PARTS[3]])}
        self.qualification={'source_type':'commit','source_commit':'a'*40,'source_object':'a'*40,'source_tree':'b'*40,
            'variant':'c3-rover-usb','sdk_image':p.load(ROOT/'embedded/esp32/migration/dependencies.json')['sdk']['image'],
            'build_and_verify':'pass','reproducibility':'pass','network_during_build_and_tests':'disconnected_no_interfaces',
            'source_mounts':False,'flash_or_publication':False,'binary':self.verified,
            'part_sha256':{name:p.digest(data) for name,data in files.items()}}
        self.flash={'flash_files':{'0x0':p.PARTS[0],'0x8000':p.PARTS[1],'0xe000':p.PARTS[2],'0x10000':p.PARTS[3]},
            'extra_esptool_args':{'chip':'esp32c3'},'flash_settings':{'flash_size':'4MB'}}
        (self.root/'sdkconfig').write_text('CONFIG_IDF_TARGET="esp32c3"\nCONFIG_ESPTOOLPY_FLASHSIZE="4MB"\nCONFIG_PARTITION_TABLE_OFFSET=0x8000\n')
        self.qualification['flash_offsets']={path:int(offset,0) for offset,path in self.flash['flash_files'].items()}
        self.write_metadata()
    def write_metadata(self):
        (self.build/'flasher_args.json').write_text(json.dumps(self.flash))
        (self.build/'project_description.json').write_text(json.dumps({'target':'esp32c3','git_revision':'v6.1',
            'project_name':'universal-gnss-esp32','config_file':str(self.root/'sdkconfig')}))
    def package(self,output='out'):
        return p.package(self.build,self.verified,self.profile,'a'*40,self.root/output,self.qualification)
    def test_actual_offsets_determinism_provenance_and_no_overwrite(self):
        first=self.package();second=self.package('other');self.assertEqual(first,second)
        manifest=p.load(self.root/'out'/first['manifest'].removeprefix('flasher/'))
        self.assertEqual([v['offset'] for v in manifest['builds'][0]['parts']],[0,0x8000,0xe000,0x10000])
        self.assertEqual(first['install_mode'],'full');self.assertEqual(first['nvs_preservation'],'not_qualified')
        with self.assertRaisesRegex(ValueError,'overwrite'):self.package()
    def test_unqualified_and_mismatched_source_rejected(self):
        for field,value in [('source_type','tree'),('source_commit','c'*40),('source_tree',False),('sdk_image','local:image'),
                            ('reproducibility','skipped'),('source_mounts',True),('network_during_build_and_tests','bridge'),('variant','s3-gateway')]:
            old=self.qualification[field];self.qualification[field]=value
            with self.assertRaises(ValueError):self.package()
            self.qualification[field]=old
    def test_binary_or_partition_substitution_rejected(self):
        for name in p.PARTS:
            path=self.build/name;data=path.read_bytes();path.write_bytes(data+b'changed')
            with self.assertRaises(ValueError):self.package()
            path.write_bytes(data)
    def test_sdk_hardware_and_compiled_pair_settings_rejected(self):
        path=self.root/'sdkconfig';original=path.read_text()
        for config in [original.replace('4MB','16MB'),original+'CONFIG_ESPNOW_RTCM_PEER_MAC="02:00:00:00:00:02"\n',original+'CONFIG_SPIRAM=y\n']:
            path.write_text(config)
            with self.assertRaises(ValueError):self.package()
        path.write_text(original)
    def test_wrong_offsets_extra_parts_and_path_traversal_rejected(self):
        original=copy.deepcopy(self.flash)
        for files in [dict(self.flash['flash_files'],**{'0x9000':p.PARTS[3]}),
                      {'0x0':p.PARTS[0],'0x8000':p.PARTS[1],'0xe000':p.PARTS[2],'0x9000':p.PARTS[3]},
                      {'0x0':'../private.bin'}, {'0x0':[]}]:
            self.flash['flash_files']=files;self.write_metadata()
            with self.assertRaises(ValueError):self.package()
        self.flash=original
    def test_table_integrity_and_duplicate_json_rejected(self):
        bad=bytearray(self.table);bad[20]^=1
        with self.assertRaises(ValueError):p.partitions(bytes(bad))
        path=self.root/'duplicate.json';path.write_text('{"schema":2,"schema":1}')
        with self.assertRaises(ValueError):p.load(path)
    def test_runtime_profile_fields_never_enter_firmware_identity(self):
        for name in ('channel','peer_mac','key','receiver'):
            self.profile[name]='private'
            with self.assertRaises(ValueError):self.package()
            del self.profile[name]
if __name__=='__main__':unittest.main()
