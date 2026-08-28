# Current-main audit delta: UGA-001 through UGA-007

Revalidation date: 2026-08-27 UTC

Production base: `aaacc6be92463ad493d6f4260426cf645188078f`

Checked-out `review` HEAD: `483a717b9a0fe1c7609d173fb1a8931cd2dfc1d9` (exactly one report-only commit above the production base, adding `UNIVERSAL_GNSS_AUDIT.md`)

Comparison base for the old audit: `804bed8d7f753f6834212cfbc9dc329f88360299`

The staged revalidation below covers UGA-001 through UGA-029. The fix continuations modify only UGA-001, UGA-022, UGA-023, UGA-026, and UGA-027; no other audit finding was changed.

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

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_driver/src/receiver_session_runner.cpp`, `ReceiverSessionRunner::StepOnce`, current lines 27-46 still calls `session_.FeedBytes(buffer.data(), read_result.bytes_read)` without the optional timestamp. `gnss_ros2/src/receiver_node.cpp`, `ReceiverNode::Impl::PublishNow`, current lines 1418-1436 still copies the state and assigns `owner_.now().nanoseconds()` whenever the state has no timestamp.

Relevant changes since old audit: The focused diff from `804bed8d...` to `aaacc6be...` for the runner, its header/test, ReceiverNode, and ReceiverNode tests is empty; their path logs contain no intervening commit. None of the 14 production-base commits changes timestamp capture or fallback behavior.

Reproduction/test result: A current-tree focused program linked against freshly built current libraries fed one valid GGA through `MemoryByteSource -> ReceiverSessionRunner -> ReceiverSession`. Command: `/tmp/universal-gnss-current-uga002-repro`. Result: `runtime_updates=1 fix=1 timestamp_present=0`. The inspected ROS path would therefore replace the missing timestamp with publication-time `owner_.now()`.

Remaining defect, if any: The live runner still records neither byte-receipt nor acquisition time, and the ROS layer still relabels such data with publish time. Backlogged/delayed observations remain indistinguishable from current observations in the public timestamp.

Recommended next action: In a separate implementation task, define receipt/measurement/public ROS clock semantics, capture time at read, propagate it through the session, and add a delayed-publication regression test.

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

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ntrip/src/ntrip_client.cpp` still sets `state_=kStreaming` as soon as a valid response header terminates, returns successful zero-byte reads without a state transition, and has no inactivity deadline in `Read`. In `gnss_ros2/src/ntrip_node.cpp`, `last_correction_activity_time_` is still assigned at current line 466 but never consumed; `first_streaming_time_` is still initialized only inside `read_result.bytes_read > 0` at lines 463-471; and `BuildHealthSummary` can report stale health but never disconnect/reconnect a silent `Streaming` client.

Relevant changes since old audit: The 14 commits from `804bed8d...` to `aaacc6be...` are Unicore configuration changes plus optional RTCM 1230 health. The focused diff/log for NTRIP client, NtripNode, and their tests contains no change. The commits do not affect transport/application liveness.

Reproduction/test result: `/tmp/universal-gnss-current-uga005-repro`, linked against freshly built current libraries, used an adopted socket pair, sent a complete `ICY 200` header and no payload, then read at simulated 1 s, 7 s, and 60 s timestamps. Result: `after_header_state=3 first_bytes=0 ... silent1=0 silent2=0 frames=0 connected=1`. State 3 is `kStreaming`; it remains connected after 60 seconds with no first RTCM frame.

Remaining defect, if any: Both header-only and post-frame silent streams still lack a forced liveness transition. The three-second node startup grace is additionally bypassed for a header-only response because its start time is never set. Diagnostic stale, correction availability, first-frame deadline, and reconnect deadline remain conflated/absent rather than independently controlled.

Recommended next action: In a separate implementation task, add distinct first-valid-frame and last-valid-frame liveness tracking plus a conservatively configured forced-reconnect threshold integrated with backoff; add header-only, frame-then-silence, legitimate-gap, and threshold-boundary tests.

### UGA-006 — reconnect backoff is reset before NTRIP success

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ntrip/src/ntrip_client.cpp` current lines 276-322 still calls `RecordReconnectSuccess` immediately after TCP `Open` or socket adoption. `RecordReconnectSuccess` at lines 845-862 still resets the reconnect state when `reset_after_success` is true. Authentication/header failures occur later through `FailWith` lines 711-723, so the new failure always starts from a reset state.

Relevant changes since old audit: The focused diff from `804bed8d...` to `aaacc6be...` for `ntrip_client`, reconnect policy, headers, and tests is empty. The intervening Unicore/RTCM-1230 commits do not alter the reconnect success boundary.

Reproduction/test result: `/tmp/universal-gnss-current-uga006-repro`, linked against current libraries, configured `initial_delay=100 ms`, multiplier 2, and `max_attempts=2`. It ran three independent TCP-success/HTTP-401 cycles. Each printed `attempts=1 delay_ms=100 exhausted=0`; the count never reached 2 and delay never reached 200 ms. Each failure correctly entered state 4 (`kFailed`), but the next socket adoption erased its backoff history before application success.

Remaining defect, if any: TCP connectivity is still treated as full NTRIP success before request/authentication/header/RTCM success. Repeated post-connect failures can retry forever at the minimum delay and evade `max_attempts`.

Recommended next action: In a separate implementation task, define and test an application-level success milestone for resetting backoff (not raw TCP connect), including repeated authentication/protocol failures and genuinely healthy flow recovery.

### UGA-007 — stale receiver state is kept fresh for NTRIP GGA by republishing

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ros2/src/receiver_node.cpp`, `ReceiverNode::Impl::OnTimer` and `PublishNow`, current lines 790-793 and 1418-1426 still publish the current state every timer tick even when `StepOnce()` produced no new observation. `gnss_ros2/src/ntrip_node.cpp`, `NtripNode::Impl::OnStatusMessage`, current lines 319-323 still replaces `last_status_time_` with callback time on every message. `MaybeInjectGga` at lines 516-530 and the diagnostic check at lines 727-732 still use only that callback age; neither checks the message timestamp, observation identity, nor upstream receiver progress.

Relevant changes since old audit: The focused diff and path log from `804bed8d...` to `aaacc6be...` for ReceiverNode, NtripNode, `GnssStatus.msg`, and their tests are empty. The intervening commits do not affect this cross-layer freshness path. The existing `DoesNotInjectGgaWhenStatusIsStaleAndReportsStaleSource` test still covers one message followed by 5.2 seconds of silence, not repeated identical republishes.

Reproduction/test result: A current-tree ROS2 build succeeded, then `/tmp/universal-gnss-current-uga007-repro` established an adopted NTRIP stream and published the exact same valid status seven times at 1.1-second intervals; its observation stamp remained fixed at one second. At the final repetition, about 6.6 seconds after the first callback, it printed `final_cycle_gga=1 stale_after_repeats=0`. The control phase stopped callbacks for 5.2 seconds and printed `stale_after_silence=1`. Thus callback repetition, not observation age, both suppresses the stale diagnostic and preserves GGA eligibility.

Remaining defect, if any: An unchanged receiver observation can still be kept indefinitely fresh by the ReceiverNode publication timer, so stale coordinates remain eligible for caster GGA. The inverse fixed five-second callback threshold also remains independent of a legitimate receiver's configured observation cadence.

Recommended next action: In a separate implementation task, carry and validate observation/receipt freshness independently of publish cadence, and add the repeated-identical-status regression plus slow-valid-source and exact-boundary cases.

### UGA-008 — semantically malformed RTCM satisfies correction health

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_protocols/src/rtcm_correction_monitor.cpp`, `RtcmCorrectionMonitor::ObserveFrame`, current lines 284-357 still performs the specialized 1005/1006, 1230, and MSM decodes but calls `RecordValidMessage` unconditionally afterward. `RecordValidMessage` at lines 773-809 records framed message-type/constellation activity, and `HasRequiredCorrectionMessages` at lines 590-688 consumes that activity rather than the decoded-valid timestamps. `BuildRtcmCorrectionHealth` at lines 812-917 still defines `parser_healthy` solely as `invalid_frames()==0`, independently emits malformed warnings, and can set `correction_available=true` for those same malformed records.

Relevant changes since old audit: Commit `3495ffb` changed only the portable preset from `require_glonass_bias=true` to false and downgraded a successfully decoded but validity-bit-cleared 1230 diagnostic to informational. It fixes the separate universal-1230 requirement, but does not move activity recording after semantic decode, gate any configured requirement on decode success, or include malformed known records in `parser_healthy`. No other intervening commit touches this path.

Reproduction/test result: `/tmp/universal-gnss-current-uga008-repro`, compiled against the fresh current-base protocol library, supplied checksum-status-valid two-byte payloads containing only each message type. Truncated 1005+1077 and 1006+1077 both printed `arp_decoded=0 msm_decoded=0 required=1 parser_healthy=1 correction_available=1`; all 49 recognized MSM family/variant types printed collectively as `malformed_msm_satisfying=49/49`. A truncated 1230 with explicit `require_glonass_bias=true` printed `decoded=0 malformed=1 required=1 parser_healthy=1 correction_available=1`. The existing current monitor test binary also passed, showing its malformed tests assert diagnostics/counters but not rejection from requirements.

Remaining defect, if any: CRC-valid framing is still treated as required semantic activity even when the implemented decoder rejects the payload. Malformed 1005/1006 and every recognized MSM class still satisfy portable availability; malformed 1230 still satisfies any profile that explicitly requires GLONASS bias. `parser_healthy` and `correction_available` can both remain true despite those decode failures.

Recommended next action: In a separate implementation task, track decoded-valid semantic activity separately from framed-valid activity, base semantic requirements on the former, define how malformed known records affect parser health, and add rejection tests for both base types, all MSM families/variants, and explicitly required 1230.

### UGA-009 — partial nonblocking RTCM writes discard an already-started frame

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ros2/src/receiver_node.cpp`, `ReceiverNode::Impl::OnRtcmMessage`, current lines 815-850 still keeps the write offset only in a callback-local lambda. It stops on an otherwise successful zero-byte write, records an error, and retains no suffix or queue for a later callback. `gnss_transport/src/tcp_client_transport.cpp`, `TcpClientTransport::Write`, current lines 442-473 still legally maps nonblocking `EAGAIN`/`EWOULDBLOCK` to `{0, kOk, kNone}`. The next RTCM callback therefore begins its new frame after any prefix already emitted by the failed callback.

Relevant changes since old audit: The focused old-to-current diff and path log for ReceiverNode, its ROS tests, TCP/serial transports, and byte-stream interfaces are empty. None of the 14 intervening commits adds persistent forwarding state, writable-event retry, or whole-frame queueing. The current ROS test still uses `MemoryByteDuplex`, whose writes always complete, so it does not exercise this path.

Reproduction/test result: `/tmp/universal-gnss-current-uga009-repro`, compiled and linked against the current ROS2 build, injected a `ByteDuplex` that wrote a three-byte prefix, then returned the same zero-progress success used for nonblocking would-block. After the first eight-byte frame it printed `after_first_bytes=3 calls_after_first=2`; an intervening `StepOnce`/`PublishNow` left `after_idle_bytes=3`. Publishing the following eight-byte frame produced `final_bytes=11 complete_ordered_size=16 prefix_then_next=1`: the first five-byte suffix was never retried, and the second frame immediately followed the orphaned prefix.

Remaining defect, if any: Once any frame prefix has reached a stream sink, ordinary backpressure can still discard its suffix. No callback-independent state preserves the remainder, and later frames are not ordered behind it, so the receiver observes a corrupt concatenation rather than whole-frame loss.

Recommended next action: In a separate implementation task, introduce a bounded persistent whole-frame queue with offsets and retry scheduling, document overflow/drop behavior, and test every split point, zero progress, recovery, terminal errors, and ordering of consecutive frames.

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

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ros2/src/ntrip_node.cpp` lines 164-168 defines `MonotonicNowNs()` from `steady_clock`; `StepOnce` passes that value into the NTRIP client at lines 325-340, and `ReadOnce` copies the resulting frame timestamp directly into public `RtcmFrame.stamp` at lines 455-488. In contrast, the same node stamps the diagnostic array header with its ROS clock at lines 346-360. `gnss_ros2/msg/RtcmFrame.msg` still exposes only an unqualified `builtin_interfaces/Time stamp`. `gnss_ros2/src/receiver_node.cpp` lines 270-280 accepts every nonzero public stamp as a protocol timestamp; lines 855-872 record it unchanged, while lines 1440-1444 calculate semantic ages using the receiver's local monotonic clock. Finally, `gnss_protocols/src/rtcm_correction_monitor.cpp` lines 81-102 subtracts timestamps without rejecting negative ages and checks only the lower observation-window bound; lines 886-920 therefore do not mark a future frame stale and can report corrections available. This traces two distinct domains end-to-end: the public ROS/system/sim-capable field and the local steady receipt clock are still conflated.

Relevant changes since old audit: The focused diff from `804bed8d7f753f6834212cfbc9dc329f88360299` to `aaacc6be92463ad493d6f4260426cf645188078f` is empty for `ntrip_node.cpp`, `receiver_node.cpp`, and `RtcmFrame.msg`. The only change in `rtcm_correction_monitor.cpp` is `3495ffb`, which makes RTCM 1230 optional and adjusts its diagnostic severity; it does not alter timestamp arithmetic, future rejection, or clock-domain handling. The commits therefore do not affect this finding.

Reproduction/test result: `/tmp/universal-gnss-current-uga014-repro` used the current ROS2 libraries, a socket-pair NTRIP stream carrying a fully decodable CRC-valid 1006 frame, a real `RtcmFrame` publisher, `ReceiverNode`, and a direct monitor scenario. The NTRIP-published stamp was within 5,102,661 ns of local steady time but 1,787,867,443,985,476,510 ns from the node's ROS/system clock (`stamp_matches_steady=1`). Passing that frame to ReceiverNode produced a plausible same-host age of 59,330,128 ns. Republishing the identical valid frame with the current ROS/system stamp produced `system_to_receiver_age_ns=-1787867443928923000`. Independently, a future message timestamp gave `monitor_future_age_ns=-1795000000000000000`, yet `required=1`, `stale=0`, and `correction_available=1`. The reproducer exited 0. The existing focused `ReceiverNodeTest.ReceiverConsumesRtcmPublishedByNtripNode` also passed, confirming why the defect is masked when both components happen to share the same host steady epoch.

Remaining defect, if any: Public ROS/system or simulated timestamps, remote-host values, and replay-relative values can still be compared directly with local steady time. Future values yield negative ages, satisfy the lower-bound-only requirement window, and can keep correction freshness available indefinitely; NtripNode also continues to publish boot-relative steady values in a ROS `Time` field.

Recommended next action: In a separate implementation task, define the public `RtcmFrame.stamp` ROS clock semantics, capture a distinct local steady receipt timestamp for freshness, reject future/out-of-domain observations, and add cross-process system/steady/sim/replay plus zero, future, and out-of-order regression cases.

## Summary

| Finding | Status | Confidence |
|---|---|---|
| UGA-001 | FIXED | Confirmed |
| UGA-002 | STILL PRESENT | Confirmed |
| UGA-003 | STILL PRESENT | Confirmed |
| UGA-004 | FIXED | Confirmed |
| UGA-005 | STILL PRESENT | Confirmed |
| UGA-006 | STILL PRESENT | Confirmed |
| UGA-007 | STILL PRESENT | Confirmed |
| UGA-008 | STILL PRESENT | Confirmed |
| UGA-009 | STILL PRESENT | Confirmed |
| UGA-010 | ALREADY FIXED | Confirmed |
| UGA-011 | FIXED | Confirmed |
| UGA-012 | FIXED | Confirmed |
| UGA-013 | FIXED | Confirmed |
| UGA-014 | STILL PRESENT | Confirmed |
| UGA-015 | ALREADY FIXED | Confirmed |
| UGA-016 | STILL PRESENT | Confirmed |
| UGA-017 | STILL PRESENT | Confirmed |
| UGA-018 | STILL PRESENT | Confirmed |
| UGA-019 | STILL PRESENT | Confirmed |
| UGA-020 | STILL PRESENT | Confirmed |
| UGA-021 | STILL PRESENT | Confirmed |
| UGA-022 | FIXED | Confirmed |
| UGA-023 | FIXED | Confirmed |
| UGA-024 | STILL PRESENT | Confirmed |
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

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_protocols/src/rtcm_correction_monitor.cpp` lines 598-615 computes one `window_start_timestamp_ns` and applies it through the same `HasSeenSince` helper to both message types and MSM constellations. Dynamic MSM freshness is checked against that window at lines 633-657, while `require_base_position` checks the last 1005/1006 message timestamp against the identical window at lines 660-673. The requirement path does not consult the separately retained, successfully decoded `last_base_station_arp`; therefore static/session metadata and dynamic observations still have no distinct lifetime semantics. `gnss_ros2/src/ntrip_node.cpp` lines 251-256 fixes the requirement window at 30 seconds and lines 562-571 passes it unchanged into the portable preset. `gnss_tools/src/gnss_ntrip_monitor.cpp` lines 162-173 does the same. The current test `TestPortableRtkRequirementsUseRecentObservationWindow` at monitor-test lines 760-783 explicitly expects an old base-position message to expire and correction requirements to fail.

Relevant changes since old audit: The only commit touching the focused monitor implementation or tests between `804bed8d7f753f6834212cfbc9dc329f88360299` and `aaacc6be92463ad493d6f4260426cf645188078f` is `3495ffb9ba8eea8be8401c9736ff081e046063a7`. It changes the portable preset so RTCM 1230 is optional and makes a validity-cleared 1230 informational, but it does not change `ComputeObservationWindowStart`, `HasSeenSince`, the 1005/1006 base-position branch, the 30-second NtripNode window, or source/station lifetime tracking. Removing 1230 from the portable requirements narrows the required set but has no effect on expiration of 1005/1006.

Reproduction/test result: `/tmp/universal-gnss-current-uga016-repro` parsed a captured CRC-valid 1006 and verified one successful base-ARP decode at T=1 s, then supplied a structurally valid GPS MSM7/1077 and verified one successful MSM decode at T=31 s. Health was evaluated at T=32 s with the current portable 30-second requirement window and 5-second stream-stale threshold. Output was `base_decoded=1 base_retained=1 base_age_s=31 msm_decoded=1 msm_age_s=1 window_s=30`, followed by `require_glonass_bias=0 required=0 correction_available=0 stale_data=0 missing_required_event=1 stream_stale_event=0`; the reproducer exited 0. The current `gnss_protocols_test_rtcm_correction_monitor` also passed 1/1, including its existing assertion that base metadata outside the common recent window expires.

Remaining defect, if any: Fresh, successfully decoded MSM traffic still becomes correction-unavailable solely because the last valid and still-retained 1005/1006 is older than 30 seconds. There is no static/session metadata lifetime tied to correction-source or station identity, nor invalidation on such an identity transition; the retained decoded ARP and its recent-message requirement can therefore disagree exactly as reproduced.

Recommended next action: In a separate implementation task, retain decoded 1005/1006 validity per correction source/station session while keeping MSM freshness dynamic, explicitly invalidate retained metadata on source/station transitions, and add long-running MSM plus source-change regression scenarios.

### UGA-017 — RTCM rate history grows without bound and queries are linear

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_protocols/include/universal_gnss_protocols/rtcm_correction_monitor.hpp` current lines 163-170 still owns lifetime `std::vector<ProtocolTimestampNs>` histories for each message type, each MSM constellation, all frames, and all valid frames. `AppendTimestamp` at `gnss_protocols/src/rtcm_correction_monitor.cpp` lines 32-38 only calls `push_back`; the only clearing occurs on an explicit whole-monitor `Reset` at lines 248-261. `ObserveMessage` lines 360-366 appends every timestamp to total and valid histories through `RecordValidMessage`, whose lines 781-807 also append the same MSM observation to its type and constellation histories. No insertion path prunes by age or count. `CountTimestampsInWindow` lines 41-57 iterates from beginning to end for every query, and `TotalFrameRateHz`, `ValidFrameRateHz`, `MessageRateHz`, and `MsmConstellationRateHz` lines 731-770 all delegate to that lifetime scan even for a narrow recent window.

Relevant changes since old audit: The only focused commit between `804bed8d7f753f6834212cfbc9dc329f88360299` and `aaacc6be92463ad493d6f4260426cf645188078f` is `3495ffb9ba8eea8be8401c9736ff081e046063a7`, which changes portable RTCM 1230 requirements and related diagnostics/tests. It does not change the history members, append paths, reset behavior, rate computation, or query complexity. No bounding or storage-semantic change affects UGA-017.

Reproduction/test result: `/tmp/universal-gnss-current-uga017-repro` instrumented the current class layout without changing repository code and inserted one timestamped GPS MSM7/1077 at a time up to 400,000 observations. At 50,000, 100,000, 200,000, and 400,000 observations, each of `total_frame_timestamps_`, `valid_frame_timestamps_`, the 1077 history, and the GPS constellation history exactly equalled the lifetime observation count; the 1077 vector capacity grew from 65,536 to 524,288. Process RSS rose from a 3,748 KiB baseline to 5,616, 7,340, 10,460, and 16,716 KiB. Fifty repeated 1077 rate queries over a deliberately tiny 1,000 ns window averaged 131.24, 247.7, 560.46, and 1,114 microseconds respectively as lifetime history increased eightfold. The invariant and RSS checks passed and the reproducer exited 0.

Remaining defect, if any: Memory still grows monotonically with timestamped RTCM input, with a typical MSM timestamp duplicated across four unbounded vectors. Rate-query work remains O(lifetime observations), not O(observations in the requested window), so diagnostic CPU cost also grows for the lifetime of a continuously running NTRIP/receiver process.

Recommended next action: In a separate implementation task, keep lifetime scalar counts but replace lifetime timestamp vectors with bounded time buckets, ring buffers, or insertion-time eviction sized to the largest supported rate window; add bounded-container and near-constant-query-cost soak regressions.

### UGA-018 — correction metadata has no endpoint/station ownership model

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ntrip/src/ntrip_client.cpp` current lines 252-259 lets `set_config` replace host, port, and mountpoint without closing the current transport, rebuilding the already-sent request, resetting the monitor, or recording a source transition. Both `Connect` lines 276-302 and `AdoptConnectedSocket` lines 305-322 unconditionally call the same `ResetSessionState`; that function at lines 726-733 calls `correction_monitor_.Reset()`, whose current lines 248-281 erase all activity and decoded 1005/1006, 1230, and MSM records without checking whether the endpoint or station is unchanged. Conversely, decoded base and MSM records expose independent `station_id` values (`rtcm_parser.cpp` lines 239-266 and 411-450), but `HasRequiredCorrectionMessages` at monitor lines 590-690 tests only recent message-type/constellation presence. No endpoint identity is stored in `RtcmCorrectionMonitor`, and no health branch compares base, MSM, or optional 1230 station identifiers.

Relevant changes since old audit: The focused old-to-current history contains only `3495ffb9ba8eea8be8401c9736ff081e046063a7` in the correction monitor. It makes 1230 optional and changes related diagnostics but adds no endpoint/source identity, station-coherence comparison, or reconnect retention policy. There are no changes to `NtripClient::set_config`, `Connect`, `AdoptConnectedSocket`, or `ResetSessionState`. The 1230 change reduces one required category but does not address ownership of the remaining base/MSM state or the unsafe interaction with unconditional retention contemplated by PR #5.

Reproduction/test result: `/tmp/universal-gnss-current-uga018-repro` used current NtripClient socket adoption and fully decodable RTCM. With endpoint A unchanged, valid base+MSM health was initially available; adopting a replacement socket with the same A configuration produced `same_endpoint_before=1 same_endpoint_after_reconnect=0 metadata_erased=1`. In a separate client, a decoded 1006 for station 1 plus decoded 1077 for station 2 yielded `ids_mismatch=1 mixed_station_health=1`. After sending an A request for `/MOUNT_A`, `set_config` changed the live client's identity to `caster-b.example:2201/MOUNT_B` while it remained connected, retained both A frames, and returned `source_b_health_from_a=1`; the stored request still identified `/MOUNT_A`. All assertions passed and the reproducer exited 0.

Remaining defect, if any: Same-endpoint/same-station reconnects still discard valid static metadata and incur avoidable unavailability, while inconsistent station IDs inside one session are accepted. A live endpoint configuration change can relabel retained source-A correction state as source B without a transport or monitor transition. UGA-016 cannot be safely fixed by unconditional retention until endpoint and station ownership plus invalidation semantics exist.

Recommended next action: In a separate implementation task, introduce a normalized `(host, port, mountpoint)` correction-source identity and decoded station identity, separate transport/dynamic resets from static metadata lifetime, retain only across a verified same-source reconnect, and invalidate immediately on endpoint or station changes with mixed-ID regressions.

### UGA-019 — “forwarding active” means “ever forwarded”

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: In `gnss_ros2/src/ntrip_node.cpp`, `BuildHealthSummary` current lines 707-713 emits the OK `rtcm_forwarding_active` event whenever lifetime `rtcm_published_frames_ > 0`; `AppendRtcmForwardingStatus` lines 771-810 uses the same predicate for `OK: RTCM forwarding active`. Although `last_rtcm_published_time_` is maintained and exposed as `last_frame_age_s` at lines 801-807, it is not used by either active predicate. ReceiverNode has the same split: successful writes update both lifetime counters and `last_rtcm_forward_time_` at `gnss_ros2/src/receiver_node.cpp` lines 848-852, but `BuildHealthSummary` lines 1094-1100 and the dedicated status lines 1170-1174 test only `rtcm_forwarded_frames_ > 0`; the age is merely reported at lines 1246-1252. Receiver-reported u-blox and Unicore active events at lines 1112-1147 likewise use lifetime accepted/status counters without a recent-use timestamp.

Relevant changes since old audit: The focused diff from `804bed8d7f753f6834212cfbc9dc329f88360299` to `aaacc6be92463ad493d6f4260426cf645188078f` is empty for NtripNode, ReceiverNode, and their tests. No recent commit changes forwarding counters, timestamp use, or active/idle semantics. The RTCM 1230 commit affects correction requirements and semantic severity only, not forwarding liveness.

Reproduction/test result: `/tmp/universal-gnss-current-uga019-repro` streamed one fully decodable CRC-valid MSM7/1077 through current NtripNode, forwarded the published frame through current ReceiverNode into a memory duplex, then allowed 5.2 seconds of silence—beyond the Ntrip correction stale threshold—before publishing diagnostics. Ntrip output was `ntrip_count=1 ntrip_age_s=5.21 ntrip_status_active=1 ntrip_active_event=1 ntrip_stale_event=1`: its dedicated status remained OK/active and its OK active event coexisted with the stale correction event. Receiver output was `receiver_count=1 receiver_age_s=5.205 receiver_status_active=1 receiver_active_event=1`. All assertions passed and the reproducer exited 0.

Remaining defect, if any: Every forwarding surface still conflates “has ever succeeded” with current activity. Silence after one success leaves contradictory or independently misleading OK/active diagnostics indefinitely; receiver-reported correction-use/status counters have the same lifetime-only problem.

Recommended next action: In a separate implementation task, define a forwarding/use freshness threshold, derive active state from the existing steady-clock last-success timestamps, retain lifetime totals under explicitly historical keys, and test timeout boundaries, silence, recovery, and success-followed-by-failure for both nodes and receiver-reported use.

### UGA-020 — fixed three-second receiver staleness rejects valid slow cadences

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: `gnss_ros2/src/receiver_node.cpp` current line 710 still declares one hard-coded `kStaleTimeout{3}`. Transport and runtime receipt times are updated from actual `StepOnce` byte/observation progress at lines 904-925, independently of publication, but `BuildHealthSummary` lines 1021-1050 applies the same three-second threshold to every transport and runtime stream. `HasFreshRuntimeState` lines 1498-1502 uses that constant again, and `CanPublishFixMessage` lines 1607-1615 suppresses fix publication whenever it expires. The only cadence parameter loaded at lines 480-507 is `publish_rate_hz`; it controls the wall timer at lines 783-793 but is neither an input observation-rate contract nor part of stale-timeout calculation. No stale-timeout or expected-observation-rate parameter is declared or derived from the active receiver/profile.

Relevant changes since old audit: The focused old-to-current diff and log are empty for ReceiverNode and its tests. No commit through `aaacc6be92463ad493d6f4260426cf645188078f` changes `kStaleTimeout`, adds a freshness parameter, or derives timeout from receiver configuration or observed input cadence. RTCM 1230 changes are unrelated.

Reproduction/test result: `/tmp/universal-gnss-current-uga020-repro` supplied two valid GPGGA runtime observations exactly four seconds apart through a healthy open source while configuring ROS publication at 20 Hz. The first observation produced a fix. At 3.1 seconds the source correctly had no new record; current diagnostics reported both `runtime_state_stale` and `transport_data_stale`, and `last_fix_message` was cleared: `first_observation=1 initial_fix=1 gap_read=0 gap_fix=0 runtime_stale_at_3_1s=1 transport_stale_at_3_1s=1`. At 4.1 seconds the next valid GGA was accepted and the fix recovered, with the stale condition emitted as cleared. The node reported neither `stale_timeout_s` nor `expected_observation_rate_hz` as parameters. All assertions passed and the reproducer exited 0.

Remaining defect, if any: Any otherwise healthy receiver whose relevant observation cadence exceeds three seconds is periodically diagnosed stale and has its fix suppressed. ROS publication rate remains independently configurable but cannot express or correct the input-cadence contract, so increasing it only evaluates the false-stale condition more often.

Recommended next action: In a separate implementation task, add/document an expected receiver-observation cadence or explicit stale timeout with a conservative jitter margin, keep it independent of `publish_rate_hz`, and test slow legal inputs across protocols at exact timeout boundaries and recovery.

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

Status: **STILL PRESENT**

Confidence: **Confirmed**

Current evidence: UBX and RTCM still calculate an announced total size from the header (`gnss_protocols/src/ubx_framer.cpp` lines 51-79; `gnss_protocols/src/rtcm_framer.cpp` lines 46-75), accumulate unconditionally until that size, build one frame, and reset. A checksum/CRC failure merely appears in the returned record; neither framer searches the failed candidate for a sync suffix. Unicore N4 does the equivalent at `gnss_protocols/src/unicore_binary_framer.cpp` lines 106-143: after the length-based accumulation, a bad CRC resets the entire buffer and returns `InvalidData`. None has a suffix replay or rescan path. The focused old-to-current diff is empty for all three framers.

Relevant changes since old audit: No commit from `804bed8d7f753f6834212cfbc9dc329f88360299` to `aaacc6be92463ad493d6f4260426cf645188078f` touches UBX, RTCM3, or Unicore N4 framing/recovery. RTCM's recent 1230 semantic-health change occurs after framing and does not change corrupt-length handling.

Reproduction/test result: `/tmp/universal-gnss-current-uga024-repro` embedded an exact complete, integrity-valid frame in each corrupt but in-range announced body, then supplied enough padding to reach the corrupt candidate boundary. Current output was `ubx_record_ready=1 ubx_valid=0 ubx_embedded_valid_lost=1`, `rtcm_record_ready=1 rtcm_valid=0 rtcm_embedded_valid_lost=1`, and `n4_record_ready=0 n4_valid=0 n4_invalid=1 n4_embedded_valid_lost=1`. The UBX/RTCM framers emitted only their invalid enclosing candidate; N4 returned one invalid-data event. No embedded valid frame was recovered. The reproducer exited 0.

Remaining defect, if any: All three binary protocols still lose a complete following valid frame after a plausible corrupt length. The exact reporting differs (invalid `RecordReady` for UBX/RTCM versus `InvalidData` for N4), but no protocol preserves a suffix for recovery.

Recommended next action: In a separate implementation task, retain the failed candidate long enough to replay the earliest viable sync suffix after integrity failure, with bounded work and explicit tests for every sync offset in UBX, RTCM3, and N4.

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
