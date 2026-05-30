# GNSS Tools

This document describes the current standalone inspection tools in
`gnss_tools`.

Most of these tools are intentionally offline and file/stdin oriented.
`gnss_serial_monitor` is the current live serial-only exception.

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

### `gnss_profile_preview`

`gnss_profile_preview` is an offline receiver-config inspection CLI.

It reuses:

- the portable `ReceiverCommand` model
- the existing u-blox config profile builder
- the existing Unicore config profile builder
- lightweight command preview formatting helpers

Current behavior:

- generates prepared `ReceiverCommand` sequences without sending them
- supports `ublox` profiles: `rover`, `diagnostics`, `base`
- supports `unicore` profiles: `rover`, `diagnostics`
- prints compact human-readable command previews by default
- can emit JSON for scripting and review
- can apply offline overrides like `--persistent`, `--baud`, and `--rate-hz`

Current non-goals:

- serial access
- command dispatch
- transaction engine execution
- receiver communication
- live configuration workflows

Examples:

```text
gnss_profile_preview ublox rover
gnss_profile_preview ublox diagnostics --json
gnss_profile_preview ublox base --persistent --rate-hz 1
gnss_profile_preview unicore rover
gnss_profile_preview unicore diagnostics --persistent --rate-hz 5
```

Text output includes:

- profile metadata and requested overrides
- one command at a time with kind, safety level, payload kind, payload size, and description
- raw text commands for ASCII-based profiles
- raw binary hex only when `--verbose` is enabled
- a summary with total, runtime, persistent, and factory-reset command counts

JSON output includes:

- top-level profile metadata and preview status
- the generated command list
- per-command descriptions and optional text/hex detail
- the same summary counts as text mode

### `gnss_config_plan`

`gnss_config_plan` is a dry-run receiver-config application planner.

It reuses:

- the portable `ReceiverCommand` model
- the existing u-blox and Unicore config profile builders
- the offline profile preview/build path
- the same command safety rules used by the driver dispatcher

Current behavior:

- builds the same prepared command sequences that a future live config flow would use
- reports receiver family, profile name, dry-run status, and command counts
- highlights whether explicit safety confirmation would be required before dispatch
- marks persistent and factory-reset commands clearly in the sequence
- supports `ublox` profiles: `rover`, `diagnostics`, `base`
- supports `unicore` profiles: `rover`, `diagnostics`
- emits either compact text output or JSON

Current non-goals:

- command execution
- serial access
- transaction engine execution
- ACK/NAK handling
- receiver communication

Examples:

```text
gnss_config_plan ublox rover
gnss_config_plan unicore diagnostics
gnss_config_plan ublox rover --persistent
gnss_config_plan ublox rover --rate-hz 5 --baud 921600
gnss_config_plan unicore rover --json
```

Text output includes:

- receiver family and profile metadata
- dry-run status
- runtime / persistent / factory-reset command counts
- whether explicit safety confirmation would be required
- the planned command sequence in application order

JSON output includes:

- a `profile` object
- a `summary` object
- an ordered `commands` array

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
- `gnss_profile_preview` is preview-only and never touches receiver I/O
- `gnss_config_plan` is dry-run only and never performs command dispatch
- `--json` provides a small machine-readable object without freezing a large
  schema yet
- most tools are currently offline and file/stdin oriented only

## Deferred Tooling

Still intentionally deferred:

- richer live stream readers
- socket readers
- NTRIP client inspection
- live playback timing
- live config execution CLIs
- a real `gnss_config_plan --execute` path
- ROS 2 bag or topic output
- RTCM payload semantic decode
- MSM satellite/signal views
- persistent satellite tracking in replay
