# NTRIP Foundation

This document describes the current scope of `gnss_ntrip`.

Today this layer is intentionally small. It provides:

- portable NTRIP configuration types
- request/header generation
- basic authentication helpers
- GGA injection policy types
- connection and RTCM-flow metrics models

It does not provide a socket client yet.

## Purpose

`gnss_ntrip` is the transport-adjacent foundation for future correction
transport, but it is not the transport implementation itself.

Current responsibilities:

- normalize caster connection settings
- build deterministic NTRIP GET requests
- model whether periodic GGA injection is enabled
- track correction-stream metrics independently from ROS 2 or any network stack

Current non-responsibilities:

- TCP sockets
- TLS
- reconnect loops
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
future socket / TLS transport
   ->
incoming RTCM byte stream
   ->
gnss_protocols RTCM framer + parser helpers
   ->
NtripConnectionMetrics updates
   ->
future ROS 2 / ESP32 / tools integrations
```

`gnss_ntrip` stays above raw protocol framing and below application-specific
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
- simple reconnect backoff settings

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

The current GGA support is policy-only.

`GgaInjectionPolicy` currently models:

- enabled vs disabled
- interval in seconds
- whether a runtime position fix is required before sending
- optional `last_sent_timestamp_ns`

What it does not do yet:

- generate a real NMEA GGA sentence
- read coordinates from `GnssRuntimeState`
- decide on socket write timing

That separation is deliberate: the policy can be shared later by ROS 2, ESP32,
BlueOS, RTK base, or LoRa-facing components without coupling them to one
transport stack.

## Metrics Model

`NtripConnectionMetrics` currently tracks:

- total bytes received
- valid RTCM frames received
- invalid RTCM frames
- last RTCM message type
- optional correction age field
- connected / disconnected state
- reconnect count
- last error enum

The current model is compatible with the existing RTCM parser/tooling:

- `gnss_protocols` already validates and classifies RTCM frames
- `rtcm_inspect` and `gnss_inspect` already expose RTCM stream summaries
- future NTRIP clients can update these same counters while feeding the RTCM
  stream into runtime or tool adapters

## Future Uses

This foundation is intended to support later:

- ROS 2 NTRIP nodes
- ESP32 transport adapters
- BlueOS correction bridges
- RTK base relay components
- LoRa filtering or forwarding policies

## Deferred Work

Still intentionally deferred:

- TCP socket client
- TLS
- reconnect state machine
- multi-caster orchestration
- RTCM forwarding to serial or sockets
- sourcetable handling
- live GGA sentence generation
- ROS 2 nodes
- ESP32-specific networking code
