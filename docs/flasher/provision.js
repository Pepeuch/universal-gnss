/* SPDX-License-Identifier: LGPL-3.0-only
 * Explicit local Web Serial transport. No fetch, reset, flash, persistent storage or logs.
 */
(() => {
'use strict';
async function restore(port,input,confirmChange) {
 const value=UGPortable.validate(input),text=JSON.stringify(value);
 let reader,writer,opened=false,failure=null;
 const decoder=new TextDecoder();let pending='',budget=0;
 async function reply(prefix) {
  const end=Date.now()+5000;
  for(;;) {
   const newline=pending.indexOf('\n');
   if(newline>=0){const line=pending.slice(0,newline).replace(/\r$/,'');pending=pending.slice(newline+1);if(line.startsWith(prefix))return line.slice(prefix.length);continue;}
   if(Date.now()>=end)throw Error('Délai USB dépassé');
   let timeout;
   const chunk=await Promise.race([reader.read(),new Promise((_,reject)=>{timeout=setTimeout(()=>reject(Error('Délai USB dépassé')),Math.max(1,end-Date.now()));})]).finally(()=>clearTimeout(timeout));
   if(chunk.done)throw Error('USB déconnecté');budget+=chunk.value.length;
   if(budget>65536)throw Error('Budget de réponse USB dépassé');
   pending+=decoder.decode(chunk.value,{stream:true});if(pending.length>4096)throw Error('Réponse USB trop grande');
  }
 }
 async function send(command,prefix) {
  let deadline;try{
   await Promise.race([writer.write(new TextEncoder().encode(command+'\n')),new Promise((_,reject)=>{deadline=setTimeout(()=>reject(Error('Écriture USB expirée')),3000);})]);
  }finally{clearTimeout(deadline);}
  const data=await reply(prefix);return data;
 }
 try {
  await port.open({baudRate:115200,bufferSize:2048});opened=true;
  reader=port.readable.getReader();writer=port.writable.getWriter();
  if(value.board==='supermini-c3') {
   const random=new Uint8Array(8);crypto.getRandomValues(random);const nonce=Array.from(random,n=>n.toString(16).padStart(2,'0')).join('');
   const state=JSON.parse(await send('UGCFG/1 OPEN '+nonce,'UGCFG/1 '));
   if(state.nonce!==nonce || state.mode!=='control')throw Error('Maintenance C3 non reconnue ; reset manuel requis');
   const before=JSON.parse(await send('UGCFG/1 CONFIG_GET','UGCFG/1 '));
   const checked=JSON.parse(await send('UGCFG/1 CONFIG_CHECK '+text,'UGCFG/1 '));
   if(checked.error || checked.result!=='valid')throw Error('Configuration refusée par le C3');
   const canonical=UGPortable.validate(checked.portable,value.board);
   if(JSON.stringify(canonical)!==text)throw Error('Aperçu C3 différent');
   if(!await confirmChange(before.error?null:UGPortable.validate(before.portable,value.board),canonical))return {applied:false};
   const saved=JSON.parse(await send('UGCFG/1 CONFIG_APPLY CONFIRM '+text,'UGCFG/1 '));
   if(saved.error || saved.result!=='saved' || JSON.stringify(UGPortable.validate(saved.portable,value.board))!==text)throw Error('Écriture C3 non confirmée');
  } else {
   const before=UGPortable.parse(await send('portable-export','UGPORT/1 JSON '),value.board);
   if(await send('portable-check '+text,'UGPORT/1 ')!=='VALID')throw Error('Configuration refusée par le S3');
   if(!await confirmChange(before,value))return {applied:false};
   if(await send('portable-apply CONFIRM '+text,'UGPORT/1 ')!=='SAVED_RESTART_REQUIRED')throw Error('Écriture S3 non confirmée');
  }
  // Do not automatically RUN/reset: controls never share a live RTCM data plane.
  return {applied:true,restart_required:true};
 } catch(error) {failure=error;throw error;}
 finally {
  let cleanupFailed=false;
  async function clean(operation) {
   let deadline;try{await Promise.race([operation(),new Promise((_,reject)=>{deadline=setTimeout(()=>reject(Error('Nettoyage USB expiré')),2000);})]);}
   catch(_){cleanupFailed=true;}finally{clearTimeout(deadline);}
  }
  if(reader){await clean(()=>reader.cancel());try{reader.releaseLock()}catch(_){cleanupFailed=true}}
  if(writer){if(failure && writer.abort)await clean(()=>writer.abort());try{writer.releaseLock()}catch(_){cleanupFailed=true}}
  if(opened)await clean(()=>port.close());
  if(cleanupFailed && !failure)throw Error('Nettoyage USB incomplet ; état de connexion à vérifier');
 }
}
globalThis.UGProvision={restore};
})();
