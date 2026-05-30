# Universal GNSS — TODO / Roadmap

## Current status

Implemented:

- Portable GNSS runtime core
- Runtime capability/value flags
- Runtime aggregation layer
- NMEA parser: GGA, RMC, GSA, GSV
- UBX parser: NAV-PVT, NAV-SAT, NAV-STATUS, MON-RF
- Unicore ASCII parser: PVTSLNA, BESTNAVA, RTKSTATUSA, RTCMSTATUSA, SATSINFOA
- RTCM3 framing, CRC24Q, message type extraction
- Driver abstraction foundation
- UbloxSession foundation
- UnicoreSession foundation
- Generic ReceiverSession router
- Receiver command/config profile model
- Receiver command dispatcher foundation
- Receiver command transaction/response foundation
- Transport abstraction foundation
- NTRIP request/config/metrics foundation
- ROS2 adapter foundation
- Tools:
  - rtcm_inspect
  - gnss_inspect
  - gnss_replay
- Synthetic/sanitized testdata corpus
- Portable CMake/CTest workflow

---

## Short-term priorities

### 1. Generic receiver session router

- [x] Add generic `ReceiverSession`
- [x] Route to `UbloxSession`
- [x] Route to `UnicoreSession`
- [x] Expose unified runtime state
- [x] Expose generic session metrics
- [x] Add portable `ReceiverSessionRunner`
- [ ] Prepare future auto-detection
- [x] Define portable receiver command/config profile model
- [x] Add portable receiver command dispatcher
- [x] Add portable receiver command transaction/response model

### 2. Runtime diagnostics model

- [ ] Define portable diagnostics/event model
- [ ] Add severity levels
- [ ] Add session health summary
- [ ] Add parser health counters
- [ ] Add correction stream health
- [ ] Keep ROS2 diagnostics as adapter-only

### 3. RTCM correction monitor

- [ ] Track RTCM frame rate
- [ ] Track message type rates
- [ ] Track last-seen timestamps
- [ ] Track base position messages 1005/1006
- [ ] Track MSM constellation availability
- [ ] Prepare LoRa filtering policy

### 4. NTRIP live client

- [ ] Add portable TCP abstraction or adapter
- [ ] Implement NTRIP connection state
- [ ] Implement reconnect/backoff loop
- [ ] Feed RTCM frames into metrics
- [ ] Keep TLS optional/deferred
- [ ] Keep ROS2/ESP32 adapters separate

---

## Protocol roadmap

### NMEA

Implemented:

- [x] GGA
- [x] RMC
- [x] GSA
- [x] GSV

To do:

- [ ] VTG
- [ ] ZDA
- [ ] GST
- [ ] proprietary NMEA extensions
- [ ] multi-sentence GSV aggregation
- [ ] persistent satellite tracking

### UBX

Implemented:

- [x] NAV-PVT
- [x] NAV-SAT
- [x] NAV-STATUS
- [x] MON-RF
- [x] CFG-VALGET payload builder
- [x] CFG-VALSET payload builder
- [x] config profile command builder

To do:

- [ ] NAV-DOP
- [ ] MON-HW / MON-HW2 if useful
- [ ] MON-SPAN
- [ ] RXM-RTCM
- [ ] ACK/NAK
- [ ] survey-in support
- [ ] live configuration transactions

### Unicore

Implemented:

- [x] PVTSLNA
- [x] BESTNAVA
- [x] RTKSTATUSA
- [x] RTCMSTATUSA
- [x] SATSINFOA
- [x] text config profile builder

To do:

- [ ] binary N4 framing
- [ ] binary BESTNAV/PVTSLN equivalents
- [ ] BESTSATA if useful
- [ ] RF/jamming messages
- [ ] hardware status messages
- [ ] live receiver configuration transactions
- [ ] raw observation support

### RTCM3

Implemented:

- [x] framing
- [x] CRC24Q
- [x] message type extraction
- [x] MSM constellation classification

To do:

- [ ] message rate monitor
- [ ] RTCM health monitor
- [ ] basic 1005/1006 base position decode
- [ ] 1230 GLONASS bias decode
- [ ] MSM signal/satellite summary
- [ ] LoRa filtering policy
- [ ] RTCM relay helpers

### Quectel

To do:

- [ ] Audit local Quectel docs
- [ ] Add PQTM/PAIR framing
- [ ] Add basic fix/status parser
- [ ] Add RTK status mapping
- [ ] Add jamming/interference mapping if documented
- [ ] Add receiver profile
- [ ] Add session foundation

### Septentrio

To do:

- [ ] Audit SBF protocol docs
- [ ] Add SBF framing
- [ ] Add PVT/status parser
- [ ] Add satellite/RF parser
- [ ] Add receiver profile
- [ ] Add session foundation

---

## Driver roadmap

Implemented:

- [x] Driver abstraction foundation
- [x] Receiver profiles
- [x] Stream detection foundation
- [x] UbloxSession
- [x] UnicoreSession
- [x] Generic receiver session router
- [x] ReceiverSession byte-source runner

To do:

- [ ] Session auto-detection
- [x] Receiver config command model
- [x] Receiver command dispatcher
- [x] Receiver command transaction/response model
- [x] u-blox config profile builder
- [ ] u-blox ACK/NAK + transaction engine
- [x] Unicore config profile builder
- [ ] Unicore response/transaction engine
- [ ] Quectel config engine
- [ ] transport binding
- [ ] reconnect/session lifecycle
- [ ] health monitoring
- [ ] runtime arbitration

---

## Transport roadmap

Implemented:

- [x] ByteSource / ByteSink / ByteDuplex
- [x] Memory transport
- [x] Ring buffer
- [x] transport metrics

To do:

- [ ] POSIX serial transport
- [ ] TCP client transport
- [ ] UDP transport
- [ ] TLS adapter
- [ ] ESP32 UART adapter
- [ ] ESP32 WiFi/Ethernet adapter
- [ ] reconnect policies
- [ ] timeout policies

---

## NTRIP roadmap

Implemented:

- [x] NTRIP config model
- [x] request builder
- [x] Basic Auth
- [x] GGA injection policy model
- [x] metrics model

To do:

- [ ] TCP-backed NTRIP client
- [ ] reconnect/backoff
- [ ] sourcetable support
- [ ] RTCM frame extraction from stream
- [ ] correction age estimation
- [ ] GGA sentence generation
- [ ] multi-caster support
- [ ] local caster / base mode support

---

## Tools roadmap

Implemented:

- [x] rtcm_inspect
- [x] gnss_inspect
- [x] gnss_replay
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

## ROS2 roadmap

Implemented:

- [x] GnssStatus message
- [x] GnssStatus adapter
- [x] NavSatFix adapter

To do:

- [ ] ROS2 receiver node
- [ ] ROS2 diagnostics adapter
- [ ] ROS2 NTRIP node
- [ ] ROS2 replay node
- [ ] launch examples
- [ ] Humble/Jazzy CI
- [ ] Foxglove-friendly topics
- [ ] BlueOS extension packaging

---

## ESP32 roadmap

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

---

## Documentation roadmap

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

## Quality / CI roadmap

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
