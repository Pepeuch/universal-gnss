# Portable Diagnostics

`gnss_core` now provides a small portable diagnostics and health-summary model.

The intent is to give parsers, drivers, session logic, RTCM/NTRIP components,
and offline tools one common way to describe health and issues before any ROS 2
adapter exists.

## Scope

- No ROS 2 dependency
- No `diagnostic_msgs`
- No logging framework dependency
- No transport-specific implementation
- Lightweight value types that are easy to unit test

## Core types

`GnssDiagnosticSeverity` provides portable severity levels:

- `kOk`
- `kInfo`
- `kWarning`
- `kError`
- `kStale`
- `kUnknown`

`GnssDiagnosticCategory` groups events by portable concern area:

- `kRuntime`
- `kParser`
- `kTransport`
- `kCorrection`
- `kReceiver`
- `kConfiguration`
- `kTiming`

`GnssDiagnosticEvent` carries one portable event with:

- severity
- category
- string code id
- human-readable message
- optional timestamp in nanoseconds
- optional source label

`GnssHealthSummary` carries one portable summary with:

- overall severity
- fix / RTK / correction availability flags
- receiver / transport / parser health flags
- stale-data flag
- accumulated diagnostic events

## Reuse boundary

This model is intended to be reused by:

- protocol parsers emitting portable parser or timing issues
- receiver/session logic emitting runtime, receiver, or configuration issues
- NTRIP and RTCM work emitting correction health and stale-data events
- offline tools surfacing reusable health summaries without ROS 2

The first protocol-side consumer is the RTCM correction monitor in
`gnss_protocols`. It uses these portable events to report correction-stream
health as:

- `kOk` when recent RTCM correction activity is present
- `kWarning` when RTCM correction activity is stale
- `kError` when required correction content has not been observed
- `kUnknown` when correction freshness cannot be judged because timestamps are
  unavailable

That RTCM monitor only tracks activity, message presence, and timing. It does
not add full RTCM payload decode, LoRa policy, ROS 2 adapters, or GUI-specific
presentation concerns.

The first live network-side consumer is the TCP-backed NTRIP client in
`gnss_ntrip`. It reuses the same RTCM correction monitor while streaming bytes
from a caster, so:

- the live client can surface portable correction health without ROS 2
- future ROS 2, GUI, and tool adapters can map one shared health model
- NTRIP transport logic stays separate from any future diagnostics adapter

## Deferred integrations

The following remain intentionally out of scope for this layer:

- ROS 2 `diagnostic_msgs` adapter
- Foxglove-specific mapping
- persistent log storage
- metrics/export backends

ROS 2 diagnostics should be added later as an adapter that maps these portable
core events into ROS-native types instead of pushing ROS concerns into
`gnss_core`.
