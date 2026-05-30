# Driver Layer

This document describes the purpose and current scope of `gnss_driver`.

Today `gnss_driver` is only a portable abstraction foundation.

It does not yet contain:

- live receiver configuration
- correction injection
- auto-detection loops
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
- RF / jamming monitoring
- PPS
- survey-in
- base mode
- rover mode

These are receiver-level claims, not protocol parser claims.

### Receiver profiles

`receiver_profiles.*` provides a small built-in set of static profiles:

- `generic_nmea`
- `ublox_f9_f10`
- `unicore_um98x_placeholder`
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
- `CONFIG SIGNALGROUP`
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
- `CONFIG SIGNALGROUP` is treated as persistent/safety-gated because it is
  model-specific and not a lightweight runtime tweak
- generation is deterministic and transport-agnostic

What this does not do:

- serial writes
- response parsing
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

### Stream detection

`stream_detector.*` provides lightweight byte-stream classification into likely
protocol families:

- `NMEA`
- `UBX`
- `RTCM3`
- `Unicore ASCII`
- `Unknown`

Current policy:

- reuse the existing protocol framers
- classify only after a complete recognizable frame or sentence is seen
- require valid checksums for `UBX` and `RTCM3`
- accept `NMEA` only when it looks like a real sentence and not an invalid
  checksum frame
- avoid classifying `$...` text as Unicore ASCII to prevent collisions with
  NMEA

This is intentionally not a full auto-detection state machine yet.

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

- `UbloxSession`
- `UnicoreSession`

and exposes a unified runtime-state and metrics surface.

Current modes:

- explicit `ublox`
- explicit `unicore`
- conservative `auto_detect`

Current policy:

- explicit mode is preferred for production use
- auto mode only locks on vendor-specific evidence
- `UBX` selects the u-blox session
- Unicore ASCII selects the Unicore session
- `NMEA` alone does not select a vendor
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
- a full auto-detection engine
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

`ReceiverSession` sits one level above those vendor sessions:

- it chooses which vendor session should own the stream
- it keeps a small, unified metrics surface
- it forwards runtime state from the selected vendor session

## Deferred Work

The following driver-layer work is intentionally deferred:

- receiver config commands
- UBX `CFG-*`
- ACK / NAK handling
- Quectel config messages
- Unicore config messages
- binary `N4` Unicore session routing
- auto-detection state machines
- correction injection paths
- receiver-family runtime arbitration
- ROS 2 driver nodes

## Current Goal

The current goal is simple:

- make protocol support explicit
- make receiver expectations explicit
- keep future driver work from leaking into `gnss_protocols` or `gnss_core`
