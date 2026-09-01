# Portable Diagnostics

`gnss_core` now provides a small portable diagnostics and health-summary model.

The intent is to give parsers, drivers, session logic, RTCM/NTRIP components,
offline tools, and thin ROS 2 adapters one common way to describe health and
issues without pushing ROS concerns into `gnss_core`.

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

Configuration-category diagnostics are intended for operator-driven config
review/apply workflows. They should report the result of an explicit operator
action, not imply that background or automatic receiver writes are happening.

The first protocol-side consumer is the RTCM correction monitor in
`gnss_protocols`. It uses these portable events to report correction-stream
health as:

- `kOk` when recent RTCM correction activity is present
- `kWarning` when RTCM correction activity is stale
- `kError` when required correction content has not been observed
- `kUnknown` when correction freshness cannot be judged because timestamps are
  unavailable

That RTCM monitor now mixes stream activity with a small semantic observation
surface for decoded RTCM metadata such as `1005` / `1006` base position,
`1007` / `1008` station antenna metadata, `1230` GLONASS code-phase bias state,
and MSM header/correction-stream summary state. It still does not add full MSM
observation decode, LoRa policy, ROS 2
adapters, or GUI-specific presentation concerns.

The next receiver-side correction consumer is the `UBX-RXM-RTCM` helper in the
UBX semantic layer. It emits portable correction events describing whether a
specific RTCM message:

- was accepted by the receiver
- was received but not used
- failed receiver-side CRC validation

This complements the stream-side RTCM monitor:

- the RTCM monitor sees correction traffic on the wire
- `RXM-RTCM` sees whether the receiver actually ingested that traffic

It still does not imply RTK float/fixed state or position quality by itself.

The Unicore session exposes the same receiver-side correction idea from
`RTCMSTATUSA` counters:

- `RTCMSTATUSA` is parsed and counted as receiver RTCM status telemetry
- the latest RTCM message type, base-station id, satellite count, and receiver
  message counter are exposed through Unicore session metrics
- ROS 2 correction diagnostics use those metrics to report that the receiver
  has observed RTCM status

`RTCMSTATUSA` still does not create a fake `correction_age_s` value. Correction
age remains sourced from position messages that document differential age, such
as Unicore `BESTNAVA` / `PVTSLNA`, or from receiver-specific acceptance
messages such as u-blox `RXM-RTCM`.

The first u-blox receiver-health consumer now also lives in the UBX semantic
layer:

- classic `MON-HW` emits portable receiver events for documented antenna states
  and documented jamming states

That `MON-HW` mapping is intentionally conservative:

- it uses only documented antenna and jamming states
- it does not threshold `noisePerMS`, `agcCnt`, or `cwSuppression`
- it does not infer fix, RTK, position, or correction quality
- `MON-HW2` remains semantic-only for now because the local docs expose raw
  imbalance / POST fields without a portable threshold model

The first Unicore receiver-health consumers now live in the Unicore ASCII
semantic layer:

- `JAMSTATUSA` emits a portable receiver event for the documented coarse
  jamming state
- `FREQJAMSTATUSA` emits a portable receiver event for documented per-band
  jamming state
- `HWSTATUSA` emits a portable receiver event only from the documented
  clock-validity flag

Those Unicore events are intentionally conservative:

- they do not infer fix, RTK, position, or correction quality
- they do not project vendor-specific hardware fields into `gnss_core`
- they complement the runtime-state booleans for jamming/interference rather
  than replacing them

The first live network-side consumer is the TCP-backed NTRIP client in
`gnss_ntrip`. It reuses the same RTCM correction monitor while streaming bytes
from a caster, so:

- the live client can surface portable correction health without ROS 2
- future ROS 2, GUI, and tool adapters can map one shared health model
- NTRIP transport logic stays separate from any future diagnostics adapter

## Deferred integrations

The following remain intentionally out of scope for this layer:

- ROS 2 diagnostic publishers / nodes
- Foxglove-specific mapping
- persistent log storage
- metrics/export backends

A ROS 2 `diagnostic_msgs` mapping helper now lives in `gnss_ros2`, but it stays
adapter-only:

- it converts portable `GnssDiagnosticEvent` / `GnssHealthSummary` values into
  ROS message types
- it also projects shared `RtcmSemanticObservations` into stable
  `diagnostic_msgs/DiagnosticStatus` entries for ROS consumers
- it does not add node lifecycle, publisher ownership, or transport policy
- it keeps ROS concerns out of `gnss_core`

The first ROS 2 runtime consumer of that mapping is `universal_gnss_ros2`
`ReceiverNode`. Its node-level diagnostics policy is intentionally thin and
operational:

- invalid startup parameters fail fast with ROS logging instead of silent
  fallback
- serial/TCP open failures keep the node alive when practical and surface as
  transport diagnostics
- lack of incoming bytes after startup becomes a node-level warning
- stale runtime updates become node-level stale diagnostics
- jamming / interference booleans already present in `GnssRuntimeState` are
  forwarded into the portable health summary and then into
  `diagnostic_msgs/DiagnosticArray`
- forwarded `/rtcm` traffic is also summarized through portable RTCM semantic
diagnostics such as `base_station_arp`, `antenna_descriptor`,
`glonass_code_phase_bias`, and `msm_summary`

`universal_gnss_ros2::NtripNode` is the second runtime consumer of the same
mapping. Its node-level diagnostics stay similarly thin:

- caster connection / disconnection / reconnecting states come from the
  low-level `NtripClient`
- correction-stream activity comes from the existing RTCM correction monitor
- missing or stale ROS-side GNSS status input is surfaced only as GGA-source
  diagnostics
- stale GNSS input also suppresses GGA injection so old positions are not reused
- shared RTCM semantic observations are exposed as machine-readable ROS
  diagnostics under `.../rtcm_semantic/*`, including base-station ARP state,
  GLONASS `1230`, and aggregate/per-message MSM summary data

This still stops short of:

- lifecycle-node health orchestration
- automatic reconnect ownership
- GUI-specific diagnostic presentation
