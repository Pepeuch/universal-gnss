# Agent checkpoint

Repository: `/workspaces/universal-gnss`
Branch: `main`
HEAD: `84d4b34b760d150b88f5e41ce3e831a30c4f47bf`

## Objective

Preserve the reusable planning boundary discovered by the BlueOS compatibility
study without making BlueOS a second GNSS implementation.

## Authoritative planning state

`TODO.md` section **Native runtime, API, web, and deployment planning** is the
source of truth for `UG-PLAN-001` through `UG-PLAN-006`. `ROADMAP.md` carries
the release/dependency view. This checkpoint is only a resumption aid.

## Established evidence

- CURRENT: `blueos/README.md` defines the adapter boundary and its physical
  device-grant/hotplug risk; reference it rather than copying its design.
- IMPLEMENTED/PARTIAL: `84d4b34` adds generic, non-ROS/non-BlueOS supervisor
  Phase 1 with one explicit serial device, session/runner lifecycle, bounded
  reconnect, incarnation clearing, snapshots, CLI, and fake-transport tests.
- COMPLETED RESEARCH/PARTIAL: BlueOS compatibility/packaging research, Bazaar
  metadata template, and minimal permission template exist. No Docker image,
  API, GUI, NTRIP supervisor orchestration, or BlueOS runtime is implemented.

## Dependency decisions

1. Finish current Universal GNSS/MowgliNext downstream validation.
2. Complete supervisor Phase 1 physical/configuration delta (`UG-PLAN-001`).
3. Compose supervisor NTRIP/RTCM (`UG-PLAN-002`).
4. Build generic API then GUI (`UG-PLAN-003`, `UG-PLAN-004`).
5. Package standalone Docker (`UG-PLAN-005`).
6. Reuse these layers for BlueOS and perform physical device-grant validation
   before publication (`UG-PLAN-006`).

## Hardware boundary

BlueOS USB hotplug/re-enumeration and Docker device grants are
HARDWARE_REQUIRED. A reopened tty is not proof that the container has a valid
grant for a newly enumerated physical receiver.

## Do not redo

- Do not add ROS2/BlueOS dependencies or duplicate GNSS/parser/runtime/NTRIP
  semantics in the supervisor, API, GUI, Docker, or BlueOS layers.
- Do not create a Dockerfile until the supervisor is production-capable.
- Do not claim API, GUI, Docker, or BlueOS support is implemented.

## Exact next step

Resume the first authorized generic runtime/deployment item from `TODO.md`;
do not start BlueOS implementation ahead of its generic dependencies.

## UG-PLAN-002 resumption state

Baseline verified 2026-09-04: `main` at `ab32f673da6e9e6ffa8eac9a57b08656f8843645`, clean before this work.

- IMPLEMENTED: `gnss_ros2::ReceiverNode` now owns
  `universal_gnss_transport::RtcmFrameWriter`, with its subscription QoS using
  the writer's 50-frame default capacity. The duplicate `PendingRtcmWrite`
  deque, capacity constant, and flush-result enum are removed.
- PRESERVED: `ReceiverNode` remains the owner of ROS2-specific diagnostics:
  successful-byte/frame counters, last-forward time/type, error counter,
  failure text, terminal transport close, and abandonment at transport terminal
  state. The writer remains the sole owner of partial-head ordering, bounded
  drop-newest FIFO, zero-progress blocking, and incomplete-frame abandonment.
- PASS: `TestRtcmFrameWriterUga009` in `gnss_transport_test_foundation` covers
  the transport-neutral writer contract (capacity 50, drop-newest, partial and
  would-block ordering, hard failure, and explicit incarnation abandonment).
- PASS: `colcon --log-base /tmp/ug-plan-002-ros2-log build --base-paths .
  --packages-select universal_gnss --build-base /tmp/ug-plan-002-ros2-build
  --install-base /tmp/ug-plan-002-ros2-install --cmake-args
  -DUNIVERSAL_GNSS_BUILD_ROS2=ON --event-handlers console_direct+` completed.
- PASS: outside the sandbox, where Fast DDS may access network interfaces,
  `ROS_LOG_DIR=/tmp/ug-plan-002-ros2-log/ros ctest --test-dir
  /tmp/ug-plan-002-ros2-build/universal_gnss --output-on-failure -R
  '^test_receiver_node$'` passed (25.82 s), including the seven existing UGA009
  queue/lifecycle regressions. Sandboxed direct execution is not evidence: it
  denies Fast DDS network-interface setup and initially lacked the CTest
  `LD_LIBRARY_PATH` configuration.
- PASS: `bash scripts/clang_format_21.sh --check
  gnss_ros2/src/receiver_node.cpp` and `git diff --check`.

Do not redo the ROS2 migration or alter `RtcmFrameWriter` without invalidating
the transport-neutral and ReceiverNode regression evidence above. Do not touch
`gnss_runtime` NTRIP integration, API, WebUI, Docker, or BlueOS runtime here.

Exact next action: begin the separately authorized native-supervisor NTRIP/RTCM
composition only when requested; use the shared writer contract rather than
reintroducing a consumer-local queue.

## Native supervisor NTRIP/RTCM state — PARTIAL

- PARTIAL: `ReceiverSupervisor` now has an independent NTRIP worker owning an
  existing `NtripClient`; production configuration uses `NtripClient::Connect`.
  It forwards only the complete CRC-valid frames surfaced by the client through
  the shared `RtcmFrameWriter`.
- PARTIAL: a receiver-incarnation `ReceiverLink` owns the writer. Active-link
  replacement and every correction flush share `correction_mutex_`; replacement
  abandons the old writer before its transport is released. NTRIP reconnect and
  receiver reconnect paths are separate. `MaybeInjectGga` is called only when
  the authoritative current receiver runtime state exists.
- PARTIAL: supervisor snapshot exposes NTRIP client state/metrics/flow/reconnect
  state, writer queue/overflow/forwarding counters, and only the bounded
  `NtripClientError` category. CLI accepts NTRIP configuration and never prints
  its credentials.
- PASS: native CMake build of `universal_gnss_runtime` and
  `universal_gnss_supervisor`; existing `gnss_runtime_test_receiver_supervisor`;
  `git diff --check`.
- CURRENT_STATE (2026-09-04): the newly added deterministic socket-pair
  exercise remains deliberately unregistered. The reported final
  `configuration` category was traced; it is not the first adopted-socket
  failure and is not evidence of an invalid supervisor `NtripConfig`.
- PROVEN_EVIDENCE: with temporary, credential-free diagnostics, the first
  socket-factory call returned fd `3`. `NtripClient` was `kDisconnected` (0)
  immediately before `AdoptConnectedSocket(3)` and returned `kNone` with state
  `kConnected` (2) immediately afterward. Its first operation was
  `SendRequest`, whose first `TcpClientTransport::Write` called `send(3, ...)`
  and failed with `errno=EPERM` in the sandbox. `SendRequest` therefore
  returned `NtripClientError::kDisconnected` (6), before `request_sent` or
  `response_received` was set; this is `WriteResult{kError, kWriteFailure}`
  mapped by `MapTransportError`, not an Ntrip configuration validation path.
- PROVEN_EVIDENCE: normal `NtripClient::FailWith(kDisconnected)` closed fd `3`,
  moved the client to `kFailed` (4), and scheduled its configured 20 ms
  reconnect. When that deadline elapsed, the supervisor correctly requested a
  second socket; the factory returned fd `5`, for which the same sandbox
  `send(...)=EPERM` sequence occurred. Thus the supervisor did not consume or
  retire fd `3` before the fixture exchange: NtripClient retired it after the
  sandbox denied the first request write.
- PROVEN_EVIDENCE: when the unbounded fixture factory was allowed to exhaust,
  its later `-1` result reached `TcpClientTransport::AdoptConnectedSocket`'s
  explicit `fd < 0` predicate, returning `kInvalidArgument`; `NtripClient`
  maps that to `NtripClientError::kConfiguration`. That later category replaced
  the earlier disconnected error in the last-error snapshot. Do not diagnose
  `configuration` without first distinguishing this exhausted test descriptor
  from the earlier request-write failure.
- PROVEN_EVIDENCE: outside the sandbox, the same diagnostic fixture produced
  fd `3`, pre-adopt state 0, successful adoption/state 2, and successful
  request (`request=0`, connected). It reached the next lifecycle assertion,
  which currently fails: `replaced receiver transport must receive no old RTCM
  suffix`. This is the next real deterministic forwarding/lifecycle delta; it
  is separate from Ntrip configuration/adoption and must be investigated
  without changing NtripClient public semantics or retry policy.
- VALIDATION: sandbox `cmake --build /tmp/universal-gnss-build --target
  gnss_runtime_test_receiver_supervisor -j2 &&
  /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor`
  yielded the fd-3/fd-5 transition above. Sandboxed
  `gnss_ntrip_test_ntrip_client` independently exhibited `send(fd=3):
  errno=EPERM`, proving the denial is not supervisor-specific. The equivalent
  supervisor fixture was rerun outside the sandbox; it passed adopt/request and
  exposed the separate old-sink assertion. After removing diagnostics, the
  existing unregistered supervisor regression rebuilt and passed outside the
  sandbox; `git diff --check` passed.
- DIAGNOSTIC_HYGIENE: temporary traces were added only to
  `gnss_runtime/src/receiver_supervisor.cpp`,
  `gnss_transport/src/tcp_client_transport.cpp`, and
  `gnss_runtime/tests/test_receiver_supervisor.cpp`; all have been removed.
  No production logging, public API, NtripClient rule, or retry/reconnect
  behaviour was added or changed for this investigation.
- CURRENT_STATE (2026-09-04, follow-up): the next outside-sandbox assertion
  was a deterministic-test defect, not a supervisor lifecycle defect. The test
  retained a raw pointer to the first `FakeTransport` and dereferenced it after
  receiver incarnation replacement had destroyed that `unique_ptr`-owned fake.
  The apparent old-sink byte count of zero was therefore undefined behaviour.
  `FakeTransportWriteRecord`, owned by the test, now retains only a locked copy
  of emitted bytes; the test no longer dereferences a destroyed transport.
- PROVEN_EVIDENCE: the registered
  `TestNtripForwardingAndIndependentReconnects` now passes outside the sandbox.
  It proves initial RTCM forwarding through `RtcmFrameWriter`, partial/zero
  progress completion ordering, abandonment/isolation of the replaced receiver
  sink, forwarding to the new receiver incarnation, and NTRIP reconnect without
  restarting the healthy receiver. This test-only repair did not alter runtime,
  writer, NtripClient, or reconnect code.
- VALIDATION (follow-up): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/tests/test_receiver_supervisor.cpp && cmake --build
  /tmp/universal-gnss-build --target gnss_runtime_test_receiver_supervisor -j2
  && /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor`
  passed outside the sandbox. The NTRIP fixture is now registered in that test
  executable.
- REMAINING_DELTA: add explicit deterministic coverage for GGA cadence and
  unavailable/invalid runtime position, stop cancellation of both reconnect
  paths, credentials absent from status/errors, and the remaining requested
  queue/lifecycle/status assertions. Then run focused runtime/NTRIP/transport/
  driver suites, native build, shared ROS2 regression if needed, formatter, and
  `git diff --check`. Hardware/caster reconnect/hotplug remains
  HARDWARE_REQUIRED.
- CURRENT_STATE (2026-09-04, GGA provenance): `RunNtrip` now records the
  receiver session incarnation and last consumed `position_observations` count.
  It calls `NtripClient::MaybeInjectGga` only for a strictly newer authoritative
  receiver position observation in the current incarnation. On incarnation
  replacement the consumed count resets. NtripClient remains the sole owner of
  enabled/fix/coordinate/cadence policy and GGA formatting; this prevents a
  cached snapshot from being treated as a new position observation.
- VALIDATION (GGA implementation): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/src/receiver_supervisor.cpp && cmake --build
  /tmp/universal-gnss-build --target universal_gnss_runtime -j2` passed outside
  the sandbox. Focused deterministic GGA tests remain the exact next step.
- CURRENT_STATE (2026-09-04, GGA tests): the fresh-observation gate initially
  exposed a real ordering issue: a position could be consumed while NTRIP was
  `kConnected`, before its response was accepted. `RunNtrip` now consumes the
  count and calls `MaybeInjectGga` only once the client is `kStreaming`. This
  preserves the same fresh authoritative observation through the connection
  handshake without treating cached state as a new observation.
- PROVEN_EVIDENCE (GGA tests): `TestGgaUsesFreshAuthoritativePosition` uses an
  adopted socketpair and an actual NMEA receiver observation. A valid fix emits
  exactly one GGA over 50 ms of repeated supervisor polls; an invalid-fix GGA
  emits none. The test observes the caster bytes directly, so the request and
  injected GGA cannot be conflated. NtripClient still owns cadence, coordinate,
  and fix-policy decisions.
- VALIDATION (GGA tests): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/src/receiver_supervisor.cpp gnss_runtime/tests/test_receiver_supervisor.cpp
  && cmake --build /tmp/universal-gnss-build --target
  gnss_runtime_test_receiver_supervisor -j2 &&
  /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor`
  passed outside the sandbox.
- CURRENT_STATE (2026-09-04, lifecycle/status): snapshot now projects the
  existing `NtripClient::BuildCorrectionHealth({})` result alongside, rather
  than in place of, NTRIP connection metrics and correction-flow state. Thus
  connection, accepted response, complete-valid frame flow, semantic health,
  reconnect state, forwarding activity, queue state, and receiver incarnation
  remain independently observable.
- PROVEN_EVIDENCE (lifecycle/status): `TestNtripStopAndRedaction` proves a
  caster disconnect schedules the NtripClient reconnect, then `Stop()` cancels
  it before a second socket-factory call; its pre-existing receiver-only
  backoff test proves receiver reconnect cancellation. The test also supplies
  username/password values and proves only the bounded `configuration` category
  appears in snapshot errors. `TestNtripForwardingAndIndependentReconnects`
  now asserts enabled/connected, response accepted, one valid complete frame,
  forwarding active, empty writer queue after flush, forwarding count, and
  receiver incarnation independently.
- VALIDATION (lifecycle/status): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/include/universal_gnss_runtime/receiver_supervisor.hpp
  gnss_runtime/src/receiver_supervisor.cpp gnss_runtime/tests/test_receiver_supervisor.cpp
  && cmake --build /tmp/universal-gnss-build --target
  gnss_runtime_test_receiver_supervisor -j2 &&
  /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor
  && git diff --check` passed outside the sandbox.
- DO_NOT_REDO: do not repeat NtripConfig field-by-field analysis, adoption
  ordering analysis, or attribute the final `configuration` snapshot to fd 3.
  The exact first-fd sequence and source predicates above remain valid unless
  `RunNtrip`, `NtripClient::AdoptConnectedSocket`/`SendRequest`, or
  `TcpClientTransport::Write` changes. Do not add retries around configuration
  errors, special-case the fixture, relax NtripClient validation, or bypass
  adopted sockets.

CURRENT_STATE (2026-09-04, deterministic closure): UG-PLAN-002 native
supervisor NTRIP/RTCM orchestration is IMPLEMENTED for deterministic software
evidence. It is coherent enough to form the requested single commit, but no
commit or push was made.

VALIDATION (closure):

- PASS: full native `cmake --build /tmp/universal-gnss-build -j2`.
- PASS: outside the sandbox, `ctest --test-dir /tmp/universal-gnss-build
  --output-on-failure -R '^(gnss_runtime|gnss_ntrip|gnss_transport|gnss_driver)_test_'`:
  33/33 runtime, NTRIP, transport, and driver tests.
- PASS: ROS2-enabled `colcon --log-base /tmp/ug-plan-002-ros2-log build
  --base-paths . --packages-select universal_gnss --build-base
  /tmp/ug-plan-002-ros2-build --install-base /tmp/ug-plan-002-ros2-install
  --cmake-args -DUNIVERSAL_GNSS_BUILD_ROS2=ON --event-handlers
  console_direct+`.
- PASS: outside the sandbox, `ROS_LOG_DIR=/tmp/ug-plan-002-ros2-log/ros ctest
  --test-dir /tmp/ug-plan-002-ros2-build/universal_gnss --output-on-failure -R
  '^test_receiver_node$'`: 1/1 (25.86 s), retaining all ReceiverNode UGA009
  regressions.
- PASS: `clang-format-21` check over all touched runtime, ROS2, and transport
  C++ files; `git diff --check`.

REMAINING_DELTA: physical receiver/caster operation, physical receiver
re-enumeration/hotplug, and actual-network reconnect remain HARDWARE_REQUIRED.
They are the only remaining evidence; do not represent them as deterministically
proven. Docker/API/WebUI/BlueOS are explicitly out of scope and unstarted.

DO_NOT_REDO: all adopted-socket, sandbox EPERM, NtripConfig, old-sink, UGA009,
ROS2 migration, GGA fresh-observation, stop-cancellation, and credential
redaction analysis above is established until its cited runtime/Ntrip/transport
contracts change.

Exact next action: if authorized, review the cohesive UG-PLAN-002 diff and make
one commit. Otherwise retain this checkpoint and begin only separately
authorized physical receiver/caster/hotplug validation.
