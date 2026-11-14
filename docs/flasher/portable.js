/* SPDX-License-Identifier: LGPL-3.0-only
 * Shared portable-config v1 boundary. No network, storage, free-form text or IDs.
 */
(() => {
'use strict';
const models=['none','auto','NMEA','UM980','UM981','UM982','ZED-F9P'];
const keys=['format','schema','board','profile','ethernet','output','uart_tx','uart_rx','uart_baud','gnss_model','radio_channel'];
const pins={ 'waveshare-s3-eth':[1,2,15,16,17,18,38,39,40,41,42,43,44,47,48], 'supermini-c3':[4,5,6,7,10] };
function defaults(board) {
 if(!Object.prototype.hasOwnProperty.call(pins,board))throw Error('Carte non prise en charge');
 const s3=board==='waveshare-s3-eth';
 return {format:'ug-esp32-portable',schema:1,board,profile:s3?'gateway':'rover',ethernet:s3?'w5500-onboard':'none',output:s3?'gnss':'usb',uart_tx:-1,uart_rx:-1,uart_baud:115200,gnss_model:s3?'auto':'none',radio_channel:1};
}
function validate(value,board) {
 if(!value || typeof value!=='object' || Array.isArray(value) || Object.keys(value).sort().join(',')!==keys.slice().sort().join(','))throw Error('Champs inconnus ou manquants ; aucun secret ni identifiant autorisé');
 const d=defaults(value.board),s3=d.profile==='gateway';
 for(const key of ['format','schema','profile','ethernet'])if(value[key]!==d[key] || typeof value[key]!==typeof d[key])throw Error('Schéma, profil ou Ethernet incompatible');
 if(board && value.board!==board)throw Error('La configuration appartient à une autre carte');
 for(const key of ['uart_tx','uart_rx','uart_baud','radio_channel'])if(!Number.isSafeInteger(value[key]))throw Error('Entier strict requis');
 if(value.radio_channel<1 || value.radio_channel>11)throw Error('Canal radio invalide');
 if(s3) {
  if(value.output!=='gnss' || !models.slice(1).includes(value.gnss_model) || value.uart_baud<9600 || value.uart_baud>921600 ||
    !((value.uart_tx===-1 && value.uart_rx===-1) || (pins[value.board].includes(value.uart_tx) && pins[value.board].includes(value.uart_rx) && value.uart_tx!==value.uart_rx)))throw Error('UART GNSS ou conflit GPIO');
 } else if(value.gnss_model!=='none' || value.uart_rx!==-1 ||
   !(value.output==='usb' && value.uart_tx===-1 && value.uart_baud===115200 || value.output==='uart' && pins[value.board].includes(value.uart_tx) && [115200,230400,460800,921600].includes(value.uart_baud)))throw Error('Sortie RTCM incompatible');
 return Object.fromEntries(keys.map(key=>[key,value[key]]));
}
function parse(text,board) {
 if(typeof text!=='string' || text.length>512)throw Error('Fichier UG trop grand');
 // Mirror the firmware flat JSON grammar, including rejection of duplicates and escapes.
 const r=/[ \t\r\n]*(?:([{}:,])|"([A-Za-z0-9_-]*)"|(-?(?:0|[1-9][0-9]*)))[ \t\r\n]*/gy;
 let pos=0,seen=new Set(),value={},expect='{',key=null;
 const take=()=>{r.lastIndex=pos;const m=r.exec(text);if(!m)throw Error('JSON portable strict attendu');pos=r.lastIndex;return m;};
 let m=take();if(m[1]!=='{')throw Error('Objet UG requis');
 for(let i=0;i<keys.length;++i) {
  if(i && take()[1]!==',')throw Error('Séparateur JSON invalide');
  m=take();key=m[2];if(key===undefined || seen.has(key))throw Error('Champ dupliqué ou invalide');seen.add(key);
  if(take()[1]!==':')throw Error('Séparateur JSON invalide');m=take();
  if(m[2]!==undefined)value[key]=m[2];else if(m[3]!==undefined && m[3]!=='-0')value[key]=Number(m[3]);else throw Error('Valeur portable invalide');
 }
 if(take()[1]!=='}' || pos!==text.length)throw Error('Objet incomplet ou champs supplémentaires');
 return validate(value,board);
}
globalThis.UGPortable={defaults,validate,parse,pins,models};
})();
