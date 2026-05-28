# GNSS Tools

This document describes the current standalone inspection tools in
`gnss_tools`.

These tools are intentionally offline and file/stdin oriented.

Current non-goals:

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

`gnss_inspect` is a structural mixed-stream inspector for raw GNSS logs that may
contain:

- NMEA
- UBX
- Unicore ASCII
- RTCM3
- arbitrary noise bytes between frames

It reuses the existing framers and metadata helpers rather than maintaining a
second protocol decoder.

The tool scans a byte stream and emits a compact timeline of recognized items:

- NMEA sentence identity as `talker + sentence type`, for example `GPGGA`
- UBX class/id and a known message name when available, for example `01:07`
  and `NAV-PVT`
- Unicore message name, for example `BESTNAVA`
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
- counts by Unicore message name
- counts by RTCM message type

Examples:

```text
gnss_inspect file.bin
cat file.bin | gnss_inspect -
gnss_inspect --summary file.bin
gnss_inspect --json file.bin
```

### `gnss_replay`

`gnss_replay` is the first semantic offline replay tool.

It reuses:

- the mixed-stream structural scan
- existing NMEA / UBX / Unicore semantic parsers
- protocol-to-runtime mapping helpers
- `GnssRuntimeAggregator`

Current replay behavior:

- recognizes mixed NMEA / UBX / Unicore / RTCM / noise streams
- maps supported semantic GNSS messages into partial `GnssRuntimeState` updates
- merges those updates into one coherent runtime state timeline
- keeps RTCM as correction-stream metadata only for now

The replay timeline shows, for each recognized record:

- byte offset
- protocol and message identity
- whether the record produced a runtime update
- the current normalized runtime state after that event

Summary output includes:

- total bytes
- recognized records
- runtime updates
- protocol counts
- RTCM message type counts
- final normalized runtime state

Examples:

```text
gnss_replay file.bin
cat file.bin | gnss_replay -
gnss_replay --summary file.bin
gnss_replay --json file.bin
```

### `gnss_serial_monitor`

`gnss_serial_monitor` is the first live hardware-facing CLI.

It reuses:

- Linux `PosixSerialTransport`
- `ReceiverSession`
- `ReceiverSessionRunner`
- the existing normalized runtime-state formatters

Current behavior:

- opens a serial device path
- routes bytes into the portable receiver session stack
- supports explicit `ublox` / `unicore` routing or conservative `auto` mode
- prints compact normalized runtime-state updates as they change
- prints a final runner/session summary when the process exits normally

Current scope:

- Linux-only
- manual field testing
- read-only monitoring

Current non-goals:

- receiver configuration
- NTRIP injection
- reconnect lifecycle
- ROS 2 publishing
- threaded or async reading

Examples:

```text
gnss_serial_monitor --port /dev/ttyACM0 --baud 921600 --vendor auto
gnss_serial_monitor --port /dev/ttyUSB0 --baud 115200 --vendor ublox
gnss_serial_monitor --port /dev/ttyUSB0 --baud 921600 --vendor unicore
gnss_serial_monitor --port /dev/ttyUSB0 --baud 921600 --vendor auto --max-bytes 200000
```

## Output Philosophy

The tools stay compact on purpose.

- text mode is optimized for quick terminal inspection
- `--summary` suppresses the per-item timeline
- `gnss_serial_monitor` prints compact live updates instead of a file timeline
- `--json` provides a small machine-readable object without freezing a large
  schema yet
- most tools are currently offline and file/stdin oriented only

## Deferred Tooling

Still intentionally deferred:

- richer live stream readers
- socket readers
- NTRIP client inspection
- live playback timing
- ROS 2 bag or topic output
- RTCM payload semantic decode
- MSM satellite/signal views
- persistent satellite tracking in replay
