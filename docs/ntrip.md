# NTRIP Foundation

This document describes the current scope of `gnss_ntrip`.

Today this layer is intentionally small. It provides:

- portable NTRIP configuration types
- request/header generation
- basic authentication helpers
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
- open a synchronous TCP connection through `gnss_transport`
- validate the initial NTRIP response header
- model whether periodic GGA injection is enabled
- generate a portable NMEA GGA sentence from `GnssRuntimeState`
- send GGA explicitly through `NtripClient` without owning a timer loop
- model reconnect/backoff decisions without owning a reconnect loop
- track correction-stream metrics independently from ROS 2 or any network stack
- feed incoming correction bytes into the existing RTCM framer and correction monitor

Current non-responsibilities:

- TLS
- reconnect loops
- sourcetable parsing
- RTCM forwarding
- serial output
- ROS 2 nodes
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

## Authentication Policy

Basic authentication is modeled separately from sockets.

Current rules:

- the encoded payload is `username:password`
- if both are empty, the auth value and header are omitted
- if only one side is empty, the colon separator is still preserved

This keeps request generation deterministic and testable before any network code
exists.

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
- cap the retry delay at `max_delay_ms`
- optionally stop scheduling after `max_attempts`
- optionally reset backoff state after a successful connect

What this does not do yet:

- run a reconnect loop
- own timers or background work
- decide when an application should actually call `Connect()`

That split is intentional so the same policy can be reused later by CLI tools,
ROS 2 nodes, ESP32 targets, and dashboard integrations.

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
- optional correction age field
- connected / disconnected state
- reconnect count for scheduled retry attempts
- last error enum

The current model is compatible with the existing RTCM parser/tooling:

- `gnss_protocols` already validates and classifies RTCM frames
- `rtcm_inspect` and `gnss_inspect` already expose RTCM stream summaries
- future NTRIP clients can update these same counters while feeding the RTCM
  stream into runtime or tool adapters

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
- build and send one NTRIP GET request with `BuildNtripGetRequest(...)`
- accept `ICY 200 OK`, `HTTP/1.0 200`, and `HTTP/1.1 200`
- reject non-200 responses as HTTP failures
- reject malformed response headers as protocol failures
- strip the HTTP/NTRIP response header from streamed output
- feed streamed payload bytes into `RtcmFrameFramer` and `RtcmCorrectionMonitor`
- expose portable correction health through the existing diagnostics model
- support explicit synchronous GGA writes from runtime state
- expose reconnect state for external retry orchestration
- update reconnect metrics/state on retry-worthy failures without starting a reconnect loop

Current non-goals:

- TLS
- reconnect loop or background timers
- sourcetable parsing
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
- RTCM correction-health summary through the portable monitor

Current non-goals:

- TLS
- reconnect loops
- sourcetable parsing
- automatic position sources
- ROS 2 or GUI ownership

Typical uses:

- confirm a caster responds with a valid NTRIP header
- confirm RTCM payload bytes are actually flowing
- confirm base-position and MSM messages appear in the stream
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
- RTCM forwarding to serial or sockets
- sourcetable handling
- automatic periodic GGA sending
- `NtripClient` delegation to the reusable `GgaInjector`
- ROS 2 / GUI-driven GGA scheduling
- ROS 2 nodes
- ESP32-specific networking code
