# Driver Layer

This document describes the purpose and current scope of `gnss_driver`.

Today `gnss_driver` is only a portable abstraction foundation.

It does not yet contain:

- serial transport
- TCP / UDP transport
- receiver configuration
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

gnss_driver
  -> receiver profiles
  -> protocol support declarations
  -> stream-family detection
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

## Deferred Work

The following driver-layer work is intentionally deferred:

- serial transport
- TCP / UDP transport
- receiver config commands
- UBX `CFG-*`
- Quectel config messages
- Unicore config messages
- auto-detection state machines
- stream timeouts and reconnection logic
- correction injection paths
- receiver-family runtime arbitration
- ROS 2 driver nodes

## Current Goal

The current goal is simple:

- make protocol support explicit
- make receiver expectations explicit
- keep future driver work from leaking into `gnss_protocols` or `gnss_core`
