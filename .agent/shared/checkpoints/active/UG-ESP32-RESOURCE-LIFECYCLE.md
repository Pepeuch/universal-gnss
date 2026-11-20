# ESP32 resource lifecycle — pause handoff
Lifecycle: ACTIVE
Status: PARTIAL
Scope: DEPLOYMENT / VALIDATION
Validation: HARDWARE_REQUIRED

Branch: `perf/esp32-resource-lifecycle`; starting commit
`c48f06f2de38991b4c42eb807c9286001123bd93`. The maintainer authorizes checkpoint
commits and branch push for this pause, without main merge or publication.
[Durable results](../../../../docs/analysis/ESP32_RESOURCE_LIFECYCLE.md) and TODO
remain authoritative. The generic engineering checkpoint indexes reusable evidence.

## Established decisions and evidence

- Exact router withdrawal acknowledgement before reset/release; on-demand PSRAM
  owners; cooperative internal12 KiB I/O plus exclusive internal10 KiB helper.
  Private1 MiB SDK TLSF PSRAM arena: internal synchronization, address provenance,
  exact-size erasure and no SRAM fallback. REJECT old4096-allocation registry;
  legitimate60-second monitor history alone can exceed it. Monitor not rewritten.
- Current Xtensa task-entry sentinel ends exception search before its handler.
  Called noinline/non-tail recovery is demonstrated at cold task start and on the
  real64 KiB arena input path: owner freed, retry and41 fresh frames, no reboot.
  Subsequent final-source OOM hardware replay remains pending. AGENTS records the
  scoped SDK/toolchain rule; do not assume a universal architecture limitation.
- Authorized common caster Basic-prefix/socket-admission fixes: Linux70/70.
  PMK derives from existing LMK; the targeted C3 Rover fix preserves association.
  No C3 Relay work. No shared GNSS/monitor rewrite.
- Matched radio PSRAM payload saves13624 B post-window internal heap and improves
  largest block4096 B, costs13828 B PSRAM. Internal DMA overlaps internal heap.
  CPU A retained; B fails a loaded radio window; C passes integrity but has3.2 s
  latency/CPU0 89%. Wi-Fi B retains AMPDU/dynamic maxima32 while static RX6 saves
  measured memory. B2/AMPDU-off show no meaningful extra permanent gain. External
  I/O-stack comparator stays OFF; only plain TCP is qualified.
- UART Flash-ISR fault reproduced as FIFO overflow during NVS set activity.
  IRAM variant passes81 writes; IRAM text cost2560 B, measured internal heap cost
 9836 B versus paired baseline includes driver placement. No double-counted DRAM.
- Final software tree `e31e2d96daf7e3c152826fbd5cb4807087a1c5b7`:112 native
  ASan/UBSan, Python/JS, ten SDK/QEMU starts and four-segment reproduction PASS.
  Other recorded suites:309 generator/CI,44 repository/policy,16 native build-policy.
  Promoted S3 defaults: UART ISR IRAM, radio payload PSRAM, Wi-Fi B; CPU automatic.
  Normal final application SHA256
  `05ac13f69abd9200748f92fc305e226f8f34afdad71e6d5d524590af0c42cdbc` is NOT installed.

## Physical pause state

Installed NORMAL tree `70af63288a2c215f30cf269b2a9590cf57d27e9b`, IRAM variant,
CPU A / radio PSRAM / Wi-Fi B, app SHA256
`f0fa1fe25cbafeb6e6984a3d3fd48c543b4327d94fa2159863502d384c5180f6`.
Nominal seven endpoints,20 synthetic RTCM frames/s,1/2/3 HTTPS:902/902 C3 frames,
60/60 requests, no UART/MAC/queue errors; minimum126667 B internal /34816 B largest.
Existing96/32 KiB guards met; indicative150 KiB loaded target NOT met.

Strict100-transition series INCOMPLETE:42 proven transitions then3 MAC failures
(63/66 C3), UART clean. Same-image no-reset input-only recovery11/11 is separate
bounded evidence, not resolution. Interference is a hypothesis, not proven cause.
Original logical user network restored; TLS/admin/GNSS/association preserved.
Radio remains enabled/channel1 as originally configured, seven network endpoints
are disabled. Source and benches stopped, both USB ports free. NVS journal is
not promised byte-identical. GNSS pins/baud/rate unchanged; actual rate not read.

## Exact next step after resume

Read local `.agent/checkpoints/ESP32-RESOURCE-LIFECYCLE.md`, verify Git and device
identity/configuration without serial concurrency, and investigate the preserved
activation22 MAC-failure window before repeating the strict transition scenario.
Reuse passing tests. Execute prepared header/TLS-helper cancellation and burst
benches only after resolving that failure; replay final-source fault recovery and
restore normal image when appropriate. No final acceptance until required cases pass.

TLS lifecycle, TOE, Rust and authentication latency remain separate studies.
No hosted CI polling (maintainer owns it). No secrets/raw coordinates/captures or
backups in Git. Pre-existing memory audit is now authorized as an unchanged versioned historical proof. Invalidate only
for changed source/SDK/flags/hardware/topology or contradictory measured evidence.

Saved implementation commit: `b8a8d57f7f120522fb3b999b517dfc6e97e51bbc` (caster dependency `1c1dc9f71fd8da3ebf853c7fc9bf1491ab26197f`).
