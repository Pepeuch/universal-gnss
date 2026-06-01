# Universal GNSS — TODO / Roadmap

## Current status

Implemented:

- Low-level GNSS foundation ready for ROS2 integration phase
- Portable GNSS runtime core
- Runtime capability/value flags
- Runtime aggregation layer
- Portable diagnostics/event model
- Portable health summary foundation
- RTCM correction monitor foundation
- POSIX serial transport
- TCP client transport
- NMEA parser: GGA, RMC, GSA, GSV, GST, VTG, ZDA
- UBX parser: NAV-PVT, NAV-DOP, NAV-SAT, NAV-STATUS, MON-HW, MON-HW2, MON-RF, RXM-RTCM
- Unicore parser: BESTNAVB, PVTSLNB, PVTSLNA, BESTNAVA, BESTSATA, RTKSTATUSA, RTCMSTATUSA, SATSINFOA
- RTCM3 framing, CRC24Q, message type extraction
- Driver abstraction foundation
- Generic NMEA session
- UbloxSession foundation
- UnicoreSession foundation
- Generic ReceiverSession router
- Receiver command/config profile model
- Receiver command dispatcher foundation
- Receiver command transaction/response foundation
- Receiver command transaction engine foundation
- Receiver config application foundation
- UBX ACK/NAK parser + response mapper
- u-blox response router foundation
- Unicore response router foundation
- Receiver driver abstraction
- generic NMEA driver
- u-blox driver
- Unicore driver
- Transport abstraction foundation
- NTRIP request/config/metrics foundation
- Portable NMEA GGA sentence builder foundation
- Explicit NTRIP GGA injection support
- Explicit-call GGA injector helper
- NTRIP reconnect/backoff policy foundation
- TCP-backed NTRIP client foundation
- ROS2 adapter foundation
- Tools:
  - rtcm_inspect
  - gnss_inspect
  - gnss_replay
  - gnss_quality_report
  - gnss_export
  - gnss_profile_preview
  - gnss_config_plan
  - gnss_config_apply
  - gnss_ntrip_monitor
- Synthetic/sanitized testdata corpus
- Portable CMake/CTest workflow

---

## Next phase after v0.4

Primary focus:

- [x] ROS2 receiver node skeleton
- [x] ROS2 NTRIP node
- [ ] ROS2 replay node
- [x] minimal ROS2 launch/examples
- [x] robot_localization example config/docs
- [ ] ROS2 CI coverage

Keep deferred until after ROS2 stabilization:

- [ ] GUI / dashboard
- [ ] ESP32 integration
- [ ] LoRa / gateway work
- [ ] RTK base gateway workflows
- [ ] future receiver vendors

---

## Immediate priorities

### Common runtime diagnostics

To do:

- [x] Define portable diagnostics/event model
- [x] Add severity levels
- [x] Keep ROS2 diagnostics as adapter-only
- [x] Add first ROS2 receiver-node skeleton
- [x] Add first robot_localization example documentation

### Offline export and analysis

Implemented:

- [x] JSONL runtime timeline export

To do:

- [ ] CSV export
- [ ] schema versioning if needed
- [ ] live stream export adapters
- [ ] plotting / notebook helpers

### ROS2 integration

Implemented:

- [x] `GnssStatus` adapter
- [x] `NavSatFix` adapter
- [x] `DiagnosticArray` adapter
- [x] `ReceiverNode`
- [x] `NtripNode`
- [x] minimal receiver launch examples
- [x] minimal NTRIP launch example
- [x] robot_localization example YAML and launch
- [x] receiver node runtime validation and diagnostics hardening

To do:

- [ ] ROS2 replay node
- [ ] correction forwarding / bringup composition between `ReceiverNode` and `NtripNode`
- [ ] production bringup / launch structure
- [ ] Humble / Jazzy validation
- [ ] Kilted linkage modernization
- [ ] Nav2 integration after localization examples stabilize

### RTCM correction monitor

Implemented:

- [x] framing
- [x] CRC24Q
- [x] message type extraction
- [x] MSM constellation classification
- [x] Track RTCM frame rate
- [x] Track message type rates
- [x] Track last-seen timestamps
- [x] Track base position messages 1005/1006
- [x] Track MSM constellation availability
- [x] message rate monitor

To do:

- [ ] Prepare LoRa filtering policy
- [x] basic 1005/1006 base position decode
- [ ] 1230 GLONASS bias decode
- [ ] MSM signal/satellite summary
- [ ] LoRa filtering policy
- [ ] RTCM relay helpers

### Transport TCP/UDP basics

Implemented:

- [x] ByteSource / ByteSink / ByteDuplex
- [x] Memory transport
- [x] Ring buffer
- [x] transport metrics
- [x] Add portable TCP abstraction or adapter
- [x] POSIX serial transport
- [x] TCP client transport

To do:

- [ ] UDP transport
- [ ] TLS adapter

### NTRIP live client

Implemented:

- [x] NTRIP config model
- [x] request builder
- [x] Basic Auth
- [x] GGA injection policy model
- [x] GGA sentence generation
- [x] configurable GGA talker / UTC formatting
- [x] explicit-call GGA injector helper
- [x] explicit-call `NtripClient` GGA injector integration
- [x] metrics model

To do:

- [x] Implement NTRIP connection state
- [x] reconnect/backoff policy/state model
- [x] Feed RTCM frames into metrics
- [ ] Keep TLS optional/deferred
- [x] Keep ROS2/ESP32 adapters separate
- [x] TCP-backed NTRIP client
- [x] sourcetable support
- [x] RTCM frame extraction from stream
- [ ] correction age estimation
- [ ] automatic periodic GGA sending
- [x] NTRIP client GGA integration
- [ ] multi-caster support
- [ ] local caster / base mode support

### Session lifecycle/reconnect

To do:

- [ ] Prepare future auto-detection
- [ ] Session auto-detection
- [ ] transport binding
- [ ] Implement reconnect/backoff loop
- [x] reconnect/backoff
- [ ] reconnect/session lifecycle
- [x] reconnect policies
- [ ] timeout policies

### Health monitoring

To do:

- [x] Add session health summary
- [ ] Add parser health counters
- [x] Add correction stream health
- [x] RTCM health monitor
- [x] offline GNSS quality report tool
- [ ] health monitoring

---

## Finish existing receiver families

### NMEA

Implemented:

- [x] GGA
- [x] RMC
- [x] GSA
- [x] GSV
- [x] GST
- [x] VTG
- [x] ZDA
- [x] generic NMEA session / driver

To do:

- [ ] generic speed/course runtime fields if needed
- [ ] GNSS wall-clock/date runtime contract if needed
- [ ] proprietary NMEA extensions
- [ ] multi-sentence GSV aggregation
- [ ] persistent satellite tracking

### u-blox

Implemented:

- [x] NAV-PVT
- [x] NAV-DOP
- [x] NAV-SAT
- [x] NAV-STATUS
- [x] MON-HW / MON-HW2
- [x] MON-RF
- [x] RXM-RTCM
- [x] CFG-VALGET payload builder
- [x] CFG-VALSET payload builder
- [x] config profile command builder
- [x] ACK-ACK / ACK-NAK parser

To do:

- [ ] MON-SPAN
- [ ] survey-in support

### Unicore

Implemented:

- [x] BESTNAVB
- [x] PVTSLNB
- [x] PVTSLNA
- [x] BESTNAVA
- [x] RTKSTATUSA
- [x] RTCMSTATUSA
- [x] SATSINFOA
- [x] BESTSATA
- [x] JAMSTATUSA
- [x] FREQJAMSTATUSA
- [x] HWSTATUSA
- [x] AGCA
- [x] text config profile builder
- [x] BESTNAVB / PVTSLNB routing through session, replay, quality report, and JSONL export

To do:

- [x] binary N4 framing
- [ ] raw observation support
- [ ] AGC threshold interpretation if a safe generic policy emerges

---

## Driver/config completion

### Session/router foundation

Implemented:

- [x] Driver abstraction foundation
- [x] Receiver profiles
- [x] Stream detection foundation
- [x] UbloxSession
- [x] UnicoreSession
- [x] Generic receiver session router
- [x] ReceiverSession byte-source runner
- [x] Receiver driver abstraction
- [x] u-blox driver
- [x] Unicore driver
- [x] Add generic `ReceiverSession`
- [x] Route to `UbloxSession`
- [x] Route to `UnicoreSession`
- [x] Expose unified runtime state
- [x] Expose generic session metrics
- [x] Add portable `ReceiverSessionRunner`
- [x] Add receiver driver abstraction
- [x] Add u-blox driver
- [x] Add Unicore driver

### u-blox live transaction integration

Implemented:

- [x] u-blox config profile builder
- [x] u-blox ACK/NAK parser + response mapper
- [x] u-blox response router
- [x] Add u-blox response router foundation

To do:

- [ ] live configuration transactions
- [ ] u-blox live transaction integration

### Unicore live transaction integration

Implemented:

- [x] Unicore config profile builder
- [x] Unicore response router foundation
- [x] Add Unicore response router foundation

To do:

- [ ] live receiver configuration transactions
- [ ] Unicore response/transaction engine

### Command/profile application hardening

Implemented:

- [x] Receiver config command model
- [x] Receiver command dispatcher
- [x] Receiver command transaction/response model
- [x] Receiver command transaction engine foundation
- [x] Receiver config application foundation
- [x] Define portable receiver command/config profile model
- [x] Add portable receiver command dispatcher
- [x] Add portable receiver command transaction/response model
- [x] Add portable receiver command transaction engine foundation
- [x] Add portable receiver config application foundation

To do:

- [ ] runtime arbitration

---

## Tools stabilization

Implemented:

- [x] rtcm_inspect
- [x] gnss_inspect
- [x] gnss_replay
- [x] gnss_profile_preview
- [x] gnss_config_plan
- [x] gnss_config_apply
- [x] gnss_ntrip_monitor
- [x] testdata corpus

To do:

- [ ] gnss_replay timing mode
- [ ] runtime state CSV export
- [ ] JSON schema stabilization
- [ ] RTCM rate report
- [ ] CN0 report
- [ ] RTK quality report
- [ ] correction stream report
- [ ] compare two receivers/logs
- [ ] generate synthetic test logs

---

## ROS2 integration

Implemented:

- [x] GnssStatus message
- [x] GnssStatus adapter
- [x] NavSatFix adapter
- [x] diagnostic_msgs adapter helpers
- [x] runtime-to-ROS2 mapping audit
- [x] ROS2 covariance projection from runtime accuracy fields
- [x] Kilted local build/test validation
- [x] ROS2 receiver node skeleton
- [x] ROS2 NTRIP node
- [x] minimal ROS2 serial/TCP launch examples
- [x] minimal ROS2 NTRIP launch example

To do:

- [x] harden ROS2 receiver node
- [ ] ROS2 replay node
- [ ] correction forwarding / bringup composition between receiver and NTRIP nodes
- [ ] richer launch/examples
- [ ] Humble/Jazzy CI
- [ ] Foxglove-friendly topics
- [ ] BlueOS extension packaging

---

## GUI / Web dashboard

GUI/dashboard work must come before ESP32 / LoRa / RTK base gateway work.

To do:

- [ ] Choose GUI stack
- [ ] Define local API between core/ROS2 and GUI
- [ ] Live GNSS status page
- [ ] RTK/correction status page
- [ ] Satellite/CN0 view
- [ ] Receiver configuration page
- [ ] Predefined mode selection:
  - rover
  - diagnostics
  - base
- [ ] Safe config apply buttons
- [ ] Receiver reset button
- [ ] Debug/log viewer
- [ ] NTRIP status view
- [ ] RTCM message rate view
- [ ] Export JSON/debug snapshot

---

## ESP32 integration

To do:

- [ ] Define ESP32 build profile
- [ ] Disable heavy tools
- [ ] Enable lightweight core/protocol/session layers
- [ ] UART transport adapter
- [ ] WiFi/Ethernet NTRIP adapter
- [ ] LoRa RTCM filtering
- [ ] WebUI metrics
- [ ] MQTT status export
- [ ] RTK base gateway mode
- [ ] ESP32 UART adapter
- [ ] ESP32 WiFi/Ethernet adapter

---

## Future vendors

### Quectel

To do:

- [ ] Audit local Quectel docs
- [ ] Add PQTM/PAIR framing
- [ ] Add basic fix/status parser
- [ ] Add RTK status mapping
- [ ] Add jamming/interference mapping if documented
- [ ] Add receiver profile
- [ ] Add session foundation
- [ ] Quectel config engine

### Septentrio

To do:

- [ ] Audit SBF protocol docs
- [ ] Add SBF framing
- [ ] Add PVT/status parser
- [ ] Add satellite/RF parser
- [ ] Add receiver profile
- [ ] Add session foundation

---

## Documentation

Implemented:

- [x] architecture overview
- [x] protocol coverage
- [x] runtime aggregation policy
- [x] driver layer docs
- [x] transport docs
- [x] NTRIP docs
- [x] tools docs
- [x] UBX runtime mapping
- [x] Unicore runtime mapping

To do:

- [ ] contributor architecture guide
- [ ] coding style
- [ ] parser writing guide
- [ ] vendor mapping guide
- [ ] test vector guide
- [ ] ROS2 integration guide
- [ ] ESP32 integration guide
- [ ] BlueOS integration guide

---

## CI / quality

Implemented:

- [x] CMake build
- [x] CTest tests
- [x] GitHub Actions portable CMake workflow

To do:

- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] sanitizer builds
- [ ] coverage report
- [ ] ROS2 CI matrix
- [ ] arm64 build check
- [ ] ESP32 build check
