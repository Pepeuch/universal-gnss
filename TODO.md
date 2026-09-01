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


## Release roadmap / blocking milestones

### v0.7.0 — Production containerization / deployment architecture

`v0.7.0` is a dedicated deployment release, not a minimal Docker wrapper.
The container must become a supported production execution environment for
Universal GNSS on development PCs, robots, embedded Linux systems, and future
integration platforms.

Architecture rules:

- Universal GNSS core behavior remains independent from Docker and ROS2.
- ROS2 is one adapter/output surface, not a mandatory dependency for every
  container deployment.
- Deployment configuration, secrets, runtime data, and logs must remain outside
  the immutable image.
- Receiver health, correction health, solution quality, and container/process
  health must remain separate concepts.
- The deployment contract must be reusable by BlueOS without duplicating GNSS
  business logic in the BlueOS extension.

Container build and release:

- [ ] production `Dockerfile` / container build layout
- [ ] reproducible versioned image tags tied to Universal GNSS releases/commits
- [ ] multi-stage builds with a minimal runtime image
- [ ] `amd64` image validation
- [ ] `arm64` image validation
- [ ] define whether `arm/v7` remains a supported target
- [ ] multi-architecture CI build/publish pipeline
- [ ] Software Bill of Materials / image provenance strategy
- [ ] documented image upgrade and rollback policy

Runtime / process lifecycle:

- [ ] define single-container versus composable-service layout for Receiver,
  NTRIP, replay, and optional adapters
- [ ] production entrypoint and deterministic startup ordering
- [ ] graceful SIGTERM/SIGINT shutdown
- [ ] restart policy and crash-recovery behavior
- [ ] receiver-process restart without stale state resurrection
- [ ] NTRIP reconnect/restart without stale source metadata leakage
- [ ] container restart with deterministic configuration reapplication
- [ ] expose running Universal GNSS version/commit in runtime diagnostics

Device access / hotplug:

- [ ] serial passthrough using stable `/dev/serial/by-id/...` identities where
  available
- [ ] document fallback behavior for platforms without `/dev/serial/by-id`
- [ ] least-privilege serial permissions; avoid `--privileged` as the normal path
- [ ] USB receiver disconnect/reconnect inside a running container
- [ ] USB serial renumbering validation
- [ ] F9P <-> UM982 physical swap/recovery validation
- [ ] define udev/device-manager integration needed by production deployments

Configuration / persistence / secrets:

- [ ] stable external configuration directory and mount contract
- [ ] configuration schema/version migration policy
- [ ] persistent receiver/profile configuration state where required
- [ ] read-only defaults plus writable operator overrides
- [ ] NTRIP credentials supplied outside the image
- [ ] secret-file / environment-variable policy with no credential leakage in
  logs or diagnostics
- [ ] persistent diagnostic/log/export directory
- [ ] backup/restore expectations for deployment configuration

Health / observability:

- [ ] container healthcheck that validates service functionality rather than only
  PID/process existence
- [ ] distinguish container/service health from receiver transport health
- [ ] distinguish receiver observation freshness from ROS/publication activity
- [ ] distinguish NTRIP transport, accepted response, correction flow, semantic
  correction health, and RTK solution quality
- [ ] healthcheck behavior when no receiver is intentionally configured
- [ ] structured logs suitable for Docker/Compose/BlueOS collection
- [ ] bounded log retention guidance
- [ ] snapshot/support bundle export for field diagnostics

Networking:

- [ ] NTRIP outbound networking validation
- [ ] Docker DNS/reconnect behavior validation
- [ ] host/network-mode policy
- [ ] ROS2 DDS cross-container validation
- [ ] ROS2 DDS host <-> container validation
- [ ] ROS2 discovery behavior documented for common Docker deployments
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

- [ ] PC Docker validation with u-blox F9P
- [ ] PC Docker validation with Unicore UM98x
- [ ] robot Docker validation
- [ ] long-run container validation
- [ ] receiver silence and reconnect validation
- [ ] NTRIP disconnect/reconnect validation
- [ ] container crash/restart validation
- [ ] host reboot/autostart validation
- [ ] low receiver rate / high publication-rate validation
- [ ] high receiver rate / low publication-rate validation
- [ ] RTK Float/Fixed transition validation through container boundaries
- [ ] no stale state survives source, receiver, process, or container incarnation
  changes

Documentation:

- [ ] Docker quick-start
- [ ] production deployment guide
- [ ] serial/device permissions guide
- [ ] Docker Compose example
- [ ] ROS2 + Docker integration guide
- [ ] troubleshooting / support-bundle guide
- [ ] security and secret-management notes


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
- [ ] extend planner/report layer into ROS2
- [ ] operator observability review
- [ ] snapshot/export surface
- [ ] ROS2 CI coverage
- [x] keep Kilted green
- [ ] validate Lyrical
- [ ] validate Humble/Jazzy
- [ ] arm64 validation
- [ ] long-run validation
- [ ] receiver restart validation
- [ ] GNSS reconnect validation
- [ ] NTRIP reconnect validation
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

- [ ] contributor architecture guide
- [ ] parser writing guide
- [ ] sanitizer builds
- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] coverage report
