# Unicore Config Profiles

This document describes the current portable command-generation layer for
Unicore receivers and the first conservative response-routing layer that sits
beside it.

Today the builder generates prepared text `ReceiverCommand` values only.
Response handling is intentionally limited to a separate conservative router.

The builder itself does not:

- open serial or TCP transports
- send commands
- implement retries
- manage survey-in or base-station orchestration

The current router does not:

- implement a full command/result grammar
- own any serial transport
- implement retries or timers
- orchestrate live profile application

## Purpose

The builder translates a small high-level `UnicoreConfigProfile` into a stable
sequence of CRLF-terminated text commands.

Current implementation lives in:

- `gnss_driver/include/universal_gnss_driver/unicore_config_profile_builder.hpp`
- `gnss_driver/src/unicore_config_profile_builder.cpp`
- `gnss_driver/include/universal_gnss_driver/unicore_response_router.hpp`
- `gnss_driver/src/unicore_response_router.cpp`

## Reference Inputs

The current builder was shaped by:

- `docs/vendors/unicore/Unicore Reference Commands Manual For N4 High Precision Products_V2_EN_R1.4.pdf`
- the current portable runtime-mapping boundary documented in
  `docs/vendors/unicore/runtime_mapping.md`

What was reused conceptually:

- a small set of practical rover/runtime commands
- the per-message output syntax split:
  - `LOG ... ONTIME`
  - direct-period syntax
  - `ONCHANGED`

What was intentionally not copied:

- ROS 2 startup scripts
- runtime environment variables
- NTRIP wiring
- firmware fallback probing logic
- project-specific launch/runtime orchestration

## Generated Command Families

Current runtime-safe command families:

- `CONFIG COM1 <baud> 8 n 1`
- `MODE ROVER`
- `CONFIG NMEA0183 V410|V411`
- `CONFIG RTK TIMEOUT <seconds>`
- `CONFIG RTK RELIABILITY <a> <b>`
- `CONFIG DGPS TIMEOUT <seconds>`
- `CONFIG SIGNALGROUP ...`
- `UNLOG`
- output enable commands for:
  - `GPGGA`
  - `GPGSV`
  - `GPGST`
  - `PVTSLNA`
  - `BESTNAVA`
  - `RTKSTATUSA`
  - `RTCMSTATUSA`
  - `SATSINFOA`

Current safety-gated commands:

- `SAVECONFIG`
- `FRESET`

## Output Syntax Policy

The builder uses a fixed per-message syntax table.

- `GPGGA` -> `LOG GPGGA ONTIME <period>`
- `GPGSV` -> `GPGSV <period>`
- `GPGST` -> `GPGST <period>`
- `PVTSLNA` -> `LOG PVTSLNA ONTIME <period>`
- `BESTNAVA` -> `BESTNAVA <period>`
- `RTKSTATUSA` -> `RTKSTATUSA <period>`
- `SATSINFOA` -> `SATSINFOA <period>`
- `RTCMSTATUSA` -> `RTCMSTATUSA ONCHANGED`

This keeps generation deterministic and aligned with the message classes
already consumed elsewhere in `universal-gnss`.

## Helper Profiles

### `runtime_only`

`runtime_only` is handled at the portable planner layer as a zero-command,
read-only/no-op path. The Unicore builder does not emit a dedicated command
sequence for it.

### `rover_high_precision`

Current `rover_high_precision` helper generates:

- `MODE ROVER`
- `CONFIG NMEA0183 V411`
- `CONFIG RTK TIMEOUT 10`
- `CONFIG RTK RELIABILITY 3 1`
- `CONFIG DGPS TIMEOUT 600`
- `CONFIG SIGNALGROUP 3 6`
- `UNLOG`
- `LOG GPGGA ONTIME 1`
- `GPGSV 1`
- `GPGST 1`
- `LOG PVTSLNA ONTIME 1`
- `BESTNAVA 0.2`
- `RTKSTATUSA 1`
- `RTCMSTATUSA ONCHANGED`
- `SATSINFOA 1`

This keeps the primary rover state on `BESTNAVA` at 5 Hz while trimming the
fallback/observability logs down to 1 Hz so live serial output stays lighter on
UM982 deployments.

If persistent mode is requested, the builder appends only:

- `SAVECONFIG`

Legacy CLI alias:

- `rover` -> `rover_high_precision`

### `rover_high_precision_debug`

Current `rover_high_precision_debug` helper extends
`rover_high_precision` by restoring the heavier heading/status log:

- `LOG PVTSLNA ONTIME 0.2`

This debug profile is intentionally higher-bandwidth than
`rover_high_precision` and is meant for short-lived capture or troubleshooting
sessions, not normal runtime.

Legacy CLI alias:

- `diagnostics` -> `rover_high_precision_debug`

### `factory_reset`

Current `factory_reset` helper generates:

- `FRESET`

This profile is intentionally modeled as a dedicated factory-reset workflow, not
as a normal runtime profile.

## Safety Policy

Runtime-safe commands are emitted with `ReceiverCommandSafetyLevel::kRuntime`.

Persistent-impact commands are emitted with
`ReceiverCommandSafetyLevel::kPersistent`.

Factory-reset commands are emitted with
`ReceiverCommandSafetyLevel::kFactoryReset`.

Current persistent-impact commands are:

- `SAVECONFIG`

Current factory-reset commands are:

- `FRESET`

This means:

- generation does not need an explicit confirmation
- dispatch still does
- the existing `ReceiverCommandDispatcher` rejects those commands until
  `explicit_safety_confirmation` is set
- the current Unicore `FRESET` path returns the receiver to `115200 bps`, so
  downstream tooling must reconnect at the factory baud, confirm a live
  `VERSIONA` response, recover `COM1` with the explicit
  `CONFIG COM1 <baud> 8 n 1` form, and then verify reachability again at the
  restored baud before continuing

## Conservative response routing

`unicore_response_router.*` currently recognizes only the smallest response set
that is both documented locally and already reused in practical scripts:

- positive:
  - `<OK`
  - `$command,...,response: OK*`
  - `#VERSIONA,...`
- negative:
  - `unsupported command`
  - `PARSING FAILED`
  - `GRAMMAR ERROR`
  - `response can't found device`

These lines map into the generic driver response model as `text_ok` or
`text_error`. Normal telemetry such as `BESTNAVA`, `PVTSLNA`, `RTKSTATUSA`,
`RTCMSTATUSA`, and `SATSINFOA` is ignored by the router.

This is intentionally conservative and does not attempt to infer undocumented
success/failure semantics. The current live-validation fixes also allow the
router to resynchronize on recognized response tokens inside mixed
binary/ASCII-buffered lines.

## Deferred Work

Still intentionally deferred:

- live command transport
- full response parsing / command-result grammar beyond the conservative router
- retry and timeout state machines
- `MODE BASE` orchestration
- survey-in orchestration
- advanced `CONFIG PVTALG`, `CONFIG RTCMDECAUTO`, `CONFIG RTCMPHASERATE`
- binary N4 configuration
