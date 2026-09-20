# Pre-release software audit checkpoint

Lifecycle: ACTIVE
Disposition: ACTIVE
Audit reference: `UG-PRE-RELEASE-001`
Date: 2026-09-19
Repository: `/workspaces/universal-gnss`
Branch: `fix/ublox-active-baud-verification`
Audited HEAD: `5c50f6c3b1b6bae75c8242081d8d4a6cba490caa`
Locally recorded upstream: `origin/fix/ublox-active-baud-verification`, same SHA
Merge base with locally recorded `origin/main`: `f974b565100b4f2aa3d522cd9a292f30f905e8bc`
Remote: `https://github.com/Pepeuch/universal-gnss.git`; no fetch during checkpoint creation.
Worktree at audit completion / checkpoint start: clean; no submodules.
Execution identity: `ubuntu`, UID 1000; no forced-identity exception.

## Objective and authorized scope

Preserve the final release-blocker audit for direct continuation. The audit was
read-only, with no production/test edits, reformatting, commit, or push. The
original checkpoint request authorized only this record and its index entry.
The 2026-09-20 remediation requests authorized scoped F03/F04/F07, F05, and
subsequent F06 work recorded below; none authorizes commit or push.

## F03/F04/F07 remediation at current HEAD (2026-09-20)

All three software findings are IMPLEMENTED at `b17f0cd`.
This checkpoint remains `ACTIVE` for the other audit findings. No canonical
UGA classification or progress count changed.

- Root cause: `ReceiverSupervisor::Stop()` closed a plain POSIX descriptor
  from one thread while the receiver worker was blocked in `read()` with
  `VMIN=1` and `VTIME=0`. Linux does not guarantee that cross-thread close
  wakes that read. The same unsynchronized descriptor was also available to
  the NTRIP forwarding thread.
- Selected lifecycle: `PosixSerialTransport` owns a private nonblocking wake
  pipe and puts its serial fd in nonblocking mode. Every read/write acquires a
  lifecycle operation lease, waits in `poll()` on serial-or-wakeup, then does
  the syscall. `Close()` marks closing, signals the pipe, waits for all leases
  to retire, and only then closes the serial/wakeup fds. Whole `Open()` and
  `Close()` operations are serialized; no in-flight operation can use a
  descriptor after close/reuse. Metrics/config snapshots are locked copies.
- Supervisor ownership: `active_` only owns/replaces the current link under
  `correction_mutex_`; each link now serializes its RTCM writer independently.
  Stop removes the active link, closes/cancels its transport, then abandons the
  writer. The receiver worker also retires its published link on every exit,
  including Stop racing publication. The active-link mutex is not held over
  a potentially blocking write, so a blocked forward cannot prevent close.
- F07: `ReceiverSessionRunner::StepOnceWithResult()` distinguishes data, idle
  (`kOk/0`), and terminal reads. Existing `StepOnce()` retains its old
  data-only boolean contract. `ReceiverSupervisor` waits its configured idle
  poll interval on idle and preserves session/incarnation and parser/runtime
  state. PTY hangup still reconnects. Normal cancellation returns `kClosed`
  without counting an I/O failure or reporting a terminal receiver error.
- F03 regression: `gnss_runtime_test_posix_receiver_supervisor` opens a real
  PTY slave with `read_timeout_ms=0`, leaves its master open and silent, and
  performs 50 bounded Stop cycles without injecting bytes. Its CTest timeout
  is five seconds, so the old blocked-read path cannot pass indefinitely. The
  audited baseline separately reproduced the old >1.5 s hang, released only
  by an injected GGA; the new test was not run on that old binary.
- F04/F07 regressions: the direct POSIX test exercises simultaneous silent
  read, RTCM queue flush with pending suffix, and Close; it proves bounded
  cancellation and suffix abandonment. The timeout PTY test splits a valid
  GGA across several idle intervals, proves the parser completes it, then
  proves the runtime timestamp/observation count and incarnation stay fixed
  through further silence. A separate PTY peer-hangup case proves reconnect.
- Validation at `591d09f` plus this worktree: full build and focused tests
  PASS; full CTest 68/68 PASS outside the network-restricted sandbox. The
  same suite inside the sandbox gave 63/68 with the five pre-existing
  socket/TLS/SIGPIPE fixture failures. GCC ThreadSanitizer builds of both
  focused PTY tests and the existing NTRIP supervisor suite PASS with no
  reports when launched through `setarch x86_64 -R`; ordinary launch hit
  `unexpected memory mapping`. The NTRIP suite also needs unrestricted socket
  fixtures in this sandbox.

F05 is independently IMPLEMENTED at `46acb9a`. F06 is IMPLEMENTED in the
current uncommitted worktree as recorded below. F08 and independent release
qualification gates remain open.

Finding references `F01` through `F08` are stable within this audit only, not
new UGA IDs or replacements for canonical backlog items. F01/F02 are
implemented at HEAD; F03/F04/F07 are implemented at `b17f0cd`; F05 is
implemented at `46acb9a`; F06 is implemented in the current uncommitted
worktree.
`TODO.md` and `docs/status/uga_backlog.json` remain authoritative; this checkpoint
does not change their classifications, conservation accounting, or progress.

## Evidence interpretation and dependencies

- AUDITED BASELINE: the source paths/symbols below at `5c50f6c`; current
  implementation and validation are summarized above.
- Reproductions used existing `build/` executables, not a fresh build during
  the audit. They corroborate the source traces but are not clean-build or CI
  attestations. No reproduction scripts or large logs were saved.
- PTY reproductions used newly allocated local pseudo-terminals, never a
  physical receiver. Their compact recipes/results are preserved below.
- During the read-only audit, no ThreadSanitizer, stalled-TLS reproduction, or
  injected partial-write end-to-end regression was run. Later F05 loopback
  reproduction and TSan evidence are recorded in its remediation section.
- The blocked response-fence and transport-incarnation checkpoints remain
  valid for the physical cutoff boundary. Their broad implementation summaries
  must not be read as proof of end-to-end apply safety: F01/F02 and F06 qualify
  those claims at this HEAD. The engine-level quarantine evidence still holds.
- Canonical `UGA-126` remains `PARTIAL / HARDWARE_REQUIRED`; `UGA-127` remains
  `PARTIAL`. `UGA-142` is currently `IMPLEMENTED` in the manifest; F05 is a
  concrete follow-up within its TLS scope, not an automatic reclassification.

## Release-blocking findings

### F01 — IMPLEMENTED AT HEAD — Optional partial write can escape quarantine through apply phases

Scope: DRIVER / TOOLS. Related backlog: `UGA-126`, with failure reporting under
`UGA-127`. Evidence: complete static call-path trace; deterministic with injected
partial-write failure, not dependent on physical recovery proof.

Locations:

- `gnss_driver/src/receiver_command_transaction_engine.cpp`, `StartTransaction`
  (baseline line 68): partial dispatch correctly calls `QuarantineSession()`.
- `gnss_driver/src/receiver_config_application.cpp`, `Step`,
  `HandleCommandFailure`, `CompleteCurrentCommand` (lines 102, 252, 289).
- `gnss_tools/src/config_apply.cpp`, `ExecuteUnicoreCommandPhase`,
  `ExecuteUnicoreSignalGroupAwarePhase` (lines 1877, 2046, 2183).

Trigger: explicitly optional `CONFIG SIGNALGROUP` writes some bytes, then gets
an error/zero-progress write. Its engine quarantines, but application failure
handling still allows optional continuation. The single-command phase becomes
completed / `kPartialSuccess`; the enclosing workflow accepts that status,
reopens/probes and, if those checks succeed, runs later phases with new engines.
This loses the quarantined owner without a qualified recovery boundary.

Coverage: `TestPartialWriteQuarantinesSession` covers the engine alone;
`TestUnicoreRuntimeApplyStopsWhenOptionalSignalGroupTimesOut` covers timeout,
whose separate handler explicitly forbids continuation. Neither covers optional
partial-write propagation across phases.

Implemented at current HEAD after
`ccb9d8ad8dd4811ab6006d83f22795bdd4bf4140`:

- `ReceiverConfigApplication` now makes engine indeterminacy terminal before
  optional/`continue_on_error` policy and exposes it in its result.
- `CommandPhaseOutcome` carries the fact explicitly; SIGNALGROUP, recovery,
  and runtime baud-switch wrappers return immediately when it is set.
- The direct SIGNALGROUP result now takes the flag from its phase rather than
  inferring it from `kTimedOut`.
- The generic result log now says a dispatched command (not only a timeout)
  may have been applied.

Regression evidence:

- `TestOptionalPartialWriteStopsIndeterminateApplyDespiteContinueOnError`:
  optional partial write, then zero-progress/error; no later application
  dispatch despite `continue_on_error`.
- `TestUnicoreRuntimeApplyStopsWhenOptionalSignalGroupWriteIsPartial`:
  runtime Unicore SIGNALGROUP receives a five-byte accepted prefix then a
  failed remainder; result is `kDispatchFailed` plus
  `receiver_state_indeterminate=true`, and no post-write reopen, VERSIONA,
  later command, or `SAVECONFIG` occurs.

Validation at the current worktree: full build PASS; direct engine/application/
config-apply tests PASS; CTest 62/67 with five pre-existing sandbox loopback/
SIGPIPE failures unrelated to F01; clang-format and `git diff --check` PASS.

Canonical UGA accounting is unchanged: UGA-126 remains `PARTIAL /
HARDWARE_REQUIRED`; this software containment does not establish an automatic
recovery/incarnation cutoff.

### F02 — IMPLEMENTED AT HEAD — Already-received responses can acknowledge later commands

Scope: TOOLS / DRIVER. Related backlog: `UGA-126`. Evidence: source trace and PTY
reproduction; deterministic for a pre-dispatch response batch.

Locations: `gnss_tools/src/config_apply.cpp`, `ProcessUbloxBytes`,
`ProcessUnicoreBytes`, `ApplyQueuedUbloxResponses`, `ApplyQueuedUnicoreResponses`,
`ExecuteConfigApply` (lines 1576, 1605, 1631, 1656, 2926).

Trigger: send A -> receive multiple responses in one read -> acknowledge A ->
retain remaining responses -> dispatch B -> consume an already-received
response as B's ACK. No dispatch eligibility boundary rejects the old records.

PTY recipe: run `build/gnss_tools/gnss_config_apply --json --family ublox
--device <new-PTY> --baud 115200 --profile rover_high_precision --apply-mode
runtime-only --timeout-ms 200 --confirm`. Answer discovery/final identity polls
with valid MON-VER (`MOD=ZED-F9P`). On the first CFG-VALSET only, send one batch
of 13 checksum-valid ACKs targeting `0x06/0x8a`. Send no ACK for commands 2–13.
Observed: 13 commands written, exit 0, `status=ok`, `commands_completed=13`,
`responses_applied=13`.

Coverage: `TestUbloxRuntimeApplyStillWorks` and
`TestUnicoreRuntimeApplyStillWorks` preload the complete response sequence and
therefore accept the defective behavior rather than prove causal acknowledgment.

Implemented at current HEAD after
`b9fd3abf8fead45ecd8330dade0d090bb9fd5c73`:

- Apply-layer `ResponseCausalityFence` assigns monotonic software observation
  generations and snapshots the last observed generation at every dispatch.
- A response must have a capture generation strictly later than that dispatch
  boundary before the existing semantic transaction matching is evaluated.
- UBX and Unicore routers preserve the capture generation. A fragmented UBX
  frame or Unicore line retains the generation of its first observed fragment,
  so a response begun before a new dispatch is conservatively ineligible.
- Same-read future responses are discarded, not held for a later command.
- Healthy apply fixtures now emit ACK/OK responses in reaction to command
  writes rather than preloading a future response batch.

Regression evidence:

- `TestUbloxPreDispatchQueuedAckCannotAcknowledgeNextCommand` and
  `TestUnicorePreDispatchQueuedResponseCannotAcknowledgeNextCommand` preload
  A plus future B acknowledgement data. They prove only A completes, B is
  dispatched, then normally times out with its stale response discarded.
- `TestUbloxRuntimeApplyStillWorks` and `TestUnicoreRuntimeApplyStillWorks`
  remain successful with write-triggered replies.

Retry policy: the config application currently never retries after a sent
command because timeout quarantine is terminal (F01). If a future application
path emits `kRetryDispatched`, the same fence records that new physical
dispatch and conservatively rejects observations captured before it.

Validation at the current worktree: full build PASS; direct engine/application/
config-apply tests PASS; CTest 62/67 with the known sandbox loopback/SIGPIPE
failures unrelated to F02; clang-format and `git diff --check` PASS.

Canonical UGA accounting is unchanged: UGA-126 remains `PARTIAL /
HARDWARE_REQUIRED`; this filters software-already-captured responses only and
does not prove cutoff of kernel, bridge, UART, firmware, or device queues.

### F03 — HIGH / IMPLEMENTED AT HEAD — Native supervisor stop hangs on a silent serial receiver

Scope: DEPLOYMENT / DRIVER. Related plan: `UG-PLAN-001` / `UG-PLAN-002` lifecycle.
Evidence: source trace and Linux PTY reproduction; software-reproducible.

Locations: `gnss_runtime/src/universal_gnss_supervisor.cpp`, `main` (line 203),
opens blocking serial with read timeout 0;
`gnss_runtime/src/receiver_supervisor.cpp`, `Stop` (line 187), closes from another
thread and joins; `gnss_transport/src/posix_serial_transport.cpp`, `Read`.

PTY recipe: run the existing supervisor with `--device <new-PTY> --baud 115200
--receiver-family nmea`; keep the peer open and silent. Wait for
`connected=true`, then send SIGINT. Observed: still running after 1.5 seconds;
supplying one valid GGA releases the pending read and immediately permits exit 0.
The elapsed check is an observation, not a proposed timing workaround.

Coverage: supervisor tests use `FakeTransport::Close`, which explicitly wakes
its condition variable. That fake does not establish POSIX read cancellation.

Smallest correction direction: bounded/cancellable acquisition with an explicit
wakeup and owner-controlled close. Add a silent-PTY shutdown regression; also
preserve nonterminal idle-read semantics from F07.

Current disposition: IMPLEMENTED. The cancellation architecture and 50-cycle
real-PTY regression are recorded above. The old CLI reproduction was observed
at audited HEAD; the new regression passed in the current worktree without
injecting serial traffic.

### F04 — HIGH / IMPLEMENTED AT HEAD — Concurrent POSIX descriptor lifecycle is unsynchronized

Scope: DEPLOYMENT / DRIVER. Related plan: `UG-PLAN-001` / `UG-PLAN-002`.
Evidence: source-level C++ race; scheduling-dependent; no sanitizer run.

Locations: `gnss_runtime/src/receiver_supervisor.cpp`, `Run` (lines 258, 264),
`ForwardRtcm` / `FlushPendingRtcm` (lines 457, 498), `Stop`;
`gnss_runtime/src/posix_serial_factory.cpp`, `MakePosixSerialTransportFactory`;
`gnss_transport/src/posix_serial_transport.cpp`, `Read`, `Write`, `IsOpen`, `Close`.

Trigger: receiver worker accesses the plain `fd_` during Read while Stop or the
NTRIP worker closes it; alternatively Run closes while forwarding writes.
The correction mutex does not protect Run's transport access. There is a C++
data race and a descriptor-lifetime/reuse hazard; shared_ptr protects object
lifetime but not fd ownership.

Coverage: the supervisor fake protects transport methods with a mutex, masking
the production adapter's missing synchronization. Existing serial tests do not
exercise the supervisor's concurrent close/read/write schedule.

Smallest correction direction: define one I/O/lifecycle owner or a synchronized,
cancellable transport protocol. An atomic integer alone does not protect the
OS descriptor lifetime. Verify forced close/read/write schedules and TSan where
available, without holding an uncancellable blocking read under a close mutex.

Current disposition: IMPLEMENTED for the POSIX transport and native supervisor.
Descriptor state and metrics are locked; active I/O leases prevent close/reuse
until read/write returns; complete Open/Close operations are serialized.
Dedicated real-PTY concurrent read/write/close and blocked RTCM flush tests,
the existing supervisor forwarding suite, and focused TSan runs pass. This is
software evidence, not qualification of physical USB/receiver behavior.

### F05 — HIGH / IMPLEMENTED IN CURRENT WORKTREE — TLS handshake is not bounded by configured timeouts

Scope: NTRIP / transport. Related backlog: `UGA-142`.
Evidence: source trace, accepted-but-silent TCP/TLS reproduction before the
fix, deterministic loopback regressions after the fix.

Locations: `gnss_transport/src/tcp_client_transport.cpp`, `Open`, `StartTls`;
`gnss_ntrip/src/ntrip_client.cpp`, `Connect`;
`gnss_runtime/src/receiver_supervisor.cpp`, `RunNtrip`, `Stop`.

Trigger: peer accepts TCP, leaves the connection open, and never completes TLS.
StartTls uses blocking SSL_connect without an enclosing deadline. TCP connect
timeout does not bound that call. A TLS-enabled supervisor can then wait for
its NTRIP thread indefinitely during Stop.

Coverage: `TestTlsConfigurationAndHandshakeFailure` covers closed peer and
configuration rejection; `TestVerifiedTlsLoopback` covers prompt handshake and
certificate verification. Neither exercises an open, silent handshake peer.

Implemented correction: TLS setup remains synchronous, but temporarily uses
`O_NONBLOCK` and repeats `SSL_connect()` on `SSL_ERROR_WANT_READ` / `WANT_WRITE`
after `poll()`. One `steady_clock` deadline spans TCP candidate connects and
the entire TLS handshake; remaining time is recalculated after every poll,
EINTR, and readiness event. `connect_timeout_ms=0` means a finite 5000 ms
default for TLS only. DNS resolution remains outside that budget; plain TCP
timeout semantics are unchanged. The socket returns to blocking mode on
success. Timeout maps to `kTimeout`; verification failure, peer close, and
other fatal TLS errors retain their own classifications. Failed setup frees
SSL/SSL_CTX and closes the candidate socket.

Cancellation: `TcpClientConfig::connect_cancelled` is an optional synchronous
TLS setup check. TLS socket waits sample it at most every 25 ms. The native
supervisor composes its atomic `stopping_` state into that check, so Stop wakes
the NTRIP worker through its owned handshake path and joins it; Stop never
cross-thread closes the in-flight TLS descriptor. The callback must be
thread-safe, remain valid through setup, and return promptly.

Regression evidence: `SilentTlsLoopbackServer` accepts real TCP and reads the
client hello but sends no TLS bytes or close. Before production edits, the
new transport regression exceeded an external 3-second watchdog (exit 124).
After the fix, a configured 100 ms handshake returns `kTimeout` under the
generous 1-second test bound; `IsOpen()==false`, `native_fd()==-1`, and the
server observes client EOF. The same transport reconnects to a verified TLS
server, whose socket mode is restored to blocking. A paced partial TLS record
creates repeated readiness without extending a 150 ms total deadline. An
explicit cross-thread cancellation regression returns `kClosed` and releases
the socket before its 5000 ms deadline. The NTRIP supervisor test reaches the
same accepted, silent handshake and proves `Stop()` joins in under 1 second
without server TLS traffic or a detached worker. Both fixture threads join.

Validation: full build PASS; full CTest 68/68 PASS with loopback access,
including verified TLS, certificate-chain/hostname rejection, peer close,
mTLS, plain TCP, NTRIP client, supervisor, and preserved POSIX PTY tests.
Focused GCC TSan TCP/TLS and NTRIP supervisor suites PASS with no reports
using `setarch x86_64 -R`; supervisor TSan suite passed ten repeats. Direct
TSan launch requires the previously established address-layout workaround.
This is bounded software setup evidence, not a claim about DNS resolver
latency, post-connect I/O cancellation, or physical serial/USB recovery.
No TLS SIGPIPE defect was established by this finding.

## High-confidence non-blocking findings

### F06 — MEDIUM / IMPLEMENTED IN CURRENT WORKTREE — Indeterminate state is lost in some apply results

Scope: TOOLS. Related backlog: `UGA-127`. Evidence: source trace and PTY reproduction.
`gnss_tools/src/config_apply.cpp`: `CommandPhaseOutcome` (line 1669) omits the
engine flag; `ExecuteUnicoreRuntimeBaudSwitchWorkflow` and
`ExecuteUnicoreRecoveryWorkflow` copy phase status/error only. The dedicated
SIGNALGROUP result infers indeterminacy only from `kTimedOut` (line 2859).
The generic read-error exit (line 2940) also omits post-dispatch indeterminacy.

PTY recipe: config-apply with `--family unicore --model UM982 --baud 115200
--config-baud 460800 --profile rover_high_precision --apply-mode runtime-only
--timeout-ms 200 --confirm --json`; supply valid VERSIONA to discovery/baud
probes, then no response to `MODE ROVER SURVEY MOW`.
Observed: exit 1, `status=timed_out`, `receiver_state_indeterminate=false`, while
error text says the dispatched command left the session indeterminate.

Current root cause and propagation: F01 had already added the independent
flag to `ReceiverConfigApplicationResult` and `CommandPhaseOutcome`, so the
historical UM982 115200→460800 timeout and optional SIGNALGROUP partial write
already returned true and stopped later work. Remaining holes were both
post-dispatch read-error exits in `ExecuteConfigApply` and
`ExecuteUnicoreCommandPhase`: they returned `kReadFailed` without setting the
flag, because the engine had not timed out or quarantined on a transport read
failure. The phase helper then overwrote the fact with its false engine flag.
Several wrappers copied a child flag with assignment rather than preserving
an already-true enclosing flag. The relevant path is engine/application result
→ generic finalizer or Unicore command phase → SIGNALGROUP/runtime-baud/
recovery wrapper → `ConfigApplyResult` → text/JSON.

Regression-first evidence: seven new config-apply cases were added before the
production fix. The historical UM982 timeout passed at the old F06 baseline;
six post-dispatch read-error cases failed only the top-level indeterminacy
assertion: generic Unicore, optional SIGNALGROUP, verified runtime baud-switch,
factory-reset recovery profile, persistent profile, and u-blox. Existing
optional SIGNALGROUP partial-write and timeout tests stayed green. The
recovery test uses a response-bearing command after reset; FRESET itself is a
no-response command and is not a suitable read-failure trigger.

Implemented rule: a read error while `ReceiverConfigApplication` waits for a
dispatched command marks the attempt indeterminate while retaining
`kReadFailed`. Application, phase, SIGNALGROUP, recovery, runtime-baud, and
top-level merges now use logical OR; no wrapper resets true to false. This is
not inferred from `kTimedOut`, an error string, or another terminal status.
F01's terminal return still prevents later phase, reopen/probe, or persistence
after an indeterminate command. Successful and optional-rejection
`kPartialSuccess` fixtures explicitly assert false.

Focused validation: driver transaction-engine, driver config-application, and
tools config-apply binaries PASS after the fix. Full build PASS; CTest 68/68
PASS with loopback access. Repository-wide and touched-file clang-format-21
checks, `git diff --check`, shared checkpoint audit (zero problems/warnings),
and backlog-status check PASS. Canonical
`UGA-126` stays `PARTIAL / HARDWARE_REQUIRED`; `UGA-127` remains `PARTIAL`.
No hardware causal cutoff or automatic quarantine recovery is claimed.

### F07 — MEDIUM / IMPLEMENTED AT `b17f0cd` — Idle read is mistaken for receiver disconnection

Scope: DEPLOYMENT / DRIVER. Related plan: `UG-PLAN-001`.
Evidence: deterministic static trace, not an executed regression.
`gnss_driver/src/receiver_session_runner.cpp`, `StepOnce` (line 42), returns false
for `kOk/0`; `ReceiverSupervisor::Run` (line 258) interprets loop termination as
disconnect. A nonblocking/read-timeout transport can therefore reopen, reset
runtime/parser state and increment incarnation on ordinary temporary silence.
The default CLI uses blocking/no-timeout reads; F07 concerns other supported
factory configurations and must be considered when correcting F03.

Coverage: supervisor fakes block when their scripted reads are exhausted;
there is no explicit idle `kOk/0` preservation case. Direction: distinguish
no progress from terminal status, with bounded waiting and a regression proving
unchanged incarnation/parser/runtime across idle reads.

Current disposition: IMPLEMENTED. A dedicated real-PTY timeout regression
checks the same incarnation, no reconnect, parser continuity across a split
GGA sentence, and retained runtime observation/timestamp through later idle
reads. A separate peer-hangup case confirms genuine disconnect still
reconnects. The existing data-only `StepOnce()` contract is unchanged.

### F08 — MEDIUM — Docker contract assertions can be masked by later success

Scope: DEPLOYMENT / CI. Evidence: deterministic shell-control-flow inspection;
no Docker fault-injection run.
`.github/workflows/docker.yml`, `Verify image contract`, inner `bash -lc`
(line 51), lacks `errexit`/`pipefail`. An early executable/dependency/interface
assertion may fail, then a successful final `test ! -e .../install/log` makes
the container command return 0. The outer shell does not enforce errors inside
this child shell. Existing assertions are present but not all are fail-closed.
Direction: enable error propagation in the inner script and verify a deliberately
failed early assertion causes a failing job.

## Established safe behavior and rejected false positives

- Transaction engine timeout/partial-write quarantine, late-response rejection,
  command-B/refusal and Reset refusal are implemented. F01 concerns callers.
- MODEL is not recovery: u-blox metadata comes from the same validated MON-VER
  frame; without MOD, transport verification does not imply model verification.
  Multiple-frame, malformed/checksum and fragmentation cases are covered by
  `TestActiveUbloxMonVerVerification`; Unicore MODEL validates VERSIONA.
- ACK-only traffic cannot establish u-blox active baud verification. Absence
  of that verification is separately exposed, not treated as a recovery fence.
- The ROS2 snapshot test fix freezes input and retains exact timestamp and
  no-write assertions. BuildSnapshot was not changed. The supervisor test fix
  synchronizes fake availability/control and preserves its assertions.
- At the audited baseline, `TestNtripForwardingAndIndependentReconnects`
  replaced the receiver after the first frame was fully flushed. The current
  POSIX test additionally cancels a blocked RTCM flush with a pending suffix
  and proves the writer abandons it.
- Formatting commit `a6c1746`: all 196 changed C/C++ files were reproduced
  byte-for-byte by clang-format-21 applied to their parent versions using the
  current root configuration. No additional change was found in that pass.

## Audited-baseline validation / current validation

PASS during the audit, using existing binaries:

- `build/gnss_driver/gnss_driver_test_receiver_command_transaction_engine`
- `build/gnss_driver/gnss_driver_test_receiver_config_application`
- `build/gnss_driver/gnss_driver_test_receiver_discovery`
- `build/gnss_tools/gnss_tools_test_config_apply`
- `bash scripts/clang_format_21.sh --check --all` (script-selected roots only).
- `git diff --check`; clean final audit worktree.

Defective behavior reproduced at the audited baseline: F02, F03, F06, as
described above. That read-only audit did not run a fresh build, full CTest,
new ROS2 stress run, Kilted/Lyrical CI, Docker image validation, TSan, or
physical hardware tests. Current F03/F04/F07 and F05 build, CTest, PTY,
loopback TLS, and TSan results are recorded in the remediation sections above.
ROS2 CI and physical hardware validation were not repeated in this software
pass.

Physical boundaries remain in
`../blocked/UG-DRIVER-RESPONSE-FENCE-001_CHECKPOINT.md` and
`../blocked/UG-DRIVER-TRANSPORT-INCARNATION-001_CHECKPOINT.md`:
no automatic old-byte cutoff or release of quarantine is claimed. MODEL,
flush, sleep, parser reset, close/reopen and local epochs are not substitutes.
Persistence/power-cycle, USB and per-model reset qualifications remain separate.

## Exact next step / do not touch

F03/F04/F07 are implemented at `b17f0cd`; F05 is implemented at `46acb9a`;
F06 is implemented in the current uncommitted worktree. F08 remains separate
MEDIUM work. Do not begin a
fresh repository-wide audit. Reuse the audited baseline and current test
evidence when resuming another finding.

Invalidate only evidence whose source/tests/contracts/build environment changed.
Preserve hardware boundaries and exact provenance assertions; no speculative
recovery fence, unrelated feature work, automatic staging, commit, or push.
The local F03, F05, and F06 working notes remain `LOCAL_ONLY`. This shared record
remains `ACTIVE` because the wider pre-release audit has open findings.
