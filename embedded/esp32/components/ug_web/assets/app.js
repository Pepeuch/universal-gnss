'use strict';
let token = '';
const $ = id => document.getElementById(id);
const show = value => { $('notice').textContent = String(value); };
const print = value => JSON.stringify(value, null, 2);
async function call(path, body, authorized = true) {
  const headers = {};
  if (authorized && token) headers.Authorization = `Bearer ${token}`;
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  const response = await fetch(path, {method: body === undefined ? 'GET' : 'POST', headers,
    body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store', credentials: 'omit'});
  const text = await response.text();
  if (!response.ok) throw new Error(`${response.status} ${text}`);
  if (response.headers.get('Content-Type')?.includes('application/json')) return JSON.parse(text);
  return text;
}
async function refresh() {
  try {
    $('status').textContent = print(await call('/api/status', undefined, false));
    if (token && !$('gnss').hidden) $('receiver').textContent = print(await call('/api/gnss/status'));
  } catch (e) { show(e.message); }
}
async function load() {
  const config = await call('/api/config'); $('config').value = print(config);
  if (config.gnss) {
    $('gnss-enabled').checked = config.gnss.enabled;
    $('gnss-tx').value = String(config.gnss.tx_pin); $('gnss-rx').value = String(config.gnss.rx_pin);
    $('gnss-baud').value = String(config.gnss.baud); $('gnss-model').value = config.gnss.model;
    $('correction-input').value = config.input;
  }
}
function handle(fn) { return async event => { event?.preventDefault(); try { await fn(event); } catch (e) { show(e.message); } }; }
$('login').onclick = handle(async () => { token = $('token').value; $('token').value = ''; await load(); await refresh(); show('Session ouverte'); });
$('logout').onclick = () => { token = ''; $('token').value = ''; $('config').value = ''; $('receiver').textContent = 'Session fermée'; show('Session fermée'); };
$('load').onclick = handle(load);
$('save').onclick = handle(async () => { const value = JSON.parse($('config').value); if (confirm('Sauvegarder cette configuration ?')) { show(await call('/api/config', value)); await load(); } });
$('export').onclick = handle(async () => {
  const blob = new Blob([print(await call('/api/config'))], {type:'application/json'});
  const url = URL.createObjectURL(blob), link = document.createElement('a'); link.href = url; link.download = 'ug-config-redacted.json'; link.click(); URL.revokeObjectURL(url);
});
$('import').onchange = handle(async event => { const file = event.target.files[0]; if (!file || file.size > 8192) throw new Error('Fichier JSON : maximum 8192 octets'); const text = await file.text(); JSON.parse(text); $('config').value = text; });
$('base').onsubmit = handle(async event => {
  const fields = new FormData(event.target), value = {mode: fields.get('mode'), height_datum: fields.get('height_datum'), rtcm_output: true, persist: fields.has('persist')};
  for (const key of ['latitude_deg','longitude_deg','height_m','duration_s','accuracy_01mm','reuse_distance_m']) {
    const input = fields.get(key); if (input !== '') value[key] = Number(input);
  }
  if (!confirm('Appliquer ce plan au récepteur identifié ? Une erreur peut interrompre les corrections.')) return;
  value.confirm = 'APPLY_BASE_CONFIGURATION';
  if (value.persist || value.mode === 'time') {
    if (prompt('Écriture persistante : saisir SAVE_RECEIVER_CONFIGURATION') !== 'SAVE_RECEIVER_CONFIGURATION') return;
    value.persistent_confirmation = 'SAVE_RECEIVER_CONFIGURATION';
  }
  show(await call('/api/gnss/base/apply', value));
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
