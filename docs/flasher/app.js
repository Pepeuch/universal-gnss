/* SPDX-License-Identifier: GPL-3.0-only
 * UG portal; conservative license/provenance for the historical GPLv3 flasher.
 * ESP Web Tools is a separately loaded Apache-2.0 dependency, pinned in HTML.
 */
(() => {
  'use strict';
  const variants = {
    's3-gateway': {chip:'ESP32-S3',board:'waveshare-s3-eth',profile:'gateway',output:'espnow',flash_mb:16,psram_mb:8,ethernet:'w5500'},
    'c3-rover-usb': {chip:'ESP32-C3',board:'supermini-c3',profile:'rover',output:'usb',flash_mb:4,psram_mb:0,ethernet:'none'},
    'c3-rover-uart': {chip:'ESP32-C3',board:'supermini-c3',profile:'rover',output:'uart',flash_mb:4,psram_mb:0,ethernet:'none'}
  };
  const sha = value => typeof value==='string' && /^[a-f0-9]{40}$/.test(value);
  const hash = value => typeof value==='string' && /^[a-f0-9]{64}$/.test(value);
  const canonical = value => JSON.stringify(Object.fromEntries(Object.keys(value).sort().map(k=>[k,value[k]])));
  function normalize(input) {
    if (!input || typeof input!=='object' || Array.isArray(input) || !Object.prototype.hasOwnProperty.call(variants,input.variant)) throw Error('Profil non pris en charge');
    const expected={schema:2,variant:input.variant,...variants[input.variant]};
    if (input.variant==='c3-rover-uart') {
      if (!Number.isInteger(input.uart_tx) || ![4,5,6,7,10].includes(input.uart_tx) ||
          !Number.isInteger(input.uart_baud) || ![115200,230400,460800,921600].includes(input.uart_baud)) throw Error('Option UART incompatible');
      Object.assign(expected,{uart_tx:input.uart_tx,uart_baud:input.uart_baud});
    }
    if (Object.keys(input).sort().join(',')!==Object.keys(expected).sort().join(',') ||
        Object.keys(expected).some(k=>typeof input[k]!==typeof expected[k] || input[k]!==expected[k])) throw Error('Option inconnue, runtime ou incompatible');
    return expected;
  }
  async function digest(bytes) {
    return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');
  }
  const json = bytes => JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
  async function keyFor(source,profile) {
    if (!sha(source)) throw Error('SHA source complet requis');
    return (await digest(new TextEncoder().encode(source+'\n'+canonical(normalize(profile))))).slice(0,20);
  }
  function approvals(data) {
    if (!data || data.schema!==1 || !Array.isArray(data.sources) || data.sources.length>128) throw Error('Registre d’approbation invalide');
    const seen=new Set();
    for (const row of data.sources) {
      if (!row || Object.keys(row).sort().join(',')!=='publication,review,source_sha,source_tree' ||
          !sha(row.source_sha) || !sha(row.source_tree) || seen.has(row.source_sha) ||
          typeof row.review!=='string' || !row.review.length || row.review.length>256 ||
          !['main_approved','not_authorized','ci_preview'].includes(row.publication)) throw Error('Approbation invalide ou dupliquée');
      seen.add(row.source_sha);
    }
    return data.sources;
  }
  async function verifyPublished(entry,selected,approval,getBytes) {
    const profile=normalize(entry.profile);
    if (canonical(profile)!==canonical(normalize(selected)) || !approval || !['main_approved','ci_preview'].includes(approval.publication) ||
        approval.source_sha!==entry.source_sha || !sha(entry.source_sha) || !hash(entry.sha256) ||
        !hash(entry.partition_sha256) || !hash(entry.manifest_sha256) ||
        entry.qualification!=='software_only' || entry.install_mode!=='full' || entry.nvs_preservation!=='not_qualified' ||
        !/^espressif\/idf@sha256:[a-f0-9]{64}$/.test(entry.sdk_image || '')) throw Error('Image non approuvée ou contrat incompatible');
    const key=await keyFor(entry.source_sha,profile),directory='flasher/published/'+key+'/';
    if (entry.build_key!==key || entry.manifest!==directory+'manifest.json' || entry.bundle!==directory+'bundle.zip' || !hash(entry.bundle_sha256)) throw Error('Chemin/identité de build incompatible');
    const manifestBytes=await getBytes(entry.manifest,65536);
    if (await digest(manifestBytes)!==entry.manifest_sha256) throw Error('Manifeste altéré');
    const manifest=json(manifestBytes), provenance=json(await getBytes(directory+'provenance.json',65536));
    const qualification=json(await getBytes(directory+'qualification.json',65536));
    const output={transport:profile.output==='uart'?'uart1-tx':'usb-serial-jtag',tx_pin:profile.uart_tx??null,baud:profile.uart_baud??null};
    if (canonical(normalize(provenance.profile))!==canonical(profile) ||
        ['source_sha','sha256','partition_sha256','manifest_sha256','build_key','sdk_image','install_mode','nvs_preservation','qualification'].some(k=>provenance[k]!==entry[k]) ||
        qualification.source_type!=='commit' || qualification.source_commit!==entry.source_sha || qualification.source_object!==entry.source_sha ||
        qualification.source_tree!==approval.source_tree || qualification.variant!==profile.variant || qualification.sdk_image!==entry.sdk_image ||
        qualification.build_and_verify!=='pass' || qualification.reproducibility!=='pass' || qualification.source_mounts!==false ||
        qualification.network_during_build_and_tests!=='disconnected_no_interfaces' || qualification.flash_or_publication!==false ||
        (profile.variant==='s3-gateway' && qualification.host_tests!=='pass') ||
        qualification.binary?.portable_configuration_schema!==1 || qualification.binary?.sha256!==entry.sha256 || qualification.binary?.radio_configuration!=='runtime_nvs_encrypted_unicast' ||
        qualification.binary?.profile!==profile.profile || qualification.binary?.local_gnss!==(profile.profile==='gateway') ||
        (profile.profile==='rover' && Object.keys(output).some(k=>qualification.binary?.rover_output?.[k]!==output[k]))) throw Error('Provenance ou qualification incompatible');
    if (Object.keys(manifest).sort().join(',')!=='builds,name,new_install_prompt_erase,version' || manifest.new_install_prompt_erase!==true ||
        !Array.isArray(manifest.builds) || manifest.builds.length!==1 || Object.keys(manifest.builds[0]).sort().join(',')!=='chipFamily,parts' || manifest.builds[0].chipFamily!==profile.chip ||
        !Array.isArray(manifest.builds[0].parts) || manifest.builds[0].parts.length!==4) throw Error('Manifeste matériel incompatible');
    const data=[],names=new Set(),basenames=new Set();
    for (const part of manifest.builds[0].parts) {
      if (!part || Object.keys(part).sort().join(',')!=='offset,path' || typeof part.path!=='string' || !/^[a-f0-9]{6}-(bootloader|partition-table|ota_data_initial|universal-gnss-esp32)\.bin$/.test(part.path) ||
          names.has(part.path) || !Number.isSafeInteger(part.offset) || part.offset<0 ||
          part.path.slice(0,6)!==part.offset.toString(16).padStart(6,'0') || !hash(provenance.part_sha256?.[part.path])) throw Error('Segment invalide');
      names.add(part.path);
      const raw=await getBytes(directory+part.path,profile.flash_mb*1024*1024);
      if (!raw.byteLength || part.offset+raw.byteLength>profile.flash_mb*1024*1024 || provenance.part_sizes?.[part.path]!==raw.byteLength ||
          await digest(raw)!==provenance.part_sha256[part.path]) throw Error('Segment altéré ou hors Flash');
      const basename=part.path.slice(7),original=basename==='bootloader.bin'?'bootloader/'+basename:basename==='partition-table.bin'?'partition_table/'+basename:basename;
      if (basenames.has(basename) || qualification.flash_offsets?.[original]!==part.offset) throw Error('Segment dupliqué ou offset non qualifié');
      basenames.add(basename);
      if (qualification.part_sha256?.[original]!==provenance.part_sha256[part.path]) throw Error('Segment non qualifié');
      if (basename==='partition-table.bin' && provenance.part_sha256[part.path]!==entry.partition_sha256) throw Error('Partition non qualifiée');
      if (basename==='universal-gnss-esp32.bin' && (provenance.part_sha256[part.path]!==entry.sha256 ||
          qualification.binary.app_bytes!==raw.byteLength || qualification.binary.app_slots?.[0]?.offset!==part.offset)) throw Error('Application non qualifiée');
      data.push({...part,bytes:raw});
    }
    const ordered=data.slice().sort((a,b)=>a.offset-b.offset);
    if (ordered.some((part,i)=>i && ordered[i-1].offset+ordered[i-1].bytes.byteLength>part.offset)) throw Error('Segments superposés');
    return {entry,manifest,parts:data};
  }
  async function boundedFetch(path,limit,signal,branchAPI=false) {
    const url=new URL(path,document.baseURI);
    if (url.origin!==location.origin && !(branchAPI && url.origin==='https://api.github.com' && url.pathname==='/repos/Pepeuch/universal-gnss/branches')) throw Error('Ressource hors du portail');
    const response=await fetch(url,{cache:'no-store',credentials:'omit',signal});
    if (!response.ok || Number(response.headers.get('Content-Length') || 0)>limit) throw Error('Ressource indisponible ou trop grande (HTTP '+response.status+')');
    if (!response.body?.getReader) {
      const bytes=new Uint8Array(await response.arrayBuffer());if(bytes.byteLength>limit)throw Error('Ressource trop grande');return bytes;
    }
    const reader=response.body.getReader(),chunks=[];let total=0;
    try {
      for (let n=0;n<4096;++n) {
        const {done,value}=await reader.read();if(done){const out=new Uint8Array(total);let pos=0;for(const c of chunks){out.set(c,pos);pos+=c.byteLength}return out}
        total+=value.byteLength;if(total>limit)throw Error('Budget de téléchargement dépassé');chunks.push(value);
      }
      throw Error('Budget de segments réseau dépassé');
    } catch(error) {await reader.cancel();throw error;}
    finally {reader.releaseLock();}
  }
  function publishedVersions(registry, catalog, history) {
    // Availability comes from the published catalogue, never optional metadata.
    const rows=new Map();
    for(const entry of catalog.images) {
      if(!sha(entry.source_sha))continue;
      rows.set(entry.source_sha,{source_sha:entry.source_sha,published_at:null,status:'published',channel:'',release_tag:''});
    }
    if(history?.schema===1&&Array.isArray(history.publications)) {
      for(const row of history.publications) {
        if(!row||!rows.has(row.source_sha))continue;
        const target=rows.get(row.source_sha);
        if(typeof row.published_at==='string'&&Number.isFinite(Date.parse(row.published_at)))target.published_at=row.published_at;
        if(typeof row.status==='string')target.status=row.status;
        if(typeof row.channel==='string')target.channel=row.channel;
        if(typeof row.release_tag==='string')target.release_tag=row.release_tag;
      }
    }
    return [...rows.values()].sort((a,b)=>(Date.parse(b.published_at)||0)-(Date.parse(a.published_at)||0));
  }
  function branchVersions(branch,catalog,registry,history,mapping,heads={}) {
    if(branch==='main'&&!Array.isArray(registry?.sources))return publishedVersions(registry,catalog,history);
    const preview=mapping?.schema===1?mapping.branches?.[branch]:null;
    const current=sha(preview?.source_sha)?preview.source_sha:null;
    const allowed=new Set(current?[current]:[]);
    if(branch==='main')for(const row of registry?.sources||[])if(row?.publication==='main_approved'&&sha(row.source_sha))allowed.add(row.source_sha);
    return publishedVersions(registry,catalog,history).filter(row=>allowed.has(row.source_sha));
  }
  async function loadBranchHeads(signal) {
    const heads=Object.create(null);
    for(let page=1;page<=10;page++) {
      const rows=json(await boundedFetch('https://api.github.com/repos/Pepeuch/universal-gnss/branches?per_page=100&page='+page,1048576,signal,true));
      if(!Array.isArray(rows))throw Error('Liste des branches invalide');
      for(const row of rows)if(typeof row.name==='string'&&sha(row.commit?.sha))heads[row.name]=row.commit.sha;
      if(rows.length<100)return heads;
    }
    throw Error('Liste des branches trop grande');
  }
  async function loadCatalogue(signal) {
    const catalog=json(await boundedFetch('flasher/catalog.json',2097152,signal));
    if(!catalog||catalog.schema!==2||!Array.isArray(catalog.images)||catalog.images.length>512)throw Error('Catalogue invalide');
    const optional=async(path)=>{try{return json(await boundedFetch(path,65536,signal));}catch{return null;}};
    const [registry,history,branches]=await Promise.all([optional('flasher/approved-sources.json'),optional('flasher/publications.json'),optional('flasher/branches.json')]);
    let sources=[];
    try {sources=approvals(registry);}catch{}
    return {catalog,registry,history,branches,sources,versions:publishedVersions(registry,catalog,history)};
  }
  function initialize() {
    const $=id=>document.getElementById(id);let images=[],sources=[],generation=0,controller=null,current=null,urls=[],loadedState=null,branchHeads={},catalogueGeneration=0,catalogueController=null;
    const status=text=>{$('status').textContent=text;};
    function clear() {
      controller?.abort();controller=null;current=null;
      for(const url of urls)URL.revokeObjectURL(url);urls=[];
      $('installer').removeAttribute('manifest');$('flash-available').classList.add('hidden');$('flash-unavailable').classList.remove('hidden');
      $('image-info').textContent='';$('downloads').replaceChildren();$('consent').checked=false;
    }
    function selected() {
      const variant=$('device').value==='c3-rover-uart'?'c3-rover-usb':$('device').value,input={schema:2,variant,...variants[variant]};
      if(variant==='c3-rover-uart')Object.assign(input,{uart_tx:Number($('tx').value),uart_baud:Number($('baud').value)});
      return normalize(input);
    }
    function request(profile,source) {
      $('request').href='https://github.com/Pepeuch/universal-gnss/actions/workflows/esp32.yml?'+new URLSearchParams({query:'branch:'+$('branch').value});
      $('request').textContent='Voir la compilation CI de cette branche';
      $('request').classList.remove('hidden');
    }
    function selectBranch() {
      clear();$('version').replaceChildren();
      if(!loadedState)return;
      const versions=branchVersions($('branch').value,loadedState.catalog,loadedState.registry,loadedState.history,loadedState.branches,branchHeads);
      for(const item of (versions.length?versions:[{source_sha:'none'}])) {
        const option=document.createElement('option');option.value=item.source_sha;
        option.textContent=sha(item.source_sha)?$('branch').value+' — '+(sources.find(s=>s.source_sha===item.source_sha)?.publication==='ci_preview'?'DERNIER BUILD CI':(item.channel||'Publié').toUpperCase())+' — '+(item.release_tag&&item.release_tag!=='esp32-ci-builds'?item.release_tag+' — ':'')+item.source_sha+' — '+(item.published_at||'date indisponible'):'Aucun build CI disponible pour cette branche';
        $('version').append(option);
      }
    }
    function summary() {
      const p=selected();$('summary').replaceChildren();
      for(const [label,value] of Object.entries({Branche:$('branch').value,'HEAD branche':branchHeads[$('branch').value]||'indisponible',Profil:p.variant,Flash:p.flash_mb+' MB',PSRAM:p.psram_mb+' MB',Ethernet:p.ethernet,Version:$('version').value,UART:p.uart_tx===undefined?'non':`GPIO${p.uart_tx} / ${p.uart_baud}`})) {
        const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=label;b.textContent=value;$('summary').append(a,b);
      }
    }
    async function render() {
      const version=++generation;clear();const p=selected(),source=$('version').value;
      portableForm(p.board);
      $('source').disabled=p.profile!=='gateway';$('receiver').disabled=p.profile!=='gateway';
      $('tx-block').classList.add('hidden');$('baud-block').classList.add('hidden');summary();request(p,source);
      const approval=sources.find(s=>s.source_sha===source);
      const candidates=images.filter(e=>e.source_sha===source && canonical(normalize(e.profile))===canonical(p));
      if(candidates.length!==1){status(candidates.length?'Catalogue ambigu : flash refusé.':'Aucun firmware CI disponible pour cette branche/profil. Attendre la compilation automatique ; aucun accord de publication n’est nécessaire pour un build de branche.');return}
      if(!approval){$('request').classList.add('hidden');status('Firmware publié disponible ; registre de provenance indisponible. Réessayez pour vérifier les binaires.');return}
      const requestController=new AbortController();controller=requestController;const signal=requestController.signal,timer=setTimeout(()=>requestController.abort(),30000);
      status('Vérification de la source, des partitions et de tous les segments…');
      try {
        const verified=await verifyPublished(candidates[0],p,approval,(path,limit)=>boundedFetch(path,limit,signal));
        if(version!==generation)return;
        // Freeze exactly the verified bytes. ESP Web Tools must never refetch mutable parts.
        const parts=verified.parts.map(part=>{const path=URL.createObjectURL(new Blob([part.bytes]));urls.push(path);return {path,offset:part.offset}});
        const manifest={...verified.manifest,builds:[{chipFamily:p.chip,parts}]};
        const manifestUrl=URL.createObjectURL(new Blob([JSON.stringify(manifest)],{type:'application/json'}));urls.push(manifestUrl);
        current={manifestUrl};
        for(let i=0;i<parts.length;++i){const a=document.createElement('a');a.href=parts[i].path;a.download=verified.parts[i].path;a.textContent='Télécharger '+verified.parts[i].path;a.className='button secondary';$('downloads').append(a)}
        $('image-info').textContent=`Source ${source} · SHA256 ${verified.entry.sha256} · qualification logicielle uniquement · installation complète.`;
        $('request').classList.add('hidden');
        status((approval.publication==='ci_preview'?'Build de développement CI — ':'Firmware publié — ')+'branche '+$('branch').value+' ; octets et provenance vérifiés. Qualification matérielle distincte. Confirmer la carte et accepter l’installation complète avant d’activer le flash.');
      } catch(error) {if(version===generation){clear();status('Image refusée : '+error.message)}}
      finally {clearTimeout(timer);}
    }
    let portableBoard=null;
    function portableForm(board) {
      if(portableBoard===board)return;portableBoard=board;
      const value=UGPortable.defaults(board),s3=board==='waveshare-s3-eth';
      for(const id of ['portable-tx','portable-rx']){
        $(id).replaceChildren();
        for(const pin of [-1,...UGPortable.pins[board]]){const option=document.createElement('option');option.value=String(pin);option.textContent=pin<0?'Non affecté':'GPIO'+pin;$(id).append(option)}
      }
      $('portable-output').replaceChildren();
      for(const output of (s3?['gnss']:['usb','uart'])){const option=document.createElement('option');option.value=output;option.textContent=output;$('portable-output').append(option)}
      $('portable-model').replaceChildren();
      for(const model of (s3?UGPortable.models.slice(1):['none'])){const option=document.createElement('option');option.value=model;option.textContent=model;$('portable-model').append(option)}
      $('portable-baud').replaceChildren();
      for(const baud of (s3?[9600,38400,115200,230400,460800,921600]:[115200,230400,460800,921600])){const option=document.createElement('option');option.value=String(baud);option.textContent=String(baud);$('portable-baud').append(option)}
      $('portable-output').value=value.output;$('portable-tx').value='-1';$('portable-rx').value='-1';$('portable-baud').value='115200';$('portable-model').value=value.gnss_model;$('portable-channel').value='1';
      $('portable-tx').disabled=!s3;$('portable-baud').disabled=!s3;
      $('portable-output').disabled=s3;$('portable-rx').disabled=!s3;$('portable-model').disabled=!s3;
      $('portable-import').value='';$('portable-info').textContent=JSON.stringify(value,null,2);
    }
    function portableValue(){
      return UGPortable.validate({...UGPortable.defaults(selected().board),output:$('portable-output').value,uart_tx:Number($('portable-tx').value),uart_rx:Number($('portable-rx').value),uart_baud:Number($('portable-baud').value),gnss_model:$('portable-model').value,radio_channel:Number($('portable-channel').value)},selected().board);
    }
    for(let c=1;c<=11;++c){const option=document.createElement('option');option.value=String(c);option.textContent=String(c);$('portable-channel').append(option)}
    for(const id of ['portable-output','portable-tx','portable-rx','portable-baud','portable-model','portable-channel'])$(id).addEventListener('change',()=>{
      try{$('portable-tx').disabled=$('portable-output').value==='usb';$('portable-baud').disabled=$('portable-output').value==='usb';if(id==='portable-output' && $('portable-output').value==='usb'){$('portable-tx').value='-1';$('portable-rx').value='-1';$('portable-baud').value='115200';}$('portable-info').textContent=JSON.stringify(portableValue(),null,2)}catch(error){$('portable-info').textContent=error.message}
    });
    $('portable-import').addEventListener('change',async event=>{
      const file=event.target.files?.[0],board=selected().board;event.target.value='';
      try {
        if(!file || file.size>512)throw Error('Fichier UG : maximum 512 octets');
        const value=UGPortable.parse(await file.text(),board);if(selected().board!==board)return;
        if(!Array.from($('portable-baud').options).some(o=>o.value===String(value.uart_baud))){const option=document.createElement('option');option.value=String(value.uart_baud);option.textContent=String(value.uart_baud);$('portable-baud').append(option)}
        for(const [field,id] of Object.entries({output:'portable-output',uart_tx:'portable-tx',uart_rx:'portable-rx',uart_baud:'portable-baud',gnss_model:'portable-model',radio_channel:'portable-channel'}))$(id).value=String(value[field]);
        $('portable-tx').disabled=value.output==='usb';$('portable-baud').disabled=value.output==='usb';
        $('portable-info').textContent=JSON.stringify(value,null,2);
      }catch(error){$('portable-info').textContent='Import refusé : '+error.message;}
    });
    $('portable-export').addEventListener('click',()=>{
      try {
        const value=portableValue(),url=URL.createObjectURL(new Blob([JSON.stringify(value,null,2)+'\n'],{type:'application/json'}));
        const link=document.createElement('a');link.href=url;link.download='ug-portable-config.json';link.click();URL.revokeObjectURL(url);
      }catch(error){$('portable-info').textContent=error.message;}
    });
    $('portable-restore').addEventListener('click',async()=>{
      const button=$('portable-restore');if(button.disabled)return;
      try {
        const value=portableValue();if(!navigator.serial)throw Error('Web Serial indisponible ; utiliser l’outil Linux local');
        button.disabled=true;
        const port=await navigator.serial.requestPort();
        const result=await UGProvision.restore(port,value,async(before,after)=>confirm('Appliquer localement ces réglages ? Aucun secret ni association ne sera importé. GNSS S3 désactivé avant revue.\nAvant : '+JSON.stringify(before)+'\nAprès : '+JSON.stringify(after)));
        $('portable-info').textContent=result.applied?'Configuration enregistrée localement. Redémarrage/réactivation explicites requis ; aucun flash effectué.':'Application annulée ; aucune écriture.';
      }catch(error){$('portable-info').textContent='Restauration refusée : '+error.message;}
      finally{button.disabled=false;}
    });
    $('consent').addEventListener('change',()=>{
      const ready=Boolean(current && $('consent').checked);
      $('flash-available').classList.toggle('hidden',!ready);$('flash-unavailable').classList.toggle('hidden',ready);
      if(ready)$('installer').setAttribute('manifest',current.manifestUrl);else $('installer').removeAttribute('manifest');
    });
    for(const id of ['device','version','tx','baud'])$(id).addEventListener('change',()=>render().catch(e=>status(e.message)));
    $('branch').addEventListener('change',()=>{selectBranch();render().catch(e=>status(e.message));});
    $('check').addEventListener('click',()=>reloadCatalogue());
    for(const id of ['source','receiver'])$(id).addEventListener('change',summary);
    $('download-profile').addEventListener('click',()=>{
      const p=selected(),url=URL.createObjectURL(new Blob([JSON.stringify(p,null,2)+'\n'],{type:'application/json'}));
      const a=document.createElement('a');a.href=url;a.download=p.variant+'-profile.json';a.click();URL.revokeObjectURL(url);
    });
    async function reloadCatalogue(){
      const load=++catalogueGeneration;catalogueController?.abort();++generation;clear();status('Actualisation du catalogue…');
      const timeout=new AbortController();catalogueController=timeout;
      const timer=setTimeout(()=>timeout.abort(),15000);
      try {
        const loaded=await loadCatalogue(timeout.signal);
        let heads={};try{heads=await loadBranchHeads(timeout.signal);}catch{}
        if(load!==catalogueGeneration)return;
        const wanted=$('branch').value||'main';
        sources=loaded.sources;images=loaded.catalog.images;loadedState=loaded;branchHeads=heads;
        for(const entry of images)normalize(entry.profile);
        const branches=new Set(['main','dev',...Object.keys(loaded.branches?.branches||{}),...Object.keys(branchHeads)]);
        $('branch').replaceChildren();
        for(const branch of [...branches].sort((a,b)=>(a==='main'?-1:b==='main'?1:a==='dev'?-1:b==='dev'?1:a.localeCompare(b)))) {
          const option=document.createElement('option');option.value=branch;option.textContent=branch;$('branch').append(option);
        }
        if(branches.has(wanted))$('branch').value=wanted;
        selectBranch();
        await render();
      } catch(error){if(load===catalogueGeneration){clear();status('Portail non disponible : '+error.message)}}finally{clearTimeout(timer)}
    }
    reloadCatalogue();
  }
  globalThis.UGFlasher={normalize,keyFor,approvals,verifyPublished,publishedVersions,loadCatalogue,branchVersions,loadBranchHeads};
  if(typeof document!=='undefined')initialize();
})();
