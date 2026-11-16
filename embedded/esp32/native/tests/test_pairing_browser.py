"""Chromium Web Serial doubles; no physical ports, firmware writes or radio."""
from pathlib import Path
from playwright.sync_api import sync_playwright
SOURCE = Path(__file__).resolve().parents[2]/'components/ug_web/assets/pairing.js'
FIXTURE = r'''
const enc=new TextEncoder(), dec=new TextDecoder();
let commands=[], apiCalls=[], notices=[], storedKey='', radioKey='', paired=false, readerController;
let rover={mode:'control',nonce:'',writable:true,firmware:'universal-gnss',board:'esp32c3',role:'rover',sta_mac:'02:00:00:00:00:03',peer:'00:00:00:00:00:00',channel:1,key_set:false,paired:false};
let base={sta_mac:'02:00:00:00:00:02',configured_channel:1,peer:'',key_set:false,enabled:false};
const abort=new AbortController();
function respond(value){readerController.enqueue(enc.encode('ROM noise\nUGCFG/1 '+JSON.stringify(value)+'\n'));}
const port={
 async open(){
  if(scenario==='open')throw Error('open failed');
  this.readable=new ReadableStream({start(c){readerController=c;},cancel(){}});
  this.writable=new WritableStream({write(bytes){
   const text=dec.decode(bytes).trim();commands.push(text);
   if(scenario==='cancel-before-write' && text.startsWith('UGCFG/1 OPEN'))abort.abort();
   if(text.startsWith('UGCFG/1 OPEN')){
    rover.nonce=text.split(' ').pop();
    if(scenario==='nonce')rover.nonce='stale';
    if(scenario==='wrong-board')rover.board='esp32s3';
    if(scenario==='wrong-role')rover.role='relay-ntrip';
    if(scenario==='locked')rover.writable=false;
    respond({...rover});
   } else if(text==='UGCFG/1 CONFIG_GET'){
    const portable=UGPortable.defaults('supermini-c3');
    if(scenario==='uart'){portable.output='uart';portable.uart_tx=4;}
    respond(scenario==='invalid-portable'?{error:'portable_invalid'}:{portable});
   } else if(text.startsWith('UGCFG/1 SET')){
    const parts=text.split(' ');rover.peer=parts[2];rover.channel=Number(parts[3]);storedKey=parts[4];rover.paired=rover.key_set=true;
    if(scenario==='disconnect-after-set'){readerController.close();return;}
    if(scenario==='timeout-after-set')return;
    if(scenario==='bad-save')rover.channel=11;
    respond({...rover});
   } else if(text==='UGCFG/1 GET'){
    if(scenario==='mismatch')rover.peer='02:00:00:00:00:09';
    respond({...rover});
   } else if(text==='UGCFG/1 RUN CONFIRM_RTCM'){
    respond(scenario==='run-error'?{error:'cannot run'}:{mode:'closed'});
   } else throw Error('unexpected command');
  }});
 },
 async close(){this.closed=true;if(scenario==='cleanup')throw Error('close failed');}
};
const api=async(path,body)=>{
 apiCalls.push(path);
 if(path==='/api/config')return {wifi:{sta:scenario==='sta'}};
 if(path.endsWith('/window')){if(scenario==='window-error')throw Error('window refused');return {...base,window_open:true};}
 if(path.endsWith('/apply')){
  if(scenario==='s3-reject')throw Error('must not echo '+body.key);
  radioKey=body.key;base={...base,peer:body.peer,key_set:true,enabled:true};
  if(scenario==='cancel-after-apply')abort.abort();
 }
 if(path.endsWith('/restart') && scenario==='restart-error')throw Error('restart');
 return {...base};
};
async function exercise(){
 try{
  const result=await UGRoverPairing.pair(port,api,text=>notices.push(text),abort.signal,async(channel,replacing)=>{
   if(channel!==1)throw Error('wrong channel');return scenario!=='decline';
  });
  return {ok:true,result,closed:port.closed,commands:commands.map(c=>c.startsWith('UGCFG/1 SET')?'SET':c),
   equal:storedKey.length===32 && storedKey===radioKey, key:storedKey, notices, apiCalls};
 }catch(e){return {ok:false,error:e.message,closed:port.closed,set:commands.some(c=>c.startsWith('UGCFG/1 SET')),apiCalls,notices,key:storedKey};}
}
'''
def run():
    cases = ['nominal','decline','open','nonce','wrong-board','wrong-role','locked','cancel-before-write','sta','window-error','uart','invalid-portable',
             'disconnect-after-set','timeout-after-set','bad-save','s3-reject','mismatch','run-error','restart-error','cleanup','cancel-after-apply']
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for scenario in cases:
            context=browser.new_context()
            page=context.new_page()
            page.goto('about:blank')
            page.evaluate('globalThis.scenario='+repr(scenario))
            page.add_script_tag(content=(Path(__file__).resolve().parents[4]/'docs/flasher/portable.js').read_text())
            page.add_script_tag(content=SOURCE.read_text())
            page.add_script_tag(content=FIXTURE)
            result=page.evaluate('exercise()')
            if scenario=='nominal':
                assert result['ok'] and result['equal'] and result['closed'], result
                assert result['result']['configured'] and result['apiCalls'][-1].endswith('/restart')
                assert result['commands'][-1]=='UGCFG/1 RUN CONFIRM_RTCM'
            elif scenario=='decline':
                assert result['ok'] and result['result']['cancelled'] and result['closed']
                assert not result['equal'] and not any(x.endswith('/apply') for x in result['apiCalls'])
            else:
                assert not result['ok'], scenario
                if scenario!='open':assert result['closed'], scenario
                if scenario in ('nonce','wrong-board','wrong-role','locked','cancel-before-write','sta','window-error','uart','invalid-portable'):
                    assert not result['set'] and not any(x.endswith('/apply') for x in result['apiCalls']), scenario
                if scenario in ('disconnect-after-set','timeout-after-set','bad-save','s3-reject','mismatch','cancel-after-apply'):
                    assert 'partiel ou incertain' in result['error'], scenario
                if scenario in ('run-error','restart-error'):
                    assert 'démarrage reste à vérifier' in result['error'], scenario
                assert not result.get('key') or result['key'] not in result['error']
            assert not page.evaluate('localStorage.length') if page.url.startswith('https') else True
            assert not result.get('key') or all(result['key'] not in n for n in result['notices'])
            context.close()
            print('Chromium pairing '+scenario+': PASS')
        browser.close()
if __name__=='__main__':run()
