'use strict';
let token = '', sessionGeneration = 0, refreshing = false, receiverPlans = null;
let portableCandidate=null;
function clearPortable(){portableCandidate=null;$('portable-file').value='';$('portable-apply').disabled=true;$('portable-preview').textContent='Aucun import préparé';}
let wireSelfTestRunning = false, wireSelfTestAllowed = false;
let ntripConfig = null, ntripSlots = 0, ntripGnss = false, ntripSaving = false, ntripMutationAllowed = false;
let functionsConfig=null;
const $ = id => document.getElementById(id);
const show = value => { $('notice').textContent = typeof value === 'object' ? print(value) : String(value); };
const print = value => JSON.stringify(value, null, 2);
const ntripTextFields = ['host','mountpoint','username','ca_file','client_cert_file','client_key_file'];
function ntripTarget() {
  const target = $('ntrip-target').value;
  if (target === 'upstream') return null;
  if (!/^[0-4]$/.test(target) || Number(target) >= ntripSlots) throw new Error('Publication indisponible sur cette carte');
  return Number(target);
}
function clearNtripSecret() {
  $('ntrip-password').value = ''; $('ntrip-password-clear').checked = false;
}
function renderNtrip() {
  clearNtripSecret();
  const slot = ntripTarget(), owner = slot === null ? ntripConfig?.upstream : ntripConfig?.publishers?.find(p => p.slot === slot);
  const endpoint = owner?.endpoint || {};
  for (const key of ntripTextFields) $('ntrip-' + key).value = endpoint[key] || '';
  $('ntrip-enabled').checked = Boolean(owner?.enabled);
  $('ntrip-tls').checked = Boolean(endpoint.tls);
  $('ntrip-port').value = String(endpoint.port || 2101);
  $('ntrip-send_gga').checked = Boolean(endpoint.send_gga);
  $('ntrip-send_gga').disabled = !ntripGnss;
  $('ntrip-gga_interval_s').value = String(endpoint.gga_interval_s || 5);
  $('ntrip-gga').hidden = slot !== null;
  $('ntrip-password-status').textContent = endpoint.password_set ? 'Mot de passe enregistré ; valeur masquée.' : 'Aucun mot de passe enregistré.';
  $('ntrip-save').disabled = !token || !ntripConfig || ntripSaving || !ntripMutationAllowed;
}
function clearFunctions(){
 functionsConfig=null;
 for(const id of ['function-caster-password','function-password','function-ap-password','function-ssid','function-ap-ssid','function-caster-user','function-caster-mount'])$(id).value='';
 for(const id of ['function-radio','function-caster','function-sta','function-ap'])$(id).checked=false;
 $('functions-save').disabled=true;$('functions-state').textContent='Session fermée';
}
function renderFunctions(config){
 functionsConfig=config;
 $('function-radio').checked=Boolean(config.radio?.enabled);$('function-caster').checked=Boolean(config.caster?.enabled);$('function-sta').checked=Boolean(config.wifi?.sta);$('function-ap').checked=Boolean(config.wifi?.ap);
 $('function-caster-port').value=String(config.caster?.port||2101);$('function-caster-mount').value=config.caster?.mountpoint||'';$('function-caster-user').value=config.caster?.username||'';
 $('function-ssid').value=config.wifi?.ssid||'';$('function-ap-ssid').value=config.wifi?.ap_ssid||'UG-RTK';
 for(const id of ['function-caster-password','function-password','function-ap-password'])$(id).value='';
 $('functions-save').disabled=!token||!ntripMutationAllowed;
 $('functions-state').textContent=print({activation_demandee:{gnss_uart:Boolean(config.gnss?.enabled),ntrip_client:Boolean(config.upstream?.enabled),espnow:Boolean(config.radio?.enabled),caster:Boolean(config.caster?.enabled),wifi_sta:Boolean(config.wifi?.sta),wifi_ap:Boolean(config.wifi?.ap)},etat_reel:'Consulter les services : activation ≠ réception.'});
}
function clearPairingForm() { $('pairing-key').value = ''; $('pairing-ticket').value = ''; $('pairing-peer').value = ''; $('pairing-info').textContent = 'Session fermée'; }
function clearNtripForm() { ntripMutationAllowed = false; ntripConfig = null; $('ntrip-target').value = 'upstream'; renderNtrip(); }
let requestQueue = Promise.resolve(), configRead = 0, editGeneration = 0;
function call(path, body, authorized = true) {
  const generation = sessionGeneration, credential = token;
  const run = async () => {
    if (authorized && generation !== sessionGeneration) throw Error('Session modifiée');
    const headers = {};
    if (authorized && credential) headers.Authorization = `Bearer ${credential}`;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(path, {method: body === undefined ? 'GET' : 'POST', headers,
        body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store', credentials: 'omit', signal: controller.signal});
      const text = await response.text();
      if (!response.ok) throw Error(`${response.status} ${text}`);
      if (authorized && generation !== sessionGeneration) throw Error('Session modifiée');
      return response.headers.get('Content-Type')?.includes('application/json') ? JSON.parse(text) : text;
    } finally { clearTimeout(timer); }
  };
  const result = requestQueue.then(run);
  requestQueue = result.catch(() => {});
  return result;
}
function satellites(report) {
  const rows = $('satellite-rows'); rows.replaceChildren();
  $('satellite-summary').textContent = !report.available ? 'Rapport non disponible' :
    `${report.source} ${report.talker || ''} ${report.signal_id === undefined ? '' : 'signal ' + report.signal_id} — ${report.stale ? 'périmé' : 'reçu récemment'} — âge ${report.report_age_ms ?? '?'} ms${report.truncated ? ' — liste tronquée' : ''}`;
  for (const sat of (report.satellites || [])) {
    const row = document.createElement('tr');
    for (const value of [sat.svid, sat.raw_system_id, sat.elevation, sat.azimuth, sat.cn0,
                         sat.used === undefined ? undefined : sat.used ? 'Oui' : 'Non']) {
      const cell = document.createElement('td'); cell.textContent = value === undefined ? 'Inconnu' : String(value); row.appendChild(cell);
    }
    rows.appendChild(row);
  }
}

const tabs = ['dashboard','gnss','rtcm','network','diagnostics','settings'];
const periods = [5000,3000,5000,5000,5000,10000];
let activeTab = 'dashboard', viewGeneration = 0, refreshTimer;
function scheduleRefresh(delay = periods[tabs.indexOf(activeTab)]) {
  clearTimeout(refreshTimer);
  if (!document.hidden) refreshTimer = setTimeout(() => refresh(), delay);
}
function selectTab(name) {
  if (!tabs.includes(name)) return;
  activeTab = name; ++viewGeneration;
  for (const key of tabs) {
    $('tab-' + key).hidden = key !== name;
    $('nav-' + key).setAttribute('aria-selected', String(key === name));
    $('nav-' + key).tabIndex = key === name ? 0 : -1;
  }
  refresh();
}
function unavailable() {
  for (const key of ['global','ethernet','https','gnss','rtcm','ntrip','memory']) $('card-' + key).textContent = 'Non actualisé';
  receiverPlans = null;
  for (const id of ['base-submit','base-disable','telemetry-submit']) $(id).disabled = true;
}
function renderStatus(status) {
  $('status').textContent = print(status);
  $('services-status').textContent = print(status.services || {});
  $('network-status').textContent = print({ethernet:status.services?.ethernet_driver,https:status.services?.https,espnow:status.espnow});
  $('card-global').textContent = status.degraded === false ? 'Services réactifs' : 'État dégradé / inconnu';
  for (const [card,service] of [['ethernet','ethernet_driver'],['https','https']]) $('card-' + card).textContent = 'Service : ' + (status.services?.[service]?.state || 'inconnu');
  $('card-rtcm').textContent = `${status.rtcm_frames ?? '?'} trames · ${status.rtcm_rejected ?? '?'} rejetées`;
  $('card-ntrip').textContent = status.upstream_flowing ? 'Flux entrant observé' : status.upstream_connected ? 'Connecté, flux non confirmé' : 'Pas de connexion entrante';
}
function renderReceiver(receiver) {
  $('receiver').textContent = print(receiver);
  $('card-gnss').textContent = !receiver.present ? 'Non détecté' : receiver.position_stale !== false || !receiver.fix_valid ? 'Position non valide / périmée' :
    `${['Inconnu','Aucun FIX','FIX','RTK FLOAT','RTK FIX','Estimé','2D','3D','DGPS'][receiver.fix_type] || 'Inconnu'} · âge ${receiver.position_observation_age_ms ?? '?'} ms`;
}
async function refresh() {
  if (refreshing || document.hidden) return;
  refreshing = true;
  const generation = sessionGeneration, view = viewGeneration, tab = activeTab;
  const current = () => generation === sessionGeneration && view === viewGeneration && !document.hidden;
  try {
    const resources = await call('/api/resources', undefined, false);
    if (!current()) return;
    $('resources').textContent = print(resources);
    $('card-memory').textContent = `${resources.pressure} · ${resources.internal_free ?? '?'} octets internes`;
    ntripMutationAllowed = Boolean(token) && ['normal','reduced'].includes(resources.pressure) && resources.web_mutations_allowed === true;
    $('functions-save').disabled = !ntripMutationAllowed || !functionsConfig;
    $('ntrip-save').disabled = !ntripMutationAllowed || !ntripConfig || ntripSaving;
    wireSelfTestAllowed = Boolean(token) && resources.pressure === 'normal';
    $('wire-selftest').disabled = !wireSelfTestAllowed || wireSelfTestRunning;
    if (!['normal','reduced'].includes(resources.pressure)) {
      unavailable(); satellites({available:false});
      show('Pression mémoire : opérations Web suspendues. Diagnostic compact disponible ; console locale pour la maintenance.');
      return;
    }
    if (tab !== 'settings') {
      const status = await call('/api/status', undefined, false);
      if (!current()) return;
      renderStatus(status);
    }
    if (token && tab === 'rtcm') {
      const report = await call('/api/ntrip/runtime');
      if (!current()) return;
      $('ntrip-runtime').textContent = print(report);
    }
    if (token && !$('gnss').hidden && (tab === 'dashboard' || tab === 'gnss')) {
      const receiver = await call('/api/gnss/status');
      if (!current()) return;
      renderReceiver(receiver);
      if (!receiverPlans || receiverPlans.model !== receiver.model || receiverPlans.model_verified !== receiver.model_verified) {
        const plans = await call('/api/gnss/capabilities');
        if (!current()) return;
        receiverPlans = plans; $('receiver-plans').textContent = print(plans);
      }
      const permitted = receiver.model_verified && !receiver.busy && !receiver.quarantined;
      $('base-submit').disabled = !permitted || !(receiverPlans.fixed || receiverPlans.survey_in || receiverPlans.time_optimization);
      $('telemetry-submit').disabled = !permitted || !receiverPlans.telemetry;
      $('base-disable').disabled = !permitted || !receiverPlans.disable_base;
      if (tab === 'gnss') {
        if (resources.satellites_allowed === false) satellites({available:false});
        else {
          const report = await call('/api/gnss/satellites');
          if (!current()) return;
          satellites(report);
        }
      }
    }
    $('connection-state').textContent = 'API accessible';
    $('freshness').textContent = 'Lecture API : ' + new Date().toLocaleTimeString() + ' · âges dans les rapports';
  } catch (e) {
    if (current()) {
      ntripMutationAllowed = false; wireSelfTestAllowed = false;
      for (const id of ['ntrip-save','functions-save','wire-selftest']) $(id).disabled = true;
      unavailable(); $('connection-state').textContent = 'API indisponible'; $('freshness').textContent = 'État non actualisé'; show(e.message);
    }
  } finally { refreshing = false; scheduleRefresh(view !== viewGeneration || generation !== sessionGeneration ? 0 : undefined); }
}
async function load(expectedEdit = editGeneration) {
  if (typeof expectedEdit !== 'number') expectedEdit = editGeneration;
  const generation = sessionGeneration, read = ++configRead;
  const config = await call('/api/config');
  if (generation !== sessionGeneration || read !== configRead || expectedEdit !== editGeneration || !token) return;
  $('config').value = print(config);
  ntripConfig = config; renderNtrip();renderFunctions(config);
  if (config.gnss) {
    $('gnss-enabled').checked = config.gnss.enabled;
    $('gnss-tx').value = String(config.gnss.tx_pin); $('gnss-rx').value = String(config.gnss.rx_pin);
    $('gnss-baud').value = String(config.gnss.baud); $('gnss-model').value = config.gnss.model;
    $('correction-input').value = config.input;
  }
}
function handle(fn) { return async event => { event?.preventDefault(); const generation = sessionGeneration; try { await fn(event, editGeneration); } catch (e) { if (generation === sessionGeneration) show(e.message); } }; }
function closeSession() { $('session-state').textContent = 'Session fermée'; unavailable(); wireSelfTestAllowed = false; $('wire-selftest').disabled = true; $('wire-selftest-result').textContent = 'Session fermée'; $('command-history').textContent = 'Session fermée'; $('ntrip-runtime').textContent = 'Session fermée'; $('ntrip-history').textContent = 'Session fermée'; $('events').textContent = 'Session fermée'; ++sessionGeneration; token = ''; clearFunctions(); clearPortable(); clearPairingForm(); clearNtripForm(); receiverPlans = null; $('receiver-plans').textContent = 'Session fermée'; $('base-submit').disabled = true; $('base-disable').disabled = true; $('telemetry-submit').disabled = true; $('token').value = ''; $('config').value = ''; $('tls-pem').value = ''; $('receiver').textContent = 'Session fermée'; $('satellite-rows').replaceChildren(); $('satellite-summary').textContent = 'Session fermée'; show('Session fermée'); }
$('logout').onclick = closeSession;
$('login').onclick = async () => {
  const credential = $('token').value;
  closeSession(); token = credential;
  const generation = sessionGeneration;
  try {
    await load();
    if (generation !== sessionGeneration) return;
    await refresh();
    if (generation === sessionGeneration) { $('session-state').textContent = 'Session ouverte'; show('Session ouverte'); }
  } catch (e) { if (generation === sessionGeneration) { closeSession(); show(e.message); } }
};
$('load').onclick = handle(load);
$('save').onclick = handle(async (_, edit) => { const value = JSON.parse($('config').value); if (confirm('Sauvegarder cette configuration ?')) { show(await call('/api/config', value)); await load(edit); } });
async function fileText(event, limit) {
  const generation = sessionGeneration, edit = editGeneration, file = event.target.files?.[0];
  if (!token || !file || file.size > limit) throw Error(`Session et fichier ≤${limit} octets requis`);
  const text = await file.text();
  return generation === sessionGeneration && edit === editGeneration && token ? text : null;
}
$('export').onclick = handle(async (_, edit) => {
  const generation=sessionGeneration;
  const value=UGPortable.validate(await call('/api/config/portable'),'waveshare-s3-eth');
  if(generation!==sessionGeneration || !token)return;
  const url=URL.createObjectURL(new Blob([print(value)+'\n'],{type:'application/json'}));
  const link=document.createElement('a');link.href=url;link.download='ug-portable-config.json';link.click();URL.revokeObjectURL(url);
});
$('portable-file').onchange=handle(async event=>{
 const generation=sessionGeneration,pending=fileText(event,512);clearPortable();
 const text=await pending;if(text===null)return;
 const value=UGPortable.parse(text,'waveshare-s3-eth');
 if(generation!==sessionGeneration || !token)return;
 const before=UGPortable.validate(await call('/api/config/portable'),'waveshare-s3-eth');
 if(generation!==sessionGeneration || !token)return;
 const after=UGPortable.validate(await call('/api/config/portable/preview',value),'waveshare-s3-eth');
 if(generation!==sessionGeneration || !token)return;
 portableCandidate=after;$('portable-preview').textContent=print({avant:before,apres:after,effets:'GNSS/source locale/GGA désactivés ; secrets et association conservés'});$('portable-apply').disabled=false;
});
$('portable-apply').onclick=handle(async(_,edit)=>{
 if(!token || !portableCandidate || !ntripMutationAllowed)throw Error('Aperçu et session disponibles requis');
 if(!confirm('Appliquer ces réglages non personnels ? GNSS/source locale/GGA seront désactivés ; vérifier le câblage avant réactivation.'))return;
 const generation=sessionGeneration,value=portableCandidate;clearPortable();
 const result=await call('/api/config/portable/apply',{confirm:'APPLY_PORTABLE_CONFIGURATION',configuration:value});
 if(generation===sessionGeneration && token){show(result);await load(edit);}
});
$('import').onchange = handle(async event => { const text = await fileText(event,8192); if(text===null)return; JSON.parse(text); $('config').value = text; });
$('base').onsubmit = handle(async event => {
  const fields = new FormData(event.target), value = {mode: fields.get('mode'), height_datum: fields.get('height_datum'), rtcm_output: true, persist: fields.has('persist')};
  for (const key of ['latitude_deg','longitude_deg','height_m','duration_s','accuracy_01mm','reuse_distance_m','receiver_port']) {
    const input = fields.get(key); if (input !== '') value[key] = Number(input);
  }
  const supported = {'fixed': 'fixed', 'survey-in': 'survey_in', 'time': 'time_optimization'};
  if (!receiverPlans || !receiverPlans[supported[value.mode]]) throw new Error('Mode indisponible pour le modèle vérifié');
  if (!confirm('Appliquer ce plan au récepteur identifié ? Une erreur peut interrompre les corrections.')) return;
  value.confirm = 'APPLY_BASE_CONFIGURATION';
  if (value.persist || value.mode === 'time') {
    if (prompt('Écriture persistante : saisir SAVE_RECEIVER_CONFIGURATION') !== 'SAVE_RECEIVER_CONFIGURATION') return;
    value.persistent_confirmation = 'SAVE_RECEIVER_CONFIGURATION';
  }
  show(await call('/api/gnss/base/apply', value));
});
$('tls-object').onsubmit = handle(async (_, edit) => {
  const kind = $('tls-kind').value, value = {name: $('tls-name').value, kind};
  const confirmation = kind === 'delete' ? 'DELETE_TLS_OBJECT' : 'REPLACE_TLS_OBJECT';
  if (prompt(`Saisir ${confirmation} pour confirmer`) !== confirmation) return;
  value.confirm = confirmation;
  if (kind !== 'delete') value.pem = $('tls-pem').value;
  $('tls-pem').value = ''; show(await call('/api/tls/object', value));
});
$('transport').onsubmit = handle(async (_, edit) => {
  const value = {input: $('correction-input').value, gnss: {enabled: $('gnss-enabled').checked,
    tx_pin: Number($('gnss-tx').value), rx_pin: Number($('gnss-rx').value),
    baud: Number($('gnss-baud').value), model: $('gnss-model').value}};
  if (!confirm('Appliquer ce transport au câblage GNSS réel ? Une commande en cours sera interrompue.')) return;
  show(await call('/api/config', value)); await load(edit);
});
call('/api/capabilities', undefined, false).then(caps => {
  $('gnss').hidden = !caps.gnss; $('gnss-unavailable').hidden = Boolean(caps.gnss);
  $('portable-section').hidden=!caps.gnss;$('export').hidden=!caps.gnss;
  ntripGnss = Boolean(caps.gnss);
  ntripSlots = Number.isInteger(caps.publishers) && caps.publishers >= 0 && caps.publishers <= 5 ? caps.publishers : 0;
  for (let slot = 0; slot < ntripSlots; ++slot) {
    const option = document.createElement('option'); option.value = String(slot); option.textContent = `Publication ${slot + 1} (SOURCE v1)`; $('ntrip-target').appendChild(option);
  }
  renderNtrip();
  for (const pin of (caps.gnss_allowed_pins || [])) for (const id of ['gnss-tx','gnss-rx']) {
    const option = document.createElement('option'); option.value = String(pin); option.textContent = String(pin); $(id).appendChild(option);
  }
}).catch(e => show(e.message));
for (const [i,key] of tabs.entries()) {
  $('nav-' + key).onclick = () => selectTab(key);
  $('nav-' + key).onkeydown = event => {
    const n = event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 : event.key === 'ArrowRight' ? (i+1)%tabs.length : event.key === 'ArrowLeft' ? (i+tabs.length-1)%tabs.length : -1;
    if (n >= 0) { event.preventDefault(); selectTab(tabs[n]); $('nav-' + tabs[n]).focus(); }
  };
}
$('refresh-now').onclick = () => refresh();
document.addEventListener('input', () => ++editGeneration);
document.addEventListener('visibilitychange', () => {
  ++viewGeneration; clearTimeout(refreshTimer);
  if (document.hidden) { unavailable(); $('freshness').textContent = 'En pause — onglet masqué'; $('connection-state').textContent = 'En pause'; }
  else refresh();
});
refresh();

$('telemetry').onsubmit = handle(async (_, edit) => {
  const value = {rate_hz: Number($('telemetry-rate').value), persist: $('telemetry-persist').checked,
    confirm: 'APPLY_GNSS_TELEMETRY'};
  if (!confirm('Modifier les messages du récepteur identifié ? Les corrections locales seront suspendues pendant la configuration.')) return;
  if (value.persist) {
    if (prompt('Écriture persistante : saisir SAVE_RECEIVER_CONFIGURATION') !== 'SAVE_RECEIVER_CONFIGURATION') return;
    value.persistent_confirmation = 'SAVE_RECEIVER_CONFIGURATION';
  }
  show(await call('/api/gnss/telemetry/apply', value));
});

$('detect').onclick = handle(async (_, edit) => {
  if (!confirm('Relancer les interrogations de modèle en lecture seule ? Les corrections locales peuvent être interrompues brièvement. La quarantaine restera active.')) return;
  show(await call('/api/gnss/detect', {confirm: 'DETECT_RECEIVER'})); receiverPlans = null; await refresh();
});

$('rollback').onclick = handle(async (_, edit) => {
  const generation = sessionGeneration, history = await call('/api/config/history');
  if (generation !== sessionGeneration || !token) return;
  if (!history.writable || !history.previous_available) throw new Error('Aucune configuration précédente intacte disponible');
  if (prompt('Restaurer les réglages de la passerelle ? Les commandes déjà appliquées au GNSS ne seront pas annulées. Saisir ROLLBACK_CONFIGURATION. Identité admin et quarantaine conservées.') !== 'ROLLBACK_CONFIGURATION') return;
  show(await call('/api/config/rollback', {confirm:'ROLLBACK_CONFIGURATION', expected_generation:history.generation}));
  await load(edit);
});

const reportVersions = {};
async function readReport(id, path) {
  const generation = sessionGeneration, version = reportVersions[id] = (reportVersions[id] || 0) + 1;
  const report = await call(path);
  if (generation === sessionGeneration && token && reportVersions[id] === version) $(id).textContent = print(report);
}
$('events-read').onclick = handle(() => readReport('events', '/api/diagnostics/events'));
const readNtrip = () => readReport('ntrip-runtime', '/api/ntrip/runtime');
$('ntrip-read').onclick = handle(readNtrip);
$('ntrip-restart').onclick = handle(async (_, edit) => {
  if (!confirm('Interrompre puis redémarrer toutes les connexions NTRIP ? Le récepteur GNSS reste inchangé.')) return;
  const generation = sessionGeneration;
  const result = await call('/api/ntrip/restart', {confirm:'RESTART_NTRIP_SERVICES'});
  if (generation !== sessionGeneration || !token) return;
  show(result); await readNtrip();
});

$('base-disable').onclick = handle(async (_, edit) => {
  if (!receiverPlans?.disable_base) throw new Error('Désactivation BASE indisponible pour ce modèle vérifié');
  if (!confirm('Quitter BASE sur le récepteur vérifié ? F9P : désactiver le mode statique et son profil RTCM ; UM980/UM982 : MODE ROVER générique et arrêt du profil RTCM sur le port choisi. Le mode concerne tout le récepteur, sans restitution d’un ancien profil rover.')) return;
  const operation = {confirm:'DISABLE_BASE_MODE', receiver_port:Number($('base-disable-port').value), persist:Boolean($('base-disable-persist').checked)};
  if (operation.persist) {
    if (prompt('Sauvegarde persistante : saisir SAVE_RECEIVER_CONFIGURATION') !== 'SAVE_RECEIVER_CONFIGURATION') return;
    operation.persistent_confirmation = 'SAVE_RECEIVER_CONFIGURATION';
  }
  const generation = sessionGeneration, result = await call('/api/gnss/base/disable', operation);
  if (generation === sessionGeneration && token) show(result);
});

$('commands-read').onclick = handle(() => readReport('command-history', '/api/gnss/commands'));

$('wire-selftest').onclick = handle(async (_, edit) => {
  if (!token || !wireSelfTestAllowed || wireSelfTestRunning) return;
  if (!confirm('Exécuter les six autotests CRC et codec dans des buffers privés ? Aucun paquet radio, aucune commande GNSS ni correction ne sera transmis.')) return;
  const generation = sessionGeneration;
  wireSelfTestRunning = true; $('wire-selftest').disabled = true;
  try {
    const report = await call('/api/diagnostics/wire-selftest', {confirm:'RUN_OFFLINE_WIRE_SELFTEST'});
    if (generation !== sessionGeneration || !token) return;
    $('wire-selftest-result').textContent = print(report);
  } finally {
    wireSelfTestRunning = false;
    if (generation === sessionGeneration) $('wire-selftest').disabled = !wireSelfTestAllowed || !token;
  }
});

$('ntrip-history-read').onclick = handle(() => readReport('ntrip-history', '/api/ntrip/history'));

$('ntrip-target').value = 'upstream';
$('ntrip-target').onchange = handle(async (_, edit) => { renderNtrip(); });
$('ntrip-endpoint').onsubmit = handle(async (_, edit) => {
  if (ntripSaving) return;
  const password = $('ntrip-password').value, erase = $('ntrip-password-clear').checked;
  clearNtripSecret();
  if (!token || !ntripConfig || !ntripMutationAllowed) throw new Error('Session, configuration et mémoire disponibles requises');
  const slot = ntripTarget(), generation = sessionGeneration, endpoint = {};
  for (const key of ntripTextFields) endpoint[key] = $('ntrip-' + key).value;
  if (password && erase) throw new Error('Choisir le remplacement ou l’effacement du mot de passe');
  if (password || erase) endpoint.password = erase ? '' : password;
  endpoint.port = Number($('ntrip-port').value);
  endpoint.tls = $('ntrip-tls').checked;
  if (slot === null) {
    endpoint.send_gga = $('ntrip-send_gga').checked;
    endpoint.gga_interval_s = Number($('ntrip-gga_interval_s').value);
  }
  const owner = {enabled:$('ntrip-enabled').checked, endpoint};
  const patch = slot === null ? {upstream:owner} : {publishers:[{slot,...owner}]};
  if (!confirm('Sauvegarder cette connexion NTRIP ? Les connexions seront reconfigurées et les corrections peuvent être interrompues. La source RTCM choisie reste inchangée.')) return;
  ntripSaving = true; $('ntrip-save').disabled = true;
  try {
    const result = await call('/api/config', patch);
    if (generation !== sessionGeneration || !token) return;
    show(result); await load(edit);
  } catch (error) {
    if (generation === sessionGeneration && token) throw error;
  } finally {
    ntripSaving = false; $('ntrip-save').disabled = !token || !ntripConfig || !ntripMutationAllowed;
  }
});

async function readPairing() {
  const generation=sessionGeneration, report=await call('/api/radio/pairing');
  if (generation!==sessionGeneration || !token) return;
  $('pairing-info').textContent=print(report);
  $('pairing-peer').value=report.peer || '';
  $('pairing-channel').value=String(report.configured_channel || 1);
}
$('pairing-read').onclick=handle(readPairing);
$('pairing-open').onclick=handle(async (_, edit) => {
  if (!confirm('Ouvrir pendant 120 secondes la configuration manuelle d’un pair local ? Aucun appairage radio automatique.')) return;
  const generation=sessionGeneration, result=await call('/api/radio/pairing/window',{confirm:'OPEN_LOCAL_PAIRING'});
  if (generation===sessionGeneration && token) $('pairing-info').textContent=print(result);
});
$('pairing-form').onsubmit=handle(async (_, edit) => {
  const key=$('pairing-key').value; $('pairing-key').value='';
  if (!token || !ntripMutationAllowed) throw Error('Session et mémoire disponibles requises');
  if (!confirm('Associer ce rover avec la clé privée partagée ? La sortie radio sera coupée jusqu’au redémarrage.')) return;
  const generation=sessionGeneration, result=await call('/api/radio/pairing/apply',{
    confirm:'PAIR_ESPNOW_ROVER',peer:$('pairing-peer').value,channel:Number($('pairing-channel').value),key});
  if (generation===sessionGeneration && token) { $('pairing-info').textContent=print(result); await load(edit); }
});
$('pairing-remove').onclick=handle(async (_, edit) => {
  if (!confirm('Supprimer l’association persistante et couper cette sortie radio ? Le C3 doit être reconfiguré séparément par USB.')) return;
  const generation=sessionGeneration, result=await call('/api/radio/pairing/remove',{confirm:'UNPAIR_ESPNOW_ROVER'});
  $('pairing-key').value='';
  if (generation===sessionGeneration && token) { $('pairing-info').textContent=print(result); await load(edit); }
});

$('pairing-ticket').onchange=handle(async event => {
  const generation=sessionGeneration, text=await fileText(event,1024);
  if(text===null)return;
  const ticket=JSON.parse(text);
  if (generation!==sessionGeneration || !token) return;
  if (!ticket || typeof ticket!=='object' || Array.isArray(ticket) ||
      Object.keys(ticket).sort().join(',')!=='channel,confirm,key,peer' || ticket.confirm!=='PAIR_ESPNOW_ROVER' ||
      typeof ticket.key!=='string' || !/^[a-fA-F0-9]{32}$/.test(ticket.key) ||
      typeof ticket.peer!=='string' || !/^(?:[a-fA-F0-9]{2}:){5}[a-fA-F0-9]{2}$/.test(ticket.peer) ||
      !Number.isInteger(ticket.channel) || ticket.channel<1 || ticket.channel>11) throw Error('Ticket privé incompatible');
  $('pairing-peer').value=ticket.peer; $('pairing-channel').value=String(ticket.channel); $('pairing-key').value=ticket.key;
  event.target.value=''; show('Ticket chargé localement ; ouvrir la fenêtre puis confirmer l’association.');
});

$('functions').onsubmit=handle(async(_,edit)=>{
 if(!token || !functionsConfig || !ntripMutationAllowed)throw Error('Session et mémoire disponibles requises');
 const generation=sessionGeneration;
 const radio=$('function-radio').checked;
 const patch={caster:{enabled:$('function-caster').checked,port:Number($('function-caster-port').value),mountpoint:$('function-caster-mount').value,username:$('function-caster-user').value},wifi:{sta:$('function-sta').checked,ssid:$('function-ssid').value,ap:$('function-ap').checked,ap_ssid:$('function-ap-ssid').value}};
 for(const [id,target,key] of [['function-caster-password',patch.caster,'password'],['function-password',patch.wifi,'password'],['function-ap-password',patch.wifi,'ap_password']]){if($(id).value)target[key]=$(id).value;$(id).value='';}
 if(!confirm('Sauvegarder ces activations ? Un équipement absent pourra rester en attente/erreur. Les changements Wi-Fi et la réactivation radio exigent un redémarrage.'))return;
 if(radio!==Boolean(functionsConfig.radio?.enabled)){
  await call('/api/radio/pairing/window',{confirm:'OPEN_LOCAL_PAIRING'});
  if(generation!==sessionGeneration||!token)return;
  patch.radio={enabled:radio};
 }
 const result=await call('/api/config',patch);
 if(generation===sessionGeneration&&token){show(result);await load(edit);}
});
