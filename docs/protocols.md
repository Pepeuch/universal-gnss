# Protocol Parser Coverage

This document describes what `gnss_protocols` currently implements, how much of
that data is projected into `universal_gnss::GnssRuntimeState`, and what is
intentionally deferred.

The goal is to keep the parser layer honest: framing, checksums, and small
semantic decoders are already present, but transport, drivers, and high-level
receiver orchestration are not.

## Purpose

`gnss_protocols` is the portable protocol layer.

Current responsibilities:

- byte framing and checksum validation
- typed protocol records
- small semantic decoders for selected message types
- conservative protocol-to-runtime mapping helpers

Current non-responsibilities:

- serial, TCP, USB, or transport handling
- receiver auto-detection
- receiver configuration
- NTRIP
- ROS 2 nodes
- persistent session tracking

## Current Tools

Standalone inspection tools now live in `gnss_tools`.

- `rtcm_inspect`
  - reads binary RTCM-like data from a file or stdin
  - reuses the RTCM framer, CRC24Q validation, and message-type helpers
  - prints per-frame summaries, aggregate counts, or simple JSON
- `gnss_inspect`
  - reads mixed GNSS byte streams from a file or stdin
  - recognizes NMEA, UBX, Unicore ASCII, RTCM3, and noise spans
  - prints a compact timeline, aggregate counts, or simple JSON
- `gnss_replay`
  - reads mixed GNSS byte streams from a file or stdin
  - reuses semantic parsers and the runtime aggregator to produce normalized
    runtime-state timelines
  - prints per-record runtime updates, aggregate counts, or simple JSON

Examples:

```text
rtcm_inspect file.rtcm
cat file.rtcm | rtcm_inspect -
rtcm_inspect --summary file.rtcm
rtcm_inspect --json file.rtcm

gnss_inspect file.bin
cat file.bin | gnss_inspect -
gnss_inspect --summary file.bin
gnss_inspect --json file.bin

gnss_replay file.bin
cat file.bin | gnss_replay -
gnss_replay --summary file.bin
gnss_replay --json file.bin
```

See [tools.md](tools.md) for the current tool-focused usage notes.

## Data Flow

The intended flow is:

```text
raw bytes
   ->
framer + checksum validation
   ->
typed protocol record
   ->
optional protocol-specific runtime mapping helper
   ->
partial GnssRuntimeState
   ->
GnssRuntimeAggregator
   ->
coherent runtime state for ROS 2 / ESP32 / tools
```

RTCM is intentionally narrower today:

```text
raw RTCM3 bytes
   ->
RTCM framer + CRC24Q
   ->
validated RtcmFrame
   ->
message type extraction / classification
```

No RTCM runtime mapping exists yet.

## Current Coverage

### NMEA

Implemented semantic messages:

- `GGA`
- `RMC`
- `GSA`
- `GSV`
- `GST`
- `VTG`
- `ZDA`

Implemented behaviors:

- checksum validation through the NMEA framer/checksum helpers
- sentence talker and sentence type extraction
- semantic field parsing for the messages above
- conservative `GnssRuntimeState` mapping helpers

Current NMEA notes:

- `GGA` provides the strongest position mapping today
- `RMC` currently contributes fix validity and coordinates
- `GSA` contributes DOP and active-satellite information
- `GSV` contributes satellites-in-view and per-sentence CN0 summaries
- `GST` contributes conservative horizontal/vertical accuracy only
- `VTG` currently contributes semantic course/speed parsing only
- `ZDA` currently contributes semantic UTC date/time and local-zone parsing only
- `GST` accuracy now flows through the NMEA session/replay routing paths that
  already consume `GGA` / `RMC` / `GSA` / `GSV`
- `VTG` is not projected into `GnssRuntimeState` yet because the core does not
  yet define a generic speed/course field contract
- `ZDA` is not projected into `GnssRuntimeState` yet because the core does not
  yet define a GNSS wall-clock or calendar-time contract beyond sample timestamps

What NMEA does not do yet:

- `GSA` / `GSV` multi-sentence aggregation
- persistent satellite tracking across epochs
- `VTG` runtime projection
- `ZDA` runtime projection or timestamp synthesis
- proprietary vendor sentences, or NMEA state fusion

### UBX

Implemented semantic messages:

- `CFG-VALSET` payload builders
- `CFG-VALGET` payload builders
- `ACK-ACK`
- `ACK-NAK`
- `RXM-RTCM`
- `NAV-STATUS`
- `NAV-PVT`
- `NAV-DOP`
- `NAV-SAT`
- `MON-RF`

Implemented behaviors:

- UBX sync and checksum validation through the UBX framer
- fixed-layout semantic decode for `ACK-ACK` / `ACK-NAK`
- fixed-layout semantic decode for the messages above
- modern `CFG-VALSET` / `CFG-VALGET` payload and full-frame generation
- conservative `GnssRuntimeState` mapping helpers

Current UBX notes:

- `CFG-VALSET` builders currently generate version `0x01` transaction-capable
  payloads only; they do not send them anywhere
- `CFG-VALGET` builders currently generate version `0x00` poll requests only
- `ACK-ACK` / `ACK-NAK` parsing currently decodes only the target message
  class/id; it does not manage transactions or retries
- `RXM-RTCM` provides receiver-side RTCM input status:
  - RTCM message type
  - reference station id when present
  - receiver-side CRC-failed flag
  - used / not-used / unknown handling state
- `NAV-STATUS` provides fix-status metadata, differential-solution state, and
  carrier-solution status when valid
- `NAV-PVT` is the main source for normalized fix, position, accuracy, and RTK
  mode
- `NAV-DOP` provides receiver-native `hdop` / `vdop` only
- `NAV-SAT` provides satellites visible / used and CN0 summaries
- `MON-RF` provides documented RF-interference / jamming state only
- `RXM-RTCM` maps into portable correction diagnostics only; it does not
  project into `GnssRuntimeState`

See [docs/vendors/ublox/runtime_mapping.md](vendors/ublox/runtime_mapping.md)
for the current message-by-message UBX runtime mapping contract.

What UBX does not do yet:

- `CFG-*` transaction execution
- live `ACK/NAK` transaction handling
- richer receiver-side correction acceptance tracking beyond the current
  per-message status helper
- `MON-SPAN`
- richer RF diagnostics or spoofing classification

### RTCM3

Implemented behaviors:

- RTCM3 framing
- CRC24Q validation
- 12-bit message type extraction from validated frames
- lightweight classification helpers

Current RTCM classifications:

- station ARP / base position: `1005`, `1006`
- GLONASS code-phase bias: `1230`
- MSM family detection and constellation classification:
  - GPS `107x`
  - GLONASS `108x`
  - Galileo `109x`
  - SBAS `110x`
  - QZSS `111x`
  - BeiDou `112x`
  - NavIC `113x`

Current RTCM monitor support:

- reusable correction-stream activity monitor in `gnss_protocols`
- total / valid / invalid frame counters
- per-message-type counts, last-seen timestamps, and simple windowed rates
- MSM constellation counts, last-seen timestamps, and simple windowed rates
- presence tracking for base-position messages `1005` / `1006`
- presence tracking for GLONASS bias message `1230`
- portable correction-health summaries for later NTRIP / ROS 2 / GUI reuse

What RTCM does not do yet:

- payload semantic decoding
- MSM satellite / signal extraction
- station metadata decode
- correction-age estimation
- runtime-state mapping
- LoRa filtering

The RTCM correction monitor still does not perform full payload decode. It
tracks correction-stream activity and health around already-classified RTCM
frames, and is intended to feed later NTRIP clients, ROS 2 diagnostics, and
GUI/dashboard work without introducing transport or middleware coupling here.

### Unicore

Implemented binary foundation:

- binary `N4` framing
- documented `AA 44 B5` sync detection
- documented 24-byte header extraction
- documented 32-bit CRC validation
- binary frame container with header metadata and raw payload bytes

Implemented semantic ASCII messages:

- `PVTSLNA`
- `BESTNAVA`
- `RTKSTATUSA`
- `RTCMSTATUSA`
- `SATSINFOA`
- `JAMSTATUSA`
- `FREQJAMSTATUSA`
- `HWSTATUSA`
- `AGCA`

Implemented behaviors:

- Unicore ASCII framing through the existing line framer
- Unicore binary `N4` framing through a dedicated incremental framer
- fixed-layout semantic decode for the messages above
- conservative `GnssRuntimeState` mapping helpers for position / RTK / heading /
  correction-age fields that are explicitly documented

Current Unicore notes:

- binary `N4` framing is currently integrity-only:
  - sync detection
  - documented header extraction
  - payload-length extraction
  - CRC validation
  - unknown binary message ids are preserved as valid binary frames when frame
    integrity is valid
- `PVTSLNA` is the richest current Unicore position / heading source
- `BESTNAVA` provides stable position-quality, accuracy, and correction-age
  fields
- `RTKSTATUSA` complements position messages with RTK-mode and dual-antenna
  status
- `SATSINFOA` provides tracked-satellite counts and CN0 summaries
- `JAMSTATUSA` and `FREQJAMSTATUSA` provide documented Unicore-side
  interference / jamming state
- `HWSTATUSA` provides conservative hardware diagnostics from documented clock
  validity only
- `AGCA` is parsed semantically, but stays out of the portable runtime because
  AGC thresholds are explicitly hardware-dependent in the vendor manual
- `RTCMSTATUSA` is parsed semantically but does not project into runtime state
  yet
- ASCII remains the primary Unicore runtime source today; binary `N4` semantic
  decode and session routing are still deferred

See [docs/vendors/unicore/runtime_mapping.md](vendors/unicore/runtime_mapping.md)
for the current message-by-message Unicore runtime mapping contract.

What Unicore does not do yet:

- binary `N4` semantic decoding
- receiver configuration commands
- AGC threshold interpretation beyond documented safe semantics
- constellation-specific aggregate statistics

## Runtime Mapping Coverage

The table below describes which normalized runtime fields are currently filled
by protocol-specific mapping helpers.

```text
Runtime field            Current protocol sources
---------------------------------------------------------------
fix_valid                NMEA GGA, NMEA RMC, NMEA GSA, UBX NAV-STATUS, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA, Unicore RTKSTATUSA
fix_type                 NMEA GGA, UBX NAV-STATUS, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA, Unicore RTKSTATUSA
rtk_mode                 UBX NAV-STATUS, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA, Unicore RTKSTATUSA
latitude / longitude     NMEA GGA, NMEA RMC, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA
altitude                 NMEA GGA, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA
horizontal accuracy      NMEA GST, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA
vertical accuracy        NMEA GST, UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA
hdop                     NMEA GGA, NMEA GSA, UBX NAV-DOP, Unicore PVTSLNA
vdop                     NMEA GSA, UBX NAV-DOP
satellites_used          NMEA GGA, NMEA GSA, UBX NAV-PVT, UBX NAV-SAT, Unicore PVTSLNA, Unicore BESTNAVA
satellites_visible       NMEA GSV, UBX NAV-SAT
satellites_tracked       Unicore PVTSLNA, Unicore BESTNAVA, Unicore SATSINFOA
mean_cn0 / max_cn0       NMEA GSV, UBX NAV-SAT, Unicore SATSINFOA
heading                  UBX NAV-PVT, Unicore PVTSLNA
interference / jamming   UBX MON-RF, Unicore JAMSTATUSA, Unicore FREQJAMSTATUSA
correction_age           Unicore PVTSLNA, Unicore BESTNAVA
dual antenna state       Unicore RTKSTATUSA
```

### Mapping Policy

Current mapping is conservative on purpose.

Rules:

- only fields explicitly present in the parsed protocol record are projected
- unsupported fields stay unsupported
- no parser invents RTK or correction semantics from indirect clues
- no parser invents full covariance, correction age, or RF quality beyond the
  documented source message

Examples:

- NMEA `GGA` can set a generic fix and coordinates, but it does not claim RTK
  fixed
- NMEA `GST` can set horizontal and vertical accuracy, but it does not imply
  fix validity, RTK mode, satellite counts, or covariance orientation in the
  portable runtime model
- `GST` horizontal accuracy currently uses `max(latitude_std_dev_m,
  longitude_std_dev_m)` as a conservative single-value summary
- UBX `NAV-PVT` can set normalized RTK mode from documented carrier-solution
  bits
- UBX `NAV-DOP` can set `hdop` / `vdop`, but it does not imply fix validity,
  RTK mode, or position
- Unicore `PVTSLNA` and `BESTNAVA` can set RTK mode only from documented
  position-type enums, not from indirect heuristics
- Unicore `JAMSTATUSA` and `FREQJAMSTATUSA` only map documented jamming state;
  they do not imply fix, RTK, or correction quality
- Unicore `HWSTATUSA` emits conservative receiver diagnostics only
- RTCM currently does not modify runtime state at all

## Current Guarantees

Today the protocol layer guarantees:

- portable C++ parsing with no ROS dependency
- bounded, lightweight semantic records
- checksum-validated framing before semantic decode
- conservative runtime mapping into `GnssRuntimeState`
- clean separation between parsing and aggregation

It does not guarantee:

- complete receiver support
- complete protocol coverage
- epoch-level satellite history
- transport-level freshness logic

## Deferred Support

The following are intentionally deferred:

- UBX `CFG-*` messages
- full RTCM semantic decoding
- RTCM MSM satellite / signal parsing
- NTRIP
- concrete receiver drivers
- serial transport
- auto-detection
- ROS 2 nodes
- persistent satellite tracking
- timeout pruning
- spoofing classification

Also deferred for later protocol growth:

- Unicore binary `N4` semantic decoding
- Unicore receiver configuration commands
- Quectel semantic decoding
- Septentrio or other vendor-specific semantic layers
- correction relay metrics
- base-station workflow logic

## Layer Boundary

The current intended split is:

```text
gnss_protocols
  -> framing
  -> checksums
  -> typed protocol records
  -> small semantic decoders
  -> conservative runtime mapping helpers

gnss_core
  -> normalized GnssRuntimeState
  -> runtime aggregation
  -> capability/value invariant handling

future gnss_driver
  -> receiver-family normalization
  -> capability declaration
  -> config / autodetect

future gnss_ntrip
  -> correction transport
  -> relay / client behavior
```

For aggregation details, see [docs/runtime_aggregation.md](runtime_aggregation.md).
