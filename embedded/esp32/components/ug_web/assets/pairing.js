/* SPDX-License-Identifier: LGPL-3.0-only
 * Two independent stores: never claim atomic pairing or RF delivery. No persistence/logging.
 */
(() => {
'use strict';
const hex = bytes => Array.from(bytes, n => n.toString(16).padStart(2, '0')).join('');
const mac = text => typeof text === 'string' && /^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(text) &&
  !(parseInt(text.slice(0, 2), 16) & 1) && !/^00(:00){5}$/.test(text);
async function pair(port, api, progress, signal, approve) {
  let reader, writer, opened = false, pending = '', budget = 0, key = '', random;
  let diagnostic='Appairage non effectué. Vérifiez la session S3, le port et le firmware C3 Rover UG ; RESET du C3 si nécessaire.';
  let roverWrite = false, baseWrite = false, verified = false, closed = false, failure;
  const nonceBytes = crypto.getRandomValues(new Uint8Array(8)), nonce = hex(nonceBytes);
  nonceBytes.fill(0);
  const check = () => { if (signal.aborted) throw Error('Opération annulée'); };
  async function bounded(operation, ms) {
    check(); let timer, abort;
    try {
      return await Promise.race([operation, new Promise((_, reject) => {
        timer = setTimeout(() => reject(Error('Délai dépassé')), ms);
        abort = () => reject(Error('Opération annulée'));
        signal.addEventListener('abort', abort, {once:true});
      })]);
    } finally { clearTimeout(timer); signal.removeEventListener('abort', abort); }
  }
  async function send(command) {
    check();
    await bounded(writer.write(new TextEncoder().encode(command + '\n')), 3000);
    const deadline = Date.now() + 5000;
    for (;;) {
      check();
      const newline = pending.indexOf('\n');
      if (newline >= 0) {
        const line = pending.slice(0, newline).replace(/\r$/, ''); pending = pending.slice(newline + 1);
        if (!line.startsWith('UGCFG/1 ')) continue;
        let value; try { value = JSON.parse(line.slice(8)); } catch (_) { throw Error('Réponse UGCFG invalide'); }
        if (!value || typeof value !== 'object' || value.error) throw Error('Commande refusée par le Rover');
        return value;
      }
      const chunk = await bounded(reader.read(), Math.max(1, deadline - Date.now()));
      if (chunk.done) throw Error('USB déconnecté');
      budget += chunk.value.length;
      if (budget > 16384) throw Error('Flux USB incompatible');
      pending += new TextDecoder().decode(chunk.value);
      if (pending.length > 2048) throw Error('Réponse USB trop grande');
    }
  }
  function control(value) {
    if (value.mode !== 'control' || value.nonce !== nonce || value.writable !== true ||
        value.firmware !== 'universal-gnss' || value.board !== 'esp32c3' || value.role !== 'rover' ||
        !mac(value.sta_mac)) throw Error('Firmware C3 Rover UG non reconnu ou stockage verrouillé');
    return value;
  }
  async function cleanup(operation) {
    let timer;
    try { await Promise.race([operation(), new Promise((_, reject) => { timer = setTimeout(() => reject(Error()), 2000); })]); }
    catch (_) { return false; } finally { clearTimeout(timer); }
    return true;
  }
  try {
    progress('Connexion USB au C3 Rover…');
    // No DTR/RTS reset on an unidentified board. If maintenance is closed, ask for physical reset.
    check();
    const opening=port.open({baudRate:115200, bufferSize:2048});
    try { await bounded(opening,5000); opened=true; }
    catch(error){opening.then(()=>port.close()).catch(()=>{});throw error;}
    reader = port.readable.getReader(); writer = port.writable.getWriter();
    progress('Identification UGCFG/1… Si nécessaire, appuyez sur RESET du C3 puis réessayez.');
    const rover = control(await send('UGCFG/1 OPEN ' + nonce));
    const portable=UGPortable.validate((await send('UGCFG/1 CONFIG_GET')).portable,'supermini-c3');
    if(portable.output!=='usb'){diagnostic='Ce parcours exige un C3 Rover configuré en sortie RTCM USB. Aucune écriture effectuée.';throw Error();}
    const base = await api('/api/radio/pairing'); check();
    if (!mac(base.sta_mac) || base.sta_mac.toLowerCase() === rover.sta_mac.toLowerCase() ||
        !Number.isInteger(base.configured_channel) || base.configured_channel < 1 || base.configured_channel > 11)
      throw Error('Identité ou canal S3 indisponible');
    const settings=await api('/api/config'); check();
    if(settings.wifi?.sta!==false){diagnostic='Wi-Fi STA du S3 actif ou inconnu : appairage à canal fixe refusé avant écriture.';throw Error();}
    const channel = base.configured_channel;
    if (!await approve(channel, rover.paired || base.key_set)) return {cancelled:true};
    check();
    const renewed = control(await send('UGCFG/1 OPEN ' + nonce));
    if (renewed.sta_mac !== rover.sta_mac) throw Error('Identité C3 modifiée');
    const window=await api('/api/radio/pairing/window',{confirm:'OPEN_LOCAL_PAIRING'}); check();
    if(window.window_open!==true)throw Error('Fenêtre S3 non confirmée');
    random = crypto.getRandomValues(new Uint8Array(16)); key = hex(random);
    progress('Enregistrement vérifié sur le Rover…');
    roverWrite = true; // A lost reply can follow a successful commit. Treat it as uncertain.
    const saved = control(await send(`UGCFG/1 SET ${base.sta_mac} ${channel} ${key} CONFIRM_PAIR`));
    if (!saved.paired || !saved.key_set || saved.peer.toLowerCase() !== base.sta_mac.toLowerCase() || saved.channel !== channel)
      throw Error('Enregistrement C3 non confirmé');
    progress('Enregistrement sécurisé sur le S3…');
    baseWrite = true;
    await api('/api/radio/pairing/apply', {confirm:'PAIR_ESPNOW_ROVER', peer:rover.sta_mac, channel, key});
    key = ''; random.fill(0); random = null; check();
    const finalBase = await api('/api/radio/pairing');
    const finalRover = control(await send('UGCFG/1 GET')); check();
    if (!finalBase.key_set || !finalBase.enabled || finalBase.peer.toLowerCase() !== rover.sta_mac.toLowerCase() ||
        finalBase.configured_channel !== channel || !finalRover.paired || !finalRover.key_set ||
        finalRover.peer.toLowerCase() !== base.sta_mac.toLowerCase() || finalRover.channel !== channel)
      throw Error('Configurations croisées non confirmées');
    verified = true;
    progress('Deux configurations enregistrées ; sortie de maintenance du Rover…');
    const run = await send('UGCFG/1 RUN CONFIRM_RTCM');
    if (run.mode !== 'closed') throw Error('Sortie de maintenance non confirmée');
    closed = true;
    progress('Configurations vérifiées. Redémarrage du S3…');
    await api('/api/radio/pairing/restart', {confirm:'RESTART_AFTER_PAIRING'});
    return {configured:true, rebooting:true, peer:rover.sta_mac, channel};
  } catch (_) {
    // Never surface transport/firmware text that might echo a command containing a key.
    failure = Error(verified ? 'Les deux configurations sont enregistrées, mais le démarrage reste à vérifier.' :
      baseWrite ? 'Appairage partiel ou incertain : écritures C3 et S3 tentées. Vérification requise ; aucune réussite confirmée.' :
      roverWrite ? 'Appairage partiel ou incertain : écriture C3 tentée, S3 non confirmé. Relancez après RESET du C3.' :
      signal.aborted ? 'Appairage annulé avant écriture.' : diagnostic);
    throw failure;
  } finally {
    key = ''; if (random) random.fill(0); pending = '';
    let clean = true;
    if (reader) { clean = await cleanup(() => reader.cancel()) && clean; try { reader.releaseLock(); } catch (_) { clean = false; } }
    if (writer) { if (!closed) clean = await cleanup(() => writer.abort()) && clean; try { writer.releaseLock(); } catch (_) { clean = false; } }
    if (opened) clean = await cleanup(() => port.close()) && clean;
    if (!clean && !failure) throw Error('USB non libéré : configurations éventuellement enregistrées ; vérifier avant de recommencer.');
  }
}
globalThis.UGRoverPairing = {pair};
})();
