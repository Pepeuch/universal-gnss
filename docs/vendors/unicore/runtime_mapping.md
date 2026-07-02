# Unicore Runtime Mapping

This document freezes the current Unicore runtime-mapping policy used by
`gnss_protocols`.

The goal is to keep Unicore support portable and conservative:

- parse documented Unicore messages conservatively
- project only fields that map cleanly into `universal_gnss::GnssRuntimeState`
- avoid Mowgli-specific diagnostics schemas, launch scripts, and ROS 2 glue
- defer broader binary `N4` semantic decode and driver-specific arbitration
- defer receiver configuration and driver-specific arbitration

## Current Coverage

Current parsed Unicore messages:

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

Current mixed NMEA sentences accepted on a Unicore runtime stream:

- `GSV` for conservative tracked-from-parsed-entries counts, visible satellite
  counts, and CN0 enrichment
- `GGA` as conservative fix/position fallback only when richer Unicore state is
  still missing
- `GST` as conservative accuracy fallback only when richer Unicore state is
  still missing

Not implemented yet:

- broader binary `N4` semantic decode beyond `BESTNAVB` and `PVTSLNB`
- runtime mapping for configuration commands such as `MODE`, `CONFIG`, and `LOG`

Binary `N4` framing now exists separately from the ASCII parser:

- documented `AA 44 B5` sync detection
- documented 24-byte header extraction
- documented little-endian `message_id` / `message_length`
- documented reflected 32-bit CRC validation across the frame minus the final
  CRC field

That binary path now includes two semantic decoders: `BESTNAVB` and
`PVTSLNB`. Those two binary messages now flow through the same portable live
and offline runtime path as the current ASCII position messages.

Current `N4` validity boundary:

- valid frames with unknown/unsupported `message_id` values remain valid
  unknown binary records
- malformed, truncated, or wrong-message semantic decodes are rejected cleanly
- parser/session counters can distinguish valid unknown frames from malformed
  or rejected decodes
- neither path infers receiver model identity, baseline capability, or
  signal-group legality from runtime traffic

## Data Flow

```text
framed Unicore ASCII or binary record
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

The runtime parser and aggregator intentionally do not answer receiver-model
questions such as whether a target supports `dual_antenna_baseline` or which
`CONFIG SIGNALGROUP` combinations are documented. That model/capability gating
lives in the Unicore driver/profile layer so ASCII/Binary runtime parsing stays
independent from config planning.

Some Unicore messages are diagnostics-only rather than direct runtime-state
producers:

- `JAMSTATUSA` and `FREQJAMSTATUSA` also emit portable receiver diagnostics
- `HWSTATUSA` emits a conservative portable hardware diagnostic
- `AGCA` is currently parsed semantically only

The first portable consumer of these mappings now exists in
`gnss_driver::UnicoreSession`:

- frame Unicore ASCII input
- frame Unicore binary `N4` input
- parse supported messages
- turn them into partial `GnssRuntimeState` updates
- merge them through `GnssRuntimeAggregator`

When a Unicore receiver emits a mixed stream of proprietary records and NMEA,
the session keeps the richer proprietary runtime sources authoritative:

- `BESTNAVA`, `BESTNAVB`, `PVTSLNA`, `PVTSLNB`, and `RTKSTATUSA` remain the
  primary fix / RTK / position / baseline sources, with `heading_deg`
  preserved only as a compatibility projection where applicable
- `GSV` is used to fill conservative `satellites_tracked` (from parsed
  per-satellite entries), `satellites_visible`, and CN0 metrics when the
  current proprietary satellite summaries are missing or sparse
- `GGA` and `GST` are fallback-only and do not override already-known richer
  Unicore fix, position, or accuracy fields

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
- documented baseline solution fields `heading_type`, `heading_length`,
  `heading_degree`, and `pitch` -> canonical `baseline_solution_status`,
  `dual_antenna_baseline`, `baseline_length_m`, `baseline_azimuth_deg`, and
  `baseline_pitch_deg`, then the current public compatibility field
  `heading_deg` only when the baseline solution status is `SOL_COMPUTED`
- `hdop` -> `hdop`

Conservative rules:

- horizontal accuracy is the larger of latitude and longitude sigma
- baseline azimuth is not projected into the current public `heading_deg`
  compatibility field unless the message explicitly reports a computed baseline
  solution
- the compatibility flag `dual_antenna_heading` is mirrored from the canonical
  solved / known-false baseline status during the `v0.6.x` transition window
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

## BESTNAVB

`BESTNAVB` is the first binary `N4` semantic decoder and follows the same
portable runtime policy as `BESTNAVA` where the documented binary fields match.

Current mappings:

- binary `solution status` + `position type` -> fix validity and generic
  `fix_type`
- binary `position type` -> generic `rtk_mode` when documented float / integer
  RTK solution types are present
- binary `lat`, `lon`, `hgt` -> coordinates / altitude
- binary `lat sigma`, `lon sigma` -> horizontal accuracy
- binary `hgt sigma` -> vertical accuracy
- binary `diff_age` -> correction age
- binary `#SVs`, `#solnSVs` -> satellites tracked / used

Current non-mappings:

- `undulation` and `datum id` are parsed semantically but not projected into
  core
- documented velocity, track, latency, and signal-mask fields are intentionally
  ignored in this first binary step
- no heading or azimuth is inferred from `BESTNAVB`
- `BESTNAVB` is routed through `UnicoreSession`, `gnss_replay`,
  `gnss_quality_report`, and JSONL export

## PVTSLNB

`PVTSLNB` is the binary `N4` counterpart to `PVTSLNA` and follows the same
portable runtime policy where the documented fields match.

Current mappings:

- binary `bestpos_type` -> generic `fix_type`
- binary `bestpos_type` -> generic `rtk_mode` when the documented position type
  is float or integer RTK
- binary `bestpos_lat`, `bestpos_lon`, `bestpos_hgt` -> coordinates / altitude
- binary `bestpos_latstd`, `bestpos_lonstd` -> horizontal accuracy
- binary `bestpos_hgtstd` -> vertical accuracy
- binary `bestpos_diffage` -> correction age
- binary `bestpos_svs`, `bestpos_solnsvs` -> satellites tracked / used
- documented binary baseline fields `heading_type`, `heading_length`,
  `heading_degree`, and `pitch` -> canonical `baseline_solution_status`,
  `dual_antenna_baseline`, `baseline_length_m`, `baseline_azimuth_deg`, and
  `baseline_pitch_deg`, then the current public compatibility field
  `heading_deg` only when the baseline solution status is `SOL_COMPUTED`
- binary `hdop` -> `hdop`

Current non-mappings:

- documented PSR position fields are parsed semantically but not projected into
  core yet
- documented velocity, tracked-PRN list, and other DOP fields are
  intentionally left out of the portable runtime in this step
- no RF, jamming, or CN0 state is inferred from `PVTSLNB`
- `PVTSLNB` is routed through `UnicoreSession`, `gnss_replay`,
  `gnss_quality_report`, and JSONL export
- shared `PVTSLNA` / `PVTSLNB` portable fields are expected to stay aligned
  where both documented message variants expose the same semantics

## RTKSTATUSA

`RTKSTATUSA` is used for narrow runtime status enrichment rather than position.

Current mappings:

- documented RTK position type -> generic `fix_type`
- documented RTK position type -> generic `rtk_mode`
- documented dual-antenna status -> canonical `baseline_solution_status` and
  `dual_antenna_baseline`
- documented dual-antenna status -> current public compatibility flag
  `dual_antenna_heading` during the `v0.6.x` transition window

Dual-antenna mapping:

- `within limit` -> `baseline_solution_status=computed`,
  `dual_antenna_baseline=true`
- `not solved` -> `baseline_solution_status=not_solved`,
  `dual_antenna_baseline=false`
- `out of limit` -> `baseline_solution_status=out_of_tolerance`,
  `dual_antenna_baseline=false`
- `not configured` -> `baseline_solution_status=not_configured`,
  `dual_antenna_baseline=false`
- unknown / malformed status -> capabilities may exist, but the values stay
  unset

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

That means `satellites_visible` is expected to come from mixed-stream `GSV`
when the receiver is configured to emit it.

## BESTSATA

`BESTSATA` is the current Unicore ASCII satellite-usage summary message.

Current semantic coverage:

- documented tracked-satellite entry count
- per-satellite constellation/system
- per-satellite satellite identifier
- GLONASS frequency-channel suffix when the documented `slot+channel` or
  `slot-channel` form is present
- documented status text
- documented signal mask

Current runtime mappings:

- tracked entry count -> `satellites_tracked`
- satellites with at least one documented solution-use signal-mask bit ->
  `satellites_used`

Current conservative rules:

- `BESTSATA` does not currently claim `satellites_visible`
- `BESTSATA` does not currently claim `mean_cn0` or `max_cn0`
- no fix, RTK, accuracy, correction-age, heading, or RF state is inferred
- the runtime helper treats the signal mask only as a documented per-satellite
  “used in solution” indicator, not as a signal-quality metric

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
- `BESTNAVB` can do the same from documented binary fields
- `PVTSLNB` can do the same for position / baseline geometry where the
  documented binary baseline status is `SOL_COMPUTED`
- `BESTSATA` can refresh tracked- and used-satellite counts without claiming
  CN0 or visibility
- `RTKSTATUSA` can refresh RTK mode and baseline validity/status
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
