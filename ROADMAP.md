# Roadmap

## v0.1 — Portable core and protocol foundation

Implemented:

- Portable runtime state model
- Capability/value flag model
- Runtime aggregator
- Portable diagnostics and health summary
- NMEA parser: GGA, RMC, GSA, GSV, GST, VTG, ZDA
- RTCM3 framing, CRC24Q, message type extraction
- RTCM correction monitor
- Synthetic testdata corpus
- CMake / CTest CI foundation

## v0.2 — u-blox and Unicore protocol support

Implemented:

- UBX parser: NAV-PVT, NAV-DOP, NAV-SAT, NAV-STATUS, MON-RF, RXM-RTCM
- UBX ACK/NAK parser
- UBX CFG-VALGET / CFG-VALSET builders
- u-blox runtime mapping
- Unicore ASCII parser: PVTSLNA, BESTNAVA, RTKSTATUSA, RTCMSTATUSA, SATSINFOA
- Unicore RF/health parser: JAMSTATUSA, FREQJAMSTATUSA, HWSTATUSA, AGCA
- Unicore text config profile builder

Remaining:

- Unicore binary N4 framing
- Unicore binary BESTNAV / PVTSLN equivalents
- Optional u-blox MON-HW / MON-SPAN
- Optional u-blox survey-in support

## v0.3 — Transport, sessions, drivers, and configuration

Implemented:

- ByteSource / ByteSink / ByteDuplex abstraction
- Memory transport
- POSIX serial transport
- TCP client transport
- Receiver profiles
- UbloxSession
- UnicoreSession
- Generic ReceiverSession router
- ReceiverSessionRunner
- ReceiverDriver abstraction
- UbloxDriver
- UnicoreDriver
- Receiver command model
- Command dispatcher
- Transaction/response model
- Transaction engine
- Config application layer
- u-blox response router
- Unicore response router
- Guarded config apply path

Remaining:

- Session auto-detection hardening
- Reconnect/session lifecycle
- Runtime arbitration
- Optional UDP transport
- Optional TLS adapter

## v0.4 — NTRIP and correction services

Implemented:

- NTRIP config model
- NTRIP GET request builder
- Basic Auth
- TCP-backed NTRIP client
- RTCM stream extraction
- RTCM correction monitor integration
- GGA sentence builder
- Explicit GGA injector
- NtripClient GGA injection API
- Reconnect/backoff policy
- Sourcetable parser

Remaining:

- Correction age estimation
- Automatic periodic GGA scheduling
- Multi-caster support
- Local caster / base mode support
- TLS support

## v0.5 — Tools and offline analysis

Implemented:

- rtcm_inspect
- gnss_inspect
- gnss_replay
- gnss_quality_report
- gnss_export JSONL
- gnss_profile_preview
- gnss_config_plan
- gnss_config_apply
- gnss_serial_monitor
- gnss_ntrip_monitor

Remaining:

- JSON schema versioning
- Replay timing mode
- Compare two receivers/logs
- Synthetic log generator
- Optional CSV export later

## v0.6 — ROS 2 integration

Implemented:

- GnssStatus message
- GnssStatus adapter
- NavSatFix adapter

Next:

- ROS2 receiver node
- ROS2 diagnostics adapter
- ROS2 NTRIP node
- ROS2 replay node
- Launch examples
- Humble/Jazzy CI
- Foxglove-friendly topics

## v0.7 — Minimal GUI / dashboard

Planned before ESP32 / LoRa / RTK base gateway work:

- Live GNSS status
- RTK/correction status
- Satellite/CN0 view
- Receiver configuration page
- Predefined mode selection: rover, diagnostics, base
- Safe config apply buttons
- Debug/log viewer
- NTRIP status
- RTCM message rate view
- JSON/debug snapshot export

## v0.8 — Embedded / gateway layer

Planned later:

- ESP32 build profile
- Lightweight protocol/session subset
- ESP32 UART transport
- ESP32 WiFi/Ethernet NTRIP adapter
- MQTT status export
- WebUI metrics
- RTK base gateway mode
- LoRa RTCM filtering

## v0.9 — Future receiver vendors

Deferred until current NMEA / u-blox / Unicore / ROS2 stack is stable:

- Quectel PQTM / PAIR support
- Septentrio SBF support
- Additional receiver profiles
- Additional config engines