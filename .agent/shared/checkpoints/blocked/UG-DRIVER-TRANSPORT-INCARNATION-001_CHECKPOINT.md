# Agent checkpoint

Lifecycle: BLOCKED

Repository: `/workspaces/universal-gnss`
Branch: `main`
HEAD: `179159e864b38204bba02b7375e04fb824ab0a2d`
Upstream: `origin/main` at the same commit

## Objective

Determine whether the concrete POSIX serial transport can prove a receive
incarnation / prior-byte cutoff after an indeterminate configuration request.
Analysis only; do not implement `ReceiverTrafficArbiter`.

## Evidence cache

- `UGA-126_RUNTIME_ARBITRATION_CHECKPOINT.md`: CURRENT; ownership and
  same-target stale-response invariant are established.
- `UG-DRIVER-RESPONSE-FENCE-001_CHECKPOINT.md`: CURRENT; neither u-blox nor
  Unicore supplies a protocol-level response fence, and an indeterminate
  post-write request must be quarantined until a proven recovery boundary.
- `UG-PLAN-005_ROBOT_SECOND_RPI_VALIDATION.md`, Phase F (2026-09-05): CURRENT
  physical evidence now includes actual u-blox tty/major:minor renumbering,
  stale-state detection, container recreation, and safe wrong-receiver refusal.
  No tagged pending A / same-target B response was available, so this evidence
  confirms operational USB handling but does not supply the causal prior-byte
  cutoff required to release quarantine or close UGA-126.

Do not duplicate or revisit those investigations unless repository state or
primary evidence contradicts them.

## Scope

- Current `ByteSource` / `ByteSink` / `ByteDuplex` abstraction.
- `PosixSerialTransport`, its focused tests, and primary POSIX/Linux tty/USB
  API evidence needed for prior-byte behavior.
- User-space, tty, USB driver/controller, and receiver output queues only as
  they affect a provable cutoff.

## Initial facts

- `ByteDuplex` has `Read`, `Write`, `IsOpen`, and `Close` only; no incarnation
  token or flush contract.
- `PosixSerialTransport::Close()` calls `close(fd)`; `Open()` opens and
  configures the named device. Neither currently calls `tcflush` or provides a
  receive-generation boundary.
- Local generations can reject bytes already captured by an old reader, but
  cannot prove delayed sender-side bytes presented on a new fd are new.

## Evidence freshness

- CURRENT: both referenced checkpoints and current `ByteDuplex` /
  `PosixSerialTransport` code at this HEAD.
- CURRENT: focused `test_posix_serial_transport.cpp` proves ordinary PTY
  open/read/write/close and raw-mode setup only; it contains no flush,
  disconnect, re-enumeration, or causal-cutoff test.
- CURRENT: POSIX `tcflush(3p)` and Linux kernel TTY/USB documentation reviewed
  below. No source has been invalidated.

## Concrete buffering and lifecycle semantics

### Buffer ownership

- Universal GNSS user space: session framers, response routers, and future
  arbiter queues can be reset and tagged with a local epoch. This removes only
  bytes already delivered to this process.
- Kernel tty: `TCIFLUSH` discards data received but not read at the terminal;
  `TCIOFLUSH` additionally discards locally written but not transmitted output
  at that instant. POSIX does not say that it stops later input from arriving.
  Linux TTY documentation also permits driver-side data to be added while TTY
  buffering is being managed.
- USB-serial driver / host controller: an input URB can be in flight or a
  driver can hold device-private queues outside the application and tty line
  discipline. A physical USB disconnect kills URBs, but the kernel documents
  that disconnect detection is asynchronous and I/O can fail before it.
- Receiver / bridge: receiver-side UART/USB transmit queues and command
  processing exist outside the OS tty contract. None of the reviewed POSIX,
  Linux USB, u-blox, or Unicore documentation proves they are cleared by a
  host fd operation, `tcflush`, or re-open.

### Operations evaluated

- `tcflush(TCIFLUSH)` / `tcflush(TCIOFLUSH)`: useful best-effort removal of
  *currently terminal-queued* input (and local untransmitted output for
  `TCIOFLUSH`), but no causal cutoff. A prior receiver response may enter the
  tty after the call.
- `close(fd)`: current implementation only closes the fd. Linux may flush
  some TTY/driver resources on last close, but that is not a portable promise
  covering upstream USB or receiver queues; a future byte from the prior
  receiver state can still reach a reopened tty.
- reopen same tty: creates/configures another fd, but current code has no
  flush, identity, re-enumeration observation, or prior-byte exclusion
  contract. The path is not an incarnation identity.
- USB disappearance/re-enumeration: stronger host-side event. Linux usbcore
  kills outstanding URBs on physical disconnect and binds a driver again only
  after disconnect/probe. It still does not prove the receiver or USB-serial
  bridge lost power or discarded an old response before it transmits on the
  new link. Linux USB persistence can also retain the device structure after a
  reset/re-enumeration with matching descriptors.
- device renumbering and process restart: respectively a naming event and a
  user-space reset; neither proves a receiver/bridge/endpoint causal cutoff.

## Decision

No deterministic software-only prior-byte cutoff exists for the current POSIX
serial abstraction. Physical USB removal/re-enumeration is operationally
stronger than close/reopen because host URBs are terminated, but is still not a
portable proof that an old receiver-side response cannot be delivered after
reconnect. A local generation token is necessary to reject captured old bytes
but cannot make a later-delivered old byte new.

## Safe recovery policy

After an indeterminate request, live configuration is permanently quarantined
for the current `PosixSerialTransport` session. The only currently safe
operational recovery is external: an operator must establish a fresh receiver
session under deployment-specific evidence of a real receiver power/reset and
link teardown/re-enumeration. Universal GNSS cannot automatically claim that
this creates a proven cutoff from current contracts.

An ordinary fd close/reopen, `tcflush`, quiet interval, process restart,
observed path change, or USB hotplug notification alone must retain the
quarantine. A receiver replacement is treated the same way unless a future
external identity/recovery provider proves a new receiver/link incarnation.

## Future state/API contract

Required owner state machine:

`Ready -> WriteStarted -> AwaitingResponse`.

- `AwaitingResponse -> Ready` only on a response eligible under the existing
  sole-writer/one-active-operation rule and no prior indeterminate operation.
- `AwaitingResponse -> Indeterminate` on timeout, any post-write cancellation,
  or a write failure after one or more bytes.
- `Indeterminate -> Indeterminate` on parser reset, `tcflush`, close/reopen,
  process restart, observed disconnect/reconnect, or replacement not backed
  by a proven incarnation capability.
- `Indeterminate -> Ready` only on a future `ProvenRecoveryBoundary` supplied
  by a transport-specific/external provider. The owner increments its local
  epoch, destroys parser/router/application state, and accepts only bytes
  tagged with the new epoch after that provider succeeds.

The minimum eventual API is not a generic `Flush` method. It is a capability
returning a new immutable transport-incarnation token only when its concrete
implementation can attest an end-to-end old-byte cutoff. Current POSIX serial
must report this capability unavailable. The token protects in-process races;
the provider's documented physical/link contract is what makes the transition
safe.

## Dependency status

`UG-DRIVER-RESPONSE-FENCE-001` is **not unblocked** for automatic recovery.
UGA-126 is canonically PARTIAL with HARDWARE_REQUIRED validation, but cannot yet
implement a reconnect that releases quarantine. It could
proceed only under an explicitly accepted hard-quarantine scope: after any
post-write indeterminate outcome, live configuration remains disabled for the
current receiver session and recovery is operator/deployment managed. Do not
start that implementation without explicit scope authorization.

## Required decision

Determine whether `tcflush`, close/reopen, physical USB re-enumeration, or
process restart can prove no byte causally belonging to the old transport
incarnation becomes eligible. If none can, define the safest operational
quarantine/recovery boundary and future state/API/regression requirements.

## Do not accept

- parser clearing, tcflush alone, close/reopen alone, quiet-time sleeping,
  mutexes, or local generations as a causal cutoff without an explicit
  applicable contract.

## Validation

No broad tests. Read focused transport tests and primary documentation; define
future unit/integration/hardware regression evidence rather than executing
unrelated suites.

Required future regression/integration matrix:

- unit: epoch rejects bytes captured before a retired incarnation;
- unit: `tcflush`, close/reopen, and hotplug signals alone never move
  `Indeterminate` to `Ready`;
- unit: partial write, post-write cancellation, timeout, and disconnect enter
  and retain quarantine; no B(target X) dispatch occurs;
- PTY integration: verify current open/read/write/close and, if a flush API is
  ever added, prove only present tty input is discarded, not a causal boundary;
- USB-serial hardware integration: inject A response before/after `tcflush`,
  close/reopen, and disconnect/reconnect; prove the implementation remains
  quarantined absent a qualified recovery provider;
- qualified recovery-provider integration: prove a forced receiver power/link
  cycle, device disappearance, new link establishment, fresh incarnation
  token, stale old-epoch rejection, then B acceptance;
- replacement test: same tty path or renumbered path without provider evidence
  remains quarantined; a provider-attested new physical receiver gets a new
  epoch.

## Exact next step

Keep automatic recovery blocked. If product requirements need automated exit
from quarantine, authorize a separate hardware/deployment-specific recovery
provider design with an explicit power/link-cutoff attestation. Otherwise,
explicitly scope UGA-126 to hard quarantine and operator-managed new sessions
before any arbiter implementation.
