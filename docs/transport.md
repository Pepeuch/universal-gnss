# Transport Layer

This document describes the purpose and current scope of `gnss_transport`.

Today `gnss_transport` provides a small portable abstraction layer plus a couple
of concrete synchronous byte-stream adapters.

It currently provides:

- synchronous byte source / sink interfaces
- generic transport status and error enums
- transport metrics helpers
- in-memory test transports
- a Linux-only POSIX serial adapter
- a Linux-only synchronous TCP client adapter
- a small fixed-capacity ring buffer helper

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
- connect failure
- timeout

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
- opening a transport must not perform any receiver configuration writes by
  itself; guarded live apply remains an explicit higher-layer action

### Stable serial device paths

For robots and field rigs, prefer the udev-created stable symlinks under
`/dev/serial/by-id/` instead of transient kernel names such as `/dev/ttyACM0`
or `/dev/ttyUSB0`.

Recommended ROS 2 configuration:

```text
transport:=serial
serial_device:=/dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00
serial_baud:=921600
receiver_family:=auto
```

Why:

- `/dev/ttyACM*` and `/dev/ttyUSB*` can change when devices are unplugged,
  rebooted, or opened in a different order.
- `/dev/serial/by-id/*` carries USB identity and is stable across boots for
  receivers such as ZED-F9P and many UM982 USB adapters.
- The receiver node passes by-id paths directly to the POSIX serial adapter;
  no special path normalization is required.

Discovery also prefers `/dev/serial/by-id/*` candidates and deduplicates them
against their target `/dev/tty*` node. If a by-id symlink exists, the discovered
path stays the by-id symlink so downstream diagnostics and launch files can
keep using the stable device name.

Platform UARTs such as `/dev/serial0`, `/dev/ttyAMA*`, `/dev/ttyS*`, and
`/dev/ttyTHS*` are excluded from automatic scanning by default because they may
be consoles, Bluetooth, or other peripherals. Enable them only when the board
wiring is known.

## TCP Client Transport

`tcp_client_transport.*` adds the first generic network byte stream in the
project.

Current scope:

- Linux-first synchronous `ByteDuplex`
- connect to a host and port
- blocking or nonblocking operation
- optional connect, read, and write timeouts
- optional `TCP_NODELAY`
- explicit close
- transport metrics and error surfacing

Current non-goals:

- nonblocking TLS handshakes
- reconnect or backoff
- NTRIP HTTP request logic
- async I/O

Current policy:

- keep the transport layer generic, with no NTRIP behavior embedded here
- allow future NTRIP live work to consume this `ByteDuplex`
- use local adopted connected sockets in tests so validation stays
  hardware-independent and internet-independent
- keep reconnect, session policy, and higher-level diagnostics above transport

### TLS client mode

On Linux, `TcpClientTransport` can establish a synchronous OpenSSL client TLS
session when `TcpClientConfig::tls_enabled` is set. Certificate-chain and DNS
host verification use the host system trust store by default. A non-empty
`tls_ca_file` adds a PEM trust bundle without disabling verification.
`tls_client_certificate_file` and `tls_client_private_key_file` optionally
configure a matching PEM client credential pair for mTLS; supplying only one
or invalid/mismatched material fails closed. The `tls_verify_peer=false`
escape hatch exists only for deterministic local test fixtures; production
NTRIP callers must retain verification. Nonblocking handshake ownership
remains out of scope.

## UDP Client Transport

On Linux, `UdpClientTransport` provides a synchronous connected UDP
`ByteDuplex` for one-peer datagram exchange. Each `Write` emits exactly one
datagram and each successful `Read` delivers exactly one complete datagram;
the transport never performs application-level fragmentation.

If the next datagram exceeds the caller buffer, the transport consumes and
discards the entire datagram, returns `kOverflow` with zero bytes, and records
a read error. A subsequent `Read` therefore begins at the next datagram
boundary. Zero-capacity reads are safe no-ops and do not consume a datagram.
The transport supports explicit read timeouts, nonblocking reads, explicit
close, and transport metrics. UDP server/listener, multicast, async, retry,
and NTRIP integration remain out of scope.

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
- a future NTRIP live client can consume the generic TCP `ByteDuplex`
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

- UDP sockets
- async I/O
- reconnect loops
- ROS 2 transport adapters
- ESP32 UART / WiFi / Ethernet adapters
- backpressure policies beyond the current small buffer helper
