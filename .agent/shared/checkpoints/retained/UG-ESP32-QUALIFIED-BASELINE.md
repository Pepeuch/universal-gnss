# ESP32 qualified administration and memory baseline

Lifecycle: RETAINED
Status: IMPLEMENTED (bounded administration/repair snapshot, not complete ESP32 acceptance)
Scope: DEPLOYMENT / VALIDATION
Canonical tracking: [TODO](../../../../TODO.md); open deltas have separate active checkpoints.
Code/freeze commit: `e5cb849dd2e1830d98dfb5ec893af2648a91bd40` (51 files), previously normal-pushed and SHA-verified.
No new remote verification, hardware operation or firmware publication in this reconciliation.

## Architecture to retain

One future `esp32` distribution has `gateway`, GNSS-free `relay-ntrip`, and `rover`
profiles; gateway/relay share one gateway engine. `repeater-espnow` is a distinct
future feature. Shared UG CRC/core code is reused, not duplicated. The historical
overlay is transitional; [native migration](../../../../embedded/esp32/migration/README.md)
is not declared fully qualified. C3 Relay and LoRa are deferred by owner priority.

## Recorded physical baseline — not a fresh probe

Hardware: Waveshare ESP32-S3 W5500, 16 MiB Flash / 8 MiB PSRAM.
Host/interface/topology: recorded Linux container tests, USB Serial/JTAG maintenance,
Ethernet LAN administration; no local GNSS receiver. C3 Relay/C6 not involved.
Installed HTTP app SHA256:
`9df28836b0afd95f990160d051d7e46179423af880f9091fac2005ac6a3e4328`
(1485216 bytes), built from tree `f7614f0064bc0054824c090371492a802f249ca0`.
App-only at 0x20000; protected NVS/table/OTA comparisons passed. Credentials/TLS and
GNSS/NTRIP settings retained. Last recorded state: HTTP running, HTTPS disabled by
explicit variant, ESP-NOW off, existing pairing retained, NTRIP restored, USB free.
This is distinct from HTTPS being broken; no automatic plaintext fallback exists.
No checkpoint authorizes flashing an earlier image or restoring old pairing data.

## Reusable evidence

- [HTTPS repair](../../../../docs/analysis/ESP32_S3_HTTPS_REPAIR.md): identity_missing
  diagnosed separately from allocation/server errors; unique commissioning restored
  strict HTTPS, no generic embedded key/password or NVS erase. Origin of the initial
  identity loss was not proven. Later full-install identity recovery was separate.
- [Tabbed UI](../../../../embedded/esp32/WEBUI.md) and
  [session/NTRIP](../../../../docs/analysis/ESP32_WEBUI_SESSION_NTRIP.md): single login,
  six views, logout/reboot/restricted recovery, serialized polling, guarded secrets.
  Owner confirms HTTP navigation and password display; no new GNSS hardware proof.
- [Memory qualification](../../../../docs/analysis/ESP32_S3_MEMORY_QUALIFICATION.md),
  [satellite followup](../../../../docs/analysis/ESP32_S3_MEMORY_FOLLOWUP.md),
  [inactive radio](../../../../docs/analysis/ESP32_S3_ESPNOW_MEMORY.md): retain each
  exact A/B baseline; none substitutes for active-radio margin or real GNSS latency.
- [Freeze/latency](../../../../docs/analysis/ESP32_LOGIN_LATENCY.md): 68 sanitizer,
  23 JS, 11 CI policy tests; HTTP/HTTPS reproducible builds; KDF compatibility.
  Last final sample: internal free115095 B, largest34816 B, PSRAM8344068 B,
  failed allocations0. These are point measurements, not endurance minima guarantees.

## Open deltas / resume rule

Follow active login, radio, USB-pairing and NTRIP checkpoints. Historical 28 runtime/111 MAVROS
formatting diagnostics and legacy TLS-key scanner failures
are unrelated debts, not fixed by this snapshot. C3 Relay/LoRa remain deferred.
Do not promote host/simulated tests to RF/CCMP, live RTCM, RTK or field proof.
Private Flash/NVS/certificates/keys/tokens/raw traces stay outside Git. Never replay
commissioning/password-change helpers as a resumption step. Reverify board/image,
backup and explicit authorization before any later write. Evidence invalidates only
when affecting source/configuration/SDK/hardware/topology or contradictory results change.
