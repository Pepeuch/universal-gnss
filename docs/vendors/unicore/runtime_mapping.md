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

Not implemented yet:

- `SATSINFOA` semantic parsing
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
- `SATSINFOA` semantic parsing
- Unicore receiver configuration commands
- transport integration
- NTRIP injection
- persistent satellite tracking
- richer correction-session metrics
- ROS 2 nodes
