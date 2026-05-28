# GNSS Tools

This document describes the current standalone inspection tools in
`gnss_tools`.

These tools are intentionally offline and file/stdin oriented.

Current non-goals:

- live serial input
- TCP/UDP ingestion
- NTRIP client behavior
- ROS 2 output
- replay timing
- full protocol semantic decoding beyond what `gnss_protocols` already exposes

## Current Tools

### `rtcm_inspect`

`rtcm_inspect` is a focused RTCM3 inspection CLI.

It reuses:

- the RTCM3 framer
- CRC24Q validation
- 12-bit RTCM message type extraction
- lightweight message classification helpers

Typical uses:

- inspect RTCM files captured from radios or NTRIP logs
- count correction message types
- confirm whether a stream contains MSM traffic

Examples:

```text
rtcm_inspect file.rtcm
cat file.rtcm | rtcm_inspect -
rtcm_inspect --summary file.rtcm
rtcm_inspect --json file.rtcm
```

### `gnss_inspect`

`gnss_inspect` is a mixed-stream inspector for raw GNSS logs that may contain:

- NMEA
- UBX
- RTCM3
- arbitrary noise bytes between frames

It reuses the existing framers and metadata helpers rather than maintaining a
second protocol decoder.

The tool scans a byte stream and emits a compact timeline of recognized items:

- NMEA sentence identity as `talker + sentence type`, for example `GPGGA`
- UBX class/id and a known message name when available, for example `01:07`
  and `NAV-PVT`
- RTCM message type plus the current lightweight classification, for example
  `1005` and `station_arp`

Summary output includes:

- total bytes read
- total recognized items
- valid vs checksum-invalid items
- malformed and truncated trailing items
- noise bytes and noise spans
- counts by protocol
- counts by NMEA sentence type
- counts by UBX class/id
- counts by RTCM message type

Examples:

```text
gnss_inspect file.bin
cat file.bin | gnss_inspect -
gnss_inspect --summary file.bin
gnss_inspect --json file.bin
```

## Output Philosophy

The tools stay compact on purpose.

- text mode is optimized for quick terminal inspection
- `--summary` suppresses the per-item timeline
- `--json` provides a small machine-readable object without freezing a large
  schema yet

## Deferred Tooling

Still intentionally deferred:

- live stream readers
- serial device support
- socket readers
- NTRIP client inspection
- RTCM payload semantic decode
- MSM satellite/signal views
- ROS 2 publishing from inspection tools
