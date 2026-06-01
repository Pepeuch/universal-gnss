# Roadmap

## Current release posture

The low-level portable GNSS foundation is ready for a `v0.4` tag.

That foundation now covers:

- portable runtime state, aggregation, diagnostics, and health summary
- NMEA, UBX/u-blox, Unicore ASCII, Unicore binary `N4`, and RTCM3 protocol work
- receiver sessions, drivers, command/config plumbing, and response routing
- memory, POSIX serial, and TCP client transport
- TCP-backed NTRIP client foundations
- offline and live low-level tools for inspection, replay, reporting, export,
  config preview/plan/apply, serial monitoring, and NTRIP monitoring

The next major phase is ROS2 integration.

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
- advanced auto-detection
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
- next phase is ROS2 receiver integration, not more low-level feature sprawl

## v0.5 — ROS2 Integration

Completed at the start of this phase:

- `GnssStatus` runtime adapter
- `NavSatFix` compatibility adapter
- `diagnostic_msgs` mapping helpers
- runtime-to-ROS2 mapping audit
- local Kilted package build/test validation
- `ReceiverNode` with serial / TCP launch examples
- `NtripNode` with ROS2 launch example
- combined receiver + NTRIP launch example
- ROS2 end-to-end audit / hardening pass
- `robot_localization` example configuration and documentation

Next:

- ROS2 replay node
- correction forwarding / bringup composition between receiver and NTRIP nodes
- Humble/Jazzy validation
- Foxglove-friendly topic surface

## v0.6 — Minimal GUI / Dashboard

Planned after the ROS2 phase:

- live GNSS status view
- RTK / correction status
- satellite / CN0 view
- receiver configuration page
- NTRIP status
- RTCM activity view
- JSON/debug snapshot export

## v0.7 — Embedded / Gateway Layer

Planned later:

- ESP32 build profile
- lightweight protocol/session subset
- ESP32 UART / WiFi / Ethernet adapters
- MQTT status export
- RTK base gateway mode
- LoRa-facing RTCM filtering policy

## v0.8 — Future Receiver Vendors

Deferred until the current NMEA / u-blox / Unicore / ROS2 stack is stable:

- Quectel support
- Septentrio support
- additional receiver profiles
- additional configuration engines
