# Terminology

This document defines the canonical GNSS/geodesy vocabulary for Universal GNSS
and records the current audit status.

Universal GNSS owns GNSS/geodesy terminology in:

- `gnss_core`
- `gnss_protocols`
- `gnss_driver`
- `gnss_ros2`
- diagnostics
- offline tools
- repository documentation

Downstream applications such as MowgliNext may translate Universal GNSS outputs
into robot-specific concepts only after applying their own mounting, frame, and
vehicle-body transforms.

## Canonical Vocabulary

Use these terms in Universal GNSS when they match the source semantics:

| Canonical term | Meaning |
| --- | --- |
| `latitude_deg` / `longitude_deg` | Geodetic WGS84 coordinates in decimal degrees. |
| `rtk_mode` | Portable RTK state: None / Float / Fixed. |
| `correction_stream` | RTCM/NTRIP correction-stream health and completeness. |
| `course_over_ground_deg` | Motion-derived course over ground. |
| `baseline_azimuth_deg` | Geodetic azimuth of a dual-antenna baseline. |
| `baseline_pitch_deg` | Vertical pitch angle of a dual-antenna baseline. |
| `baseline_length_m` | Distance between antennas in a dual-antenna baseline solution. |
| `baseline_solution_status` | Receiver-native solution-status state for a dual-antenna baseline. |
| `azimuth_deg` | Sky-plot or satellite azimuth, or another explicitly documented azimuth quantity. |
| `orientation_deg` | Only when the source really means a geometric orientation, such as NMEA GST ellipse orientation. |

Avoid these terms in Universal GNSS core and ROS2 APIs unless the data truly
comes from that domain:

- `yaw`
- `robot_yaw`
- `vehicle_yaw`
- `robot_heading`
- `body_heading`
- `vehicle_heading`

## Audit Summary

Current repository state after this audit:

- No core or ROS2 public field is named `yaw`, `robot_yaw`, or `vehicle_yaw`.
- NMEA already uses canonical motion-derived `course_over_ground_deg`.
- UBX semantic records now use `course_over_ground_deg` for `headMot`.
- Unicore `PVTSLNA` / `PVTSLNB` semantic records now use explicit
  `baseline_*` names instead of ambiguous `heading_*` names for dual-antenna
  baseline geometry.
- Current public runtime/ROS2 fields `heading_deg` and
  `dual_antenna_heading` remain stable in `v0.6.x`, but they are the main
  terminology debt still visible in the API.

## Audit Table

| Term | Current scope | Decision |
| --- | --- | --- |
| `heading` | Core runtime, ROS2 status, tools, some docs | Keep temporarily as the public generic heading field. Treat it as GNSS heading only, never robot yaw. |
| `yaw` | Downstream/robot docs only | Forbidden in Universal GNSS core and ROS2 APIs. Allowed only in downstream explanatory material. |
| `bearing` | Not used as a core public field | Keep avoided unless a protocol explicitly reports a bearing quantity. |
| `course` | NMEA, UBX semantic records, docs | Canonical for motion-derived travel direction. |
| `azimuth` | Satellite sky plots, future baseline naming, Unicore protocol layer | Canonical when the quantity is a geometric azimuth. |
| `orientation` | NMEA GST ellipse orientation, robot-localization docs | Keep only where the source explicitly means orientation. |
| `baseline` | Unicore protocol layer, docs | Canonical for dual-antenna geometry. |
| `pitch` | Unicore protocol layer | Canonical as `baseline_pitch_deg` for documented baseline vertical angle. |
| `roll` | No current GNSS public surface | Keep absent until a receiver exposes a real GNSS/geodetic roll quantity. |
| `vehicle` | UBX semantic record `heading_vehicle_deg`, robot docs | Allowed only for source-faithful vendor semantics or downstream robot docs. |
| `robot` | `robot_localization` and downstream docs | Allowed only outside Universal GNSS canonical APIs. |

## Immediate Renames Completed In This Branch

These were considered low-risk and semantically wrong enough to fix now:

| Old name | New name | Scope |
| --- | --- | --- |
| `heading_motion_deg` | `course_over_ground_deg` | UBX semantic record |
| `heading_status` | `baseline_solution_status` | Unicore PVTSLN ASCII/binary semantic records |
| `heading_length_m` | `baseline_length_m` | Unicore PVTSLN ASCII/binary semantic records |
| `heading_deg` | `baseline_azimuth_deg` | Unicore PVTSLN ASCII/binary semantic records |
| `heading_pitch_deg` | `baseline_pitch_deg` | Unicore PVTSLN ASCII/binary semantic records |
| `heading_tracked_satellites` | `baseline_tracked_satellites` | Unicore PVTSLN ASCII/binary semantic records |
| `heading_used_satellites` | `baseline_used_satellites` | Unicore PVTSLN ASCII/binary semantic records |

## Risky Public Names Kept For Compatibility

These public names are still exposed and are intentionally not renamed in this
branch because they affect the portable runtime contract and ROS2 message
compatibility:

| Current public name | Current meaning | Problem |
| --- | --- | --- |
| `heading_deg` | Portable GNSS heading/azimuth field | Too broad for explicit dual-antenna baseline semantics, too easy to confuse with robot yaw. |
| `heading_accuracy_deg` | Accuracy of `heading_deg` | Inherits the ambiguity of `heading_deg`. |
| `dual_antenna_heading` | Boolean dual-antenna/baseline solution state | Name sounds like a numeric heading rather than a solution-validity flag. |
| `CAP_HEADING` / `CAP_DUAL_ANTENNA_HEADING` | Capability bits for the fields above | Same ambiguity as the fields they gate. |

## Compatibility / Deprecation Plan

Planned direction after `v0.6.x` stabilization:

1. Introduce explicit public baseline fields in `GnssRuntimeState` and
   `GnssStatus` when the portable model is ready:
   - `baseline_azimuth_deg`
   - `baseline_pitch_deg`
   - `baseline_length_m`
   - `baseline_solution_status` or a narrower boolean/enum if the portable
     contract stays intentionally small
2. Keep `heading_deg` as a compatibility field for one transition window.
3. Define exact projection rules:
   - source-native vehicle heading may still map to generic `heading_deg`
   - dual-antenna baseline azimuth should prefer the explicit baseline field
4. Mark `dual_antenna_heading` as deprecated once an explicit baseline
   validity/status field exists.
5. Update downstream integrations such as MowgliNext only after the new
   canonical fields are available; downstream code should never assume that a
   GNSS heading equals robot yaw without its own transform.
