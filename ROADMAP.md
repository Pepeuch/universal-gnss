# Roadmap

## Current phase

A single future `esp32` distribution is planned with compilation profiles
`gateway`, `relay-ntrip`, `rover` and, later, `repeater-espnow`. The BASE gateway
and GNSS-free NTRIP relay share one gateway engine; local GNSS is optional for
`gateway`. The [portable RTCM pilot and migration plan](embedded/esp32/README.md)
are implemented on `feat/esp32`. The [transitional application integration](embedded/esp32/integration/README.md)
now builds the real historical S3 Gateway/C3 Rover with UG CRC, with software
validation and two-build reproducibility; definitive profiles remain to migrate.
The overlay remains transitional; native service migration is described below.
No new hardware qualification is claimed.
[Native ESP32 reconstruction](embedded/esp32/migration/README.md) is now in progress:
UG receiver/routing/BASE plans, native NTRIP services, strict persistence and a
first HTTPS operator surface compile under IDF 6.1, including private NTRIP TLS
credential provisioning and native ESP-NOW replay diagnostics. The C3 relay uses
its declared one-publisher capacity without unused caster queues. Guarded receiver
telemetry/signals and fresh F9P Survey-In reports are available;
service supervision, degraded startup and bounded satellite-report diagnostics
are implemented in the native profiles.
Offline legacy JSON conversion now prepares a disabled, schema-validated candidate
with an explicit review report. Explicit previous-bank settings restore now validates
both banks and preserves admin identity and GNSS quarantine; it does not undo
receiver commands. Native Web resource admission distinguishes internal RAM from
PSRAM and retains compact diagnostics under memory pressure. A bounded typed
service event history is available without capturing raw SDK logs. Per-owner NTRIP
metrics and explicitly confirmed connection restart are exposed with stale-sample
protection. A bounded NTRIP transition history now retains typed client/publisher
connection, acceptance and failure outcomes with configuration/link provenance;
it stores no endpoint, credential or raw response. NTRIP endpoint forms now
configure the input or one supported publisher through the same strict settings
API, preserving hidden passwords unless replacement or erasure is explicit. The minimal C3 rover now includes USB and TX-only UART1 in the same generic
firmware, selected by persistent runtime configuration before radio startup.
A [strict anonymous portable configuration](embedded/esp32/PORTABLE_CONFIG.md)
now connects S3 WebUI export/preview/apply, C3 USB maintenance, Linux provisioning
and the Web Flasher. Only board-authorized GPIO/UART/model/channel fields are
portable; secrets, identities, endpoints and coordinates are absent. Local USB
restoration is software-tested; physical USB/wiring acceptance remains pending.
Native network budgets now account for NTRIP, caster, HTTPS and SDK resources;
S3 uses 20 sockets, C3 keeps 10, with compile-time rejection of insufficient pools.
A confirmed F9P/UM980/UM982 BASE disable operation now reuses a portable UG plan and
the native UART owner/journal; command acceptance remains distinct from physical stop.
A bounded, volatile GNSS command-result history now retains typed dispatch, response
and failure outcomes across observation resets, without raw payload capture.
An authenticated, explicitly confirmed offline self-test now exercises the real UG
CRC and shared wire codec in private buffers, without transmitting corrections.
Native wire helpers now omit routine per-fragment INFO console output while
retaining warnings and ESP-NOW startup diagnostics; rover logging stays disabled.
Native Unicore SIGNALGROUP mutation is now refused: it can persist and reset
automatically. Ordinary telemetry and F9P constellation requests remain available.
The native ESP32 CI recipe now validates four bounded variants with disconnected
non-root builds, host tests and independent reconstruction; GitHub qualification
is pending an authorized push/PR. No firmware is published.
Native ESP-NOW now defaults to unpaired/off and stores an explicit peer/channel/
private LMK at runtime. S3 pairing uses an authenticated 120-second manual window;
C3 uses nonce-checked boot USB maintenance, separated from its binary data plane.
Settings rollback preserves the current radio identity. Software tests/builds pass;
CCMP/RF, USB lifecycle and board wiring remain hardware acceptance tasks. See the
[pairing and future ROS2 control contract](embedded/esp32/PAIRING.md).
Remaining useful legacy features, device recovery,
OTA/runtime qualification and final qualification remain open. LoRa is deferred
by maintainer priority; ESP-NOW comes first. This is not a completed firmware claim.
Nominal historical ESP-NOW validation is accepted for development; further
endurance and range tests belong to field validation. Existing release versions,
historical checklists and completion metrics are unchanged.

The [UG Build & Flash portal source](docs/flasher.html) now uses bounded generic
S3 Gateway and C3 USB/UART Rover profiles. Runtime pairing is separate from ROM
identity. Local packaging verifies actual IDF offsets, partitions and qualified
binary hashes; the browser freezes verified bytes before offering installation.
A [manual alpha publication pipeline](docs/flasher/ALPHA.md) now couples two real
builds, a generated nonempty catalogue, immutable prerelease assets and Pages
verification. Its main-only dispatch is the publication authority; no second
manual register approval is required. Public deployment remains pending main
integration and the maintainer's dispatch; no published firmware is claimed.
S3 function switches remain configurable without connected GNSS hardware, while
requested activation and observed service state stay distinct. The first
[S3 hardware trial](embedded/esp32/HARDWARE_ALPHA.md) verified corrected boot,
W5500/Ethernet, local HTTPS and WebUI caster switching without a GNSS receiver.
GNSS/RF/RTK and field endurance remain unqualified. MQTT is not yet implemented
and LoRa remains deferred. Configuration-preserving updates are not
offered. See the [portal procedure and limits](docs/flasher/README.md).
Existing UG release/backlog completion metrics are unaffected.

The [native tabbed ESP32 WebUI](embedded/esp32/WEBUI.md) now preserves the existing
forms in six responsive views, with serialized API requests, active-view polling
and background suspension. Physical S3 validation now covers six tabs, sessions,
read-only forms, desktop/mobile use and HTTPS after reboot. The first
[S3 memory qualification](docs/analysis/ESP32_S3_MEMORY_QUALIFICATION.md) compares
four firmware variants and moves the USB line buffer and cJSON allocations to
PSRAM, with allocation-failure and repeated WebUI-cycle tests. Internal free
memory increases, but the largest block remains smaller and memory pressure
remains reduced. The [satellite-cache followup](docs/analysis/ESP32_S3_MEMORY_FOLLOWUP.md)
now qualifies S3-only owned PSRAM storage: 11860 additional internal bytes at the
first HTTPS GET and a larger contiguous block, preserving the shared snapshot.
The [S3 radio-storage phase](docs/analysis/ESP32_S3_ESPNOW_MEMORY.md) qualifies
conditional internal allocation with radio disabled: 13760 additional free bytes
at the first HTTPS GET, largest block retained at 34816 bytes, and simulated
lifecycle/failure/late-ACK tests. Active-radio memory qualification is deferred
until a real pairing exists; the idle saving is not an active-radio saving.
Long-duration and live GNSS/RTCM latency qualification, further S3 optimization
and shared-core optimization remain pending. The C3 relay is
deferred and does not constrain the S3. Existing Rover profiles remain unchanged.
No release metric changes.
The [WebUI pairing/authentication work](docs/analysis/ESP32_WEBUI_PAIRING_AUTH.md)
adds one-button USB C3 Rover pairing and restricted initial/password-migration
sessions on a separate feature branch. Software checks and physical acceptance
are tracked separately; hardware pairing and acceptable S3 password latency remain
pending; the requested account is verified on hardware. No release gate or hardware milestone is marked complete by this work.

Current prerelease: **`v0.7.4-rc6`** at
`d02e23f17d34e58f75f4c7a7f72f8d34ea766bfa`.

The maintainer considers **v0.7.4 functionally stable and validated**.
Publication of tags, images and a GitHub Release is separate: no exact stable
tag or Release was observed at the audit/synchronization baseline
`ba7f220da30ee0b216f199ab24947db0678b3d7b`. The
[version policy](docs/releases/versioning.md) separates functional versions,
engine/distribution qualification and publication. Historical
[release notes](docs/releases/v0.7.4.md) and
[publication preparation](docs/releases/v0.7.4-publication.md) retain their context.

Repository refactoring follows its own [plan](TODO_REPO_REFACTOR.md), independent
of these functional release milestones. [Contributions](CONTRIBUTING.md) target
`dev`; only validated changes pass into the complete stable `main`. Future
distribution branches are generated exclusively from `main` and receive no
manual development or contribution PRs. Rearrangement and generation do not
automatically trigger a functional release. No existing checklist completion
or historical validation evidence changes with this policy.

The project is no longer
in a "next phase is basic ROS2 integration" posture, so the roadmap is grouped
by ownership:

1. Universal GNSS core stabilization
2. ROS2 package hardening
3. receiver-specific backend completion
4. downstream field validation and operator surfaces

The [2026-11-08 rc6 mower report](docs/validation/2026-11-08-rc6-passive-mowing.md)
adds bounded native Lyrical/arm64 field evidence. The canonical v0.7 checklist
remains 61/65; this session is not an incarnation-cutoff or persistence test.
Previously reported endurance use is accepted by the maintainer as field-use
feedback, without an automatic new endurance run. Its documentary closeout is
kept distinct from the measurements acquired in this session.

Already delivered in `v0.6.0` and its follow-up fixes:

- portable runtime state, aggregation, diagnostics, and health summary
- NMEA, UBX/u-blox, Unicore ASCII, Unicore binary `N4`, and RTCM3 current-scope
  support
- receiver sessions, drivers, guarded command/config plumbing, and
  discovery-aware plan/apply CLIs
- auto-discovery v2 with stable `/dev/serial/by-id` preference plus
  `ReceiverNode` integration
- `ReceiverNode`, `NtripNode`, and `ReplayNode`
- live RTCM forwarding and receiver-side correction diagnostics
- portable RTCM MSM header/correction-stream summary observability
- ROS2 RTCM semantic diagnostics projection for base-station ARP, `1230`, and
  MSM summary/per-message activity
- parser counters plus malformed/rejected diagnostic visibility
- portable receiver profile surface:
  - `runtime_only`
  - `rover_high_precision`
  - `rover_high_precision_debug`
  - `factory_reset`
- distinct operator-driven Unicore persistent apply and destructive
  factory-reset/recovery apply
- u-blox persistent FLASH configuration and output-port selection
- model-aware Unicore signal-group planning/profile selection with safe
  unknown-model fallback and documented UM982 baseline gating
- Unicore binary `N4` regression coverage for valid unknown-frame accounting,
  malformed/rejected decode handling, and ASCII/Binary portable-field
  consistency on shared `PVTSLN*` mappings
- UM982 / Unicore runtime validation through downstream MowgliNext field use
- generic NMEA `GGA fix_quality` mapping into normalized `rtk_mode` for
  runtime-only receivers
- additive portable dual-antenna baseline capability/runtime/ROS2 surface with
  `v0.6.x` compatibility for `heading_deg` / `dual_antenna_heading`

Validation boundary:

- MowgliNext is a downstream field-validation environment for real operator
  workflows
- downstream GUI/install issues are not Universal GNSS core roadmap items
  unless they expose a missing portable feature or a bug in this repository

## v0.6.x — Stabilization

### Universal GNSS core

- runtime arbitration
- safe rollback
- receiver metadata
- finish the post-`v0.6.x` deprecation plan for compatibility-only heading
  fields now that canonical baseline fields exist
- RTCM observation-level decode beyond MSM summary
- RTCM semantic expansion beyond `1005` / `1006` / `1230` / MSM summary
- remaining Generic NMEA completion:
  - VTG/ZDA portable runtime contracts
  - runtime_only/write-side boundary documentation

### Runtime observability

- planner/report
- diagnostics
- operator metadata
- ROS2 reporting

### ROS2 package

- extend planner/report information into ROS2 surfaces
- review operator observability for status, correction, discovery, and parser
  data, including Foxglove-style consumers
- add CI and distro/arch validation for the integrated stack
- continue long-run and qualified retired-incarnation rejection validation;
  receiver-child recovery is proven physically and deterministic Docker DNS/NTRIP
  alias loss and recovery is covered in image CI

### Receiver-specific backends

- complete portable u-blox reset/base workflow coverage
- continue Unicore semantic/config growth only where it remains portable
- keep future Quectel work as a dedicated backend, not as generic-NMEA scope

## v0.7 — ROS2-first production containerization

The deployment work was investigated slightly ahead of schedule because the
BlueOS study exposed generic Universal GNSS needs. Priority remains current
Universal GNSS/MowgliNext downstream validation, then the portable runtime—not
BlueOS-specific implementation.

- `universal_gnss_supervisor` Phase 1 is implemented for one explicitly chosen
  serial receiver: lifecycle/session ownership, bounded reconnect, incarnation
  boundaries, snapshots, fake-transport and silent-PTY lifecycle tests, and a
  native CLI. Real USB/UART
  lifecycle validation and configuration loading/projection remain.
- Phase 2 deterministic supervisor NTRIP/RTCM composition is complete, including
  a bounded TLS setup deadline and cancellation of a stalled handshake on Stop;
  physical receiver/caster/reconnect/hotplug evidence remains pending.
- Phase A ROS2-first Docker evidence is established for Kilted and Lyrical
  amd64 build/runtime, shared runtime-library packaging, non-root 1000:1000,
  tini-managed clean shutdown, read-only external configuration, and same-host
  DDS over Docker bridge. Kilted and Lyrical arm64 BuildKit/QEMU packaging and
  smoke are green, but remain emulated evidence only. Live u-blox and Unicore
  GNSS/NTRIP/RTCM validation is green; the UM982 USB-loss contract requires
  container recreation and replay of its volatile runtime profile. The Docker
  healthcheck proves bounded responsiveness of both enabled nodes; receiver transport/freshness,
  NTRIP/RTCM/correction semantics, and RTK remain independent diagnostics.
  Native Kilted arm64 build/runtime is green on a physical Raspberry Pi,
  including the bounded no-device lifecycle and a least-privilege live UM982
  serial path; Lyrical-native and caster/network coverage were not inferred.
  External-LAN DDS is validated between the robot and second RPi: unmodified
  default-bridge multicast discovery failed, while the single minimal
  bridge/unicast alternative proved discovery and payload in both directions
  plus different-domain non-delivery. Host networking was not used and domain
  IDs are not a security boundary. Serial renumbering, MowgliNext, and the
  remaining receiver/topology matrices remain pending.
  The exact current image is also validated independently on the real robot with
  a sole u-blox device grant, live fix, protected runtime-only NTRIP, valid RTCM
  forwarding/correction health, clean SIGINT stop, and exact restoration of the
  legacy GPS container. The 2026-09-07 mower bag then records a natural
  Float-to-Fixed transition on that current Kilted arm64 sidecar with advancing,
  fresh observations and continuously healthy NTRIP/RTCM correction context.
  The same bounded closeout campaign proves receiver-child recovery with fresh
  process/source state plus both 7 Hz receiver / 20 Hz publication and 7 Hz
  receiver / 1 Hz publication mismatch contracts without stale promotion,
  fabricated position observations, or unbounded backlog.
- Release identity is available through standard OCI labels and the existing
  ROS diagnostic/snapshot surface: image build inputs supply version, revision,
  and deterministic source-commit creation time; runtime identity reports those
  values with ROS distro and configured receiver family without redefining
  health or introducing an API.
- then reuse the ROS-independent portable runtime/supervisor for a native,
  headless standalone image, without a ROS2 or GUI requirement.
- the following BlueOS integration milestone reuses that same generic
  container/runtime contract; it does not displace ROS2 Docker priority.
- add the generic HTTP API and independent Tailwind WebUI afterward as reusable
  presentation/control layers; neither is a prerequisite for Docker or initial
  BlueOS skeleton/packaging work.

## v0.8 — BlueOS deployment integration

BlueOS is the next integration milestone: it reuses the generic production
container/runtime contract and never owns an independent parser, receiver,
runtime, correction, or configuration model. Generic API/WebUI integration is
a later BlueOS phase.
The compatibility study, architecture proposal, Bazaar metadata template,
receiver permission template, and device/hotplug risk analysis are complete
evidence; see [`blueos/README.md`](blueos/README.md).

- Phase 0: validate device permissions, `HostConfig.Devices`, persistent userdata,
  and supported target architectures on real BlueOS.
- Phase 1: reuse the generic supervisor/runtime container contract for
  skeleton/packaging, configure one selected receiver, expose generic status,
  and connect lifecycle/restart; API/WebUI integration is later and BlueOS may
  initially be headless.
- Phase 2: use the generic supervisor's NTRIP/RTCM orchestration.
- Phase 3: add BlueOS `register_service`, relative-path-safe GUI exposure, and
  settings/apply/restart integration.
- Phase 4: publish multi-architecture SemVer images with logos and Bazaar metadata,
  then validate install/update/rollback on real hardware.

USB hotplug/re-enumeration plus Docker device grants are a hardware-required
boundary: reopening a tty does not prove a valid new BlueOS device grant.

## Functional priority — embedded / gateway integration (no assigned version)

Future ESP32 **BASE / RELAY / ROVER** and **ESP-NOW LR** integration is a
functional priority, separate from repository refactoring and publication.
It does not depend on assigning a new UG version merely to rearrange files.
Linux standalone, BlueOS and ROS2 Humble work remains planned with its own
technical prerequisites; their tasks and existing release-gate evidence are
preserved. No implementation or support claim is made here.

- define the roles, ESP-NOW LR transport contract and embedded/gateway cut for ESP32
- audit the prototype/donor before reuse, as described in the [plan](TODO_REPO_REFACTOR.md)
- preserve a lightweight shared protocol/session subset for constrained builds
- add UART / WiFi / Ethernet adapters, MQTT export, and base-gateway policy
- qualify role-specific RTCM forwarding and common GNSS semantics without duplication

## v0.9 — Receiver Ecosystem

- Quectel
- Septentrio
- Hemisphere
- Trimble
- NovAtel

## Later vendor expansion

### Quectel

- dedicated framing/parsing audit
- dedicated session/profile/config foundation
- RTK/runtime mapping only where documented and portable

### Septentrio

- SBF audit
- dedicated session/profile foundation
- runtime/status/satellite coverage for current portable needs

## Historical milestones already delivered

- `v0.1` portable core and diagnostics foundation
- `v0.2` current-scope receiver protocol coverage
- `v0.3` sessions, drivers, transport, and configuration plumbing
- `v0.4` NTRIP, tools, and low-level validation
- `v0.5` ROS2 integration, discovery hardening, and first hardware validation
- `v0.6.0` operational bringup release
