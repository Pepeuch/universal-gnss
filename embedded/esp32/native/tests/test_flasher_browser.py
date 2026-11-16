"""Run public portal validation JS against synthetic SDK bundles, real host SHA256.

QuickJS is host-only. No network, browser USB permission or physical device is used.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import unittest
import quickjs
ROOT=Path(__file__).resolve().parents[4]
SPEC=importlib.util.spec_from_file_location('package_fixture',ROOT/'tools/branches/tests/test_firmware_package.py')
f=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(f)
class Flasher(unittest.TestCase):
    def setUp(self):
        self.fixture=f.Package();self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.entry=self.fixture.package();self.profile=self.fixture.profile
        self.approval={'source_sha':'a'*40,'source_tree':'b'*40,'review':'synthetic fixture','publication':'main_approved'}
        base=self.fixture.root/'out';directory=base/'published'/self.entry['build_key']
        self.files={'flasher/'+str(path.relative_to(base)):list(path.read_bytes()) for path in directory.iterdir()
                    if path.suffix in ('.bin','.json') and path.name!='DEPENDENCIES.json'}
    def vm(self):
        vm=quickjs.Context();vm.set_memory_limit(32*1024*1024)
        vm.add_callable('_sha256',lambda values:hashlib.sha256(bytes(json.loads(values))).hexdigest())
        vm.eval('''
          class TextEncoder {encode(s){return Uint8Array.from(s,c=>c.charCodeAt(0));}}
          class TextDecoder {decode(b){return String.fromCharCode(...b);}}
          const crypto={subtle:{async digest(kind,data){
            if(kind!=='SHA-256')throw Error('unexpected digest');
            const hex=_sha256(JSON.stringify(Array.from(data instanceof Uint8Array?data:new Uint8Array(data))));
            return Uint8Array.from(hex.match(/../g),x=>parseInt(x,16)).buffer;
          }}};
        ''')
        vm.eval((ROOT/'docs/flasher/app.js').read_text());return vm
    def evaluate(self,entry=None,approval=None,files=None):
        vm=self.vm()
        vm.eval('const downloads=[];const files='+json.dumps(self.files if files is None else files)+';')
        vm.eval('UGFlasher.verifyPublished('+','.join(json.dumps(v) for v in (self.entry if entry is None else entry,self.profile,self.approval if approval is None else approval))+''',async(path,limit)=>{
          downloads.push(path);if(!files[path])throw Error('missing fixture');
          if(files[path].length>limit)throw Error('budget exceeded');return Uint8Array.from(files[path]);
        }).then(value=>globalThis.result={ok:true,count:value.parts.length},error=>globalThis.result={error:error.message});''')
        for _ in range(200):
            if not vm.execute_pending_job():break
        result=vm.eval('JSON.stringify(globalThis.result)')
        self.assertIsNotNone(result,'browser promise budget exceeded')
        return json.loads(result),json.loads(vm.eval('JSON.stringify(downloads)'))
    def test_latest_complete_authorized_publication_selected_first(self):
        vm=self.vm();old=dict(self.approval);new=dict(self.approval,source_sha='b'*40)
        registry={'schema':1,'sources':[old,new]}
        images=[{'source_sha':sha,'profile':{'variant':v}} for sha in ('a'*40,'b'*40) for v in ('s3-gateway','c3-rover-usb')]
        history={'schema':1,'publications':[{'source_sha':sha,'status':'validated_authorized_alpha','published_at':date} for sha,date in [('a'*40,'2026-11-15T00:00:00Z'),('b'*40,'2026-11-16T00:00:00Z')]]}
        expr='UGFlasher.publishedVersions('+','.join(json.dumps(v) for v in (registry,{'images':images},history))+')'
        self.assertEqual(vm.eval(expr+'[0].source_sha'),'b'*40)
        registry['sources'][1]['publication']='not_authorized'
        with self.assertRaises(Exception):vm.eval('UGFlasher.publishedVersions('+','.join(json.dumps(v) for v in (registry,{'images':images},history))+')')
        registry['sources'][1]['publication']='main_approved'
        history['publications'][1]['source_sha']='c'*40
        with self.assertRaises(Exception):vm.eval('UGFlasher.publishedVersions('+','.join(json.dumps(v) for v in (registry,{'images':images},history))+')')
    def test_actual_packager_and_browser_agree(self):
        result,paths=self.evaluate();self.assertEqual(result,{'ok':True,'count':4});self.assertEqual(len(paths),7)
    def test_unapproved_source_never_downloads_images(self):
        for field,value in [('publication','not_authorized'),('source_sha','c'*40)]:
            row=copy.deepcopy(self.approval);row[field]=value
            result,paths=self.evaluate(approval=row);self.assertIn('error',result);self.assertEqual(paths,[])
    def test_torn_provenance_and_corrupt_payload_refused(self):
        prefix='flasher/published/'+self.entry['build_key']+'/'
        for path in [prefix+'provenance.json',prefix+'qualification.json']:
            files=copy.deepcopy(self.files);data=json.loads(bytes(files[path]));data['source_sha']='c'*40;data['source_commit']='c'*40
            files[path]=list(json.dumps(data).encode());self.assertIn('error',self.evaluate(files=files)[0])
        files=copy.deepcopy(self.files);path=next(p for p in files if p.endswith('universal-gnss-esp32.bin'));files[path][-1]^=1
        self.assertIn('error',self.evaluate(files=files)[0])
    def test_wrong_image_paths_and_update_mode_refused(self):
        for field,value in [('manifest','../private.json'),('install_mode','preserve_nvs'),('build_key','0'*20),('manifest_sha256','0'*64)]:
            entry=copy.deepcopy(self.entry);entry[field]=value
            self.assertIn('error',self.evaluate(entry=entry)[0])
    def test_qualified_offsets_are_mandatory(self):
        files=copy.deepcopy(self.files);path=next(p for p in files if p.endswith('/qualification.json'))
        data=json.loads(bytes(files[path]));data['flash_offsets']['universal-gnss-esp32.bin']=0x9000
        files[path]=list(json.dumps(data).encode());self.assertIn('offset',self.evaluate(files=files)[0]['error'])
    def test_previous_firmware_without_portable_capability_is_refused(self):
        files=copy.deepcopy(self.files);path=next(p for p in files if p.endswith('/qualification.json'))
        data=json.loads(bytes(files[path]));data['binary'].pop('portable_configuration_schema')
        files[path]=list(json.dumps(data).encode());self.assertIn('error',self.evaluate(files=files)[0])
    def test_profile_contract_matches_python_and_has_no_runtime_fields(self):
        vm=self.vm()
        for variant in f.p.profiles.VARIANTS:
            profile=f.p.profiles.from_variant(variant)
            normalized=json.loads(vm.eval('JSON.stringify(UGFlasher.normalize('+json.dumps(profile)+'))'))
            self.assertEqual(normalized,profile)
            for field in ('channel','peer_mac','key','receiver'):
                bad={**profile,field:'private'}
                with self.assertRaises(quickjs.JSException):vm.eval('UGFlasher.normalize('+json.dumps(bad)+')')
    def test_full_install_manifest_cannot_suppress_erase_prompt(self):
        files=copy.deepcopy(self.files);entry=copy.deepcopy(self.entry)
        path=entry['manifest'];data=json.loads(bytes(files[path]));data['new_install_prompt_erase']=False
        raw=json.dumps(data).encode();files[path]=list(raw);entry['manifest_sha256']=hashlib.sha256(raw).hexdigest()
        provenance_path=path.removesuffix('manifest.json')+'provenance.json'
        provenance=json.loads(bytes(files[provenance_path]));provenance['manifest_sha256']=entry['manifest_sha256']
        files[provenance_path]=list(json.dumps(provenance).encode())
        self.assertIn('Manifeste',self.evaluate(entry=entry,files=files)[0]['error'])
    def test_page_connections_and_no_mutable_flash_refetch(self):
        html=(ROOT/'docs/flasher.html').read_text();script=(ROOT/'docs/flasher/app.js').read_text()
        self.assertIn('esp-web-tools@10.4.0',html);self.assertNotIn('@latest',html)
        self.assertNotIn('id="peer"',html);self.assertNotIn('id="channel"',html)
        self.assertIn('URL.createObjectURL(new Blob([part.bytes]))',script)
        self.assertIn("setAttribute('manifest',current.manifestUrl)",script)
        self.assertNotIn('Authorization',script)
        ids=set(re.findall(r'id="([^"]+)"',html))
        for name in set(re.findall(r"\$\('([^']+)'\)",script)):
            self.assertIn(name,ids)
if __name__=='__main__':unittest.main()
