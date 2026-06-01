# Runtime Routing Audit

This document records the current end-to-end runtime path audit before ROS 2
receiver-node work begins.

Scope:

- parser implementation
- runtime mapping helper implementation
- live session routing
- offline replay routing
- quality-report visibility
- JSONL runtime-export visibility
- current ROS 2 adapter visibility

Date of audit: `2026-06-01`

## Audit Summary

The portable runtime path is now consistent for the currently supported
runtime-producing messages:

```text
protocol bytes
  -> framer + checksum
  -> semantic parser
  -> conservative runtime mapper
  -> session / replay
  -> GnssRuntimeAggregator
  -> quality report / JSONL export / ROS 2 adapters
```

The main gaps found in this audit were:

- Unicore binary `BESTNAVB` / `PVTSLNB` existed as semantic parsers only, but
  were not routed through `UnicoreSession`, `ReceiverSession` auto-detect,
  `gnss_inspect`, `gnss_replay`, `gnss_quality_report`, or `gnss_export`.
- UBX structural naming was incomplete for `NAV-DOP`, `RXM-RTCM`, `ACK-ACK`,
  and `ACK-NAK`.
- `RTCMSTATUSA` was documented as semantic-only, but still produced a
  timestamp-only runtime update in session mode.
- `dual_antenna_heading` existed in the runtime model and ROS 2 status adapter,
  but was missing from JSONL runtime export.

Fixes made in this audit:

- Added Unicore binary N4 detection to the stream detector and mixed-stream
  inspector.
- Routed `BESTNAVB` / `PVTSLNB` through `UnicoreSession`, `ReceiverSession`,
  replay, quality-report final-state reconstruction, and JSONL export.
- Added UBX structural names for `NAV-DOP`, `RXM-RTCM`, `ACK-ACK`, and
  `ACK-NAK`.
- Removed the timestamp-only `RTCMSTATUSA` runtime projection.
- Added `dual_antenna_heading` to JSONL runtime export.

## Runtime Model Audit

`GnssRuntimeState` and `GnssRuntimeAggregator` currently cover all mapped
portable runtime fields:

- fix validity / fix type / RTK mode
- coordinates / altitude
- horizontal / vertical accuracy
- `hdop` / `vdop`
- satellites used / tracked / visible
- mean / max CN0
- correction age
- heading
- dual-antenna heading state
- interference / jamming state

Current intentional model limits:

- no generic speed / course field contract yet
  - `NMEA VTG` remains semantic-only
- no GNSS wall-clock / calendar-time contract yet
  - `NMEA ZDA` remains semantic-only
- no portable receiver-diagnostics adapter in ROS 2 yet
  - correction and hardware diagnostics stay in tools / diagnostics helpers

Current export / adapter notes:

- `gnss_export` now covers every currently mapped runtime field except the raw
  capability / value-flag bitmasks, which remain internal metadata.
- `gnss_quality_report` exposes the final normalized runtime state plus
  correction / diagnostic summaries, but does not try to mirror every runtime
  field into the top-level summary structure.
- ROS 2 visibility currently means:
  - `NavSatFix`: coordinates / altitude / conservative covariance only
  - `GnssStatus`: the broader normalized runtime surface

## Session Notes

- `UbloxSession` is the current live NMEA carrier.
  - `GGA`, `RMC`, `GSA`, `GSV`, and `GST` flow through it.
  - `VTG` and `ZDA` do not, because they are semantic-only today.
- `ReceiverSession` auto-detect is still conservative.
  - UBX selects u-blox
  - Unicore ASCII selects Unicore
  - Unicore binary `N4` now selects Unicore
  - RTCM-only stays undecided
  - NMEA-only still stays undecided; there is no generic NMEA vendor session yet

## Coverage Matrix

Legend:

- `yes` = implemented and visible through that stage
- `diag` = visible only through diagnostics / quality helpers, not runtime
- `status` = visible via ROS 2 `GnssStatus`
- `navsat` = visible via ROS 2 `NavSatFix`
- `no` = intentionally absent at that stage

### NMEA

| Message | Parser | Runtime map | Live session | Replay | Quality report | JSONL | ROS 2 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `GGA` | yes | yes | yes | yes | yes | yes | status + navsat | main generic NMEA position source |
| `RMC` | yes | yes | yes | yes | yes | yes | status + navsat | contributes fix validity and coordinates |
| `GSA` | yes | yes | yes | yes | yes | yes | status | contributes DOP and active-satellite counts |
| `GSV` | yes | yes | yes | yes | yes | yes | status | contributes visible satellites and CN0 summaries |
| `GST` | yes | yes | yes | yes | yes | yes | status + navsat | conservative accuracy only |
| `VTG` | yes | no | no | no | no | no | no | semantic-only until generic speed/course fields exist |
| `ZDA` | yes | no | no | no | no | no | no | semantic-only until GNSS wall-clock/date contract exists |

### UBX

| Message | Parser | Runtime map | Live session | Replay | Quality report | JSONL | ROS 2 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `NAV-PVT` | yes | yes | yes | yes | yes | yes | status + navsat | main u-blox runtime source |
| `NAV-DOP` | yes | yes | yes | yes | yes | yes | status | `hdop` / `vdop` only |
| `NAV-SAT` | yes | yes | yes | yes | yes | yes | status | visible / used satellites and CN0 |
| `NAV-STATUS` | yes | yes | yes | yes | yes | yes | status | fix-status and carrier-solution metadata |
| `MON-RF` | yes | yes | yes | yes | yes | yes | status | portable interference / jamming only |
| `RXM-RTCM` | yes | no | no | no | diag | no | no | receiver-side correction acceptance diagnostics only |
| `ACK-ACK` | yes | no | no | no | no | no | no | config / transaction plumbing only |
| `ACK-NAK` | yes | no | no | no | no | no | no | config / transaction plumbing only |

### Unicore ASCII

| Message | Parser | Runtime map | Live session | Replay | Quality report | JSONL | ROS 2 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `PVTSLNA` | yes | yes | yes | yes | yes | yes | status + navsat | richest current Unicore ASCII runtime source |
| `BESTNAVA` | yes | yes | yes | yes | yes | yes | status + navsat | stable position / accuracy / correction-age source |
| `RTKSTATUSA` | yes | yes | yes | yes | yes | yes | status | RTK + dual-antenna state, no position |
| `RTCMSTATUSA` | yes | no | yes | no | no | no | no | semantic inspection only; no longer produces timestamp-only runtime updates |
| `SATSINFOA` | yes | yes | yes | yes | yes | yes | status | tracked-satellite count + CN0 summary only |
| `BESTSATA` | yes | yes | yes | yes | yes | yes | status | tracked / used counts only; no CN0 or visibility |
| `JAMSTATUSA` | yes | yes | yes | yes | yes | yes | status | runtime RF booleans + diagnostics |
| `FREQJAMSTATUSA` | yes | yes | yes | yes | yes | yes | status | runtime RF booleans + diagnostics |
| `HWSTATUSA` | yes | no | yes | no | diag | no | no | parsed and surfaced as conservative hardware diagnostics only |
| `AGCA` | yes | no | yes | no | no | no | no | semantic-only; thresholds remain hardware-dependent |

### Unicore Binary N4

| Message | Parser | Runtime map | Live session | Replay | Quality report | JSONL | ROS 2 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `BESTNAVB` | yes | yes | yes | yes | yes | yes | status + navsat | binary counterpart to `BESTNAVA` |
| `PVTSLNB` | yes | yes | yes | yes | yes | yes | status + navsat | binary counterpart to `PVTSLNA`; heading gated by documented solution status |

## Remaining Deferred Items

These remain intentionally deferred after the audit:

- ROS 2 receiver node work
- ROS 2 diagnostics adapter work
- generic NMEA live session / NMEA-only vendor auto-selection
- generic speed / course runtime contract for `VTG`
- GNSS wall-clock / date runtime contract for `ZDA`
- broader Unicore binary `N4` semantic decoding beyond `BESTNAVB` / `PVTSLNB`
- AGC threshold interpretation
- GUI / dashboard work
- ESP32 / LoRa / RTK base gateway work

## Stable-but-Intentional Schema Notes

The current JSONL export keeps the previously documented CN0 key names:

- `mean_cn0_dbhz`
- `max_cn0_dbhz`

Those names do not exactly match the C++ field spelling `*_db_hz`, but they are
retained for JSONL schema stability.
