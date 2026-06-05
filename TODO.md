# Universal GNSS — TODO

## Current status

Implemented:

- `v0.1` to `v0.4` portable core, protocol, driver, transport, NTRIP, and tool foundations
- `ReceiverNode` and `NtripNode`
- `GnssStatus`, `NavSatFix`, and `diagnostic_msgs` projection
- combined receiver + NTRIP ROS2 bringup
- `robot_localization` example configuration and docs
- replay, inspection, export, config-plan/apply, serial-monitor, and NTRIP-monitor tooling
- live RTCM forwarding from `NtripNode` into `ReceiverNode`
- receiver-side correction diagnostics for u-blox and Unicore paths
- stable `/dev/serial/by-id` support and preferred enumeration
- `gnss_discover` CLI
- `serial_device:=auto`, `serial_baud:=auto`, and `receiver_family:=auto`
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

1. Auto Configuration
2. ROS2 Replay Node
3. Foxglove Surface
4. GUI / Dashboard
5. ESP32 / Gateway
6. Quectel
7. Septentrio

## v0.6 — Operational Bringup

### Auto Configuration

- [x] design/audit pass for portable Auto Configuration architecture
- [x] add a driver-level planner/report layer for discovery-aware dry-run planning
- [ ] extend the planner/report layer into ROS2
- [ ] live u-blox configuration transactions
- [ ] live Unicore configuration transactions
- [x] make vendor-specific persistence semantics explicit in plan/apply output
- [ ] runtime arbitration between streaming traffic and config traffic
- [x] explicit policy for runtime-only vs persistent apply
- [ ] capability/profile consistency cleanup between built-in receiver profiles and driver support
- [ ] keep `base` as a portable role but gate live apply until vendor base workflows are complete
- [x] post-discovery auto-configuration when explicitly enabled
- [ ] production-safe failure handling and rollback expectations

### ROS2 Replay Node

- [ ] ROS2 replay node that publishes `/status`, `/fix`, `/diagnostics`, and optional `/rtcm`
- [ ] stepped replay mode for deterministic debugging
- [ ] wall-time replay mode for demos and integration tests
- [ ] launch/examples for replayed receiver + replayed RTCM workflows
- [ ] reuse existing `gnss_replay` parsing/mapping logic instead of duplicating decode paths

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

## v0.7 — GUI / Dashboard

- [ ] choose GUI stack
- [ ] define local API between core/ROS2 and GUI
- [ ] live GNSS status page
- [ ] RTK/correction status page
- [ ] satellite/CN0 view
- [ ] receiver configuration page
- [ ] safe config apply actions
- [ ] debug/log viewer
- [ ] NTRIP status view
- [ ] RTCM message rate view
- [ ] export JSON/debug snapshot

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
- [ ] ROS2 integration guide refresh for post-`v0.5` bringup
- [ ] sanitizer builds
- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] coverage report
