# u-blox Runtime Mapping

This document describes how the current UBX semantic layer maps parsed u-blox
messages into `universal_gnss::GnssRuntimeState`.

The goal is to make the current mapping contract explicit before additional UBX
messages, driver logic, or receiver configuration are added.

## Scope

Current UBX semantic coverage:

- `NAV-PVT`
- `NAV-SAT`
- `NAV-STATUS`
- `MON-RF`
- `RXM-RTCM`

Current non-goals:

- receiver configuration
- transport or serial handling
- ROS 2 projection
- NTRIP
- persistent satellite history
- spoofing classification

## Design Goals

The UBX mapping layer is intentionally conservative.

Rules:

- only documented UBX fields are mapped
- only fields already represented generically in `GnssRuntimeState` are
  projected into the portable core
- no RTK inference is made from indirect hints when a documented carrier
  solution field is absent
- no correction age is inferred from UBX navigation status messages
- non-RF messages do not synthesize RF or jamming state
- vendor-specific details stay in typed UBX semantic records unless the core
  already has a matching generic field

## Data Flow

The current UBX path is:

```text
UBX frame
   ->
typed UBX semantic record
   ->
partial GnssRuntimeState
   ->
GnssRuntimeAggregator
   ->
coherent runtime state
```

Each UBX message contributes only the fields it actually knows about.

The first portable consumer of these mappings now exists in
`gnss_driver::UbloxSession`.

That session layer:

- accepts mixed `UBX`, `NMEA`, and `RTCM3` byte streams
- routes supported `UBX` messages into these mapping helpers
- optionally accepts conservative `NMEA` runtime updates
- keeps `RTCM3` as correction metadata only
- merges all runtime updates through `GnssRuntimeAggregator`

It does not configure the receiver, send `CFG-*`, process `ACK/NAK`, manage
survey-in control, or own transport/reconnect policy.

This means:

- `NAV-STATUS` can contribute fix and RTK-status metadata without coordinates
- `NAV-PVT` can contribute coordinates and accuracy without erasing other
  fields
- `NAV-SAT` can contribute visibility and CN0 without changing fix state
- `MON-RF` can contribute jamming/interference without touching position fields
- `RXM-RTCM` can contribute receiver-side RTCM acceptance diagnostics without
  touching runtime fix or position fields

## Message Mapping

### NAV-PVT

`NAV-PVT` is currently the richest UBX runtime source.

Mapped fields:

- `fix_valid`
- `fix_type`
- `latitude_deg`
- `longitude_deg`
- `altitude_m`
- `horizontal_accuracy_m`
- `vertical_accuracy_m`
- `satellites_used`
- `rtk_mode`
- `heading_deg` when `headVehValid` is set

Current mapping details:

- `fix_valid` is only true when the message indicates a usable navigation
  solution:
  - `fixType` is `2D`, `3D`, or `GNSS + dead reckoning combined`
  - `gnssFixOK` is true
  - `invalidLlh` is false
- `fix_type` is mapped conservatively into generic core values:
  - `no fix` -> `kNoFix`
  - `dead reckoning only` -> `kDeadReckoning`
  - `2D` / `3D` -> `kFix` when valid
  - `GNSS + dead reckoning combined` -> `kFix` when valid, otherwise
    `kDeadReckoning`
  - `time only` -> `kNoFix`
- altitude currently uses `hMSL`, not ellipsoid height
- `hAcc` and `vAcc` are converted from millimeters to meters
- `numSV` is mapped to `satellites_used`
- `carrSoln` is mapped to normalized `rtk_mode`
- heading is mapped from `headVeh` only when `headVehValid` is true and the
  position itself is valid

Current capability behavior:

- `NAV-PVT` advertises support for:
  - `rtk_mode`
  - `horizontal_accuracy`
  - `vertical_accuracy`
  - `satellites_used`
- it advertises `heading` only when a valid heading can actually be exposed

What is intentionally not inferred from `NAV-PVT`:

- correction age
- RF / jamming state
- spoofing state
- dual-antenna state
- covariance matrices
- generic RTK truth from anything other than documented `carrSoln`
- speed or course in the portable runtime core

### NAV-SAT

`NAV-SAT` is currently the main UBX source for satellite visibility and CN0
summaries.

Mapped fields:

- `satellites_visible`
- `satellites_used`
- `mean_cn0_db_hz`
- `max_cn0_db_hz`

Current mapping details:

- `numSvs` maps to `satellites_visible`
- the count of satellites with `used_in_navigation` maps to `satellites_used`
- `mean_cn0_db_hz` and `max_cn0_db_hz` are computed from per-satellite `cno`
  values
- `cno == 0` is ignored for CN0 summary calculations

Per-satellite details currently retained only in the semantic record:

- `gnssId`
- `svId`
- `cno`
- `elev`
- `azim`
- `qualityInd`
- `used_in_navigation`
- `healthy`

What `NAV-SAT` does not do yet:

- persistent satellite tracking across epochs
- per-constellation runtime fields
- signal-level history
- fix-quality inference
- RTK inference

### NAV-STATUS

`NAV-STATUS` complements `NAV-PVT` by carrying fix-status metadata that can be
useful even when no position/accuracy values are projected from the message.

Parsed fields:

- `iTOW`
- `gpsFix`
- `flags`
- `fixStat`
- `flags2`
- `ttff`
- `msss`
- derived booleans:
  - `gnss_fix_ok`
  - `differential_solution`
  - `carrier_solution_valid`
- derived carrier solution:
  - `none`
  - `float`
  - `fixed`

Mapped fields:

- `fix_valid`
- `fix_type`
- `rtk_mode`

Current mapping details:

- `gpsFix` is mapped using the same conservative generic fix policy as
  `NAV-PVT`
- `gpsFixOk` controls whether `2D`, `3D`, or combined modes become a valid
  generic fix
- `diffSoln` is parsed and preserved in the semantic record, but it is not
  projected into the core because there is no generic portable field yet for
  "differential corrections applied"
- `carrSoln` is only projected when `carrSolnValid` is set

Capability / value behavior for `carrSolnValid`:

- `NAV-STATUS` always advertises `rtk_mode` capability because this message
  family can expose a carrier-solution state
- if `carrSolnValid == 0`, the capability bit is set but the value bit is not
  set
- this means: "supported by this path, but unknown for this sample"
- if `carrSolnValid == 1`, the value bit is set and `rtk_mode` becomes one of:
  - `kNone`
  - `kFloat`
  - `kFixed`

How `NAV-STATUS` complements `NAV-PVT`:

- it can carry fix/RTK metadata even when no coordinates are being projected
- it can refresh fix/RTK fields without erasing older position or accuracy
  values
- it does not override `NAV-PVT` coordinates, altitude, or accuracy because it
  does not publish those fields into the runtime state

What `NAV-STATUS` intentionally does not do:

- no position mapping
- no accuracy mapping
- no satellite count mapping
- no CN0 mapping
- no RF mapping
- no RTK inference from `diffSoln` alone

### MON-RF

`MON-RF` is currently the UBX source for normalized RF health state.

Mapped fields:

- `interference_detected`
- `jamming_detected`

Current mapping details:

- `MON-RF` advertises `interference` and `jamming` capability when at least one
  RF block exists
- if any RF block reports `warning` or `critical` jamming state:
  - `interference_detected = true`
  - `jamming_detected = true`
- if at least one RF block reports a known non-problem state and none report
  warning or critical:
  - `interference_detected = false`
  - `jamming_detected = false`
- if all RF blocks are `unknown`, capability remains present but the value bits
  stay clear

Why the boolean projection is intentionally simple:

- the portable core currently exposes generic boolean RF-health flags
- the toolchain does not yet have a stable cross-vendor RF severity model
- warning/critical states are the portable "problem detected" boundary today

Fields parsed but not projected into the core yet:

- `noisePerMS`
- `agcCnt`
- `cwSuppression`
- `postStatus`
- antenna status / power details

Those remain in the semantic record for future vendor-aware tooling or a later
portable RF model.

### RXM-RTCM

`RXM-RTCM` is treated as a receiver-side correction-status message, not as a
runtime-state source.

Parsed fields:

- `version`
- `flags`
- `crcFailed`
- `msgUsed`
- `subType`
- `refStation`
- `msgType`

Current mapping behavior:

- `RXM-RTCM` maps into portable correction diagnostic events
- `crcFailed == 1` maps to a correction warning
- `msgUsed == used` maps to a correction `kOk` event
- `msgUsed == not used` maps to a correction warning
- `msgUsed == unknown` maps to a correction info event

Why it stays out of `GnssRuntimeState`:

- it does not provide a generic fix state
- it does not prove RTK float/fixed by itself
- it reports whether the receiver ingested an RTCM message, not whether the
  normalized solution quality improved

How it complements the RTCM correction monitor:

- the RTCM correction monitor answers: "did RTCM frames arrive on the stream?"
- `RXM-RTCM` answers: "did the receiver parse and use that RTCM frame?"

What `RXM-RTCM` intentionally does not do:

- no RTK-mode inference
- no position or accuracy mapping
- no correction-age mapping
- no receiver-configuration or forwarding logic

## Conservative Rules

The current UBX mapping follows these guardrails:

- no RTK mode without documented carrier-solution fields
- no correction age from `NAV-PVT`, `NAV-STATUS`, `NAV-SAT`, or `MON-RF`
- no RTK inference from `RXM-RTCM` alone
- no RF state from `NAV-PVT`, `NAV-STATUS`, or `NAV-SAT`
- no accuracy from `NAV-STATUS`
- no vendor-specific public fields in `GnssRuntimeState`
- no forced clearing of unrelated runtime fields from partial UBX updates

Examples:

- `diffSoln` does not imply `rtk_mode = float`
- `NAV-STATUS` does not erase coordinates from an earlier `NAV-PVT` update
- `NAV-SAT` CN0 values do not imply a valid fix
- `MON-RF` does not imply a degraded position solution directly

## Relationship To Aggregation

Every UBX message currently produces a partial `GnssRuntimeState`.

Those partial states are merged later by `GnssRuntimeAggregator`.

The merge contract matters here:

- fields are merged field-by-field
- only present fields update the aggregate
- newer timestamped updates win where timestamps are available
- untimed updates fall back to arrival order
- missing value flags do not erase previously known values

This means a typical UBX merge can look like:

```text
NAV-STATUS -> fix metadata + optional RTK mode
NAV-PVT    -> coordinates + altitude + accuracy + satellites_used + RTK mode
NAV-SAT    -> satellites_visible + satellites_used + CN0 summary
MON-RF     -> interference/jamming booleans
RXM-RTCM   -> receiver-side correction diagnostics only
```

After aggregation, the coherent runtime state may contain fields from all four
messages without any single UBX message needing to carry the whole model.

For the generic merge rules, see [docs/runtime_aggregation.md](../../runtime_aggregation.md).

## Deferred UBX Work

The following UBX areas are intentionally deferred:

- `CFG-*` messages
- `MON-SPAN`
- richer RF severity models
- spoofing-state projection
- persistent satellite tracking
- constellation-specific runtime fields
- receiver configuration
- transport / serial integration
- auto-detection
- driver-layer source arbitration
