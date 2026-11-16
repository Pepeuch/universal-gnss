# Native ESP-NOW pairing and local USB control v1

Status: software implementation; physical S3/C3/CCMP/USB acceptance is pending.
One peer is supported per device. No C6, LoRa, RF discovery or remote association
protocol is enabled. A MAC read through local USB is device identification, not
an RF discovery result. No firmware variant contains a user's peer or channel.
[Portable configuration v1](PORTABLE_CONFIG.md) adds private-free CONFIG_GET/CHECK/APPLY
commands in this same maintenance phase; USB/UART output is selected before data starts.

## Authorization and persistence

The gateway defaults to radio **off**, with no peer/key fallback. A fixed channel
(1–11) and unicast STA MAC require a private shared 16-byte LMK. Both ends configure
SDK encrypted unicast and install that LMK; each boot creates its own random local
PMK. See the [IDF 6.1 security contract](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/network/esp_now.html#security).
The receiver additionally checks the source peer and its own unicast destination.
This is not a claim that CCMP/RF acceptance has been physically qualified.

S3 changes use the existing authenticated HTTPS administration interface. The
operator opens a 120-second local window and explicitly confirms association.
The secret is never returned by status/export APIs. Removal has its own explicit
confirmation. Generic Web JSON edits cannot bypass the pairing window; settings
rollback preserves current radio identity, as well as admin identity/quarantine.
Console provisioning remains a trusted local operation. No radio packet can write
configuration. A mode window does not broadcast an invitation to unknown peers.

The S3 stores settings through the existing validated dual-bank store. C3 stores
one atomic NVS blob (`ug_rover/pairing`, 32 bytes): magic/version, paired flag,
channel, peer, LMK and existing UG CRC24Q. CRC detects corruption, not malicious
flash modification. These keys are not protected against physical flash access
unless a separate qualified flash-encryption policy is added. Unknown versions,
bad CRC, I/O and readback failures lock writes/startup; no namespace/flash erase
is automatic. Old experimental plaintext-radio configurations are rejected rather
than silently treated as authenticated pairs; they need explicit local migration.

## C3 control channel and future ROS2 adapter

USB Serial/JTAG maintenance runs **before** Wi-Fi, ESP-NOW and RTCM output, including
when RTCM output is UART1. After an ordinary paired reset, the first 8 seconds are
silent. A local host can send `UGCFG/1 OPEN <16-hex-random-nonce>\n`; the control
response echoes that unpredictable nonce. A tool must validate it, preventing a
normal RTCM payload from impersonating an unsolicited configuration handshake.
An unpaired/locked device stays silent and available for local maintenance.

The explicit maintenance window lasts 120 seconds; another local OPEN renews it.
Commands are ASCII, LF terminated, at most 128 bytes, with exact single spaces:

```text
UGCFG/1 GET
UGCFG/1 SET <BASE_STA_MAC> <CHANNEL> <32_HEX_LMK> CONFIRM_PAIR
UGCFG/1 CLEAR CONFIRM_UNPAIR
UGCFG/1 RUN CONFIRM_RTCM
```

Replies are one `UGCFG/1 ` prefix followed by bounded JSON and LF. GET returns
identity, pairing state, channel, key presence and storage status, never the LMK.
The `ttl_ms` field reports the configured 120-second control-window limit, not
a continuously updated remaining time.
SET/CLEAR commit and verify storage before acknowledgement. RUN requires a valid
pair; it emits `{"mode":"closed"}` and drains control TX before the data plane
starts. At timeout a paired valid device does the same; an unpaired device returns
to silent local waiting. A failed drain does not start RTCM. No application
commands/diagnostics are emitted in normal USB/UART RTCM mode. Inputs in that mode
are not interpreted as control. ROM boot bytes remain a separate hardware concern.

A future ROS2 adapter must enter an **explicit maintenance phase**, stop forwarding
RTCM, arrange an operator-approved reset, validate the fresh nonce, and process
control replies separately. It must only resume its RTCM parser after the closed
marker. Reset/re-enumeration, Linux grants and actual USB delivery remain physical
acceptance tasks. This protocol does not offer live unsolicited diagnostics in a
binary stream, nor does it implement a ROS2 node or automatic reset here.

## Bouton WebUI « Appairer un Rover »

Dans Réseau & radio, connecter le **C3 Rover** en USB au PC affichant la WebUI
HTTPS du S3, puis cliquer sur le bouton. Chromium demande le port, jamais une MAC
ou une clé. L'origine HTTPS doit être de confiance et Web Serial autorisé ; les
navigateurs incompatibles affichent un diagnostic. Voir le
[contrat Web Serial Chromium](https://developer.chrome.com/docs/capabilities/serial/).
Le C3 Relay n'est pas concerné.

Le navigateur ouvre UGCFG/1 avec un nonce aléatoire. La réponse doit déclarer
`firmware=universal-gnss`, `board=esp32c3`, `role=rover`, `mode=control`, stockage
modifiable et le même nonce. Les anciens firmwares sans ces métadonnées doivent
être mis à jour explicitement. Aucun reset DTR/RTS automatique d'une carte encore
non identifiée n'est tenté : si sa maintenance est fermée, RESET physique puis
nouvel essai sont indiqués.

Le S3 fournit sa MAC STA via `/api/radio/pairing` authentifié. Le canal déjà
configuré (1–11 ; canal 1 pour la campagne actuelle) est conservé : aucun scan,
association au point d'accès ou choix automatique du canal. Après confirmation,
WebCrypto génère une LMK de 16 octets ; SET l'enregistre sur le C3, puis les API
existantes ouvrent la fenêtre S3 et enregistrent son pair. GET vérifie ensuite
MAC croisées, canal, présence de clé et état persistant. La clé elle-même n'est
jamais retournée. Elle ne passe ni dans un champ DOM, ni dans un fichier ticket,
ni dans localStorage/sessionStorage ; seules les requêtes autorisées la portent.
Les tableaux aléatoires sont effacés ; JavaScript ne garantit pas l'effacement
physique des chaînes transitoires par son ramasse-miettes.

RUN clôt et draine la maintenance C3. Une nouvelle commande S3 authentifiée
`POST /api/radio/pairing/restart` avec `RESTART_AFTER_PAIRING` planifie le reboot
uniquement avec une association enregistrée. Les nouvelles sessions Web étant
volatiles, il faut se reconnecter après reboot : la WebUI reprend alors la
vérification du pair/canal et du démarrage radio sans conserver le mot de passe.
La confirmation de configuration/démarrage ne démontre ni CCMP sur l'air, ni
réception RTCM/USB. Aucun flux synthétique n'est injecté par le bouton.

Une seule opération locale est admise à la fois. Le bouton devient Annuler ;
fermer la session/la page annule la suite et libère les streams USB. Timeout,
clé non confirmée, déconnexion et refus S3 ont des résultats distincts des
réussites. Les deux écritures **ne sont pas atomiques**. Après toute tentative
SET sans réponse, le C3 peut déjà avoir enregistré la clé ; après une tentative
S3, les deux états peuvent être incertains. Aucun rollback d'une ancienne clé
inconnue ni CLEAR destructif automatique n'est exécuté. Le résultat demande une
vérification et un nouvel appairage explicite ; une réussite n'est jamais affichée
sur la seule présence d'une configuration S3.

L'outil `tools/rover_config.py` reste une maintenance locale avancée ; la WebUI
ne demande plus ses tickets ou une copie manuelle de secrets.

Qualification de ce parcours et de la nouvelle authentification :
[rapport WebUI](../../docs/analysis/ESP32_WEBUI_PAIRING_AUTH.md).

S3 pair changes latch TX stopped and invalidate pending frames until reboot.
The receiver's new boot starts fresh reassembly state; configuration never edits
an active C3 codec. Already in-flight physical bytes cannot be recalled. Radio
configuration does not depend on Internet/NTRIP availability. No Wi-Fi STA join,
automatic channel selection or network/Zigbee configuration change is performed.
