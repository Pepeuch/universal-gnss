'use strict';
let token = '', sessionGeneration = 0, refreshing = false, receiverPlans = null;
const $ = id => document.getElementById(id);
const show = value => { $('notice').textContent = String(value); };
const print = value => JSON.stringify(value, null, 2);
async function call(path, body, authorized = true) {
  const headers = {};
  if (authorized && token) headers.Authorization = `Bearer ${token}`;
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 10000);
  try {
    const response = await fetch(path, {method: body === undefined ? 'GET' : 'POST', headers,
      body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store', credentials: 'omit', signal: controller.signal});
    const text = await response.text();
    if (!response.ok) throw new Error(`${response.status} ${text}`);
    if (response.headers.get('Content-Type')?.includes('application/json')) return JSON.parse(text);
    return text;
  } finally { clearTimeout(timer); }
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

async function refresh() {
  if (refreshing) return;
  refreshing = true;
  const generation = sessionGeneration;
  try {
    const resources = await call('/api/resources', undefined, false);
    if (generation !== sessionGeneration) return;
    $('resources').textContent = print(resources);
    if (resources.pressure === 'critical') {
      $('status').textContent = 'Rapport détaillé suspendu : pression mémoire';
      $('receiver').textContent = 'Rapport suspendu : pression mémoire';
      satellites({available:false}); receiverPlans = null;
      $('base-submit').disabled = true; $('telemetry-submit').disabled = true;
      show('Pression mémoire : opérations Web suspendues. Diagnostic compact disponible ; console locale pour la maintenance.');
      return;
    }
    const status = await call('/api/status', undefined, false);
    if (generation !== sessionGeneration) return;
    $('status').textContent = print(status);
    if (token && !$('gnss').hidden) {
      const receiver = await call('/api/gnss/status');
      if (generation !== sessionGeneration || !token) return;
      $('receiver').textContent = print(receiver);
      if (!receiverPlans || receiverPlans.model !== receiver.model || receiverPlans.model_verified !== receiver.model_verified) {
        const plans = await call('/api/gnss/capabilities');
        if (generation !== sessionGeneration || !token) return;
        receiverPlans = plans; $('receiver-plans').textContent = print(plans);
        $('base-submit').disabled = !(plans.fixed || plans.survey_in || plans.time_optimization);
        $('telemetry-submit').disabled = !plans.telemetry;
      }
      if (resources.satellites_allowed === false) {
        satellites({available:false});
        return;
      }
      const report = await call('/api/gnss/satellites');
      if (generation !== sessionGeneration || !token) return;
      satellites(report);
    }
  } catch (e) { if (generation === sessionGeneration) show(e.message); }
  finally { refreshing = false; }
}

async function load() {
  const generation = sessionGeneration;
  const config = await call('/api/config');
  if (generation !== sessionGeneration || !token) return;
  $('config').value = print(config);
  if (config.gnss) {
    $('gnss-enabled').checked = config.gnss.enabled;
    $('gnss-tx').value = String(config.gnss.tx_pin); $('gnss-rx').value = String(config.gnss.rx_pin);
    $('gnss-baud').value = String(config.gnss.baud); $('gnss-model').value = config.gnss.model;
    $('correction-input').value = config.input;
  }
}
function handle(fn) { return async event => { event?.preventDefault(); try { await fn(event); } catch (e) { show(e.message); } }; }
$('login').onclick = handle(async () => { const generation = ++sessionGeneration; receiverPlans = null; token = $('token').value; $('token').value = ''; await load(); if (generation !== sessionGeneration) return; await refresh(); if (generation === sessionGeneration) show('Session ouverte'); });
$('logout').onclick = () => { $('ntrip-runtime').textContent = 'Session fermée'; $('events').textContent = 'Session fermée'; ++sessionGeneration; token = ''; receiverPlans = null; $('receiver-plans').textContent = 'Session fermée'; $('base-submit').disabled = true; $('telemetry-submit').disabled = true; $('token').value = ''; $('config').value = ''; $('tls-pem').value = ''; $('receiver').textContent = 'Session fermée'; $('satellite-rows').replaceChildren(); $('satellite-summary').textContent = 'Session fermée'; show('Session fermée'); };
$('load').onclick = handle(load);
$('save').onclick = handle(async () => { const value = JSON.parse($('config').value); if (confirm('Sauvegarder cette configuration ?')) { show(await call('/api/config', value)); await load(); } });
$('export').onclick = handle(async () => {
  const blob = new Blob([print(await call('/api/config'))], {type:'application/json'});
  const url = URL.createObjectURL(blob), link = document.createElement('a'); link.href = url; link.download = 'ug-config-redacted.json'; link.click(); URL.revokeObjectURL(url);
});
$('import').onchange = handle(async event => { const file = event.target.files[0]; if (!file || file.size > 8192) throw new Error('Fichier JSON : maximum 8192 octets'); const text = await file.text(); JSON.parse(text); $('config').value = text; });
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
$('tls-object').onsubmit = handle(async () => {
  const kind = $('tls-kind').value, value = {name: $('tls-name').value, kind};
  const confirmation = kind === 'delete' ? 'DELETE_TLS_OBJECT' : 'REPLACE_TLS_OBJECT';
  if (prompt(`Saisir ${confirmation} pour confirmer`) !== confirmation) return;
  value.confirm = confirmation;
  if (kind !== 'delete') value.pem = $('tls-pem').value;
  show(await call('/api/tls/object', value)); $('tls-pem').value = '';
});
$('transport').onsubmit = handle(async () => {
  const value = {input: $('correction-input').value, gnss: {enabled: $('gnss-enabled').checked,
    tx_pin: Number($('gnss-tx').value), rx_pin: Number($('gnss-rx').value),
    baud: Number($('gnss-baud').value), model: $('gnss-model').value}};
  if (!confirm('Appliquer ce transport au câblage GNSS réel ? Une commande en cours sera interrompue.')) return;
  show(await call('/api/config', value)); await load();
});
call('/api/capabilities', undefined, false).then(caps => {
  $('gnss').hidden = !caps.gnss;
  for (const pin of (caps.gnss_allowed_pins || [])) for (const id of ['gnss-tx','gnss-rx']) {
    const option = document.createElement('option'); option.value = String(pin); option.textContent = String(pin); $(id).appendChild(option);
  }
}).catch(e => show(e.message));
refresh(); setInterval(refresh, 3000);

$('telemetry').onsubmit = handle(async () => {
  const value = {rate_hz: Number($('telemetry-rate').value), persist: $('telemetry-persist').checked,
    confirm: 'APPLY_GNSS_TELEMETRY'};
  if (!confirm('Modifier les messages du récepteur identifié ? Les corrections locales seront suspendues pendant la configuration.')) return;
  if (value.persist) {
    if (prompt('Écriture persistante : saisir SAVE_RECEIVER_CONFIGURATION') !== 'SAVE_RECEIVER_CONFIGURATION') return;
    value.persistent_confirmation = 'SAVE_RECEIVER_CONFIGURATION';
  }
  show(await call('/api/gnss/telemetry/apply', value));
});

$('detect').onclick = handle(async () => {
  if (!confirm('Relancer les interrogations de modèle en lecture seule ? Les corrections locales peuvent être interrompues brièvement. La quarantaine restera active.')) return;
  show(await call('/api/gnss/detect', {confirm: 'DETECT_RECEIVER'})); receiverPlans = null; await refresh();
});

$('rollback').onclick = handle(async () => {
  const generation = sessionGeneration, history = await call('/api/config/history');
  if (generation !== sessionGeneration || !token) return;
  if (!history.writable || !history.previous_available) throw new Error('Aucune configuration précédente intacte disponible');
  if (prompt('Restaurer les réglages de la passerelle ? Les commandes déjà appliquées au GNSS ne seront pas annulées. Saisir ROLLBACK_CONFIGURATION. Identité admin et quarantaine conservées.') !== 'ROLLBACK_CONFIGURATION') return;
  show(await call('/api/config/rollback', {confirm:'ROLLBACK_CONFIGURATION', expected_generation:history.generation}));
  await load();
});

$('events-read').onclick = handle(async () => {
  const generation = sessionGeneration, report = await call('/api/diagnostics/events');
  if (generation !== sessionGeneration || !token) return;
  $('events').textContent = print(report);
});

async function readNtrip() {
  const generation = sessionGeneration, report = await call('/api/ntrip/runtime');
  if (generation !== sessionGeneration || !token) return;
  $('ntrip-runtime').textContent = print(report);
}
$('ntrip-read').onclick = handle(readNtrip);
$('ntrip-restart').onclick = handle(async () => {
  if (!confirm('Interrompre puis redémarrer toutes les connexions NTRIP ? Le récepteur GNSS reste inchangé.')) return;
  const generation = sessionGeneration;
  const result = await call('/api/ntrip/restart', {confirm:'RESTART_NTRIP_SERVICES'});
  if (generation !== sessionGeneration || !token) return;
  show(result); await readNtrip();
});
