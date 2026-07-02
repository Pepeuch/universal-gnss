# Runtime Routing Audit

This document records the current end-to-end runtime path audit across parser,
session, export, and ROS2 surfaces.

Scope:

- parser implementation
- runtime mapping helper implementation
- live session routing
- offline replay routing
- quality-report visibility
- JSONL runtime-export visibility
- current ROS 2 adapter visibility

Date of audit: `2026-07-02`

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
- public baseline-specific runtime/ROS2 fields did not exist yet, so
  dual-antenna semantics were only partially visible through compatibility
  fields such as `heading_deg` and `dual_antenna_heading`.

Fixes made in this audit:

- Added Unicore binary N4 detection to the stream detector and mixed-stream
  inspector.
- Routed `BESTNAVB` / `PVTSLNB` through `UnicoreSession`, `ReceiverSession`,
  replay, quality-report final-state reconstruction, and JSONL export.
- Added UBX structural names for `NAV-DOP`, `RXM-RTCM`, `ACK-ACK`, and
  `ACK-NAK`.
- Removed the timestamp-only `RTCMSTATUSA` runtime projection.
- Added canonical dual-antenna baseline runtime fields and ROS2 `GnssStatus`
  projection, while preserving `heading_deg` / `dual_antenna_heading`
  compatibility for `v0.6.x`.

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
- heading compatibility
- dual-antenna heading compatibility state
- dual-antenna baseline validity
- baseline azimuth / pitch / length
- baseline solution status
- interference / jamming state

Current intentional model limits:

- no generic speed / course field contract yet
  - `NMEA VTG` remains semantic-only
- no GNSS wall-clock / calendar-time contract yet
  - `NMEA ZDA` remains semantic-only
- runtime state does not map directly to ROS diagnostics
  - correction and hardware diagnostics flow through the portable
    `GnssHealthSummary` / `GnssDiagnosticEvent` model instead

Current export / adapter notes:

- `gnss_export` now covers every currently mapped runtime field except the raw
  capability / value-flag bitmasks, which remain internal metadata.
- `gnss_quality_report` exposes the final normalized runtime state plus
  correction / diagnostic summaries, but does not try to mirror every runtime
  field into the top-level summary structure.
- ROS 2 visibility currently means:
  - `NavSatFix`: coordinates / altitude / conservative covariance only
  - `GnssStatus`: the broader normalized runtime surface
  - `DiagnosticArray`: portable health / diagnostic summaries, not direct
    `GnssRuntimeState` fields

## Session Notes

- `NmeaSession` is now the generic live NMEA carrier.
  - `GGA`, `RMC`, `GSA`, `GSV`, and `GST` flow through it as runtime updates.
  - `VTG` and `ZDA` are parsed there as semantic-only records.
- `UbloxSession` still accepts `NMEA` as a mixed-stream companion path.
  - `GGA`, `RMC`, `GSA`, `GSV`, and `GST` flow through it.
- `ReceiverSession` auto-detect is still conservative.
  - UBX selects u-blox
  - Unicore ASCII selects Unicore
  - Unicore binary `N4` now selects Unicore
  - RTCM-only stays undecided
  - NMEA-only stays undecided by default
  - generic NMEA fallback exists only when `allow_generic_nmea_auto_detect`
    is explicitly enabled

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
| `VTG` | yes | no | yes | no | no | no | no | semantic-only in `NmeaSession` until generic speed/course fields exist |
| `ZDA` | yes | no | yes | no | no | no | no | semantic-only in `NmeaSession` until GNSS wall-clock/date contract exists |

### UBX

| Message | Parser | Runtime map | Live session | Replay | Quality report | JSONL | ROS 2 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `NAV-PVT` | yes | yes | yes | yes | yes | yes | status + navsat | main u-blox runtime source |
| `NAV-DOP` | yes | yes | yes | yes | yes | yes | status | `hdop` / `vdop` only |
| `NAV-SAT` | yes | yes | yes | yes | yes | yes | status | visible / used satellites and CN0 |
| `NAV-STATUS` | yes | yes | yes | yes | yes | yes | status | fix-status and carrier-solution metadata |
| `MON-HW` | yes | yes | yes | yes | yes | yes | status | classic hardware payload only; antenna + jamming diagnostics plus conservative RF booleans |
| `MON-HW2` | yes | no | no | no | no | no | no | semantic-only extended hardware status; no portable threshold model yet |
| `MON-RF` | yes | yes | yes | yes | yes | yes | status | portable interference / jamming only |
| `RXM-RTCM` | yes | no | no | no | diag | no | no | receiver-side correction acceptance diagnostics only |
| `ACK-ACK` | yes | no | no | no | no | no | no | config / transaction plumbing only |
| `ACK-NAK` | yes | no | no | no | no | no | no | config / transaction plumbing only |

### Unicore ASCII

| Message | Parser | Runtime map | Live session | Replay | Quality report | JSONL | ROS 2 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `PVTSLNA` | yes | yes | yes | yes | yes | yes | status + navsat | richest current Unicore ASCII runtime source |
| `BESTNAVA` | yes | yes | yes | yes | yes | yes | status + navsat | stable position / accuracy / correction-age source |
| `RTKSTATUSA` | yes | yes | yes | yes | yes | yes | status | RTK + baseline validity/status, no position |
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
| `PVTSLNB` | yes | yes | yes | yes | yes | yes | status + navsat | binary counterpart to `PVTSLNA`; baseline geometry and compatibility heading gated by documented solution status |

## Remaining Deferred Items

These remain intentionally deferred after the audit:

- ROS 2 receiver node work
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
