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
- UM982 / Unicore runtime validation through downstream MowgliNext field use
- generic NMEA `GGA fix_quality` mapping into normalized `rtk_mode` for
  runtime-only receivers

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

## v0.7 — Operator Experience

- keep Universal GNSS itself scoped to the module API, CLIs, portable runtime,
  and ROS2 package
- define stable hooks for downstream dashboards, onboarding flows, and field
  tools
- treat MowgliNext as the first practical downstream testbed rather than as a
  built-in GUI commitment

## v0.8 — Embedded / Gateway Layer

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
