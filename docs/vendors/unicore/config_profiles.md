# Unicore Config Profiles

This document describes the current portable command-generation layer for
Unicore receivers.

Today this layer generates prepared text `ReceiverCommand` values only.

It does not:

- open serial or TCP transports
- send commands
- parse command responses
- implement retries
- manage survey-in or base-station orchestration

## Purpose

The builder translates a small high-level `UnicoreConfigProfile` into a stable
sequence of CRLF-terminated text commands.

Current implementation lives in:

- `gnss_driver/include/universal_gnss_driver/unicore_config_profile_builder.hpp`
- `gnss_driver/src/unicore_config_profile_builder.cpp`

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

## Deferred Work

Still intentionally deferred:

- live command transport
- response parsing / OK / error handling
- retry and timeout state machines
- `MODE BASE` orchestration
- survey-in orchestration
- advanced `CONFIG PVTALG`, `CONFIG RTCMDECAUTO`, `CONFIG RTCMPHASERATE`
- binary N4 configuration
