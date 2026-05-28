# Transport Layer

This document describes the purpose and current scope of `gnss_transport`.

Today `gnss_transport` is a portable abstraction foundation only.

It currently provides:

- synchronous byte source / sink interfaces
- generic transport status and error enums
- transport metrics helpers
- in-memory test transports
- a Linux-only POSIX serial adapter
- a small fixed-capacity ring buffer helper

It does not yet provide any OS or network transport implementation.

Exception:

- Linux now has a minimal POSIX serial adapter for local byte-stream I/O

## Purpose

The transport layer exists so future byte movement can stay separate from:

- protocol framing and semantic parsing
- receiver-family logic
- NTRIP request / metrics policy
- ROS 2 integration

The intended split is:

```text
gnss_transport
  -> byte sources / sinks
  -> buffering primitives
  -> future serial / TCP / UDP / replay adapters

gnss_protocols
  -> framing
  -> checksums
  -> semantic parsers

gnss_driver
  -> receiver profiles
  -> stream-family detection
  -> future receiver-family mapping / config

gnss_ntrip
  -> caster config
  -> request generation
  -> correction-flow metrics
```

## Why This Layer Exists

The rest of the stack needs portable byte movement, but it should not have to
care whether those bytes come from:

- a serial port
- a TCP socket
- a UDP stream
- an NTRIP client
- a replay file
- an ESP32 UART or WiFi adapter

By defining the smallest useful byte-stream contract early, later integrations
can share tests and buffering behavior without pulling protocol or driver logic
into transport code.

## Current Interfaces

### Byte streams

`byte_stream.hpp` defines:

- `ByteSource`
- `ByteSink`
- `ByteDuplex`
- `ReadResult`
- `WriteResult`

The current interface is intentionally synchronous and minimal:

- read into a caller-provided buffer
- write from a caller-provided buffer
- expose `IsOpen()`
- support `Close()`

No async semantics are modeled yet.

### Status and errors

`transport_status.hpp` currently distinguishes:

- `kOk`
- `kEndOfStream`
- `kClosed`
- `kError`

`transport_error.hpp` currently provides small generic error categories such as:

- closed
- invalid argument
- overflow
- read failure
- write failure

These are transport-level categories, not protocol-level parse errors.

### Metrics

`transport_metrics.hpp` currently tracks:

- bytes read
- bytes written
- read errors
- write errors
- reconnect count
- last error

This keeps future serial, TCP, NTRIP, replay, and embedded adapters aligned on
basic observability without forcing any ROS 2 dependency.

## Memory Transport

`memory_stream.*` provides the first concrete implementation:

- `MemoryByteSource`
- `MemoryByteSink`
- `MemoryByteDuplex`

These are intended for:

- unit tests
- parser integration tests
- replay-style byte feeding
- future tool-side prototyping

Current behavior:

- reads preserve input order
- writes preserve output order
- EOF is explicit for sources
- close state is explicit
- injected read/write failures can be used in tests
- metrics are updated directly by the transport object

## POSIX Serial Transport

`posix_serial_transport.*` is the first real transport adapter in the project.

Current scope:

- Linux-only
- `ByteDuplex` implementation
- open a device path
- configure a small set of common baud rates
- raw serial read/write
- explicit close
- transport metrics and error surfacing

Current non-goals:

- reconnect logic
- background threads
- async I/O
- line discipline helpers
- receiver configuration

Current policy:

- build only on Linux
- keep the API synchronous and minimal
- allow optional nonblocking or read-timeout configuration
- use pseudo-terminal tests so no real GNSS hardware is required

## Ring Buffer Helper

`ring_buffer.hpp` currently provides a small fixed-capacity byte FIFO.

Current properties:

- compile-time capacity
- no dynamic allocation
- push / pop helpers
- FIFO ordering
- overflow counting

This is intentionally simple enough for portable Linux and embedded use.

## Relationship To Other Layers

`gnss_transport` does not parse bytes.

It only moves them.

Examples:

- a future serial adapter would implement `ByteDuplex`
- a future TCP NTRIP socket would likely provide a `ByteSource` or `ByteDuplex`
- `gnss_protocols` would consume the resulting bytes for framing and parsing
- `gnss_driver` would stay above transport and below application logic
- `receiver_session_runner.*` is the current synchronous bridge from a
  `ByteSource` into a portable receiver session

## Current Driver Bridge

`gnss_driver/receiver_session_runner.*` is the first integration point that
consumes transport abstractions directly.

Current role:

- read fixed-size chunks from any `ByteSource`
- feed those chunks into `ReceiverSession`
- stop on EOF, closed transport, or read error
- surface lightweight runner metrics for offline and embedded use

It is still intentionally not:

- a transport implementation
- a reconnect manager
- an async loop
- a serial or TCP adapter

The current Linux `gnss_serial_monitor` CLI is built on top of this bridge:

```text
PosixSerialTransport
  -> ReceiverSessionRunner
  -> ReceiverSession
  -> normalized runtime state
```

## Deferred Work

Still intentionally deferred:

- TCP / UDP sockets
- TLS
- async I/O
- reconnect loops
- ROS 2 transport adapters
- ESP32 UART / WiFi / Ethernet adapters
- backpressure policies beyond the current small buffer helper
