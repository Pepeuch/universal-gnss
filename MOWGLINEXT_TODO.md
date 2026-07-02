# MowgliNext TODO — Universal GNSS integration

Audit note as of 2026-07-02:

MowgliNext already consumes Universal GNSS public baseline fields,
`correction_stream_status`, and `msm_summary_*` through its public
`mowgli_interfaces/GnssStatus` projection. The existing GUI/test seam also
already covers solved baseline, unknown baseline, correction-stream state
transitions, malformed MSM summaries, and generic NMEA `rtk_mode`.

The remaining items below are still-pending downstream follow-up work only.

## 1. Pending MowgliNext work — live field validation of canonical GNSS baseline and correction surfaces

What new Universal GNSS capability exists:

* Universal GNSS `main` now exposes canonical GNSS/geodesy-first runtime fields
  and diagnostics through ROS2 `/gps/status` and `/diagnostics`, including:
  * `dual_antenna_baseline`
  * `baseline_azimuth_deg`
  * `baseline_pitch_deg`
  * `baseline_length_m`
  * `baseline_solution_status`
  * `rtk_mode`
  * `correction_stream_status`
  * `msm_summary_*`
* Generic NMEA `GGA fix_quality` is now normalized into `rtk_mode` on the
  shared `receiver_family=nmea` path.

Why it matters for the robot:

* dual-antenna baseline validity can now be distinguished from RTK position
  validity
* operator UX can separate GNSS receiver health, correction health, and RTCM
  semantic completeness instead of collapsing them into one status
* generic NMEA receivers can now report RTK Float / Fixed without a
  vendor-specific backend

Where MowgliNext should consume it:

* the public `mowgli_interfaces/GnssStatus` projection
* GPS sidecar runtime/diagnostic bridges
* operator GNSS status panels, diagnostics pages, and validation tooling

Expected GUI/operator behavior:

* preserve canonical labels such as `baseline_azimuth_deg`,
  `baseline_pitch_deg`, `baseline_length_m`, and
  `baseline_solution_status`
* do not present `baseline_azimuth_deg` as robot yaw or robot heading unless a
  downstream robot-frame transform has actually been applied
* show correction-stream state as explicit `Unknown`, `Waiting`, `Active`,
  `Unavailable`, or `Error`
* keep supported-but-unknown baseline/MSM states explicit instead of silently
  inventing false values

Expected safety/localization behavior if relevant:

* any robot-yaw use of GNSS baseline azimuth must apply an explicit antenna
  mounting transform downstream
* localization/autonomy logic should not infer correction health from RTK fix
  state alone when the diagnostics surface is available

Suggested field-validation checks:

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

## 2. Pending MowgliNext work — expose richer RTCM semantic diagnostics if operators need deeper correction debugging

What new Universal GNSS capability exists:

* Universal GNSS also publishes richer ROS2 diagnostics beyond
  `correction_stream_status` and `msm_summary`, including:
  * `rtcm_semantic/base_station_arp`
  * `rtcm_semantic/glonass_code_phase_bias`
  * per-message MSM diagnostics such as `rtcm_semantic/msm_gps_msm7`
  * counters such as `decode_success_count`, `decode_failure_count`, and
    `malformed_count`
* MowgliNext currently consumes the portable downstream minimum:
  `correction_stream_status` plus `msm_summary_*`.

Why it matters for the robot:

* operators may need deeper evidence when RTK silently degrades even though the
  receiver still has a nominal fix
* localization debugging benefits from explicit malformed-count or
  message-class-specific failures instead of a generic “corrections bad” label

Where MowgliNext should consume it:

* diagnostics aggregation around `/diagnostics`
* any backend API/websocket layer that already forwards GNSS diagnostics
* expert/operator correction-health panels

Expected GUI/operator behavior:

* keep correction completeness separate from rover fix state
* surface malformed counter growth or missing required RTCM classes without
  inventing an in-app RTCM parser
* tolerate optional per-message diagnostics appearing only after the message has
  actually been seen

Expected safety/localization behavior if relevant:

* if autonomy or localization policy eventually reacts to correction health, it
  should use Universal GNSS diagnostics as evidence rather than adding a second
  ad-hoc RTCM parser inside MowgliNext

Suggested field-validation checks:

* verify `station_id` and `constellations_seen` update on live correction
  streams
* verify malformed counters rise on intentionally bad or truncated RTCM inputs
* verify dashboards remain stable when one constellation is absent from the
  correction stream
* verify optional per-message MSM diagnostics can appear without breaking the
  public `GnssStatus` projection

## 3. Pending MowgliNext work — decide robot-side policy for correction-health degradation

What new Universal GNSS capability exists:

* MowgliNext can now observe RTK position state separately from correction
  transport health and RTCM semantic completeness.

Why it matters for the robot:

* the mower should not silently appear healthy when RTK is degrading because
  corrections are stale, malformed, or incomplete
* future autonomy/localization policy can now react to explicit evidence rather
  than guessing from fix type alone

Where MowgliNext should consume it:

* localization gating or degraded-mode policy, if any
* mission/event logging for post-run debugging
* operator warnings around correction loss or stale streams

Expected GUI/operator behavior:

* avoid collapsing everything into a single `RTK OK` badge
* make any degraded or unknown correction-health policy explicit to operators

Expected safety/localization behavior if relevant:

* decide whether autonomy should allow `RTK_FIXED` only, `RTK_FLOAT` in
  degraded mode, plain GNSS, stale corrections, or missing MSM
* log correction-health transitions during missions so post-run debugging can
  explain RTK degradation

Suggested field-validation checks:

* verify behavior during `RTK Float -> Fixed` transitions
* verify behavior during `Fixed -> Float` or correction-loss transitions
* verify behavior when NTRIP is disconnected or when only some constellations
  are present
* verify any future degraded-mode policy is visible in logs and operator UI

## 4. Pending MowgliNext work — consume Unicore model/capability metadata in any configurator UI

What new Universal GNSS capability exists:

* Universal GNSS now has a model-aware Unicore profile layer that can answer:
  * which Unicore model is selected
  * whether that model supports `dual_antenna_baseline`
  * which `CONFIG SIGNALGROUP` selections are documented and allowed
  * when `CONFIG SIGNALGROUP` must be skipped because the model is unknown or
    because no documented automatic rover selection exists
* The current portable CLIs expose that seam through an optional Unicore
  `--model` selector and warning/report output.

Why it matters for the robot:

* a receiver can support RTK positioning without supporting antenna baseline
* the operator UI should not offer dual-antenna/baseline-only signal-group
  choices to single-antenna receivers
* unknown Unicore models should fail safe instead of inheriting a family-wide
  `CONFIG SIGNALGROUP` guess

Where MowgliNext should consume it:

* any GNSS setup/configuration wizard or plan/apply wrapper
* any backend API that builds `gnss_profile_preview`, `gnss_config_plan`, or
  `gnss_config_apply` requests
* operator review screens that explain why a requested Unicore config item was
  applied, rejected, or skipped

Expected GUI/operator behavior:

* when the receiver model is known, show only the documented signal-group
  selections for that model
* when the receiver model is unknown, disable or hide Unicore
  `CONFIG SIGNALGROUP` choices and explain that the command was skipped for
  safety
* do not present baseline-only signal groups for non-baseline models such as
  UM980 or UB9A0
* keep unsupported or undocumented models explicit instead of silently mapping
  them onto UM982 behavior

Expected safety/localization behavior if relevant:

* prevent operators from applying dual-antenna baseline-specific configuration
  to single-antenna receivers
* keep robot-side localization policy separate from config policy; the
  configurator should not infer baseline capability from current RTK mode or
  runtime baseline fields

Suggested field-validation checks:

* verify a confirmed UM982 path surfaces the documented portable rover choice
  and does not offer undocumented combinations
* verify UM980 and UB9A0 paths do not surface baseline-only signal-group
  choices
* verify unknown or undocumented Unicore models visibly skip
  `CONFIG SIGNALGROUP` and explain why
* verify operator review pages preserve the selected receiver model and any
  skip/rejection warning in logs or exported plans

## Notes

Universal GNSS MSM support currently provides correction-stream observability
only.

It does not yet expose detailed satellite/signal identities, pseudorange,
carrier phase, Doppler, C/N0, or RTCM observation extraction into robot
localization.

MowgliNext should treat MSM data as correction-stream diagnostics, not as
navigation measurements.
