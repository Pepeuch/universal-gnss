# UG-RTCM-GATEWAY-001 — optional RTCM correction-input gateway handshake

Lifecycle: RETAINED
Status: IMPLEMENTED (generic software) / HARDWARE_PENDING (worked example)
Baseline: `origin/main` 9a329cc; branch `feat/rtcm-gateway-handshake`

## Contract

- `gnss_transport::RtcmGatewayHandshake` (pure, clock-injected) owns gateway
  state (`off|idle|enabling|active|lost`) and emits command lines; it never
  writes. `ReceiverNode` owns parameters, a 100 ms tick (created only when
  enabled), FIFO queueing and the diagnostic projection.
- Demand = valid RTCM frame submitted for forwarding; lapses after
  `rtcm_forwarding_activity_timeout_s`. Not "frame written": with ack gating a
  written-frame trigger would deadlock.
- Commands go through `RtcmFrameWriter` as `control` frames: serialised with
  RTCM, never inside a frame, never counted as forwarded RTCM.
- Withheld frames (ack gating) are discarded and counted, never queued stale.
- Inbound matching: substring of an LF-terminated line (CR stripped, >256 B
  never matches) via read-only `ReceiverSessionRunnerConfig::received_bytes_observer`,
  installed only when a pattern is configured. Partial lines are dropped on
  transport loss.
- Transport loss -> `idle` without writing; next frame re-enables on the new
  incarnation. Shutdown -> disable commands, one best-effort flush.
- Off by default: no command configured => no writes, no timer, no new
  diagnostic keys or events.

## Evidence

- Unit: `gnss_transport_test_rtcm_gateway_handshake`, foundation control-frame
  test, `gnss_driver_test_receiver_session_runner` observer test.
- Node: `test_receiver_node` `*RtcmGateway*` (off, enable/keep-alive/disable,
  shutdown, ack gating/timeout/reset, parameter validation).
- Non-ROS CTest 69/69; colcon `universal_gnss_ros2` all gtests pass. The only
  colcon failure, `test_build_library_provenance` (`test_replay_node` does not
  load `libuniversal_gnss_ntrip.so` in a Release build), also fails at 9a329cc.

## Hardware plan (worked example only)

Hardware: Airseekers Tron, Unicore UM960 behind AT32 `rtk_rover` board, one UART.
Facts to verify independently: `$GNRTK,ON*69`+CRLF enables network RTK; ~4 s
keep-alive suffices; ~6 s silence returns to LoRa; LoRa mode blocks host bytes;
`$GNRTK,ON`/`$GNRTK,OFF` reports (cadence, checksum, whether ON is periodic);
vendor `RTKRESET` on ON->OFF edge. Acceptance: receiver reports RTK from NTRIP
with keep-alive config; LoRa RTK resumes after node stop. Config lives in
`docs/vendors/airseekers/rtk_rover_gateway.md`.

## Invalidation conditions

Change of `RtcmFrameWriter` FIFO semantics, `ReceiverSessionRunner` read path,
forwarding activity semantics, or hardware evidence contradicting the example
(update the example doc, not the code, unless a new generic option is needed).
