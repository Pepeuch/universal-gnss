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

- the local Unicore N4 command manual
- `mowglinext/sensors/unicore/configure_receiver.sh`
- `mowglinext/docs/unicore_profiles.md`

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
- mower-specific assumptions like launch/runtime orchestration

## Generated Command Families

Current runtime-safe command families:

- `MODE ROVER`
- `CONFIG NMEA0183 V410|V411`
- `CONFIG RTK TIMEOUT <seconds>`
- `CONFIG RTK RELIABILITY <a> <b>`
- `CONFIG DGPS TIMEOUT <seconds>`
- output enable commands for:
  - `GPGGA`
  - `PVTSLNA`
  - `BESTNAVA`
  - `RTKSTATUSA`
  - `RTCMSTATUSA`
  - `SATSINFOA`

Current safety-gated commands:

- `SAVECONFIG`
- `CONFIG SIGNALGROUP ...`

## Output Syntax Policy

The builder uses a fixed per-message syntax table.

- `GPGGA` -> `LOG GPGGA ONTIME <period>`
- `PVTSLNA` -> `LOG PVTSLNA ONTIME <period>`
- `BESTNAVA` -> `BESTNAVA <period>`
- `RTKSTATUSA` -> `RTKSTATUSA <period>`
- `SATSINFOA` -> `SATSINFOA <period>`
- `RTCMSTATUSA` -> `RTCMSTATUSA ONCHANGED`

This keeps generation deterministic and aligned with the message classes
already consumed elsewhere in `universal-gnss`.

## Helper Profiles

### Rover

Current rover helper generates:

- `MODE ROVER`
- `CONFIG NMEA0183 V411`
- `CONFIG RTK TIMEOUT 10`
- `CONFIG RTK RELIABILITY 3 1`
- `CONFIG DGPS TIMEOUT 600`
- `LOG GPGGA ONTIME 0.2`
- `LOG PVTSLNA ONTIME 0.2`
- `BESTNAVA 0.2`
- `RTKSTATUSA 1`
- `RTCMSTATUSA ONCHANGED`

### Diagnostics

Current diagnostics helper extends the rover helper with:

- `SATSINFOA 1`

## Safety Policy

Runtime-safe commands are emitted with `ReceiverCommandSafetyLevel::kRuntime`.

Persistent-impact commands are emitted with
`ReceiverCommandSafetyLevel::kPersistent`.

Current persistent-impact commands are:

- `SAVECONFIG`
- `CONFIG SIGNALGROUP ...`

This means:

- generation does not need an explicit confirmation
- dispatch still does
- the existing `ReceiverCommandDispatcher` rejects those commands until
  `explicit_safety_confirmation` is set

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
success/failure semantics.

## Deferred Work

Still intentionally deferred:

- live command transport
- full response parsing / command-result grammar beyond the conservative router
- retry and timeout state machines
- `MODE BASE` orchestration
- survey-in orchestration
- advanced `CONFIG PVTALG`, `CONFIG RTCMDECAUTO`, `CONFIG RTCMPHASERATE`
- binary N4 configuration
