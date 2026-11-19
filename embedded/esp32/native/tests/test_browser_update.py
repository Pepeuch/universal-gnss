"""Browser update boundaries against independently constructed flash fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

import quickjs

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('update_fixture', Path(__file__).with_name('test_fresh_update.py'))
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)


class BrowserUpdate(unittest.TestCase):
    def setUp(self):
        self.data = f.ApplicationUpdate()
        self.data.setUp()
        # Real application descriptor spans 256 bytes after the segment header.
        self.data.current = self.data.current.ljust(288, b'\0')
        self.data.new = self.data.new.ljust(288, b'\0')
        self.data.flash[0x20000:0x20120] = self.data.current
        self.vm = quickjs.Context()
        self.vm.eval((ROOT/'docs/flasher/update.js').read_text())

    def plan(self):
        args = [self.data.flash, self.data.current, self.data.new, self.data.table, self.data.boot]
        js = ','.join('new Uint8Array('+json.dumps(list(v))+')' for v in args)
        return json.loads(self.vm.eval('JSON.stringify(UGUpdate.plan('+js+'))'))

    def test_usb_preflight_abort_write_and_readback(self):
        # Exercise the actual async browser controller with an emulated USB loader.
        self.vm.add_callable('hash_bytes', lambda value: json.dumps(list(hashlib.sha256(bytes(json.loads(value))).digest())))
        self.vm.eval("globalThis.crypto={subtle:{digest:async(_name,v)=>new Uint8Array(JSON.parse(hash_bytes(JSON.stringify(Array.from(v))))).buffer}}")
        args=[self.data.flash,self.data.current,self.data.new,self.data.table,self.data.boot]
        self.vm.eval('globalThis.inputs=['+','.join('new Uint8Array('+json.dumps(list(v))+')' for v in args)+']')
        self.vm.eval("""
        globalThis.outcome=null;
        (async()=>{
          const [backup,old,next,table,boot]=inputs;
          const digest=async v=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',v)),x=>x.toString(16).padStart(2,'0')).join('');
          const baseline={settings_schema:1,profile:{board:'waveshare-s3-eth'},sha256:await digest(old)};
          const verified={entry:{...baseline,sha256:await digest(next)},parts:[{offset:0,bytes:boot,path:'bootloader.bin'},{offset:32768,bytes:table,path:'partition-table.bin'},{offset:131072,bytes:next,path:'universal-gnss-esp32.bin'}]};
          let writes=0,live=new Uint8Array(backup),allow=false;
          const loader={chip:{CHIP_NAME:'ESP32-S3'},detectFlashSize:async()=> '16MB',readFlash:async(a,n)=>live.slice(a,a+n),writeFlash:async options=>{
            if(options.eraseAll || options.fileArray.length!==1 || options.fileArray[0].address!==131072)throw Error('Unsafe write');
            writes++;live.set(options.fileArray[0].data,131072);
          }};
          const attempt=()=>UGUpdate.run(loader,backup,old,verified,baseline,async()=>allow,()=>{});
          try{await attempt();throw Error('Cancellation ignored');}catch(e){if(writes || !e.message.includes('annulée'))throw e;}
          allow=true;live[36864]^=1;
          try{await attempt();throw Error('NVS mismatch ignored');}catch(e){if(writes || !e.message.includes('référence'))throw e;}
          live=new Uint8Array(backup);await attempt();
          if(writes!==1 || !live.slice(36864,61440).every((v,i)=>v===backup[36864+i]))throw Error('Preservation failed');
          outcome='PASS';
        })().catch(e=>outcome=e.message);
        """)
        while self.vm.execute_pending_job():
            pass
        self.assertEqual(self.vm.eval('outcome'),'PASS')

    def test_active_application_span_matches_python_guard(self):
        self.assertEqual(self.plan()['offset'], self.data.plan()['write']['offset'])
        self.assertEqual(self.plan()['size'], len(self.data.new))
        self.assertEqual(self.plan()['nvs']['offset'], 0x9000)

    def test_usb_reset_pulses_rts_with_gpio0_released(self):
        self.vm.eval("""
        globalThis.resetEvents=[];globalThis.resetDone=false;
        UGUpdate.restart({setDTR:async v=>resetEvents.push(['DTR',v]),setRTS:async v=>resetEvents.push(['RTS',v])},async ms=>resetEvents.push(['wait',ms])).then(()=>resetDone=true);
        """)
        while self.vm.execute_pending_job():
            pass
        self.assertTrue(self.vm.eval('resetDone'))
        self.assertEqual(json.loads(self.vm.eval('JSON.stringify(resetEvents)')),
                         [['DTR',False],['RTS',True],['wait',200],['RTS',False],['wait',200]])

    def test_local_reference_is_bound_to_hash_hardware_size_and_schema(self):
        record={'settings_schema':1,'sha256':'a'*64,'app_bytes':1500,'profile':'gateway',
                'firmware_identity':{'hardware_variant':'waveshare-s3-eth','web_transport':'http','esp_idf':'v6.1'}}
        def check(value):
            return json.loads(self.vm.eval('JSON.stringify(UGUpdate.baseline('+json.dumps(value)+',"'+'a'*64+'",1500))'))
        self.assertEqual(check(record)['sha256'],'a'*64)
        self.assertEqual(check({'binary':record})['sha256'],'a'*64)
        for key,value in [('settings_schema',2),('sha256','b'*64),('app_bytes',1501),('profile','rover')]:
            with self.subTest(key=key),self.assertRaises(quickjs.JSException):
                check({**record,key:value})
        with self.assertRaises(quickjs.JSException):
            check({**record,'firmware_identity':{**record['firmware_identity'],'esp_idf':'v6.10'}})

    def test_independent_ota_crc_vectors(self):
        self.assertEqual(self.vm.eval('UGUpdate.crcSequence(1)'), 0x4743989a)
        self.assertEqual(self.vm.eval('UGUpdate.crcSequence(2)'), 0x55f63774)

    def test_live_flash_or_partitions_mismatch_is_rejected(self):
        self.data.flash[0x9000] ^= 1  # NVS is opaque and may contain secrets.
        self.assertEqual(self.plan()['offset'], 0x20000)
        self.data.flash[0x8000] ^= 1
        with self.assertRaisesRegex(quickjs.JSException, 'Partitionnement'):
            self.plan()

    def test_pending_ota_and_wrong_application_are_rejected(self):
        self.data.flash[0xf000] = 1
        with self.assertRaisesRegex(quickjs.JSException, 'OTA'):
            self.plan()
        self.data.flash[0xf000] = 255
        self.data.flash[0x20000] ^= 1
        with self.assertRaisesRegex(quickjs.JSException, 'installée'):
            self.plan()


if __name__ == '__main__':
    unittest.main()
