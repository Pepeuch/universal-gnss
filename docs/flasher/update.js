/* SPDX-License-Identifier: LGPL-3.0-only */
(() => {
  'use strict';
  const bytes = value => value instanceof Uint8Array ? value : new Uint8Array(value);
  const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

  function requireEqual(a, b, message) {
    if (!same(bytes(a), bytes(b))) throw Error(message);
  }

  function crcSequence(sequence) {
    let crc = 0;
    for (let i = 0; i < 4; i++) {
      crc ^= (sequence >>> (8 * i)) & 255;
      for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
  }
  async function digest(value) {
    return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', value)), v => v
      .toString(16).padStart(2, '0')).join('');
  }

  function application(image) {
    if (image.length < 288 || image[0] !== 0xe9 || new DataView(image.buffer, image.byteOffset)
      .getUint16(12, true) !== 9 || new DataView(image.buffer, image.byteOffset).getUint32(32,
        true) !== 0xabcd5432) throw Error('Application ESP32-S3 invalide');
  }

  function plan(backup, current, next, table, boot) {
    [backup, current, next, table, boot] = [backup, current, next, table, boot].map(bytes);
    if (backup.length !== 16 * 1024 * 1024) throw Error('Sauvegarde complète de 16 Mio requise');
    requireEqual(backup.slice(0, boot.length), boot, 'Bootloader incompatible');
    requireEqual(backup.slice(0x8000, 0x8000 + table.length), table,
      'Partitionnement incompatible');
    const view = new DataView(table.buffer, table.byteOffset, table.byteLength),
      rows = [];
    for (let p = 0; p + 32 <= table.length && view.getUint16(p, true) === 0x50aa; p += 32) rows
      .push({
        type: table[p + 2],
        sub: table[p + 3],
        offset: view.getUint32(p + 4, true),
        size: view.getUint32(p + 8, true)
      });
    const slots = rows.filter(r => r.type === 0).sort((a, b) => a.sub - b.sub),
      nvs = rows.filter(r => r.type === 1 && r.sub === 2),
      ota = rows.filter(r => r.type === 1 && r.sub === 0);
    if (slots.length !== 2 || slots[0].sub !== 16 || slots[1].sub !== 17 || nvs.length !== 1 ||
      nvs[0].offset !== 0x9000 || nvs[0].size !== 0x6000 || ota.length !== 1 || ota[0].size !==
      8192) throw Error('Partitions S3 non prises en charge');
    const sequences = [];
    for (const index of [0, 4096]) {
      const raw = backup.slice(ota[0].offset + index, ota[0].offset + index + 32);
      if (raw.every(v => v === 255)) continue;
      const v = new DataView(raw.buffer),
        seq = v.getUint32(0, true),
        state = v.getUint32(24, true);
      if (!seq || seq === 0xffffffff || ![2, 0xffffffff].includes(state) || v.getUint32(28,
        true) !== crcSequence(seq)) throw Error('Sélection OTA indéterminée');
      sequences.push(seq);
    }
    const slot = slots[sequences.length ? (Math.max(...sequences) - 1) % 2 : 0];
    application(current);
    application(next);
    if (next.length > slot.size || current.length > slot.size || slot.offset < nvs[0].offset +
      nvs[0].size || slot.offset + slot.size > backup.length) throw Error(
      'Limites de partition incompatibles');
    requireEqual(backup.slice(slot.offset, slot.offset + current.length), current,
      'Application installée différente de la référence');
    return {
      offset: slot.offset,
      size: next.length,
      nvs: nvs[0],
      ota: ota[0]
    };
  }

  function baseline(reference, sha256, size) {
    reference = reference?.binary ?? reference;
    const identity = reference?.firmware_identity;
    if (reference?.settings_schema !== 1 || reference.sha256 !== sha256 || reference.app_bytes !==
      size || reference.profile !== 'gateway' || identity?.hardware_variant !==
      'waveshare-s3-eth' || !['http', 'https'].includes(identity.web_transport) || !
      /^v?6\.1(?:$|[.-])/.test(identity.esp_idf)) throw Error(
      'Rapport installé incompatible ou différent du binaire');
    return {
      settings_schema: 1,
      sha256,
      profile: {
        board: 'waveshare-s3-eth'
      }
    };
  }
  async function restart(transport, wait = ms => new Promise(resolve => setTimeout(resolve,
  ms))) {
    // Espressif USB-Serial/JTAG hard-reset sequence, with GPIO0 released.
    await transport.setDTR(false);
    await transport.setRTS(true);
    await wait(200);
    await transport.setRTS(false);
    await wait(200);
  }
  async function run(loader, backup, current, verified, baseline, confirmWrite, progress) {
    const target = verified.entry;
    if (target.profile.board !== 'waveshare-s3-eth' || baseline?.profile?.board !==
      'waveshare-s3-eth' || target.settings_schema !== 1 || baseline.settings_schema !== 1)
      throw Error('Compatibilité de configuration non qualifiée pour ces versions');
    if (await digest(current) !== baseline.sha256) throw Error(
      'Référence installée non vérifiée');
    const part = offset => verified.parts.find(p => p.offset === offset)?.bytes;
    const next = verified.parts.find(p => p.path.endsWith('universal-gnss-esp32.bin'))?.bytes;
    if (!next || await digest(next) !== target.sha256) throw Error(
      'Application cible non vérifiée');
    const table = part(0x8000),
      boot = part(0);
    if (!table || !boot) throw Error('Segments de compatibilité absents');
    const write = plan(backup, current, next, table, boot);
    if (loader.chip?.CHIP_NAME !== 'ESP32-S3' || await loader.detectFlashSize() !== '16MB')
      throw Error('Puce ou taille Flash incompatible');
    async function check(address, size, expected) {
      // Bounded reads avoid the large-read stall observed with the S3 stub.
      try {
        for (let offset = 0; offset < size; offset += 16384) {
          const count = Math.min(16384, size - offset),
            actual = await loader.readFlash(address + offset, count);
          try {
            requireEqual(actual, expected.subarray(offset, offset + count),
              'Les octets de la carte ne correspondent pas à la référence');
          } finally {
            actual.fill(0);
          }
        }
      } finally {
        expected.fill(0);
      }
    }
    // Live readback proves the backup belongs to the device immediately before writing.
    for (const [address, size] of [
        [0, boot.length],
        [0x8000, table.length],
        [write.nvs.offset, write.nvs.size],
        [write.ota.offset, write.ota.size],
        [write.offset, current.length]
      ]) {
      progress('Lecture de compatibilité USB…');
      await check(address, size, backup.slice(address, address + size));
    }
    if (!await confirmWrite()) throw Error('Mise à jour annulée avant écriture');
    progress('Écriture de l’application ; garder la carte alimentée…');
    await loader.writeFlash({
      fileArray: [{
        data: next,
        address: write.offset
      }],
      flashSize: 'keep',
      flashMode: 'keep',
      flashFreq: 'keep',
      eraseAll: false,
      compress: true
    });
    await check(write.offset, next.length, next.slice());
    for (const [address, size] of [
        [0, boot.length],
        [0x8000, table.length],
        [write.nvs.offset, write.nvs.size],
        [write.ota.offset, write.ota.size]
      ]) await check(address, size, backup.slice(address, address + size));
    return write;
  }
  globalThis.UGUpdate = {
    plan,
    run,
    baseline,
    restart,
    crcSequence
  };
})();
