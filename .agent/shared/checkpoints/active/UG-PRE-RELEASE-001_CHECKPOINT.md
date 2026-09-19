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
subsequent user request authorizes this checkpoint and its index entry only.
It does not authorize implementing the proposed corrections.

Audit recommendation: do not merge/tag the audited HEAD until the five HIGH
software findings below are resolved and regression-tested. The three MEDIUM
findings were reported as non-blocking. This is a software assessment, not a
claim that every release qualification gate is satisfied.

Finding references `F01` through `F08` are stable within this audit only, not
new UGA IDs or replacements for canonical backlog items. All remain unresolved.
`TODO.md` and `docs/status/uga_backlog.json` remain authoritative; this checkpoint
does not change their classifications, conservation accounting, or progress.

## Evidence interpretation and dependencies

- CURRENT: the exact source paths/symbols below at the audited HEAD.
- Reproductions used existing `build/` executables, not a fresh build during
  the audit. They corroborate the source traces but are not clean-build or CI
  attestations. No reproduction scripts or large logs were saved.
- PTY reproductions used newly allocated local pseudo-terminals, never a
  physical receiver. Their compact recipes/results are preserved below.
- Static findings are explicitly distinguished from executed reproductions;
  no ThreadSanitizer, stalled-TLS reproduction, or injected partial-write
  end-to-end regression was run.
- The blocked response-fence and transport-incarnation checkpoints remain
  valid for the physical cutoff boundary. Their broad implementation summaries
  must not be read as proof of end-to-end apply safety: F01/F02 and F06 qualify
  those claims at this HEAD. The engine-level quarantine evidence still holds.
- Canonical `UGA-126` remains `PARTIAL / HARDWARE_REQUIRED`; `UGA-127` remains
  `PARTIAL`. `UGA-142` is currently `IMPLEMENTED` in the manifest; F05 is a
  concrete follow-up within its TLS scope, not an automatic reclassification.

## Release-blocking findings

### F01 — HIGH — Optional partial write can escape quarantine through apply phases

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

Smallest correction direction: make engine indeterminacy terminal regardless of
optional/continue-on-error policy; carry that state through all phase outcomes
and prohibit recovery/probes/new command phases after this failure.
Acceptance: partial write -> hard stop; no reopen, MODEL probe, later command,
or persistence operation; result explicitly indeterminate.

### F02 — HIGH — Already-received responses can acknowledge later commands

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

Smallest correction direction: associate captured responses with eligible
operations and reject records captured before the current dispatch. Update
fixtures to emit replies in reaction to writes. This only excludes already
captured stale records; it does not prove freshness of bytes still in hardware.

### F03 — HIGH — Native supervisor stop hangs on a silent serial receiver

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

### F04 — HIGH — Concurrent POSIX descriptor lifecycle is unsynchronized

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

### F05 — HIGH — TLS handshake is not bounded by configured timeouts

Scope: NTRIP / transport. Related backlog: `UGA-142`.
Evidence: static call-path trace; deterministic with a silent TLS peer;
runtime reproduction still pending.

Locations: `gnss_transport/src/tcp_client_transport.cpp`, `Open`, `StartTls`
(line 336); `gnss_ntrip/src/ntrip_client.cpp`, `Connect`;
`gnss_runtime/src/receiver_supervisor.cpp`, `RunNtrip`, `Stop`.

Trigger: peer accepts TCP, leaves the connection open, and never completes TLS.
StartTls uses blocking SSL_connect without an enclosing deadline. TCP connect
timeout does not bound that call. A TLS-enabled supervisor can then wait for
its NTRIP thread indefinitely during Stop.

Coverage: `TestTlsConfigurationAndHandshakeFailure` covers closed peer and
configuration rejection; `TestVerifiedTlsLoopback` covers prompt handshake and
certificate verification. Neither exercises an open, silent handshake peer.

Smallest correction direction: enforce a TLS handshake deadline and cancellation
while retaining synchronous public semantics if desired. Add a controlled
stalled-handshake test including supervisor shutdown. No TLS SIGPIPE defect was
established/reported by this audit; do not infer one from this finding.

## High-confidence non-blocking findings

### F06 — MEDIUM — Indeterminate state is lost in some apply results

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

Coverage: the existing flag assertion covers the direct optional SIGNALGROUP
timeout only. Direction: propagate indeterminacy as a fact through every phase
and failure exit, including partial writes and read failure after dispatch;
test each wrapper rather than infer the flag from one terminal status.

### F07 — MEDIUM — Idle read is mistaken for receiver disconnection

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
- `TestNtripForwardingAndIndependentReconnects` replaces the receiver after
  the first frame is fully flushed: its old-suffix assertion does not prove
  abandonment during a pending partial write. This is a coverage limit, not
  evidence that production queue abandonment is broken.
- Formatting commit `a6c1746`: all 196 changed C/C++ files were reproduced
  byte-for-byte by clang-format-21 applied to their parent versions using the
  current root configuration. No additional change was found in that pass.

## Validation already performed / remaining

PASS during the audit, using existing binaries:

- `build/gnss_driver/gnss_driver_test_receiver_command_transaction_engine`
- `build/gnss_driver/gnss_driver_test_receiver_config_application`
- `build/gnss_driver/gnss_driver_test_receiver_discovery`
- `build/gnss_tools/gnss_tools_test_config_apply`
- `bash scripts/clang_format_21.sh --check --all` (script-selected roots only).
- `git diff --check`; clean final audit worktree.

Defective behavior reproduced: F02, F03, F06, as described above.
Not run during this audit: fresh build, full CTest, new ROS2 stress run,
Kilted/Lyrical CI, Docker image validation, TSan, physical hardware tests.
Passing existing tests does not close the findings or prove the uncovered paths.

Physical boundaries remain in
`../blocked/UG-DRIVER-RESPONSE-FENCE-001_CHECKPOINT.md` and
`../blocked/UG-DRIVER-TRANSPORT-INCARNATION-001_CHECKPOINT.md`:
no automatic old-byte cutoff or release of quarantine is claimed. MODEL,
flush, sleep, parser reset, close/reopen and local epochs are not substitutes.
Persistence/power-cycle, USB and per-model reset qualifications remain separate.

## Exact next step / do not touch

On an authorized remediation turn, verify HEAD/diff, reuse this evidence, and
start with F01: add an optional-SIGNALGROUP partial-write regression at the
application/workflow boundary, then enforce terminal quarantine propagation.
Do not begin a fresh repository-wide audit. F02 is the next independent safety
regression; coordinate F03/F04/F07 as one transport-lifecycle correction.

After fixes, rerun focused regressions, affected suites, clean build/full CTest,
ROS2 Kilted/Lyrical and applicable Docker checks, formatting and diff checks.
Reconcile canonical backlog/release dependency classifications and existing
checkpoint summaries when implementation/status changes are authorized and
proven; do not count these open findings or HARDWARE_REQUIRED items as complete.

Invalidate only evidence whose source/tests/contracts/build environment changed.
Preserve hardware boundaries and exact provenance assertions; no speculative
recovery fence, unrelated feature work, automatic staging, commit, or push.
No local scratch checkpoint is promoted or staged. This shared ACTIVE record
and its index entry are the only changes authorized by the checkpoint request.
