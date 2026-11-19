'use strict';
let passwordChangeRequired=false, passwordLogin=false, webTransport='https';
let token = '', sessionGeneration = 0, refreshing = false, receiverPlans = null;
let portableCandidate=null;
function sessionView() {
  const restricted=Boolean(token)&&passwordChangeRequired;
  $('webui').hidden=!token||restricted;
  $('login-screen').hidden=Boolean(token)&&!restricted;
  $('login-form').hidden=restricted;
  $('login-password-slot').hidden=!restricted;
  (restricted?$('login-password-slot'):$('settings-password-slot')).appendChild($('password-section'));
}
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
let ntripSecretGeneration=0;
function clearNtripSecret() {
  ++ntripSecretGeneration;
  $('ntrip-password').value = ''; $('ntrip-password').type='password'; $('ntrip-password-clear').checked = false;
  $('ntrip-password-toggle').textContent='Afficher';$('ntrip-password-toggle').setAttribute('aria-pressed','false');
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
  $('ntrip-messages-field').hidden = slot === null;
  $('ntrip-messages').value = owner?.rtcm_messages || '';
  $('ntrip-password-status').textContent = endpoint.password_set ? 'Mot de passe enregistré. Afficher pour le consulter, ou saisir pour le modifier.' : 'Aucun mot de passe enregistré.';
  $('ntrip-password-toggle').disabled=!token||!ntripConfig;
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
let pairingController = null, pairingPendingVerification = null;
function clearPairingForm() { const interrupted=Boolean(pairingController); pairingController?.abort(); $('pairing-info').textContent = interrupted?'Session fermée ; association interrompue, état des deux cartes à vérifier.':'Session fermée'; $('pairing-details').textContent = ''; }

function clearNtripForm() { ntripMutationAllowed = false; ntripConfig = null; $('ntrip-target').value = 'upstream'; renderNtrip(); }
let requestQueue = Promise.resolve(), configRead = 0, editGeneration = 0;
function call(path, body, authorized = true) {
  const generation = sessionGeneration, credential = token;
  const run = async () => {
    if (authorized && generation !== sessionGeneration && path!=='/api/auth/logout') throw Error('Session modifiée');
    if(pairingController && body!==undefined && !path.startsWith('/api/radio/pairing/') && path!=='/api/auth/logout') throw Error('Appairage en cours ; terminer ou annuler avant une autre modification');
    if (authorized && passwordChangeRequired && !path.startsWith('/api/auth/')) throw Error('Changer le mot de passe dans Paramètres');
    const headers = {};
    if (authorized && credential) headers.Authorization = `Bearer ${credential}`;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), path.startsWith('/api/auth/') ? 120000 : 10000);
    try {
      const response = await fetch(path, {method: body === undefined ? 'GET' : 'POST', headers,
        body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store', credentials: 'omit', signal: controller.signal});
      const text = await response.text();
      if (!response.ok) { if(response.status===401 && authorized && generation===sessionGeneration && path!=='/api/auth/logout')closeSession(); throw Error(`${response.status} ${text}`); }
      if (authorized && generation !== sessionGeneration && path!=='/api/auth/logout') throw Error('Session modifiée');
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
  if (!document.hidden && token) refreshTimer = setTimeout(() => refresh(), delay);
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
  for (const id of ['base-submit','base-disable','telemetry-submit','receiver-restart','system-reboot']) $(id).disabled = true;
}
function renderStatus(status) {
  $('status').textContent = print(status);
  const firmware=status.firmware;
  $('firmware-identity').textContent=firmware ? `UG ${firmware.ug_version} · ${firmware.hardware_variant} · ${firmware.web_transport} · ESP-IDF ${firmware.esp_idf} · ${firmware.source_sha}${firmware.modified?' · sources modifiées':''}` : 'Identification du firmware indisponible';
  $('services-status').textContent = print(status.services || {});
  const transport=['http','https'].includes(status.web_transport)?status.web_transport:webTransport;
  $('network-status').textContent = print({ethernet:status.services?.ethernet_driver,transport,http:status.services?.http,https:status.services?.https,espnow:status.espnow});
  $('card-global').textContent = status.degraded === false ? 'Services réactifs' : 'État dégradé / inconnu';
  $('card-ethernet').textContent='Service : '+(status.services?.ethernet_driver?.state||'inconnu');
  $('card-https').textContent=transport.toUpperCase()+' : '+(status.services?.[transport]?.state||'inconnu');
  $('card-rtcm').textContent = `${status.rtcm_frames ?? '?'} trames · ${status.rtcm_rejected ?? '?'} rejetées`;
  $('card-ntrip').textContent = status.upstream_flowing ? 'Flux entrant observé' : status.upstream_connected ? 'Connecté, flux non confirmé' : 'Pas de connexion entrante';
}
function renderReceiver(receiver) {
  $('receiver').textContent = print(receiver);
  $('card-gnss').textContent = !receiver.present ? 'Non détecté' : receiver.position_stale !== false || !receiver.fix_valid ? 'Position non valide / périmée' :
    `${['Inconnu','Aucun FIX','FIX','RTK FLOAT','RTK FIX','Estimé','2D','3D','DGPS'][receiver.fix_type] || 'Inconnu'} · âge ${receiver.position_observation_age_ms ?? '?'} ms`;
}
async function refresh() {
  if (refreshing || document.hidden || pairingController || !token) return;
  refreshing = true;
  const generation = sessionGeneration, view = viewGeneration, tab = activeTab;
  const current = () => generation === sessionGeneration && view === viewGeneration && !document.hidden;
  try {
    await call('/api/auth/session');
    if (!current()) return;
    // Restricted sessions may only renew/check their validity, never load station data.
    if (passwordChangeRequired) return;
    const resources = await call('/api/resources', undefined, false);
    if (!current()) return;
    $('resources').textContent = print(resources);
    $('card-memory').textContent = `${resources.pressure} · ${resources.internal_free ?? '?'} octets internes`;
    ntripMutationAllowed = Boolean(token) && !passwordChangeRequired && ['normal','reduced'].includes(resources.pressure) && resources.web_mutations_allowed === true;
    $('system-reboot').disabled = !ntripMutationAllowed;
    $('functions-save').disabled = !ntripMutationAllowed || !functionsConfig;
    $('ntrip-save').disabled = !ntripMutationAllowed || !ntripConfig || ntripSaving;
    wireSelfTestAllowed = Boolean(token) && !passwordChangeRequired && resources.pressure === 'normal';
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
    if (token && !passwordChangeRequired && tab === 'rtcm') {
      const report = await call('/api/ntrip/runtime');
      if (!current()) return;
      $('ntrip-runtime').textContent = print(report);
    }
    if (token && !passwordChangeRequired && !$('gnss').hidden && (tab === 'dashboard' || tab === 'gnss')) {
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
      $('receiver-restart').disabled = !ntripMutationAllowed || !permitted || !receiverPlans.restart;
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
function closeSession() { $('session-state').textContent = 'Session fermée'; unavailable(); wireSelfTestAllowed = false; $('wire-selftest').disabled = true; $('wire-selftest-result').textContent = 'Session fermée'; $('command-history').textContent = 'Session fermée'; $('ntrip-runtime').textContent = 'Session fermée'; $('ntrip-history').textContent = 'Session fermée'; $('events').textContent = 'Session fermée'; ++sessionGeneration; token = ''; clearFunctions(); clearPortable(); clearPairingForm(); clearNtripForm(); receiverPlans = null; $('receiver-plans').textContent = 'Session fermée'; $('base-submit').disabled = true; $('base-disable').disabled = true; $('telemetry-submit').disabled = true; $('password').value = ''; for(const id of ['current-password','new-password','confirm-password'])$(id).value=''; passwordChangeRequired=false; sessionView(); clearTimeout(refreshTimer); $('config').value = ''; $('tls-pem').value = ''; $('receiver').textContent = 'Session fermée'; $('satellite-rows').replaceChildren(); $('satellite-summary').textContent = 'Session fermée'; show('Session fermée'); }
const logout = async () => {
  const pending = token && passwordLogin ? call('/api/auth/logout', {}) : Promise.resolve();
  closeSession(); await pending.catch(()=>{});
};
$('logout').onclick = logout;
$('restricted-logout').onclick = logout;
let loginPending=false;
$('login').onclick = event => {event?.preventDefault(); return login();};
$('login-form').onsubmit = event => {event.preventDefault();return login();};
async function login() {
  if(loginPending)return;
  const username=$('username').value, password=$('password').value;
  closeSession(); loginPending=true; $('login').disabled=true; $('session-state').textContent='Connexion en cours…';show('Vérification des identifiants…');
  const generation=sessionGeneration;
  try {
    const result=passwordLogin?await call('/api/auth/login',{username,password},false):{token:password,password_change_required:false};
    if(generation!==sessionGeneration)return;
    if(typeof result.token!=='string' || (passwordLogin?!/^[a-f0-9]{64}$/.test(result.token):!/^[A-Za-z0-9_-]{32,128}$/.test(result.token)) || typeof result.password_change_required!=='boolean')throw Error('Réponse de connexion invalide');
    token=result.token; passwordChangeRequired=result.password_change_required; sessionView();
    if(passwordChangeRequired){
      $('auth-state').textContent='Changement obligatoire : cette session autorise uniquement le choix du nouveau mot de passe.';
      $('session-state').textContent='Mot de passe à changer'; show('Changer le mot de passe dans Paramètres avant toute opération sensible.');
      refresh();
    } else {
      selectTab('dashboard'); await load(); await refresh();
      if(pairingPendingVerification){
        const pending=pairingPendingVerification, state=await call('/api/radio/pairing');
        if(generation===sessionGeneration && token){
          const ok=state.started && !state.restart_required && state.key_set && state.enabled &&
            state.peer?.toLowerCase()===pending.peer.toLowerCase() && state.effective_channel===pending.channel;
          $('pairing-info').textContent=ok?'Association enregistrée sur les deux cartes, S3 radio démarrée. Réception RTCM/USB non vérifiée.':'Configurations enregistrées ; démarrage radio S3 non confirmé.';
          if(ok)pairingPendingVerification=null;
        }
      }
      if(generation===sessionGeneration){$('session-state').textContent='Session ouverte';$('auth-state').textContent='Identifiant : admin. Le changement révoque toutes les sessions Web.';show('Session ouverte');}
    }
  } catch(e){if(generation===sessionGeneration){closeSession();show(e.message);}}
  finally { loginPending=false; $('login').disabled=false; }
}
$('password-form').onsubmit=handle(async()=>{
  const current_password=$('current-password').value, new_password=$('new-password').value, confirmation=$('confirm-password').value;
  for(const id of ['current-password','new-password','confirm-password'])$(id).value='';
  if(!token || new_password.length<8 || new_password.length>128 || new_password!==confirmation)throw Error('Mot de passe de 8 à 128 caractères et confirmation identique requis');
  if(!confirm('Changer le mot de passe et fermer toutes les sessions Web ?'))return;
  await call('/api/auth/password',{current_password,new_password,confirm:'CHANGE_PASSWORD'});
  closeSession(); show('Mot de passe changé ; reconnectez-vous.');
});
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
  webTransport=['http','https'].includes(caps.web_transport)?caps.web_transport:'https';
  $('transport-state').textContent=webTransport==='http'?'HTTP · réseau local':'HTTPS';
  passwordLogin=caps.password_authentication===true; $('login').disabled=false; $('password-section').hidden=!passwordLogin; $('pairing-start').disabled=!passwordLogin;
  if(!passwordLogin)$('auth-state').textContent='Profil historique : mot de passe = jeton existant, aucun changement d’identifiants ici.';
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
sessionView();

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

$('system-reboot').onclick = handle(async () => {
  if (!token || !ntripMutationAllowed) throw Error('Session et mémoire disponibles requises');
  if (!confirm('Redémarrer l’ESP32 ? Les connexions seront interrompues ; les réglages enregistrés seront conservés.')) return;
  await call('/api/system/reboot', {confirm:'REBOOT_ESP32'});
  closeSession(); show('ESP32 en cours de redémarrage. Reconnectez-vous lorsque la station est disponible.');
});
$('receiver-restart').onclick = handle(async () => {
  if (!token || !ntripMutationAllowed || !receiverPlans?.restart) throw Error('F9P identifié et disponible requis');
  if (!confirm('Redémarrer le GNSS du F9P à chaud, sans effacer sa configuration ? Les observations et corrections locales seront invalidées.')) return;
  show(await call('/api/gnss/restart', {confirm:'RESTART_GNSS'}));
  receiverPlans=null; $('receiver-restart').disabled=true; await refresh();
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
$('ntrip-password-toggle').onclick = handle(async (_, edit) => {
  if(!token||!ntripConfig)return;
  const field=$('ntrip-password'), button=$('ntrip-password-toggle');
  if(field.type==='text'){
    field.type='password';button.textContent='Afficher';button.setAttribute('aria-pressed','false');return;
  }
  const slot=ntripTarget(), owner=slot===null?ntripConfig.upstream:ntripConfig.publishers?.find(p=>p.slot===slot);
  if(!field.value && owner?.endpoint?.password_set){
    const generation=sessionGeneration, secretGeneration=ntripSecretGeneration;
    button.disabled=true;
    try {
      const value=await call('/api/ntrip/password/'+(slot===null?'upstream':slot));
      if(generation!==sessionGeneration||secretGeneration!==ntripSecretGeneration||edit!==editGeneration||!token)return;
      if(typeof value.password!=='string'||value.password.length>128)throw Error('Réponse de mot de passe invalide');
      field.value=value.password;
    } finally {
      if(generation===sessionGeneration && secretGeneration===ntripSecretGeneration)button.disabled=!token||!ntripConfig;
    }
  }
  field.type='text';button.textContent='Masquer';button.setAttribute('aria-pressed','true');
});
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
  if(slot!==null) owner.rtcm_messages=$('ntrip-messages').value;
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
  if (generation===sessionGeneration && token) $('pairing-details').textContent=print(report);
}
$('pairing-read').onclick=handle(readPairing);
$('pairing-start').onclick=async () => {
  if (pairingController) { pairingController.abort(); return; }
  const output=$('pairing-info'), button=$('pairing-start'), generation=sessionGeneration;
  if (!passwordLogin) { output.textContent='Appairage USB disponible sur le Gateway S3.'; return; }
  if (!token || !ntripMutationAllowed) { output.textContent='Session et mémoire disponibles requises'; return; }
  if (!globalThis.isSecureContext || !navigator.serial || !crypto?.getRandomValues) {
    output.textContent='Web Serial indisponible sur cette origine : appairage USB depuis HTTPS ou localhost requis. L’administration réseau reste disponible.'; return;
  }
  const controller=new AbortController(); pairingController=controller;
  button.textContent='Annuler l’appairage'; clearTimeout(refreshTimer);
  const progress=text => { if (generation===sessionGeneration && token) output.textContent=text; };
  try {
    // requestPort must run directly in the click activation, before any asynchronous API call.
    const port=await navigator.serial.requestPort();
    const result=await UGRoverPairing.pair(port,async(path,body)=>{
      if(controller.signal.aborted || generation!==sessionGeneration || !token)throw Error('Session modifiée');
      return call(path,body);
    },progress,controller.signal,async(channel,replacing)=>confirm(
      'Appairer ce C3 Rover au S3 sur le canal '+channel+' avec une nouvelle clé privée ? Le S3 redémarrera. '+
      (replacing?'Une association existe : elle sera remplacée. Une interruption peut laisser une configuration partielle.':'Les deux cartes enregistreront cette association.')));
    if(result.cancelled){progress('Appairage annulé avant écriture.');return;}
    pairingPendingVerification=result;
    progress('Configurations enregistrées ; attente du retour du S3…');
    const deadline=Date.now()+45000;
    await new Promise(resolve=>setTimeout(resolve,2500));
    while(Date.now()<deadline){
      if(controller.signal.aborted || generation!==sessionGeneration || !token)throw Error();
      try {
        const state=await call('/api/radio/pairing');
        if(state.started && !state.restart_required && state.key_set && state.enabled &&
           state.peer?.toLowerCase()===result.peer.toLowerCase() && state.effective_channel===result.channel){
          pairingPendingVerification=null; progress('Association enregistrée sur les deux cartes, S3 radio démarrée. Réception RTCM/USB non vérifiée.');return;
        }
      } catch(error) {
        if(error.message?.startsWith('401')){progress('Configurations enregistrées. S3 redémarré : reconnectez-vous pour vérifier son état radio.');return;}
      }
      await new Promise(resolve=>setTimeout(resolve,1500));
    }
    progress('Configurations enregistrées ; démarrage radio S3 non confirmé. Vérifiez le statut avant utilisation.');
  } catch(error) {
    if(generation===sessionGeneration && token)output.textContent=error.name==='NotFoundError'?'Sélection USB annulée ; aucune écriture.':
      error.message || 'Vérification interrompue ; état des cartes à contrôler.';
  } finally {
    pairingController=null; button.textContent='Appairer un Rover'; scheduleRefresh(0);
  }
};
globalThis.addEventListener?.('pagehide',()=>pairingController?.abort());
$('pairing-remove').onclick=handle(async (_, edit) => {
  if(pairingController)throw Error('Appairage en cours');
  if (!confirm('Supprimer l’association persistante et couper cette sortie radio ? Le C3 doit être reconfiguré séparément par USB.')) return;
  const generation=sessionGeneration, result=await call('/api/radio/pairing/remove',{confirm:'UNPAIR_ESPNOW_ROVER'});
  if (generation===sessionGeneration && token) { $('pairing-details').textContent=print(result); await load(edit); }
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
