# Roadmap

## Current phase

`v0.6.0` is released.

Universal GNSS is now in post-`v0.6.x` stabilization. The project is no longer
in a "next phase is basic ROS2 integration" posture, so the roadmap is grouped
by ownership:

1. Universal GNSS core stabilization
2. ROS2 package hardening
3. receiver-specific backend completion
4. downstream field validation and operator surfaces

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
- operator-driven Unicore reset/recovery persistent apply
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
- continue long-run and disconnect/restart operational validation

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
  boundaries, snapshots, fake-transport tests, and a native CLI. Real USB/UART
  lifecycle validation and configuration loading/projection remain.
- Phase 2 deterministic supervisor NTRIP/RTCM composition is complete; physical
  receiver/caster/reconnect/hotplug evidence remains pending.
- Phase A ROS2-first Docker evidence is established for Kilted and Lyrical
  amd64 build/runtime, shared runtime-library packaging, non-root 1000:1000,
  tini-managed clean shutdown, read-only external configuration, and same-host
  DDS over Docker bridge. Kilted and Lyrical arm64 BuildKit/QEMU packaging and
  smoke are green, but remain emulated evidence only. Live u-blox and Unicore
  GNSS/NTRIP/RTCM validation is green; the UM982 USB-loss contract requires
  container recreation and replay of its volatile runtime profile. The Docker
  healthcheck is intentionally process-only; receiver transport/freshness,
  NTRIP/RTCM/correction semantics, and RTK remain independent diagnostics.
  Native arm64, external-LAN/robot DDS, serial renumbering, MowgliNext, and the
  remaining receiver/topology matrices remain pending. External-LAN DDS is
  specifically `BLOCKED_BY_ENVIRONMENT / HARDWARE_OR_TOPOLOGY_REQUIRED`: the
  current workspace has only the same-host Docker bridge, so an independent LAN
  host is required for bidirectional Fast DDS and domain-isolation acceptance.
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

## Later embedded / gateway layer

- define the embedded/gateway cut for ESP32 or similar targets
- preserve a lightweight protocol/session subset for constrained builds
- add UART / WiFi / Ethernet adapters, MQTT export, and base-gateway policy

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
