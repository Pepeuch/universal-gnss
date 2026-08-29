# MowgliNext TODO — Universal GNSS integration

Audit note as of 2026-07-02:

MowgliNext already consumes Universal GNSS public baseline fields,
`correction_stream_status`, and `msm_summary_*` through its public
`mowgli_interfaces/GnssStatus` projection. The existing GUI/test seam also
already covers solved baseline, unknown baseline, correction-stream state
transitions, malformed MSM summaries, and generic NMEA `rtk_mode`.

The remaining items below are still-pending downstream follow-up work only.

Dual-antenna GNSS baseline is now a first-class GNSS/geodesy surface, but it is
not automatically a robot yaw source. Any robot-frame heading/orientation use
must remain downstream of explicit antenna mounting transform/calibration and
localization policy.

Immediate UI pass scope to preserve:

* make the GPS/GNSS panel readable using existing downstream data surfaces only
* prioritize bargraphs / compact visual summaries for:
  * `satellites_used`
  * `satellites_visible`
  * `satellites_tracked`
  * `mean_cn0_db_hz`
  * `max_cn0_db_hz`
  * `correction_stream_status`
  * `msm_summary_*`
* keep the implementation portable by using existing `GnssStatus` projection
  and existing diagnostics forwarding only

Explicitly not in this UI pass:

* robot-frame baseline heading/yaw use
* Sensors panel antenna mounting configuration work
* Localization panel policy/fusion work
* a new RTCM parser or observation extractor inside MowgliNext
* navigation/autonomy decisions driven by baseline/MSM/correction state

## 1. Current UI pass — make the GPS/GNSS panel readable with satellite / C/N0 / MSM bargraphs

What Universal GNSS already provides:

* satellite / RF summary fields already present in the public projection:
  * `satellites_used`
  * `satellites_visible`
  * `satellites_tracked`
  * `mean_cn0_db_hz`
  * `max_cn0_db_hz`
* canonical dual-antenna baseline state:
  * `dual_antenna_baseline`
  * `baseline_azimuth_deg`
  * `baseline_pitch_deg`
  * `baseline_length_m`
  * `baseline_solution_status`
* adjacent receiver/correction state already present in the public projection:
  * `receiver_model`
  * `rtk_mode`
  * `correction_stream_status`
  * `msm_summary_*`

What MowgliNext should still add:

* render readable satellite bargraphs or equivalent compact visual summaries
  for `satellites_used`, `satellites_visible`, and `satellites_tracked`
* render readable C/N0 bargraphs or equivalent compact visual summaries for
  `mean_cn0_db_hz` and `max_cn0_db_hz`
* show `correction_stream_status` and `msm_summary_*` in the same panel as the
  satellite / C/N0 summaries so operators can read signal quality and
  correction completeness together
* keep the implementation downstream of existing `GnssStatus` and diagnostics
  surfaces; do not add a second RTCM parser or per-message observation parser
  inside MowgliNext
* show the existing raw Universal GNSS baseline fields only as GNSS/geodesy
  state if they remain visible in the same panel
* preserve canonical labels such as `baseline_azimuth_deg`,
  `baseline_pitch_deg`, `baseline_length_m`, and
  `baseline_solution_status`
* keep supported-but-unknown baseline/MSM states explicit instead of silently
  inventing false values
* add clear operator wording that `baseline_azimuth_deg` is GNSS
  antenna-baseline azimuth, not robot yaw, unless downstream antenna mounting
  calibration/transform is configured

Suggested operator wording constraint:

* do not present `baseline_azimuth_deg` as robot yaw or robot heading in the
  raw GPS/GNSS panel
* do not rename `baseline_azimuth_deg` to `yaw` or `heading` unless a
  robot-frame transform has actually been applied downstream
* if the panel mixes baseline, satellites, C/N0, and MSM information, make it
  visually obvious which items are raw GNSS/geodesy state versus correction
  transport/semantic state

## 2. Staged roadmap — Sensors UI / physical antenna mounting configuration

Why this is separate from the GPS/GNSS panel:

* the GPS/GNSS panel reports raw receiver/geodesy state
* the Sensors panel describes physical installation relative to `base_link`

What MowgliNext should still add:

* a GNSS dual-antenna baseline mounting section in the Sensors UI
* primary antenna position relative to `base_link`
* secondary antenna position relative to `base_link`
* expected baseline length computed from mounting offsets
* live comparison between expected length and `baseline_length_m`
* antenna vector direction relative to the robot frame
* preparation for calibration/validation of baseline direction and sign

Required downstream rule:

* robot heading/yaw use requires an explicit antenna mounting
  transform/calibration downstream
* baseline geometry should not be inferred from RTK mode or from raw
  `baseline_azimuth_deg` alone

## 3. Staged roadmap — Localization UI / heading source policy

Why this is separate from Sensors and GPS/GNSS display:

* GPS/GNSS shows raw receiver state
* Sensors describes physical mounting
* Localization decides how and whether that information is fused into robot
  orientation

What MowgliNext should still add:

* GNSS baseline as a possible heading/orientation source in localization policy
* explicit separation from magnetometer policy and IMU gyro/odometry policy
* policy choices such as:
  * never use GNSS baseline
  * use only when `baseline_solution_status` is computed/valid
  * use only when live `baseline_length_m` is consistent with configured
    mounting geometry
  * use only with acceptable covariance/confidence settings, if exposed
    downstream
  * fall back to IMU / magnetometer / odometry when baseline is unavailable
    or degraded

Required downstream rule:

* do not infer robot yaw directly from `baseline_azimuth_deg` without the
  configured transform/calibration from antenna baseline frame into robot frame

## 4. Staged roadmap — validation / field testing

What must be validated downstream:

* live UM982 baseline data on hardware
* `baseline_azimuth_deg` direction and sign against the robot frame after
  mounting configuration/calibration exists
* `baseline_length_m` against measured physical antenna spacing
* behavior when baseline is unavailable, unknown, pending, or degraded
* localization fallback behavior when baseline drops out

Additional GNSS status checks to keep in scope:

* verify solved UM982/Unicore streams publish
  `baseline_azimuth_deg`, `baseline_pitch_deg`, `baseline_length_m`, and
  `baseline_solution_status=COMPUTED`
* verify unsolved or unavailable dual-antenna cases keep baseline capability
  explicit while leaving values unknown/pending
* verify `correction_stream_status` transitions correctly through `WAITING`,
  `ACTIVE`, `UNAVAILABLE`, and `ERROR`
* verify valid MSM traffic populates `msm_summary_seen`,
  `msm_summary_decoded`, `msm_summary_valid`, `station_id`,
  `constellations_seen`, and aggregate counts
* verify malformed or undecodable MSM traffic keeps `seen=true`,
  `decoded=false`, and `valid=false` explicit instead of collapsing to
  unsupported
* verify generic NMEA receivers that output GGA quality `1/4/5` transition
  `rtk_mode` through `NONE`, `FLOAT`, and `FIXED`

## 5. Architecture notes — raw GNSS/geodesy vs robot-frame use

Architecture split to preserve:

* GPS/GNSS panel shows raw receiver/geodesy state
* Sensors panel describes physical antenna mounting and expected geometry
* Localization panel decides how and whether to use/fuse GNSS baseline as
  robot heading/orientation

Terminology rules to preserve:

* `baseline_azimuth_deg` is GNSS antenna-baseline azimuth, not robot yaw
* MowgliNext must not rename `baseline_azimuth_deg` to `yaw` or `heading`
  unless a robot-frame transform has actually been applied
* `baseline_pitch_deg`, `baseline_length_m`, and
  `baseline_solution_status` should stay canonical until transformed
  downstream

Ownership rules to preserve:

* Universal GNSS remains the source of GNSS/geodesy fields
* MowgliNext owns robot-frame mounting configuration, calibration UX, and
  localization policy
* downstream localization/autonomy logic should not infer correction health
  from RTK fix state alone when richer diagnostics are available

## 6. Additional downstream GNSS follow-up — expose richer RTCM semantic diagnostics if operators need deeper correction debugging

What Universal GNSS already provides:

* richer ROS2 diagnostics beyond `correction_stream_status` and
  `msm_summary_*`, including:
  * `rtcm_semantic/base_station_arp`
  * `rtcm_semantic/glonass_code_phase_bias`
  * per-message MSM diagnostics such as `rtcm_semantic/msm_gps_msm7`
  * counters such as `decode_success_count`, `decode_failure_count`, and
    `malformed_count`

What MowgliNext should still add if needed:

* correction-health panels that keep correction completeness separate from
  rover fix state
* malformed counter growth or missing required RTCM class visibility without
  inventing an in-app RTCM parser
* tolerant handling of optional per-message diagnostics that appear only after
  those messages are seen

Suggested validation checks:

* verify `station_id` and `constellations_seen` update on live correction
  streams
* verify malformed counters rise on intentionally bad or truncated RTCM inputs
* verify dashboards remain stable when one constellation is absent from the
  correction stream
* verify optional per-message MSM diagnostics can appear without breaking the
  public `GnssStatus` projection

## 7. Additional downstream GNSS follow-up — decide robot-side policy for correction-health degradation

What MowgliNext can already observe:

* RTK position state separately from correction transport health and RTCM
  semantic completeness

What MowgliNext should still decide:

* whether autonomy should allow `RTK_FIXED` only, `RTK_FLOAT` in degraded
  mode, plain GNSS, stale corrections, or missing MSM
* how correction-health transitions should appear in mission/event logs and
  operator UI

Suggested validation checks:

* verify behavior during `RTK Float -> Fixed` transitions
* verify behavior during `Fixed -> Float` or correction-loss transitions
* verify behavior when NTRIP is disconnected or when only some constellations
  are present
* verify any future degraded-mode policy is visible in logs and operator UI

## 8. Additional downstream GNSS follow-up — keep model-aware Unicore configurator UI aligned with portable Universal GNSS behavior

What Universal GNSS already provides:

* a model-aware Unicore profile layer that can answer:
  * which Unicore model is selected
  * whether that model supports `dual_antenna_baseline`
  * which `CONFIG SIGNALGROUP` selections are documented and allowed
  * when `CONFIG SIGNALGROUP` must be skipped because the model is unknown or
    because no documented automatic rover selection exists
* portable CLI seams through optional `--model` selection and warning/report
  output

What MowgliNext should still preserve:

* when the receiver model is known, show only the documented signal-group
  selections for that model
* when the receiver model is unknown, disable or hide Unicore
  `CONFIG SIGNALGROUP` choices and explain that the command was skipped for
  safety
* do not present baseline-only signal-group choices for non-baseline models
  such as UM960, UM980, or UM981
* keep unsupported or undocumented models explicit instead of silently mapping
  them onto UM982 behavior
* keep configurator policy separate from localization policy; the configurator
  should not infer baseline capability from current RTK mode or runtime
  baseline fields

Suggested validation checks:

* verify a confirmed UM982 path surfaces the documented portable rover choice
  and does not offer undocumented combinations
* verify UM960, UM980, and UM981 paths do not surface baseline-only
  signal-group choices
* verify unknown or undocumented Unicore models visibly skip
  `CONFIG SIGNALGROUP` and explain why
* verify operator review pages preserve the selected receiver model and any
  skip/rejection warning in logs or exported plans

## 9. Pending downstream integration — preserve GNSS observation provenance

New Universal GNSS capability:

* ROS `GnssStatus.stamp` now explicitly denotes local receiver receipt time in
  the ROS clock domain, not publication time or a receiver-provided measurement
  epoch
* `position_observation_sequence` advances only when Universal GNSS accepts a
  new position/fix observation; republishing cached state leaves it unchanged
* public `RtcmFrame.stamp` remains ROS time while correction freshness is kept
  on an internal monotonic clock

Why this matters for MowgliNext:

* a high ROS publication rate must not make stale GNSS input appear newer
* two real consecutive fixes may have identical coordinates and still need to
  be recognized as distinct observations
* robot-side age or safety logic must not subtract ROS timestamps from a local
  steady-clock timestamp

Pending MowgliNext work:

* preserve `position_observation_sequence` through the robot-side GNSS status
  projection or explicitly document where equivalent provenance is retained
* drive any cached-status/new-observation distinction from this provenance,
  not coordinate equality or callback frequency
* keep GUI/operator stale indications based on a local monotonic receipt timer;
  do not display publication activity as receiver observation activity
* ensure localization and safety consumers reject stale observations even when
  the status topic continues to publish

Suggested robot field-validation checks:

* pause receiver observations while leaving ROS status publication active and
  verify the operator display and localization policy still transition stale
* resume with a numerically identical fix and verify freshness recovers because
  the observation sequence advances
* test simulated-time jumps in both directions and verify correction/GNSS
  liveness remains governed by monotonic elapsed time
* verify logs preserve the ROS receipt stamp without describing it as GNSS
  measurement time

## 10. Pending downstream integration — expose NTRIP correction-flow liveness separately from connection and semantic health

New Universal GNSS capability:

* NTRIP now distinguishes TCP connectivity, an accepted HTTP/ICY response,
  recent complete CRC-valid RTCM flow, and RTCM semantic correction health
* ROS2 exposes configurable `correction_first_frame_timeout_s`,
  `correction_inter_frame_timeout_s`, and
  `correction_operational_min_valid_frames` controls
* the `correction_flowing` diagnostic reports framed correction progress;
  `ntrip_streaming` continues to mean only that the response stream is open
* silent accepted streams enter the existing reconnect/backoff path, while
  source/station-owned static base metadata follows explicit ownership rules

Why this matters for MowgliNext:

* an open caster socket or accepted response must not be shown to an operator
  as live corrections
* correction bytes flowing and the required correction set being semantically
  healthy are distinct operator and localization conditions
* field deployments with intentionally slow RTCM cadence may need timeout
  values matched to the caster configuration

Pending MowgliNext work:

* consume `ntrip_streaming`, `correction_flowing`, and semantic correction
  diagnostics as separate states in the GNSS/operator view and event logs
* surface reconnect caused by first-frame or inter-frame silence without
  presenting it as a receiver-position failure
* retain conservative localization/safety behavior when flow is absent or the
  required RTCM set is incomplete, even if TCP remains connected
* expose timeout overrides only in deployment configuration with units and the
  zero-disables behavior explicit; do not silently derive them from GUI refresh
  or ROS publication rates

Suggested robot field-validation checks:

* connect to a caster that accepts the request but emits no RTCM and verify the
  GUI transitions from waiting to reconnecting
* interrupt a healthy correction stream without closing TCP, then verify flow
  loss, reconnect/backoff, operator notification, and localization degradation
* validate a deliberately slow but healthy caster below the configured
  inter-frame deadline without reconnect thrashing
* switch mountpoint or reference station and verify old base metadata never
  makes the new stream appear semantically healthy

## Notes

Universal GNSS MSM support currently provides correction-stream observability
only.

It does not yet expose detailed satellite/signal identities, pseudorange,
carrier phase, Doppler, C/N0, or RTCM observation extraction into robot
localization.

MowgliNext should treat MSM data as correction-stream diagnostics, not as
navigation measurements.
