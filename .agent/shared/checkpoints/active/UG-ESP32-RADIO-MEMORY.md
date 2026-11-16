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

Review internal permanent allocations and fragmentation from existing ELF and
measurements after the common-buffer review. Keep DMA/control/piles internal and
all guards unchanged. Do not shrink RTCM capacity or claim a PSRAM allocation is
safe without its owner/callback/ISR contract. The current compact-core host gain
is not proof of S3 SRAM savings or a fix for this radio finding.
Before any future physical trial: new authorization, exact app/board verification,
private backup, same channel 1 and existing keys/peers, no injected RTCM/application
TX, 1/2/3-client protocol, explicit timeout/stop rules, exact logical restoration.
Acceptance includes >=32768-byte largest block, service stability and measured
latencies; real GNSS/RTCM and endurance are separate campaigns. No C3 Relay work.
[Recorded baseline](../retained/UG-ESP32-QUALIFIED-BASELINE.md) prevents restoring an
obsolete image or treating an old live-test checkpoint as a current instruction.
