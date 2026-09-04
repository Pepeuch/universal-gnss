# Universal GNSS — TODO

## Current status

`v0.6.0` is released.

Current phase: post-`v0.6.x` stabilization.

Next blocking milestones:

- **`v0.7.0` — Production containerization / deployment architecture**
- **`v0.8.0` — BlueOS extension and non-ROS deployment surface**

Broad feature expansion in the backlog is intentionally secondary until the
`v0.7.0` container deployment baseline is production-ready. BlueOS work starts
immediately after that baseline is stable, and must reuse the same Universal
GNSS core rather than fork GNSS/NTRIP/configuration logic into the extension.

Recently completed:

- `v0.1` to `v0.4` portable core, protocol, driver, transport, NTRIP, and tool foundations
- auto-discovery v2 with `gnss_discover` plus `ReceiverNode` wiring for
  `serial_device:=auto`, `serial_baud:=auto`, and `receiver_family:=auto`
- `ReceiverNode`, `NtripNode`, and `ReplayNode`
- `GnssStatus`, `NavSatFix`, and `diagnostic_msgs` projection
- live RTCM forwarding from `NtripNode` into `ReceiverNode`
- portable RTCM MSM correction-stream summary observability
- ROS2 RTCM semantic diagnostics projection for base-station ARP, `1230`, and
  MSM summary/per-message activity
- parser counters plus recent malformed/rejected diagnostic visibility in ROS2
- portable Auto Configuration planner/report/apply flow
- operator-driven runtime-only apply for u-blox and Unicore
- Unicore persistent / `factory_reset` reset-recovery apply
- u-blox persistent FLASH configuration and output-port selection
- model-aware Unicore signal-group planning/profile selection with safe
  unknown-model fallback and documented UM982 baseline gating
- Unicore binary `N4` regression coverage for valid unknown-frame accounting,
  malformed/rejected decode handling, and ASCII/Binary portable-field
  consistency on shared `PVTSLN*` mappings
- UM982 / Unicore runtime hardening and downstream field validation through
  MowgliNext
- generic NMEA `GGA fix_quality` runtime mapping into normalized `rtk_mode`
- NMEA framer lifetime/resynchronization hardening and finite-value rejection
  for NMEA and Unicore ASCII numeric fields
- decimal-degree latitude/longitude outputs preserve at least 9 decimal places
- additive public dual-antenna baseline capability/runtime/ROS2 surface with
  `v0.6.x` compatibility for `heading_deg` / `dual_antenna_heading`

## Native runtime, API, web, and deployment planning

This register is the authoritative planning state for deployment work identified
slightly ahead of schedule by the BlueOS compatibility study. It is intentionally
separate from the frozen UGA audit checklist/dashboard baseline. The generic
Universal GNSS runtime, API, GUI, and Docker layers are reusable components;
BlueOS is a deployment/integration adapter, not a separate GNSS implementation.
See [`blueos/README.md`](blueos/README.md) for the current BlueOS boundary and
hardware-risk analysis.

Project implementation accounting is separate from the 205-item UGA quality
dashboard: each TODO checklist item has equal weight, and only checked items
count as complete. `PARTIAL` and `BLOCKED` UG-PLAN phases are reported
separately and receive no fractional credit. The generated README dashboard is
the current numeric source of truth.

| ID | Status | Scope | Validation | Dependency | Current state / remaining work |
| --- | --- | --- | --- | --- | --- |
| `UG-PLAN-001` | PARTIAL | DEPLOYMENT | HARDWARE_PENDING | Complete current downstream/MowgliNext validation first | Native `universal_gnss_supervisor` Phase 1 is implemented: one explicit serial receiver, `ReceiverSession` / `ReceiverSessionRunner` orchestration, bounded reconnect/backoff, incarnation boundary, clean stop, runtime/metrics snapshot, fake-transport regression coverage, and native CLI. Remaining: real serial/USB/UART lifecycle validation and configuration loading/projection. It must remain non-ROS and non-BlueOS, use steady-clock liveness/backoff, and preserve timestamp/provenance semantics without carrying state across incarnations. |
| `UG-PLAN-002` | IMPLEMENTED | NTRIP / DEPLOYMENT | HARDWARE_PENDING | `UG-PLAN-001` | Deterministic Phase 2 is complete: shared transport-neutral `UGA009` `RtcmFrameWriter`; ROS2 `ReceiverNode` semantic-parity migration; native supervisor `NtripClient` orchestration; independent receiver/NTRIP reconnect; RTCM forwarding; GGA observation-sequence/cadence policy; stop cancellation; redacted credential/status behavior; 33/33 runtime/NTRIP/transport/driver CTests; ROS2-enabled build; `test_receiver_node`; formatting; and diff checks. Remaining physical evidence: receiver lifecycle, real caster/network reconnect, USB/hotplug/re-enumeration, and BlueOS device grants. |
| `UG-PLAN-003` | OPEN | DEPLOYMENT | — | `UG-PLAN-005` | Lightweight generic HTTP API above the production container/runtime contract: lifecycle/health, receiver identity and transport, normalized `GnssRuntimeState`, parser/session diagnostics, correction/RTK/NTRIP health, redacted configuration, and reconnect/incarnation data. Later endpoints validate and deliberately apply/restart configuration with existing receiver-configuration safeguards. HTTP handlers must not own GNSS logic; defer SSE/WebSocket selection until evidence justifies it. |
| `UG-PLAN-004` | OPEN | DEPLOYMENT | — | `UG-PLAN-003` | Generic Universal GNSS web GUI, served by or alongside the native API and usable from native Linux, standalone Docker, and BlueOS. Use responsive Tailwind CSS and the existing logo under `docs/`; keep it presentation/configuration-only. Plan Basic (connection/fix/RTK/position/accuracy/correction/NTRIP health), Advanced (satellites, C/N0, motion, UTC, RTCM, metrics, reconnect/configuration summary), and Expert (AGC, interference, raw diagnostics, transport, auto-configuration, logs, incarnation, advanced controls) views. |
| `UG-PLAN-005` | PARTIAL | DEPLOYMENT | HARDWARE_REQUIRED | `UG-PLAN-002` | Phase A evidence is now established for Kilted/Lyrical amd64 and BuildKit/QEMU arm64 packaging, non-root/tini lifecycle, external read-only config, same-host DDS, live u-blox/Unicore GNSS+NTRIP/RTCM, and the exact Unicore USB-loss contract. The current CH340/UM982 bench requires recreation after USB loss and runtime-profile replay after power loss; stable by-id returned unchanged in one non-renumbering trial. Remaining: serial-renumbering and other receiver/topology matrices, a qualified UGA-126 transport-incarnation cutoff, UGA-170's per-model u-blox reset matrix, native arm64/hardware, and robot/BlueOS validation. Phase B reuses the portable runtime/supervisor for a native/headless standalone UG image with no ROS2 or GUI requirement and the same configuration/device/persistence principles. Phase C enriches images with the generic API/WebUI once those layers exist. The WebUI must not create or validate the Docker baseline. |
| `UG-PLAN-006` | PARTIAL | BLUEOS | HARDWARE_REQUIRED | `UG-PLAN-005` | Completed evidence: compatibility study, architecture proposal, Bazaar metadata and minimal receiver-permission templates, packaging/architecture research, and identified device/hotplug risks. BlueOS skeleton/packaging may reuse the production container/runtime contract once it exists and may initially be headless/status-oriented; it must not displace ROS2 Docker priority or reimplement GNSS semantics. Later phases add API/WebUI integration, `register_service`, relative-path-safe GUI exposure, settings/apply/restart, multi-arch publication, Bazaar submission, and install/update/rollback validation. USB hotplug/re-enumeration and Docker grants require real BlueOS proof; a reopened tty is not proof of a valid new device grant. |

Priority and dependency order: complete deterministic `UG-PLAN-002`; establish
the ROS2-first production Docker baseline; reuse the portable runtime/supervisor
for native/headless standalone Docker where practical; reuse the same
container/runtime contract for BlueOS skeleton/packaging; then add the generic
HTTP API and independent Tailwind WebUI, integrating that WebUI later into
standalone, BlueOS, and optionally ROS2-facing deployments. The WebUI must not
block Docker, and BlueOS must not displace ROS2 Docker priority or drive duplicate
GNSS semantics.


## Release roadmap / blocking milestones

### v0.7.0 — Production containerization / deployment architecture

Release-scope accounting for `v0.6 -> v0.7` is exactly the checklist sections
below through Documentation. The later **Non-ROS control/status surface** and
the two Networking API-policy tasks, plus the `v0.8.0` BlueOS checklist, are
explicitly excluded: the roadmap schedules them after the ROS2-first Docker
baseline.

`v0.7.0` is a dedicated deployment release, not a minimal Docker wrapper.
The container must become a supported production execution environment for
Universal GNSS on development PCs, robots, embedded Linux systems, and future
integration platforms.

Architecture rules:

- Universal GNSS core behavior remains independent from Docker and ROS2.
- ROS2 is the primary first production deployment target; the generic runtime
  remains ROS-independent so native/headless reuse can follow.
- Deployment configuration, secrets, runtime data, and logs must remain outside
  the immutable image.
- Receiver health, correction health, solution quality, and container/process
  health must remain separate concepts.
- The deployment contract must be reusable by BlueOS without duplicating GNSS
  business logic in the BlueOS extension.
- Generic API/UI layers are later presentation/control surfaces and are not
  prerequisites for the production Docker baseline.

Container build and release:

- [x] production `Dockerfile` / container build layout
- [x] reproducible versioned image tags tied to Universal GNSS releases/commits
- [x] multi-stage builds with a minimal runtime image
- [x] `amd64` image validation
- [ ] `arm64` image validation (BuildKit/QEMU packaging and smoke are green; native runtime/hardware pending)
- [x] define whether `arm/v7` remains a supported target (not supported for v0.7)
- [ ] multi-architecture CI build/publish pipeline
- [x] Software Bill of Materials / image provenance strategy
- [x] documented image upgrade and rollback policy

Runtime / process lifecycle:

- [x] define single-container versus composable-service layout for Receiver,
  NTRIP, replay, and optional adapters
- [x] production entrypoint and deterministic startup ordering
- [x] graceful SIGTERM/SIGINT shutdown
- [ ] restart policy and crash-recovery behavior
- [ ] receiver-process restart without stale state resurrection
- [x] NTRIP reconnect/restart without stale source metadata leakage
- [ ] container restart with deterministic configuration reapplication
- [ ] expose running Universal GNSS version/commit in runtime diagnostics

Device access / hotplug:

- [x] serial passthrough using stable `/dev/serial/by-id/...` identities where
  available
- [x] document fallback behavior for platforms without `/dev/serial/by-id`
- [x] least-privilege serial permissions; avoid `--privileged` as the normal path
- [x] USB receiver disconnect/reconnect inside a running container
- [ ] USB serial renumbering validation
- [ ] F9P <-> UM982 physical swap/recovery validation
- [x] define udev/device-manager integration needed by production deployments

Hardware sweep (2026-09-04): the current UM982/CH340 bench proves the exact
stable-by-id mapping, explicit stale health on physical loss, required container
recreation, and runtime-profile replay after USB power loss. It does not prove
serial renumbering, a receiver-incarnation byte cutoff, automatic recovery, or
u-blox reset recovery; those remain separately tracked rather than being
implicitly closed by the successful replug trial.

Configuration / persistence / secrets:

- [x] stable external configuration directory and mount contract
- [ ] configuration schema/version migration policy
- [ ] persistent receiver/profile configuration state where required
- [x] read-only defaults plus writable operator overrides
- [x] NTRIP credentials supplied outside the image
- [x] secret-file / environment-variable policy with no credential leakage in
  logs or diagnostics
- [ ] persistent diagnostic/log/export directory
- [x] backup/restore expectations for deployment configuration

Health / observability:

- [ ] container healthcheck that validates service functionality rather than only
  PID/process existence
- [x] distinguish container/service health from receiver transport health
- [x] distinguish receiver observation freshness from ROS/publication activity
- [x] distinguish NTRIP transport, accepted response, correction flow, semantic
  correction health, and RTK solution quality
- [ ] healthcheck behavior when no receiver is intentionally configured
- [ ] structured logs suitable for Docker/Compose/BlueOS collection
- [x] bounded log retention guidance
- [ ] snapshot/support bundle export for field diagnostics

Networking:

- [x] NTRIP outbound networking validation
- [ ] Docker DNS/reconnect behavior validation
- [x] host/network-mode policy for tested same-host bridge topology
- [x] ROS2 DDS cross-container validation
- [x] ROS2 DDS host <-> container validation
- [x] ROS2 discovery behavior documented for tested same-host Docker deployments
- [ ] non-ROS status/configuration API contract for platform integrations
- [ ] authentication/bind-address policy for any exposed HTTP/WebSocket API

Non-ROS control/status surface:

- [ ] define a stable portable service API independent from ROS2
- [ ] receiver identity/model/firmware status
- [ ] normalized GNSS runtime status
- [ ] correction/NTRIP status
- [ ] diagnostics/health status
- [ ] configuration plan/report/apply operations
- [ ] safe restart/reconnect operations
- [ ] versioned API/schema compatibility policy
- [ ] ensure API state preserves observation provenance and source ownership

Deployment validation gates:

- [x] PC Docker validation with u-blox F9P
- [x] PC Docker validation with Unicore UM98x
- [ ] robot Docker validation
- [ ] long-run container validation
- [x] receiver silence and reconnect validation (current UM982 contract requires recreation)
- [x] NTRIP disconnect/reconnect validation
- [ ] container crash/restart validation
- [ ] host reboot/autostart validation
- [ ] low receiver rate / high publication-rate validation
- [ ] high receiver rate / low publication-rate validation
- [ ] RTK Float/Fixed transition validation through container boundaries (RTK Float is proven; RTK Fixed remains pending)
- [ ] no stale state survives source, receiver, process, or container incarnation
  changes

Documentation:

- [x] Docker quick-start
- [x] production deployment guide
- [x] serial/device permissions guide
- [x] Docker Compose example
- [x] ROS2 + Docker integration guide
- [x] troubleshooting / support-bundle guide
- [x] security and secret-management notes

v0.7 deployment reconciliation (2026-09-04), limited to the 65-task release
scope:

- **Actionable now:** release tagging/provenance/CI, lifecycle and persistent
  configuration policies, operational observability/logging/support guidance,
  Compose, and the remaining deterministic documentation work. The Docker
  quick-start, production deployment, serial/device permissions, and ROS2
  integration guidance are complete in [`docs/ros2_docker.md`](docs/ros2_docker.md).
- **HARDWARE_REQUIRED:** serial renumbering; F9P/UM982 swap and recovery;
  long-run, crash/restart, reboot/autostart, rate-mismatch, RTK Fixed, and
  source/incarnation validation; UGA-126 transport-incarnation cutoff; and
  UGA-170's receiver-model reset matrix. The proven UM982 USB-loss contract is
  documented there; it requires container recreation and profile replay.
- **Native-arm64-required:** native runtime and receiver/caster hardware; the
  existing BuildKit/QEMU package/smoke result is not native evidence.
- **BLOCKED_BY_ENVIRONMENT / HARDWARE_OR_TOPOLOGY_REQUIRED — external-LAN:**
  DDS discovery/data flow, topology/firewall policy, and domain isolation across
  physical LAN hosts. The current validation namespace (`172.17.0.6`) is on the
  same Docker bridge (`172.17.0.0/16`) and has no second physical LAN peer;
  same-host bridge evidence is separate and receives no release credit. Resume
  only with machine A running the default-bridge Dockerized ROS2 node and an
  independent physical-LAN machine B: prove B -> container and container -> B
  discovery/message flow on one explicit domain, then prove different-domain
  isolation while recording Fast DDS interface/discovery and firewall evidence.
  Do not use host networking unless a real failure justifies it.
- **MowgliNext-required:** robot and downstream deployment validation only;
  neither is exercised by this release-scope work.
- **Deferred beyond v0.7:** the non-ROS API/control surface, generic WebUI,
  BlueOS runtime/Bazaar work, and `arm/v7` support decision unless explicitly
  brought into a later release scope.

The Docker healthcheck intentionally proves only that launch-managed processes
are alive. Receiver transport, fresh observations, NTRIP connectivity, RTCM
flow/semantic health, and RTK state remain independent diagnostics and must
not be inferred from Docker health.


### v0.8.0 — BlueOS extension

`v0.8.0` follows the production container milestone directly. The BlueOS
extension must be a thin deployment/integration layer over the same Universal
GNSS container/core. Receiver parsing, NTRIP, configuration semantics,
provenance, correction health, and safety rules must remain owned by Universal
GNSS.

BlueOS packaging / lifecycle:

- [ ] define BlueOS extension architecture against the `v0.7.x` container contract
- [ ] extension metadata/manifest
- [ ] BlueOS-compatible image build and release pipeline
- [ ] `arm64` BlueOS target validation
- [ ] development/test path for `amd64`
- [ ] install/update/remove behavior
- [ ] persistent settings across extension upgrades
- [ ] restart/recovery behavior through the BlueOS extension lifecycle
- [ ] prepare extension-store/repository publication assets and documentation

BlueOS hardware integration:

- [ ] serial receiver discovery/access through BlueOS
- [ ] least-privilege hardware permissions
- [ ] receiver hotplug/reconnect
- [ ] F9P validation
- [ ] UM98x validation
- [ ] receiver swap validation without stale identity/configuration state

BlueOS configuration UI:

- [ ] receiver discovery and identity view
- [ ] receiver/profile configuration
- [ ] NTRIP endpoint/mountpoint configuration
- [ ] secure NTRIP credential entry/storage path
- [ ] rover/base/runtime profile controls where supported
- [ ] configuration plan/preview before apply
- [ ] explicit apply result/error reporting
- [ ] restart/reconnect controls where safe
- [ ] import/export deployment configuration

BlueOS monitoring UI:

- [ ] solution type / RTK Float/Fixed state
- [ ] observation age/freshness
- [ ] accuracy/DOP/satellite status where available
- [ ] correction transport/flow/semantic health displayed separately
- [ ] caster/source/station identity
- [ ] receiver identity/model/firmware
- [ ] parser/transport diagnostics
- [ ] container/service health
- [ ] downloadable support snapshot/log bundle

BlueOS integration API:

- [ ] consume the versioned non-ROS API introduced for `v0.7.0`
- [ ] no duplicated GNSS parser/configuration logic in the extension
- [ ] no dependency on ROS2 for normal BlueOS operation
- [ ] define optional ROS2 bridge enablement separately
- [ ] evaluate optional MAVLink integration/export as a separate adapter contract
  rather than coupling it to core GNSS behavior

BlueOS validation:

- [ ] clean install on supported BlueOS hardware
- [ ] upgrade from previous extension version
- [ ] receiver reconnect
- [ ] NTRIP reconnect
- [ ] BlueOS restart
- [ ] extension/container restart
- [ ] host reboot
- [ ] caster/mountpoint/source change
- [ ] F9P <-> UM98x swap
- [ ] long-run correction/RTK validation
- [ ] confirm no stale receiver/correction state survives lifecycle transitions

## Immediate correctness

- [ ] runtime arbitration between streaming traffic and config traffic
- [ ] production-safe failure handling and rollback expectations
- [x] live receiver identity / model / firmware metadata in discovery and planning output

## Universal GNSS core tasks

- [x] continue capability/profile consistency cleanup between built-in receiver profiles and driver support beyond the current Unicore model-aware signal-group gating
- [x] correction age estimation
- [x] runtime state CSV export
- [x] JSON schema stabilization/versioning
- [x] compare two receivers/logs
- [x] generate additional sanitized test logs
- [x] generic speed/course runtime contract for VTG
- [x] generic GNSS wall-clock/date runtime contract for ZDA
- [ ] finalize the deprecation/removal plan for compatibility fields
  `heading_deg`, `heading_accuracy_deg`, and `dual_antenna_heading` after
  `v0.6.x`
- [ ] extend RTCM semantic observations beyond `1005` / `1006` / `1230` / MSM summary
- [x] `gnss_replay` timing mode outside the ROS2 replay node
- [x] TLS support
- [x] client certificate authentication
- [x] custom CA certificate support
- [x] UDP transport
- [x] multi-caster support
- [x] local caster / base mode support

## ROS2 package tasks

- [x] expose receiver identity / firmware / model metadata
- [x] extend planner/report layer into ROS2
- [x] operator observability review
- [x] snapshot/export surface
- [x] ROS2 CI coverage
- [x] keep Kilted green
- [x] validate Lyrical
- [ ] validate Humble/Jazzy
- [ ] arm64 validation
- [ ] long-run validation
- [ ] receiver restart validation
- [x] GNSS reconnect validation
- [x] NTRIP reconnect validation
- [ ] USB serial renumbering validation
- [ ] F9P ↔ UM982 swap validation

## Receiver-specific backends

### u-blox

- [ ] complete portable factory_reset support
- [ ] survey-in support
- [ ] MON-SPAN

### Unicore

- [ ] broader Unicore binary `N4` semantic decode beyond `BESTNAVB` /  `PVTSLNB`
- [ ] raw observations
- [ ] conservative `AGCA` threshold interpretation if a safe generic policy emerges

### Quectel

- [ ] Quectel dedicated backend audit (`PQTM` / `PAIR`, parser, RTK mapping, config engine)
- [ ] runtime mapping
- [ ] keep Quectel work separate from generic NMEA unless the message is standard NMEA


### Septentrio

- [ ] Septentrio dedicated backend audit (`SBF`, PVT/status, satellite/RF, config/session)
- [ ] runtime/status
- [ ] configuration/session


## Downstream integration tasks

Downstream GUI, install, and operator-workflow issues should stay out of the
Universal GNSS core backlog unless they expose a missing portable feature or a
bug in this repository.

- [ ] Validate Universal GNSS -> `navsat_to_absolute_pose`
- [ ] Validate Universal GNSS -> `localization_monitor`
- [ ] Validate Universal GNSS -> Nav2
- [ ] Validate Universal GNSS -> mower bringup stack
- [ ] MowgliNext onboarding / GUI / operator workflow validation
- [ ] static accuracy validation
- [ ] short waypoint mission validation
- [ ] long waypoint mission validation
- [ ] full Nav2 mission validation
- [ ] verify localization stability during RTK Float/Fixed transitions

## Runtime quality

- [ ] GNSS confidence score
- [ ] RTK confidence score
- [ ] RF quality score
- [ ] correction quality score

## Documentation / Quality

- [x] repair `scripts/agent/checkpoint_audit.py` compact-manifest parsing and
  shared-checkpoint ownership detection; `--check` expands all 205 UGA records,
  treats only stable IDs in checkpoint filenames as ownership, and retains real
  duplicate detection
- [ ] contributor architecture guide
- [ ] parser writing guide
- [ ] sanitizer builds
- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] coverage report
