# Web Flasher USB pairing and multiple-rover request

Lifecycle: ACTIVE
Status: PARTIAL
Scope: DEPLOYMENT (ESP32 pairing)
Validation: HARDWARE_REQUIRED for full USB/RF acceptance; portal/fanout software unfinished.
Canonical tracking: [TODO](../../../../TODO.md), alternate USB pairing and multi-peer open.
Code baseline: `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`.

## Current boundary

The embedded WebUI contains `UGRoverPairing.pair` in
`embedded/esp32/components/ug_web/assets/pairing.js`. It requires a secure browser
context; S3 HTTP on a LAN IP cannot provide Web Serial. Owner chose pairing from
the existing HTTPS Web Flasher, not a local PC proxy or a new page.
`ug_platform/src/ug_commission.cpp` and `include/ug_pairing_read.hpp` implement
read-only S3 USB identity discovery; nonce/window/expiry/refusal tests pass.
The portal currently uses USB for portable configuration, NOT complete two-board
pairing. A port chooser recognizing a C3 is not UGCFG firmware verification.
C3 UGCFG maintenance responds only in its bounded boot/open window; the authorized
C3-only reset resolved the earlier no-reply diagnosis. No further reset/flash now.

## Required delta / exact next step

Adapt the existing pairing engine to authenticated-by-physical-access S3 USB and
C3 UGCFG/1 in the existing HTTPS portal. Verify actual role/board/STA MAC, crossed
readbacks, shared channel and partial outcome; keep LMK only in memory and wipe it.
No key in browser storage/logs/tickets. Cancellation, detach, timeouts and one-sided
writes must report uncertainty, never successful two-board pairing.
Implement only after explicitly resuming this feature, separately from memory work.
The additionally requested encrypted MAC list/fanout and optional explicitly
unencrypted broadcast are NOT implemented: current backend reports `peer_limit:1`
and the stored pairing record contains one peer/key. Do not add decorative options.
No encrypted-broadcast claim; receiver admission and late-ACK/configuration fences
must remain intact. C3 Rover only, C3 Relay deferred. Radio/RF acceptance depends on
[the active memory finding](UG-ESP32-RADIO-MEMORY.md) and a separately authorized
physical S3/C3 test. Do not generate keys or associate devices during maintenance.
Detailed evidence: [pairing/auth](../../../../docs/analysis/ESP32_WEBUI_PAIRING_AUTH.md),
[HTTP portal boundary](../../../../docs/analysis/ESP32_HTTP_ADMIN_EXPERIMENT.md),
[protocol](../../../../embedded/esp32/PAIRING.md).
