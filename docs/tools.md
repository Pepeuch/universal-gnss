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
  - `230400`
  - `115200`
  - `38400`
  - `9600`
- keeps onboard/platform UART scanning disabled by default
- can opt into platform UART probing for embedded Linux targets such as
  Raspberry Pi, Orange Pi, or Jetson-like boards
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
- generic `NMEA` fallback is enabled by default; `--allow-nmea` is retained as
  a compatibility no-op for explicit CLI runs
- onboard UART scanning must be opted in with `--include-platform-uarts`
  because `/dev/serial0`, `/dev/ttyAMA*`, `/dev/ttyS*`, or `/dev/ttyTHS*`
  may be console, Bluetooth, or non-GNSS peripherals

Examples:

```text
gnss_discover
gnss_discover --path /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00
gnss_discover --json
gnss_discover --baud 921600,115200
gnss_discover --allow-nmea
gnss_discover --include-platform-uarts
gnss_discover --path /dev/ttyAMA2 --baud 921600
```

Typical text output includes:

- chosen device path
- selected baud rate
- detected family
- confidence
- numeric discovery score
- discovery reason
- transport/source
- stable `/dev/serial/by-id` id when available
- evidence summary
  - optional note when only weak evidence is present

Detection policy:

- u-blox is detected from valid UBX frames.
- Unicore is detected from supported Unicore ASCII records such as `BESTNAVA`,
  `PVTSLNA`, `RTKSTATUSA`, `RTCMSTATUSA`, `SATSINFOA`, and from valid Unicore
  binary `N4` frames.
- Generic NMEA discovery only counts valid runtime GNSS NMEA sentences with
  known GNSS talkers (`GP`, `GL`, `GA`, `GB`, `BD`, `GQ`, `GN`) and known
  runtime sentence types such as `GGA`, `RMC`, `GSA`, `GSV`, `GST`, `VTG`,
  `ZDA`, and `GLL`.
- RTCM-only streams are reported as unknown with a weak `rtcm_only_stream`
  note, because an NTRIP/radio correction stream is not itself a receiver.
- MAVLink heartbeat streams are rejected with a negative score.
- Random serial text is rejected with a negative score.
- Silent ports are reported as `no_data` with no confidence.

Scoring:

- valid UBX frame: `+100`
- Unicore `RTKSTATUSA`: `+100`
- Unicore `PVTSLNA`: `+100`
- other supported Unicore ASCII or binary runtime/status frame: `+100`
- valid `GGA`: `+20`
- other valid runtime GNSS NMEA sentence: `+10`
- random ASCII text: `-50`
- MAVLink heartbeat: `-200`

Confidence is derived from score:

- `high`: score `>= 100`
- `medium`: score `>= 20`
- `low`: score `> 0`
- `none`: score `<= 0`

Auto baud probing stops once a high-confidence result reaches the configured
score threshold, currently `100`.

`--json` emits a list of stable result objects intended for later ROS 2 or GUI
integration work.

Recommended embedded Linux usage:

```text
gnss_discover --include-platform-uarts
gnss_discover --path /dev/ttyAMA2 --baud 921600
```

On boards that expose many inactive `ttyS*` devices, `--include-platform-uarts`
may produce several `unknown` results. When the target UART is already known,
prefer the explicit `--path` form.

## Current Tools

### `rtcm_inspect`

`rtcm_inspect` is a focused RTCM3 inspection CLI.

It reuses:

- the RTCM3 framer
- CRC24Q validation
- 12-bit RTCM message type extraction
- the shared RTCM semantic observation layer used by the correction monitor

Typical uses:

- inspect RTCM files captured from radios or NTRIP logs
- count correction message types
- confirm whether a stream contains MSM traffic
- confirm whether a stream contains a decodable `1005` / `1006` base-station
  ARP position
- inspect portable MSM summary state such as station id, constellation, MSM
  variant, and satellite / signal / cell counts
- inspect decoded `1230` GLONASS code-phase bias state without writing a
  message-specific parser in the tool

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
- provides the exact event/state timeline reused by `universal_gnss_ros2`
  `replay_node` for hardware-free ROS2 playback
- NMEA `GST` can enrich replayed runtime states with conservative horizontal /
  vertical accuracy without changing fix or position
- Unicore binary `BESTNAVB` and `PVTSLNB` now contribute the same conservative
  runtime fields as their documented ASCII counterparts where those fields are
  present

Typical regression workflow:

```text
gnss_inspect --summary testdata/mixed/nmea_ubx_rtcm_unicore.bin
gnss_replay --summary testdata/ubx/nav_pvt_sat_monrf.ubx
gnss_replay --summary testdata/nmea/basic_fix.nmea
gnss_replay --summary testdata/unicore/basic_ascii.log
rtcm_inspect --summary testdata/rtcm/basic_msm.rtcm
```

Captured receiver logs can be replayed directly:

```text
gnss_inspect --summary f9p_capture.ubx
gnss_replay --summary f9p_capture.ubx
gnss_inspect --summary um982_capture.log
gnss_replay --summary um982_capture.log
```

Keep private NTRIP credentials out of captures before adding any replay data to
`testdata/`. Prefer small sanitized byte-for-byte samples that cover one
behavior, such as a UBX `NAV-PVT` update, a UM982 `RTKSTATUSA`/`RTCMSTATUSA`
pair, or a short mixed stream with RTCM corrections.

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

ROS2 reuse:

- `universal_gnss_ros2/replay_node` reuses `ReplayGnssBytes(...)` instead of
  reimplementing protocol parsing
- the replay node republishes `status`, `fix`, and `diagnostics` from the same
  normalized runtime states that `gnss_replay` reconstructs
- when `publish_rtcm=true`, the replay node also republishes RTCM frame bytes
  from the original capture on `/rtcm`

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
- reports shared RTCM semantic observations, currently `1005`, `1006`, and
  `1230`
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
- `dual_antenna_heading`
- `dual_antenna_baseline`
- `baseline_azimuth_deg`
- `baseline_pitch_deg`
- `baseline_length_m`
- `baseline_solution_status`
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

- the driver-level `ReceiverAutoConfig` planner/report layer
- the portable `ReceiverCommand` model
- lightweight command preview formatting helpers

Current behavior:

- generates prepared `ReceiverCommand` sequences without sending them
- supports the current portable profile names:
  - `runtime_only`
  - `rover_high_precision`
  - `rover_high_precision_debug`
  - `factory_reset`
- keeps legacy aliases accepted:
  - `rover` -> `rover_high_precision`
  - `diagnostics` -> `rover_high_precision_debug`
- current receiver-family support:
  - `ublox`: `runtime_only`, `rover_high_precision`,
    `rover_high_precision_debug`; `factory_reset` currently previews as an
    unsupported/stub portable profile
  - `unicore`: `runtime_only`, `rover_high_precision`,
    `rover_high_precision_debug`, `factory_reset`
  - `nmea`: `runtime_only` only
- prints compact human-readable command previews by default
- can emit JSON for scripting and review
- can apply offline overrides like `--persistent`, `--signal-profile`,
  `--baud`, `--rate-hz`, and `--output-port`
- for `unicore`, accepts an optional `--model` selector so preview can apply
  documented model/capability-aware signal-group rules
- current recognized Unicore model selectors are:
  `UM960`, `UM980`, `UM981`, `UM982`, and `UB9A0`
- for `unicore`, unknown or undocumented models skip `CONFIG SIGNALGROUP` and
  report the safe fallback instead of guessing
- for `ublox`, separates the current host transport from the receiver output
  interface being configured:
  - `--output-port usb` enables the required `CFG-MSGOUT-*USB` keys only
  - `--output-port uart1` or `uart2` targets only that UART interface
  - `--output-port all` enables USB, UART1, and UART2 outputs together
  - `--output-port auto` uses the available transport-path context when present
  - when `--output-port` is omitted, the legacy planner default remains
    `UART1 + USB` for backwards compatibility
- for `ublox`, `--config-baud` only applies to UART output-port plans; USB has
  no baud-rate configuration key
- shows `factory_reset` command counts and baud-reset implications when the
  vendor support is known

Current non-goals:

- serial access
- command dispatch
- transaction engine execution
- receiver communication
- live configuration workflows

Examples:

```text
gnss_profile_preview nmea runtime_only
gnss_profile_preview ublox rover_high_precision
gnss_profile_preview ublox rover_high_precision --output-port usb --rate-hz 7
gnss_profile_preview ublox rover_high_precision_debug --json
gnss_profile_preview unicore rover_high_precision --model UM960
gnss_profile_preview unicore rover_high_precision
gnss_profile_preview unicore rover_high_precision --model UM982 --signal-profile minimal --rate-hz 1
gnss_profile_preview unicore rover_high_precision --model UM982 --persistent --rate-hz 5
gnss_profile_preview unicore factory_reset
```

Text output includes:

- profile metadata and requested overrides
- receiver model when present
- signal-profile intent when present
- requested and resolved output-port context when present
- one command at a time with kind, safety level, payload kind, payload size, and description
- raw text commands for ASCII-based profiles
- raw binary hex only when `--verbose` is enabled
- warnings when a requested Unicore signal-group command was skipped for safety
- a summary with total, runtime, persistent, and factory-reset command counts

JSON output includes:

- top-level profile metadata and preview status
- the generated command list
- per-command descriptions and optional text/hex detail
- the same summary counts as text mode

### `gnss_config_plan`

`gnss_config_plan` is a dry-run receiver-config application planner.

It reuses:

- the driver-level `ReceiverAutoConfig` planner/report layer
- the portable `ReceiverCommand` model
- the same command safety rules used by the driver dispatcher

Current behavior:

- builds the same prepared command sequences that a future live config flow would use
- reports receiver family, profile name, dry-run status, and command counts
- reports whether the planned profile is production-ready
- reports whether the same plan would be ready for later manual execution
- highlights whether explicit safety confirmation would be required before dispatch
- marks persistent and factory-reset commands clearly in the sequence
- accepts vendor-neutral `--signal-profile balanced|high_precision|all_signals|minimal|custom`
- for `unicore`, accepts an optional `--model` selector so planning can apply
  documented model/capability-aware signal-group rules
- current recognized Unicore model selectors are:
  `UM960`, `UM980`, `UM981`, `UM982`, and `UB9A0`
- accepts `--output-port usb|uart1|uart2|all|auto` for `ublox` interface
  selection
- supports the same portable profile names and legacy aliases as
  `gnss_profile_preview`
- current receiver-family support:
  - `ublox`: `runtime_only`, `rover_high_precision`,
    `rover_high_precision_debug`; `factory_reset` currently reports as an
    unsupported/stub portable profile
  - `unicore`: `runtime_only`, `rover_high_precision`,
    `rover_high_precision_debug`, `factory_reset`
  - `nmea`: `runtime_only` only
- supports generic `nmea runtime_only` as a zero-command read-only plan
- rejects write-side portable profiles for generic NMEA with a clear reason
- rejects `runtime_only --persistent` because that profile does not modify
  receiver state
- emits either compact text output or JSON

u-blox interface-planning notes:

- the transport device is how the CLI would talk to the receiver right now
- the output port is which receiver interface should emit configured runtime
  messages later
- `--config-baud` only changes UART interfaces; it is ignored for
  `--output-port usb`
- omitting `--output-port` preserves the legacy `UART1 + USB` command plan for
  backwards compatibility
- `--output-port auto` resolves to USB for `ttyACM*` and
  `/dev/serial/by-id/usb-u-blox*` paths when transport context is available
- the same USB inference also accepts container-local aliases such as
  `/dev/usb-u-blox_*`
- otherwise it warns and falls back to `uart1`
- when no Unicore model is supplied, the planner keeps the receiver's current
  signal-group configuration unchanged and warns instead of applying a
  family-wide guess

Current non-goals:

- command execution
- serial access
- transaction engine execution
- ACK/NAK handling
- receiver communication

Examples:

```text
gnss_config_plan nmea runtime_only
gnss_config_plan ublox rover_high_precision
gnss_config_plan ublox rover_high_precision --output-port usb --rate-hz 7
gnss_config_plan ublox rover_high_precision --output-port uart1 --config-baud 460800 --rate-hz 7
gnss_config_plan unicore rover_high_precision --model UM981
gnss_config_plan unicore rover_high_precision_debug
gnss_config_plan unicore rover_high_precision --model UM982 --signal-profile minimal --rate-hz 1
gnss_config_plan ublox rover_high_precision --persistent
gnss_config_plan ublox rover_high_precision --rate-hz 5 --config-baud 921600
gnss_config_plan unicore factory_reset --json
```

Text output includes:

- receiver family and profile metadata
- receiver model when present
- signal-profile intent and other requested overrides
- requested and resolved output-port context when present
- requested apply mode
- dry-run status
- runtime / persistent / factory-reset command counts
- production-ready and ready-to-execute status
- whether explicit safety confirmation would be required
- the planned command sequence in application order
- warnings and rollback expectations when relevant

JSON output includes:

- a `profile` object
- a `validation` object
- a `summary` object
- a `warnings` array
- an ordered `commands` array

### `gnss_config_apply`

`gnss_config_apply` is the operator-facing Auto Configuration apply CLI.

It reuses:

- the driver-level `ReceiverAutoConfig` planner/report layer
- `ReceiverConfigApplication`
- `ReceiverCommandTransactionEngine`
- `PosixSerialTransport`
- the existing u-blox and Unicore response routers

Current behavior:

- can discover the receiver when `--receiver auto` or `--family auto` is used
- can probe an explicit device path with `--device`
- accepts `--baud auto` to reuse discovery-time baud detection for live apply
- prefers stable `/dev/serial/by-id/*` paths when the operator provides one
- prints the full plan/report before any live-write decision
- accepts the same portable profile names and legacy aliases as
  `gnss_profile_preview`
- accepts the same vendor-neutral `--signal-profile` values as
  `gnss_config_plan`
- for `unicore`, accepts the same optional `--model` selector used by preview
  and plan
- current recognized Unicore model selectors are:
  `UM960`, `UM980`, `UM981`, `UM982`, and `UB9A0`
- accepts the same `--output-port usb|uart1|uart2|all|auto` values as
  `gnss_config_plan`
- refuses runtime-only live writes unless `--confirm` or `--yes` is present
- supports Unicore persistent live apply through the reset/recovery workflow
- allows `runtime_only` live apply as a no-op when the selected family/profile
  generates no receiver commands
- rejects unknown receivers for apply
- supports generic NMEA only through the `runtime_only` no-op profile
- supports Unicore live `factory_reset` through the same reset/recovery flow
- executes one command at a time over a Linux serial port
- waits synchronously for one matching response at a time
- on mixed Unicore binary/ASCII streams, resynchronizes to recognized
  `$command,...,response: OK*` tokens even when prefix noise shares the same
  buffered line
- after Unicore `FRESET`, actively queries `VERSIONA` at `115200`, restores
  `COM1` with the explicit `CONFIG COM1 <baud> 8 n 1` form, then verifies the
  receiver again at the restored baud before replaying the profile
- supports a simple per-command `--timeout-ms` loop without threads
- stops on the first rejected command, dispatch failure, read failure, or timeout

Current scope:

- `ublox`: `runtime_only`, `rover_high_precision`,
  `rover_high_precision_debug`; `factory_reset` unsupported/stub
- `unicore`: `runtime_only`, `rover_high_precision`,
  `rover_high_precision_debug`, `factory_reset`
- `nmea`: `runtime_only` only
- Linux POSIX serial only

Current non-goals:

- daemon mode
- ROS 2 services
- NTRIP integration
- background retry scheduling
- interactive prompts
- fully automatic rollback after persistent receiver writes

Examples:

```text
gnss_config_apply --family nmea --device /dev/ttyUSB9 --baud 115200 --profile runtime_only --apply-mode runtime-only
gnss_config_apply --receiver auto --device /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud auto --profile rover_high_precision --output-port auto --apply-mode runtime-only --confirm
gnss_config_apply --family unicore --model UM981 --device /dev/ttyAMA4 --baud 921600 --profile rover_high_precision --apply-mode runtime-only --confirm
gnss_config_apply --family unicore --model UM982 --device /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud 921600 --profile rover_high_precision --signal-profile high_precision --apply-mode runtime-only --confirm
gnss_config_apply --family ublox --device /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud 921600 --profile rover_high_precision_debug --output-port usb --apply-mode runtime-only --confirm
gnss_config_apply --receiver auto --model UM982 --device /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud auto --profile rover_high_precision --apply-mode runtime-only --confirm --timeout-ms 5000
gnss_config_apply --receiver auto --model UM982 --device /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud auto --profile rover_high_precision --apply-mode persistent --confirm
```

Hardware notes from the `v0.6-4` operator validation pass:

- a u-blox F9P at `921600` completed the runtime-only
  `rover_high_precision` apply with the default `1000 ms` timeout budget
- a Unicore UM982 at `921600` completed the same runtime-only
  `rover_high_precision` apply after the mixed-stream response-router fix
  above, using `--timeout-ms 5000`
- the UM982 `rover_high_precision` profile enables `RTCMSTATUSA ONCHANGED`;
  short read-only
  captures may therefore show the accepted apply response but still not show an
  emitted `RTCMSTATUSA` record until receiver-side correction state changes
- the validated UM982 persistent workflow now performs
  `FRESET -> VERSIONA@115200 -> CONFIG COM1 921600 8 n 1 -> VERSIONA@921600`
  before replaying the rover profile and finishing with `SAVECONFIG`
- the documented UM982 model-aware rover profile now includes
  `CONFIG SIGNALGROUP 3 6`; unknown or non-baseline Unicore models now skip
  that command instead of inheriting a family-wide default
- after a full Unicore reset/recovery apply, the receiver may need a couple of
  minutes to reacquire satellites and corrections even though the GNSS runtime
  is already healthy and publishing at `5 Hz`

Text output includes:

- discovered device/family/baud context when available
- receiver model when present
- plan validation, warnings, and rollback expectations
- signal-profile intent and its translated command-plan impact when present
- requested and resolved output-port context when present
- dry-run vs live-apply-requested status
- runtime/persistent confirmation requirements
- the command sequence to be applied
- command progress and the final execution summary

JSON output includes:

- profile metadata
- discovery metadata
- validation and warning state
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
- works best with stable `/dev/serial/by-id/*` paths when those symlinks exist
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
gnss_serial_monitor --port /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud 921600 --vendor auto
gnss_serial_monitor --port /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud 921600 --vendor ublox
gnss_serial_monitor --port /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud 921600 --vendor unicore
gnss_serial_monitor --port /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud 921600 --vendor auto --max-bytes 200000
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
- RTCM semantic observation state, including `1230` validity / mask / decoded
  bias values plus portable MSM summary fields when available
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
- `gnss_config_apply` remains operator-confirmed and only performs live writes
  after explicit confirmation
- `--json` provides a small machine-readable object without freezing a large
  schema yet
- most tools are currently offline and file/stdin oriented only

## Deferred Tooling

Still intentionally deferred:

- richer live stream readers
- socket readers
- live playback timing
- richer live config execution workflows beyond the current
  `gnss_config_apply` path
- a real `gnss_config_plan --execute` path
- background config daemons
- ROS 2 bag or topic output
- RTCM observation-level decode beyond the current semantic summary
- MSM observation payload views
- persistent satellite tracking in replay
