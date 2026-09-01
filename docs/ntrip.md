# NTRIP Foundation

This document describes the current scope of `gnss_ntrip`.

Today this layer is intentionally small. It provides:

- portable NTRIP configuration types
- request/header generation
- basic authentication helpers
- portable sourcetable parsing helpers
- GGA injection policy types
- portable GGA sentence generation
- explicit synchronous GGA injection support
- reconnect/backoff policy types
- connection and RTCM-flow metrics models
- a first synchronous TCP-backed live client foundation
- a first live NTRIP monitor CLI in `gnss_tools`

## Purpose

`gnss_ntrip` is the transport-adjacent foundation for future correction
transport, but it is not the transport implementation itself.

Current responsibilities:

- normalize caster connection settings
- build deterministic NTRIP GET requests
- parse sourcetable text into portable stream / caster / network records
- open a synchronous TCP connection through `gnss_transport`
- validate the initial NTRIP response header
- model whether periodic GGA injection is enabled
- generate a portable NMEA GGA sentence from `GnssRuntimeState`
- send GGA explicitly through `NtripClient` without owning a timer loop
- model reconnect/backoff decisions without owning a reconnect loop
- track correction-stream metrics independently from ROS 2 or any network stack
- feed incoming correction bytes into the existing RTCM framer and correction monitor
- accept both full HTTP-style NTRIP responses and legacy `ICY 200 OK` caster
  responses seen in real deployments

Current non-responsibilities:

- reconnect loops
- ROS 2-side RTCM forwarding ownership
- serial output
- owning ROS 2 nodes inside `gnss_ntrip`
- ESP32 WiFi or Ethernet integration

## Data Flow

The intended flow is:

```text
NtripConfig
   ->
BuildNtripGetRequest(...)
   ->
TcpClientTransport
   ->
NtripClient
   ->
incoming RTCM byte stream
   ->
gnss_protocols RTCM framer + parser helpers
   ->
RtcmCorrectionMonitor + NtripConnectionMetrics updates
   ->
future ROS 2 / ESP32 / tools integrations
```

`gnss_ntrip` stays above raw socket mechanics and below application-specific
runtime orchestration.

## Local Caster / Base Mode Boundary

UGA-147 is `IMPLEMENTED`: the Linux MVP provides a synchronous local TCP/NTRIP
caster. It accepts exactly one active RTCM source at a time, identified by an
explicit `source_id` and non-zero incarnation. Activating or ending a source
clears all caches; restarting a source requires a new incarnation.

- `LocalRtcmCaster::Start()` binds one configurable local TCP endpoint and
  mountpoint; `Poll()` accepts a minimal `GET /mountpoint` request and replies
  `ICY 200 OK` before raw RTCM bytes.
- Multiple clients are supported. Each has a fixed output buffer; a full buffer
  disconnects only that client, never stalls `PublishFrame()` or another client.
- Only CRC-valid input frames are served. `1005`/`1006` are cached exclusively
  for clients joining the active incarnation. MSM and `1230` are live only;
  they are never replayed from cache. `EndSource()` immediately clears static
  caches and invalidates the dynamic stream.
- Authentication, server TLS, sourcetable serving, multiple mountpoints,
  external-caster proxying, UDP, multicast, asynchronous frameworks, and cache
  or client persistence remain outside the MVP.

The existing RTCM semantics remain the boundary: `1005`/`1006` are static,
MSM is dynamic/fresh, `1230` is optional, and a source/incarnation transition
cannot mix retained state with a new source.

`gnss_ros2` now provides that first orchestration wrapper through
`universal_gnss_ros2::NtripNode`, but the ownership split stays deliberate:

- `gnss_ntrip` owns `NtripClient`, request generation, GGA injection policy,
  reconnect policy, RTCM extraction, and correction monitoring
- `gnss_ros2` owns subscriptions, timers, diagnostics publishing, launch
  wiring, and the `rtcm` topic bridge into the live receiver path
- `ReceiverNode` can consume the forwarded `rtcm` topic and project the same
  portable RTCM semantic observation surface through ROS diagnostics without
  adding receiver-specific correction logic to downstream apps

The current ROS 2 wrapper also applies one additional runtime policy at the
node boundary: if the subscribed GNSS status becomes stale, it suppresses GGA
injection instead of reusing an old rover position.

## Configuration Model

`NtripConfig` currently includes:

- `host`
- `port`
- `mountpoint`
- `username`
- `password`
- `user_agent`
- `version`
- `send_gga`
- `gga_interval_s`
- `reconnect_policy`

The default user agent is currently `universal-gnss`.

## Request Policy

`BuildNtripGetRequest(...)` currently generates a deterministic HTTP-style GET
request without opening any network connection.

Mountpoint handling:

- leading slashes are normalized to exactly one slash
- an empty mountpoint becomes `/`

Version policy:

- NTRIP v1 uses `HTTP/1.0`
- NTRIP v2 uses `HTTP/1.1`
- NTRIP v2 adds `Host:`
- NTRIP v2 adds `Ntrip-Version: Ntrip/2.0`

Shared request behavior:

- `User-Agent:` is always emitted
- the current format is `User-Agent: NTRIP <user-agent>`
- `Accept: */*` is emitted
- `Connection: close` is emitted
- `Authorization:` is emitted only when at least one credential field is non-empty

## Response Compatibility Policy

The live client now accepts two common response styles:

- full HTTP-style responses such as `HTTP/1.1 200 OK` with normal header
  termination
- legacy NTRIP v1 caster responses such as `ICY 200 OK`

Compatibility notes:

- some real casters send only `ICY 200 OK\r\n` and then start streaming RTCM
  bytes immediately
- some real casters may attach a new client in the middle of an RTCM frame
  rather than on a frame boundary
- `NtripClient` therefore treats `ICY 200 OK` followed by binary payload as a
  valid streaming transition even when no extra blank-line header terminator is
  present

This behavior was validated against a real local caster during the ROS2
end-to-end audit recorded in
[ros2_end_to_end_audit.md](ros2_end_to_end_audit.md).

## Authentication Policy

Basic authentication is modeled separately from sockets.

Current rules:

- the encoded payload is `username:password`
- if both are empty, the auth value and header are omitted
- if only one side is empty, the colon separator is still preserved

This keeps request generation deterministic and testable before any network code
exists.

## Sourcetable Parser

`ntrip_sourcetable.*` now provides a small portable parser for sourcetable text
responses.

Current scope:

- parse `STR`, `CAS`, `NET`, and `ENDSOURCETABLE`
- extract the common typed `STR` fields used for diagnostics and later
  selection workflows
- keep missing fields optional instead of forcing partial defaults
- report malformed lines separately from valid records
- expose helpers for:
  - RTCM stream detection
  - NMEA-required detection
  - MSM capability checks
  - mountpoint lookup
  - filtering RTCM-capable streams

Current non-goals:

- live sourcetable fetch workflow
- automatic mountpoint selection
- GUI or ROS 2 presentation
- ESP32-specific integration

## GGA Injection Policy

The current GGA support is policy plus portable sentence generation.

`GgaInjectionPolicy` currently models:

- enabled vs disabled
- interval in seconds
- whether a runtime position fix is required before sending
- optional `last_sent_timestamp_ns`

`gga_sentence_builder.*` and `BuildNmeaGgaSentence(...)` currently provide:

- portable `$GPGGA` / `$GNGGA` sentence generation from `GnssRuntimeState`
- configurable talker selection with `GPGGA` as the default
- optional externally supplied UTC time, defaulting to `000000.00`
- latitude / longitude formatting with NMEA hemisphere fields
- fix-quality mapping from normalized runtime fix state
- altitude, satellites-used, and HDOP fields when available
- deterministic NMEA checksum generation

That GGA coordinate formatting is protocol-specific:

- it emits NMEA `ddmm.mmmmm` / `dddmm.mmmmm` fields with five decimal places of
  minutes
- it is not a decimal-degree text/JSON surface
- it is therefore tracked separately from the Universal GNSS policy that
  human-readable and machine-readable decimal-degree outputs preserve at least
  9 decimal places

`gga_generator.hpp` remains as a compatibility include for callers that already
use the older generator naming.

`NtripClient` currently provides:

- `SendGga(...)` for explicit synchronous GGA writes
- `MaybeSendGga(...)` for policy-gated GGA writes
- `MaybeInjectGga(...)` for explicit-call streaming-only GGA injection through the reusable injector
- policy tracking through `GgaInjectionPolicy::last_sent_timestamp_ns`
- GGA send metrics and last-error tracking
- `gga_metrics()` for injector-specific attempt / skip / write-error detail

`gga_injector.*` now also provides a small reusable explicit-call helper that:

- evaluates `GgaInjectionPolicy`
- decides whether injection is due at a caller-supplied timestamp
- builds a GGA sentence through the portable sentence builder
- writes through a generic `ByteSink`
- updates `last_sent_timestamp_ns` only after a successful write

This helper intentionally does not own any timer, thread, or autonomous loop.
The caller still owns scheduling and decides when to call it.

`NtripClient::MaybeInjectGga(...)` is the thin integration point on top of that
helper. It only injects once the client is already streaming, and the caller
still owns all scheduling decisions.

What it does not do yet:

- schedule GGA sending automatically
- own periodic timing or scheduling
- integrate with ROS 2 timers or GUI workflows

That separation is deliberate: the policy can be shared later by ROS 2, ESP32,
BlueOS, RTK base, or LoRa-facing components without coupling them to one
transport stack.

## Reconnect Policy Model

`NtripReconnectPolicy` and `NtripReconnectState` provide the portable reconnect
foundation for future live clients.

Current policy behavior:

- enable or disable reconnect scheduling
- schedule the first retry after `initial_delay_ms`
- apply deterministic exponential backoff with `multiplier`
- expose `NextDelay(...)` so callers can inspect the next deterministic delay without starting a loop
- cap the retry delay at `max_delay_ms`
- carry an `exhausted` flag in reconnect state once `max_attempts` has been reached
- keep `jitter_enabled` in the policy model, but leave jitter disabled and deterministic for now
- optionally stop scheduling after `max_attempts`
- optionally reset backoff state after a successful connect

What this does not do yet:

- run a reconnect loop
- own timers or background work
- decide when an application should actually call `Connect()`

That split is intentional so the same policy can be reused later by CLI tools,
ROS 2 nodes, ESP32 targets, and dashboard integrations.

`NtripClient` already exposes `reconnect_state()` and updates it on retry-worthy
failures and successful connections, but it still leaves all actual retry timing
and `Connect()` calls to the outer application.

## Metrics Model

`NtripConnectionMetrics` currently tracks:

- total bytes received
- total bytes sent
- request-sent flag
- response-received flag
- total RTCM frames seen
- valid RTCM frames received
- invalid RTCM frames
- last RTCM message type
- connected / disconnected state
- reconnect count for scheduled retry attempts
- last error enum

The current model is compatible with the existing RTCM parser/tooling:

- `gnss_protocols` already validates, classifies, and semantically decodes the
  current RTCM subset
- `rtcm_inspect` and `gnss_inspect` already expose RTCM stream summaries
- future NTRIP clients can update these same counters while feeding the RTCM
  stream into runtime or tool adapters

### Local correction-arrival age

`NtripClient::EstimatedCorrectionArrivalAgeS()` is a portable local estimate of
elapsed `steady_clock` time since the client accepted its last decoded,
station-owned RTCM MSM observation. It is unavailable before that observation
and after disconnect, failure, reconnect, source change, or RTCM station
replacement. It is independent for each NTRIP client/stream; silence makes the
value grow rather than making it a health verdict.

This is not `GnssRuntimeState::correction_age_s`, which remains a
receiver-reported differential age when a receiver protocol documents one.
Local NTRIP arrival age is not projected into that runtime field, does not use
public/GNSS timestamps, and does not claim RTK usability or receiver correction
age.

## Live TCP Client

`ntrip_client.*` provides the first live NTRIP client layer in the project.

Current state model:

- `kDisconnected`
- `kConnecting`
- `kConnected`
- `kStreaming`
- `kFailed`

Current behavior:

- connect through `TcpClientTransport`
- optionally establish a synchronous TLS session through the same transport
- build and send one NTRIP GET request with `BuildNtripGetRequest(...)`
- accept `ICY 200 OK`, `HTTP/1.0 200`, and `HTTP/1.1 200`
- reject non-200 responses as HTTP failures
- reject malformed response headers as protocol failures
- strip the HTTP/NTRIP response header from streamed output
- feed streamed payload bytes into `RtcmFrameFramer` and `RtcmCorrectionMonitor`
- expose portable correction health through the existing diagnostics model
- expose shared RTCM semantic observations so higher layers can consume
  base-station ARP, GLONASS `1230`, and MSM summary/per-message metadata
- support explicit synchronous GGA writes from runtime state
- expose reconnect state for external retry orchestration
- update reconnect metrics/state on retry-worthy failures without starting a reconnect loop

Current non-goals:

- nonblocking TLS handshakes
- reconnect loop or background timers
- live sourcetable fetch workflow
- chunked transfer support
- redirects
- automatic periodic GGA sending
- multi-caster orchestration

## Monitor CLI

`gnss_tools/gnss_ntrip_monitor` is the first practical live consumer of the
current NTRIP foundation.

Current scope:

- plain TCP only
- synchronous foreground execution
- one caster / one mountpoint
- optional manual latitude / longitude input for GGA injection
- RTCM correction-health summary plus portable MSM/header semantic summary
  through the portable monitor

Current non-goals:

- TLS
- reconnect loops
- live sourcetable fetch or discovery orchestration
- automatic position sources
- ROS 2 or GUI ownership

Typical uses:

- confirm a caster responds with a valid NTRIP header
- confirm RTCM payload bytes are actually flowing
- confirm base-position and MSM messages appear in the stream
- inspect portable MSM summary state such as station id, constellation, MSM
  variant, and satellite / signal / cell counts
- test NEAR-style mounts with explicit latitude / longitude input

The CLI is intentionally a thin synchronous harness over `NtripClient`, so it
can validate live caster behavior now while staying reusable for later ROS 2,
GUI, and embedded integrations.

## Future Uses

This foundation is intended to support later:

- CLI retry orchestration
- ROS 2 NTRIP nodes
- ESP32 transport adapters
- BlueOS correction bridges
- RTK base relay components
- LoRa filtering or forwarding policies

## Deferred Work

Still intentionally deferred:

- TLS
- reconnect loop or state machine
- multi-caster orchestration
- live sourcetable fetch and discovery workflows
- automatic mountpoint selection
- automatic periodic GGA sending
- `NtripClient` delegation to the reusable `GgaInjector`
- ROS 2 / GUI-driven GGA scheduling
- ESP32-specific networking code
