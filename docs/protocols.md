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
  - recognizes NMEA, UBX, Unicore ASCII, Unicore binary `N4`, RTCM3, and noise
    spans
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
- `GGA` also maps standard fix-quality values into normalized `rtk_mode`
  conservatively:
  - `0/1/2/3/6/7/8` -> `kNone`
  - `4` -> `kFixed`
  - `5` -> `kFloat`
- `RMC` currently contributes fix validity and coordinates
- `GSA` contributes DOP and active-satellite information
- `GSV` contributes satellites-in-view and per-sentence CN0 summaries
- `GST` contributes conservative horizontal/vertical accuracy only
- `VTG` currently contributes semantic course/speed parsing only
- `ZDA` currently contributes semantic UTC date/time and local-zone parsing only
- `GST` accuracy now flows through the NMEA session/replay routing paths that
  already consume `GGA` / `RMC` / `GSA` / `GSV`
- the generic NMEA live-session path now routes `GGA` / `RMC` / `GSA` / `GSV`
  / `GST` into normalized runtime state too
- `VTG` and `ZDA` are parsed by the generic NMEA session for semantic/metrics
  purposes only
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
- `MON-HW`
- `MON-HW2`
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
- `MON-HW` provides documented antenna / receiver-health diagnostics and
  conservative jamming booleans when the classic hardware payload is available
- `MON-HW2` is parsed as documented extended low-level hardware status only; it
  is not thresholded into the portable runtime or diagnostics model yet
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
- richer RF diagnostics or spoofing classification beyond documented
  `MON-HW` / `MON-RF` state

### RTCM3

Implemented behaviors:

- RTCM3 framing
- CRC24Q validation
- 12-bit message type extraction from validated frames
- lightweight classification helpers
- base-station ARP decode for `1005` / `1006`
- station antenna descriptor decode for `1007` / `1008`
- GLONASS code-phase bias decode for `1230`
- portable MSM header/summary decode for supported RTCM MSM families

Current RTCM classifications:

- station ARP / base position: `1005`, `1006`
- station antenna descriptor / optional serial: `1007`, `1008`
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
- latest decoded base-station ARP ECEF position from `1005` / `1006`
- latest decoded station-owned antenna descriptor, setup id, and optional serial
  from `1007` / `1008`
- semantic observations for decoded RTCM content, currently base-station ARP
  position, station antenna descriptor, GLONASS code-phase bias, and MSM
  correction-stream summary
- `1230` seen / decoded / valid state, station id, age, mask, and decode
  success/failure counters through the correction monitor
- MSM seen / decoded state, last age, constellations seen, latest station id,
  variant, satellite count, signal count, cell count, and per-message decode
  success/failure counters through the correction monitor
- portable correction-health summaries for later NTRIP / ROS 2 / GUI reuse

Current RTCM semantic decode coverage:

- `1005`: reference-station ARP ECEF position
- `1006`: reference-station ARP ECEF position plus antenna height
- `1007`: reference-station antenna descriptor and setup id
- `1008`: `1007` metadata plus optional antenna serial number
- `1230`: GLONASS code-phase bias indicator, signal mask, and optional L1/L2
  bias values
- MSM `1071..1137`: portable header/correction-stream summary with station id,
  constellation, MSM variant, and satellite / signal / cell counts

Current RTCM decode policy:

- decoded RTCM semantics stay in correction-stream metadata, not in
  `GnssRuntimeState`
- ECEF coordinates are exposed only as base-station metadata
- antenna descriptors are static station metadata, not correction-health input
- `1230` is exposed through correction/RTCM state, not as direct-navigation
  rover runtime state
- MSM summary stays at the correction-stream/header level for now; observation
  payload values are not mapped yet

What RTCM does not do yet:

- broader semantic decoding beyond `1005` / `1006` / `1007` / `1008` / `1230`
  / MSM summary
- MSM pseudorange / carrier-phase / Doppler extraction
- broader station metadata decode beyond base-station ARP and antenna descriptor
- correction-age estimation
- runtime-state mapping
- LoRa filtering

The RTCM correction monitor now combines stream activity with a small semantic
observation layer. It still does not attempt full RTCM coverage, but it does
provide one common portable interface for decoded RTCM metadata so tools,
diagnostics, and later integrations do not need message-specific `1230` or MSM
tool logic.

### Unicore

Implemented binary foundation:

- binary `N4` framing
- documented `AA 44 B5` sync detection
- documented 24-byte header extraction
- documented 32-bit CRC validation
- binary frame container with header metadata and raw payload bytes

Implemented semantic messages:

- `BESTNAVB`
- `PVTSLNB`
- `PVTSLNA`
- `BESTNAVA`
- `BESTSATA`
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
  correction-age / baseline fields that are explicitly documented
- current live/offline routing for `BESTNAVB` and `PVTSLNB` through
  `UnicoreSession`, `ReceiverSession` auto-detect, `gnss_inspect`,
  `gnss_replay`, `gnss_quality_report`, and `gnss_export`

Current Unicore notes:

- binary `N4` framing currently provides:
  - sync detection
  - documented header extraction
  - payload-length extraction
  - CRC validation
  - unknown binary message ids preserved as valid binary frames when frame
    integrity is valid
  - semantic decoding for `BESTNAVB` and `PVTSLNB`
- wrong-message-id, truncated-payload, and other malformed binary semantic
  decodes are rejected cleanly; valid-but-unsupported `N4` frames stay unknown
  and do not invent runtime fields
- `BESTNAVB` and `PVTSLNB` are the first binary semantic decoders
- `BESTNAVB` mirrors the documented conservative runtime projection of
  `BESTNAVA` for fix, RTK, position, accuracy, correction age, and satellite
  counts
- `PVTSLNB` mirrors the documented conservative runtime projection of
  `PVTSLNA` for fix, RTK, position, accuracy, baseline azimuth/length/pitch,
  compatibility heading, correction age, and satellite counts
- shared ASCII/Binary portable fields now follow the same runtime-mapping
  contract where both documented message variants expose the same data
- `PVTSLNA` is the richest current Unicore position / baseline source
- `BESTNAVA` provides stable position-quality, accuracy, and correction-age
  fields
- `RTKSTATUSA` complements position messages with RTK-mode and canonical
  baseline solution status / validity state
- `BESTSATA` provides tracked-satellite counts and signal-mask-derived
  used-satellite counts
- `SATSINFOA` provides tracked-satellite counts and CN0 summaries
- `JAMSTATUSA` and `FREQJAMSTATUSA` provide documented Unicore-side
  interference / jamming state
- `HWSTATUSA` provides conservative hardware diagnostics from documented clock
  validity only
- `AGCA` is parsed semantically, but stays out of the portable runtime because
  AGC thresholds are explicitly hardware-dependent in the vendor manual
- `RTCMSTATUSA` is parsed semantically but does not project into runtime state
  yet
- `BESTNAVB` and `PVTSLNB` now participate in the same conservative runtime
  path as the ASCII Unicore position messages:
  - structural detection in `gnss_inspect`
  - runtime routing in `UnicoreSession`
  - offline replay / quality-report / JSONL export visibility through
    `gnss_replay`
- ASCII remains the richest Unicore runtime source today because most binary
  `N4` semantic messages beyond `BESTNAVB` / `PVTSLNB` are still deferred
- receiver model identity, `dual_antenna_baseline` capability, and
  `CONFIG SIGNALGROUP` legality are driver/profile concerns; the protocol layer
  does not infer them from runtime traffic

See [docs/vendors/unicore/runtime_mapping.md](vendors/unicore/runtime_mapping.md)
for the current message-by-message Unicore runtime mapping contract.
See [runtime_audit.md](runtime_audit.md) for the end-to-end routing audit across
parsers, sessions, replay, quality reporting, JSONL export, and current ROS 2
adapters.

What Unicore does not do yet:

- broader binary `N4` semantic decoding beyond `BESTNAVB` / `PVTSLNB`
- receiver configuration commands
- AGC threshold interpretation beyond documented safe semantics
- constellation-specific aggregate statistics

## Runtime Mapping Coverage

The table below describes which normalized runtime fields are currently filled
by protocol-specific mapping helpers.

```text
Runtime field            Current protocol sources
---------------------------------------------------------------
fix_valid                NMEA GGA, NMEA RMC, NMEA GSA, UBX NAV-STATUS, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB, Unicore RTKSTATUSA
fix_type                 NMEA GGA, UBX NAV-STATUS, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB, Unicore RTKSTATUSA
rtk_mode                 NMEA GGA, UBX NAV-STATUS, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB, Unicore RTKSTATUSA
latitude / longitude     NMEA GGA, NMEA RMC, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB
altitude                 NMEA GGA, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB
horizontal accuracy      NMEA GST, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB
vertical accuracy        NMEA GST, UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB
hdop                     NMEA GGA, NMEA GSA, UBX NAV-DOP, Unicore PVTSLNA, Unicore PVTSLNB
vdop                     NMEA GSA, UBX NAV-DOP
satellites_used          NMEA GGA, NMEA GSA, UBX NAV-PVT, UBX NAV-SAT, Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB, Unicore BESTSATA
satellites_visible       NMEA GSV, UBX NAV-SAT
satellites_tracked       Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB, Unicore BESTSATA, Unicore SATSINFOA
mean_cn0 / max_cn0       NMEA GSV, UBX NAV-SAT, Unicore SATSINFOA
heading                  UBX NAV-PVT, Unicore PVTSLNA, Unicore PVTSLNB
dual_antenna_baseline    Unicore PVTSLNA, Unicore PVTSLNB, Unicore RTKSTATUSA
baseline_azimuth_deg     Unicore PVTSLNA, Unicore PVTSLNB
baseline_pitch_deg       Unicore PVTSLNA, Unicore PVTSLNB
baseline_length_m        Unicore PVTSLNA, Unicore PVTSLNB
baseline_solution_status Unicore PVTSLNA, Unicore PVTSLNB, Unicore RTKSTATUSA
interference / jamming   UBX MON-HW, UBX MON-RF, Unicore JAMSTATUSA, Unicore FREQJAMSTATUSA
correction_age           Unicore PVTSLNA, Unicore PVTSLNB, Unicore BESTNAVA, Unicore BESTNAVB
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

- NMEA `GGA` can set a generic fix, coordinates, and standard RTK mode from
  documented `fix_quality` values only; it still keeps `fix_type` conservative
  as generic `fix` / `no_fix`
- NMEA `GST` can set horizontal and vertical accuracy, but it does not imply
  fix validity, RTK mode, satellite counts, or covariance orientation in the
  portable runtime model
- `GST` horizontal accuracy currently uses `max(latitude_std_dev_m,
  longitude_std_dev_m)` as a conservative single-value summary
- UBX `NAV-PVT` can set normalized RTK mode from documented carrier-solution
  bits
- UBX `NAV-DOP` can set `hdop` / `vdop`, but it does not imply fix validity,
  RTK mode, or position
- Unicore `PVTSLNA`, `PVTSLNB`, `BESTNAVA`, and `BESTNAVB` can set RTK mode
  only from documented position-type enums, not from indirect heuristics
- Unicore runtime traffic does not imply that a receiver model supports
  `dual_antenna_baseline` or any specific `CONFIG SIGNALGROUP` selection; that
  capability gating lives in the driver/profile layer
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
- broader RTCM semantic decoding
- RTCM observation payload extraction beyond the current portable MSM summary
- NTRIP
- concrete receiver drivers
- serial transport
- auto-detection
- ROS 2 nodes
- persistent satellite tracking
- timeout pruning
- spoofing classification

Also deferred for later protocol growth:

- most Unicore binary `N4` semantic decoding beyond `BESTNAVB` and `PVTSLNB`
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
