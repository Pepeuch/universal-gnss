# Generic ESP32 engineering knowledge preservation
Lifecycle: RETAINED
Status: IMPLEMENTED
Scope: DOCUMENTATION / VALIDATION

Preservation only is implemented; the underlying resource-lifecycle exercise is
PARTIAL. No independent repository, extraction or ArduPilot/DroneCAN port is made.
The proposed15-category engineering classification is deferred at user request.

## Sources and traceability

- Main versioned report: [resource lifecycle](../../../../docs/analysis/ESP32_RESOURCE_LIFECYCLE.md).
  Includes numerical comparisons, rejected strategies, exact source trees and
  the read-only Astra HTTPS study. Existing
  [architecture audit](../../../../docs/analysis/ESP32_ARCHITECTURE_RESOURCE_AUDIT.md)
  remains the ownership reference. Source and regression tests are authoritative.
- Local, deliberately unversioned copies: `ESP/analyses/` contains the campaign
  report, HTTPS and W5500 TOE read-only notes and the pre-existing memory audit.
  These copies must not be silently included in commits. They are not updated
  automatically and do not supersede source reports.
- Private evidence preservation uses a local pointer
  `.agent/checkpoints/ESP32-ENGINEERING-ARCHIVE`. Its private archive and SHA256
  inventory preserve scripts, raw observations, SDK configs, compilation records,
  normal/fault/comparator BIN/ELF/MAP and test logs. They are not Git content.
  Original private backup/certificate/credential storage remains separate.
  Local checkpoint names locate evidence without publishing private paths.
- Immutable final software tree:`e31e2d96daf7e3c152826fbd5cb4807087a1c5b7`;
  physical UART comparator:`70af63288a2c215f30cf269b2a9590cf57d27e9b`;
  CPU/radio comparator:`1d87ebad932bfd7238f6ea8b9943ece7318e03f6`.
- SDK image:`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`;
  IDF6.1 source:`fff9895c82d744c7237be8847347bdd1b07c6643`;
  qualified Xtensa toolchain:`esp-15.2.0_20251204`.

## Reusable knowledge, with evidence class

**PHYSICAL:** cold-task exception recovery on actual application OOM, cleanup/retry;
radio payload PSRAM paired heap savings; CPU/Wi-Fi/external-stack comparators;
UART Flash-ISR FIFO failure and IRAM81-write correction; nominal902/902 USB RTCM
and60/60 HTTPS coexistence. Every result is bounded to its exact configuration.

**SOFTWARE:**112 native sanitizer tests, full pinned-SDK Python/JS/QEMU cold-task
and independent four-segment reproduction. Native tests cover allocation failure,
ownership/retirement, worker lifecycle and radio policy. SDK TLSF/provenance and
zeroization mechanisms are inspectable in committed code, not a new allocator fork.

**HYPOTHESES:** ambient RF cause of intermittent MAC failures; exact Flash erase/GC
cause during correlated NVS set; server-stack external placement and on-demand
HTTPS savings; TOE SRAM/CPU savings. Do not describe these as measured benefits.

**PENDING:** strict100 transitions (42 then radio failure), cancellation/bursts,
final-source hardware OOM replay, long endurance, actual GNSS/RTCM field continuity,
bidirectional/STA/AP qualification under reduced buffers. Actual GNSS nav rate is
not measured. Future shared-core/Rust/TOE/HTTPS redesigns remain outside this exercise.

**REJECT / NOT SELECTED:** fixed4096-allocation registry; CPU B/C defaults; counting
DMA twice; counting MbedTLS PSRAM as new; summing relocation/removal of the same
HTTPS stack. External I/O stacks remain disabled because plain-TCP proof does
not qualify transitive TLS/Flash/ROM/DMA accesses. Failed measurements are retained.

## Future reuse conditions

Before public extraction, review licences, strip UG-specific code/contracts and
all private paths, addresses, captures, coordinates or credentials from raw material.
Never copy flash/NVS backups, keys, certificates or authentication information.
Requalify architecture/SDK-sensitive claims after toolchain, SDK, board, placement,
workload or radio-topology changes. Do not generalize the Xtensa task-entry defect
or GPIO/partition assumptions to ArduPilot/DroneCAN devices.

Next action: preserve the archive pointer with the workspace and use the active
resource checkpoint for tomorrow's continuation. Build the independent engineering
repository only on a future explicit request.

Saved implementation commit: `b8a8d57f7f120522fb3b999b517dfc6e97e51bbc` (caster dependency `1c1dc9f71fd8da3ebf853c7fc9bf1491ab26197f`).
