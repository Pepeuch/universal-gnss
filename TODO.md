# Universal GNSS — TODO

## Current status

Implemented:

- `v0.1` to `v0.4` portable core, protocol, driver, transport, NTRIP, and tool foundations
- `ReceiverNode` and `NtripNode`
- `ReplayNode` for hardware-free ROS2 status/fix/diagnostics replay with
  optional RTCM publication
- `GnssStatus`, `NavSatFix`, and `diagnostic_msgs` projection
- combined receiver + NTRIP ROS2 bringup
- `robot_localization` example configuration and docs
- replay, inspection, export, config-plan/apply, serial-monitor, and NTRIP-monitor tooling
- live RTCM forwarding from `NtripNode` into `ReceiverNode`
- receiver-side correction diagnostics for u-blox and Unicore paths
- stable `/dev/serial/by-id` support and preferred enumeration
- `gnss_discover` CLI
- `serial_device:=auto`, `serial_baud:=auto`, and `receiver_family:=auto`
- Auto Configuration planner/report layer
- operator-driven runtime-only apply for u-blox and Unicore
- operator-driven Unicore persistent / `factory_reset` reset-recovery apply
- portable receiver profile surface:
  - `runtime_only`
  - `rover_high_precision`
  - `rover_high_precision_debug`
  - `factory_reset`
- legacy alias compatibility for `rover` and `diagnostics`
- module-level receiver profile API plus standalone preview/plan/apply CLIs as
  the intended downstream integration surface
- real F9P and UM982 runtime-only apply validation
- auto-discovery v2:
  - score-based detection
  - u-blox / Unicore / generic NMEA classification
  - MAVLink heartbeat rejection
  - random serial text rejection
  - silent-port rejection
  - discovery score/reason diagnostics
- real ROS2 validation on ZED-F9P and UM982
- real local NTRIP caster validation
- Kilted validation in the local devcontainer and local MowgliNext development image
- MowgliNext integration hardening with fixes kept in Universal GNSS instead of downstream workarounds

## Next milestone order

1. Foxglove Surface
2. Auto Configuration ROS2/report extension and arbitration
3. Downstream UI / Dashboard integration
4. ESP32 / Gateway
5. Quectel
6. Septentrio

## Post-v0.6 — MowgliNext Validation

### Integration Validation

- [ ] Validate Universal GNSS → navsat_to_absolute_pose
- [ ] Validate Universal GNSS → localization_monitor
- [ ] Validate Universal GNSS → Nav2
- [ ] Validate Universal GNSS → mower bringup stack

### Runtime Validation

- [ ] 30 min continuous runtime test
- [ ] 1 h continuous runtime test
- [ ] 2 h continuous runtime test
- [ ] Monitor memory usage
- [ ] Monitor CPU usage
- [ ] Monitor diagnostics stability

### Fault Injection

- [ ] GNSS disconnect / reconnect test
- [ ] NTRIP disconnect / reconnect test
- [ ] Receiver restart test
- [ ] USB serial port renumbering test
- [ ] F9P ↔ UM982 swap without software changes

### Navigation Validation

- [ ] Static accuracy validation
- [ ] Short waypoint mission
- [ ] Long waypoint mission
- [ ] Full Nav2 mission validation
- [ ] Verify localization stability during RTK Float/Fixed transitions

## v0.6 — Operational Bringup

### Auto Configuration

- [x] design/audit pass for portable Auto Configuration architecture
- [x] add a driver-level planner/report layer for discovery-aware dry-run planning
- [ ] extend the planner/report layer into ROS2
- [x] live u-blox runtime-only operator configuration transactions
- [x] live Unicore runtime-only operator configuration transactions
- [x] expose portable profile names and keep legacy alias compatibility in the
  standalone CLIs
- [x] keep the module-level receiver profile API plus standalone
  preview/plan/apply CLIs as the source of truth for downstream integrations
- [x] make vendor-specific persistence semantics explicit in plan/apply output
- [x] post-reset reconnect / active probe loop for Unicore `factory_reset`
- [ ] complete u-blox `factory_reset` and future portable profile coverage
- [ ] runtime arbitration between streaming traffic and config traffic
- [x] explicit policy for runtime-only vs persistent apply
- [ ] capability/profile consistency cleanup between built-in receiver profiles and driver support
- [ ] document and preserve generic NMEA `runtime_only` limitations until a
  portable write-side config contract exists
- [x] post-discovery auto-configuration when explicitly enabled
- [ ] production-safe failure handling and rollback expectations
- [ ] live receiver identity / model / firmware metadata in discovery and
  planning output

### ROS2 Replay Node

- [x] ROS2 replay node that publishes `/status`, `/fix`, `/diagnostics`, and optional `/rtcm`
- [x] stepped replay mode for deterministic debugging
- [x] wall-time replay mode for demos and integration tests
- [x] fast replay mode for hardware-free tests
- [x] launch/examples for replayed receiver + replayed RTCM workflows
- [x] reuse existing `gnss_replay` parsing/mapping logic instead of duplicating decode paths

### Foxglove Surface

- [ ] Foxglove-friendly topic surface review for status, diagnostics, correction, and discovery data
- [ ] discovery/correction/receiver diagnostics shaped for fast operator inspection
- [ ] snapshot/export surface for debugging sessions

### Validation / Packaging

- [ ] ROS2 CI coverage for the integrated stack
- [ ] keep Kilted green as the reference distro
- [ ] validate Lyrical when the image/toolchain is available
- [ ] validate Humble/Jazzy compatibility where practical
- [ ] arm64 build check for the ROS2-integrated stack

## Later work

### Tools / Analysis

- [ ] `gnss_replay` timing mode outside the ROS2 replay node
- [ ] runtime state CSV export
- [ ] JSON schema stabilization/versioning
- [ ] compare two receivers/logs
- [ ] generate additional sanitized test logs

### Transport / NTRIP

- [ ] TLS (ssl)
- [ ] Client certificate authentication
- [ ] Custom CA certificate support
- [ ] RTCM ROS message package selection (rtcm_msgs / mavros_msgs)
- [ ] UDP transport
- [ ] TLS adapter/support
- [ ] correction age estimation
- [ ] automatic periodic GGA sending
- [ ] multi-caster support
- [ ] local caster / base mode support

### Receiver / Runtime Gaps

- [ ] generic speed/course runtime contract for `VTG`
- [ ] generic GNSS wall-clock/date runtime contract for `ZDA`
- [ ] u-blox `MON-SPAN`
- [ ] u-blox survey-in support
- [ ] RTCM `1230` GLONASS bias decode
- [ ] RTCM MSM signal/satellite summary
- [ ] Unicore raw observation support
- [ ] conservative AGC threshold interpretation if a safe generic policy emerges

## v0.7 — Downstream UI / Dashboard Integrations

- [ ] keep Universal GNSS itself scoped to the module API, CLI tools, and
  downstream integration hooks rather than a built-in GUI
- [ ] define the local API between core/ROS2 surfaces and downstream UI
  consumers
- [ ] use MowgliNext GUI/onboarding as the first practical downstream
  integration and testbed for receiver profile selection
- [ ] capture lessons from MowgliNext before designing a minimal generic
  Universal GNSS UI for other projects
- [ ] downstream live GNSS status page
- [ ] downstream RTK/correction status page
- [ ] downstream satellite/CN0 view
- [ ] downstream receiver configuration page
- [ ] downstream safe config apply actions
- [ ] downstream debug/log viewer
- [ ] downstream NTRIP status view
- [ ] downstream RTCM message rate view
- [ ] downstream export JSON/debug snapshot

## v0.8 — ESP32 / Gateway

- [ ] define ESP32 build profile
- [ ] disable heavy tools for embedded builds
- [ ] enable lightweight core/protocol/session layers
- [ ] UART transport adapter
- [ ] WiFi/Ethernet NTRIP adapter
- [ ] LoRa RTCM filtering policy
- [ ] MQTT status export
- [ ] RTK base gateway mode

## v0.9 — Quectel

- [ ] audit local Quectel docs
- [ ] add PQTM/PAIR framing
- [ ] add basic fix/status parser
- [ ] add RTK status mapping
- [ ] add receiver profile/session foundation
- [ ] add Quectel configuration engine

## v1.0 — Septentrio

- [ ] audit SBF protocol docs
- [ ] add SBF framing
- [ ] add PVT/status parser
- [ ] add satellite/RF parser
- [ ] add receiver profile/session foundation

## Documentation / Quality

- [ ] contributor architecture guide
- [ ] parser writing guide
- [ ] test vector guide
- [ ] ROS2 integration guide refresh for post-`v0.6.0` bringup
- [ ] sanitizer builds
- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] coverage report
