# ROS 2 Mapping Audit

This document records the current ROS 2 projection contract for the portable
Universal GNSS runtime and diagnostics models.

Date of audit: `2026-06-01`

Scope:

- `universal_gnss::GnssRuntimeState` -> `universal_gnss_ros2/msg/GnssStatus`
- `universal_gnss::GnssRuntimeState` -> `sensor_msgs/msg/NavSatFix`
- `universal_gnss::GnssHealthSummary` / `GnssDiagnosticEvent`
  -> `diagnostic_msgs/msg/DiagnosticArray`

Out of scope for this step:

- receiver-node transport ownership beyond the current skeleton
- launch files
- `robot_localization` or Nav2 integration

## Audit Summary

The ROS 2 layer now has three mapping surfaces:

- `GnssStatus` for the full normalized runtime view
- `NavSatFix` for ecosystem compatibility
- `DiagnosticArray` for portable health / diagnostic events

Those mappings are now also exercised by the first minimal `ReceiverNode`, but
the node is intentionally just a composition layer over the existing adapters.

Current design intent:

- `GnssStatus` is the authoritative ROS-facing runtime message
- `NavSatFix` is intentionally lossy and conservative
- ROS diagnostics map from the portable health model, not directly from raw
  protocol messages

## Runtime Field Coverage

Legend:

- `yes` = mapped directly
- `derived` = influences the output indirectly
- `no` = intentionally not mapped there today
- `deferred` = waiting on a future core/runtime contract

| Runtime field | `GnssStatus` | `NavSatFix` | ROS diagnostics | Notes |
| --- | --- | --- | --- | --- |
| `fix_valid` | yes | derived | no | Drives `NavSatFix.status.status` but is preserved directly in `GnssStatus`. |
| `fix_type` | yes | derived | no | `kFix`, `kRtkFloat`, `kRtkFixed`, and `kDeadReckoning` map directly to `GnssStatus`; `NavSatFix` compresses them into conservative status codes. |
| `rtk_mode` | yes | derived | no | `NavSatFix` only distinguishes explicit RTK fixed via `STATUS_GBAS_FIX`. |
| `latitude_deg` | yes | yes | no | Uses `NaN` when absent. |
| `longitude_deg` | yes | yes | no | Uses `NaN` when absent. |
| `altitude_m` | yes | yes | no | Uses `NaN` when absent. |
| `horizontal_accuracy_m` | yes | derived | no | Feeds `NavSatFix` covariance only when both horizontal and vertical accuracy are explicitly available. |
| `vertical_accuracy_m` | yes | derived | no | Same conservative covariance rule as horizontal accuracy. |
| `hdop` | yes | no | no | DOP is preserved in `GnssStatus` only. |
| `vdop` | yes | no | no | DOP is preserved in `GnssStatus` only. |
| `satellites_used` | yes | no | no | `NavSatFix` has no equivalent field. |
| `satellites_tracked` | yes | no | no | `NavSatFix` has no equivalent field. |
| `satellites_visible` | yes | no | no | `NavSatFix` has no equivalent field. |
| `mean_cn0_db_hz` | yes | no | no | Preserved only in `GnssStatus`. |
| `max_cn0_db_hz` | yes | no | no | Preserved only in `GnssStatus`. |
| `correction_age_s` | yes | no | no | Useful for RTK / correction monitoring, but not projected into `NavSatFix`. |
| `heading_deg` | yes | no | no | Preserved in `GnssStatus`; no `NavSatFix` equivalent exists. This is a current public compatibility name, not robot yaw. |
| `dual_antenna_heading` | yes | no | no | Preserves baseline-solution availability only; the name is kept for compatibility during `v0.6.x`. |
| `dual_antenna_baseline` | yes | no | no | Canonical boolean baseline-validity field in `GnssStatus`; capability/value-gated like other optional booleans. |
| `baseline_azimuth_deg` | yes | no | no | Canonical dual-antenna baseline azimuth in `GnssStatus`; downstream robot yaw transforms remain out of scope. |
| `baseline_pitch_deg` | yes | no | no | Canonical dual-antenna baseline pitch in `GnssStatus`; no `NavSatFix` equivalent exists. |
| `baseline_length_m` | yes | no | no | Canonical dual-antenna baseline length in `GnssStatus`; no `NavSatFix` equivalent exists. |
| `baseline_solution_status` | yes | no | no | Canonical baseline status enum in `GnssStatus`; preserves solved vs known-unsolved states without implying robot orientation. |
| `interference_detected` | yes | no | no | Preserved in `GnssStatus`; diagnostics come from `GnssHealthSummary`, not the runtime message directly. |
| `jamming_detected` | yes | no | no | Preserved in `GnssStatus`; diagnostics come from `GnssHealthSummary`, not the runtime message directly. |

## Normalization Notes

Some source-protocol distinctions are intentionally normalized away before the
ROS 2 layer:

- 2D vs 3D fixes currently collapse to `GnssFixType::kFix`
- DGPS-like solutions currently collapse to `GnssFixType::kFix`
  - correction context still survives through fields such as
    `correction_age_s`
- RTK float and RTK fixed remain distinct in both the runtime model and
  `GnssStatus`

This means the ROS 2 adapter cannot emit separate 2D, 3D, or DGPS-specific
message variants today because the normalized core contract does not carry
those distinctions.

## `NavSatFix` Mapping Policy

`sensor_msgs/msg/NavSatFix` is intentionally conservative.

### Coordinates

- latitude / longitude map directly when present
- altitude maps directly when present
- absent coordinates remain `NaN`
- coordinates may still be present even when `status.status` remains
  `STATUS_NO_FIX`; fix validity is not inferred from mere coordinate presence

### `NavSatStatus`

Current status mapping:

- no coordinates, invalid fix, `kUnknown`, or `kNoFix`
  -> `STATUS_NO_FIX`
- any valid normalized non-RTK-fixed solution
  -> `STATUS_FIX`
- explicit RTK fixed
  -> `STATUS_GBAS_FIX`

Important limits:

- `STATUS_SBAS_FIX` is not used
- `STATUS_GBAS_FIX` is used only for explicit RTK fixed, not RTK float
- no distinct DGPS / 2D / 3D status exists because the normalized runtime
  model does not expose those states separately

### Covariance

No covariance is invented from partial data.

Current rule:

- if both horizontal and vertical accuracy are explicitly available in the
  runtime value flags, emit a diagonal covariance using `sigma^2`
- otherwise keep `position_covariance_type = COVARIANCE_TYPE_UNKNOWN`

This preserves the capability/value-flag contract:

- raw optional fields alone are not trusted
- the adapter requires the runtime path to mark the accuracy values as current

### `service` bits

`NavSatStatus.service` remains `0`.

Reason:

- the core runtime model does not yet encode per-sample constellation-service
  provenance
- inferring service bits from partial runtime state would be guesswork

## `GnssStatus` Mapping Policy

`universal_gnss_ros2/msg/GnssStatus` is the direct ROS projection of the
portable runtime model.

Key rules:

- every optional runtime enrichment field uses the same capability/value-flag
  semantics as `gnss_core`
- unsupported fields remain outside `value_flags`
- supported-but-currently-unknown fields keep the capability bit without the
  value bit
- absent coordinates use `NaN`
- absent integer / boolean optional values use the public message defaults while
  remaining distinguishable through `value_flags`

This makes `GnssStatus` the preferred ROS message for:

- fix / RTK state
- DOP
- satellite counts
- CN0 summaries
- correction age
- heading compatibility state
- canonical dual-antenna baseline fields and status
- RF / jamming booleans

## Diagnostics Mapping Foundation

Portable diagnostics do not map from `GnssRuntimeState` directly. They map from
the portable health model:

- `GnssDiagnosticEvent`
- `GnssHealthSummary`

Current ROS helpers provide:

- `GnssDiagnosticEvent` -> `diagnostic_msgs/msg/DiagnosticStatus`
- `GnssHealthSummary` -> summary `DiagnosticStatus`
- `GnssHealthSummary` -> `diagnostic_msgs/msg/DiagnosticArray`

Current policy:

- summary status is emitted first in the array
- later statuses carry per-event code, source, and timestamp metadata
- the array header timestamp uses the latest event timestamp when available

Still deferred:

- higher-level topic naming policy
- higher-level receiver-node aggregation policy
- richer transport / reconnection ownership

## Semantic-Only Inputs

The ROS 2 layer intentionally does not project these semantic-only inputs into
runtime messages yet:

- `NMEA VTG`
  - deferred until the core grows a generic speed / course contract
- `NMEA ZDA`
  - deferred until the core grows a GNSS wall-clock / date contract

## ROS 2 Distribution Compatibility

Target baseline for source compatibility:

- Humble
- Jazzy
- Kilted

Future expectation:

- Lyrical should remain compatible if the message definitions and `ament`
  interfaces used here stay stable

Current compatibility assumptions:

- only standard message packages are used:
  - `builtin_interfaces`
  - `sensor_msgs`
  - `diagnostic_msgs`
- the mapping helpers remain pure conversion code
  - no `rclcpp`
  - no executors
  - no timers
  - no node APIs
- the package now also contains a minimal `rclcpp` receiver node, but the
  message adapters themselves still avoid distro-specific runtime APIs
- the package uses standard `ament_cmake` and `rosidl_default_generators`
  patterns shared across modern ROS 2 distros
- no distro-specific preprocessor branches are required in the mapping helpers

Locally validated in this audit:

- Kilted package build and test execution succeeded in the local MowgliNext
  development image
- Humble and Jazzy were not built locally in this pass

Compatibility evidence used for the audit:

- the current adapter code relies only on stable message types:
  - `sensor_msgs/msg/NavSatFix`
  - `sensor_msgs/msg/NavSatStatus`
  - `diagnostic_msgs/msg/DiagnosticArray`
  - `diagnostic_msgs/msg/DiagnosticStatus`
  - `std_msgs/msg/Header`
- those message surfaces are documented in the official ROS docs for Humble,
  Jazzy, and Kilted

Practical recommendation:

- keep `gnss_ros2` mapping helpers free of node/runtime logic
- validate package builds in CI later against at least Humble and Jazzy
- add Kilted to CI only when that matrix is already useful for the node phase

## Local Kilted Validation Method

The local host used for this audit did not provide `/opt/ros` directly, but a
Kilted-capable MowgliNext development image was available locally.

Validation method example:

```bash
docker run --rm \
  -v <universal_gnss_repo>:/repo:ro \
  <ros2_kilted_image> \
  bash -lc '
    source /opt/ros/kilted/setup.bash
    rm -rf /tmp/universal_gnss_ros2_ws
    mkdir -p /tmp/universal_gnss_ros2_ws/src
    ln -s /repo/gnss_ros2 /tmp/universal_gnss_ros2_ws/src/gnss_ros2
    ln -s /repo/gnss_core /tmp/universal_gnss_ros2_ws/src/gnss_core
    ln -s /repo/gnss_protocols /tmp/universal_gnss_ros2_ws/src/gnss_protocols
    ln -s /repo/gnss_transport /tmp/universal_gnss_ros2_ws/src/gnss_transport
    ln -s /repo/gnss_driver /tmp/universal_gnss_ros2_ws/src/gnss_driver
    cd /tmp/universal_gnss_ros2_ws
    colcon build --packages-select universal_gnss_ros2 --event-handlers console_direct+
    colcon test --packages-select universal_gnss_ros2 --event-handlers console_direct+
    colcon test-result --verbose
  '
```

Notes:

- `<universal_gnss_repo>` should be replaced with the local Universal GNSS repository path.
- `<ros2_kilted_image>` should be replaced with any ROS2 Kilted-capable container image.
- The exact image and repository paths are environment-specific and intentionally omitted.

Why this shape was used:

- the Universal GNSS source tree stayed the only modified tree
- `gnss_ros2` now composes sibling low-level libraries during standalone ROS 2 package builds
- the container workspace was throwaway and did not modify MowgliNext

Current local result:

- `universal_gnss_ros2` built successfully on Kilted
- adapter tests passed:
  - `test_gnss_status_adapter`
  - `test_navsat_fix_adapter`
  - `test_diagnostic_adapter`
  - `test_receiver_node`

Known follow-up compatibility gap:

- Kilted emits deprecation warnings for `ament_target_dependencies()`
- this does not block the current build, but a later ROS 2 cleanup pass should
  modernize those link declarations before adding a CI distro matrix
