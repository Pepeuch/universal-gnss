# Current-main audit delta: UGA-001 through UGA-007

Revalidation date: 2026-08-27 UTC

Production base: `aaacc6be92463ad493d6f4260426cf645188078f`

Checked-out `review` HEAD: `483a717b9a0fe1c7609d173fb1a8931cd2dfc1d9` (exactly one report-only commit above the production base, adding `UNIVERSAL_GNSS_AUDIT.md`)

Comparison base for the old audit: `804bed8d7f753f6834212cfbc9dc329f88360299`

The staged revalidation below covers UGA-001 through UGA-029. The fix continuations modify only UGA-001, UGA-002, UGA-005, UGA-006, UGA-007, UGA-009, UGA-014, UGA-016, UGA-018, UGA-022, UGA-023, UGA-026, and UGA-027; no other audit finding was changed.

### UGA-001 — NMEA header parsing uses a dangling `string_view`

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `NmeaSentenceFramer::BuildSentence` now creates `payload_text` as a `std::string_view` over the owning `sentence.payload_text`, then derives `header` with `string_view::substr` (`gnss_protocols/src/nmea_framer.cpp`). No view refers to a temporary-owned `std::string`; header extraction still produces the same talker and sentence type.

Files changed: `gnss_protocols/src/nmea_framer.cpp`, `gnss_protocols/tests/test_protocol_foundation.cpp`, `TODO.md`, and this delta report.

Regression test added: `TestNmeaFramerHeaderExtractionOwnsViewedStorage`.

Pre-fix failure evidence: The targeted normal test failed its talker/message assertions; the ASan+UBSan execution exited 1 with `AddressSanitizer: stack-use-after-scope` in `NmeaSentenceFramer::BuildSentence`.

Production fix: View the owning `sentence.payload_text` first, then derive the header with `std::string_view::substr`.

Post-fix validation: The foundation and NMEA parser tests pass normally.

Sanitizer result: The affected foundation test passes under ASan+UBSan with no diagnostic.

Relevant changes since old audit: The old-to-current production-base comparison had no change in this path. This fix changes only `gnss_protocols/src/nmea_framer.cpp` and adds focused coverage in `gnss_protocols/tests/test_protocol_foundation.cpp`.

Reproduction/test result: Added `TestNmeaFramerHeaderExtractionOwnsViewedStorage`, covering a normal short/SSO `GPGGA` header and a long proprietary-style header while asserting talker, message type, and checksum. Before the production fix, the targeted test failed its talker/message assertions and its ASan+UBSan run exited 1 with `AddressSanitizer: stack-use-after-scope` at `BuildSentence` line 104. After the fix, `gnss_protocols_test_foundation` passed normally and under `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; `gnss_protocols_test_nmea_parser` also passed. No sanitizer diagnostic remained.

Remaining defect, if any: None for the temporary-backed header-view lifetime. This test intentionally covers framer extraction rather than imposing a new NMEA header grammar.

Recommended next action: Keep the focused foundation test in the normal and sanitizer test lanes.

### UGA-002 — live acquisition timestamps are lost and publish time is substituted

Status: **FIXED**

Confidence: **Confirmed**

Old defect: Live reads reached `ReceiverSession::FeedBytes` without a timestamp, so parsed runtime state had no acquisition provenance and `ReceiverNode::PublishNow` substituted publication-time `owner_.now()`.

Pre-fix reproduction: The new receiver-runner regression failed two assertions because the first accepted GGA had no timestamp and a following GGA could not have a newer timestamp. `ReceiverNodeTest.PublishesStableReceiptProvenanceInsteadOfPublicationTime` then showed the public stamp fell about 50 ms after the receipt window and changed again when the cached state was republished.

Temporal contract applied: `ReceiverSessionRunner::StepOnce` captures one immutable local receipt timestamp immediately after each successful non-empty transport read and supplies it to the existing session/parser buffering path. The portable default is `steady_clock`; ReceiverNode supplies ROS time captured at the same receipt boundary for the public runtime stamp and separately retains the paired `steady_clock` time for local freshness. Observations decoded from one read may share that local receipt time; it is explicitly not a receiver-provided GNSS measurement epoch or publication time. Before any observation has been accepted, public observation provenance remains explicitly unavailable/zero rather than being synthesized from publication time.

Production files changed: `gnss_driver/include/universal_gnss_driver/receiver_session_runner.hpp`, `gnss_driver/src/receiver_session_runner.cpp`, `gnss_ros2/src/receiver_node.cpp`, and the timestamp contract comment in `gnss_ros2/msg/GnssStatus.msg`.

Post-fix validation: The focused runner binary passes, including receipt presence, no mutation without new input, newer provenance for a following observation, fragmented input, and batched identical fixes. The targeted ReceiverNode provenance test passes, including the final no-observation/zero-provenance assertion. Complete `gnss_driver` CTest passed 19/19, complete non-ROS CTest passed 61/61, and complete ROS2 CTest passed 6/6; ReceiverNode contributed 36/36 gtests. The two touched non-ROS binaries also passed ASan+UBSan with leak detection disabled for the sandbox and passed standalone UBSan.

Public/API compatibility impact: `ReceiverSessionRunnerConfig` gains an additive receipt-timestamp provider at the end of the configuration struct. `GnssStatus.stamp` now has explicit local-receipt-in-ROS-time semantics instead of receiving a later publication fallback. This is a corrected timestamp contract; it does not claim GNSS measurement time.

Remaining limitation: Byte-level arrival times inside one read are unknowable in the current transport API, so records beginning in the same read can share receipt provenance. No hardware validation is required for the software lifetime/propagation invariant.

Recommended next action: Keep the runner and delayed-publication regressions in normal and sanitizer lanes.

### UGA-003 — receiver drain throughput is coupled to ROS publish cadence

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ros2/src/receiver_node.cpp` current lines 783-794 still creates one timer from `publish_rate_hz`, and `OnTimer()` still calls exactly one `StepOnce()` before `PublishNow()`. `gnss_driver/src/receiver_session_runner.cpp` current lines 27-47 still performs exactly one source `Read` per `StepOnce`. The source comment at ReceiverNode lines 71-76 still explicitly states the resulting `read_chunk_size * publish_rate_hz` bound.

Relevant changes since old audit: The focused old-to-current diff for ReceiverNode, runner, and their tests contains no throughput/drain-loop change. No intervening path commit changes `OnTimer` or `StepOnce`; the 64 KiB mitigation predates the old audit and remains the only mitigation.

Reproduction/test result: A focused current-library runner scenario used 24 pending bytes and `read_chunk_size=8`, then called the same single `StepOnce` performed by one ROS timer. `/tmp/universal-gnss-current-uga003-repro` returned `reads=1 bytes=8 remaining=16`. At the accepted `publish_rate_hz=0.1` and default 65,536-byte chunk, the request-capacity ceiling remains 6,553.6 B/s versus the source comment's approximately 13,000 B/s measured producer, a 6,446.4 B/s deficit before considering short reads.

Remaining defect, if any: Input draining is still scheduled by publication cadence; legal parameter combinations can build an unobservable transport backlog even though individual reads and parses succeed.

Recommended next action: In a separate implementation task, decouple bounded readiness-driven draining from publication and add sustained-rate/backlog-latency regression coverage across independent input and publish rates.

### UGA-004 — negative ROS `read_chunk_size` becomes a huge allocation

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `LoadReceiverNodeConfig` now retains the declared signed `read_chunk_size` until it has checked the explicit `1..1048576` range (`gnss_ros2/src/receiver_node.cpp`), then converts the validated value to `size_t`. `ReceiverSessionRunner` receives only this bounded node configuration, so its buffer cannot be reached with a signed-wrap value from the public ROS parameter.

Relevant changes since old audit: The production-base interval had no `read_chunk_size` validation or allocation change. This fix changes only the ReceiverNode parameter loader and its ROS2 test.

Files changed: `gnss_ros2/src/receiver_node.cpp`, `gnss_ros2/tests/test_receiver_node.cpp`, and this delta report.

Regression test added: `ReceiverNodeTest.ValidatesReadChunkSizeBeforeConversionToSizeT`.

Pre-fix failure evidence: The ReceiverNode test expected construction to reject `-1`, `INT64_MIN`, `0`, 1 MiB + 1, and `INT64_MAX`, but the current loader accepted them all and cast them to `size_t`; the full ReceiverNode CTest therefore failed its new regression.

Production fix: Validate the signed ROS parameter before conversion, requiring `1..1048576` bytes (the existing 65536-byte default remains valid).

Post-fix validation: 1, 65536, and exactly 1 MiB construct successfully. All negative/zero/over-limit values throw the existing `std::invalid_argument` path before runner allocation. ROS2 `test_receiver_node` passed 1/1 (30 gtests) and complete non-ROS CTest passed 61/61.

Sanitizer result: Not separately run; the regression proves parameter-time rejection without allocating a read buffer, and the requested ROS2/non-ROS suites passed.

Remaining defect, if any: None for public ROS `read_chunk_size` conversion/allocation. Direct construction of `ReceiverSessionRunner` remains a lower-level API and is intentionally unchanged by this ROS parameter-validation fix.

Recommended next action: Keep the signed boundary coverage with the ReceiverNode parameter tests if the default or upper bound changes.

### UGA-005 — NTRIP `Streaming` has no correction-flow liveness transition

Status: **FIXED**

Confidence: **Confirmed**

Pre-fix reproduction: `gnss_ntrip_test_ntrip_client` failed three focused assertions: an accepted response with no first RTCM frame never timed out, one CRC-valid frame followed by silence never timed out, and arbitrary junk bytes kept the client in `Streaming`. The additional fragmented-frame and slow-valid-stream scenarios established the required boundary: only a complete CRC-valid frame is progress, while valid frames below the configured inter-frame deadline remain live.

Shared state/liveness contract: TCP connectivity, accepted NTRIP response, recent complete CRC-valid RTCM flow, and semantic correction health are independent. A successful HTTP/ICY header still establishes `Streaming`, but it starts a first-frame deadline. Each complete CRC-valid RTCM frame refreshes the separate inter-frame deadline; partial frames and arbitrary bytes do not. Defaults are 30 seconds for each deadline, zero disables the corresponding deadline, and ROS2 exposes `correction_first_frame_timeout_s` and `correction_inter_frame_timeout_s`. Expiry closes the transport, enters `kFailed`, and uses the existing reconnect path. Deadline decisions use only the client's internal `steady_clock`; optional caller timestamps remain observable provenance/metrics and cannot disable or spuriously advance liveness.

Production fix: `NtripClient` now owns an explicit `NtripCorrectionFlowState`, first/inter-frame monotonic deadlines, and `IsCorrectionFlowing`; `Read` transitions silent flows through `FailWith(kTimeout)`. `NtripNode` reports `ntrip_streaming`, `correction_stream_waiting`, `correction_flowing`, and semantic correction health separately. UGA-019 forwarding/publication activity remains a different diagnostic and does not drive reconnect.

Post-fix validation: Focused NTRIP client, reconnect-policy, and RTCM-monitor CTests passed 3/3. The ROS integration regression `SilentAcceptedStreamTimesOutAndEntersReconnectState` passed, including the waiting-to-reconnecting transition. Complete `gnss_ntrip` passed 7/7, `gnss_protocols` 18/18, non-ROS CTest 61/61, and affected ROS2 CTest 6/6. The four touched non-ROS test binaries passed 4/4 under combined ASan+UBSan and 4/4 under standalone UBSan.

Compatibility impact and remaining limitation: `NtripConfig` gains additive liveness fields and NtripNode gains three parameters; dependent C++ code must be rebuilt. Correction-flow progress intentionally means integrity-valid framing, not semantic validity: CRC-valid but semantically malformed RTCM remains UGA-008 and can prove flow without proving semantic health. Validate deployed timeout values against each caster's real cadence.

Recommended next action: Keep the header-only, post-frame silence, junk, fragmentation, low-rate, omitted-timestamp, and ROS reconnect scenarios in continuous testing; field-test the selected deadlines against the intended caster.

### UGA-006 — reconnect backoff is reset before NTRIP success

Status: **FIXED**

Confidence: **Confirmed**

Pre-fix reproduction: `gnss_ntrip_test_ntrip_reconnect_policy` failed four focused assertions. Repeated TCP-success/HTTP-failure cycles reset attempts instead of preserving exponential progression; an accepted response with no correction flow reset history; and a one-frame-then-drop stream reset backoff too early. The original three-cycle HTTP-401 reproducer likewise stayed at attempt 1/minimum delay.

Shared operational-success contract: TCP connect and accepted response are progress states, not operational success. Backoff resets only after the accepted response has delivered `operational_min_valid_rtcm_frames` complete CRC-valid frames; the default is two, explicitly preventing connect-one-frame-drop loops from resetting history. ROS2 exposes `correction_operational_min_valid_frames`; its accepted range is 1..1,000,000. Existing initial/max delay, multiplier, max-attempt, and reset-after-success policy remain authoritative.

Production fix: Removed `RecordReconnectSuccess` from raw `Connect`/`AdoptConnectedSocket` success. `NoteCorrectionFlowProgress` declares the session operational and invokes the existing reset policy exactly once when the configured valid-frame threshold is reached. Failures before that point preserve and advance prior failure history; a failure after operational flow starts again from the normal initial delay.

Post-fix validation: The regression now proves exponential progression across TCP+HTTP failures, no reset for accepted/no-flow and one-frame-drop sessions, reset after two valid frames, and initial-delay restart after a later failure. Focused CTests passed 3/3; complete `gnss_ntrip` passed 7/7; non-ROS CTest passed 61/61; ROS2 passed 6/6; ASan+UBSan and standalone UBSan each passed the four affected binaries 4/4.

Compatibility impact and remaining limitation: The default operational milestone is intentionally framing-level, consistent with UGA-005 and separate from UGA-008 semantic health. Deployments may raise the threshold, but setting it excessively high delays backoff reset even on healthy slow streams.

Recommended next action: Keep the failure progression, one-frame-drop, operational reset, max-bound, and post-success failure cases in the reconnect-policy lane.

### UGA-007 — stale receiver state is kept fresh for NTRIP GGA by republishing

Status: **FIXED**

Confidence: **Confirmed**

Old defect: Every `GnssStatus` callback replaced NtripNode's local status receipt time, so ReceiverNode's periodic republication made one cached fix remain eligible for GGA indefinitely.

Pre-fix reproduction: `NtripNodeTest.RepeatedCachedStatusCannotKeepGgaSourceFresh` repeatedly published one fixed status for more than the five-second freshness interval. Before the fix, the final read still began with `$GPGGA`, `gga_source_stale` was absent, and the recovery assertion also failed because the incorrect stale-period send had reset the GGA interval.

Temporal contract applied: The driver sessions now count only accepted position/fix observations: NMEA GGA/RMC, u-blox NAV-PVT or its GGA/RMC paths, and Unicore BESTNAV/PVTSLN/GGA ASCII or binary paths. GSA/GSV/GST, receiver-health, and correction enrichment do not advance this generation. ReceiverNode publishes the generation as `GnssStatus.position_observation_sequence`; NtripNode refreshes its steady-clock GGA source time only when that nonzero generation changes, so identical coordinates remain valid new observations while cached republication does nothing.

Production files changed: the NMEA/u-blox/Unicore/aggregate session metric headers and sources under `gnss_driver`, `gnss_ros2/msg/GnssStatus.msg`, `gnss_ros2/src/receiver_node.cpp`, and `gnss_ros2/src/ntrip_node.cpp`. `MOWGLINEXT_TODO.md` records the required pending downstream projection and stale-policy integration for the new ROS field.

Post-fix validation: The focused repeated-cache test passes: no GGA after expiry, stale diagnostics appear, and a following observation with identical numeric values restores eligibility. The runner regression also proves fragmented GGA counts once, GSV enrichment does not advance provenance, and two identical GGA records in one batch advance it twice. Complete `gnss_driver` passed 19/19, NtripNode passed 10/10 gtests, complete ROS2 CTest passed 6/6, and complete non-ROS CTest passed 61/61. Existing UGA-019 forwarding activity and UGA-020 cadence-aware runtime freshness scenarios remain green in those full ROS suites.

Public/API compatibility impact: `GnssStatus.msg` gains `uint64 position_observation_sequence`, changing the ROS interface type hash/wire layout and requiring downstream ROS packages to rebuild. Zero is reserved for publishers without explicit provenance; NtripNode retains a stamp-based compatibility fallback for those legacy messages.

Remaining limitation: A legacy external publisher that leaves the sequence at zero and rewrites its stamp on every cached publication can still appear new; current Universal GNSS publishers always provide the explicit generation. The GGA freshness duration itself remains the existing five seconds and was not increased.

Recommended next action: Rebuild downstream ROS consumers, preserve the new field through projections, and keep the cached/high-publication-rate regression.

### UGA-008 — semantically malformed RTCM satisfies correction health

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_protocols/src/rtcm_correction_monitor.cpp`, `RtcmCorrectionMonitor::ObserveFrame`, current lines 284-357 still performs the specialized 1005/1006, 1230, and MSM decodes but calls `RecordValidMessage` unconditionally afterward. `RecordValidMessage` at lines 773-809 records framed message-type/constellation activity, and `HasRequiredCorrectionMessages` at lines 590-688 consumes that activity rather than the decoded-valid timestamps. `BuildRtcmCorrectionHealth` at lines 812-917 still defines `parser_healthy` solely as `invalid_frames()==0`, independently emits malformed warnings, and can set `correction_available=true` for those same malformed records.

Relevant changes since old audit: Commit `3495ffb` changed only the portable preset from `require_glonass_bias=true` to false and downgraded a successfully decoded but validity-bit-cleared 1230 diagnostic to informational. It fixes the separate universal-1230 requirement, but does not move activity recording after semantic decode, gate any configured requirement on decode success, or include malformed known records in `parser_healthy`. No other intervening commit touches this path.

Reproduction/test result: `/tmp/universal-gnss-current-uga008-repro`, compiled against the fresh current-base protocol library, supplied checksum-status-valid two-byte payloads containing only each message type. Truncated 1005+1077 and 1006+1077 both printed `arp_decoded=0 msm_decoded=0 required=1 parser_healthy=1 correction_available=1`; all 49 recognized MSM family/variant types printed collectively as `malformed_msm_satisfying=49/49`. A truncated 1230 with explicit `require_glonass_bias=true` printed `decoded=0 malformed=1 required=1 parser_healthy=1 correction_available=1`. The existing current monitor test binary also passed, showing its malformed tests assert diagnostics/counters but not rejection from requirements.

Remaining defect, if any: CRC-valid framing is still treated as required semantic activity even when the implemented decoder rejects the payload. Malformed 1005/1006 and every recognized MSM class still satisfy portable availability; malformed 1230 still satisfies any profile that explicitly requires GLONASS bias. `parser_healthy` and `correction_available` can both remain true despite those decode failures.

Recommended next action: In a separate implementation task, track decoded-valid semantic activity separately from framed-valid activity, base semantic requirements on the former, define how malformed known records affect parser health, and add rejection tests for both base types, all MSM families/variants, and explicitly required 1230.

### UGA-009 — partial nonblocking RTCM writes discard an already-started frame

Status: **FIXED**

Confidence: **Confirmed**

Pre-fix reproduction: Seven focused ReceiverNode scenarios were added before changing production. On the current code, only the ordinary full/short-write case passed; 6/7 failed. The controlled `partial(3) -> WouldBlock -> next frame` sequence emitted exactly the three-byte prefix of frame A followed by all eight bytes of frame B, versus the required 16-byte `A || B`. Zero-progress and multiple-partial cases were never retried by `StepOnce`; a hard error did not close the session; partial bytes were absent from `forwarded_bytes`; and the would-block path was counted as a write error.

Persistent-write invariant: `ReceiverNode` owns a persistent FIFO bounded to the existing RTCM subscription depth of 50 complete frames. Each entry retains its complete byte vector, current offset, and message type. Only the FIFO head may reach the transport. Once any prefix of A is accepted, no byte of B is written until A completes or the transport/session fails and the entire session-local FIFO is explicitly abandoned. Queue saturation drops only the new whole frame before writing any byte; it cannot grow without bound or interleave frames.

Production fix: `ReceiverNode::Impl` replaces callback-local `write_all` state with the bounded `pending_rtcm_writes_` FIFO and `FlushPendingRtcmWrites`. Positive partial progress advances only the head offset and may continue synchronously; a successful zero-byte result stops that flush immediately without an error or busy loop, preserving the suffix for the next timer `StepOnce` or RTCM callback. A following callback first retries the head and otherwise queues its complete frame. A non-OK write closes the sink, clears all old-session pending bytes, marks the transport unavailable, and records one failure. A terminal read similarly clears pending state. No old suffix can reach a subsequently opened transport session.

Accounting semantics: `forwarded_bytes` now advances for every byte actually accepted by the transport, including a prefix later abandoned after a hard failure. `forwarded_frame_count`, `last_message_type`, and forwarding-active time advance only after the complete frame is written, preserving UGA-019 completion-based liveness. Would-block/zero progress does not increment `write_error_count`; hard failures, disconnect with pending data, unsupported/closed sinks, and bounded-queue overflow do.

Post-fix validation: Focused UGA-009 regressions passed 7/7 after failing 6/7 pre-fix. Complete ReceiverNode passed 43/43, including UGA-014 timestamp-domain, UGA-019 forwarding active/stale/recovery, and UGA-020 cadence-aware freshness coverage. Complete affected ROS2 CTest passed 6/6, including NtripNode 11/11; complete non-ROS CTest passed 61/61, including all NTRIP reconnect/liveness and transport tests. The seven focused ReceiverNode cases passed 7/7 under combined ASan+UBSan.

Remaining limitation: The bounded FIFO intentionally drops the newest complete frame when all 50 entries remain occupied; it does not block the ROS executor or allocate an unbounded backlog. ReceiverNode still has no in-place transport reconnection facility: a hard write failure closes that session, and reconnect/reconstruction remains the owning deployment's responsibility. No public header, ROS message, topic, or parameter changed.

Recommended next action: Keep full write, short write, WouldBlock continuation, zero progress, multiple partials, two-frame ordering, queue bound, hard failure, disconnect, and new-session isolation in the ReceiverNode regression lane.

### UGA-010 — Unicore GPGGA and PVTSLNA commands use the wrong grammar

Status: **ALREADY FIXED**

Confidence: **Confirmed**

Current evidence: `gnss_driver/src/unicore_config_profile_builder.cpp`, `ToOutputMessageName` and `BuildOutputCommand`, current lines 117-157 now serialize all periodic outputs as `<MESSAGE> <period>`; the old `UsesLogOntimeSyntax` branch is absent. The current UM982 production plan emits `GPGGA 1` and `PVTSLNA 1`, and the debug plan emits `PVTSLNA 0.2`. The authoritative local N4 manual's GPGGA section gives `GPGGA 1` for the current port and `GPGGA COM2 1` for an explicit port; its PVTSLNA section gives `PVTSLNA 1`. These match the current generated current-port forms exactly.

Relevant changes since old audit: Commits `f4417dd` and especially `56d747e` substantially revised Unicore grammar and current-port policy. The old `LOG GPGGA ONTIME ...` / `LOG PVTSLNA ONTIME ...` branch was removed, production/tests/docs were changed to direct-period syntax, and manual-supported periods were constrained. Later signal-group/application commits do not revert these output strings. Searching the current builder/tests finds no remaining `LOG GPGGA`, `LOG PVTSLNA`, or `UsesLogOntimeSyntax` production path.

Reproduction/test result: `/tmp/universal-gnss-current-delta-build/gnss_tools/gnss_config_plan unicore rover_high_precision --model UM982` exited successfully and printed command 6 `GPGGA 1` and command 9 `PVTSLNA 1`; the debug profile printed `GPGGA 1` and `PVTSLNA 0.2`. A direct extraction of the repository PDF confirmed the manual examples above and its generic optional-port/current-port rule. Both current `gnss_driver_test_unicore_config_profile_builder` and `gnss_driver_test_receiver_auto_config` passed.

Remaining defect, if any: None for the original GPGGA/PVTSLNA grammar finding. This audit did not repeat the hardware acceptance probe documented in `docs/vendors/unicore/config_profiles.md`, but current generation now agrees with the authoritative repository manual rather than relying on that probe.

Recommended next action: Retain manual-derived golden command assertions for both current-port and any future explicit-port forms, and include GPGGA/PVTSLNA in routine supported-hardware provisioning smoke tests.

### UGA-011 — `SIGNALGROUP` can reboot before the remainder of the plan

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `gnss_tools/src/config_apply.cpp` now routes every emitted Unicore `CONFIG SIGNALGROUP` command—not only required commands—through `ExecuteUnicoreSignalGroupAwarePhase`. That phase queries the current value, closes/reopens before the dedicated command, waits for recovery, reprobes an active receiver response, queries/verifies the applied value, reopens again, and only then executes the pre/post profile phases. The generic apply path rejects an emitted `SIGNALGROUP` plan without transport hooks before any write. A plan with no `SIGNALGROUP` still stays on the ordinary apply path and makes no recovery calls.

Relevant changes since old audit: The production base had already added the dedicated workflow for required persistent/recovery plans, but intentionally limited its selection to `FindFirstRequiredUnicoreSignalGroupCommandIndex`. This fix replaces that condition with the all-command `FindFirstUnicoreSignalGroupCommandIndex` in the normal, recovery-profile, and runtime-baud workflow selections. It retains optional-command semantics: a receiver-rejected optional override is verified as unchanged after recovery and the rest of the plan continues; a required mismatch, missing verification, or failed reopen/reprobe stops safely.

Files changed: `gnss_tools/src/config_apply.cpp`, `gnss_tools/tests/test_config_apply.cpp`, and this delta report.

Regression tests added: `TestUnicoreRuntimeSignalGroupOverrideUsesRecoveryBoundary` simulates an accepted runtime override followed by a disconnect and asserts recovery hooks run, verified reopen/reprobe succeeds, no stale write occurs, and `GPGGA` follows the recovered boundary. `TestUnicoreRuntimeSignalGroupVerificationFailureStopsProfilePhase` proves a failed verification returns `kRejected` before `GPGGA`. The existing optional-rejection test now covers an explicit `PARSING FAILED GRAMMAR ERROR` response, required recovery, and safe continuation only after verification.

Pre-fix failure evidence: Before the production change, the new tests failed four assertions: the accepted optional override did not complete or prove post-boundary recovery, and verification failure neither stopped safely nor prevented the later output command. After broadening the selection but before preserving optional-rejection semantics, the existing optional-rejection regression failed two assertions; that proved the recovery path must distinguish receiver rejection from an accepted rebooting override.

Production fix: Select the dedicated state boundary whenever any Unicore `SIGNALGROUP` command is actually present. Refuse the generic no-hook path without writing. After a verified optional command rejection, retain the receiver's reported unchanged group and continue the pre/post phases only through reopened transport; verification absence, required mismatch, and recovery failures remain terminal.

Post-fix validation: `gnss_tools_test_config_apply` passed 1/1. The complete `gnss_tools` CTest selection passed 11/11. Full non-ROS CTest passed 61/61 outside the restricted sandbox; the sandbox itself denies the socket `send(MSG_NOSIGNAL)` used by unrelated TCP/NTRIP tests. No ROS2 test is applicable: this change is limited to the CLI/configuration-application workflow and does not alter ROS2 surfaces.

Compatibility/public-behavior impact: Normal no-override plans remain unchanged. Explicit runtime `SIGNALGROUP` plans now require the existing recovery hooks and return `kTransportUnavailable` rather than risking a stale write when hooks are unavailable. Save and output commands retain their existing plan order, but are now sent only after the documented recovery/verification boundary.

Sanitizer result: Not separately run; the regression exercises deterministic transport lifecycle and state-machine behavior. Targeted, tools-wide, and full non-ROS validation passed.

Remaining defect, if any: None in the configuration-application state-machine path. Physical Unicore validation remains recommended because actual reset duration, port movement, and baud behavior vary by model and firmware.

Recommended next action: Run the accepted-override recovery sequence on every supported Unicore model/firmware while retaining the hook-based recovery assertions.

### UGA-012 — unknown Unicore models receive guessed production-ready configuration

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `gnss_driver/src/receiver_auto_config.cpp`, `BuildUnicorePlan`, now stops before profile construction when `ResolveUnicoreModelProfile` returns `kUnknown`. Mutating requests return `kUnsupportedReceiver`, an empty command list, `receiver_recognized=false`, `config_supported=false`, `production_ready=false`, and `ready_to_execute=false`. This applies equally to absent, empty, malformed, case-variant, and future model identifiers and prevents a `SIGNALGROUP` override from bypassing the guard. Documented UM960, UM980, UM981, and UM982 profiles retain their normal supported plans. `runtime_only` remains a successful zero-command/read-only plan but explicitly reports no recognized model or configuration support and is not ready to execute as configuration.

Relevant changes since old audit: The production-base changes removed automatic `UNLOG` and runtime `SIGNALGROUP`, corrected command grammar, and added warnings, but still treated the generic unknown profile as production-ready. This fix replaces that fallback only at the model-recognition boundary; it neither narrows any documented model profile nor introduces an expert bypass.

Files changed: `gnss_driver/src/receiver_auto_config.cpp`, `gnss_driver/tests/test_receiver_auto_config.cpp`, `gnss_tools/tests/test_config_plan.cpp`, `gnss_tools/tests/test_profile_preview.cpp`, and this delta report.

Regression test added: `TestUnicoreUnknownModelConfigurationIsBlocked` covers `FUTURE123`, `future123`, malformed `UM98?`, empty, and absent identifiers; a `SIGNALGROUP` override; persistent mutation; unknown-model `runtime_only`; and all four currently documented models. Config-plan and profile-preview assertions now preserve the model identifier in the reported error while requiring zero commands for unknown models. Existing profile-workflow fixtures now identify UM981 explicitly instead of relying on the removed unsafe generic fallback.

Pre-fix failure evidence: Before the production change, `gnss_driver_test_receiver_auto_config` failed eight new assertions: each of the five unknown/empty forms returned a 13-command production-ready plan, an unknown override emitted a command, persistent configuration was accepted, and unknown `runtime_only` falsely claimed recognition/configuration support and readiness.

Production fix: Gate Unicore mutation on a documented resolved model. For unknown identity, return a no-command `kUnsupportedReceiver` plan with an explicit error and no capabilities claim; for `runtime_only`, construct only the existing no-change plan and clear the configuration-support/readiness claims. The guard executes before rate, baud, output, signal-profile, or persistence profile construction.

Post-fix validation: The focused driver/config-plan/profile-preview tests passed 3/3. Complete `gnss_driver` plus `gnss_tools` CTest passed 30/30. Complete non-ROS CTest passed 61/61 outside the restricted sandbox; the sandbox denies socket operations required by unrelated TCP/NTRIP tests. Repository search found no ROS2 caller of `BuildReceiverAutoConfigPlan` or the config-apply workflow, so no ROS2 test applies.

Compatibility/public-behavior impact: A prior generic Unicore profile without `--model`, or an unrecognized model string, no longer produces a mutating plan. Operators must supply one of the documented model identities for configuration. Read-only `runtime_only` discovery/inspection behavior remains available with zero receiver writes; no unsafe expert override was added.

Sanitizer result: Not separately run; this is deterministic planner classification/command-generation coverage. Focused, driver/tools-wide, and complete non-ROS validation passed.

Remaining defect, if any: None for unknown-model mutation or production-ready classification. Hardware/vendor validation is still recommended when adding a new supported model, before adding it to the documented model resolver.

Recommended next action: Keep the unknown-model guard tests and require a local vendor-documentation review plus hardware provisioning evidence for every future supported model entry.

### UGA-013 — high Unicore rate overrides round to a zero-period command

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `gnss_driver/src/receiver_auto_config.cpp` now validates every supplied Unicore rate against the existing documented `{1,2,5,10,20,50}` Hz domain before `NormalizeUnicoreOutputRateHz` performs distance arithmetic. Non-finite and non-positive values remain rejected by the shared validator; finite values below 1 Hz or above 50 Hz are now `kInvalidArgument` with no commands. The existing `gnss_driver/src/unicore_config_profile_builder.cpp` period allowlist continues to validate the final output periods `{1,0.5,0.2,0.1,0.05,0.02}` before three-decimal command serialization. Accepted input therefore reaches only an exactly representable documented period.

Relevant changes since old audit: `f4417dd` added documented rate/period tables and final-period validation, eliminating the original `BESTNAVA 0` serialization. It retained a broad positive-finite front-end domain, however, allowing extreme values to corrupt nearest-rate selection. This fix makes the front-end domain match the pre-existing documented table without changing the accepted in-domain rounding policy or low-level serializer.

Files changed: `gnss_driver/src/receiver_auto_config.cpp`, `gnss_driver/tests/test_receiver_auto_config.cpp`, and this delta report.

Regression test added: `TestUnicoreOutputRateDomainAndSerialization` asserts exact serialized BESTNAVA periods at all six documented boundaries, lower-tie/upper-neighbor behavior at 7.5/7.5001 Hz, and rejection before command generation for zero, negative, `+NaN`, `-NaN`, `+Inf`, `-Inf`, a subnormal, immediately below 1 Hz, immediately above 50 Hz, 10,000 Hz, and `1e308`.

Pre-fix failure evidence: Before the production change, the focused driver test failed five new assertions: the subnormal, both just-outside-boundary values, 10,000 Hz, and `1e308` each produced a valid mutation plan. The latter reproduced the delta defect by collapsing to `BESTNAVA 1`; the old 10,000-Hz case was normalized to 50 Hz rather than rejected. The existing shared validation already rejected zero, negative, NaN, and infinities.

Production fix: Add `ValidateUnicoreOutputRateHz` after shared finiteness/positivity validation and before output-rate normalization. It rejects rate inputs outside the documented inclusive 1–50 Hz range, so the nearest-distance computation cannot overflow, underflow, or normalize an absurd request to an unrelated command.

Post-fix validation: The focused auto-config, Unicore builder, config-plan, and profile-preview tests passed 4/4. The rebuilt CLI now returns exit 1 with `Unicore rate-hz must be within the documented portable range of 1 to 50 Hz` for `--rate-hz 1e308`; 50 Hz succeeds and emits `BESTNAVA 0.02`. Complete `gnss_driver` plus `gnss_tools` CTest passed 30/30. Complete non-ROS CTest passed 61/61 outside the restricted sandbox. No ROS2 caller uses the auto-configuration planner or config-apply workflow, so no ROS2 test applies.

Compatibility/public-behavior impact: Legal documented rates and existing in-range nearest-rate behavior are unchanged. Prior finite out-of-range requests that were silently normalized (including 10,000 Hz) now fail explicitly rather than generate an unrelated receiver command.

Sanitizer result: Not separately run; this is bounded numeric input validation plus deterministic planner serialization. Focused, driver/tools-wide, CLI, and complete non-ROS validation passed.

Remaining defect, if any: None for Unicore output-rate validation and serialized-period representability in the portable 1–50 Hz contract. Hardware validation remains recommended for the existing manual-derived rate table on each supported model/firmware.

Recommended next action: Retain boundary/midpoint/extreme-rate regressions and require vendor-documentation plus hardware acceptance evidence before extending the portable rate table.

### UGA-014 — RTCM ROS stamps mix monotonic and ROS/system clock domains

Status: **FIXED**

Confidence: **Confirmed**

Old defect: NtripNode copied boot-relative steady nanoseconds into public ROS `RtcmFrame.stamp`; ReceiverNode then interpreted any public ROS/system/sim stamp as a steady protocol timestamp. The correction monitor subtracted incompatible epochs, allowed negative ages, and let future observations satisfy correction requirements.

Pre-fix reproduction: The focused monitor regression failed four assertions: a future RTCM observation produced a negative age, satisfied the bounded MSM requirement, made corrections available, and omitted `rtcm.freshness_unknown`. ReceiverNode's semantic test produced immediate RTCM ages of thousands of seconds after deliberately incompatible public stamps, and NtripNode's public frame stamp was about `2.47e12` ns while the node ROS clock was about `1.79e18` ns.

Temporal contract applied: NtripNode captures public `RtcmFrame.stamp` from its ROS clock at local read receipt while its NTRIP client and correction monitor retain steady timestamps internally. ReceiverNode ignores the public stamp for freshness and captures one new steady timestamp at its RTCM callback receipt boundary. The correction monitor now treats `last_seen > now` as unknown age, excludes future values from lower-and-upper-bounded requirement windows, and disallows future timestamps from startup grace. No value is clamped and no epoch conversion or absolute-age workaround exists.

Production files changed: `gnss_protocols/src/rtcm_correction_monitor.cpp`, `gnss_ros2/src/ntrip_node.cpp`, `gnss_ros2/src/receiver_node.cpp`, and the clarified contract in `gnss_ros2/msg/RtcmFrame.msg`.

Post-fix validation: Both focused ROS clock-domain tests pass: the NTRIP stamp lies within the ROS receipt window, and ReceiverNode reports a small non-negative monotonic age before and after public stamp jumps from a far-future value to an old value. The monitor future-timestamp regression passes. Complete protocols passed 18/18, full ReceiverNode 36/36 and NtripNode 10/10 gtests passed, complete ROS2 CTest passed 6/6, and non-ROS CTest passed 61/61. `TestTimestampHistoryRetention` (UGA-017), forwarding active/stale/recovery tests (UGA-019), and all cadence/fallback/boundary/recovery tests (UGA-020) remained green. ASan+UBSan and standalone UBSan passed the two touched non-ROS binaries.

Public/API compatibility impact: `RtcmFrame` wire layout is unchanged, but its formerly ambiguous stamp is now explicitly ROS-clock local receipt time. Internal correction freshness no longer consumes that public value. The `GnssStatus` interface change described under UGA-007 is the only ROS wire-layout change in this batch.

Remaining limitation: Public ROS timestamps can move with simulated/system time by design and are unsuitable for age arithmetic; consumers must capture their own local monotonic receipt time, as documented. Hardware/vendor validation is not required for the clock-domain separation.

Recommended next action: Keep the public-stamp-domain, future-time, ROS-jump, stale, and recovery cases in continuous testing.

## Summary

| Finding | Status | Confidence |
|---|---|---|
| UGA-001 | FIXED | Confirmed |
| UGA-002 | FIXED | Confirmed |
| UGA-003 | STILL PRESENT | Confirmed |
| UGA-004 | FIXED | Confirmed |
| UGA-005 | FIXED | Confirmed |
| UGA-006 | FIXED | Confirmed |
| UGA-007 | FIXED | Confirmed |
| UGA-008 | STILL PRESENT | Confirmed |
| UGA-009 | FIXED | Confirmed |
| UGA-010 | ALREADY FIXED | Confirmed |
| UGA-011 | FIXED | Confirmed |
| UGA-012 | FIXED | Confirmed |
| UGA-013 | FIXED | Confirmed |
| UGA-014 | FIXED | Confirmed |
| UGA-015 | ALREADY FIXED | Confirmed |
| UGA-016 | FIXED | Confirmed |
| UGA-017 | FIXED | Confirmed |
| UGA-018 | FIXED | Confirmed |
| UGA-019 | FIXED | Confirmed |
| UGA-020 | FIXED | Confirmed |
| UGA-021 | STILL PRESENT | Confirmed |
| UGA-022 | FIXED | Confirmed |
| UGA-023 | FIXED | Confirmed |
| UGA-024 | FIXED | Confirmed |
| UGA-025 | FIXED | Confirmed |
| UGA-026 | FIXED | Confirmed |
| UGA-027 | FIXED | Confirmed |
| UGA-028 | FIXED | Confirmed |
| UGA-029 | FIXED | Confirmed |

### UGA-015 — portable correction health unconditionally requires RTCM 1230

Status: **ALREADY FIXED**

Confidence: **Confirmed**

Current evidence: `gnss_protocols/src/rtcm_correction_monitor.cpp` lines 231-245 now configures the portable RTK preset with `require_any_msm=true`, `require_base_position=true`, and `require_glonass_bias=false`. `HasRequiredCorrectionMessages` lines 590-690 evaluates 1230 only when a caller explicitly sets `require_glonass_bias`; the public option remains available at `gnss_protocols/include/universal_gnss_protocols/rtcm_correction_monitor.hpp` lines 37-47 for a specialized GLONASS-dependent profile. `gnss_ros2/src/ntrip_node.cpp` lines 562-571 and `gnss_tools/src/gnss_ntrip_monitor.cpp` lines 162-173 build fresh options and apply the corrected portable preset, so their correction availability no longer implicitly depends on 1230. A decoded 1230 whose validity indicator is clear is now informational at monitor lines 829-840 rather than suppressing availability.

Relevant changes since old audit: Commit `3495ffb9ba8eea8be8401c9736ff081e046063a7` directly changes the portable preset from `require_glonass_bias=true` to `false`, downgrades `rtcm.1230_not_valid` from warning to information, adjusts the ROS semantic projection, and adds regressions for a stream without 1230 and for a decoded-but-not-valid 1230. No later change through `aaacc6be92463ad493d6f4260426cf645188078f` reverses those semantics. This is the merged change anticipated by the original finding, and it affects every current in-tree consumer of the portable preset.

Reproduction/test result: `ctest --test-dir /tmp/universal-gnss-current-delta-build -R '^gnss_protocols_test_rtcm_correction_monitor$' --output-on-failure` passed 1/1. Its current `TestPortableRtkRequirementsDoNotRequireGlonassBias` observes fresh 1006 plus GPS MSM 1077 with no 1230 and verifies `require_glonass_bias=false`, required corrections satisfied, `correction_available=true`, and overall severity OK. `TestDecodedInvalid1230IsInformationalAndCorrectionStillAvailable` also verifies that a present but validity-cleared 1230 leaves corrections available, produces no error, and is reported at information severity.

Remaining defect, if any: None for UGA-015. The former unconditional portable requirement and its false unavailable/error result are removed. Callers that genuinely require GLONASS bias can still opt into the existing explicit `require_glonass_bias=true` path; automatic receiver/source capability selection was not necessary to eliminate the audited defect.

Recommended next action: Keep the two focused regression scenarios in the current monitor suite; no production change is recommended for this finding.

### UGA-016 — static base metadata is expired as a 30-second dynamic observation

Status: **FIXED**

Confidence: **Confirmed**

Pre-fix reproduction: `gnss_protocols_test_rtcm_correction_monitor` failed `TestStaticBaseMetadataOutlivesDynamicObservationWindow`: decoded station-42 1005 at T0 plus fresh station-42 MSM through T31 became unavailable solely because the 1005 age exceeded the 30-second dynamic window. The same scenario confirmed that the decoded base record and its age remained observable even though it no longer satisfied health.

Static/dynamic lifetime contract: Decoded 1005/1006 is static metadata owned by the normalized correction source and decoded RTCM station ID. Its last-seen age remains reportable, but requirement satisfaction does not use the MSM observation window. MSM and other dynamic correction observations remain window/freshness based. RTCM 1230 retains its existing optional portable-policy behavior and is not promoted to static base metadata.

Production fix: `HasRequiredCorrectionMessages` treats 1005/1006 presence as static ownership state while retaining the common rolling window for MSM, constellations, optional 1230, and other dynamic types. `ResetDynamicState` clears session/dynamic histories while preserving only fully decoded station-owned 1005/1006 metadata across a same-source reconnect. Full reset, source change, or decoded station mismatch clears it.

Post-fix validation: The focused test now keeps correction availability true at base age 31 seconds with fresh MSM, reports the base age unchanged, and makes health false once MSM itself expires. Replacement/source/station invalidation scenarios pass with UGA-018. Complete `gnss_protocols` passed 18/18, `gnss_ntrip` 7/7, non-ROS CTest 61/61, and ROS2 6/6. UGA-015 optional-1230 and UGA-017 bounded-history coverage remained green. ASan+UBSan and standalone UBSan each passed 4/4 affected binaries.

Compatibility impact and remaining limitation: Static lifetime changes the corrected public health result for valid long-lived base metadata. Integrity-valid but semantically malformed known RTCM is still governed by the separate UGA-008 behavior; only fully decoded 1005/1006 is carried across reconnect.

Recommended next action: Keep long-lived base/fresh MSM, MSM expiry, age reporting, same-source reconnect, station replacement, full reset, and optional-1230 tests together.

### UGA-017 — RTCM rate history grows without bound and queries are linear

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `RtcmCorrectionMonitor` now stores all four timestamp-history categories in ordered multisets rather than lifetime vectors. Each timestamp insertion removes records older than the 60-second `kTimestampHistoryRetentionNs` horizon; eviction is ordered and amortized, while window queries use `lower_bound`/`upper_bound` over only retained history. Lifetime frame/message/constellation counters and latest-record metadata remain separate and are not pruned. Rate queries wider than the documented retention horizon return no rate rather than a silently incomplete lifetime result.

Relevant changes since old audit: The sole production-base RTCM-monitor change (`3495ffb`) made RTCM 1230 optional; it did not affect timestamp storage. This fix changes only the monitor's rate-history representation and its focused regression coverage.

Files changed: `gnss_protocols/include/universal_gnss_protocols/rtcm_correction_monitor.hpp`, `gnss_protocols/src/rtcm_correction_monitor.cpp`, `gnss_protocols/tests/test_rtcm_correction_monitor.cpp`, and this delta report.

Regression test added: `TestTimestampHistoryRetention` drives 100,000 mixed GPS-MSM, GLONASS-MSM, and invalid-frame observations over 100 seconds. It verifies recent total and constellation rates, lifetime totals, expiration of an old rate window, and a bounded aggregate retained-history count across total, valid, type, and constellation storage.

Pre-fix failure evidence: Before eviction, the focused monitor test failed the retained-history assertion after 100,000 observations: all timestamp copies remained stored and exceeded the 250,000-entry bound. The rate/count assertions otherwise showed why the leak was masked by normal short runs.

Production fix: Retain timestamped rate diagnostics for a time horizon rather than for process lifetime. Ordered insertion supports safe out-of-order timestamps; pruning erases each old entry once, avoiding an O(n) scan on every observation. Window queries restrict their search to the retained ordered range, so their cost no longer increases with total process lifetime.

Post-fix validation: Focused `gnss_protocols_test_rtcm_correction_monitor` passed 1/1 in 0.15 s. The complete `gnss_protocols` suite passed 18/18. Complete non-ROS CTest passed 61/61 outside the restricted sandbox.

Compatibility/public-behavior impact: Current diagnostic windows (up to the existing 30-second correction requirement window) retain exact timestamp semantics with an additional 30-second margin. Rate callers requesting more than 60 seconds now receive no rate instead of an unbounded/lifetime statistic or a silently partial result.

Remaining defect, if any: None for unbounded history and lifetime-scaling rate scans. Within the chosen horizon, a very high rate still makes a very large requested window proportionally expensive to count, but work is bounded by the requested/current diagnostic window rather than process lifetime.

Recommended next action: If a future consumer requires rate windows over 60 seconds, make that requirement explicit and raise the monitor retention horizon together with a bounded-memory budget and soak test.

### UGA-018 — correction metadata has no endpoint/station ownership model

Status: **FIXED**

Confidence: **Confirmed**

Pre-fix reproduction: The focused monitor test failed five ownership assertions: station-A base combined with station-B MSM, incompatible retained base was not invalidated, station-A MSM combined with station-B base, incompatible MSM remained, and the 0/4095 boundary transition was not deterministic. The NTRIP client test failed three more: same-source reconnect erased decoded base metadata, same-source/same-station MSM could not restore health without a repeated base message, and a mountpoint change neither closed the session nor cleared source-owned metadata.

Source/station ownership contract: `source_identity` is normalized lowercase host, port, and normalized mountpoint path. Host case and equivalent slash spelling do not create a transition; a different host, port, or mountpoint does. `station_identity` is the full decoded 12-bit reference-station ID from 1005/1006, MSM, or 1230, including 0 and 4095. Ownership is established/checked before the current frame is recorded. A decoded mismatch fully invalidates the previous station state, then records only the new frame, so no transient mixed healthy state is possible. Non-station-bearing messages cannot fabricate or replace ownership.

Reconnect/source-transition behavior: Reconnect to the same normalized source clears the response buffer, framer, flow state, dynamic MSM/1230 freshness, and rate/session histories, while provisionally retaining fully decoded base metadata and station ID. Same-station post-reconnect MSM may reuse it. The first decoded different-station frame immediately clears it. `set_config` to a different source closes the live transport, resets reconnect/session state, and fully clears correction ownership; no source-A state can satisfy source B.

Production fix: Added `NtripSourceIdentity`/`BuildNtripSourceIdentity`, source-change handling in `NtripClient`, split full-source versus session/dynamic monitor resets, and explicit station ownership in `RtcmCorrectionMonitor`. Tests cover A+A health, A+B in both arrival orders, replacement recovery, same-source reconnect, same/different post-reconnect stations, host/port/mountpoint changes and normalization, dynamic reset, boundary IDs, non-station records, and deterministic full reset. Existing single-stream tool fixtures that accidentally used different station IDs were corrected to represent one station; no production expectation was weakened.

Post-fix validation: Focused CTests passed 3/3 plus the base-position target. Complete `gnss_protocols` passed 18/18, `gnss_ntrip` 7/7, non-ROS CTest 61/61, and ROS2 6/6. UGA-015, UGA-017, UGA-019, UGA-020, and UGA-014 regression paths remained green. ASan+UBSan and standalone UBSan each passed 4/4 affected binaries.

Compatibility impact and remaining limitation: New additive C++ source/station APIs and monitor state change class ABI, requiring dependent C++ binaries to rebuild; ROS message wire layouts are unchanged. Ownership changes only on fully decoded station-bearing records. A CRC-valid but semantically undecodable frame cannot establish a trustworthy station ID and remains part of UGA-008 rather than being guessed here.

Recommended next action: Keep source normalization/change, reconnect retention, mixed-station ordering, replacement, reset, and boundary-ID tests; validate real caster station IDs across an intentional reconnect/mountpoint switch.

### UGA-019 — “forwarding active” means “ever forwarded”

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `NtripNode` now decides forwarding activity from `last_rtcm_published_time_` measured on the local steady clock; it keeps `published_frame_count` and last-message fields as historical values. `ReceiverNode` applies the same local-clock rule to successful transport writes and tracks separate local receipt times for u-blox accepted-RTCM counter advances and Unicore `RTCMSTATUS` counter advances. Therefore a stale correction-use report can no longer make `correction_available` or `receiver_rtcm_active` true merely because it occurred once. The TCP/NTRIP `Streaming` state remains independently visible as an open response stream, distinct from fresh RTCM delivery.

Relevant changes since old audit: The old-to-current production-base diff was empty for NtripNode and ReceiverNode forwarding liveness; RTCM 1230 changes do not affect it. This fix adds the explicitly configured `rtcm_forwarding_activity_timeout_s` (positive, finite; default 5 s) to both ROS nodes and uses it only for local forwarding/use liveness. It deliberately does not consume public `RtcmFrame.stamp`, so it does not couple this correction to UGA-014 clock-domain work.

Files changed: `gnss_ros2/src/ntrip_node.cpp`, `gnss_ros2/src/receiver_node.cpp`, `gnss_ros2/tests/test_ntrip_node.cpp`, `gnss_ros2/tests/test_receiver_node.cpp`, and this delta report.

Regression tests added: `NtripNodeTest.ReportsRtcmForwardingStaleAfterSilenceAndRecovers` and extended `ReceiverNodeTest.ConsumesRtcmTopicAndWritesCorrectionsToDuplexTransport` cover never received, recent success, silence past a 50 ms test threshold, historical counter retention, and recovery after a following frame. `ReceiverNodeTest.ReportsReceiverRtcmUseStaleAfterSilenceAndRecovers` covers receiver-reported u-blox acceptance through the same transition and verifies `receiver_correction_available` clears then recovers.

Pre-fix failure evidence: With the new focused tests but before production changes, both NtripNode and ReceiverNode remained `OK: RTCM forwarding active` after 80 ms of complete silence despite the requested 50 ms activity threshold; CTest reported one failing regression in each binary. That is the direct lifetime-counter failure, reproduced without any ROS/steady/public timestamp comparison.

Production fix: Successful RTCM publish/write/use is timestamped on receipt with `steady_clock`; `*_active` predicates now require an age strictly below the configured forwarding activity timeout. A prior success beyond that threshold emits a stale forwarding/use event and a WARN dedicated forwarding status, while frame/byte/message totals remain unchanged. A subsequent successful frame or receiver report refreshes only its local activity time and returns the status to active. The existing Ntrip `Streaming` state message now says the response stream is open rather than claiming fresh correction activity.

Post-fix validation: `ctest --test-dir /tmp/universal-gnss-current-delta-ros2-build/universal_gnss_ros2 -R '^(test_ntrip_node|test_receiver_node)$' --output-on-failure` passed 2/2 (31 ReceiverNode tests, 9 NtripNode tests). The complete non-ROS `ctest --test-dir /tmp/universal-gnss-current-delta-build --output-on-failure -j2` passed 61/61. The focused tests prove active -> stale -> active recovery while retaining the count of 1/2 frames.

Remaining defect, if any: None for the audited active-versus-ever diagnostics. The default activity timeout is deliberately configurable because correction cadence is source-dependent; separate NTRIP silent-stream reconnection (UGA-005) and public RTCM timestamp provenance (UGA-014) remain outside this fix.

Recommended next action: Configure `rtcm_forwarding_activity_timeout_s` to the expected caster/receiver correction cadence during deployment, and address reconnect policy and public timestamp-domain policy in their respective findings.

### UGA-020 — fixed three-second receiver staleness rejects valid slow cadences

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `ReceiverNode` no longer has `kStaleTimeout`. Its runtime and transport receipt checks, `HasFreshRuntimeState`, and fix-publication gate share `RuntimeObservationFreshnessTimeout()`, derived only from expected receiver observation cadence or its explicit fallback. `publish_rate_hz` still controls the node timer and is never part of the freshness calculation. All liveness times remain local `steady_clock` receipt times.

Relevant changes since old audit: The old-to-current diff had no ReceiverNode cadence policy change. This fix introduces `expected_runtime_observation_rate_hz` and `runtime_observation_fallback_timeout_s` on ReceiverNode; neither is derived from ROS publication rate or public message timestamps. RTCM 1230 work is unrelated.

Files changed: `gnss_ros2/src/receiver_node.cpp`, `gnss_ros2/tests/test_receiver_node.cpp`, and this delta report.

Regression tests added: `KeepsQuarterHertzRuntimeFreshAcrossFourSecondCadence` verifies a 0.25 Hz GGA stream stays fresh through 3.1 s and accepts its next observation at about 4.1 s even with `publish_rate_hz=20`. `UsesExpectedOneHertzCadenceWithJitter` covers a 1.2 s arrival interval. `DetectsHighRateSilenceAndRecoversAtDerivedTimeout` checks a 10 Hz stream at 250 ms (fresh, before the 300 ms timeout), 350 ms (stale, after it), then a fresh subsequent observation (recovered). `UsesConservativeFallbackWithoutExpectedCadence` verifies the explicitly configured fallback when no expected rate is available. Existing stale-recovery tests now declare their intended 1 Hz source cadence instead of accidentally relying on the former global constant.

Pre-fix failure evidence: Before the production change, the new ReceiverNode suite failed 3 regressions. At 3.1 s a valid configured 0.25 Hz stream emitted both `runtime_state_stale` and `transport_data_stale` and lost its fix; 10 Hz silence was not stale at its requested derived boundary because the expected-rate parameter did not exist; and the configured unknown-cadence fallback was ignored. The 1 Hz jitter scenario happened to pass the old three-second constant, as expected, but did not prove cadence configuration.

Production fix: When `expected_runtime_observation_rate_hz > 0`, freshness is exactly `3.0 / expected_runtime_observation_rate_hz` seconds: three expected observation periods provide the jitter/missed-sample margin. A zero expected rate selects the positive, finite, `steady_clock`-representable `runtime_observation_fallback_timeout_s` (default 10 s). Invalid/non-finite/unrepresentable settings are rejected. The same local timing governs runtime freshness, transport-data stale reporting, startup no-data grace, and whether a cached fix may be published; it is intentionally independent of forwarding activity (UGA-019) and of ROS/public timestamp domains.

Post-fix validation: `ctest --test-dir /tmp/universal-gnss-current-delta-ros2-build/universal_gnss_ros2 -R '^test_receiver_node$' --output-on-failure` passed 35/35, including the four new cadence/fallback regressions and exact 10 Hz boundary checks. `test_ntrip_node` passed 9/9, preserving UGA-019. `gnss_protocols_test_rtcm_correction_monitor` passed 1/1, preserving UGA-017. Complete non-ROS CTest passed 61/61. `git diff --check` passed.

Remaining defect, if any: None for the fixed universal three-second staleness behavior. The node cannot infer a distinct expected rate for each heterogeneous runtime sentence; deployments with such a profile must set the configured aggregate rate conservatively. Timestamp provenance (UGA-002/007/014) and transport/backlog policy remain deliberately outside this change.

Recommended next action: Configure `expected_runtime_observation_rate_hz` per receiver output profile whenever known; otherwise choose `runtime_observation_fallback_timeout_s` for the deployment’s safe maximum observation gap.

### UGA-021 — runtime aggregation cannot propagate explicit value invalidation

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_core/include/universal_gnss/gnss_runtime_state.hpp` current lines 119-126 implements `ClearOptionalValue` by resetting the optional and clearing its sample `value_flags` bit; no separate clear intent is recorded. `GnssRuntimeAggregator::Merge` in `gnss_runtime_aggregator.hpp` lines 31-43 unions capability flags permanently. Direct coordinates are merged only when the source optional has a value at lines 320-333, and every capability field is merged only when both its effective value bit and optional value are present at lines 336-353. Thus absent-as-omitted and absent-as-explicitly-cleared take the same no-op branch; per-field versions are not advanced for clears. `MergeNmeaGsaIntoRuntimeState`, `MergeNmeaGsvIntoRuntimeState`, and `MergeNmeaGstIntoRuntimeState` at `gnss_protocols/src/nmea_parser.cpp` lines 1197-1242, 1245-1293, and 1296-1336 still explicitly call `ClearOptionalValue` for absent current HDOP/VDOP/satellite, CN0, and accuracy data, but the aggregator cannot carry that intent. Existing `TestExistingValuesSurviveUpdatesWithoutValueFlags` intentionally verifies useful cross-sentence retention but does not distinguish a true clear.

Relevant changes since old audit: The focused diff and log from `804bed8d7f753f6834212cfbc9dc329f88360299` to `aaacc6be92463ad493d6f4260426cf645188078f` are empty for `gnss_core`, the NMEA runtime merge path, and the relevant u-blox/Unicore parser merge code. No tri-state field intent, clear mask, clear versioning, or coordinate invalidation semantics have been added.

Reproduction/test result: `/tmp/universal-gnss-current-uga021-repro` initialized two aggregators with a valid fix, coordinates, HDOP, horizontal accuracy, corrections-active=true, baseline length, and baseline solution status at timestamp 100. At timestamp 200 one aggregator received `NO_FIX` plus explicit `ClearOptionalValue` calls and absent coordinates; the other received the same `NO_FIX` while simply omitting all enrichment fields. Output showed `clear_update_hdop_value=0 clear_update_hdop_flag=0 clear_merged=1 omitted_merged=1 outcomes_identical=1`. The aggregate advanced to timestamp 200 and no-fix, yet reported `retained_latitude=1 retained_hdop=1 retained_hdop_flag=1 retained_accuracy=1 retained_corrections_active=1 retained_baseline_length=1 retained_baseline_status=1`. The reproducer exited 0. The current `gnss_core_test_runtime_aggregator` also passed 1/1, including its preservation-without-value-flags behavior.

Remaining defect, if any: A fresh explicit invalidation cannot clear direct coordinates, optional capability values, value-availability flags, or capability metadata. Old position, accuracy, correction, baseline, and other enrichment can coexist with a newer no-fix/update state. Desired retention across unrelated partial sentences remains necessary, but the model provides no representation by which parsers can request the opposite behavior.

Recommended next action: In a separate implementation task, add per-field tri-state update intent or explicit clear masks with timestamp/version ordering, preserve omission as no-op, and add set/omit/clear plus out-of-order-clear regressions for coordinates, accuracy, satellites/CN0, correction flags, baseline, and RF fields.

### UGA-022 — ASCII parsers accept NaN/Inf as valid GNSS values

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: Both textual-parser `TryParseDouble` helpers now reject `!std::isfinite(parsed)` immediately after their existing `std::strtod` syntax/overflow checks (`gnss_protocols/src/nmea_parser.cpp` and `gnss_protocols/src/unicore_parser.cpp`). This shared conversion gate precedes field-specific parsing and runtime mapping, so no parsed ASCII non-finite value reaches normalized runtime state. Valid finite-number and existing per-field validation behavior are otherwise unchanged.

Files changed: `gnss_protocols/src/nmea_parser.cpp`, `gnss_protocols/src/unicore_parser.cpp`, `gnss_driver/tests/test_nmea_session.cpp`, `gnss_driver/tests/test_unicore_session.cpp`, `TODO.md`, and this delta report.

Regression tests added: `TestNonFiniteGgaValuesAreRejectedBeforeRuntimeMerge` and `TestNonFiniteBestNavValuesAreRejectedBeforeRuntimeMerge`.

Pre-fix failure evidence: Both targeted session tests failed all six `nan`/`inf` variants because the parsed value generated a second runtime update.

Production fix: Add the finite-value requirement to each shared textual `TryParseDouble` helper.

Post-fix validation: NMEA/Unicore parser tests and both driver session tests pass with each non-finite value rejected before runtime merge.

Sanitizer result: The parser and session coverage passes under ASan+UBSan with no diagnostic.

Relevant changes since old audit: The production-base comparison had no numeric-parser change. This fix adds the finite check to the two shared ASCII helpers and focused runtime-session regression coverage in `gnss_driver/tests/test_nmea_session.cpp` and `gnss_driver/tests/test_unicore_session.cpp`.

Reproduction/test result: Added `TestNonFiniteGgaValuesAreRejectedBeforeRuntimeMerge` and `TestNonFiniteBestNavValuesAreRejectedBeforeRuntimeMerge`. Each feeds a valid baseline observation followed by checksum/CRC-valid textual `nan`, `+nan`, `-nan`, `inf`, `+inf`, and `-inf` in a runtime-producing numeric field, then asserts one parsed update, one rejection, and retention of the prior finite state. Before the production fix, both targeted session tests failed all six variants because each created a second runtime update. After the fix, `gnss_protocols_test_nmea_parser`, `gnss_protocols_test_unicore_ascii_parser`, `gnss_driver_test_nmea_session`, and `gnss_driver_test_unicore_session` passed normally and with ASan+UBSan (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`), with no sanitizer diagnostic.

Remaining defect, if any: None for non-finite values admitted through the NMEA and Unicore ASCII shared floating-point parsers. This deliberately does not introduce new finite physical-range policy beyond existing protocol rules.

Recommended next action: Keep these runtime-state regressions in the parser/session test lanes; assess independent binary-decoder finite handling only in a separately scoped finding.

### UGA-023 — malformed NMEA can consume the next valid sentence

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: While buffering a candidate, `NmeaSentenceFramer::PushByte` now treats `$` and `!` as a definitive resynchronization marker (`gnss_protocols/src/nmea_framer.cpp`): it discards the unfinished candidate, starts a one-byte candidate at the new leader, and stores that byte's timestamp. Newline completion, checksum validation, and ordinary valid-frame behavior are unchanged.

Files changed: `gnss_protocols/src/nmea_framer.cpp`, `gnss_protocols/tests/test_protocol_foundation.cpp`, `TODO.md`, and this delta report.

Regression test added: `TestNmeaFramerResynchronizesOnNestedLeader`.

Pre-fix failure evidence: The foundation test failed all four `$`/`!` truncation and garbage-prefix vectors because the following valid sentence became an invalid combined candidate with the old timestamp.

Production fix: Replace an unfinished candidate at either legal NMEA leader, preserving the new leader's timestamp.

Post-fix validation: The foundation, NMEA parser, and relevant NMEA/Unicore session tests pass with exactly one valid following record.

Sanitizer result: Foundation, NMEA parser, and NMEA session coverage pass under ASan+UBSan with no diagnostic.

Relevant changes since old audit: The production-base comparison had no NMEA framing change. This fix changes only the nested-leader branch in `gnss_protocols/src/nmea_framer.cpp` and adds a focused framer regression in `gnss_protocols/tests/test_protocol_foundation.cpp`.

Reproduction/test result: Added `TestNmeaFramerResynchronizesOnNestedLeader`, covering truncated `$` followed by valid `$`, truncated `$` followed by valid `!`, truncated `!` followed by valid `$`, and a truncated candidate plus garbage before a valid `$`. The test asserts exactly one resulting record, valid checksum, leader/talker/type extraction, and timestamp from the new leader. Before the production fix, all four vectors failed because the following sentence was emitted as an invalid combined candidate using the old timestamp. After the fix, `gnss_protocols_test_foundation`, `gnss_protocols_test_nmea_parser`, `gnss_driver_test_nmea_session`, and `gnss_driver_test_unicore_session` passed; foundation, NMEA parser, and NMEA session also passed with ASan+UBSan (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`) without diagnostics.

Remaining defect, if any: None for a legal NMEA `$`/`!` start marker arriving before the prior candidate's line ending. The discarded prefix is intentionally not emitted as a second invalid record, preventing duplicate downstream records.

Recommended next action: Keep the mixed-leader/timestamp resynchronization regression in the NMEA framer test lane.

### UGA-024 — corrupt binary lengths swallow a complete following frame

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: Each binary framer now reaches its normal declared boundary and validates the enclosing frame before it considers a later boundary. Only after an invalid UBX checksum, RTCM CRC24Q, or N4 CRC32 does `FindEmbeddedValidFrame` scan the already bounded candidate buffer for the earliest fully present, integrity-valid frame (`gnss_protocols/src/ubx_framer.cpp` lines 78-164; `gnss_protocols/src/rtcm_framer.cpp` lines 74-161; `gnss_protocols/src/unicore_binary_framer.cpp` lines 134-230). UBX requires its two-byte sync, in-range declared length, and checksum; RTCM additionally preserves its reserved-bit check and CRC24Q; N4 requires its full three-byte sync, in-range declared length, and CRC32. Buffered per-byte receipt timestamps preserve the recovered frame's own leader timestamp. The scan is bounded by each existing `max_frame_length_`, and it never runs for a checksum/CRC-valid enclosing frame, so legal sync-like payload bytes remain payload.

Relevant changes since old audit: No commit from `804bed8d7f753f6834212cfbc9dc329f88360299` to `aaacc6be92463ad493d6f4260426cf645188078f` touched UBX, RTCM3, or Unicore N4 framing/recovery; the RTCM 1230 semantic-health work occurs after framing. This fix changes only the three framers, their private framing state, their focused tests, and this report.

Files changed: `gnss_protocols/include/universal_gnss_protocols/ubx_framer.hpp`, `gnss_protocols/include/universal_gnss_protocols/rtcm_framer.hpp`, `gnss_protocols/include/universal_gnss_protocols/unicore_binary_framer.hpp`, their three matching `src/*_framer.cpp` implementations, `gnss_protocols/tests/test_protocol_foundation.cpp`, `gnss_protocols/tests/test_unicore_binary_framer.cpp`, and this delta report.

Regression tests added: `TestUbxFramerRecoversFollowingValidFrameAfterCorruptLength`, `TestRtcmFramerRecoversFollowingValidFrameAfterCorruptLength`, and `TestCorruptLengthRecoversFollowingValidFrame`. Each mutates an originally short candidate's declared length to cover a following valid frame, then corrupts the enclosing integrity trailer. They cover immediate and partially truncated prefixes, one-byte fragmentation, normal bad-integrity-followed-by-valid behavior, valid payloads containing the native sync sequence, and exactly-once delivery.

Pre-fix failure evidence: Before the production change, `ctest --test-dir /tmp/universal-gnss-current-delta-build -R '^(gnss_protocols_test_foundation|gnss_protocols_test_unicore_binary_framer)$' --output-on-failure` failed both tests. Foundation reported six failed UBX/RTCM recovery assertions (immediate, truncated-prefix, and one-byte-fragmented input); N4 reported three equivalent failures. The old UBX/RTCM results contained only an invalid enclosing record, while N4 returned `InvalidData` and emitted no following frame.

Production fix: On an integrity failure, inspect each possible later sync offset in the retained bounded buffer and return the first candidate whose complete, protocol-specific header, length limits, and checksum/CRC validate. Reset only after that decision; if no credible candidate exists, retain the old invalid-result behavior. Normal valid frames are emitted unchanged without scanning, and recovered frames are built from their own subrange and leader timestamp.

Post-fix validation: The targeted two-test CTest selection passes 2/2; all `gnss_protocols` tests pass 18/18; the existing combined ASan+UBSan build (`/tmp/universal-gnss-fix-asan`, with leak detection disabled) passes the two affected tests 2/2 with no sanitizer diagnostic; and the complete non-ROS CTest passes 61/61. The complete suite needed to run outside the sandbox for its local socketpair/loopback tests; the sandbox-only failure was limited to those unrelated transport tests.

Reproduction/test result: The three regressions now emit exactly one valid following UBX, CRC-valid RTCM 1005, or CRC-valid N4 message 520 after their corrupt declared-length candidate. The tests also prove a legal valid frame containing `0xB5 0x62`, `0xD3`, or the N4 three-byte sync remains one valid original frame rather than an early resynchronization.

Remaining defect, if any: None for a following complete frame within a candidate that reaches integrity validation. A candidate that has not reached its declared boundary remains indistinguishable from a valid long frame containing a byte-identical nested frame; deliberately switching before integrity validation would violate binary payload transparency and is not performed.

Recommended next action: Keep the three protocol-specific recovery regressions and their valid-payload sync cases in the framing suites; add property/fuzz coverage for many sync offsets separately if broader malformed-stream hardening is desired.

### UGA-025 — checksum-free text gets high-confidence Unicore discovery

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: Both `StreamDetector::Detect` and `CountUnicoreAsciiRecords` now call the shared private `detail::IsVerifiedUnicoreAsciiRecord` predicate (`gnss_driver/src/unicore_ascii_validation.hpp`). It requires a `#` leader, a valid CRC32, a supported record name, and a successful corresponding Unicore ASCII parser result before producing Unicore evidence. The existing score and confidence logic can therefore award +100 only after integrity and semantic validation; generic NMEA and binary predicates are unchanged.

Relevant changes since old audit: No production-base commit tightened ASCII Unicore detection. This fix changes `gnss_driver/src/stream_detector.cpp`, `gnss_driver/src/receiver_discovery.cpp`, adds their shared private validation helper, and adds/updates focused discovery coverage.

Regression test added: `TestUnicoreAsciiDiscoveryRequiresVerifiedPlausibleEvidence` in `gnss_driver/tests/test_receiver_discovery.cpp` exercises both public APIs with a supported name plus missing CRC/garbage payload, bad CRC, CRC-valid malformed payload, truncation, and unrelated text containing `#`; it also verifies a CRC-valid parseable BESTNAVA record and a noisy-prefix-plus-valid-record stream.

Pre-fix failure evidence: Before the production change, the targeted `gnss_driver_test_receiver_discovery` CTest failed nine assertions: each complete unverified/malformed input selected `unicore_ascii` and high-confidence Unicore discovery, and a leading garbage record made `StreamDetector` select it before the following valid record.

Production fix: `IsVerifiedUnicoreAsciiRecord` first rejects non-`#` and non-CRC-valid frames, then dispatches each supported output type to its existing parser and accepts only `kRecordReady` with a record. Unsupported or malformed CRC-valid text is not evidence.

Post-fix validation: The focused discovery and foundation tests pass. All five unverified/malformed vectors remain `kUnknown` with no Unicore evidence; a structurally valid CRC-verified BESTNAVA, including after the noisy prefix, is `unicore_ascii`/high-confidence Unicore with exactly one ASCII record. The complete `gnss_driver` CTest suite passed 19/19 and complete non-ROS CTest passed 61/61.

Sanitizer result: Not separately run; this is a bounded framed-text validation change and the targeted, component, and complete non-ROS suites passed.

Remaining defect, if any: None for high-confidence discovery from the supported ASCII runtime record set. Documented CRC-omitting Unicore outputs are deliberately not sufficient by themselves to select the vendor family at high confidence; they require separate corroborating evidence.

Recommended next action: Keep the missing/bad-CRC and CRC-valid-malformed cases beside the valid-stream cases whenever new supported Unicore ASCII records are added.

### UGA-026 — serial “raw mode” inherits stop-bit and flow-control flags

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `ConfigureRawMode` in `gnss_transport/src/posix_serial_transport.cpp` now clears `IXON|IXOFF|IXANY`, `CSTOPB`, and `CRTSCTS` where the platform exposes it, before explicitly setting `CS8|CLOCAL|CREAD`. This makes the documented GNSS serial framing deterministic as 8N1 with no software or hardware flow control while preserving the other existing raw-mode and configurable behavior.

Relevant changes since old audit: No production-base commit changed terminal raw-mode masks, framing, or flow-control policy. This fix changes only `gnss_transport/src/posix_serial_transport.cpp` and adds the focused pty regression to `gnss_transport/tests/test_posix_serial_transport.cpp`.

Files changed: `gnss_transport/src/posix_serial_transport.cpp`, `gnss_transport/tests/test_posix_serial_transport.cpp`, and this delta report.

Regression test added: `TestOpenClearsInheritedRawModeFlags`.

Pre-fix failure evidence: The pty test seeded `CSTOPB`, `CRTSCTS`, `IXON`, `IXOFF`, and `IXANY`, then opened the slave through `PosixSerialTransport`; the targeted CTest failed because the final termios state retained incompatible framing/flow-control flags.

Production fix: Clear all five inherited flag classes in `ConfigureRawMode`, conditionally clearing `CRTSCTS` only where it is supported.

Post-fix validation: The targeted pty test passes and asserts 8N1 plus disabled software/hardware flow control. Complete `gnss_transport` CTest passed 3/3 and complete non-ROS CTest passed 61/61.

Sanitizer result: Not separately run; this is a deterministic termios-state regression and the requested transport/full-suite validation passed.

Remaining defect, if any: None for inherited `CSTOPB`, `CRTSCTS`, `IXON`, `IXOFF`, or `IXANY` on the supported POSIX path. Flow-control modes remain unsupported rather than implicitly inherited.

Recommended next action: Keep the seeded-pty state assertion in the transport test lane for future termios changes.

### UGA-027 — serial timeout conversion wraps instead of rejecting/clamping

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: The POSIX transport now defines the maximum blocking timeout from `std::numeric_limits<cc_t>::max()` and validates it before opening/configuring a serial device (`gnss_transport/src/posix_serial_transport.cpp`). The 1–100 ms rounding uses `(timeout_ms - 1) / 100 + 1`, avoiding the old `+99` overflow; conversion to `cc_t` happens only after representability validation. `PosixSerialConfig` documents that unrepresentable blocking values are rejected by `Open()`.

Relevant changes since old audit: No production-base commit changes serial timeout conversion, input validation, or API documentation. This fix changes only the POSIX serial transport implementation/header and its focused pty test.

Files changed: `gnss_transport/src/posix_serial_transport.cpp`, `gnss_transport/include/universal_gnss_transport/posix_serial_transport.hpp`, `gnss_transport/tests/test_posix_serial_transport.cpp`, and this delta report.

Regression test added: `TestReadTimeoutConversionRejectsUnrepresentableValues`.

Pre-fix failure evidence: The targeted pty CTest accepted both 25,600 ms and `UINT32_MAX` instead of returning `kInvalidArgument`; on this platform the former reproduced the known `VTIME=1` (100 ms) wrap.

Production fix: Reject nonblocking-disabled timeouts above the platform `VTIME` range before opening, and calculate rounded deciseconds with overflow-safe subtraction/division/addition before the checked cast.

Post-fix validation: 0, 1, 99, 100, and 25,500 ms open with the expected `VMIN`/`VTIME`; 25,600 ms and `UINT32_MAX` are rejected and leave the transport closed. The complete `gnss_transport` suite passed 3/3 and complete non-ROS CTest passed 61/61.

Sanitizer result: Not separately run; this is a bounded configuration-conversion regression and the requested transport/full-suite validation passed.

Remaining defect, if any: None for the POSIX `VTIME` configuration path. The public 32-bit range is not fully realizable as a terminal-driver timeout; values above the documented platform range now fail explicitly rather than changing to a short timeout.

Recommended next action: If a future API requires timeouts beyond the terminal driver's range, implement them separately with `poll`/`ppoll` and a monotonic deadline rather than broadening `VTIME` casting.

### UGA-028 — adopted socket writes can terminate the process with SIGPIPE

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `AdoptConnectedSocket` now sets `use_generic_fd_io_ = false`, matching `Open`. Both public socket-construction paths therefore use `recv`/`send`; the existing Linux `send` branch supplies `MSG_NOSIGNAL`, so a broken socket reports the normal write error without requiring a process-global SIGPIPE policy.

Relevant changes since old audit: No production-base commit changes normal or adopted socket I/O, `MSG_NOSIGNAL`, or signal policy. This fix changes only the adopted-socket I/O selection and focused TCP transport coverage.

Regression test added: `TestClosedPeerWritesDoNotRaiseSigpipe` in `gnss_transport/tests/test_tcp_client_transport.cpp`. A forked child restores `SIGPIPE` to `SIG_DFL`, adopts one end of a closed `SOCK_STREAM` pair, writes, and succeeds only if `Write` returns `kWriteFailure` with the corresponding metric. The same test creates a synchronized loopback TCP listener, opens it through `TcpClientTransport::Open`, abortively closes the accepted peer, then verifies the same survival/error contract for the normal path.

Pre-fix failure evidence: With the newly added adopted-socket child test and default SIGPIPE disposition, the targeted CTest failed with `wait status=13`; the child was terminated by SIGPIPE before `Write` could return. This reproduced the audit defect. The normal-path source branch was independently `send(MSG_NOSIGNAL)` before the fix.

Production fix: Stop routing the documented adopted *socket* API through the generic `::read`/`::write` branch; route it through the existing socket-safe `recv`/`send(MSG_NOSIGNAL)` branch instead. No partial-write, nonblocking, or error-mapping logic changed.

Post-fix validation: The focused subprocess test passed for both adopted and normally opened closed-peer sockets, proving child-process survival and `kWriteFailure`. Complete `gnss_transport` CTest passed 3/3; complete `gnss_ntrip` CTest passed 7/7; complete non-ROS CTest passed 61/61; relevant ROS2 `test_ntrip_node` passed 1/1.

Sanitizer result: Not separately run; this is a process-signal/socket-system-call regression. The subprocess test used the default SIGPIPE disposition and passed under the normal system socket environment.

Remaining defect, if any: None for the Linux `TcpClientTransport` normal and adopted socket APIs. The direct loopback portion of the regression is skipped only in the restricted file-system sandbox because AF_INET socket creation is denied (`EPERM`); it passed when run outside that sandbox. A future public generic-file-descriptor API must remain separate rather than reusing `AdoptConnectedSocket`.

Recommended next action: Retain the default-disposition subprocess regression whenever transport I/O ownership paths change.

### UGA-029 — malformed HTTP status codes with a `200` prefix are accepted

Status: **FIXED**

Confidence: **Confirmed**

Current evidence: `HasExactNtripStatusCode` in `gnss_ntrip/src/ntrip_client.cpp` now requires an exact `200` token after one of the currently supported `ICY `, `HTTP/1.0 `, or `HTTP/1.1 ` prefixes, followed by a legal space delimiter or end of the status line. `IsAcceptedNtripStatusLine` and legacy-ICY payload detection use that shared check, so malformed tokens cannot reach the `kStreaming` transition or the RTCM monitor.

Relevant changes since old audit: The only production-base NTRIP-adjacent change was RTCM 1230 correction-health semantics, not response grammar. This fix changes only `gnss_ntrip/src/ntrip_client.cpp` and focused coverage in `gnss_ntrip/tests/test_ntrip_client.cpp`.

Files changed: `gnss_ntrip/src/ntrip_client.cpp`, `gnss_ntrip/tests/test_ntrip_client.cpp`, and this delta report.

Regression test added: `TestNtripStatusCodeTokenValidation`.

Pre-fix failure evidence: The targeted NTRIP client CTest failed four malformed-success cases: `HTTP/1.1 2000`, `HTTP/1.1 200X`, and `ICY 200anything` with both full-header and legacy-ICY payload layouts. Each incorrectly entered streaming and consumed the RTCM-looking suffix.

Production fix: Replace unrestricted successful-status prefix checks with exact-token validation; reject code suffixes by requiring `200` followed only by a space or status-line end. Reuse the check before legacy ICY payload recognition.

Post-fix validation: HTTP/1.0 200, HTTP/1.1 200 (including status-line end), and ICY 200 remain streaming responses and preserve first-read RTCM payload. `2000`, `200X`, truncated `20`, HTTP 401, and both malformed ICY layouts return the existing `kHttp` error, transition to `kFailed`, return zero payload bytes, and record no RTCM frame. Targeted client CTest passed; complete `gnss_ntrip` CTest passed 7/7; complete non-ROS CTest passed 61/61; ROS2 `test_ntrip_node` passed 1/1.

Sanitizer result: Not separately run; this is a bounded protocol-token parser regression and the requested NTRIP, non-ROS, and ROS2 validation passed.

Remaining defect, if any: None for the currently supported HTTP/1.0, HTTP/1.1, and ICY successful status-line forms. Other HTTP versions remain unsupported and continue to take the existing protocol-error path.

Recommended next action: Keep exact-token and legacy-ICY payload regressions in the NTRIP client test lane.
