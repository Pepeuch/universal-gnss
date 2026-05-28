# GNSS Runtime Aggregation

This document describes how Universal GNSS merges partial normalized runtime
updates into one coherent `universal_gnss::GnssRuntimeState`.

The aggregation layer lives in `gnss_core` as `GnssRuntimeAggregator`.

## Purpose

Different protocol messages expose different slices of GNSS state.

Examples:

- `GGA` can provide fix validity, coordinates, altitude, and HDOP
- `GSA` can provide fix dimension, HDOP, VDOP, and active satellite count
- `GSV` can provide satellites in view and per-sentence CN0 summaries

The aggregator exists so those partial updates can be merged in a portable,
backend-agnostic way before projection into ROS 2, ESP32 adapters, or tools.

## Data Flow

The intended flow is:

```text
protocol record
    ->
partial GnssRuntimeState
    ->
GnssRuntimeAggregator
    ->
coherent GnssRuntimeState
    ->
ROS 2 / ESP32 / tools adapters
```

For the current NMEA path, that looks like:

```text
GGA / RMC / GSA / GSV record
            ->
 per-message runtime mapping helper
            ->
 partial GnssRuntimeState
            ->
   GnssRuntimeAggregator
            ->
 coherent normalized runtime state
```

## Merge Model

Aggregation is field-by-field.

The aggregator:

- holds one current `GnssRuntimeState`
- accepts partial `GnssRuntimeState` updates
- merges only the fields that are explicitly present
- does not invent values that were absent from the update
- preserves the capability/value flag invariant after every merge

This is intentionally not a protocol-specific session tracker. It merges
already-normalized runtime updates, regardless of where they came from.

## Merge Policy

### Core fix and position fields

The following fields are merged directly when the incoming update carries a
value for them:

- `latitude_deg`
- `longitude_deg`
- `altitude_m`

Fix state is merged only when the incoming update explicitly carries a fix type
other than `UNKNOWN`.

Meaning:

- `fix_valid` and `fix_type` do not change unless the producer made an explicit
  fix statement
- position fields do not clear existing values when they are absent from the
  incoming update

### Optional enriched fields

Optional enriched fields follow the core capability/value contract.

A field is only merged when:

- the incoming `capability_flags` contains the field bit
- the incoming `value_flags` contains the same field bit
- the corresponding optional field is actually populated

Examples:

- RTK mode
- horizontal / vertical accuracy
- HDOP / VDOP
- satellites used / visible / tracked
- mean / max CN0
- correction age
- heading
- dual-antenna state
- interference state
- jamming state

If a producer sets a value in the struct but does not set the matching value
flag, the aggregator treats that field as unavailable and does not overwrite the
existing aggregate value.

### Capability and value flag handling

Capabilities accumulate across merged updates.

Meaning:

- if one runtime path can provide `HDOP` and another can provide `mean CN0`,
  the aggregate state may advertise both capabilities
- capability metadata can grow even when a specific update does not carry a new
  value

Value flags are not copied blindly from the incoming update.

Instead:

- invalid incoming value bits are sanitized by ignoring bits that are not also
  present in the incoming capability set
- aggregate `value_flags` are recomputed from the merged fields after each
  update

This preserves the core invariant:

```text
value_flags must never contain a bit that is absent from capability_flags
```

### Missing fields do not clear state

The aggregator is conservative about absence.

If an incoming partial update omits a field:

- the existing aggregate value is kept
- the field is not cleared
- the matching value flag remains set if the aggregate still has a value

This is important because many GNSS protocols split state across multiple
messages, and not every message repeats every field.

## Timestamp Policy

The aggregator keeps simple per-field recency metadata internally.

Rules:

- newest timestamped update wins for that field
- if the incoming update has no timestamp, arrival order wins
- an untimed update may overwrite a previous value because it is the most recent
  arrival from the caller's point of view

Aggregate timestamp semantics:

- the exported aggregate `timestamp_ns` tracks the newest accepted timestamped
  update
- untimed updates do not invent a timestamp
- untimed updates do not clear an existing aggregate timestamp

This gives deterministic behavior without introducing full freshness logic yet.

## What The Aggregator Guarantees

Current guarantees:

- field-by-field deterministic merge behavior
- no overwrite from optional fields unless capability and value flags allow it
- capability flags may accumulate across updates
- aggregate value flags are recomputed, not blindly copied
- invalid incoming value bits do not leak into the aggregate state
- reset returns the aggregator to a safe default unknown state
- no vendor, driver, or ROS-specific assumptions are required

## What The Aggregator Does Not Do

The aggregator is deliberately narrow.

It does not do:

- vendor-specific logic
- protocol framing or parsing
- transport or UART handling
- ROS 2 conversion
- NTRIP correction handling
- multi-sentence satellite session tracking
- stale timeout pruning
- per-source arbitration
- RTK inference from indirect clues
- RF / jamming / interference inference

If a field is not explicitly present in normalized runtime updates, the
aggregator does not synthesize it.

## Current Layer Boundary

The current intended split is:

```text
gnss_protocols
  -> typed protocol records
  -> protocol-specific runtime mapping helpers

gnss_core
  -> GnssRuntimeState
  -> GnssRuntimeAggregator
  -> capability/value invariant enforcement

gnss_ros2 / gnss_esp32 / gnss_tools
  -> projection of the coherent runtime state into platform-specific outputs
```

Future driver layers may feed the same aggregator with updates from UBX,
Unicore, Quectel, RTCM-derived status, or other normalized sources, but the
aggregation contract should remain the same.
