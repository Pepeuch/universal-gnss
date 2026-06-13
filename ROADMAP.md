# Roadmap

## Current release posture

Universal GNSS is no longer in the "next phase is ROS2" posture.

The project now has a working ROS2-integrated stack and is ready for a
defensible `v0.6.0` tag once release docs and test results stay synchronized.

That current `v0.6.0` posture covers:

- portable runtime state, aggregation, diagnostics, and health summary
- NMEA, UBX/u-blox, Unicore ASCII, Unicore binary `N4`, and RTCM3 protocol work
- receiver sessions, drivers, command/config plumbing, and response routing
- memory, POSIX serial, and TCP client transport
- TCP-backed NTRIP client foundations
- offline and live low-level tools for inspection, replay, reporting, export,
  config preview/plan/apply, serial monitoring, and NTRIP monitoring
- ROS2 `ReceiverNode` and `NtripNode`
- live RTCM forwarding and receiver-side correction diagnostics
- serial auto-discovery with stable `/dev/serial/by-id` preference
- score-based auto-discovery hardening for vendor detection and serial noise rejection
- Auto Configuration planner/report coverage
- guarded operator-driven runtime-only apply
- portable receiver profile surface:
  - `runtime_only`
  - `rover_high_precision`
  - `rover_high_precision_debug`
  - guarded `factory_reset`
- legacy alias compatibility for `rover` and `diagnostics`
- explicit confirmation gating for live writes
- persistent apply guarded out of the default workflow
- real F9P and UM982 runtime-only apply validation
- UM982 live apply timeout guidance around `5000 ms`
- real ZED-F9P and UM982 validation on ROS2 Kilted
- local NTRIP caster validation
- MowgliNext migration validation with fixes kept portable in Universal GNSS

The next major phase after `v0.6.0` is replay/observability and ROS2 packaging
hardening, not more foundational ROS2 or low-level churn.

## v0.1 — Portable Core And Protocol Base

Implemented:

- portable runtime state model
- capability/value flag model
- runtime aggregator
- portable diagnostics and health summary
- NMEA parser: `GGA`, `RMC`, `GSA`, `GSV`, `GST`, `VTG`, `ZDA`
- RTCM3 framing, CRC24Q, message typing, and correction monitor foundation
- synthetic testdata corpus
- CMake / CTest / CI foundation

## v0.2 — Receiver Protocol Coverage

Implemented:

- UBX parser: `NAV-PVT`, `NAV-DOP`, `NAV-SAT`, `NAV-STATUS`, `MON-HW`,
  `MON-HW2`, `MON-RF`, `RXM-RTCM`, `ACK/NAK`
- UBX `CFG-VALGET` / `CFG-VALSET` payload builders
- Unicore ASCII parser: `PVTSLNA`, `BESTNAVA`, `RTKSTATUSA`, `RTCMSTATUSA`,
  `SATSINFOA`, `BESTSATA`, `JAMSTATUSA`, `FREQJAMSTATUSA`, `HWSTATUSA`, `AGCA`
- Unicore binary `N4` framing plus `BESTNAVB` and `PVTSLNB`
- RTCM `1005` / `1006` base position decode

Still intentionally deferred inside protocol scope:

- broad RTCM MSM payload decode
- Unicore raw observations
- richer vendor-specific RF severity models

## v0.3 — Sessions, Drivers, Transport, And Configuration Plumbing

Implemented:

- `ByteSource` / `ByteSink` / `ByteDuplex`
- memory transport
- POSIX serial transport
- TCP client transport
- `NmeaSession`, `UbloxSession`, `UnicoreSession`
- `ReceiverSession` router and `ReceiverSessionRunner`
- `NmeaDriver`, `UbloxDriver`, `UnicoreDriver`
- receiver command model, dispatcher, transaction engine, response routers
- u-blox and Unicore config profile builders

Still intentionally deferred inside this layer:

- reconnect/session lifecycle ownership in drivers
- UDP transport
- TLS transport adapter

## v0.4 — NTRIP, Tools, And Low-Level Validation

Implemented:

- NTRIP config/request/auth foundations
- TCP-backed `NtripClient`
- RTCM stream extraction and correction-monitor integration
- GGA sentence builder / injector / policy / reconnect-backoff model
- sourcetable parser
- `rtcm_inspect`
- `gnss_inspect`
- `gnss_replay`
- `gnss_quality_report`
- `gnss_export` JSONL
- `gnss_profile_preview`
- `gnss_config_plan`
- `gnss_config_apply`
- `gnss_serial_monitor`
- `gnss_ntrip_monitor`
- full low-level readiness audit and runtime audit

Release verdict:

- ready for `v0.4`

## v0.5 — ROS2 Integration, Hardware Validation, And Discovery Hardening

Implemented:

- `GnssStatus` runtime adapter
- `NavSatFix` compatibility adapter
- `diagnostic_msgs` mapping helpers
- runtime-to-ROS2 mapping audit
- `ReceiverNode` with serial / TCP launch examples
- `NtripNode` with ROS2 launch example
- combined receiver + NTRIP launch example
- `robot_localization` example configuration and documentation
- live RTCM forwarding from `NtripNode` into `ReceiverNode`
- live u-blox `RXM-RTCM` acceptance validation through the ROS2 path
- live UM982 correction forwarding validation through the same ROS2 path
- portable serial receiver discovery foundation
- `gnss_discover` CLI
- stable `/dev/serial/by-id` preference and documentation
- optional onboard/platform UART discovery for embedded Linux targets
- `ReceiverNode` serial auto-discovery integration for `serial_device:=auto`,
  `serial_baud:=auto`, and `receiver_family:=auto`
- auto-discovery v2 scoring and rejection policy:
  - reliable u-blox detection
  - reliable Unicore detection
  - reliable generic NMEA detection
  - MAVLink heartbeat rejection
  - random serial text rejection
  - silent-port rejection
  - discovery score/reason diagnostics
- replay/tooling-backed regression coverage for discovery inputs
- local Kilted package build/test validation
- Kilted validation inside the local MowgliNext development image
- real ZED-F9P + local NTRIP caster smoke validation on ROS2 Kilted
- real UM982 ROS2 smoke validation on `/dev/ttyUSB0` at `921600`
- real `ReceiverNode` auto-discovery smoke validation on F9P and UM982 at
  `921600`
- MowgliNext integration validation, with bugs fixed in Universal GNSS rather
  than as Mowgli-specific workarounds

Phase verdict:

- `v0.5` scope is effectively complete
- next work should focus on operator workflows, replay bringup, and visualization

## v0.6 — Operational Bringup

Implemented and validated scope:

- Auto Configuration
  - `v0.6-1` design pass completed:
    - audited the existing config/profile, dry-run, and guarded-apply stack
    - defined a portable planner/validation/rollback-report layer above the
      existing builders and apply engine
    - identified base-role scope and persistence-semantics gaps
  - `v0.6-2` planner/report layer implemented:
    - added a driver-level `ReceiverAutoConfig` planner
    - wired `gnss_config_plan` to the portable planner/report layer
    - added portable warnings, rollback expectations, and production-readiness
      reporting
  - `v0.6-3` operator-driven apply flow implemented:
    - wired `gnss_config_apply` to the same portable planner/report object
    - added discovery-aware `--receiver auto` / `--family auto` / `--baud auto`
      apply targeting
    - required explicit operator confirmation for runtime-only live writes
    - kept persistent live apply guarded while still surfacing plan warnings and
      manual rollback expectations
    - exposed portable profiles as `runtime_only`, `rover_high_precision`,
      `rover_high_precision_debug`, and guarded `factory_reset`
    - kept legacy `rover` and `diagnostics` aliases accepted by the CLIs
  - `v0.6-4` runtime-only hardware validation completed:
    - confirmed runtime-only `rover_high_precision` apply on the F9P
    - confirmed runtime-only `rover_high_precision` apply on the UM982
    - verified that no persistent write/save path was used
    - validated stable `/dev/serial/by-id/*` device targeting
    - captured and fixed a mixed-stream Unicore response-matching gap
    - documented UM982 operator timeout guidance around `--timeout-ms 5000`
  - implementation reuses the existing guarded apply path instead of creating a
    second live-write mechanism
  - vendor-specific persistence semantics are explicit in plan/apply output
  - post-discovery configuration policy is implemented for operator-driven use
  - `base` remains architected, but live base orchestration stays gated until
    the vendor config/profile layer is complete
- ROS2 Replay Node
  - replay saved UBX / NMEA / Unicore / RTCM logs through ROS2 topics
  - support regression, demos, and hardware-free bringup
- Foxglove Surface
  - Foxglove-friendly status, correction, diagnostics, and discovery surface
- ROS2 validation and packaging hardening
  - keep Kilted green
  - validate Lyrical when the image/toolchain is available
  - validate Humble/Jazzy compatibility where practical

Out of scope for `v0.6`:

- full custom GUI
- ESP32 / gateway work
- new receiver vendors

Release verdict:

- ready for `v0.6.0`
- persistent apply remains intentionally guarded beyond the default release
  workflow

Carry-over follow-up immediately after `v0.6.0`:

- keep the module-level receiver-profile API plus
  `gnss_profile_preview`, `gnss_config_plan`, and `gnss_config_apply` CLIs as
  the source of truth for downstream integrations
- post-reset reconnect / probe loop for guarded `factory_reset`
- complete u-blox portable receiver-profile implementation for reset and future
  profile growth
- live receiver identity / model / firmware metadata in discovery, planner,
  and ROS2 reporting
- keep generic NMEA limited to `runtime_only` until a portable write-side
  configuration contract exists

## v0.7 — Downstream UI / Dashboard Integrations

Planned after `v0.6`:

- Universal GNSS does not ship its own GUI yet; the module currently exposes
  only the profile API, standalone CLIs, and downstream integration hooks
- MowgliNext GUI/onboarding will be the first practical downstream integration
  and testbed for profile selection and guarded apply flows
- lessons from MowgliNext can later be reused to design a minimal generic
  Universal GNSS UI for other projects
- define stable downstream hooks from the module-level profile API and ROS2
  surfaces
- live GNSS status view
- RTK / correction status
- satellite / CN0 view
- onboarding / profile selector for the portable receiver profiles
- receiver configuration page
- NTRIP status
- RTCM activity view
- JSON/debug snapshot export

## v0.8 — Embedded / Gateway Layer

Planned later:

- ESP32 build profile
- lightweight protocol/session subset
- ESP32 UART / WiFi / Ethernet adapters
- MQTT status export
- RTK base gateway mode
- LoRa-facing RTCM filtering policy

## v0.9 — Quectel

Deferred until the current stack is stable:

- Quectel framing/parsing audit
- Quectel session/profile foundation
- Quectel configuration engine

## v1.0 — Septentrio

Deferred until the current stack is stable:

- Septentrio SBF audit
- Septentrio session/profile foundation
- Septentrio runtime/status mapping
