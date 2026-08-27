# Universal GNSS repository-wide adversarial audit

Audit date: 2026-08-27 UTC

Audited branch: `review`

Audited commit: `804bed8d7f753f6834212cfbc9dc329f88360299`

Scope: first-pass audit only; no production-code fixes

## Executive summary

The audited tree builds and its normal tests pass, but it is not ready for unsupervised field deployment. The audit found **29 concrete defects: 0 P0, 14 P1, 14 P2, and 1 P3**. The most consequential classes are: undefined behavior in every normally framed NMEA sentence; loss and invention of data timestamps; receiver input throughput coupled to ROS publication cadence; NTRIP connections that remain `Streaming` forever after correction flow stops; correction health accepting semantically malformed RTCM; unsafe partial RTCM writes; and multiple Unicore plans that are documented as production-ready but emit invalid, reboot-interrupted, unknown-model, or zero-period commands.

The normal suite did not reveal these defects. ASan did reveal the NMEA lifetime bug in 18 tests, and cppcheck independently identified the same dangling temporary. Focused `/tmp` reproducers independently confirmed the parser, NTRIP, RTCM, transport, auto-detection, aggregation, configuration, and long-run failures described below.

No P0 was assigned. None of the confirmed failures demonstrated immediate arbitrary code execution, unrecoverable data destruction, or a guaranteed safety event without additional deployment conditions. Several P1 findings can nevertheless terminate a process, silently corrupt correction delivery, misreport correction availability, or configure a receiver incorrectly.

## Baseline and preservation

- Initial tracked worktree: clean; `git status --short --branch` reported `## review...origin/review`.
- Local `HEAD`, `origin/review`, and the locally recorded `origin/main` were all `804bed8d7f753f6834212cfbc9dc329f88360299` at audit start.
- A read-only temporary fetch found the public upstream `main` at `aaacc6be92463ad493d6f4260426cf645188078f`, 14 commits ahead of the audited commit. This report assesses the requested local tree, not that later tree.
- Existing user work was preserved. No reset, clean, checkout, production edit, commit, or push was performed.
- All custom repro source was supplied directly to the compiler on stdin; binaries and build trees are under `/tmp/universal-gnss-audit-*` and are untracked.
- The only tracked modification produced by this audit is this report.

## Repository inventory and audit method

The audit covered `gnss_core`, `gnss_protocols`, `gnss_driver`, `gnss_transport`, `gnss_ntrip`, `gnss_tools`, `gnss_ros2`, examples, CMake, tests, public documentation, and local vendor manuals. Cross-layer passes followed these paths:

1. serial/TCP source -> session runner -> protocol framer/parser -> runtime aggregator;
2. runtime state -> ROS status/fix/diagnostics -> NTRIP GGA input;
3. NTRIP TCP/HTTP -> RTCM framer/parser/monitor -> ROS RTCM -> receiver forwarding;
4. receiver model/profile -> portable plan -> command builder -> live apply/reconnect;
5. discovery bytes -> scoring/family selection -> parser/configuration selection.

The audit did not treat tests, comments, fork patches, or local prose as authoritative. Findings were checked against code paths, focused reproducers, protocol reasoning, local vendor documentation, and relevant PR history. Each P1/P2 item was challenged for benign interpretations before inclusion.

## Build, test, sanitizer, and static-analysis matrix

| Validation | Configuration / command family | Result |
|---|---|---|
| GCC Debug | CMake build in `/tmp/universal-gnss-audit-gcc-debug` | Build passed; 61/61 CTest tests passed |
| GCC Release | CMake build in `/tmp/universal-gnss-audit-gcc-release` | Build passed; 61/61 CTest tests passed |
| GCC ASan+UBSan | `/tmp/universal-gnss-audit-gcc-san` | Build passed; initial LeakSanitizer runs were blocked by the environment's ptrace policy; with `ASAN_OPTIONS=detect_leaks=0`, 43/61 passed and 18/61 aborted on the same NMEA stack-use-after-scope |
| GCC UBSan only | `/tmp/universal-gnss-audit-gcc-ubsan` | Build passed; 61/61 passed; no UBSan runtime report |
| ROS2 Kilted Debug | colcon build in `/tmp/universal-gnss-audit-ros2-build` | Build passed; with `ROS_LOG_DIR=/tmp/universal-gnss-audit-ros2-runtime-logs`, 70 tests ran with 0 failures and 0 skips |
| ROS2 initial test run | default `/root/.ros/log` | Failed only because the audit environment could not write that directory; clean rerun above isolates this as an environment issue |
| cppcheck | 128 translation units using compile commands | Completed; independently reported the NMEA dangling temporary; remaining output was performance advice, not promoted to defects |
| Clang / clang-tidy / scan-build | availability check | Not installed in the audit environment; no result claimed |
| Valgrind | availability check | Not installed in the audit environment; no result claimed |
| Focused reproducers | `/tmp/universal-gnss-audit-*-repro` | Confirmed findings UGA-001, 005-009, 014, 018, 020-029 as referenced below |
| Long-run RTCM monitor | 2,000,000 observations | `VmRSS` about 66.3 MB; one rate query scanned full history in about 1.18 ms; storage continued to grow |

Compiler/tool versions observed: GCC/G++ 13.3.0, CMake 3.28.3, ROS2 Kilted, colcon, and cppcheck. Clang-based parity could not be established.

## Finding count

| Severity | Count |
|---|---:|
| P0 Critical | 0 |
| P1 High | 14 |
| P2 Medium | 14 |
| P3 Low | 1 |
| **Total** | **29** |

## Ten highest-value findings

1. UGA-001: the NMEA framer dereferences a `string_view` into a destroyed temporary; ASan aborts ordinary valid NMEA tests.
2. UGA-002: live receiver bytes have no acquisition timestamp, and ROS publication invents `now`, making delayed/backlogged data look fresh.
3. UGA-003: one receiver read per publish timer caps drain rate at `read_chunk_size * publish_rate_hz` and couples latency to an unrelated output rate.
4. UGA-005: NTRIP can remain `Streaming` indefinitely with no first RTCM frame or after correction flow becomes silent; no forced reconnect exists.
5. UGA-007: repeated republishing of stale `GnssStatus` refreshes NTRIP's GGA source timer, so obsolete coordinates can continue to be injected.
6. UGA-008: CRC-valid but structurally malformed RTCM 1005/MSM/1230 can satisfy all correction requirements and set `correction_available=true`.
7. UGA-009: a short nonblocking RTCM write can put an unrecoverable frame prefix on the receiver link and discard the remainder.
8. UGA-010/011: Unicore output syntax conflicts with the vendor manual, and `SIGNALGROUP` can reboot before subsequent output and save commands execute.
9. UGA-012: an explicitly unknown Unicore model receives a full guessed profile marked production-ready.
10. UGA-014: monotonic timestamps are encoded as ROS `Time` and later compared with either monotonic or ROS/system time, invalidating RTCM freshness semantics.

## P0 Critical findings

None.

## P1 High findings

### UGA-001 — NMEA header parsing uses a dangling `string_view`

- **ID:** UGA-001
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** NMEA framer / all NMEA-consuming sessions and tools
- **File(s):** `gnss_protocols/src/nmea_framer.cpp`
- **Function(s):** `NmeaSentenceFramer::BuildSentence`
- **Relevant line(s):** 96-105, especially 99-100
- **Category:** C++ lifetime / undefined behavior

**Observed behaviour:** A valid ordinary GGA sentence causes ASan to report `stack-use-after-scope` while assigning the talker. Eighteen sanitizer tests abort on this same root cause.

**Expected behaviour:** Framing a valid NMEA sentence must only read storage whose lifetime covers the read.

**Root cause:** `sentence.payload_text.substr(...)` returns a temporary `std::string`; it is implicitly converted to `std::string_view` at line 99, and the temporary is destroyed before `header` is read at lines 102-105.

**Why this is a real bug:** This is direct standard C++ lifetime undefined behavior, not an analyzer heuristic. Normal non-sanitized tests pass only because small-string storage remains readable by accident.

**User/runtime impact:** Any NMEA path can misidentify sentences, corrupt parsed records, or crash depending on compiler, optimization, and stack reuse. Generic NMEA and vendor streams containing NMEA are affected.

**Reproduction:** `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 /tmp/universal-gnss-audit-gcc-san/gnss_protocols/gnss_protocols_test_nmea_parser`.

**Evidence:** ASan points to `BuildSentence()` line 104 and storage from a destroyed stack temporary; cppcheck independently reports the dangling temporary. Debug/Release success disproves only deterministic failure, not UB.

**Minimal proposed fix:** Form the view from `sentence.payload_text.data()/size()` or retain an owning substring through all uses.

**Regression test that should be added:** Keep the existing valid NMEA tests in an ASan CI job; add long and short headers to cover both SSO and heap-backed strings.

**Compatibility/public-behaviour impact:** None intended; removes UB.

**ROS2 impact:** `ReceiverNode` NMEA mode and auto-detected NMEA/vendor sessions are affected.

**MowgliNext downstream impact, if any:** Robot GNSS ingestion may crash or produce missing fixes; no downstream logic change is required after the core fix, but sanitizer/field validation is required.

**Related existing PR:** None found.

**Possible false-positive considerations:** ASan custom-stack false positives were considered; the source-level temporary lifetime independently proves the defect.

### UGA-002 — live acquisition timestamps are lost and publish time is substituted

- **ID:** UGA-002
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** session runner -> parser -> runtime state -> ROS publisher
- **File(s):** `gnss_driver/src/receiver_session_runner.cpp`; `gnss_ros2/src/receiver_node.cpp`
- **Function(s):** `ReceiverSessionRunner::StepOnce`; `ReceiverNode::Impl::PublishNow`
- **Relevant line(s):** runner 27-46; receiver node 1418-1436
- **Category:** timestamp provenance / stale-data safety

**Observed behaviour:** Live bytes are passed to `FeedBytes` without a timestamp. When the resulting state has no timestamp, every publication copies the state and assigns `owner_.now()`.

**Expected behaviour:** The state should carry a defined receipt/acquisition timestamp captured when bytes are read, and publication must not relabel old data as newly measured.

**Root cause:** `StepOnce` has no clock/timestamp input and calls the untimestamped `FeedBytes` overload; `PublishNow` treats absence as permission to stamp the current ROS time.

**Why this is a real bug:** A serial/TCP backlog is explicitly possible (UGA-003). Parsing delayed bytes later and stamping them at publication makes measurement/receipt age unobservable and contradicts freshness diagnostics.

**User/runtime impact:** Delayed positions can look current to logs, bag consumers, localization, and NTRIP GGA injection. Latency cannot be reconstructed after publication.

**Reproduction:** Code-path trace: inject an untimestamped `MemoryByteSource` sentence, call `StepOnce`, delay, then call `PublishNow`; the published `GnssStatus.stamp` is publication time rather than byte receipt or message time.

**Evidence:** Direct call graph and the historical throughput backlog in Pepeuch PR #1. No live runner call supplies a timestamp.

**Minimal proposed fix:** Capture a monotonic receipt timestamp at each successful read, propagate it through `FeedBytes`, and separately define ROS publication/header time. If protocol measurement time exists, expose it distinctly.

**Regression test that should be added:** Feed data at T1, publish at T2, and assert the observation timestamp remains T1; add a queued/backlogged multi-frame case.

**Compatibility/public-behaviour impact:** Timestamp semantics become explicit; document the change because consumers may currently assume ROS time.

**ROS2 impact:** Changes `GnssStatus`, `NavSatFix`, and diagnostic stamp interpretation.

**MowgliNext downstream impact, if any:** Localization must consume the documented measurement/receipt time and validate end-to-end latency.

**Related existing PR:** Pepeuch PR #1 exposed the backlog that makes this timestamp defect operationally important, but did not solve provenance.

**Possible false-positive considerations:** Some NMEA messages carry UTC, but the runtime path does not consistently convert that to the transport receipt timestamp and binary/vendor streams have the same issue.

### UGA-003 — receiver drain throughput is coupled to ROS publish cadence

- **ID:** UGA-003
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** ROS2 ReceiverNode / session runner
- **File(s):** `gnss_ros2/src/receiver_node.cpp`; `gnss_driver/src/receiver_session_runner.cpp`
- **Function(s):** `ReceiverNode::Impl::OnTimer`; `ReceiverSessionRunner::StepOnce`
- **Relevant line(s):** receiver node 70-77, 783-794; runner 27-47
- **Category:** streaming throughput / backpressure / hidden latency

**Observed behaviour:** Each publish timer performs exactly one `Read`, then publishes. Maximum requested drain is `read_chunk_size * publish_rate_hz`; at 0.1 Hz with the 64 KiB default it is 6.55 kB/s, below the source comment's measured approximately 13 kB/s F9P output. Short OS reads reduce it further.

**Expected behaviour:** Input draining should follow source readiness and continue until drained or a bounded work budget is reached; output publication rate should only control publication.

**Root cause:** `OnTimer()` calls `StepOnce()` once. The 64 KiB change increases capacity but retains the one-read/publish coupling.

**Why this is a real bug:** When producer rate exceeds drain rate, kernel/driver queues grow while reads, parses, and diagnostics continue succeeding. This recreates the historical failure at other legal rate/chunk combinations.

**User/runtime impact:** Increasing fix latency, stale-but-apparently-fresh timestamps, eventual serial buffer loss, and delayed correction/status handling.

**Reproduction:** Configure `publish_rate_hz=0.1` and default `read_chunk_size=65536`; compare the 6.55 kB/s upper bound with the code's measured 13 kB/s example. The call graph proves only one read per 10 seconds.

**Evidence:** Source comment lines 71-76, independent arithmetic, and the merged Pepeuch PR #1 bug class.

**Minimal proposed fix:** Separate an input-drain loop/thread/executor callback from publication, with bounded per-spin work and explicit backlog/latency diagnostics.

**Regression test that should be added:** A sustained source above one-chunk-per-publish capacity must remain bounded and publish observations within a latency budget; test 0.1/0.5/1/10 Hz and short reads.

**Compatibility/public-behaviour impact:** Scheduling and diagnostics change; public message content should not.

**ROS2 impact:** Direct; launch parameters currently permit unsafe combinations.

**MowgliNext downstream impact, if any:** Validate high-rate receiver output under the robot's actual publish rate and expose backlog/latency to operators.

**Related existing PR:** Pepeuch PR #1, independently confirmed as incomplete for the general invariant.

**Possible false-positive considerations:** A 64 KiB `read` may drain typical bursts at normal rates, but it cannot overcome the mathematical bound at all accepted publish rates and does not guarantee a full-size read.

### UGA-004 — negative ROS `read_chunk_size` becomes a huge allocation

- **ID:** UGA-004
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** ROS2 ReceiverNode parameter handling
- **File(s):** `gnss_ros2/src/receiver_node.cpp`; `gnss_driver/src/receiver_session_runner.cpp`
- **Function(s):** `LoadReceiverNodeConfig`; `ReceiverSessionRunner::StepOnce`
- **Relevant line(s):** receiver node 496-498 and 560-576; runner 12-29
- **Category:** signed/unsigned conversion / process termination

**Observed behaviour:** `read_chunk_size:=-1` is cast directly from `int64_t` to `size_t` and is not validated. On the first timer, `std::vector<uint8_t>(SIZE_MAX, 0)` throws `length_error`/`bad_alloc` from an uncaught callback.

**Expected behaviour:** The node must reject non-positive or unreasonably large chunk sizes during parameter loading.

**Root cause:** Validation exists for discovery sizes and publish rate but not `read_chunk_size`; `NormalizeReadChunkSize` only raises zero to one and cannot repair a wrapped negative.

**Why this is a real bug:** This is a deterministic conversion and allocation path reachable through a public ROS parameter.

**User/runtime impact:** Node startup/timer process termination and potentially severe allocation pressure for other excessively large values.

**Reproduction:** Launch `ReceiverNode` with `--ros-args -p read_chunk_size:=-1` and an open/injected source, then allow the first timer callback.

**Evidence:** Direct type conversion and allocation trace; no parameter guard at lines 560-576.

**Minimal proposed fix:** Validate the signed value before casting and enforce a documented practical upper bound.

**Regression test that should be added:** Reject -1, 0, and above-limit values; accept boundary values without allocating during parameter validation.

**Compatibility/public-behaviour impact:** Invalid configurations that currently crash will be rejected explicitly.

**ROS2 impact:** Direct.

**MowgliNext downstream impact, if any:** Launch/config validation will fail early instead of crashing; update robot launch tests if they set this parameter.

**Related existing PR:** Pepeuch PR #1 introduced/configured this public parameter but did not add signed-range validation.

**Possible false-positive considerations:** `size_t` width is platform-dependent, but every supported conversion of -1 produces a very large positive value; the exact exception may vary.

### UGA-005 — NTRIP `Streaming` has no correction-flow liveness transition

- **ID:** UGA-005
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** NTRIP client and ROS2 NtripNode
- **File(s):** `gnss_ntrip/src/ntrip_client.cpp`; `gnss_ros2/src/ntrip_node.cpp`
- **Function(s):** `NtripClient::Read`, `HandleResponseBytes`; `NtripNode::Impl::ReadOnce`, `BuildHealthSummary`
- **Relevant line(s):** client 553-580 and 864-913; node 440-490, 626-631, 683-705
- **Category:** state machine / liveness / reconnect

**Observed behaviour:** Receipt of a valid HTTP/ICY header immediately sets `Streaming`, even with zero payload. Subsequent zero-byte nonblocking reads return success forever. A stream that later becomes silent behaves the same. Health may become stale, but no code leaves `Streaming`, closes the socket, or schedules reconnect. For header-only streams `first_streaming_time_` is never set because it is assigned only when `bytes_read > 0`, so even the intended startup grace is skipped.

**Expected behaviour:** TCP/HTTP state, first-frame deadline, current RTCM freshness, diagnostic staleness, and forced-reconnect liveness must be independent states/thresholds. A genuinely dead stream must eventually reconnect without using the short diagnostic threshold blindly.

**Root cause:** The state machine has transport/protocol failure transitions but no correction inactivity deadline; `last_correction_activity_time_` is written but never used for control.

**Why this is a real bug:** A TCP peer may remain open indefinitely while application data stops. `Streaming` therefore does not imply RTCM flow.

**User/runtime impact:** Corrections stop, RTK degrades, and the node never self-recovers; one diagnostic still says `ntrip_streaming`/active transport.

**Reproduction:** `/tmp/universal-gnss-audit-ntrip-liveness-repro` reports `header_only_state=3 ... silent_payload=0 rtcm_frames=0` with the state unchanged.

**Evidence:** Focused adopted-socket repro; independent source trace; pbatsa stale-reconnect branches report the same field hypothesis.

**Minimal proposed fix:** Track header time, first valid RTCM time, and last valid RTCM time; add separately configurable first-frame and forced-reconnect thresholds with jitter/backoff. Use correction-health staleness only for diagnostics.

**Regression test that should be added:** Header-only silence; one frame then silence; legitimate gaps below reconnect threshold; exact threshold boundaries; repeated reconnects without storms.

**Compatibility/public-behaviour impact:** Adds reconnect behavior and parameters; document defaults carefully.

**ROS2 impact:** Direct NtripNode state and diagnostics change.

**MowgliNext downstream impact, if any:** Operator UI should distinguish connected, streaming-header, RTCM flowing, stale, and reconnecting; localization/safety should react to correction loss independently of TCP.

**Related existing PR:** pbatsa `codex/ntrip-stale-reconnect*`; failure confirmed, proposed universal 5-second action threshold rejected as insufficiently separated.

**Possible false-positive considerations:** Some casters have legitimate gaps, which argues for independent thresholds, not for omitting liveness detection.

### UGA-006 — reconnect backoff is reset before NTRIP success

- **ID:** UGA-006
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** NTRIP client reconnect policy
- **File(s):** `gnss_ntrip/src/ntrip_client.cpp`; `gnss_ntrip/src/ntrip_reconnect_policy.cpp`
- **Function(s):** `NtripClient::Connect`, `AdoptConnectedSocket`, `RecordReconnectSuccess`, `FailWith`
- **Relevant line(s):** client 276-322, 711-723, 845-862; policy 90-132
- **Category:** retry/backoff state machine

**Observed behaviour:** Every successful TCP `connect`/socket adoption calls `RecordReconnectSuccess` and resets attempt count/delay before the NTRIP request, authentication, HTTP response, or RTCM stream succeeds. Repeated TCP success followed by HTTP 401/protocol failure therefore repeats attempt 1 and the minimum delay forever.

**Expected behaviour:** Reconnect success should be recorded only after a meaningful application-level success, such as accepted response plus valid correction flow or a documented stability interval.

**Root cause:** TCP connectivity is treated as full NTRIP success at `Connect` lines 298-301.

**Why this is a real bug:** Authentication and caster protocol failures occur after TCP connect and are exactly the failures exponential backoff is meant to rate-limit.

**User/runtime impact:** A bad credential/mountpoint or unhealthy caster can be hammered indefinitely, produce log storms, and never exhaust a configured maximum attempt count.

**Reproduction:** `/tmp/universal-gnss-audit-ntrip-liveness-repro` performs three post-connect failures and prints `attempts=1 delay_ms=100 exhausted=0` after each.

**Evidence:** Focused policy/client repro and direct transition trace.

**Minimal proposed fix:** Reset backoff only at a defined application-level success; decide whether accepted header, first RTCM frame, or sustained valid flow is the appropriate milestone.

**Regression test that should be added:** Repeated TCP-success/HTTP-failure sequences must increase delay and honor `max_attempts`; a genuinely healthy stream must reset it.

**Compatibility/public-behaviour impact:** Retry timing changes and should be documented.

**ROS2 impact:** NtripNode reconnect frequency and diagnostics.

**MowgliNext downstream impact, if any:** Fewer reconnect storms; UI should show attempt/backoff state.

**Related existing PR:** pbatsa stale-reconnect branches interact with this bug but do not establish the correct success boundary.

**Possible false-positive considerations:** Reset-on-TCP makes recovery fast after a transient HTTP failure, but defeats explicit exponential/max-attempt semantics and is not a valid success definition.

### UGA-007 — stale receiver state is kept fresh for NTRIP GGA by republishing

- **ID:** UGA-007
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** ReceiverNode -> GnssStatus -> NtripNode GGA injection
- **File(s):** `gnss_ros2/src/receiver_node.cpp`; `gnss_ros2/src/ntrip_node.cpp`
- **Function(s):** `ReceiverNode::Impl::PublishNow`; `NtripNode::Impl::OnStatusMessage`, `MaybeInjectGga`
- **Relevant line(s):** receiver node 1418-1461; Ntrip node 319-323, 516-537, 715-733
- **Category:** cross-layer freshness / stale-data injection

**Observed behaviour:** ReceiverNode publishes its current `GnssStatus` on every publish timer whether or not a new runtime observation arrived. NtripNode overwrites `last_status_time_` on every callback and tests only callback age against five seconds. A dead receiver stream can therefore keep obsolete coordinates eligible for GGA injection indefinitely as long as ReceiverNode continues publishing.

**Expected behaviour:** GGA eligibility must be based on the underlying GNSS observation/receipt age and validity, not ROS callback arrival. Publication cadence and measurement cadence must be independent.

**Root cause:** NtripNode ignores `message.stamp`/upstream health and has no observation-generation identity; ReceiverNode republishes unchanged state.

**Why this is a real bug:** Callback receipt proves only that the ROS graph and publisher timer are alive, not that GNSS data is current.

**User/runtime impact:** A caster receives stale position after GNSS loss, potentially selecting or maintaining the wrong VRS correction context. Conversely, a valid but slower-than-0.2 Hz status publisher is falsely rejected between callbacks.

**Reproduction:** Publish one valid status, then repeatedly publish the identical status at <5-second intervals without new receiver bytes; `last_status_time_` continually refreshes and `MaybeInjectGga` remains enabled.

**Evidence:** Direct cross-node transition trace. MowgliNext PR #2's low-rate concern is directionally valid, but deriving freshness from publish rate does not fix observation provenance.

**Minimal proposed fix:** Carry and validate an explicit receipt/measurement timestamp and fix validity; expose a configurable expected GNSS age independent of publish rate. Do not refresh it for identical republished state.

**Regression test that should be added:** One observation followed by repeated republishes; slow valid observations; no-fix transition; ROS clock jumps; exact stale boundary.

**Compatibility/public-behaviour impact:** GGA injection may stop where it currently continues; document the safety semantics.

**ROS2 impact:** Direct; likely needs a status freshness/sequence surface.

**MowgliNext downstream impact, if any:** UI and localization should consume true observation freshness and stop treating status callback liveness as GNSS liveness.

**Related existing PR:** mowglinext PR #2 partially confirms the rate problem; its publish-rate formula is not a complete fix.

**Possible false-positive considerations:** ReceiverNode itself diagnoses runtime staleness, but NtripNode does not consume that diagnosis and independently continues GGA injection.

### UGA-008 — semantically malformed RTCM satisfies correction health

- **ID:** UGA-008
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** RTCM correction monitor / NTRIP and ROS diagnostics
- **File(s):** `gnss_protocols/src/rtcm_correction_monitor.cpp`
- **Function(s):** `RtcmCorrectionMonitor::ObserveFrame`, `HasRequiredCorrectionMessages`, `BuildRtcmCorrectionHealth`
- **Relevant line(s):** 275-307, 567-681, 803-910
- **Category:** protocol validation / false availability

**Observed behaviour:** CRC-valid two-byte payloads labelled 1005, 1077, and 1230 fail their semantic decoders and increment malformed counters, yet are recorded as valid message-type activity. They satisfy base/MSM/1230 requirements; `parser_healthy` remains true because it checks only `invalid_frames`; health returns `correction_available=true`.

**Expected behaviour:** A required semantic category must be satisfied only by a successfully decoded, structurally valid record; malformed known messages must affect parser health/availability.

**Root cause:** Message-type activity is recorded after generic type parsing but before/independently of successful specialized decoding. `parser_healthy` ignores malformed counters.

**Why this is a real bug:** CRC authenticates transport integrity, not payload structure. A truncated/malformed known message cannot provide base coordinates, MSM observations, or bias semantics.

**User/runtime impact:** Corrections are reported available/healthy while a receiver cannot use them, delaying fault response and misleading autonomy/operator logic.

**Reproduction:** `/tmp/universal-gnss-audit-rtcm-semantic-repro` prints `valid=3 invalid=0 malformed_arp=1 malformed_msm=1 malformed_1230=1 required=1 parser_healthy=1 correction_available=1`.

**Evidence:** Focused CRC-valid frame repro plus code trace.

**Minimal proposed fix:** Maintain decoded-valid activity separately from framed-valid activity and use it for semantic requirements; include malformed known records in parser-health severity.

**Regression test that should be added:** CRC-valid undersized/malformed 1005/1006, every MSM class, and 1230 must never satisfy requirements; valid unknown RTCM should remain transport-valid without satisfying semantics.

**Compatibility/public-behaviour impact:** Correction availability becomes stricter and truthful.

**ROS2 impact:** NtripNode and ReceiverNode correction diagnostics/status change.

**MowgliNext downstream impact, if any:** Operator and localization/safety consumers must treat the corrected availability signal as authoritative and validate degradation transitions.

**Related existing PR:** None directly; PR #4 addresses optionality, not malformed semantic acceptance.

**Possible false-positive considerations:** Some message types may intentionally be only classified, but required 1005/MSM/1230 semantics have implemented decoders whose failure is currently ignored for availability.

### UGA-009 — partial nonblocking RTCM writes discard an already-started frame

- **ID:** UGA-009
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** ROS2 correction forwarding / transport
- **File(s):** `gnss_ros2/src/receiver_node.cpp`; `gnss_transport/src/tcp_client_transport.cpp`
- **Function(s):** `ReceiverNode::Impl::OnRtcmMessage`; `TcpClientTransport::Write`
- **Relevant line(s):** receiver node 815-850; TCP transport 442-473
- **Category:** partial write / protocol stream corruption

**Observed behaviour:** The forwarding loop may successfully write a frame prefix, then receive a legal nonblocking `status=Ok, bytes_written=0` (`EAGAIN`). It breaks, reports the frame failed, and discards the unwritten suffix. The next callback starts the next RTCM frame on the same byte stream.

**Expected behaviour:** Once a frame prefix is emitted, the exact remainder must remain queued and be retried before any later frame; backpressure must not create an invalid concatenation.

**Root cause:** The lambda handles immediate short writes but has no persistent output queue/state across callbacks and treats zero progress as terminal for that message.

**Why this is a real bug:** Stream transports explicitly permit short/zero-progress nonblocking writes. Logging an error cannot retract bytes already delivered.

**User/runtime impact:** The receiver sees a truncated frame followed by another preamble, may lose multiple corrections during resynchronization, and can degrade RTK under ordinary backpressure.

**Reproduction:** Use a `ByteDuplex` stub returning a positive prefix on the first call and `{0, Ok, None}` on the second; observe only the prefix stored and the next message written afterward.

**Evidence:** Deterministic return-path analysis; the historical mowglinext PR #1 includes a partial-write concern among inherited changes.

**Minimal proposed fix:** Add a bounded persistent write queue with offset, retry on writability/timer, preserve ordering, and define overflow/drop policy at whole-frame granularity.

**Regression test that should be added:** Every split position, zero-progress after prefix, EAGAIN/recovery, terminal error, queue overflow, and two consecutive frames.

**Compatibility/public-behaviour impact:** Forwarding becomes buffered; latency/backpressure metrics should be documented.

**ROS2 impact:** Direct receiver correction subscription behavior.

**MowgliNext downstream impact, if any:** Add operator visibility for queued/dropped corrections and field-test constrained serial/TCP sinks.

**Related existing PR:** mowglinext PR #1 partially overlaps; do not import unrelated branch history.

**Possible false-positive considerations:** POSIX serial often accepts small RTCM frames atomically in practice, but neither `ByteDuplex` nor TCP/nonblocking serial guarantees it.

### UGA-010 — Unicore GPGGA and PVTSLNA commands use the wrong grammar

- **ID:** UGA-010
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** Unicore configuration builder and documentation
- **File(s):** `gnss_driver/src/unicore_config_profile_builder.cpp`; `docs/vendors/unicore/config_profiles.md`
- **Function(s):** `UsesLogOntimeSyntax`, `BuildOutputCommand`
- **Relevant line(s):** builder 121-147, 426-435; documentation 132-174
- **Category:** vendor protocol / invalid configuration command

**Observed behaviour:** Production-ready plans emit `LOG GPGGA ONTIME <period>` and `LOG PVTSLNA ONTIME <period>`.

**Expected behaviour:** The local authoritative N4 command manual documents direct N4 output grammar (`GPGGA <period>` or its documented COM form, and `PVTSLNA <period>`). It does not define these NovAtel-style `LOG ... ONTIME` forms.

**Root cause:** A per-message syntax table encodes an undocumented grammar, and the repository documentation repeats the implementation rather than vendor truth.

**Why this is a real bug:** The exact emitted command strings do not match the receiver command manual; a valid C++ string is not a valid receiver command.

**User/runtime impact:** GGA/PVTSLNA output may remain disabled, configuration apply may reject/time out, and runtime/diagnostic fields expected from those messages never arrive.

**Reproduction:** `/tmp/universal-gnss-audit-gcc-debug/gnss_tools/gnss_config_plan unicore rover_high_precision --model UM982` and inspect commands 8 and 11.

**Evidence:** Local `docs/vendors/unicore/Unicore Reference Commands Manual For N4 High Precision Products_V2_EN_R1.4.pdf`, GPGGA and PVTSLNA command sections; no `LOG PVTSLNA` command occurs in the manual, and `LOG GPGGA` appears only as part of `UNLOG GPGGA` text. `config_profiles.md` lines 136/139 contradict the vendor manual.

**Minimal proposed fix:** Replace the syntax table with model/manual-validated N4 forms and correct documentation; keep receiver-specific grammar isolated.

**Regression test that should be added:** Golden commands derived from the vendor manual for every message/model/port form, not from duplicated production constants.

**Compatibility/public-behaviour impact:** Public plan output changes; document the correction.

**ROS2 impact:** Indirect: missing configured outputs reduce runtime/status observability.

**MowgliNext downstream impact, if any:** Revalidate receiver provisioning and confirm expected topics/diagnostics on hardware.

**Related existing PR:** mowglinext PR #1 investigates Unicore configuration but its inherited diff is not authoritative for these command forms.

**Possible false-positive considerations:** A firmware-specific undocumented alias is possible; no such support exists in the authoritative local manual, so hardware acceptance is a useful confirmation but not a basis for claiming the documented planner is correct.

### UGA-011 — `SIGNALGROUP` can reboot before the remainder of the plan

- **ID:** UGA-011
- **Severity:** P1 High
- **Confidence:** High
- **Component:** Unicore plan ordering and live apply
- **File(s):** `gnss_driver/src/unicore_config_profile_builder.cpp`; `gnss_tools/src/config_apply.cpp`; `docs/vendors/unicore/config_profiles.md`
- **Function(s):** `UnicoreConfigProfileBuilder::Build`; `ExecuteConfigApply`
- **Relevant line(s):** builder 354-397; apply 1508-1663; documentation 256-263
- **Category:** vendor reboot/state transition / partial configuration

**Observed behaviour:** `CONFIG SIGNALGROUP` is placed before `UNLOG`, all output commands, and `SAVECONFIG`. Generic apply has no post-`SIGNALGROUP` close/reprobe/reopen phase. The local vendor manual warns that `SIGNALGROUP` can reboot the receiver and instructs saving other configurations first.

**Expected behaviour:** A reboot-causing command must terminate a phase; required preceding changes must be saved in the documented order, then transport reachability must be re-established and post-reboot state verified before continuing.

**Root cause:** `SIGNALGROUP` is classified as an ordinary runtime command, and only the factory-reset workflow has reconnect hooks.

**Why this is a real bug:** If the receiver restarts, the waiting transaction times out or reads a closed link; later output/save commands are not executed and earlier runtime-only changes may be lost.

**User/runtime impact:** A plan reported production-ready leaves the receiver partially configured or unsaved, possibly with changed signals but missing navigation output.

**Reproduction:** Generate the UM982 plan shown under UGA-013: command 6 is `SIGNALGROUP`, commands 7-15 depend on the same uninterrupted connection.

**Evidence:** Local N4 manual `CONFIG SIGNALGROUP` warning; direct command ordering; apply code has special reopen only for `PlanUsesUnicoreRecoveryWorkflow` (factory reset), not signal changes.

**Minimal proposed fix:** Model `SIGNALGROUP` as a reboot boundary, order/save commands per vendor instructions, and use explicit reprobe/reopen/verification phases.

**Regression test that should be added:** Simulate disconnect/reboot at `SIGNALGROUP`; ensure no later command is sent on the stale fd and resume only after verified model/baud recovery.

**Compatibility/public-behaviour impact:** Plan order, safety classification, and live-apply behavior change; documentation required.

**ROS2 impact:** Indirect provisioning/runtime-output impact.

**MowgliNext downstream impact, if any:** Robot provisioning must surface reboot/recovery progress and fail safe on incomplete configuration; hardware field validation is required.

**Related existing PR:** mowglinext PR #1 is related but its proposed application-specific UM980 mode/defaults do not fix this generic sequencing defect.

**Possible false-positive considerations:** Exact reboot timing may vary by model/build, so hardware validation is marked below; the manual warning and lack of any recovery path make the current general plan unsafe regardless.

### UGA-012 — unknown Unicore models receive guessed production-ready configuration

- **ID:** UGA-012
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** receiver auto-configuration planner
- **File(s):** `gnss_driver/src/receiver_auto_config.cpp`
- **Function(s):** `BuildUnicorePlan`
- **Relevant line(s):** 795-840, 865-895, 997-1026
- **Category:** receiver/model safety / unsafe fallback

**Observed behaviour:** An explicitly unknown model such as `FUTURE123` gets mode, NMEA version, RTK/DGPS/reliability defaults, `UNLOG`, and eight output commands. The plan says `Production ready: yes`; only `SIGNALGROUP` is skipped.

**Expected behaviour:** An unknown model must not receive guessed vendor-specific mutation. Discovery/read-only operation may fall back, but configuration must require a documented model/profile.

**Root cause:** The generic model profile is treated as `config_supported=true`; the unknown-model guard applies only to signal-group override.

**Why this is a real bug:** Model identity is a precondition for command applicability, reboot behavior, valid output grammar, and defaults. Skipping one command does not make the rest safe.

**User/runtime impact:** Unsupported commands, disabled outputs, unexpected reboot/mode changes, or persistent misconfiguration on future/different N4 products.

**Reproduction:** `/tmp/universal-gnss-audit-gcc-debug/gnss_tools/gnss_config_plan unicore rover_high_precision --model FUTURE123` emits 14 mutations and marks the plan production-ready.

**Evidence:** Exact CLI output and code path. This directly violates the audit/AGENTS invariant for unknown models.

**Minimal proposed fix:** Restrict unknown models to read-only/runtime-no-op or require an explicit expert unsafe override with no production-ready claim; validate every emitted command against a model profile.

**Regression test that should be added:** Every unknown/empty/malformed model token must emit zero mutating commands for non-read-only profiles.

**Compatibility/public-behaviour impact:** Previously accepted unsafe plans become rejected; document it.

**ROS2 impact:** Indirect if provisioning precedes ROS runtime.

**MowgliNext downstream impact, if any:** Robot provisioning must provide verified model identity and handle explicit rejection.

**Related existing PR:** mowglinext PR #1 reinforces model-specific differences but is not itself a safe generic fallback.

**Possible false-positive considerations:** Some commands are shared across N4 products, but the public invariant explicitly forbids guessed configuration and the emitted set includes model/build-sensitive behavior.

### UGA-013 — high Unicore rate overrides round to a zero-period command

- **ID:** UGA-013
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** Unicore auto-configuration rate planning
- **File(s):** `gnss_driver/src/receiver_auto_config.cpp`; `gnss_driver/src/unicore_config_profile_builder.cpp`
- **Function(s):** `ValidateRateHz`, `BuildUnicorePlan`, `FormatPeriodSeconds`, `ValidateOutputRate`
- **Relevant line(s):** auto config 391-406 and 984-995; builder 15-34 and 149-165
- **Category:** numeric range/rounding / invalid receiver configuration

**Observed behaviour:** Any positive finite rate passes the generic validation. At 10,000 Hz, period `0.0001` is formatted to three decimals as `0`, but validation occurred before formatting and accepts it. The production-ready plan emits `BESTNAVA 0` while describing it as every 0 seconds. u-blox correctly rejects the same rate through its builder.

**Expected behaviour:** Rate must be checked against per-message/model legal ranges and against representable command precision; formatting must never turn a validated positive period into zero.

**Root cause:** Unicore validation has no maximum/minimum period and is disconnected from lossy three-decimal formatting.

**Why this is a real bug:** The emitted command does not represent the operator request and may disable output or be rejected, while the plan explicitly claims readiness.

**User/runtime impact:** Loss of primary navigation output or failed/partial live configuration.

**Reproduction:** `/tmp/universal-gnss-audit-gcc-debug/gnss_tools/gnss_config_plan unicore rover_high_precision --model UM982 --rate-hz 10000`; command 12 is `BESTNAVA 0` and exit status is success.

**Evidence:** Exact CLI result and source arithmetic; corresponding u-blox invocation returns `rate is out of supported range`.

**Minimal proposed fix:** Add model/message-specific supported periods/rates, validate the final serialized value, and reject unrepresentable rates.

**Regression test that should be added:** Just below/at/above each supported limit and formatting half-step; assert serialized period remains positive and round-trip error is bounded.

**Compatibility/public-behaviour impact:** Extreme formerly accepted rates become errors.

**ROS2 impact:** Indirect output loss if provisioning succeeds incorrectly.

**MowgliNext downstream impact, if any:** Provisioning UI should constrain rates to verified receiver limits and field-test actual output cadence.

**Related existing PR:** None direct.

**Possible false-positive considerations:** A receiver might assign a special meaning to zero, but that meaning still cannot represent 10,000 Hz and is not documented as the requested rate.

### UGA-014 — RTCM ROS stamps mix monotonic and ROS/system clock domains

- **ID:** UGA-014
- **Severity:** P1 High
- **Confidence:** Confirmed
- **Component:** NtripNode -> `RtcmFrame` -> ReceiverNode correction monitor
- **File(s):** `gnss_ros2/src/ntrip_node.cpp`; `gnss_ros2/src/receiver_node.cpp`; `gnss_ros2/msg/RtcmFrame.msg`; `gnss_protocols/src/rtcm_correction_monitor.cpp`
- **Function(s):** `NtripNode::Impl::StepOnce`, `ReadOnce`; `RtcmTimestampFromRosMessage`, `ObserveRtcmSemanticMessage`; `ComputeAgeSince`, `HasSeenSince`
- **Relevant line(s):** Ntrip node 333-360 and 455-485; receiver node 263-280 and 855-872; monitor 81-113
- **Category:** clock-domain mismatch / false freshness

**Observed behaviour:** NtripNode passes `steady_clock::time_since_epoch()` into RTCM frames, serializes that integer into a ROS `builtin_interfaces/Time`, and publishes it. ReceiverNode interprets any nonzero ROS stamp as a protocol timestamp and compares it to its own monotonic clock. An ordinary external ROS/system-time producer instead supplies epoch-scale values, yielding negative ages and future observations that pass the monitor's lower-bound-only freshness tests. Replay timestamps can have yet another origin.

**Expected behaviour:** A ROS `Time` field must have a documented ROS clock domain. Internal monotonic receipt time must not masquerade as ROS time, and freshness comparisons must use one domain with future/out-of-order rejection.

**Root cause:** One field is reused for public ROS time and internal monotonic health time; monitor arithmetic does not reject `last_seen > now`.

**Why this is a real bug:** Clock epochs are not interchangeable. Correctness currently depends on both nodes sharing the same host boot-time origin, an unstated guarantee absent for other publishers/replay/sim time.

**User/runtime impact:** RTCM may appear indefinitely fresh, immediately ancient, or have nonsensical public stamps; source B can inherit misleading health based solely on timestamp domain.

**Reproduction:** Publish a valid `RtcmFrame` stamped with current Unix/ROS time to ReceiverNode on a host whose monotonic uptime is much smaller; `AgeSince...` becomes negative and stale/required-window checks treat it as current/future.

**Evidence:** Direct arithmetic and serialization trace; monitor `ComputeAgeSince` subtracts without domain/future validation and `HasSeenSince` has no upper bound.

**Minimal proposed fix:** Define `RtcmFrame.stamp` as ROS time, capture a separate local steady receipt time for health, and validate future/out-of-order values. Do not compare remote/public stamps to local steady clock.

**Regression test that should be added:** ROS/system, steady, simulated, zero, future, out-of-order, and replay-relative stamps across separate node processes.

**Compatibility/public-behaviour impact:** Clarifies/corrects public timestamp semantics; bag/replay consumers need documentation.

**ROS2 impact:** Direct message and diagnostic behavior.

**MowgliNext downstream impact, if any:** Localization/operator displays must use corrected ROS stamps while safety freshness uses local receipt time; validate multi-host and simulation deployments.

**Related existing PR:** None found.

**Possible false-positive considerations:** Linux `steady_clock` normally shares a boot origin across processes on one host, explaining passing integration tests; ROS explicitly permits other hosts/clocks/producers, so that coincidence is not a contract.

## P2 Medium findings

### UGA-015 — portable correction health unconditionally requires RTCM 1230

- **ID:** UGA-015
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** RTCM correction requirements / NTRIP and ROS diagnostics
- **File(s):** `gnss_protocols/src/rtcm_correction_monitor.cpp`; `gnss_ros2/src/ntrip_node.cpp`
- **Function(s):** `ConfigurePortableRtkCorrectionRequirements`, `BuildCorrectionHealthOptions`
- **Relevant line(s):** monitor 231-237 and 666-679; Ntrip node 247-258 and 562-571
- **Category:** incorrect protocol requirement / availability semantics

**Observed behaviour:** The portable preset always sets `require_glonass_bias=true`; a fresh, valid GPS/Galileo/BeiDou-only or otherwise non-GLONASS correction stream is reported missing required corrections solely because it has no 1230.

**Expected behaviour:** RTCM 1230 should be required only when the selected correction content/receiver use actually needs GLONASS FDMA code-phase bias; optional metadata must not make a valid non-GLONASS stream unavailable.

**Root cause:** A single hard-coded portable requirement is used without constellation/source capability context.

**Why this is a real bug:** 1230 is not universally mandatory for RTK correction streams. The later upstream fix/PR #4 independently removes this universal assumption.

**User/runtime impact:** False error/unavailable diagnostics and possible downstream refusal of usable corrections.

**Reproduction:** Observe fresh valid 1005 plus a valid non-GLONASS MSM and build health with the portable options; required messages remain false until 1230 is observed.

**Evidence:** Direct option/function trace and Pepeuch PR #4, merged into later public `main` but absent from the audited commit.

**Minimal proposed fix:** Make required semantic categories explicit/configurable and derive 1230 need from enabled GLONASS correction content/receiver requirements.

**Regression test that should be added:** Non-GLONASS MSM with no 1230 must be healthy; GLONASS-required profiles must fail without it and pass only with a semantically valid 1230.

**Compatibility/public-behaviour impact:** Some streams change from error to available; document the requirement model.

**ROS2 impact:** Correction diagnostics/availability change.

**MowgliNext downstream impact, if any:** Operator UI should display actual constellation requirements rather than a universal 1230 alarm.

**Related existing PR:** Pepeuch PR #4 — independently confirmed.

**Possible false-positive considerations:** 1230 is important for GLONASS FDMA ambiguity handling, but that does not make it mandatory when GLONASS is absent/not required.

### UGA-016 — static base metadata is expired as a 30-second dynamic observation

- **ID:** UGA-016
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** RTCM correction health
- **File(s):** `gnss_protocols/src/rtcm_correction_monitor.cpp`; `gnss_ros2/src/ntrip_node.cpp`
- **Function(s):** `HasRequiredCorrectionMessages`, `BuildCorrectionHealthOptions`
- **Relevant line(s):** monitor 589-679; Ntrip node 250-258 and 562-571
- **Category:** state lifetime / static-versus-dynamic semantics

**Observed behaviour:** The same 30-second `required_observation_window_ns` is applied to dynamic MSM and static/session base ARP 1005/1006 (and 1230 when enabled). Fresh MSM stops satisfying correction health once the last static message ages past the window.

**Expected behaviour:** Dynamic epochs need freshness; successfully decoded station metadata should remain valid for the identified correction source/station until explicitly replaced, invalidated, or a source-identity transition occurs.

**Root cause:** Requirement satisfaction is reduced to recent message-type timestamps without semantic lifetime categories.

**Why this is a real bug:** A caster may transmit unchanged static metadata once or less frequently than 30 seconds. Absence of repetition does not invalidate its content.

**User/runtime impact:** Periodic false correction outages/errors despite continuous usable observations.

**Reproduction:** Observe a valid 1005 at T0 and valid MSM through T31s; health at T31 with the ROS options rejects the base requirement although the ARP has not changed.

**Evidence:** Direct window logic and the failure pattern motivating Pepeuch PR #5; the audit brief explicitly distinguishes static/session metadata from dynamic observations.

**Minimal proposed fix:** Track decoded static metadata validity by source/station identity, separate from dynamic freshness windows.

**Regression test that should be added:** One static ARP plus long-running MSM remains healthy; station/source changes invalidate static state; malformed replacement does not silently retain incompatible metadata.

**Compatibility/public-behaviour impact:** Availability remains true longer for unchanged identified stations; source invalidation semantics must be documented.

**ROS2 impact:** Correction health/semantic observation diagnostics.

**MowgliNext downstream impact, if any:** UI should distinguish retained station metadata from current MSM flow and validate station switches.

**Related existing PR:** Pepeuch PR #5 identifies retention but its patch alone lacks the source ownership required by UGA-018.

**Possible false-positive considerations:** Many casters repeat 1005/1006 every 10 seconds, masking the issue; that cadence is not guaranteed by the monitor's API or RTCM semantics.

### UGA-017 — RTCM rate history grows without bound and queries are linear

- **ID:** UGA-017
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** RTCM correction monitor / long-running nodes
- **File(s):** `gnss_protocols/include/universal_gnss_protocols/rtcm_correction_monitor.hpp`; `gnss_protocols/src/rtcm_correction_monitor.cpp`
- **Function(s):** `AppendTimestamp`, `CountTimestampsInWindow`, `ComputeRateHz`, `RecordValidMessage`
- **Relevant line(s):** header 167-170; source 32-78 and 275-280, 792-800
- **Category:** unbounded memory / O(n) liveness degradation

**Observed behaviour:** Every timestamped frame is appended to total, valid, type, and often constellation vectors. Nothing prunes history. Every rate query scans an entire vector even though only a recent window is requested.

**Expected behaviour:** Monitoring storage/work should be bounded by the largest configured window or fixed-size counters/buckets.

**Root cause:** Lifetime vectors are used as window statistics without eviction.

**Why this is a real bug:** NtripNode is intended to run continuously; memory and diagnostic CPU cost increase monotonically with correction count.

**User/runtime impact:** Long-run memory growth, increasingly expensive diagnostics, and eventual resource pressure/restart on embedded or robot computers.

**Reproduction:** `/tmp/universal-gnss-audit-rtcm-growth-repro` records 2,000,000 type-1077 messages: `VmRSS` about 66,340 kB and a single rate scan about 1,181 us; both continue growing with count.

**Evidence:** Focused long-run repro and container layout. One observation can be stored in multiple vectors, so raw vector element estimates understate overhead.

**Minimal proposed fix:** Prune by maximum window on insertion or use bounded time buckets/ring buffers; keep lifetime counts separately.

**Regression test that should be added:** Tens of millions of synthetic observations with asserted bounded container size/memory and near-constant query time.

**Compatibility/public-behaviour impact:** Rate outputs should remain equivalent within documented windows.

**ROS2 impact:** NtripNode and ReceiverNode diagnostic liveness.

**MowgliNext downstream impact, if any:** Improves long-duration mission stability; perform multi-hour soak validation.

**Related existing PR:** None found.

**Possible false-positive considerations:** 66 MB may be acceptable on a workstation, but growth has no ceiling and the deployment includes resource-constrained systems.

### UGA-018 — correction metadata has no endpoint/station ownership model

- **ID:** UGA-018
- **Severity:** P2 Medium
- **Confidence:** High
- **Component:** NTRIP reconnect/reset and RTCM semantic coherence
- **File(s):** `gnss_ntrip/src/ntrip_client.cpp`; `gnss_protocols/src/rtcm_correction_monitor.cpp`
- **Function(s):** `NtripClient::set_config`, `Connect`, `ResetSessionState`; `RtcmCorrectionMonitor::Reset`, `HasRequiredCorrectionMessages`
- **Relevant line(s):** client 252-259, 276-284, 726-733; monitor 239-273 and 581-681
- **Category:** source identity / reset too much and validate too little

**Observed behaviour:** Every reconnect clears all decoded static metadata even for the same endpoint/station. Conversely, within a connection the health check never requires base/MSM/1230 station identifiers to agree; it only checks message type/constellation presence. `set_config` can also change endpoint fields while a socket is open without forcing a source transition.

**Expected behaviour:** Static metadata should be owned by a normalized endpoint plus decoded station/source identity: retained only across a verified same-source reconnect, invalidated on host/port/mountpoint/station change, and never combined across inconsistent station IDs.

**Root cause:** The monitor has frame/message activity but no correction-source identity object or station-coherence invariant; reset is all-or-nothing.

**Why this is a real bug:** Current behavior creates an avoidable outage for stable same-source reconnects, while a naive retention fix (PR #5) would let source A make source B healthy. Station mixing can already occur inside one accepted stream.

**User/runtime impact:** False unavailability after reconnect or false availability from mixed station metadata/observations; VRS/mountpoint changes are especially sensitive.

**Reproduction:** Same endpoint: observe static metadata, call `Connect`/`ResetSessionState`, inspect cleared monitor. Mixed source: provide decoded base and MSM with different station IDs; requirement logic contains no comparison and can pass.

**Evidence:** Reset trace; decoded records carry station IDs but health ignores them; Pepeuch PR #5 confirms the field symptom but not safe ownership.

**Minimal proposed fix:** Introduce explicit endpoint/source/station identity and separate transport-session reset, dynamic reset, same-source retention, and source-change invalidation.

**Regression test that should be added:** Same endpoint/same station retention; every endpoint component change; station-ID change; VRS reassignment; malformed/missing identity; alternating A/B sources.

**Compatibility/public-behaviour impact:** Reconnect health semantics change and require documentation.

**ROS2 impact:** NtripNode state/diagnostics and correction metadata surfaces.

**MowgliNext downstream impact, if any:** UI should show source/station transitions; localization/safety should invalidate corrections immediately on unverified source switches.

**Related existing PR:** Pepeuch PR #5 — symptom confirmed, proposed retention only partially safe.

**Possible false-positive considerations:** Current full reset prevents cross-reconnect leakage, but does not solve same-source erasure or within-stream station mismatch; the finding does not recommend unconditional retention.

### UGA-019 — “forwarding active” means “ever forwarded”

- **ID:** UGA-019
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** NtripNode and ReceiverNode diagnostics
- **File(s):** `gnss_ros2/src/ntrip_node.cpp`; `gnss_ros2/src/receiver_node.cpp`
- **Function(s):** `BuildHealthSummary`, `AppendRtcmForwardingStatus`
- **Relevant line(s):** Ntrip node 707-713 and 771-805; receiver node 1094-1148
- **Category:** diagnostics / lifetime counter used as freshness

**Observed behaviour:** Once `rtcm_published_frames_`, `rtcm_forwarded_frames_`, u-blox accepted count, or Unicore status count becomes positive, an OK `*_active` event can be emitted forever. Ntrip's dedicated forwarding status remains `OK: RTCM forwarding active` after flow has stopped.

**Expected behaviour:** “Active” must require recent successful frame publication/write/use; lifetime counters should be labelled separately as totals.

**Root cause:** Boolean activity is derived from monotonic lifetime counters instead of existing last-frame/last-forward timestamps and freshness thresholds.

**Why this is a real bug:** The label describes current state but the condition only proves history. It directly violates `RTCM seen once != RTCM fresh now`.

**User/runtime impact:** Operators and monitoring systems can see simultaneous stale correction warnings and an OK active status, or only the misleading OK substatus.

**Reproduction:** Publish/forward one valid frame, stop the stream, advance beyond all correction thresholds, and call `PublishNow`; the lifetime-count active event/status remains.

**Evidence:** Direct predicates; pbatsa's stale-stream hypothesis explicitly requested this check and is confirmed.

**Minimal proposed fix:** Base “active” on last successful activity age; expose `ever/total` counters separately and resolve stale OK events.

**Regression test that should be added:** Active just before/exactly at/after timeout, recovery, failure after success, counter reset, and receiver-reported use freshness.

**Compatibility/public-behaviour impact:** Diagnostic event/status behavior changes, with no protocol change.

**ROS2 impact:** Direct diagnostics.

**MowgliNext downstream impact, if any:** Operator GUI should use current freshness and display totals as history, not health.

**Related existing PR:** pbatsa stale-reconnect branches; failure confirmed independently.

**Possible false-positive considerations:** The overall summary may also contain a stale warning, but contradictory per-component OK semantics remain unsafe and machine consumers often select individual statuses.

### UGA-020 — fixed three-second receiver staleness rejects valid slow cadences

- **ID:** UGA-020
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** ROS2 ReceiverNode freshness
- **File(s):** `gnss_ros2/src/receiver_node.cpp`
- **Function(s):** `BuildHealthSummary`, `HasFreshRuntimeState`, `CanPublishFixMessage`
- **Relevant line(s):** 710, 1025-1053, 1498-1502, 1607-1616
- **Category:** timeout/data-rate mismatch

**Observed behaviour:** A universal fixed three-second timeout marks transport/runtime stale and suppresses fixes. A valid receiver/profile producing the relevant message every four seconds is necessarily stale for part of every cycle. No expected observation rate or timeout parameter exists.

**Expected behaviour:** Freshness must be configured/derived from the actual expected receiver observation cadence plus jitter, not ROS publish rate.

**Root cause:** One magic timeout is applied to all receiver protocols, profiles, and data rates.

**Why this is a real bug:** Slow output is legal and can be intentional; elapsed time alone is only meaningful relative to the expected data cadence.

**User/runtime impact:** Periodic stale alarms and dropped `/fix` output despite a healthy low-rate receiver.

**Reproduction:** Feed valid runtime observations at T=0 and T=4s while publishing faster; from T=3s to T=4s `HasFreshRuntimeState` is false and `CanPublishFixMessage` suppresses the fix.

**Evidence:** Exact threshold code; mowglinext PR #2 independently reports this class.

**Minimal proposed fix:** Add an explicit expected observation cadence/stale timeout per active stream/profile; keep publication cadence independent. A conservative derived default may use detected input cadence, not merely `publish_rate_hz`.

**Regression test that should be added:** 0.1/0.5/1/10 Hz observation rates crossed with independent publish rates and exact boundaries.

**Compatibility/public-behaviour impact:** Adds/changes a public ROS parameter and stale behavior; document it.

**ROS2 impact:** Direct.

**MowgliNext downstream impact, if any:** Configure according to actual receiver output and validate GUI/localization transitions.

**Related existing PR:** mowglinext PR #2 — failure confirmed, proposed publish-rate-derived timeout only partially accepted because publish rate is not the source cadence.

**Possible false-positive considerations:** Default profiles normally output faster than three seconds; the node explicitly accepts arbitrary receivers/rates and is hardware-agnostic, so the invariant still fails.

### UGA-021 — runtime aggregation cannot propagate explicit value invalidation

- **ID:** UGA-021
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** core runtime aggregation / protocol sessions
- **File(s):** `gnss_core/include/universal_gnss/gnss_runtime_aggregator.hpp`; `gnss_core/include/universal_gnss/gnss_runtime_state.hpp`; `gnss_protocols/src/nmea_parser.cpp`
- **Function(s):** `GnssRuntimeAggregator::Merge`, `MergeDirectField`, `MergeCapabilityField`; `ClearOptionalValue`; NMEA state merge functions
- **Relevant line(s):** aggregator 21-219 and 300-340; runtime state 100-112; NMEA parser 1197-1242, 1278-1293, 1318-1336
- **Category:** state coherence / stale optional values

**Observed behaviour:** Parsers explicitly call `ClearOptionalValue` when a current sample lacks HDOP, accuracy, satellite, or CN0 data, but this produces the same state as “this partial update did not mention the field.” The aggregator applies only present/value-flagged fields and retains the old value/flag. Direct coordinates likewise cannot be explicitly cleared. A fresh no-fix update can therefore coexist with old coordinates/enrichment.

**Expected behaviour:** The model must distinguish omitted/no-op, current value, and explicit invalidation for every aggregatable field.

**Root cause:** Optional absence and cleared value share one representation; merge semantics intentionally preserve absence. Existing tests encode preservation but do not test explicit parser invalidation.

**Why this is a real bug:** Upstream code attempts explicit clears, proving that some absence is semantically meaningful, but the aggregator silently drops that information.

**User/runtime impact:** Stale accuracy/RTK/baseline/RF fields remain advertised; old coordinates may be published with a fresh `NO_FIX` status and can be misused by consumers that fail to gate every field.

**Reproduction:** `/tmp/universal-gnss-audit-aggregator-clear-repro` prints `clear_update_merged=0 retained_hdop=1 retained_value_flag=1 state_timestamp=1` after a newer explicit clear.

**Evidence:** Focused core repro and parser call sites.

**Minimal proposed fix:** Add tri-state update intent (unchanged/set/clear) or explicit clear masks, with per-field versioning for clears.

**Regression test that should be added:** Explicit clear versus omitted update for every field family; no-fix coordinates; baseline invalidation; out-of-order clears.

**Compatibility/public-behaviour impact:** Previously retained values may become unavailable; document normalized-state semantics.

**ROS2 impact:** Capability/value flags, NavSatFix, status, and diagnostics become more truthful.

**MowgliNext downstream impact, if any:** Localization/operator code must honor value invalidation and test loss/recovery transitions.

**Related existing PR:** None found.

**Possible false-positive considerations:** Retaining values across unrelated partial NMEA sentences is desirable; the defect is the inability to distinguish that case from an explicit clear, not retention itself.

### UGA-022 — ASCII parsers accept NaN/Inf as valid GNSS values

- **ID:** UGA-022
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** NMEA and Unicore ASCII parsing / tools/runtime export
- **File(s):** `gnss_protocols/src/nmea_parser.cpp`; `gnss_protocols/src/unicore_parser.cpp`; downstream formatting paths in `gnss_tools`
- **Function(s):** both `TryParseDouble` implementations and numeric record parsers
- **Relevant line(s):** NMEA 64-81, 127-160, 360-369, 535-570; Unicore 181-236 and position parsers including 1294-1296, 1399-1401
- **Category:** floating-point validation / invalid serialized output

**Observed behaviour:** `strtod` accepts `nan`, `inf`, and signed variants; neither helper requires `std::isfinite`. Positive-only checks also accept NaN because comparisons are false. Valid-checksum NMEA and CRC-valid Unicore BESTNAVA can update runtime with non-finite coordinates/accuracy. `gnss_replay --json` then emits bare `nan`, which is invalid JSON.

**Expected behaviour:** Protocol fields representing coordinates, accuracies, time, speed, and receiver measurements must reject non-finite text and enforce documented physical ranges.

**Root cause:** Syntactic numeric parsing is treated as semantic validity.

**Why this is a real bug:** NaN propagates through arithmetic/comparisons and violates JSON and GNSS coordinate contracts. A checksum/CRC does not make a textual special value valid.

**User/runtime impact:** Poisoned normalized state, invalid exported JSON, inconsistent ROS filtering (fix may be suppressed while status still carries NaN), and unreliable diagnostics.

**Reproduction:** `/tmp/universal-gnss-audit-nmea-repro` prints `nan_record_ready=1 nan_values=1`. A CRC-correct BESTNAVA piped to `gnss_replay --json -` produces `runtime_updated=true` and `"latitude_deg":nan`.

**Evidence:** Two independent protocol repros and source helpers.

**Minimal proposed fix:** Reject non-finite values centrally, then apply field-specific ranges (latitude, longitude, nonnegative accuracies, time, etc.). JSON formatters should remain defensive.

**Regression test that should be added:** `nan`, `+nan`, `inf`, `-inf`, overflow, and physical range boundaries for every textual numeric field and JSON exporter.

**Compatibility/public-behaviour impact:** Malformed inputs become rejected rather than propagated.

**ROS2 impact:** Prevents non-finite status/fix values and contradictory diagnostics.

**MowgliNext downstream impact, if any:** Downstream receives explicit invalid/rejected state instead of NaN; validate alarms and localization gating.

**Related existing PR:** None found.

**Possible false-positive considerations:** IEEE parsers legitimately support special values, but NMEA/Unicore GNSS field grammars and JSON do not grant them meaningful validity here.

### UGA-023 — malformed NMEA can consume the next valid sentence

- **ID:** UGA-023
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** NMEA framer resynchronization
- **File(s):** `gnss_protocols/src/nmea_framer.cpp`
- **Function(s):** `NmeaSentenceFramer::PushByte`
- **Relevant line(s):** 30-60
- **Category:** stream recovery / frame loss

**Observed behaviour:** After a start character is buffered, a new `$`/`!` is treated as ordinary payload until newline. Thus a truncated sentence immediately followed by a complete valid sentence becomes one invalid combined record; the valid sentence is lost.

**Expected behaviour:** An unescaped NMEA leader inside an unfinished line should resynchronize to the new candidate so one malformed sentence cannot corrupt the following valid one.

**Root cause:** Only the empty-buffer state recognizes leaders; there is no nested-start recovery.

**Why this is a real bug:** Serial truncation/drop can remove CR/LF. The next legal leader is a strong resynchronization point and NMEA payload does not require an embedded raw `$`/`!` in this grammar.

**User/runtime impact:** One transport fault loses at least the next otherwise valid fix and can prolong stale state.

**Reproduction:** `/tmp/universal-gnss-audit-nmea-repro` feeds a truncated `$...` followed immediately by valid GGA and prints `nested_start_records=1 valid_records=0`.

**Evidence:** Focused byte-stream repro and direct state-machine trace.

**Minimal proposed fix:** On a new leader while buffering, report/reset the old candidate and start a new frame at that byte, preserving its timestamp.

**Regression test that should be added:** Truncated prefix plus valid sentence for both leaders; leader at every byte position; garbage-valid-garbage-valid.

**Compatibility/public-behaviour impact:** Recovery behavior improves; malformed combined records will be split/rejected differently.

**ROS2 impact:** Fewer dropped fixes/parser anomalies after line damage.

**MowgliNext downstream impact, if any:** Improved fault recovery; field-test induced serial drops.

**Related existing PR:** None found.

**Possible false-positive considerations:** Proprietary text could theoretically contain `$`, but generic NMEA must prioritize documented sentence framing; vendor exceptions should remain isolated.

### UGA-024 — corrupt binary lengths swallow a complete following frame

- **ID:** UGA-024
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** UBX, RTCM3, and Unicore N4 binary framers
- **File(s):** `gnss_protocols/src/ubx_framer.cpp`; `gnss_protocols/src/rtcm_framer.cpp`; `gnss_protocols/src/unicore_binary_framer.cpp`
- **Function(s):** each binary `PushByte`
- **Relevant line(s):** UBX 51-79; RTCM 46-75; Unicore binary 106-143
- **Category:** binary stream recovery / frame loss

**Observed behaviour:** Once a plausible in-range length is read, sync bytes inside the announced body are never considered. A corrupt header can choose a length ending after a complete valid next frame; the framer emits only one invalid candidate (or `InvalidData`) and the embedded valid frame is irretrievably consumed.

**Expected behaviour:** Checksum/CRC failure should rescan buffered bytes for the earliest viable sync sequence so a malformed packet does not corrupt the following valid packet.

**Root cause:** Framers discard the entire candidate on checksum/CRC failure and retain no suffix for resynchronization. UBX/RTCM return an invalid record only after consuming the declared length; N4 resets similarly.

**Why this is a real bug:** A single bit error in a length field is a realistic serial fault; max-length bounds prevent memory overflow but not loss of valid subsequent frames.

**User/runtime impact:** Multiple fixes/corrections may be lost per damaged header, extending outages and parser latency.

**Reproduction:** `/tmp/universal-gnss-audit-binary-resync-repro` feeds corrupt in-range headers whose bodies contain one exact valid frame: `ubx records=1 valid=0 invalid=1 embedded_valid_lost=1` and the same for RTCM.

**Evidence:** Independent UBX/RTCM byte repro; identical N4 state-machine pattern confirmed by inspection.

**Minimal proposed fix:** On failed integrity, replay the longest buffered suffix beginning with a possible sync, with bounded linear-time recovery; reuse `mixed_stream_resync` where appropriate.

**Regression test that should be added:** Corrupt length containing a valid frame at every offset for UBX/RTCM/N4; prove bounded work under repeated sync bytes.

**Compatibility/public-behaviour impact:** Parser recovery/counters change only for malformed streams.

**ROS2 impact:** Fewer dropped runtime/correction records.

**MowgliNext downstream impact, if any:** Improved serial-noise resilience; validate on fault-injected recordings.

**Related existing PR:** None found.

**Possible false-positive considerations:** Immediate nested-sync switching before integrity failure would be unsafe because sync can occur in payload; suffix rescan after failure avoids that ambiguity.

### UGA-025 — checksum-free text gets high-confidence Unicore discovery

- **ID:** UGA-025
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** stream detector / receiver discovery
- **File(s):** `gnss_driver/src/stream_detector.cpp`; `gnss_driver/src/receiver_discovery.cpp`
- **Function(s):** `StreamDetector::Detect`; `CountUnicoreAsciiRecords`; `AnalyzeReceiverProbeBytes`
- **Relevant line(s):** stream detector 126-145; discovery 235-270 and 781-815
- **Category:** auto-discovery false positive

**Observed behaviour:** `#BESTNAVA,garbage\r\n`, with no CRC and no parseable record, is detected as `unicore_ascii`; discovery awards +100, classifies family `unicore`, and reports high confidence. The random-ASCII penalty is not applied to text containing `#`.

**Expected behaviour:** High-confidence vendor detection must require a checksum/CRC-valid, structurally plausible supported record or a verified command response, not just a recognized name prefix.

**Root cause:** The detector predicate requires only `sync_char != '$' && !message_name.empty()`; discovery checks supported name but not checksum or semantic parse status.

**Why this is a real bug:** Noise/log text or another protocol can contain an ASCII token. High confidence influences session/family selection and may precede configuration decisions.

**User/runtime impact:** Wrong parser/session selection, no GNSS output, misleading hardware identity, and unsafe model-family assumptions in tooling.

**Reproduction:** `/tmp/universal-gnss-audit-stream-detect-repro` prints `protocol=unicore_ascii`; `/tmp/universal-gnss-audit-discovery-repro` prints `family=unicore confidence=high score=100 ... reason=unicore_ascii:+100`.

**Evidence:** Two focused public-API repros and scoring trace.

**Minimal proposed fix:** Require valid integrity where the record provides it and semantic plausibility/verified response; aggregate multiple independent observations before high confidence.

**Regression test that should be added:** Supported names with absent/bad CRC, random logs, partial lines, mixed binary, real valid Unicore, and receiver response probes.

**Compatibility/public-behaviour impact:** Noisy inputs previously accepted become unknown/low confidence.

**ROS2 impact:** Receiver auto-family/device discovery becomes safer.

**MowgliNext downstream impact, if any:** Robot startup should surface inconclusive discovery rather than selecting Unicore; validate noisy UART/shared-port cases.

**Related existing PR:** None found.

**Possible false-positive considerations:** Some Unicore outputs may omit CRC in specific modes, but one unverified name should not be sufficient for high confidence; such modes need separately documented evidence.

### UGA-026 — serial “raw mode” inherits stop-bit and flow-control flags

- **ID:** UGA-026
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** POSIX serial transport
- **File(s):** `gnss_transport/src/posix_serial_transport.cpp`
- **Function(s):** `ConfigureRawMode`
- **Relevant line(s):** 53-62
- **Category:** serial configuration / inherited OS state

**Observed behaviour:** Opening a tty whose prior settings contain `CSTOPB`, `CRTSCTS`, `IXOFF`, or `IXANY` leaves all those flags enabled. Only `IXON` is cleared. The resulting link may be 8N2, hardware-flow-controlled, and/or software-flow-controlled rather than the intended portable GNSS raw 8N1 behavior.

**Expected behaviour:** The transport must deterministically configure documented framing and flow control independent of the tty's previous owner.

**Root cause:** `ConfigureRawMode` clears an incomplete subset of `c_iflag/c_cflag`.

**Why this is a real bug:** Termios state persists and pseudo-/real ttys can inherit arbitrary settings. GNSS receivers commonly use 8N1 without flow control.

**User/runtime impact:** No data, intermittent stalls on XOFF-valued binary bytes, or framing mismatch, varying with host history.

**Reproduction:** `/tmp/universal-gnss-audit-serial-raw-repro` preloads the flags on a pty, opens the transport, and prints `CSTOPB=1 IXOFF=1 IXANY=1 CRTSCTS=1`.

**Evidence:** Focused pty/termios repro and source mask.

**Minimal proposed fix:** Clear `CSTOPB`, `CRTSCTS` where available, `IXON|IXOFF|IXANY`, and other documented raw flags; explicitly set 8N1/no-flow-control or expose validated options.

**Regression test that should be added:** Seed every relevant termios flag, open, then assert the exact final configuration.

**Compatibility/public-behaviour impact:** Deterministic serial framing; users relying on inherited flow control would need explicit supported configuration.

**ROS2 impact:** Serial receiver and discovery reliability.

**MowgliNext downstream impact, if any:** Revalidate UART/USB serial on the robot and document any receiver requiring flow control.

**Related existing PR:** None found.

**Possible false-positive considerations:** Fresh ttys often default to compatible flags, which masks the defect; the transport cannot rely on that external state.

### UGA-027 — serial timeout conversion wraps instead of rejecting/clamping

- **ID:** UGA-027
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** POSIX serial transport
- **File(s):** `gnss_transport/src/posix_serial_transport.cpp`
- **Function(s):** `ConfigureSerialPort`
- **Relevant line(s):** 85-100
- **Category:** timeout conversion / integer narrowing

**Observed behaviour:** Milliseconds are rounded in `uint32_t`, cast to `cc_t`, then conditionally replaced with 1 only if the wrapped result is zero. On this platform a requested 25,600 ms becomes `VTIME=1` (100 ms); other values wrap periodically. `read_timeout_ms + 99` also overflows near `UINT32_MAX`.

**Expected behaviour:** Values outside POSIX `VTIME`'s representable range must be rejected, clamped/documented, or implemented with `poll`/deadline logic without narrowing.

**Root cause:** Narrowing occurs before range validation and arithmetic is not overflow-safe.

**Why this is a real bug:** The public field is 32-bit milliseconds while `VTIME` is usually an 8-bit decisecond field.

**User/runtime impact:** Reads return hundreds of times earlier than configured, causing false timeouts, busy polling, or broken discovery/config response timing.

**Reproduction:** `/tmp/universal-gnss-audit-serial-timeout-repro` opens a pty with 25,600 ms and prints `VMIN=0 VTIME_ds=1`.

**Evidence:** Focused termios repro and exact arithmetic.

**Minimal proposed fix:** Validate before conversion and use `poll/ppoll` plus monotonic deadline for the full `uint32_t` range; at minimum cap at 255 deciseconds explicitly.

**Regression test that should be added:** 0, 1, 99, 100, 25,500, 25,600, 25,700, and `UINT32_MAX`, including elapsed-time tolerance.

**Compatibility/public-behaviour impact:** Invalid/extreme values become explicit errors or gain accurate behavior.

**ROS2 impact:** Discovery and serial read timeout behavior.

**MowgliNext downstream impact, if any:** Validate configured probe/read timeouts on the robot.

**Related existing PR:** None found.

**Possible false-positive considerations:** Current ROS defaults are small, but the public transport accepts the full type and no range contract warns callers.

### UGA-028 — adopted socket writes can terminate the process with SIGPIPE

- **ID:** UGA-028
- **Severity:** P2 Medium
- **Confidence:** Confirmed
- **Component:** TCP transport adopted-socket path / NTRIP tests and integrations
- **File(s):** `gnss_transport/src/tcp_client_transport.cpp`
- **Function(s):** `AdoptConnectedSocket`, `TcpClientTransport::Write`
- **Relevant line(s):** 321-343, 442-457
- **Category:** signal safety / process termination

**Observed behaviour:** Normal connected sockets use `send(..., MSG_NOSIGNAL)`, but adopted descriptors set `use_generic_fd_io_=true` and use plain `write`. Writing after the peer closes delivers SIGPIPE with the default disposition and terminates the process before an error result is returned.

**Expected behaviour:** All socket write paths must suppress SIGPIPE locally and return `kWriteFailure`/disconnected state.

**Root cause:** The adopted path switches to generic fd I/O even though the public API is specifically `AdoptConnectedSocket`.

**Why this is a real bug:** SIGPIPE is standard POSIX socket behavior. The two construction paths expose different fatality semantics for the same class.

**User/runtime impact:** Whole node/tool process termination on a routine peer disconnect.

**Reproduction:** `/tmp/universal-gnss-audit-sigpipe-repro` restores default SIGPIPE, adopts one side of a socket pair, closes the peer, calls `Write`, and exits with status 141 (signal 13).

**Evidence:** Focused process-level repro and branch trace.

**Minimal proposed fix:** Use `send(MSG_NOSIGNAL)` for adopted sockets after verifying socket type, or set per-socket no-SIGPIPE where supported; preserve a truly generic fd path separately if required.

**Regression test that should be added:** Fork/subprocess test for closed peer on opened and adopted sockets; assert no signal and correct error/metrics.

**Compatibility/public-behaviour impact:** Fatal termination becomes a returned error.

**ROS2 impact:** Adopted-socket NtripNode tests/integrations and any public transport user.

**MowgliNext downstream impact, if any:** Prevents correction-client process loss; field-test caster disconnects.

**Related existing PR:** None found.

**Possible false-positive considerations:** Frameworks sometimes ignore SIGPIPE process-wide, but this library neither requires nor enforces that global policy and its normal socket path already acknowledges the risk.

## P3 Low findings

### UGA-029 — malformed HTTP status codes with a `200` prefix are accepted

- **ID:** UGA-029
- **Severity:** P3 Low
- **Confidence:** Confirmed
- **Component:** NTRIP HTTP/ICY response parser
- **File(s):** `gnss_ntrip/src/ntrip_client.cpp`
- **Function(s):** `IsAcceptedNtripStatusLine`, `ParseNtripResponseStatus`
- **Relevant line(s):** 84-112
- **Category:** protocol grammar validation

**Observed behaviour:** Prefix checks accept `HTTP/1.1 2000 ...`, `HTTP/1.0 200X ...`, and `ICY 200anything` as success because no status-token boundary is checked.

**Expected behaviour:** Parse an exact three-digit status token equal to 200 followed by a legal delimiter/end.

**Root cause:** `StartsWith("... 200")` substitutes for status-line parsing.

**Why this is a real bug:** Malformed/nonstandard responses enter `Streaming` rather than failing protocol validation, compounding UGA-005.

**User/runtime impact:** Misclassified caster/proxy responses and a stuck no-data streaming state. Real compliant casters are unaffected.

**Reproduction:** Adopt a socket, send `HTTP/1.1 2000 Not OK\r\n\r\n`, and observe accepted response/`Streaming`.

**Evidence:** Exact prefix predicate and response transition at `HandleResponseBytes` lines 889-900.

**Minimal proposed fix:** Tokenize the status line and require exact protocol/version plus exactly three numeric digits and code 200 (while preserving documented ICY compatibility).

**Regression test that should be added:** 200, 2000, 200X, 20, whitespace variants, 401, and valid ICY forms.

**Compatibility/public-behaviour impact:** Only malformed responses are newly rejected.

**ROS2 impact:** NtripNode connection diagnostics become accurate sooner.

**MowgliNext downstream impact, if any:** None beyond clearer connection failure reporting.

**Related existing PR:** None found.

**Possible false-positive considerations:** No valid HTTP status code is four digits; permissiveness has no interoperability benefit for compliant servers.

## Hardening opportunities (not counted as defects)

- Document `TcpClientConfig::connect_timeout_ms == 0`. It currently performs a blocking `connect` before post-connect nonblocking configuration. The code is internally consistent with “zero means no timeout,” but the interaction with `nonblocking=true` is surprising and needs an explicit contract.
- Validate future/out-of-order timestamps centrally even after UGA-014 separates clock domains. Saturating subtraction and a documented tolerance would prevent negative ages from being interpreted as fresh.
- Add checked/saturating arithmetic for discovery timeout multipliers, duration conversions, metric counters, and replay wall-time scaling. No reachable default-configuration failure was demonstrated, so these were not promoted.
- Decide and document calendar validity for two-digit RMC dates. The parser rejects day 0/>31 and month 0/>12 but accepts combinations such as 31 February, whereas ZDA has stronger calendar checks.
- Define behavior for `NtripClient::set_config` and `set_tcp_config` while connected. Prefer rejecting mutation or forcing a source transition; today callers can create a socket/request configuration mismatch.
- Bound all public buffer/chunk/header parameters, not only values currently surfaced through ROS. Resource limits should be part of the public transport/protocol contract.
- Consider whole-frame queue watermarks and explicit drop counters for every streaming edge, even after fixing receiver RTCM writes.
- Preserve the conservative generic-NMEA invariant: GGA RTK quality should populate `rtk_mode`, while generic `fix_type` remains `Fix/NoFix` unless a stronger cross-sentence/vendor guarantee exists.
- Preserve coordinate precision. No forced narrowing of canonical latitude/longitude was found in the audited core/ROS conversion path; future changes should keep the existing high-precision formatting/tests.

## Test coverage gaps

The suite is broad but often tests implementation constants rather than external truth. The following missing regression families would have caught most findings:

- A sanitizer CI lane. Existing valid NMEA tests immediately expose UGA-001 under ASan.
- Generic split-at-every-byte, concatenate, garbage-between, truncated-prefix, corrupt-length, and suffix-resynchronization meta-tests for every framer.
- NaN/Inf/overflow and physical-range matrices for every ASCII numeric field, plus valid-JSON assertions for every exporter.
- Distinct tests for omitted partial fields versus explicit clear/invalidation in the runtime aggregator.
- Sustained producer/consumer throughput tests with short reads and a measurable latency/backlog budget, crossing independent receiver and publish rates.
- ROS signed-parameter boundary tests for every integer later stored in unsigned/`size_t` fields.
- NTRIP first-header/no-frame, frame-then-silence, legitimate-gap, reconnect-threshold, and reconnect-backoff sequences.
- RTCM health tests requiring successful specialized decode, source/station coherence, static retention, dynamic expiry, and future/out-of-order timestamps.
- Long-duration monitor tests asserting bounded storage/query time.
- Partial write/EAGAIN tests at every byte offset and subprocess SIGPIPE tests.
- Auto-discovery negative corpora: supported names with absent/bad CRC, noisy logs, mixed streams, wrong baud leftovers, and partial responses.
- Golden receiver-command tests sourced from vendor manual examples, including failure/reboot at every command and post-reboot continuation.
- Multi-host/sim-time/bag-replay ROS timestamp tests; current same-process tests mask clock-origin errors.
- Clang build/test parity and a true LeakSanitizer/Valgrind run in an environment that permits them.

Some existing tests encode unsafe assumptions:

- `TestExistingValuesSurviveUpdatesWithoutValueFlags` correctly covers omitted partial updates but, without a distinct clear representation, reinforces UGA-021.
- RTCM requirement tests based on type presence do not prove semantic decode validity and therefore miss UGA-008.
- Unicore golden strings mirror `config_profiles.md`, which itself mirrors the incorrect builder grammar instead of vendor truth.
- Same-process ROS tests make monotonic values appear mutually compatible and do not enforce public ROS timestamp semantics.

## Documentation inconsistencies

- `docs/vendors/unicore/config_profiles.md` lines 132-174 documents `LOG GPGGA ONTIME` and `LOG PVTSLNA ONTIME`; the local vendor manual documents different N4 grammar (UGA-010).
- The same file lines 256-263 calls `CONFIG SIGNALGROUP` an ordinary runtime command and discusses only selection validation, omitting the vendor reboot/save-order warning and apply recovery requirement (UGA-011).
- The planner calls unknown-model plans a “safe generic non-baseline fallback” and marks them production-ready even though most commands remain guessed (UGA-012).
- `ntrip_streaming` says “correction stream is active” when only an accepted header/open TCP stream is known (UGA-005), and forwarding statuses use “active” for lifetime history (UGA-019).
- ROS message comments do not define the clock domain of `RtcmFrame.stamp`; implementation uses incompatible domains (UGA-014).
- The receiver source comment correctly documents one-read-per-publish arithmetic, but user documentation does not constrain unsafe `publish_rate_hz`/`read_chunk_size` combinations or disclose that publication rate controls input drain (UGA-003).

## Needs hardware/vendor validation

These items are not additional confirmed defect counts. They require real receivers/casters to finalize policy or confirm exact behavior:

| Area | What is established in code/manual | Hardware/vendor validation still needed |
|---|---|---|
| Unicore GPGGA/PVTSLNA output grammar | Current commands conflict with the local N4 manual | Confirm response/error and output behavior on UM960/980/981/982/UB9A0 firmware builds; check for undocumented aliases |
| Unicore `SIGNALGROUP` | Manual warns of reboot/save ordering; current apply has no recovery | Measure reboot timing, active port/baud behavior, response timing, and retained/cleared settings on each supported model |
| RTK timeout defaults | Code forces 10 s for every model; N4 manual lists model-dependent defaults/ranges (including UM980 120 s and UM982 600 s in the referenced manual) | Determine portable model/profile defaults from real correction-gap behavior; do not adopt the application-specific UM980 UAV mode from a downstream PR into core |
| DGPS timeout | Code forces 600 s while the manual's documented default is 300 s (both within range) | Establish whether the portable profile intentionally needs 600 s per model/use case |
| Output rate limits | Zero-period rounding is proven; complete per-message limits are not encoded | Confirm legal periods, actual cadence, load, and firmware-specific restrictions for every output |
| RTCM 1230/static cadence | Universal requirement and 30 s static expiry are incorrect semantically | Capture representative non-GLONASS, GLONASS, VRS, and sparse-static casters to choose defaults and identity transitions |
| Same-source metadata retention | Current reset erases it; unconditional retention is unsafe | Verify endpoint/station stability across caster reconnects, mountpoint changes, and VRS station-ID reassignment |
| Silent-stream reconnect thresholds | No reconnect exists; a single 5 s action threshold is unsafe | Measure legitimate caster gaps and recovery requirements; set separate diagnostic, availability, first-frame, and forced-reconnect thresholds |
| Serial raw/partial writes | POSIX behavior is proven on ptys | Validate USB UARTs, platform UARTs, real receiver flow control, and induced backpressure/disconnects |
| High-rate/long-run operation | Mathematical bound and unbounded monitor history are proven | Multi-hour robot soak with actual high-rate UBX/Unicore plus RTCM, recording kernel backlog, latency, CPU, and memory |

## Known PR and fork verdicts

Fork histories were compared by commit/merge-base in a temporary bare repository. Inherited/rebased changes were separated from genuinely novel claims; no patch was cherry-picked.

| Source | Independent verdict against audited commit | Notes |
|---|---|---|
| Pepeuch PR #1 — configurable 64 KiB receiver reads | **Confirmed underlying bug; patch incomplete as a general invariant** | 512 B was insufficient and 64 KiB helps normal rates, but one-read-per-publish remains mathematically unsafe (UGA-003) and timestamps still hide backlog (UGA-002). |
| Pepeuch PR #2 — Unicore signal groups | **Core model-gating concern confirmed; current audited tree already contains much of it** | Local vendor mapping supports documented model-specific groups. Unknown models still receive all other guessed commands (UGA-012), and reboot sequencing remains absent (UGA-011). |
| Pepeuch PR #4 — make RTCM 1230 optional | **Confirmed** | The audited commit unconditionally requires 1230 (UGA-015). The patch is present in later public `main`, not the requested local HEAD. |
| Pepeuch PR #5 — retain static RTCM metadata | **Symptom confirmed; proposed direction only partially safe** | Current same-source reset is too aggressive, but retention without endpoint/station ownership can leak source A into B (UGA-018). |
| mowglinext PR #1 — Unicore receiver configuration | **Partially confirmed** | Novel model/default/timeout concerns deserve hardware validation; partial-write risk is real. Proposed UM980 UAV mode is downstream/application-specific and must not become a Universal GNSS core default. Diff also contains inherited changes. |
| mowglinext PR #2 — derive stale timeout from rate | **Failure class confirmed; formula only partially accepted** | Fixed 3 s is incompatible with slow observations (UGA-020), but ROS publish rate is not necessarily receiver observation rate, and true timestamps are missing (UGA-002/007). |
| lukaskrnac comparison — GGA RTK propagation | **Current upstream semantic preferred** | Audited code already maps GGA quality to `rtk_mode`. Assigning generic `fix_type=kRtk*` is not automatically preferable; conservative `Fix/NoFix` avoids overstating generic NMEA semantics. |
| pbatsa stale-reconnect branches | **Failure confirmed; proposed universal 5 s reconnect not accepted as complete** | Open `Streaming` sockets can remain silent forever (UGA-005). Diagnostic stale, correction availability, first-frame deadline, and forced reconnect need separate thresholds; backoff and metadata identity also interact (UGA-006/018). |

## Layer-by-layer final validation matrix

| Layer / pass | Validation performed | Result and residual risk |
|---|---|---|
| `gnss_core` runtime/state | Manual field/version/flag audit; focused clear repro; normal/UBSan tests | Explicit invalidation is lost (UGA-021). Capability/value invariant helpers are otherwise internally consistent. |
| NMEA | Framing/parser/tests, malformed chaining, special floats, date/time/range mapping, ASan/cppcheck | UB, NaN acceptance, and resync loss confirmed (UGA-001/022/023). GGA `fix_quality -> rtk_mode` and conservative fix type are appropriate. |
| UBX | Framing/checksum/length/resync, representative NAV/MON/RXM/ACK tests, endian/scaling inspection | Corrupt length can swallow next frame (UGA-024). No additional concrete endian/layout defect was proven. |
| Unicore ASCII | CRC/numeric/status/runtime/config response inspection; CRC-correct NaN repro | Non-finite data accepted (UGA-022). Runtime mappings otherwise had no independently proven additional defect. |
| Unicore binary N4 | Header/payload/CRC/endian/layout/resync inspection and existing tests | Same corrupt-length recovery weakness (UGA-024); no additional concrete scaling/endian defect proven. |
| RTCM3 parser/monitor | CRC, 1005/1006, 1230, MSM classification/bit-length tests; malformed semantic, freshness, long-run repros | False semantic availability, requirement/lifetime/identity, clock, and unbounded-history defects (UGA-008, 014-018). |
| receiver sessions/driver | Session selection, aggregation, runner, discovery, command/transaction paths | Missing timestamp provenance and false Unicore discovery (UGA-002/025). |
| serial transport | EINTR/EAGAIN/read/write/open/close review; pty termios repros | Inherited flow/framing and timeout wrap confirmed (UGA-026/027). |
| TCP transport | connect/adopt/read/write/peer-close/partial behavior; socket-pair repro | Adopted write SIGPIPE confirmed (UGA-028). Zero connect-timeout semantics remain a documented hardening issue. |
| NTRIP | HTTP/ICY, partial headers, payload in header read, auth, reconnect/backoff, GGA, silence | Liveness/backoff/status grammar/current-vs-ever failures (UGA-005/006/019/029); first payload in header read is handled. |
| receiver configuration | All family/profile/apply-mode combinations, rate/model/port/safety ordering, local vendor manual | Four P1 Unicore plan/apply defects (UGA-010-013); model defaults need hardware policy. `runtime_only` remains a no-command path as required. |
| ROS2 ReceiverNode | parameters, timers, freshness, fix/status/diagnostics, RTCM forwarding, QoS/concurrency paths | UGA-003/004/007/009/014/019/020/021. Normal ROS test suite passes but misses adversarial boundaries. |
| ROS2 NtripNode | parameters, state/reconnect, GGA, correction health/forwarding | UGA-005-008, 014-019, 029. Same callback group avoids a proven data race in normal construction. |
| ROS2 ReplayNode | timing/stamps, parser feed, publishing/diagnostics, existing tests | No additional counted defect; relative/ROS/steady stamp policy must be resolved with UGA-014, and extreme scale arithmetic needs hardening. |
| tools | config plan/apply, replay/export/inspect/monitor, CLI parsing | Tools expose configuration and invalid-JSON failures above; normal tool tests pass. |
| build/CMake/examples | target inventory, Debug/Release/sanitizer/ROS builds, warning/static-analysis pass | GCC builds are healthy. Clang parity unavailable. No production build-system defect proven. |
| docs/vendor docs | README/TODO/ROADMAP/ROS/vendor cross-check | Operator-relevant inconsistencies listed above; authoritative N4 manual conflicts with generated commands. |
| long-run/liveness | 2M RTCM observations, silent NTRIP socket, repeated post-connect failures | Unbounded monitor storage, missing reconnect, and defeated backoff independently confirmed. |

## Mandatory final architectural cross-check

| Producer assumption | Consumer guarantee actually available | Consequence |
|---|---|---|
| Receiver publisher assumes `now` is an acceptable missing observation timestamp | Parser/runner never established when queued bytes were received | Delayed data appears fresh (UGA-002). |
| Receiver timer assumes one large read drains input | POSIX/TCP only guarantee a short read up to capacity | Backlog grows at valid parameter combinations (UGA-003). |
| NtripNode assumes a status callback represents new GNSS data | ReceiverNode republishes unchanged state every timer | Stale GGA remains eligible (UGA-007). |
| NTRIP state assumes accepted header means active corrections | TCP/HTTP guarantees no future payload | Silent stream never reconnects (UGA-005). |
| Reconnect policy assumes TCP connect is success | Authentication/header/RTCM can fail later | Backoff resets on every failed application attempt (UGA-006). |
| RTCM health assumes CRC-valid type presence supplies semantics | Specialized decoders can reject/truncate the payload | Malformed corrections report available (UGA-008). |
| RTCM health assumes all required types share one lifetime | ARP metadata is static while MSM is dynamic | False periodic outages and unsafe retention pressure (UGA-016/018). |
| ROS RTCM consumer assumes message stamp can be compared to steady time | Public producers use ROS/system/sim/relative clocks | Negative or nonsensical freshness (UGA-014). |
| Config apply assumes every runtime command preserves the connection | Vendor says `SIGNALGROUP` can reboot | Later outputs/save are skipped (UGA-011). |
| Planner assumes generic N4 commands are safe on unknown models | Model/firmware applicability is not guaranteed | Guessed production-ready mutation (UGA-012). |

## Areas not fully validated

- No physical GNSS receiver, correction caster, robot, serial adapter, or multi-host ROS deployment was available. Items in the hardware table remain explicitly pending.
- No live authenticated Internet caster was exercised; NTRIP socket-pair/adopted-socket tests validated the state machine deterministically without external side effects.
- Clang, clang-tidy, scan-build, Valgrind, and a functional LeakSanitizer environment were unavailable. GCC ASan (without leak detection), UBSan, cppcheck, Debug, Release, and ROS2 were completed.
- The audit was adversarial and repository-wide but not an exhaustive fuzzer or formal proof of every byte combination. The report identifies where property/fuzz coverage is still required.
- Local vendor PDFs were used as authoritative requested. Firmware-specific deviations and undocumented aliases require hardware/vendor confirmation.
- Later public upstream commit `aaacc6be...` was inspected only to understand PR state; it was not substituted for or merged into the requested audited commit.

## Recommended triage order (no fixes performed)

1. Stop-the-line correctness: UGA-001, 002, 004, 008, 009, 014.
2. Correction liveness/state: UGA-005-007, 015-019, 029.
3. Receiver provisioning safety: UGA-010-013, then hardware-validate defaults.
4. Streaming/long-run resilience: UGA-003, 017, 023, 024, 026-028.
5. Runtime semantic cleanup and rate policy: UGA-020-022, 025.

Fixes should be separated by the AGENTS.md planning buckets: Universal GNSS core/protocol/transport, receiver-specific backend, ROS2 package, and downstream MowgliNext integration. Do not introduce Mowgli-specific assumptions into core fixes.

## Audit handoff state

- **Files modified:** `UNIVERSAL_GNSS_AUDIT.md` only.
- **Why changed:** Added the requested evidence-backed first-pass repository audit and validation matrix.
- **Tests executed:** GCC Debug/Release/ASan+UBSan/UBSan CTest; ROS2 Kilted colcon tests; cppcheck; focused parser/state/transport/NTRIP/RTCM/config reproducers; long-run RTCM monitor repro.
- **Validation performed:** Source/manual cross-checks, external PR/fork lineage review, cross-layer invariant pass, sanitizer/static analysis, exact CLI plan inspection, and worktree integrity checks.
- **Remaining known limitations:** Hardware/caster/multi-host validation, Clang-family analysis, leak checking, and exhaustive fuzz/property testing as detailed above.
- **Commit status:** Report is uncommitted; no commit was requested or created.
- **Push status:** Not pushed; no push was requested or performed.
