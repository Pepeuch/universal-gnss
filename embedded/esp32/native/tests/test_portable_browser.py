"""Shared JS validator and explicit USB restoration protocol (no physical port)."""
import importlib.util
import json
from pathlib import Path
import unittest
import quickjs
ROOT=Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('portable',ROOT/'embedded/esp32/tools/portable_config.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class Browser(unittest.TestCase):
 def vm(self):
  vm=quickjs.Context();vm.set_memory_limit(32*1024*1024)
  vm.eval((ROOT/'docs/flasher/portable.js').read_text());return vm
 def test_python_js_schema_and_gpio_contract(self):
  vm=self.vm()
  for board in p.PINS:
   base=p.defaults(board);self.assertEqual(json.loads(vm.eval('JSON.stringify(UGPortable.parse('+json.dumps(json.dumps(base))+'))')),base)
   for tx in range(-1,49):
    value={**base,'uart_tx':tx,'uart_rx':16 if board=='waveshare-s3-eth' else -1,'output':'gnss' if board=='waveshare-s3-eth' else 'uart'}
    try:p.validate(value);valid=True
    except ValueError:valid=False
    try:vm.eval('UGPortable.parse('+json.dumps(json.dumps(value))+')');accepted=True
    except quickjs.JSException:accepted=False
    self.assertEqual(valid,accepted,value)
  for bad in ('{}','{"schema":1,"schema":1}','[1]',json.dumps({**base,'ssid':'private'})):
   with self.assertRaises(quickjs.JSException):vm.eval('UGPortable.parse('+json.dumps(bad)+')')
 def provision(self,board,accept=True,reject=False,nonce_bad=False,cleanup_fail=False,read_timeout=False):
  vm=self.vm();value=p.defaults(board)
  vm.eval('const input='+json.dumps(value)+';const accept='+json.dumps(accept)+';const reject='+json.dumps(reject)+';const nonceBad='+json.dumps(nonce_bad)+';const cleanupFail='+json.dumps(cleanup_fail)+';const readTimeout='+json.dumps(read_timeout)+';')
  vm.eval('''
  const calls=[],queue=[];let released=0,closed=0,cancelled=0;
  function setTimeout(fn,ms){if(readTimeout && ms>3500)Promise.resolve().then(fn);return 1}function clearTimeout(){}
  class TextEncoder {encode(s){return Uint8Array.from(s,c=>c.charCodeAt(0));}}
  class TextDecoder {decode(b){return String.fromCharCode(...b);}}
  const crypto={getRandomValues(a){a.fill(1);return a}};
  const reader={async read(){if(readTimeout)return new Promise(()=>{});if(!queue.length)throw Error('no fixture');return {value:new TextEncoder().encode(queue.shift()),done:false}},async cancel(){cancelled++;if(cleanupFail)throw Error('cleanup fixture')},releaseLock(){released++}};
  const writer={async write(bytes){const s=new TextDecoder().decode(bytes).trim();calls.push(s);
   let response;
   if(s.startsWith('UGCFG/1 OPEN '))response={mode:'control',nonce:nonceBad?'wrong':s.split(' ').pop()};
   else if(s==='UGCFG/1 CONFIG_GET')response={portable:input,result:'current'};
   else if(s.startsWith('UGCFG/1 CONFIG_CHECK '))response=reject?{error:'incompatible'}:{portable:input,result:'valid'};
   else if(s.startsWith('UGCFG/1 CONFIG_APPLY '))response={portable:input,result:'saved'};
   if(response){queue.push('ROM noise\\nUGCFG/1 '+JSON.stringify(response)+'\\n');return}
   queue.push(s==='portable-export'?'UGPORT/1 JSON '+JSON.stringify(input)+'\\n':s.startsWith('portable-check ')?'UGPORT/1 '+(reject?'ERROR':'VALID')+'\\n':'UGPORT/1 SAVED_RESTART_REQUIRED\\n');
  },releaseLock(){released++}};
  const port={async open(){},readable:{getReader:()=>reader},writable:{getWriter:()=>writer},async close(){closed++}};
  ''')
  vm.eval((ROOT/'docs/flasher/provision.js').read_text())
  vm.eval('UGProvision.restore(port,input,async(before,after)=>accept).then(v=>globalThis.result=v,e=>globalThis.result={error:e.message});')
  for _ in range(200):
   if not vm.execute_pending_job():break
  result=json.loads(vm.eval('JSON.stringify(result)'));calls=json.loads(vm.eval('JSON.stringify(calls)'))
  self.assertEqual(vm.eval('released'),2);self.assertEqual(vm.eval('closed'),1);self.assertEqual(vm.eval('cancelled'),1)
  return result,calls
 def test_confirmed_c3_and_s3_restore_is_real_transport_not_flash(self):
  for board in p.PINS:
   result,calls=self.provision(board);self.assertEqual(result,{'applied':True,'restart_required':True})
   self.assertTrue(any('APPLY CONFIRM' in c or 'apply CONFIRM' in c for c in calls));self.assertFalse(any('RUN' in c or 'reboot' in c or 'flash' in c for c in calls))
 def test_cancel_and_device_rejection_never_apply(self):
  for board in p.PINS:
   result,calls=self.provision(board,accept=False);self.assertEqual(result,{'applied':False});self.assertFalse(any('APPLY' in c or 'apply' in c for c in calls))
   result,calls=self.provision(board,reject=True);self.assertIn('error',result);self.assertFalse(any('APPLY' in c or 'apply' in c for c in calls))
  result,calls=self.provision('supermini-c3',nonce_bad=True);self.assertIn('error',result);self.assertEqual(len(calls),1)
 def test_timeout_and_cleanup_failure_never_return_success(self):
  result,calls=self.provision('supermini-c3',read_timeout=True);self.assertIn('Délai USB',result['error'])
  result,calls=self.provision('supermini-c3',cleanup_fail=True);self.assertIn('Nettoyage',result['error'])
 def test_portal_import_export_runtime_uart_does_not_change_rom_or_send_config(self):
  vm=self.vm()
  vm.eval(r'''const elements={},requests=[],blobs=[];
  const document={baseURI:'https://example.invalid/flasher.html',getElementById(id){return elements[id] ||= {value:id==='device'?'s3-gateway':'',classList:{add(){},remove(){},toggle(){}},children:[],get options(){return this.children},append(x){this.children.push(x)},replaceChildren(){this.children=[]},removeAttribute(){},setAttribute(){},addEventListener(kind,fn){this[kind]=fn}}},createElement(){return {click(){}}}};
  const location={origin:'https://example.invalid'};
  function setTimeout(){return 1}function clearTimeout(){}
  class AbortController{constructor(){this.signal={}}abort(){}}
  class TextEncoder{encode(s){return Uint8Array.from(s,c=>c.charCodeAt(0));}}
  class TextDecoder{decode(b){return String.fromCharCode(...b);}}
  class Blob{constructor(parts){this.text=parts.join('')}}
  class URL{constructor(path){this.origin=location.origin;this.path=path}static createObjectURL(blob){blobs.push(blob.text);return 'blob:fixture'}static revokeObjectURL(){}}
  class URLSearchParams{constructor(fields){this.fields=fields}toString(){return JSON.stringify(this.fields)}}
  async function fetch(url,options){requests.push(url.path);if(url.path==='flasher/publications.json')return {ok:false,status:404,headers:{get:()=>null}};if(!['flasher/catalog.json','flasher/approved-sources.json'].includes(url.path))throw Error('unexpected network request');return {ok:true,headers:{get:()=>null},arrayBuffer:async()=>new TextEncoder().encode(JSON.stringify(url.path.includes('catalog')?{schema:2,images:[]}:{schema:1,sources:[]})).buffer};}
  ''')
  vm.eval((ROOT/'docs/flasher/app.js').read_text())
  def drain():
   for _ in range(200):
    if not vm.execute_pending_job():break
  drain();self.assertIn('Aucune image',vm.eval("elements.status.textContent"))
  vm.eval("elements.device.value='c3-rover-usb';elements.device.change();")
  drain()
  value={**p.defaults('supermini-c3'),'output':'uart','uart_tx':7,'uart_baud':460800}
  vm.eval("elements['portable-import'].change({target:{value:'file',files:[{size:300,text:async()=>"+json.dumps(json.dumps(value))+"}]}});")
  drain();vm.eval("elements['portable-export'].click();")
  self.assertEqual(json.loads(vm.eval('blobs[0]')),value)
  # The missing-firmware request includes only the generic ROM profile, never runtime pins/channel.
  self.assertNotIn('uart_tx',vm.eval('elements.request.href'))
  self.assertNotIn('radio_channel',vm.eval('elements.request.href'))
  self.assertEqual(json.loads(vm.eval('JSON.stringify(requests)')),['flasher/catalog.json','flasher/approved-sources.json','flasher/publications.json'])
  vm.eval("elements['portable-import'].change({target:{value:'private',files:[{size:300,text:async()=>JSON.stringify({...UGPortable.defaults('supermini-c3'),ssid:'PRIVATE_SENTINEL'})}]}});")
  drain();self.assertIn('Import refusé',vm.eval("elements['portable-info'].textContent"));self.assertNotIn('PRIVATE_SENTINEL',vm.eval("elements['portable-info'].textContent"))
  self.assertEqual(vm.eval('requests.length'),3)
 def test_configuration_never_enters_network_or_persistent_storage(self):
  source=(ROOT/'docs/flasher/provision.js').read_text()+(ROOT/'docs/flasher/portable.js').read_text()
  for forbidden in ('fetch(', 'localStorage','sessionStorage','console.log','setSignals','write-flash'):
   self.assertNotIn(forbidden,source)
if __name__=='__main__':unittest.main()
