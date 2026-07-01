# MowgliNext TODO — Universal GNSS integration

## Universal GNSS correction observability

Universal GNSS now exposes portable RTCM correction-stream observability, including RTCM MSM summary information.

Universal GNSS now also projects that shared RTCM semantic surface through ROS2
`/diagnostics` from both `NtripNode` and `ReceiverNode` under stable
`rtcm_semantic/*` entries.

MowgliNext must consume and display this information so robot operators can distinguish between:

* GNSS receiver health
* RTK fix state
* NTRIP/correction-stream health
* RTCM message completeness
* MSM constellation availability
* malformed/decode failures

## Tasks

### 0. Pending MowgliNext work — consume ROS2 RTCM semantic diagnostics

What new Universal GNSS capability exists:

* Universal GNSS now publishes machine-readable ROS2 diagnostics for:
  * `rtcm_semantic/base_station_arp`
  * `rtcm_semantic/glonass_code_phase_bias`
  * `rtcm_semantic/msm_summary`
  * per-message MSM entries such as `rtcm_semantic/msm_gps_msm7`
* These diagnostics expose fields such as:
  * `seen`
  * `decoded`
  * `valid`
  * `decode_success_count`
  * `decode_failure_count`
  * `malformed_count`
  * `message_type`
  * `station_id`
  * `constellations_seen`
  * `satellite_count`
  * `signal_count`
  * `cell_count`
  * `age_ns`

Why it matters for the robot:

* operators can distinguish "receiver has a fix" from "corrections are healthy"
* localization debugging can detect missing/stale/malformed correction streams
  before RTK silently degrades

Where MowgliNext should consume it:

* GPS sidecar diagnostics aggregation
* robot diagnostics API/websocket layer if one exists
* operator GUI GNSS/corrections panel

Expected GUI/operator behavior:

* show base-station metadata separately from rover fix state
* show whether `1230` has been seen/decoded and whether malformed counts are rising
* show MSM presence, last station id, constellations seen, and last aggregate counts
* avoid collapsing these states into a single "RTK OK" badge

Expected safety/localization behavior if relevant:

* stale or malformed correction diagnostics should be visible to operators before
  localization confidence is reduced
* if MowgliNext gates autonomy/localization modes on GNSS quality, it should use
  these diagnostics as correction-health evidence rather than inventing its own
  RTCM parser

Suggested field-validation checks:

* verify `NtripNode` and `ReceiverNode` both publish the expected
  `rtcm_semantic/*` diagnostics
* verify `station_id` and `constellations_seen` update on live correction streams
* verify malformed counters rise on intentionally bad/truncated RTCM captures
* verify dashboards remain stable when one constellation is absent from the NTRIP
  stream
* verify GUI/API tolerate optional per-message MSM entries appearing only when
  seen

### 1. Update Universal GNSS dependency

* [ ] Update the Universal GNSS version/submodule/branch used by MowgliNext.
* [ ] Confirm the integrated version includes portable RTCM MSM summary support.
* [ ] Rebuild MowgliNext ROS2 stack against the updated Universal GNSS API.
* [ ] Verify existing GNSS launch files still work.

### 2. Consume RTCM/NTRIP diagnostics

* [ ] Identify where `ReceiverNode` and `NtripNode` diagnostics are consumed in MowgliNext.
* [ ] Surface correction-stream health separately from receiver position/fix health.
* [ ] Track and display:

  * `rtcm.stream_active`
  * `rtcm.stream_stale`
  * `rtcm.required_messages_missing`
  * `rtcm.required_messages_pending`
  * `rtcm.1230_malformed`
  * `rtcm.1230_not_valid`
  * `rtcm.msm_malformed`

### 3. Display MSM observability

* [ ] Add operator-visible fields for RTCM MSM summary:

  * MSM seen / not seen
  * last MSM age
  * last station ID
  * constellations seen
  * last MSM message type
  * MSM variant
  * satellite count
  * signal count
  * cell count
  * decode success count
  * decode failure count
  * malformed count

### 4. GUI / dashboard integration

* [ ] Add a GNSS correction panel or extend the existing GNSS panel.
* [ ] Separate these states visually:

  * receiver connected
  * GNSS fix valid
  * RTK Float / RTK Fixed
  * NTRIP connected
  * RTCM stream active
  * MSM corrections present
  * required correction messages missing
* [ ] Add a warning state when RTCM stream is active but MSM is missing.
* [ ] Add a warning state when MSM malformed count increases.
* [ ] Add a warning state when correction stream is stale.

### 5. Robot behavior hooks

* [ ] Decide whether navigation should be allowed with:

  * RTK Fixed only
  * RTK Float with degraded mode
  * GNSS fix without RTK
  * stale corrections
  * missing MSM
* [ ] Add or update localization safety logic to react to correction health.
* [ ] Prevent silent degradation where the robot appears healthy but correction stream is broken.
* [ ] Log correction health during missions for post-run debugging.

### 6. Field validation

* [ ] Test with local NTRIP caster.
* [ ] Test with public NTRIP caster.
* [ ] Test with LoRa RTCM path when available.
* [ ] Verify behavior when NTRIP is disconnected.
* [ ] Verify behavior when RTCM stream is stale.
* [ ] Verify behavior when only some constellations are present.
* [ ] Verify behavior during RTK Float → Fixed transitions.
* [ ] Verify behavior during Fixed → Float / correction loss transitions.

## Notes

Universal GNSS MSM support currently provides correction-stream observability only.

It does not yet expose detailed satellite/signal identities, pseudorange, carrier phase, Doppler, C/N0, or RTCM observation extraction into robot localization.

MowgliNext should treat MSM data as correction-stream diagnostics, not as navigation measurements.
