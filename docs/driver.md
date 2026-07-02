# Driver Layer

This document describes the purpose and current scope of `gnss_driver`.

Today `gnss_driver` is the portable receiver-integration layer for the current
low-level stack.

It does not yet contain:

- autonomous live receiver configuration loops
- correction injection
- autonomous attach/connect loops
- ROS 2 nodes

## Purpose

The driver layer sits between raw protocol parsing and higher-level receiver
integration.

The intended split is:

```text
gnss_protocols
  -> framing
  -> checksums
  -> semantic message parsers

gnss_transport
  -> future serial / TCP / UDP / replay adapters

gnss_driver
  -> receiver profiles
  -> protocol support declarations
  -> stream-family detection
  -> portable receiver sessions
  -> future receiver-family mapping and configuration

gnss_core
  -> normalized runtime state
  -> runtime aggregation
```

## Why This Layer Exists

`gnss_protocols` can parse bytes and produce typed semantic records, but it
should not decide:

- which receiver family is attached
- which protocols a receiver can consume or emit
- which configuration paths are legal
- which runtime fields are expected from a given receiver family

Those are driver concerns, even before transport or configuration commands are
implemented.

Likewise, `gnss_driver` should not own raw byte transport implementations.
Those belong in `gnss_transport`.

## Current Abstractions

### Protocol support

`protocol_support.hpp` defines portable protocol-support flags for:

- `NMEA`
- `UBX`
- `RTCM3`
- `Unicore ASCII`
- `Unicore binary`

These flags describe what a receiver profile can accept as input or emit as
output.

### Receiver capabilities

`receiver_capabilities.hpp` defines generic high-level feature flags for:

- RTK
- heading
- dual antenna
- dual-antenna baseline
- RF / jamming monitoring
- PPS
- survey-in
- base mode
- rover mode
- constellation configuration
- CFG-VALSET configuration
- signal-group configuration
- ASCII command configuration

These are receiver-level claims, not protocol parser claims.

### Receiver profiles

`receiver_profiles.*` provides a small built-in set of static profiles:

- `generic_nmea`
- `ublox_f9_f10`
- `unicore_um98x_placeholder`
- `unicore_um980`
- `unicore_um982`
- `unicore_ub9a0`
- `quectel_placeholder`

These profiles are intentionally conservative.

They are useful for:

- declaring likely protocol families
- documenting expected high-level capabilities
- seeding future driver selection and configuration paths

They are not yet:

- full SKU databases
- hardware probes
- guarantees about every model in a vendor family

For Unicore, the built-in profile table is now paired with
`unicore_model_profile.*`, a small model-aware seam that answers four driver
questions without mixing runtime parser state into config planning:

- what model is selected
- whether that model supports `dual_antenna_baseline`
- which `CONFIG SIGNALGROUP` selections are documented and allowed
- when `CONFIG SIGNALGROUP` should be skipped because the model is unknown or
  because no documented automatic rover selection exists

Current documented Unicore model profiles are intentionally narrow:

- unknown/generic
  - safe non-baseline fallback
  - no automatic `CONFIG SIGNALGROUP`
- `UM960`
  - known non-baseline
  - no documented signal-group selections in the current repo sources
- `UM980`
  - non-baseline
  - documented explicit signal-group selections only
- `UM981`
  - known non-baseline
  - no documented signal-group selections in the current repo sources
- `UM982`
  - documented dual-antenna baseline capable
  - documented portable rover signal-group default
- `UB9A0`
  - non-baseline
  - documented explicit signal-group selections only

### Receiver command model

`receiver_command.hpp` defines the first portable command/request model for
future receiver configuration work.

Current model coverage:

- generic command kind
- target receiver selector
- expected response kind
- timeout and retry policy
- binary or text payload container
- command safety level

Current safety levels are:

- `runtime`
- `persistent`
- `factory_reset`

Current policy:

- runtime commands are considered safe-by-default
- persistent and factory-reset commands require explicit safety confirmation
- this layer does not generate vendor payloads yet
- this layer does not send bytes to a transport yet

### Receiver command dispatcher

`receiver_command_dispatcher.*` is the first write-side bridge between the
portable command model and `gnss_transport`.

Current role:

- accept prepared `ReceiverCommand` values
- verify dispatch safety using the existing safety helpers
- reject empty payloads by default
- write text or binary payload bytes to any `ByteSink`
- expose lightweight dispatcher metrics

Current policy:

- runtime commands can be sent without extra confirmation
- persistent and factory-reset commands are rejected unless explicitly confirmed
- no ACK/NAK handling exists inside the dispatcher
- no retry or timeout orchestration exists inside the dispatcher
- this layer writes prepared bytes only; it does not generate vendor payloads

Current dispatcher metrics include:

- commands attempted
- commands sent
- safety rejections
- invalid-command rejections
- bytes written
- write errors

### Receiver command responses and transactions

`receiver_command_response.hpp` and `receiver_command_transaction.hpp` add the
first portable response/result state model on top of prepared
`ReceiverCommand` values.

Current model coverage:

- transaction id
- embedded command
- transaction state:
  - `pending`
  - `sent`
  - `acknowledged`
  - `rejected`
  - `timed_out`
  - `failed`
- concrete response kind:
  - `none`
  - `ack`
  - `nak`
  - `text_ok`
  - `text_error`
  - `timeout`
- optional created / sent / completed timestamps
- send attempt count
- retry budget taken from the command retry policy

Current helpers:

- `mark_sent()`
- `mark_ack()`
- `mark_nak()`
- `mark_timeout()`
- `can_retry()`
- `reset_for_retry()`

Current policy:

- this layer does not parse ACK/NAK by itself
- this layer does not run timers or a retry loop
- this layer does not dispatch bytes by itself
- dispatch safety remains owned by `ReceiverCommand` + `ReceiverCommandDispatcher`
- vendor-specific live config flows remain deferred

### Receiver command transaction engine

`receiver_command_transaction_engine.*` is the first small coordination layer
that ties together:

- `ReceiverCommandDispatcher`
- `ReceiverCommandTransaction`
- externally supplied `ReceiverCommandResponse` values

Current role:

- create transaction ids for prepared commands
- dispatch commands synchronously through the existing dispatcher
- keep one active transaction slot at a time
- expose the current transaction plus the last completed transaction
- accept externally supplied responses and apply them to the active transaction
- support manual timeout checks and manual retry requests
- expose lightweight transaction-engine metrics

Current policy:

- only one active transaction is supported at a time
- responses must be supplied by the caller after protocol parsing/mapping
- no protocol parsing happens inside the engine
- no automatic serial read loop exists
- no background timeout or retry thread exists
- no multi-command queue exists
- timeouts stay in the active slot so the caller can retry or reset explicitly
- live receiver-configuration orchestration remains deferred

### Receiver config application

`receiver_config_application.*` is the next thin orchestration layer above the
transaction engine for prepared command/profile sequences.

Current role:

- accept a prepared list of `ReceiverCommand` values
- dispatch one command at a time through `ReceiverCommandTransactionEngine`
- keep a single active command/transaction at a time
- advance the internal command index after `ack` / `text_ok`
- optionally advance past `nak` / `text_error` when `continue_on_error=true`
- support manual timeout checks plus retry dispatch through the underlying
  transaction engine
- expose current command index, current command, state, and lightweight
  application metrics

Current state model:

- `idle`
- `running`
- `waiting_for_response`
- `completed`
- `failed`

Current policy:

- this layer is externally driven
- responses must come from outside the application
- vendor routers such as `UbloxResponseRouter` and `UnicoreResponseRouter`
  are expected to supply `ReceiverCommandResponse` values
- no protocol parsing happens inside the application
- no serial read loop or parser ownership exists here
- no threading, async I/O, or background timers exist here
- one command is active at a time
- caller-driven `Step()` dispatches the next prepared command after the prior
  command has finished

### u-blox response router

`ublox_response_router.*` is the small bridge between parsed UBX frames and the
generic transaction-response surface.

Current role:

- accept framed `UbxFrame` values from `gnss_protocols`
- reuse `ParseUbxAck()` to recognize `ACK-ACK` / `ACK-NAK`
- reuse the existing UBX response mapper to produce `ReceiverCommandResponse`
  values
- ignore non-response UBX traffic such as `NAV-PVT`, `NAV-SAT`, `NAV-STATUS`,
  and `MON-RF`
- queue routed responses so a caller can peek/pop them synchronously
- expose lightweight routing metrics

Current policy:

- this router does not own a serial source
- this router does not dispatch commands
- this router does not own a transaction engine
- this router does not parse protocols other than UBX
- this router does not run retries, timers, or scheduling
- future wiring to `ReceiverCommandTransactionEngine` remains explicit and
  caller-driven

### Unicore response router

`unicore_response_router.*` is the equivalent narrow bridge for conservative
Unicore text command responses.

Current role:

- accept already-separated ASCII lines or caller-supplied byte chunks
- recognize only a small documented/practical success set:
  - `<OK`
  - `$command,...,response: OK*`
  - `#VERSIONA,...`
- recognize only a small explicit failure set already used in the local
  validator/config scripts:
  - `unsupported command`
  - `PARSING FAILED`
  - `GRAMMAR ERROR`
  - `response can't found device`
- map success to `ReceiverCommandResponseKind::kTextOk`
- map failure to `ReceiverCommandResponseKind::kTextError`
- ignore normal Unicore telemetry such as `BESTNAVA`, `PVTSLNA`,
  `RTKSTATUSA`, `RTCMSTATUSA`, and `SATSINFOA`
- queue routed responses so a caller can peek/pop them synchronously
- expose lightweight routing metrics

Current policy:

- response mapping is intentionally conservative
- no full Unicore command/result grammar exists yet
- this router does not own a serial source
- this router does not dispatch commands
- this router does not own a transaction engine
- this router does not run retries, timers, or scheduling
- future wiring to `ReceiverCommandTransactionEngine` remains explicit and
  caller-driven

### u-blox ACK/NAK response mapping

`ubx_command_response_mapper.*` is the first narrow bridge from parsed UBX
ACK/NAK records into the generic driver-side response model.

Current role:

- accept parsed `UbxAckRecord` values from `gnss_protocols`
- map `ACK-ACK` into `ReceiverCommandResponseKind::kAck`
- map `ACK-NAK` into `ReceiverCommandResponseKind::kNak`
- preserve response timestamps
- expose the target UBX class/id in response text for lightweight diagnostics
- verify whether an ACK/NAK target matches the expected outbound UBX command
  header

Current policy:

- this bridge does not read from a serial stream
- this bridge does not own transaction state transitions
- this bridge does not run retries or timeout logic
- this bridge only inspects already-prepared UBX command frames
- live parser/transport integration remains deferred

### u-blox config profile builder

`ublox_config_profile_builder.*` is the first vendor-specific bridge from a
high-level configuration intent to prepared `ReceiverCommand` objects.

Current role:

- describe a small `UbloxConfigProfile`
- convert that profile into a deterministic list of prepared `ReceiverCommand`
  values
- reuse the existing `UBX-CFG-VALSET` payload builders from `gnss_protocols`
- mark commands as `runtime` or `persistent` based on requested safety and
  target CFG layers

Current coverage:

- UART1 baud-rate command generation
- measurement-rate command generation
- message enable/disable command generation
- constellation enable/disable command generation
- modest predefined helper profiles for rover, diagnostics, and a minimal base
  monitoring setup

Current policy:

- this layer generates commands only
- it does not write to a transport
- persistent safety is inferred when a profile targets `BBR` or `Flash`
- explicit confirmation is still enforced later by the dispatcher, not at
  generation time

Still deferred:

- live ACK/NAK transaction handling
- automatic transaction/retry orchestration
- survey-in orchestration
- full base-station setup
- live serial/TCP configuration flows

### Unicore config profile builder

`unicore_config_profile_builder.*` is the equivalent vendor-specific bridge
for Unicore text commands.

Current role:

- describe a small `UnicoreConfigProfile`
- convert that profile into deterministic CRLF-terminated text
  `ReceiverCommand` values
- separate runtime-safe commands from safety-gated persistent commands
- reuse the existing command/dispatcher model without sending anything

Current coverage:

- `MODE ROVER`
- `CONFIG NMEA0183`
- `CONFIG RTK TIMEOUT`
- `CONFIG RTK RELIABILITY`
- `CONFIG DGPS TIMEOUT`
- model-validated `CONFIG SIGNALGROUP`
- output-message enables for:
  - `GPGGA`
  - `PVTSLNA`
  - `BESTNAVA`
  - `RTKSTATUSA`
  - `RTCMSTATUSA`
  - `SATSINFOA`
- optional `SAVECONFIG`

Current policy:

- output commands use a small per-message syntax table:
  - `LOG ... ONTIME` for `GPGGA` and `PVTSLNA`
  - direct-period syntax for `BESTNAVA`, `RTKSTATUSA`, and `SATSINFOA`
  - `ONCHANGED` for `RTCMSTATUSA`
- `SAVECONFIG` is generated only when persistent storage is explicitly requested
- `CONFIG SIGNALGROUP` remains a runtime command, but it is only generated
  when the selected `UnicoreModelProfile` confirms that exact documented
  selection
- unknown or undocumented Unicore models keep the receiver's current
  signal-group configuration unchanged and surface a warning instead of
  guessing
- generation is deterministic and transport-agnostic

What this does not do:

- serial writes
- full Unicore response/result parsing
- retries
- survey-in/base orchestration
- binary N4 configuration

The message-format choices are inspired by the practical per-message syntax
table developed in MowgliNext, but the builder does not copy ROS 2 glue,
start scripts, or Mowgli-specific runtime assumptions.

### Receiver config profiles

`receiver_config_profile.hpp` defines a small generic vocabulary for future
high-level configuration intents such as:

- rover
- base
- survey-in
- NMEA output
- RTCM output
- diagnostics output

These profiles declare:

- required receiver features
- required input/output protocol support
- default command safety level

Today they are validation and planning primitives only.

They are not yet:

- vendor payload generators
- live config transactions
- guarantees that every profile maps to one vendor command

### Receiver driver abstraction

`receiver_driver.*` adds the first portable high-level receiver-driver
interface above vendor sessions and profile builders.

Current role:

- expose receiver vendor and family identity
- expose receiver capabilities and supported config profiles
- expose current normalized runtime state
- accept byte/string input and forward it into the wrapped vendor session
- build prepared `ReceiverCommand` sequences for supported high-level profiles
- keep driver capability claims aligned with the portable runtime surface,
  including additive baseline capabilities only when a backend really provides
  them

Current concrete drivers:

- `NmeaDriver`
- `UbloxDriver`
- `UnicoreDriver`

Current policy:

- one driver instance wraps one vendor-session implementation
- the driver owns no serial port or transport lifecycle
- the driver does not auto-detect hardware or reconnect
- the driver does not execute config commands by itself
- profile generation remains offline and returns prepared command lists only

Current vendor coverage:

- `NmeaDriver`
  - family: `NMEA`
  - profiles: none
  - runtime state: delegated to `NmeaSession`
  - capabilities: read-only NMEA output support, RTK read visibility through
    standard `GGA fix_quality`, rover mode
- `UbloxDriver`
  - family: `F9/F10`
  - profiles: rover, diagnostics, base
  - runtime state: delegated to `UbloxSession`
  - capabilities: RTK, RF monitoring, constellation configuration,
    CFG-VALSET, base mode, rover mode, PPS
- `UnicoreDriver`
  - family: `UM98x`
  - profiles: rover, diagnostics
  - runtime state: delegated to `UnicoreSession`
  - capabilities: model-aware
  - generic/unknown fallback:
    RTK, ASCII command configuration, rover mode, base mode, survey-in, PPS
  - `UM960` and `UM981` stay known non-baseline models without documented
    signal-group support in the current repo sources
  - `UM982` additionally advertises heading compatibility, dual antenna,
    dual-antenna baseline, and documented signal-group configuration
  - `UM980` and `UB9A0` advertise documented signal-group configuration only;
    they are not treated as baseline-capable

Still deferred:

- automatic receiver attachment
- live transport ownership
- auto-connect lifecycle
- command execution ownership
- ROS 2 integration
- NTRIP integration

### Receiver discovery

`receiver_discovery.*` is the first portable read-only discovery layer for
finding candidate serial receivers and identifying likely receiver families.

Its job is intentionally narrow:

- enumerate likely Linux serial receiver paths
- prefer stable `/dev/serial/by-id/*` symlinks when available
- fall back to `/dev/ttyACM*` and `/dev/ttyUSB*`
- support explicit-path probing even when enumeration is skipped
- probe common baud rates without writing any bytes
- reuse the existing protocol framers and semantic detectors
- return structured evidence and a conservative confidence level

Current Linux enumeration order is:

- `/dev/serial/by-id/*`
- `/dev/ttyACM*`
- `/dev/ttyUSB*`

Optional platform UART scanning exists but stays disabled by default.

When `include_platform_uarts=true`, discovery also considers:

- `/dev/serial0`
- `/dev/serial1`
- `/dev/ttyAMA*`
- `/dev/ttyS*`
- `/dev/ttyTHS*`

This is intentionally opt-in because onboard UARTs may belong to:

- a Linux serial console
- Bluetooth firmware links
- unrelated board peripherals
- other application-specific device wiring

Current default baud probe order is:

- `921600`
- `460800`
- `230400`
- `115200`
- `38400`
- `9600`

Current family detection is score based:

- `high`
  - score `>= 100`
- `medium`
  - score `>= 20`
- `low`
  - score `> 0`
- `none`
  - score `<= 0`

Scoring examples:

- valid `UBX` frame -> `+100`, family `ublox`
- Unicore `RTKSTATUSA` -> `+100`, family `unicore`
- Unicore `PVTSLNA` -> `+100`, family `unicore`
- valid `GGA` -> `+20`, family `nmea` when no stronger vendor evidence exists
- random ASCII serial text -> `-50`, family `unknown`
- MAVLink heartbeat -> `-200`, family `unknown`
- silent port -> score `0`, family `unknown`, reason `no_data`

Auto baud probing stops once a high-confidence probe reaches the configured
score threshold, currently `100`.

Current implementation intentionally does not:

- configure the receiver
- send probes or poll commands
- persist any setting
- own reconnect logic
- attach automatically inside ROS 2
- replace the existing stream/session auto-routing layer

The implementation reuses existing Universal GNSS framers and parsers rather
than building a second protocol detector.

Conceptually reused from the local `ELT_RTKBase` reference:

- stable `/dev/serial/by-id` preference
- practical `ttyACM` / `ttyUSB` scan order
- a baud probing order biased toward modern high-rate RTK receivers

Intentionally not reused:

- project-specific shell/device heuristics
- receiver writes during discovery
- external dependencies or imported code
- configuration-side probing

### Stream detection

`stream_detector.*` provides lightweight byte-stream classification into likely
protocol families:

- `NMEA`
- `UBX`
- `RTCM3`
- `Unicore ASCII`
- `Unicore binary`
- `Unknown`

Current policy:

- reuse the existing protocol framers
- classify only after a complete recognizable frame or sentence is seen
- require valid checksums for `UBX` and `RTCM3`
- accept `NMEA` only when it looks like a real sentence and not an invalid
  checksum frame
- avoid classifying `$...` text as Unicore ASCII to prevent collisions with
  NMEA

This is intentionally not the same thing as the new serial-port discovery
layer:

- `stream_detector.*` classifies bytes already coming from an attached stream
- `receiver_discovery.*` finds candidate ports, probes baud rates, and uses
  those stream-level detectors conservatively

### Generic NMEA session

`nmea_session.*` is the portable session layer for generic NMEA-only receivers.

Its job is:

- accept incremental byte chunks or strings
- frame NMEA sentences with the existing NMEA framer
- route conservative runtime-producing sentences:
  - `GGA`
  - `RMC`
  - `GSA`
  - `GSV`
  - `GST`
- map standard `GGA fix_quality` into normalized `rtk_mode` without adding any
  receiver-specific write/config behavior
- parse `VTG` and `ZDA` semantically without projecting them into runtime state
- merge runtime updates through `GnssRuntimeAggregator`
- expose current runtime state and lightweight session metrics

Current metrics include:

- bytes seen
- framed sentences seen
- parsed vs rejected sentences
- runtime updates
- semantic-only sentence count
- unknown sentences
- malformed checksum / truncated / overflowed sentences

This session is intentionally not:

- a serial driver
- a TCP driver
- a receiver configuration engine
- a speed/course runtime mapper
- a GNSS wall-clock runtime mapper
- a ROS 2 node

### Unicore session

`unicore_session.*` is the first portable receiver-session layer in
`gnss_driver`.

Its job is narrow:

- accept byte chunks or strings
- frame Unicore ASCII records
- route supported messages to existing semantic parsers
- map those records into partial `GnssRuntimeState` updates
- merge them through `GnssRuntimeAggregator`
- expose current runtime state and lightweight session metrics

Current supported routed messages:

- `PVTSLNA`
- `BESTNAVA`
- `RTKSTATUSA`
- `RTCMSTATUSA`
- `SATSINFOA`
- `BESTSATA`
- `JAMSTATUSA`
- `FREQJAMSTATUSA`
- `HWSTATUSA`
- `AGCA`
- binary `BESTNAVB`
- binary `PVTSLNB`

Current metrics include:

- bytes seen
- framed lines seen
- parsed vs rejected records
- runtime updates applied
- unknown records
- malformed trailing or overflowed lines

This is intentionally not yet:

- a serial driver
- a TCP driver
- a ROS 2 node
- a receiver configuration engine
- a reconnecting session manager

### u-blox session

`ublox_session.*` is the equivalent portable session layer for mixed u-blox
streams.

Its job is:

- accept incremental byte chunks
- route `UBX`, `NMEA`, and `RTCM3` using the existing framers
- parse supported `UBX` semantic messages:
  - `NAV-PVT`
  - `NAV-DOP`
  - `NAV-SAT`
  - `NAV-STATUS`
  - `MON-RF`
- optionally merge valid `NMEA` runtime updates
- keep `RTCM3` as correction metadata only
- merge all runtime updates through `GnssRuntimeAggregator`
- expose current runtime state and lightweight session metrics

Current metrics include:

- bytes seen
- UBX / NMEA / RTCM frame counts
- parsed vs rejected frames
- runtime updates
- unknown frames
- malformed checksum / truncated / overflowed frames
- RTCM message-type counts

This session is intentionally not yet:

- a serial driver
- a TCP driver
- a command/configuration engine
- an ACK/NAK session manager
- a survey-in controller
- a ROS 2 node

### Generic receiver session

`receiver_session.*` is the portable router on top of vendor sessions.

It wraps:

- `NmeaSession`
- `UbloxSession`
- `UnicoreSession`

and exposes a unified runtime-state and metrics surface.

Current modes:

- explicit `nmea`
- explicit `ublox`
- explicit `unicore`
- conservative `auto_detect`

Current policy:

- explicit mode is preferred for production use
- auto mode only locks on vendor-specific evidence
- generic `NMEA` can be selected explicitly
- `UBX` selects the u-blox session
- Unicore ASCII selects the Unicore session
- `NMEA` alone stays undecided by default
- `NMEA` can become a generic fallback only when
  `allow_generic_nmea_auto_detect` is explicitly enabled
- `RTCM3` alone does not select a vendor
- ambiguous mixed vendor evidence stays undecided

Current generic metrics include:

- total bytes seen
- selected session kind
- selection lock state
- runtime updates
- malformed record count
- unknown record count

Vendor-specific child metrics remain accessible separately through the wrapped
session accessors.

This router is intentionally not:

- a serial transport
- a TCP transport
- a reconnect lifecycle manager
- a full auto-attach engine
- a ROS 2 node

### Receiver session runner

`receiver_session_runner.*` is the first small bridge between `gnss_transport`
and `gnss_driver`.

Its job is intentionally narrow:

- accept any `ByteSource`
- read synchronous fixed-size chunks
- feed those chunks into a `ReceiverSession`
- stop on EOF, closed source, or read error
- expose lightweight runner metrics

Current runner metrics include:

- bytes read
- chunks read
- EOF observed
- read-error count
- runtime updates observed from the wrapped receiver session

Current policy:

- this is not a serial driver
- this is not a TCP client
- this does not own reconnect logic
- this does not configure the receiver
- this only bridges portable byte input into an already configured session

This allows future POSIX serial, TCP, replay, NTRIP, or embedded adapters to
plug into the same session surface without moving transport concerns into the
session layer.

## Relationship To Protocol Parsers

The parser layer and driver layer have different jobs.

`gnss_protocols` answers:

- can we frame this byte stream?
- can we validate checksums?
- can we decode this known message type?

`gnss_driver` answers:

- what receiver family might this stream belong to?
- what protocols does this receiver likely support?
- what high-level runtime features should later driver logic expect?

## Relationship To Runtime Mapping

This driver foundation does not yet map protocol records into
`GnssRuntimeState`.

That work currently lives in:

- `gnss_protocols` for conservative per-message mapping helpers
- `gnss_core` for runtime aggregation

Later concrete drivers may:

- consume protocol semantic records
- enrich or arbitrate runtime updates per receiver family
- declare receiver-specific capability expectations

`UnicoreSession` is one example of that bridge:

- `gnss_protocols` still owns framing and semantic decode
- `gnss_core` still owns normalized runtime state and merge policy
- `gnss_driver` owns the receiver-session routing glue

`UbloxSession` is the same kind of bridge for mixed `UBX` / `NMEA` / `RTCM3`
streams:

- `gnss_protocols` owns framing, checksum validation, and semantic decode
- `gnss_core` owns normalized state and merge invariants
- `gnss_driver` owns byte-stream session routing and per-session metrics

`NmeaSession` is the same kind of bridge for generic NMEA-only receivers:

- `gnss_protocols` owns NMEA framing, checksum validation, and semantic decode
- `gnss_core` owns normalized state and merge invariants
- `gnss_driver` owns NMEA session routing and per-session metrics

`ReceiverSession` sits one level above those vendor sessions:

- it chooses which vendor session should own the stream
- it keeps a small, unified metrics surface
- it forwards runtime state from the selected vendor session

## Deferred Work

The following driver-layer work is intentionally deferred:

- ROS 2-side discovery integration
- live transport ownership
- command execution ownership inside drivers
- live config transactions
- multi-command scheduling
- Quectel config messages
- broader binary `N4` Unicore semantic routing beyond `BESTNAVB` / `PVTSLNB`
- automatic attach / reconnect state machines
- correction injection paths
- receiver-family runtime arbitration
- ROS 2 driver nodes

## Current Goal

The current goal is simple:

- make protocol support explicit
- make receiver expectations explicit
- keep future driver work from leaking into `gnss_protocols` or `gnss_core`
