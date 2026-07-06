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

- `gnss_driver/include/universal_gnss_driver/unicore_model_profile.hpp`
- `gnss_driver/src/unicore_model_profile.cpp`
- `gnss_driver/include/universal_gnss_driver/unicore_config_profile_builder.hpp`
- `gnss_driver/src/unicore_config_profile_builder.cpp`
- `gnss_driver/include/universal_gnss_driver/unicore_response_router.hpp`
- `gnss_driver/src/unicore_response_router.cpp`

## Model-aware profile seam

Unicore configuration planning no longer relies on a family-wide
`CONFIG SIGNALGROUP 3 6` default.

The current `UnicoreModelProfile` layer answers five backend-only questions:

- what model is this
- does it support `dual_antenna_baseline`
- which documented `CONFIG SIGNALGROUP` presets are known for automatic defaults
  and UI hints
- whether the documented mower-oriented rover dynamic mode is supported
- what should be skipped for unknown or non-baseline models

Current documented model profiles are intentionally narrow:

- unknown/generic
  - safe non-baseline fallback
  - no documented signal-group selections
  - no verified mower-oriented rover dynamic mode
  - no automatic `CONFIG SIGNALGROUP`
- `UM960`
  - known non-baseline
  - no documented `CONFIG SIGNALGROUP` selections in the current repo sources
  - documented support for `MODE ROVER SURVEY MOW`
  - no automatic rover signal-group selection
- `UM980`
  - non-baseline
  - documented explicit selections: `1`, `2`, `8`
  - documented support for `MODE ROVER SURVEY MOW` with `Build7923+`
  - no automatic rover signal-group selection
- `UM981`
  - known non-baseline
  - no documented `CONFIG SIGNALGROUP` selections in the current repo sources
  - no documented mower-oriented rover dynamic mode in the current repo sources
  - no automatic rover signal-group selection
- `UM982`
  - documented dual-antenna baseline capable
  - documented explicit selections: `4 5`, `3 6`, `5 0`, `7 0`
  - documented support for `MODE ROVER SURVEY MOW` with `Build7650+`
  - documented portable rover default: `3 6`
- `UB9A0`
  - non-baseline
  - documented explicit selections: `2`, `9`
  - documented support for `MODE ROVER SURVEY MOW`
  - no automatic rover signal-group selection

Undocumented or unconfirmed models stay on the generic fallback. The planner
warns and skips `CONFIG SIGNALGROUP` instead of guessing from family, RTK mode,
or runtime baseline fields.

## Reference Inputs

The current builder was shaped by:

- `docs/vendors/unicore/Unicore Reference Commands Manual For N4 High Precision Products_V2_EN_R1.4.pdf`
- the current portable runtime-mapping boundary documented in
  `docs/vendors/unicore/runtime_mapping.md`

What was reused conceptually:

- a small set of practical rover/runtime commands
- the documented Unicore output-command form:
  - `MESSAGE COM1 <period>`
  - `MESSAGE COM1 ONCHANGED`

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
- `MODE ROVER SURVEY MOW`
- `CONFIG NMEA0183 V410|V411`
- `CONFIG RTK TIMEOUT <seconds>`
- `CONFIG RTK RELIABILITY <a> <b>`
- `CONFIG DGPS TIMEOUT <seconds>`
- model-validated `CONFIG SIGNALGROUP ...`
- output enable commands for:
  - `GPGGA`
  - `GPGSV`
  - `GPGST`
  - `PVTSLNA`
  - `BESTNAVA`
  - `RTKSTATUSA`
  - `RTCMSTATUSA`
  - `SATSINFOA`

Default runtime-safe profiles do not emit `UNLOG`; they enable the required
MowgliNext outputs without cutting any pre-existing receiver outputs on the
active port.

Current safety-gated commands:

- `SAVECONFIG`
- `FRESET`

## Output Syntax Policy

The builder uses the documented Unicore abbreviated ASCII output syntax with an
explicit `COM1` target because the current portable runtime planner configures
and applies Unicore profiles on `COM1`.

- `GPGGA` -> `GPGGA COM1 <period>`
- `GPGSV` -> `GPGSV COM1 <period>`
- `GPGST` -> `GPGST COM1 <period>`
- `PVTSLNA` -> `PVTSLNA COM1 <period>`
- `BESTNAVA` -> `BESTNAVA COM1 <period>`
- `RTKSTATUSA` -> `RTKSTATUSA COM1 <period>`
- `SATSINFOA` -> `SATSINFOA COM1 <period>`
- `RTCMSTATUSA` -> `RTCMSTATUSA COM1 ONCHANGED`

The low-level builder only accepts documented Unicore periodic values:

- `1`
- `0.5`
- `0.2`
- `0.1`
- `0.05`
- `0.02`

These correspond to the documented `1`, `2`, `5`, `10`, `20`, and `50 Hz`
rates.

The user-facing planner only applies `rate_hz` overrides to `BESTNAVA`. Exact
documented requests such as `5 Hz` and `10 Hz` are preserved. Unsupported
values such as `7 Hz` are normalized to the nearest documented rate with a
warning, for example `7 Hz -> 5 Hz -> 0.2 s`.

This keeps generation deterministic and aligned with the message classes
already consumed elsewhere in `universal-gnss`.

## Helper Profiles

### `runtime_only`

`runtime_only` is handled at the portable planner layer as a zero-command,
read-only/no-op path. The Unicore builder does not emit a dedicated command
sequence for it.

### `rover_high_precision`

Current `rover_high_precision` helper always generates the core rover/runtime
commands:

- `MODE ROVER` for unknown or unsupported models
- `MODE ROVER SURVEY MOW` for documented mower-oriented models
- `CONFIG NMEA0183 V411`
- `CONFIG RTK TIMEOUT 10`
- `CONFIG RTK RELIABILITY 3 1`
- `CONFIG DGPS TIMEOUT 600`
- `GPGGA COM1 1`
- `GPGSV COM1 1`
- `GPGST COM1 1`
- `PVTSLNA COM1 1`
- `BESTNAVA COM1 0.2`
- `RTKSTATUSA COM1 1`
- `RTCMSTATUSA COM1 ONCHANGED`
- `SATSINFOA COM1 1`

Model-specific signal-group behavior:

- `UM960`, `UM980`, `UM982`, and `UB9A0` use the documented mower-oriented
  rover dynamic mode `MODE ROVER SURVEY MOW`
- `UM980` requires `Build7923+` and `UM982` requires `Build7650+`; the current
  portable planner cannot verify firmware build metadata, so it emits a
  warning when those models are selected
- unknown models and models without documented support keep the safe fallback
  `MODE ROVER`
- `UM982` adds the documented portable rover selection
  `CONFIG SIGNALGROUP 3 6`
- `UM960` and `UM981` stay known non-baseline, but the current repo sources do
  not document portable signal-group selections for them
- `UM980` and `UB9A0` do not get an automatic signal-group command because the
  current portable layer has no documented automatic rover selection for those
  models
- unknown or undocumented models skip `CONFIG SIGNALGROUP` and keep the
  receiver's current signal-group configuration unchanged

This keeps the primary rover state on `BESTNAVA` at `5 Hz` while trimming the
fallback/observability logs down to `1 Hz`. It also keeps dual-antenna
signal-group choices gated by the confirmed receiver model instead of by RTK
state or runtime baseline fields. The profile is additive and does not disable
existing outputs before enabling the required runtime messages.

If persistent mode is requested, the builder appends only:

- `SAVECONFIG`

Legacy CLI alias:

- `rover` -> `rover_high_precision`

### `rover_high_precision_debug`

Current `rover_high_precision_debug` helper extends
`rover_high_precision` by restoring the heavier baseline/status log:

- `PVTSLNA COM1 0.2`

This debug profile is intentionally higher-bandwidth than
`rover_high_precision` and is meant for short-lived capture or troubleshooting
sessions, not normal runtime. It inherits the same model-aware
`CONFIG SIGNALGROUP` gating as `rover_high_precision`.

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

`CONFIG SIGNALGROUP` remains a runtime command. Safety comes from
model-profile validation rather than from upgrading it to persistent/factory
severity:

- only documented selections are accepted
- unknown models skip the command and warn
- explicit overrides are rejected when the selected model does not document the
  requested combination

Current documented explicit override selections are:

- `UM960`: none documented in the current repo sources
- `UM980`: `1`, `2`, `8`
- `UM981`: none documented in the current repo sources
- `UM982`: `4 5`, `3 6`, `5 0`, `7 0`
- `UB9A0`: `2`, `9`

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
