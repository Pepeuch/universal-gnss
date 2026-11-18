/* Universal GNSS — configurateur de broches multi-cartes.
 * Le JSON de brochage reste distinct de la configuration runtime UG.
 * Ce module n'ecrit jamais en NVS et ne modifie aucun binaire.
 */
(() => {
  'use strict';
  const boards = window.UGPinBoards || [];
  const byId = new Map(boards.map(board => [board.id, board]));
  const $ = id => document.getElementById(id);
  const svgHost = $('ug-pin-svg');
  if (!svgHost || !boards.length) return;
  const boardSelect = $('ug-pin-board');
  const busSelect = $('ug-pin-bus');
  const signalSelect = $('ug-pin-signal');
  const gnssSelect = $('ug-pin-gnss-uart');
  const status = $('ug-pin-status');
  const bridge = $('ug-pin-bridge');
  const assignmentsElement = $('ug-pin-assignments');
  const fixedElement = $('ug-pin-fixed');
  const creditLink = $('ug-pin-source-link');
  const fullView = $('ug-pin-full');
  const flashRegion = $('ug-flasher-region');
  const flashWarning = $('ug-flasher-warning');
  const flasherDevice = $('device');
  const ns = 'http://www.w3.org/2000/svg';
  const STORAGE_KEY = 'universal-gnss.pin-configurator.v1';
  const colors = { UART: '#3b82f6', SPI: '#d89b38', I2C: '#18a68a', GPIO: '#a36ae6' };
  let svgToken = 0;
  let currentId = boards[0].id;
  const profiles = Object.create(null);

  const fresh = board => ({
    schema: 'ug-pin-config-v1', board: board.id,
    gnss_uart: board.defaultGnssUart, assignments: {}
  });
  const currentBoard = () => byId.get(currentId);
  const currentProfile = () => profiles[currentId];
  const selectedBus = () => currentBoard().buses.find(bus => bus.id === busSelect.value);
  const signalKey = () => `${busSelect.value}.${signalSelect.value}`;
  const isIntegerPin = value => Number.isInteger(value) && value >= 0 && value <= 48;
  const hasPin = (board, gpio) => board.pins.includes(gpio);

  function signalNeedsOutput(bus, signal) {
    // RX / CTS / MISO / IRQ / PPS : entree possible sur GPIO input-only.
    if (bus.kind === 'UART') return signal === 'TX' || signal === 'RTS';
    if (bus.kind === 'SPI') return signal !== 'MISO';
    if (bus.kind === 'I2C') return true; // bus bidirectionnel open-drain
    return signal === 'PWM' || signal.startsWith('AUX');
  }

  function checkPin(board, key, gpio) {
    if (!isIntegerPin(gpio) || !hasPin(board, gpio)) return 'GPIO non expose ou non verifie sur cette carte';
    if (Object.hasOwn(board.locked, gpio)) return board.locked[gpio];
    const dot = key.indexOf('.');
    if (dot < 1 || dot === key.length - 1) return 'Fonction invalide';
    const bus = board.buses.find(item => item.id === key.slice(0, dot));
    const signal = key.slice(dot + 1);
    if (!bus || !bus.signals.includes(signal)) return 'Fonction non supportee par ce profil';
    if (bus.locked) return `Bus reserve : ${bus.locked}`;
    if (board.inputOnly.includes(gpio) && signalNeedsOutput(bus, signal)) {
      return 'GPIO entree seule : signal necessitant une sortie';
    }
    return null;
  }

  function validateProfile(data, requireReady = false) {
    if (!data || typeof data !== 'object' || Array.isArray(data) || data.schema !== 'ug-pin-config-v1') {
      throw new Error('Schema de profil inconnu (ug-pin-config-v1 attendu)');
    }
    const board = byId.get(data.board);
    if (!board) throw new Error('Carte non prise en charge par le configurateur');
    if (requireReady && (!board.svg || !board.pins.length)) throw new Error('Carte en attente de son SVG et de ses GPIO valides');
    if (data.gnss_uart != null && !board.gnssUarts.includes(data.gnss_uart)) {
      throw new Error('UART GNSS non disponible sur cette carte');
    }
    if (!data.assignments || typeof data.assignments !== 'object' || Array.isArray(data.assignments)) {
      throw new Error('La liste des affectations est invalide');
    }
    const checked = {};
    const used = new Map();
    for (const [key, gpio] of Object.entries(data.assignments)) {
      const reason = checkPin(board, key, gpio);
      if (reason) throw new Error(`${key} = GPIO${gpio} : ${reason}`);
      if (used.has(gpio)) throw new Error(`Conflit GPIO${gpio} : ${used.get(gpio)} et ${key}`);
      checked[key] = gpio;
      used.set(gpio, key);
    }
    return { schema:'ug-pin-config-v1', board: board.id,
      gnss_uart: data.gnss_uart ?? board.defaultGnssUart, assignments:checked };
  }

  function say(message, error = false) {
    status.textContent = message;
    status.classList.toggle('warn', error);
    status.classList.toggle('ready', !error);
  }
  function persist() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ board: currentId, profiles }));
    } catch (_) { /* Le mode prive peut interdire localStorage. */ }
  }
  function restore() {
    try {
      const data = JSON.parse(localStorage.getItem(STORAGE_KEY) || 'null');
      if (!data || typeof data !== 'object') return;
      for (const board of boards) {
        const profile = data.profiles?.[board.id];
        if (!profile) continue;
        try { profiles[board.id] = validateProfile(profile); } catch (_) { /* Ignorer cache obsolete */ }
      }
      if (byId.has(data.board)) currentId = data.board;
    } catch (_) { /* Ignore cache illisible */ }
  }
  function cloneProfile() {
    return JSON.parse(JSON.stringify(currentProfile()));
  }
  function publish() {
    persist();
    document.dispatchEvent(new CustomEvent('ug:pin-profile-changed', { detail:cloneProfile() }));
  }

  function fillBoardOptions() {
    for (const board of boards) {
      const option = document.createElement('option');
      option.value = board.id;
      option.textContent = `${board.label}${board.svg ? '' : ' (SVG a venir)'}`;
      boardSelect.append(option);
    }
  }
  function fillBusOptions() {
    busSelect.replaceChildren();
    const board = currentBoard();
    const labels = { UART:'UART', SPI:'SPI', I2C:'I²C', GPIO:'GPIO / PPS' };
    for (const kind of ['UART','SPI','I2C','GPIO']) {
      const entries = board.buses.filter(bus => bus.kind === kind);
      if (!entries.length) continue;
      const group = document.createElement('optgroup');
      group.label = labels[kind];
      for (const bus of entries) {
        const option = document.createElement('option');
        option.value = bus.id;
        option.disabled = Boolean(bus.locked);
        option.textContent = `${bus.id}${bus.locked ? ' (reserve)' : ''}`;
        group.append(option);
      }
      busSelect.append(group);
    }
    busSelect.disabled = !board.svg || !board.buses.some(bus => !bus.locked);
    const preferred = board.buses.find(bus => bus.kind === 'UART' && !bus.locked)
      ?? board.buses.find(bus => !bus.locked);
    if (preferred) busSelect.value = preferred.id;
    fillSignalOptions();
  }
  function fillSignalOptions() {
    signalSelect.replaceChildren();
    const bus = selectedBus();
    for (const sig of bus?.signals || []) {
      const option = document.createElement('option');
      option.value = sig;
      option.textContent = sig;
      signalSelect.append(option);
    }
    signalSelect.disabled = busSelect.disabled || !bus;
  }
  function fillGnssOptions() {
    gnssSelect.replaceChildren();
    const board = currentBoard();
    if (!board.gnssUarts.length) {
      const o = document.createElement('option'); o.textContent = 'Non defini'; o.value = '';
      gnssSelect.append(o);
    } else {
      for (const busId of board.gnssUarts) {
        const o = document.createElement('option'); o.value = busId; o.textContent = busId;
        gnssSelect.append(o);
      }
    }
    gnssSelect.disabled = !board.svg || !board.gnssUarts.length;
    gnssSelect.value = currentProfile().gnss_uart ?? '';
  }

  function syncFlashBoard() {
    const fw = currentBoard().firmwareDevice;
    const canFlash = Boolean(fw && [...flasherDevice.options].some(option => option.value === fw));
    flashRegion.classList.toggle('hidden', !canFlash);
    flashRegion.inert = !canFlash;
    flashWarning.classList.toggle('hidden', canFlash);
    if (canFlash && flasherDevice.value !== fw) {
      flasherDevice.value = fw;
      flasherDevice.dispatchEvent(new Event('change', { bubbles:true }));
    }
    if (!canFlash) flashWarning.textContent = `Aucun firmware de flasher disponible pour « ${currentBoard().label} ». Le configurateur ne permet pas de flasher une autre carte par erreur.`;
  }

  // Pont limité au formulaire portable existant (UART GNSS seulement).
  // SPI/I2C/GPIO/PPS restent dans le JSON materiel; support NVS a ajouter dans UG.
  function bridgePortable() {
    const profile = currentProfile();
    const board = currentBoard();
    if (!board.svg) {
      bridge.textContent = 'Profil en attente de son SVG : aucune broche transferee.';
      return;
    }
    if (!board.firmwareDevice || flasherDevice?.value !== board.firmwareDevice) {
      bridge.textContent = 'Carte configurée pour préparer son câblage, mais pas qualifiée pour le firmware UG actuel : aucune donnée transférée au formulaire portable.';
      return;
    }
    const uart = profile.gnss_uart;
    const tx = profile.assignments[`${uart}.TX`];
    const rx = profile.assignments[`${uart}.RX`];
    if (tx === undefined || rx === undefined) {
      bridge.textContent = `UART GNSS ${uart || 'non defini'} incomplet : TX et RX doivent etre affectes. Le formulaire portable conserve ses valeurs independantes tant que le couple n'est pas complet.`;
      return;
    }
    const fields = [['TX', tx], ['RX', rx]];
    const missing = fields.filter(([name, gpio]) => {
      const field = $(`portable-${name.toLowerCase()}`);
      return !field || ![...field.options].some(opt => opt.value === String(gpio));
    });
    if (missing.length) {
      bridge.textContent = `Couple ${uart} : TX=GPIO${tx}, RX=GPIO${rx}. Non applicable au profil portable actuel (options absentes : ${missing.map(([name])=>name).join(', ')}). Le profil JSON conserve les broches.`;
      return;
    }
    const portableOutput = $('portable-output');
    // Ne pas modifier la sortie du runtime en cachette; elle reste choisie par l'utilisateur.
    if (board.id === 's3-gateway' && portableOutput?.value !== 'gnss') {
      bridge.textContent = `TX=GPIO${tx}, RX=GPIO${rx} prets. Choisir « GNSS S3 » dans Sortie pour les appliquer au profil portable.`;
      return;
    }
    if (board.id === 'c3-rover-usb' && portableOutput?.value !== 'uart') {
      bridge.textContent = `TX=GPIO${tx}, RX=GPIO${rx} prets. Choisir « RTCM UART C3 » dans Sortie pour les appliquer au profil portable.`;
      return;
    }
    for (const [name, gpio] of fields) {
      const field = $(`portable-${name.toLowerCase()}`);
      if (field.value !== String(gpio)) {
        field.value = String(gpio);
        field.dispatchEvent(new Event('change', { bubbles:true }));
      }
    }
    bridge.textContent = `${uart} : TX=GPIO${tx}, RX=GPIO${rx} copies dans les champs du profil portable. Les autres bus ne sont pas encore provisionnes automatiquement.`;
  }

  function renderFixed() {
    if (!fixedElement) return;
    fixedElement.replaceChildren();
    const groups = currentBoard().fixedGroups || [];
    if (!groups.length) {
      const p = document.createElement('p');
      p.className = 'muted'; p.textContent = 'Aucune fonction matérielle documentée pour cette carte.';
      fixedElement.append(p);
      return;
    }
    for (const group of groups) {
      const details = document.createElement('details');
      details.className = 'ug-pin-fixed-group';
      details.open = Boolean(group.open);
      const summary = document.createElement('summary');
      const title = document.createElement('strong'); title.textContent = group.title;
      const state = document.createElement('span'); state.className='ug-pin-fixed-state';
      state.textContent = group.state;
      summary.append(title, state);
      const body = document.createElement('p'); body.textContent = group.summary;
      const pins = document.createElement('p'); pins.className = 'muted'; pins.textContent = group.pins;
      details.append(summary, body, pins);
      fixedElement.append(details);
    }
  }
  function renderAssignments() {
    assignmentsElement.replaceChildren();
    const items = Object.entries(currentProfile().assignments).sort(([a], [b]) => a.localeCompare(b));
    if (!items.length) {
      const li = document.createElement('li');
      li.className = 'muted'; li.textContent = 'Aucune affectation utilisateur.';
      assignmentsElement.append(li);
    }
    for (const [key, gpio] of items) {
      const li = document.createElement('li');
      li.className = 'ug-pin-assignment';
      const name = document.createElement('span');
      name.className = 'ug-pin-label';
      name.textContent = `${key.replace('.', ' ')} — GPIO${gpio}`;
      const remove = document.createElement('button');
      remove.type = 'button'; remove.className = 'secondary';
      remove.textContent = 'Retirer';
      remove.setAttribute('aria-label', `Retirer ${key} sur GPIO${gpio}`);
      remove.addEventListener('click', () => {
        delete currentProfile().assignments[key];
        render(); publish(); bridgePortable();
        say(`${key} libere.`);
      });
      li.append(name, remove);
      assignmentsElement.append(li);
    }
  }

  function colorFor(key) {
    const prefix = key.split('.')[0];
    return colors[prefix.replace(/\d+$/, '')] || colors.GPIO;
  }
  function decorateSvg() {
    const svg = svgHost.querySelector('svg');
    if (!svg) return;
    const board = currentBoard();
    const current = currentProfile();
    const bySlot = new Map([...svg.querySelectorAll('.bpad[data-slot]')].map(el => [el.dataset.slot, el]));
    const used = new Map(Object.entries(current.assignments).map(([key, gpio]) => [String(gpio), key]));
    for (const el of svg.querySelectorAll('.ug-pin-assigned,.ug-pin-locked')) {
      el.classList.remove('ug-pin-assigned','ug-pin-locked');
      el.style.removeProperty('--ug-pin-color');
    }
    svg.querySelectorAll('.ug-pin-role,.ug-pin-dynamic-title').forEach(el => el.remove());
    for (const row of svg.querySelectorAll('.prow[data-slot]')) {
      const pad = bySlot.get(row.dataset.slot);
      const raw = pad?.dataset.gpio ?? board.extraPads?.[row.dataset.slot]?.toString();
      if (!raw || !/^\d+$/.test(raw)) continue;
      const gpio = Number(raw);
      const key = used.get(raw);
      const locked = Object.hasOwn(board.locked, gpio);
      if (locked) {
        row.classList.add('ug-pin-locked'); pad?.classList.add('ug-pin-locked');
      }
      if (key) {
        const color = colorFor(key);
        for (const el of [row,pad].filter(Boolean)) {
          el.classList.add('ug-pin-assigned');
          el.style.setProperty('--ug-pin-color',color);
        }
        const text = document.createElementNS(ns,'text');
        text.classList.add('ug-pin-role');
        // Position relative a la ligne existante du dessin : ne modifie pas l'original.
        text.setAttribute('x', row.dataset.slot.startsWith('L') ? '-15' : '15');
        text.setAttribute('y', '-16');
        text.setAttribute('text-anchor', 'middle');
        text.setAttribute('font-size', '10');
        text.setAttribute('font-family', 'system-ui, sans-serif');
        text.setAttribute('font-weight', '800');
        text.setAttribute('fill', color);
        text.setAttribute('stroke', '#ffffff');
        text.setAttribute('stroke-width', '2.5');
        text.setAttribute('paint-order', 'stroke');
        text.textContent = key.replace('.', ' ');
        row.append(text);
      }
      const title = document.createElementNS(ns, 'title');
      title.classList.add('ug-pin-dynamic-title');
      title.textContent = `GPIO${gpio} — ${locked ? 'RESERVE : '+board.locked[gpio] : key ? key : (board.warnings[gpio] || 'Libre')}`;
      pad?.prepend(title);
      row.append(title.cloneNode(true));
      row.setAttribute('tabindex', '0'); row.setAttribute('role', 'button');
      row.setAttribute('aria-label', title.textContent);
    }
  }
  function render() { renderFixed(); renderAssignments(); decorateSvg(); }

  function assign(event) {
    const el = event.target instanceof Element
      ? event.target.closest('.bpad[data-slot],.prow[data-slot]') : null;
    if (!el) return;
    const svg = svgHost.querySelector('svg');
    if (!svg) return;
    const pad = [...svg.querySelectorAll('.bpad[data-slot]')].find(node => node.dataset.slot === el.dataset.slot);
    const raw = pad?.dataset.gpio ?? currentBoard().extraPads?.[el.dataset.slot]?.toString();
    if (!raw || !/^\d+$/.test(raw)) {
      say('Cette broche correspond a une alimentation, une masse ou une fonction non-GPIO.', true);
      return;
    }
    const gpio = Number(raw);
    const key = signalKey();
    const board = currentBoard();
    const reason = checkPin(board, key, gpio);
    if (reason) { say(`GPIO${gpio} refuse pour ${key} : ${reason}`, true); return; }
    const other = Object.entries(currentProfile().assignments)
      .find(([name, number]) => name !== key && number === gpio);
    if (other) {
      say(`Conflit : GPIO${gpio} est deja affecte a ${other[0]}. Retirer d'abord cette fonction.`, true);
      return;
    }
    const warning = board.warnings[gpio] ||
      (pad && (pad.classList.contains('warn') || pad.classList.contains('sys')) ? pad.dataset.note : null);
    if (warning && !window.confirm(`GPIO${gpio} : ${warning}\n\nAffecter a ${key} ?`)) return;
    currentProfile().assignments[key] = gpio;
    render(); publish(); bridgePortable();
    say(`${key} -> GPIO${gpio}. Brochage sauvegarde localement. A verifier sur le materiel.`);
  }

  function scrubSvg(svg) {
    // Un SVG de projet peut contenir des metadonnees Inkscape ; retirer toute surface executable.
    svg.querySelectorAll('script,foreignObject,iframe,object,embed').forEach(node => node.remove());
    for (const node of [svg,...svg.querySelectorAll('*')]) {
      for (const attr of [...node.attributes]) {
        const value = attr.value || '';
        if (/^on/i.test(attr.name) || (/(?:href|src)$/i.test(attr.name) && !value.startsWith('#'))) {
          node.removeAttributeNode(attr);
        }
      }
    }
  }
  function applyViewBox() {
    const svg = svgHost.querySelector('svg');
    if (!svg) return;
    svg.setAttribute('viewBox', (!fullView?.checked && currentBoard().svgViewBox)
      || svg.dataset.originalViewbox);
  }
  async function loadSvg() {
    const token = ++svgToken;
    const board = currentBoard();
    svgHost.replaceChildren();
    if (!board.svg) {
      const p = document.createElement('p');
      p.style.color = '#334155';
      p.textContent = `SVG non disponible pour « ${board.label} ». Ajouter le fichier et verifier les GPIO dans flasher/pinout/boards.js.`;
      svgHost.append(p);
      say('Profil reserve a une future carte : aucune affectation possible avant validation du brochage.', true);
      return;
    }
    try {
      const response = await fetch(board.svg);
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const xml = new DOMParser().parseFromString(await response.text(),'image/svg+xml');
      if (xml.querySelector('parsererror') || xml.documentElement.localName !== 'svg') throw new Error('SVG invalide');
      if (token !== svgToken) return; // carte changee entre-temps
      const svg = document.importNode(xml.documentElement, true);
      scrubSvg(svg);
      svg.removeAttribute('width'); svg.removeAttribute('height');
      svg.dataset.originalViewbox = svg.getAttribute('viewBox') || '0 0 100 100';
      svg.setAttribute('aria-label', `Brochage interactif ${board.label}`);
      const dataPads = [...svg.querySelectorAll('.bpad[data-slot][data-gpio]')];
      if (!dataPads.length) throw new Error('SVG sans broches .bpad data-slot / data-gpio');
      svgHost.replaceChildren(svg);
      applyViewBox();
      render();
      say('SVG charge. Selectionne une fonction et clique sur une broche libre.');
    } catch (error) {
      if (token !== svgToken) return;
      say(`Chargement du SVG impossible : ${error.message}. Utiliser HTTP(S) ou localhost, pas file://.`,true);
    }
  }

  function selectBoard(id, syncFlasher = true) {
    if (!byId.has(id)) return;
    currentId = id;
    if (!profiles[id]) profiles[id] = fresh(currentBoard());
    boardSelect.value = id;
    if (fullView) fullView.checked = false;
    if (creditLink) {
      creditLink.href = currentBoard().sourceUrl || 'https://www.espboards.dev/';
      creditLink.textContent = `ESPBoards · ${currentBoard().label}`;
      creditLink.parentElement.hidden = !currentBoard().sourceUrl;
    }
    fillBusOptions(); fillGnssOptions();
    $('ug-pin-board-note').textContent = currentBoard().description;
    if (syncFlasher) syncFlashBoard();
    loadSvg(); bridgePortable(); publish();
  }

  function download() {
    const json = JSON.stringify(cloneProfile(), null, 2) + '\n';
    const url = URL.createObjectURL(new Blob([json],{type:'application/json'}));
    const link = document.createElement('a');
    link.href = url; link.download = `ug-pinout-${currentId}.json`;
    document.body.append(link); link.click(); link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 3000);
    say('Profil materiel JSON exporte. Le firmware ne recoit rien tant que sa configuration runtime n\'est pas appliquee.');
  }
  async function importFile(file) {
    if (!file) return;
    if (file.size > 1024 * 1024) throw new Error('Taille de fichier superieure a 1 Mo');
    const profile = validateProfile(JSON.parse(await file.text()));
    profiles[profile.board] = profile;
    selectBoard(profile.board);
    render(); bridgePortable();
    say(`Profil ${profile.board} importe et charge. Les GPIO doivent etre verifies avant tout flash.`);
  }

  restore();
  fillBoardOptions();
  selectBoard(currentId);
  boardSelect.addEventListener('change', () => selectBoard(boardSelect.value));
  busSelect.addEventListener('change', fillSignalOptions);
  gnssSelect.addEventListener('change', () => {
    currentProfile().gnss_uart = gnssSelect.value || null;
    publish(); bridgePortable();
  });
  svgHost.addEventListener('click', assign);
  fullView?.addEventListener('change', applyViewBox);
  svgHost.addEventListener('keydown', event => {
    if (event.key === 'Enter' || event.key === ' ') {
      if (event.target.closest?.('.bpad,.prow')) {
        event.preventDefault(); assign(event);
      }
    }
  });
  $('ug-pin-export').addEventListener('click', download);
  $('ug-pin-import').addEventListener('change', async event => {
    const input = event.target;
    try { await importFile(input.files?.[0]); }
    catch (error) { say(`Import refuse : ${error.message}`,true); }
    finally { input.value=''; }
  });
  $('portable-output')?.addEventListener('change', bridgePortable);
  // Le flasher et ses scripts originaux restent maitres du selecteur #device.
  flasherDevice?.addEventListener('change', () => {
    if (flasherDevice.value !== currentBoard().firmwareDevice && byId.has(flasherDevice.value)) {
      selectBoard(flasherDevice.value,false);
      syncFlashBoard();
    }
  });
  // portable.js peuple ses listes au chargement : resynchroniser une fois ses scripts prets.
  window.addEventListener('load', bridgePortable);
  window.UGPinConfigurator = Object.freeze({
    getProfile:cloneProfile,
    getBoards: () => boards.map(({id,label,chip,firmwareDevice,status})=>({id,label,chip,firmwareDevice,status})),
    validate: validateProfile
  });
})();
