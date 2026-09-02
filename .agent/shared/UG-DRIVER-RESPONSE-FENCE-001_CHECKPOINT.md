# Agent checkpoint

Repository: `/workspaces/universal-gnss`
Branch: `main`
HEAD: `179159e864b38204bba02b7375e04fb824ab0a2d`
Upstream: `origin/main` at the same commit

## Objective

Define the smallest protocol-safe response fencing or recovery contract for
`UG-DRIVER-RESPONSE-FENCE-001`, the dependency blocking `UGA-126` live runtime
arbitration. Analysis only; do not implement `ReceiverTrafficArbiter`.

## Evidence cache

`UGA-126_RUNTIME_ARBITRATION_CHECKPOINT.md` is CURRENT and authoritative for
existing ownership, routing, and same-target conclusions. Reuse it; do not
repeat its architecture investigation.

CURRENT primary evidence already reviewed:

- u-blox M10 interface description `UBXDOC-304424225-20430`, sections 3.5,
  3.9, and CFG: ACK/NAK carries only `(clsID,msgID)`, no transaction nonce.
- Unicore N4 Commands Manual R1.13: supported ASCII command family; no known
  response nonce, sequence, or command/response order guarantee.

## Established invariant

After A(target X) has indeterminate completion (timeout or post-write cancel),
no later B(target X) may accept any A response. A mutex, queue flush, sleep,
or serial arrival ordering is not a fence. A receiver-incarnation epoch only
rejects already-captured prior-owner responses; it does not prove a
same-incarnation late wire response belongs to B.

## Results

### u-blox: no deterministic live fence

- `UBX-ACK-ACK` / `UBX-ACK-NAK` identifies only the acknowledged `(clsID,
  msgID)`. All supported profile writes use `UBX-CFG-VALSET (0x06/0x8a)`, so
  same-target A and B remain indistinguishable. Receiver-side VALSET
  transaction fields are configuration atomicity, not a host response ID.
- The documented ACK deadline (up to one second) is not an ordering or fence
  guarantee. No reviewed primary documentation promises command processing or
  response emission ordering for a barrier/poll.
- `UBX-CFG-RST` is explicitly not reliably acknowledged; older firmware may
  emit an incomplete ACK before resetting. It is therefore not a response
  fence or safe automatic recovery operation.

### Unicore: no deterministic live fence

- Supported ASCII configuration has only generic `<OK`, optional echoed
  `$command,...,response: OK*`, and known textual errors in the current
  conservative contract. No reviewed primary documentation provides a command
  nonce, response sequence, abort, or processing-order guarantee.
- An identical repeated command has an identical optional echo, while `<OK` is
  unscoped. `FRESET` is destructive, changes the documented operational baud
  to 115200, and requires a recovery workflow; it is not a portable automatic
  fence and documentation does not establish that it purges earlier replies.

### Transport/reincarnation result

- `ByteDuplex` exposes only read/write/open/close. `PosixSerialTransport`
  `Close()` is an OS `close`; `Open()` configures a new fd but has no
  flush/discard, generation, peer-identity, or prior-byte exclusion contract.
  It cannot establish a portable serial reincarnation fence.
- TCP/serial close-and-reopen is likewise not a portable receiver protocol
  fence in the current abstraction. A future transport-specific incarnation
  API must explicitly guarantee that no byte from the prior transport session
  can be delivered after the new session becomes eligible. Current code has
  no such API or test evidence.

### Smallest safe policy

There is no supported transition from post-write indeterminate completion to
`Ready` on the same live transport. Per family, every active configuration
command is non-retryable after any byte is written unless a future
family-specific fence is documented and implemented. The portable policy is:

1. cancellation before dispatch/first write byte -> `Ready`;
2. full successful write -> `Awaiting`;
3. positive response while `Awaiting` -> `Succeeded` -> `Ready`;
4. deterministic negative response while `Awaiting` -> `Rejected` -> `Ready`;
5. timeout, cancellation after any byte, or write error after any byte ->
   `Indeterminate` and block all further live configuration dispatch;
6. transport disconnect while a request may have been written ->
   `Disconnected/Indeterminate`; clear parser/router/application state and
   block dispatch;
7. only a future explicitly guaranteed transport reincarnation/recovery fence
   can move `Indeterminate` or `Disconnected` to `Ready`.

`Succeeded` and `Rejected` are deterministic only under the future arbiter's
sole-writer, one-active-operation invariant and only when no earlier operation
is indeterminate. The current Unicore generic text router is insufficient for
stronger per-command attribution and must not be used to bypass this state
machine.

### Decision

No portable generic fence exists in current u-blox, Unicore, or transport
contracts. `UGA-126` remains blocked. The minimum viable future design is an
explicit `Indeterminate` quarantine state plus a separately specified,
transport- and family-qualified recovery/reincarnation capability; it must not
silently re-enable a live session after timeout or post-write cancellation.

## Investigation boundaries

- Per family: documented response correlation, processing order/barrier,
  abort/reset, and reconnect semantics only.
- Inspect the minimum current transport close/open contract needed to determine
  whether it supplies a portable reincarnation fence.
- Do not alter production code, tests, TODO, or UGA-126 conclusions absent
  contradictory evidence.

## Initial state model to validate

- `Ready`: dispatch permitted.
- `Awaiting`: one command response eligible.
- `Succeeded` / `Rejected`: deterministic terminal result, return `Ready`.
- `CancelledBeforeWrite`: no wire request, return `Ready`.
- `Indeterminate`: timeout or cancellation after any write; reject all further
  live commands until a documented family fence or proven exclusive recovery /
  reincarnation completes.
- `Disconnected`: invalidate active operation and router/parser state; no
  dispatch until new incarnation is established.

## Candidate evidence / files

- `gnss_transport/*serial*`, `byte_stream.hpp`, connection/open lifecycle.
- u-blox and Unicore primary command documentation for reset/abort/barrier.
- UGA-126 checkpoint and current command/response engine only as cached
  implementation evidence.

## Rejected until proven

- u-blox CFG transaction bitfield as a host-response fence.
- Unicore echoed/generic OK as a same-command correlation key.
- drain-to-idle, arbitrary waiting, poll/barrier, mutex, or blind reopen.
- automatic u-blox `CFG-RST` or Unicore `FRESET` after an indeterminate write.

## Validation

No tests rerun: analysis-only and no relevant repository change. Final work
must define a focused future regression matrix, including the A-late-B case.

Required future regression matrix:

- successful u-blox ACK and Unicore accepted/negative response return to
  `Ready` while streaming remains routed;
- pre-write cancellation emits no bytes and returns to `Ready`;
- partial write, post-write cancellation, and timeout enter `Indeterminate`;
- A(target X) indeterminate -> late positive X -> B(target X): B is rejected
  before dispatch until a proven recovery fence completes;
- unrelated/malformed/vendor traffic does not affect `Awaiting`;
- disconnect clears router/parser/application state and blocks dispatch;
- only a mock transport that proves an explicit new-incarnation cutoff may
  exit `Disconnected/Indeterminate`; old-epoch records are rejected;
- failed recovery remains quarantined and does not poison a later independently
  established incarnation.

## Exact next step

Keep `UGA-126` blocked. Create a separate authorized design task for a
transport-incarnation/recovery capability, beginning by defining which concrete
transport(s) can prove a byte-source cutoff after reconnect. Only then decide
whether a family-qualified recovery mechanism can safely release the
`Indeterminate` quarantine; do not implement `ReceiverTrafficArbiter` first.
