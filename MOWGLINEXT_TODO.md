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

### 0b. Pending MowgliNext work — consume generic NMEA RTK mode from `/gps/status`

What new Universal GNSS capability exists:

* Universal GNSS now maps standard NMEA `GGA fix_quality` into normalized
  `rtk_mode` on the generic `receiver_family=nmea` path.
* This means `/gps/status` can now report:
  * `RTK_MODE_NONE`
  * `RTK_MODE_FLOAT`
  * `RTK_MODE_FIXED`
  without requiring a dedicated vendor backend when the receiver already
  exposes standard GGA quality values.

Why it matters for the robot:

* robots using a generic NMEA path can now distinguish plain GNSS, RTK Float,
  and RTK Fixed instead of appearing permanently non-RTK
* operator UX and localization logic no longer need to assume "generic NMEA
  means no RTK visibility"

Where MowgliNext should consume it:

* GPS sidecar ROS2 status consumers
* diagnostics/API layers that already read `/gps/status`
* operator GNSS panels or badges

Expected GUI/operator behavior:

* show RTK Float / RTK Fixed from `/gps/status.rtk_mode` even for generic NMEA
  receivers
* do not label the path as vendor-specific Quectel support unless a dedicated
  backend is actually in use

Expected safety/localization behavior if relevant:

* if localization/autonomy gating depends on RTK state, generic NMEA receivers
  should now follow the same normalized RTK mode contract as other backends
* a fallback generic-NMEA deployment should not be treated as permanently
  non-RTK when standard GGA already proves otherwise

Suggested field-validation checks:

* verify `/gps/status.rtk_mode` transitions correctly for generic NMEA
  receivers that output GGA quality `1/4/5`
* verify dashboards update RTK badges without requiring vendor strings or a
  dedicated backend
* verify localization/safety logic reacts to Float -> Fixed -> None transitions
  on the generic NMEA path

### 0c. Pending MowgliNext work — keep GNSS terminology separate from robot yaw

What new Universal GNSS capability exists:

* Universal GNSS now documents a canonical GNSS/geodesy-first terminology
  contract in `docs/terminology.md`.
* Dual-antenna Unicore protocol records now use explicit `baseline_*` terms in
  the low-level layer.
* Current public ROS2/runtime names such as `heading_deg` remain stable in
  `v0.6.x`, but they must be treated as GNSS-domain terms, not robot body yaw.

Why it matters for the robot:

* a GNSS heading or baseline azimuth is not automatically the mower's final yaw
* robot yaw still depends on antenna placement, frame conventions, and any
  downstream mounting transform

Where MowgliNext should consume it:

* GPS sidecar status/diagnostics adapters
* localization bridges that convert GNSS heading into robot orientation
* operator GUI wording and telemetry labels

Expected GUI/operator behavior:

* label GNSS-derived direction as GNSS heading / baseline azimuth until the
  robot-frame transform is applied
* avoid presenting raw Universal GNSS `heading_deg` as robot yaw unless the
  downstream transform has already been performed

Expected safety/localization behavior if relevant:

* any localization logic that uses GNSS direction must make the antenna/body
  transform explicit
* Universal GNSS baseline-specific public fields should be consumed as
  geodetic inputs first, then converted downstream into robot orientation

Suggested field-validation checks:

* verify UI labels do not collapse GNSS heading into robot yaw prematurely
* verify localization code documents and tests the antenna mounting transform
* verify canonical Universal GNSS baseline fields integrate cleanly without
  breaking existing operator concepts

### 0d. Pending MowgliNext work — consume canonical baseline fields from `/gps/status`

What new Universal GNSS capability exists:

* Universal GNSS now exposes additive canonical baseline fields and capability
  bits in `GnssRuntimeState` and ROS2 `GnssStatus`:
  * `dual_antenna_baseline`
  * `baseline_azimuth_deg`
  * `baseline_pitch_deg`
  * `baseline_length_m`
  * `baseline_solution_status`
* Compatibility fields `heading_deg` and `dual_antenna_heading` still exist
  during `v0.6.x`, but they are no longer the preferred downstream contract
  for dual-antenna receivers.

Why it matters for the robot:

* robot stacks can now consume an explicit GNSS baseline surface instead of
  guessing whether `heading_deg` is vehicle yaw, course over ground, or a
  dual-antenna azimuth
* dashboards and localization bridges can separate GNSS baseline validity from
  robot-frame orientation transforms

Where MowgliNext should consume it:

* GPS sidecar ROS2 status consumers
* localization bridges that convert GNSS baseline azimuth into robot yaw
* operator GUI GNSS/heading panels

Expected GUI/operator behavior:

* show baseline azimuth/length/pitch only when the matching capability/value
  flags indicate current data
* label these values as GNSS baseline quantities, not as robot yaw
* keep `heading_deg` only as a fallback compatibility display during the
  transition window

Expected safety/localization behavior if relevant:

* any robot-yaw use of GNSS baseline azimuth must apply an explicit antenna
  mounting transform downstream
* baseline-solution status should be visible separately from RTK position state
  so degraded dual-antenna geometry does not silently masquerade as valid
  robot orientation

Suggested field-validation checks:

* verify `/gps/status.capability_flags` advertise the new baseline capability
  bits only on receivers that truly provide them
* verify `/gps/status.baseline_azimuth_deg`, `baseline_pitch_deg`, and
  `baseline_length_m` appear on UM982/Unicore streams when solved
* verify `/gps/status.baseline_solution_status` distinguishes solved vs
  unsolved cases
* verify GUI/API continue to tolerate `heading_deg` during the compatibility
  window while preferring canonical baseline fields when available

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
