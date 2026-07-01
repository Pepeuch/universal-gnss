# Universal GNSS — TODO

## Current status

`v0.6.0` is released.

Current phase: post-`v0.6.x` stabilization.

Recently completed:

- `v0.1` to `v0.4` portable core, protocol, driver, transport, NTRIP, and tool foundations
- auto-discovery v2 with `gnss_discover` plus `ReceiverNode` wiring for
  `serial_device:=auto`, `serial_baud:=auto`, and `receiver_family:=auto`
- `ReceiverNode`, `NtripNode`, and `ReplayNode`
- `GnssStatus`, `NavSatFix`, and `diagnostic_msgs` projection
- live RTCM forwarding from `NtripNode` into `ReceiverNode`
- portable RTCM MSM correction-stream summary observability
- ROS2 RTCM semantic diagnostics projection for base-station ARP, `1230`, and
  MSM summary/per-message activity
- parser counters plus recent malformed/rejected diagnostic visibility in ROS2
- portable Auto Configuration planner/report/apply flow
- operator-driven runtime-only apply for u-blox and Unicore
- Unicore persistent / `factory_reset` reset-recovery apply
- u-blox persistent FLASH configuration and output-port selection
- UM982 / Unicore runtime hardening and downstream field validation through
  MowgliNext
- generic NMEA `GGA fix_quality` runtime mapping into normalized `rtk_mode`
- decimal-degree latitude/longitude outputs preserve at least 9 decimal places

## Immediate correctness

- [ ] runtime arbitration between streaming traffic and config traffic
- [ ] production-safe failure handling and rollback expectations
- [ ] live receiver identity / model / firmware metadata in discovery and planning output
- [ ] document and preserve generic NMEA runtime_only limitations until a portable write-side config contract exists

## Universal GNSS core tasks

- [ ] capability/profile consistency cleanup between built-in receiver profiles and driver support
- [ ] correction age estimation
- [ ] automatic periodic GGA sending
- [ ] runtime state CSV export
- [ ] JSON schema stabilization/versioning
- [ ] compare two receivers/logs
- [ ] generate additional sanitized test logs
- [ ] generic speed/course runtime contract for VTG
- [ ] generic GNSS wall-clock/date runtime contract for ZDA
- [ ] extend RTCM semantic observations beyond `1005` / `1006` / `1230` / MSM summary
- [ ] `gnss_replay` timing mode outside the ROS2 replay node
- [ ] TLS support
- [ ] client certificate authentication
- [ ] custom CA certificate support
- [ ] UDP transport
- [ ] multi-caster support
- [ ] local caster / base mode support
- [ ] RTCM observation-level decode beyond MSM summary

## Network / NTRIP

- [ ] multi-caster support
- [ ] local caster / base mode support
- [ ] UDP transport
- [ ] TLS support
- [ ] client certificate authentication
- [ ] custom CA certificate support

## ROS2 package tasks

- [ ] expose receiver identity / firmware / model metadata
- [ ] extend planner/report layer into ROS2
- [ ] operator observability review
- [ ] snapshot/export surface
- [ ] ROS2 CI coverage
- [ ] keep Kilted green
- [ ] validate Lyrical
- [ ] validate Humble/Jazzy
- [ ] arm64 validation
- [ ] long-run validation
- [ ] receiver restart validation
- [ ] GNSS reconnect validation
- [ ] NTRIP reconnect validation
- [ ] USB serial renumbering validation
- [ ] F9P ↔ UM982 swap validation

## Receiver-specific backends

### u-blox

- [ ] complete portable factory_reset support
- [ ] survey-in support
- [ ] MON-SPAN

### Unicore

- [ ] broader Unicore binary `N4` semantic decode beyond `BESTNAVB` /  `PVTSLNB`
- [ ] raw observations
- [ ] conservative `AGCA` threshold interpretation if a safe generic policy emerges
- [ ] Unicore raw observation support

### Quectel

- [ ] Quectel dedicated backend audit (`PQTM` / `PAIR`, parser, RTK mapping, config engine)
- [ ] runtime mapping
- [ ] keep Quectel work separate from generic NMEA unless the message is standard NMEA


### Septentrio

- [ ] Septentrio dedicated backend audit (`SBF`, PVT/status, satellite/RF, config/session)
- [ ] runtime/status
- [ ] configuration/session


## Downstream integration tasks

Downstream GUI, install, and operator-workflow issues should stay out of the
Universal GNSS core backlog unless they expose a missing portable feature or a
bug in this repository.

- [ ] Validate Universal GNSS -> `navsat_to_absolute_pose`
- [ ] Validate Universal GNSS -> `localization_monitor`
- [ ] Validate Universal GNSS -> Nav2
- [ ] Validate Universal GNSS -> mower bringup stack
- [ ] MowgliNext onboarding / GUI / operator workflow validation
- [ ] static accuracy validation
- [ ] short waypoint mission validation
- [ ] long waypoint mission validation
- [ ] full Nav2 mission validation
- [ ] verify localization stability during RTK Float/Fixed transitions

## Runtime quality

- [ ] GNSS confidence score
- [ ] RTK confidence score
- [ ] RF quality score
- [ ] correction quality score

## Documentation / Quality

- [ ] contributor architecture guide
- [ ] parser writing guide
- [ ] test vector guide
- [ ] ROS2 integration guide refresh
- [ ] sanitizer builds
- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] coverage report
