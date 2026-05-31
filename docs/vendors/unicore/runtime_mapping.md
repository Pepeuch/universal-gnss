# Unicore Runtime Mapping

This document freezes the current Unicore ASCII runtime-mapping policy used by
`gnss_protocols`.

The goal is to keep Unicore support portable and conservative:

- parse documented ASCII messages only
- project only fields that map cleanly into `universal_gnss::GnssRuntimeState`
- avoid Mowgli-specific diagnostics schemas, launch scripts, and ROS 2 glue
- defer receiver configuration, binary `N4`, and driver-specific arbitration

## Current Coverage

Current parsed Unicore ASCII messages:

- `PVTSLNA`
- `BESTNAVA`
- `RTKSTATUSA`
- `RTCMSTATUSA`
- `SATSINFOA`
- `JAMSTATUSA`
- `FREQJAMSTATUSA`
- `HWSTATUSA`
- `AGCA`

Not implemented yet:

- binary `N4`
- Unicore configuration commands like `MODE`, `CONFIG`, and `LOG`

## Data Flow

```text
framed Unicore ASCII record
    ->
typed Unicore semantic record
    ->
partial GnssRuntimeState
    ->
GnssRuntimeAggregator
    ->
coherent portable runtime state
```

Each Unicore message can contribute only part of the runtime view. The
aggregator is responsible for merging those pieces.

Some Unicore messages are diagnostics-only rather than direct runtime-state
producers:

- `JAMSTATUSA` and `FREQJAMSTATUSA` also emit portable receiver diagnostics
- `HWSTATUSA` emits a conservative portable hardware diagnostic
- `AGCA` is currently parsed semantically only

The first portable consumer of these mappings now exists in
`gnss_driver::UnicoreSession`:

- frame Unicore ASCII input
- parse supported messages
- turn them into partial `GnssRuntimeState` updates
- merge them through `GnssRuntimeAggregator`

That session layer is transport-agnostic. It does not configure the receiver,
open serial ports, publish ROS topics, or manage reconnects.

## PVTSLNA

`PVTSLNA` is the richest current Unicore ASCII source.

Current mappings:

- `bestpos_type` -> generic `fix_type`
- `bestpos_type` -> generic `rtk_mode` when the documented position type is
  float or integer RTK
- `bestpos_lat`, `bestpos_lon`, `bestpos_hgt` -> coordinates / altitude
- `bestpos_latstd`, `bestpos_lonstd` -> horizontal accuracy
- `bestpos_hgtstd` -> vertical accuracy
- `bestpos_diffage` -> correction age
- `bestpos_svs`, `bestpos_solnsvs` -> satellites tracked / used
- `heading_type` + `heading_degree` -> heading only when heading status is
  `SOL_COMPUTED`
- `hdop` -> `hdop`

Conservative rules:

- horizontal accuracy is the larger of latitude and longitude sigma
- heading is not published unless the message explicitly reports a computed
  heading solution
- no RF, jamming, or correction transport state is inferred

## BESTNAVA

`BESTNAVA` is a stable position-quality message and complements `PVTSLNA`.

Current mappings:

- `p-sol status` + `pos type` -> fix validity and generic `fix_type`
- `pos type` -> generic `rtk_mode` when documented float / integer RTK solution
  types are present
- `lat`, `lon`, `hgt` -> coordinates / altitude
- `lat sigma`, `lon sigma` -> horizontal accuracy
- `hgt sigma` -> vertical accuracy
- `diff_age` -> correction age
- `#SVs`, `#solnSVs` -> satellites tracked / used

Current non-mappings:

- `datum id` is parsed only to confirm the documented `WGS84` value and is not
  projected into core
- velocity and track fields are kept out of core for now
- no heading is inferred from `BESTNAVA`

## RTKSTATUSA

`RTKSTATUSA` is used for narrow runtime status enrichment rather than position.

Current mappings:

- documented RTK position type -> generic `fix_type`
- documented RTK position type -> generic `rtk_mode`
- documented dual-antenna status -> `dual_antenna_heading`

Dual-antenna mapping:

- `within limit` -> `true`
- `not solved`, `out of limit`, `not configured` -> `false`
- unknown / malformed status -> capability may exist, but the value stays unset

Current non-mappings:

- ionosphere-effect flags are parsed but not projected into core
- calculation-status internals are not projected into core

## RTCMSTATUSA

`RTCMSTATUSA` is currently parsed for semantic inspection only.

Current semantic coverage:

- RTCM message type
- message count
- base station id
- satellites in message
- per-band observable counts

Current runtime behavior:

- no direct `GnssRuntimeState` projection yet

Reason:

- the portable core currently does not model per-message RTCM observables
- correction transport metrics belong more naturally in `gnss_ntrip`,
  `gnss_tools`, or a future driver/session layer

## SATSINFOA

`SATSINFOA` is the current Unicore satellite-signal summary message.

Current semantic coverage:

- documented tracked-satellite count
- per-satellite PRN
- azimuth
- elevation
- system identifier
- frequency status
- frequency count
- per-satellite CN0 / SNR summary

Current runtime mappings:

- tracked-satellite count -> `satellites_tracked`
- mean CN0 across parsed tracked satellites -> `mean_cn0`
- max CN0 across parsed tracked satellites -> `max_cn0`

Current CN0 strategy:

- the semantic record keeps one CN0 summary per satellite
- when multiple frequency tuples exist for one satellite, the current parser
  keeps the maximum documented SNR across those tuples
- runtime `mean_cn0` and `max_cn0` are then computed from those per-satellite
  summaries

Current conservative rules:

- `SATSINFOA` does not currently claim `satellites_visible`
- `SATSINFOA` does not currently claim `satellites_used`
- no RTK, correction-age, or RF state is inferred from signal-strength data
- no constellation-specific aggregate fields are projected into core yet

## JAMSTATUSA

`JAMSTATUSA` is the current coarse Unicore jamming-status message.

Current semantic coverage:

- documented position type
- documented `CWRatio`
- documented `CWFlag`

Current runtime mappings:

- `CWFlag == 0` -> `interference_detected = false`,
  `jamming_detected = false`
- `CWFlag == 1` or `2` -> `interference_detected = true`,
  `jamming_detected = true`
- undocumented `CWFlag` values leave the runtime value unset

Current diagnostic behavior:

- `CWFlag == 0` -> portable receiver diagnostic `kOk`
- `CWFlag == 1` -> portable receiver diagnostic `kWarning`
- `CWFlag == 2` -> portable receiver diagnostic `kError`

Conservative rules:

- no fix, RTK, accuracy, or correction state is inferred
- `CWRatio` is preserved semantically, but no portable threshold is inferred
  from it alone

## FREQJAMSTATUSA

`FREQJAMSTATUSA` is the current per-frequency Unicore jamming-status message.

Current semantic coverage:

- documented position type
- `L1CWRatio`, `L1CWFlag`
- `L2CWRatio`, `L2CWFlag`
- `L5CWRatio`, `L5CWFlag`

Current runtime mappings:

- if any documented band reports jamming, `interference_detected` and
  `jamming_detected` become `true`
- if all documented bands explicitly report no jamming, both booleans become
  `false`
- if all band states are unknown, capability may exist but the value stays
  unset

Current diagnostic behavior:

- any strong-jam band -> portable receiver diagnostic `kError`
- any jam band with no strong-jam band -> `kWarning`
- all known bands clear -> `kOk`

Conservative rules:

- no RTK, fix, or correction semantics are inferred from per-band RF status
- no constellation-specific RF aggregate fields are projected into core yet

## HWSTATUSA

`HWSTATUSA` is the current coarse Unicore hardware-status message.

Current semantic coverage:

- `DC09`, `DC10`, `DC18`
- documented `Clockflag`
- documented `ClockDrift`
- documented `hwFlag`
- documented `PLL_LOCK`

Current runtime behavior:

- no direct `GnssRuntimeState` projection yet

Current diagnostic behavior:

- `Clockflag == 0` -> portable receiver diagnostic `kWarning`
- `Clockflag == 1` -> portable receiver diagnostic `kOk`

Conservative rules:

- voltage rails, `hwFlag`, and `PLL_LOCK` are preserved semantically only
- no universal fault threshold is inferred from those fields yet

## AGCA

`AGCA` is the current automatic-gain-control telemetry message.

Current semantic coverage:

- `ANT1L1`, `ANT1L2`, `ANT1L5`
- `ANT2L1`, `ANT2L2`, `ANT2L5`
- `-1` is treated as the documented invalid-channel sentinel

Current runtime behavior:

- no direct `GnssRuntimeState` projection yet

Reason:

- the vendor manual explicitly says AGC values vary with hardware
- generic open-circuit or interference thresholds would therefore be
  non-portable
- `AGCA` is kept as semantic telemetry for future tooling, not a universal
  runtime fault source yet

## Position-Type Mapping

Current generic mapping rules:

- `NONE` -> `kNoFix`
- `SINGLE`, `PSRDIFF`, `SBAS`, PPP-like, and non-RTK computed positions ->
  `kFix`
- `L1_FLOAT`, `IONOFREE_FLOAT`, `NARROW_FLOAT`, `INS_RTKFLOAT` ->
  `kRtkFloat`
- `L1_INT`, `WIDE_INT`, `NARROW_INT`, `INS_RTKFIXED` -> `kRtkFixed`
- `INS` -> `kDeadReckoning`

RTK mode rules:

- float solution types -> `kFloat`
- integer solution types -> `kFixed`
- documented non-RTK solution types -> `kNone`
- unknown solution types -> no RTK value is invented

## Relationship To Aggregation

Unicore messages are expected to merge through `GnssRuntimeAggregator`.

That means:

- each message emits a partial `GnssRuntimeState`
- fields merge independently
- newer timestamped updates win per field
- untimed updates fall back to arrival order
- missing value flags do not erase previously known values

Examples:

- `BESTNAVA` can refresh coordinates, accuracy, and correction age
- `RTKSTATUSA` can refresh RTK mode and dual-antenna state
- `SATSINFOA` can refresh tracked-satellite count and CN0 summaries
- `JAMSTATUSA` and `FREQJAMSTATUSA` can refresh portable
  jamming/interference booleans
- `RTCMSTATUSA` can be retained as a semantic record without touching the core
  runtime state

## What Was Not Extracted From MowgliNext

The following ideas were intentionally not copied into `universal-gnss`:

- ROS 2 runtime builders and adapters
- `/diagnostics` string schemas and Mowgli key names
- start scripts and deployment assumptions
- GUI-facing status shaping
- backend-specific public hacks

Only the documented message meaning and a few conservative runtime-mapping ideas
were reused conceptually.

## Deferred Work

Still intentionally deferred:

- binary `N4`
- constellation-specific satellite statistics
- Unicore receiver configuration commands
- transport integration
- NTRIP injection
- persistent satellite tracking
- richer correction-session metrics
- AGC threshold interpretation beyond documented safe semantics
- ROS 2 nodes
