# S3 active-radio contiguous-memory margin

Lifecycle: ACTIVE
Status: PARTIAL
Scope: DEPLOYMENT (ESP32 radio/network memory)
Validation: HARDWARE_REQUIRED for eventual active-radio acceptance; software work remains.
Canonical tracking: [TODO](../../../../TODO.md), active-radio 32 KiB acceptance fails.
Code baseline: `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`.

## Established evidence

[Radio/HTTPS report](../../../../docs/analysis/ESP32_RADIO_HTTPS_COEXISTENCE.md) and
[HTTP experiment](../../../../docs/analysis/ESP32_HTTP_ADMIN_EXPERIMENT.md) distinguish
startup repair from load acceptance. S3 queue payloads in PSRAM restore HTTPS startup.
Under authorized channel-1 activation with existing pairing, largest free block fell
to 31744 B versus required 32768 B. Both HTTPS and HTTP load trials fail this criterion;
zero failed allocations does not make them pass. Later 1/2/3-client active stages
were not completed after the early stop. No application RTCM test stream was injected.
Earlier [inactive storage qualification](../../../../docs/analysis/ESP32_S3_ESPNOW_MEMORY.md)
measured +13760 internal bytes and a 34816-byte block with radio off; it does not
measure the activated Wi-Fi/worker cost. Its then-unpaired state is historical.
Current last-known pairing is preserved, radio disabled, NTRIP restored.

## Required delta / exact next step

Review the native routing candidate below, then its physical
qualification plan. Keep DMA/control/piles internal and all guards unchanged. Do not shrink RTCM capacity or claim a PSRAM allocation is
safe without its owner/callback/ISR contract. The current compact-core host gain
is not proof of S3 SRAM savings or a fix for this radio finding.
Before any future physical trial: new authorization, exact app/board verification,
private backup, same channel 1 and existing keys/peers, no injected RTCM/application
TX, 1/2/3-client protocol, explicit timeout/stop rules, exact logical restoration.
Acceptance includes >=32768-byte largest block, service stability and measured
latencies; real GNSS/RTCM and endurance are separate campaigns. No C3 Relay work.
[Recorded baseline](../retained/UG-ESP32-QUALIFIED-BASELINE.md) prevents restoring an
obsolete image or treating an old live-test checkpoint as a current instruction.

## Native S3 candidate — software evidence, not active-radio acceptance

Current work branch `perf/esp32-s3-native-buffers`, base
`19541b77f8d565d95bb40e250b5acdee798b3c4a` (PR51 test-only correction; its seven
workflows were verified successful). Commit/push for review is now authorized;
no flash or firmware publication is authorized for this memory phase.
[Candidate report and physical matrix](../../../../docs/analysis/ESP32_S3_NATIVE_BUFFERS.md),
[numerical provenance](../../../../docs/analysis/ESP32_S3_NATIVE_BUFFERS_MEASUREMENTS.json).
One explicit checked SPIRAM/8BIT boot block contains Gateway, ingress and owner
scratch frames. ELF DRAM BSS70608→42384 (−28224 B); requested PSRAM28240 B.
Controls/locks/piles/DMA stay internal; no shared UG, WebUI, NVS or C3-specific edit.
76 native sanitizer tests, 265 policy tests and two reproducible builds per S3
HTTP/HTTPS variant pass. The exact compiled-input tree is
`14d053ae0b36ec9b59044b56656fd4fa3a8eedbc`; no firmware installed. Host A/B ordinary
RAM overhead about +1% on small frames, not a PSRAM/radio timing proof.
The ESP-NOW bundle itself stays INTERNAL and keeps its existing lifecycle.

Next: review source/measurements, obtain separate application-only flash approval
for fixed baseline/candidate images, then execute the report's bounded read-only
radio-off campaign. Radio/publishers load needs a reviewed volatile test mechanism
or distinct authorization for configuration; current persistent APIs must not bypass
the no-NVS-change constraint. Existing 31744 B active failure and real RTCM/GNSS
acceptance remain OPEN. Do not repeat successful SDK builds merely after doc edits.
