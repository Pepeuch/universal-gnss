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

What NMEA does not do yet:

- `GSA` / `GSV` multi-sentence aggregation
- persistent satellite tracking across epochs
- `VTG`, `ZDA`, proprietary vendor sentences, or NMEA state fusion

### UBX

Implemented semantic messages:

- `NAV-STATUS`
- `NAV-PVT`
- `NAV-SAT`
- `MON-RF`

Implemented behaviors:

- UBX sync and checksum validation through the UBX framer
- fixed-layout semantic decode for the messages above
- conservative `GnssRuntimeState` mapping helpers

Current UBX notes:

- `NAV-STATUS` provides fix-status metadata, differential-solution state, and
  carrier-solution status when valid
- `NAV-PVT` is the main source for normalized fix, position, accuracy, and RTK
  mode
- `NAV-SAT` provides satellites visible / used and CN0 summaries
- `MON-RF` provides documented RF-interference / jamming state only

See [docs/vendors/ublox/runtime_mapping.md](vendors/ublox/runtime_mapping.md)
for the current message-by-message UBX runtime mapping contract.

What UBX does not do yet:

- `CFG-*`
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

What RTCM does not do yet:

- payload semantic decoding
- MSM satellite / signal extraction
- station metadata decode
- correction-age estimation
- runtime-state mapping

### Unicore ASCII

Implemented semantic messages:

- `PVTSLNA`
- `BESTNAVA`
- `RTKSTATUSA`
- `RTCMSTATUSA`
- `SATSINFOA`

Implemented behaviors:

- Unicore ASCII framing through the existing line framer
- fixed-layout semantic decode for the messages above
- conservative `GnssRuntimeState` mapping helpers for position / RTK / heading /
  correction-age fields that are explicitly documented

Current Unicore notes:

- `PVTSLNA` is the richest current Unicore position / heading source
- `BESTNAVA` provides stable position-quality, accuracy, and correction-age
  fields
- `RTKSTATUSA` complements position messages with RTK-mode and dual-antenna
  status
- `SATSINFOA` provides tracked-satellite counts and CN0 summaries
- `RTCMSTATUSA` is parsed semantically but does not project into runtime state
  yet

See [docs/vendors/unicore/runtime_mapping.md](vendors/unicore/runtime_mapping.md)
for the current message-by-message Unicore runtime mapping contract.

What Unicore does not do yet:

- binary `N4`
- receiver configuration commands
- Mowgli-style diagnostics projection
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
horizontal accuracy      UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA
vertical accuracy        UBX NAV-PVT, Unicore PVTSLNA, Unicore BESTNAVA
hdop                     NMEA GGA, NMEA GSA, Unicore PVTSLNA
vdop                     NMEA GSA
satellites_used          NMEA GGA, NMEA GSA, UBX NAV-PVT, UBX NAV-SAT, Unicore PVTSLNA, Unicore BESTNAVA
satellites_visible       NMEA GSV, UBX NAV-SAT
satellites_tracked       Unicore PVTSLNA, Unicore BESTNAVA, Unicore SATSINFOA
mean_cn0 / max_cn0       NMEA GSV, UBX NAV-SAT, Unicore SATSINFOA
heading                  UBX NAV-PVT, Unicore PVTSLNA
interference / jamming   UBX MON-RF
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
- UBX `NAV-PVT` can set normalized RTK mode from documented carrier-solution
  bits
- Unicore `PVTSLNA` and `BESTNAVA` can set RTK mode only from documented
  position-type enums, not from indirect heuristics
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

- Unicore binary `N4`
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
