# GNSS Tools

This document describes the current standalone inspection tools in
`gnss_tools`.

Most of these tools are intentionally offline and file/stdin oriented.
`gnss_discover`, `gnss_serial_monitor`, and `gnss_ntrip_monitor` are the
current live/read-only exceptions.

Current non-goals:

- TCP/UDP ingestion beyond the dedicated live monitors
- ROS 2 output
- replay timing
- full protocol semantic decoding beyond what `gnss_protocols` already exposes

## Discovery And Live Tools

### `gnss_discover`

`gnss_discover` is the first read-only receiver discovery CLI.

It reuses:

- the Linux serial-port enumeration logic from `receiver_discovery.*`
- the existing `UBX`, `NMEA`, `RTCM3`, Unicore ASCII, and Unicore binary `N4`
  framers
- the same conservative family-detection heuristics used by the driver layer

Current behavior:

- scans likely Linux serial receiver paths
- prefers stable `/dev/serial/by-id/*` symlinks when available
- probes a conservative default baud list:
  - `921600`
  - `460800`
  - `115200`
  - `38400`
- performs bounded read-only probing with no receiver writes
- reports a detected family:
  - `ublox`
  - `unicore`
  - `nmea`
  - `unknown`
- reports a conservative confidence level:
  - `none`
  - `low`
  - `medium`
  - `high`
- exposes supporting evidence such as:
  - valid `UBX` frames seen
  - Unicore ASCII records seen
  - Unicore binary `N4` frames seen
  - `NMEA` sentences seen
  - `RTCM3` frames seen
  - total bytes read

Current policy:

- discovery is read-only
- no receiver configuration is sent
- no baud or protocol settings are persisted
- generic `NMEA` fallback is disabled by default and must be opted in with
  `--allow-nmea`

Examples:

```text
gnss_discover
gnss_discover --path /dev/ttyACM0
gnss_discover --json
gnss_discover --baud 921600,115200
gnss_discover --allow-nmea
```

Typical text output includes:

- chosen device path
- selected baud rate
- detected family
- confidence
- transport/source
- stable `/dev/serial/by-id` id when available
- evidence summary
- optional note when only weak evidence is present

`--json` emits a list of stable result objects intended for later ROS 2 or GUI
integration work.

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
- confirm whether a stream contains a decodable `1005` / `1006` base-station
  ARP position

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
- Unicore binary `N4`
- RTCM3
- arbitrary noise bytes between frames

It reuses the existing framers and metadata helpers rather than maintaining a
second protocol decoder.

The tool scans a byte stream and emits a compact timeline of recognized items:

- NMEA sentence identity as `talker + sentence type`, for example `GPGGA`
- UBX class/id and a known message name when available, for example `01:07`
  and `NAV-PVT`
- Unicore message name, for example `BESTNAVA`
- Unicore binary message name when known, for example `BESTNAVB` or `PVTSLNB`
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

- recognizes mixed NMEA / UBX / Unicore ASCII / Unicore binary `N4` / RTCM /
  noise streams
- maps supported semantic GNSS messages into partial `GnssRuntimeState` updates
- merges those updates into one coherent runtime state timeline
- keeps RTCM as correction-stream metadata only for now
- NMEA `GST` can enrich replayed runtime states with conservative horizontal /
  vertical accuracy without changing fix or position
- Unicore binary `BESTNAVB` and `PVTSLNB` now contribute the same conservative
  runtime fields as their documented ASCII counterparts where those fields are
  present

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

### `gnss_quality_report`

`gnss_quality_report` is an offline GNSS quality summarizer built on top of the
existing replay, RTCM monitor, and diagnostic foundations.

It reuses:

- `gnss_replay`-style normalized runtime reconstruction
- the RTCM correction monitor
- existing RTCM message classification
- existing UBX `RXM-RTCM` receiver-side correction diagnostics
- the portable diagnostics model

Current behavior:

- reads an offline GNSS log from a file or stdin
- reconstructs the final normalized runtime state
- reports final fix / RTK state, accuracy, DOP, satellite counts, and CN0
- reports RTCM frame counts and message-type activity
- reports decoded RTCM `1005` / `1006` base-station ECEF position when present
- reports receiver-side RTCM acceptance diagnostics when `UBX-RXM-RTCM` is
  present
- reports portable receiver RF / hardware diagnostics when supported messages
  such as `UBX-MON-HW`, `Unicore JAMSTATUSA`, or `Unicore HWSTATUSA` are present
- emits either readable text output or compact JSON
- inherits Unicore binary `BESTNAVB` / `PVTSLNB` support through the same replay
  path used by `gnss_replay`

Current non-goals:

- live serial monitoring
- ROS 2 output
- GUI visualization
- advanced scoring or charting

Examples:

```text
gnss_quality_report log.bin
gnss_quality_report --summary log.bin
gnss_quality_report --json log.bin
```

The current quality levels are intentionally simple:

- `unknown`
- `poor`
- `usable`
- `good`
- `rtk_float`
- `rtk_fixed`

They are based conservatively on the final normalized runtime state plus any
portable diagnostics already available from the parsed log.

### `gnss_export`

`gnss_export` is the structured offline runtime timeline exporter.

It reuses:

- `gnss_replay`-style normalized runtime reconstruction
- the existing protocol-to-runtime mapping helpers
- the existing `GnssRuntimeAggregator` timeline updates

Current behavior:

- reads an offline GNSS log from a file or stdin
- emits one JSON object per runtime update, not one object per parsed frame
- defaults to JSON Lines (`jsonl`) output
- can write to stdout or an explicit output file
- supports a stable key order intended for notebooks, graphing, future GUI
  work, MQTT bridges, and API adapters
- inherits the same Unicore binary `BESTNAVB` / `PVTSLNB` runtime samples that
  `gnss_replay` reconstructs

Current JSONL schema includes:

- `event_index`
- `timestamp_ns`
- `protocol`
- `message`
- `fix_valid`
- `fix_type`
- `rtk_mode`
- `latitude_deg`
- `longitude_deg`
- `altitude_m`
- `horizontal_accuracy_m`
- `vertical_accuracy_m`
- `hdop`
- `vdop`
- `satellites_used`
- `satellites_tracked`
- `satellites_visible`
- `mean_cn0_dbhz`
- `max_cn0_dbhz`
- `correction_age_s`
- `heading_deg`
- `interference_detected`
- `jamming_detected`

Schema policy:

- every runtime-update line uses the same keys
- unavailable optional values are emitted as JSON `null`
- `RTCM3` metadata-only frames are not exported as runtime samples

Current non-goals:

- CSV export
- live streaming
- MQTT / WebSocket export
- ROS 2 bag export
- plotting or schema negotiation

Examples:

```text
gnss_export log.bin
gnss_export --format jsonl log.bin
gnss_export --output runtime.jsonl log.bin
gnss_export --pretty log.bin
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

### `gnss_config_apply`

`gnss_config_apply` is the first guarded live receiver-config CLI.

It reuses:

- the offline config-plan/profile-builder path
- `ReceiverConfigApplication`
- `ReceiverCommandTransactionEngine`
- `PosixSerialTransport`
- the existing u-blox and Unicore response routers

Current behavior:

- defaults to dry-run mode and never touches serial unless `--execute` is set
- requires `--confirm-runtime` before sending runtime-only command plans
- requires `--confirm-persistent` before sending persistent command plans
- executes one command at a time over a Linux serial port
- waits synchronously for one matching response at a time
- supports a simple per-command `--timeout-ms` loop without threads
- stops on the first rejected command, dispatch failure, read failure, or timeout

Current scope:

- `ublox` profiles: `rover`, `diagnostics`, `base`
- `unicore` profiles: `rover`, `diagnostics`
- Linux POSIX serial only

Current non-goals:

- daemon mode
- ROS 2 services
- NTRIP integration
- background retry scheduling
- interactive prompts
- factory-reset profile execution

Examples:

```text
gnss_config_apply ublox rover --port /dev/ttyACM0 --baud 921600
gnss_config_apply ublox rover --port /dev/ttyACM0 --baud 921600 --execute --confirm-runtime
gnss_config_apply unicore diagnostics --port /dev/ttyUSB0 --baud 921600 --execute --confirm-runtime
gnss_config_apply ublox rover --persistent --port /dev/ttyACM0 --baud 921600 --execute --confirm-persistent
```

Text output includes:

- dry-run vs execute-requested status
- runtime/persistent confirmation requirements
- the command sequence to be applied
- command progress and the final execution summary

JSON output includes:

- profile metadata
- safety status
- the planned command sequence
- progress messages
- a final execution summary

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

### `gnss_ntrip_monitor`

`gnss_ntrip_monitor` is the first live caster-facing CLI.

It reuses:

- `NtripConfig` and the existing NTRIP request builder
- the synchronous `NtripClient`
- the portable GGA generator and GGA injection policy
- the RTCM correction monitor
- the portable diagnostics / health-summary model

Current behavior:

- connects to a single caster over plain TCP
- validates `ICY 200 OK`, `HTTP/1.0 200`, and `HTTP/1.1 200`
- optionally sends an initial GGA when `--lat` and `--lon` are provided
- optionally keeps checking `MaybeSendGga(...)` when `--gga-interval` is set
- monitors streamed RTCM activity and correction health through the existing client foundation
- prints live status lines by default and a final text or JSON summary
- stops on `--max-bytes`, `--max-seconds`, disconnect, or read / protocol failure

Current non-goals:

- TLS
- reconnect loops
- multi-caster support
- ROS 2 nodes
- GUI integration
- automatic position sourcing

Examples:

```text
gnss_ntrip_monitor --host caster.example.org --port 2101 --mountpoint MOUNT
gnss_ntrip_monitor --host caster.example.org --port 2101 --mountpoint NEAR --user user --password pass
gnss_ntrip_monitor --host caster.example.org --port 2101 --mountpoint NEAR --lat 48.0 --lon 2.0 --gga-interval 5
gnss_ntrip_monitor --host caster.example.org --port 2101 --mountpoint NEAR --max-seconds 30
gnss_ntrip_monitor --host caster.example.org --port 2101 --mountpoint NEAR --summary
gnss_ntrip_monitor --host caster.example.org --port 2101 --mountpoint NEAR --json
```

Summary output includes:

- bytes received and bytes sent
- RTCM frames seen, valid, and invalid
- per-message-type counts
- MSM constellation counts
- base-position and `1230`-bias presence flags
- correction-health severity and availability flags
- optional GGA send counters

## Output Philosophy

The tools stay compact on purpose.

- text mode is optimized for quick terminal inspection
- `--summary` suppresses the per-item timeline
- `gnss_serial_monitor` prints compact live updates instead of a file timeline
- `gnss_ntrip_monitor` prints compact live status instead of becoming a daemon
- `gnss_profile_preview` is preview-only and never touches receiver I/O
- `gnss_config_plan` is dry-run only and never performs command dispatch
- `gnss_config_apply` is guarded and remains dry-run unless `--execute` plus the
  required confirmation flags are present
- `--json` provides a small machine-readable object without freezing a large
  schema yet
- most tools are currently offline and file/stdin oriented only

## Deferred Tooling

Still intentionally deferred:

- richer live stream readers
- socket readers
- live playback timing
- live config execution CLIs
- a real `gnss_config_plan --execute` path
- background config daemons
- ROS 2 bag or topic output
- RTCM payload semantic decode
- MSM satellite/signal views
- persistent satellite tracking in replay
