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
- parser counters plus recent malformed/rejected diagnostic visibility in ROS2
- portable Auto Configuration planner/report/apply flow
- operator-driven runtime-only apply for u-blox and Unicore
- Unicore persistent / `factory_reset` reset-recovery apply
- u-blox persistent FLASH configuration and output-port selection
- UM982 / Unicore runtime hardening and downstream field validation through
  MowgliNext
- decimal-degree latitude/longitude outputs preserve at least 9 decimal places

## Universal GNSS core tasks

- [ ] Generic NMEA improvement: propagate `GGA` fix_quality `4/5` into
  normalized `rtk_mode` / `fix_type`
- [ ] document and preserve generic NMEA `runtime_only` limitations until a
  portable write-side config contract exists
- [ ] runtime arbitration between streaming traffic and config traffic
- [ ] production-safe failure handling and rollback expectations
- [ ] capability/profile consistency cleanup between built-in receiver profiles
  and driver support
- [ ] live receiver identity / model / firmware metadata in discovery and
  planning output
- [ ] `gnss_replay` timing mode outside the ROS2 replay node
- [ ] runtime state CSV export
- [ ] JSON schema stabilization/versioning
- [ ] compare two receivers/logs
- [ ] generate additional sanitized test logs
- [ ] TLS support
- [ ] client certificate authentication
- [ ] custom CA certificate support
- [ ] UDP transport
- [ ] correction age estimation
- [ ] automatic periodic GGA sending
- [ ] multi-caster support
- [ ] local caster / base mode support
- [ ] RTCM `1230` GLONASS bias decode
- [ ] RTCM MSM signal/satellite summary
- [ ] generic speed/course runtime contract for `VTG`
- [ ] generic GNSS wall-clock/date runtime contract for `ZDA`

## ROS2 package tasks

- [ ] extend the planner/report layer into ROS2
- [ ] expose receiver identity / model / firmware metadata in ROS2 reporting
- [ ] operator observability surface review for status, diagnostics, correction,
  discovery, and parser counters
- [ ] snapshot/export surface for debugging sessions
- [ ] ROS2 CI coverage for the integrated stack
- [ ] keep Kilted green as the reference distro
- [ ] validate Lyrical when the image/toolchain is available
- [ ] validate Humble/Jazzy compatibility where practical
- [ ] arm64 build check for the ROS2-integrated stack
- [ ] long-run runtime validation
- [ ] GNSS disconnect / reconnect validation
- [ ] NTRIP disconnect / reconnect validation
- [ ] receiver restart validation
- [ ] USB serial port renumbering validation
- [ ] F9P <-> UM982 swap validation without software changes

## Receiver-specific backend tasks

- [ ] complete portable u-blox `factory_reset` support
- [ ] u-blox survey-in support
- [ ] u-blox `MON-SPAN`
- [ ] broader Unicore binary `N4` semantic decode beyond `BESTNAVB` /
  `PVTSLNB`
- [ ] Unicore raw observation support
- [ ] conservative `AGCA` threshold interpretation if a safe generic policy
  emerges
- [ ] Quectel dedicated backend audit (`PQTM` / `PAIR`, parser, RTK mapping,
  config engine)
- [ ] Septentrio dedicated backend audit (`SBF`, PVT/status, satellite/RF,
  config/session)
- [ ] keep Quectel work separate from generic NMEA unless the message is
  standard NMEA

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

## Documentation / Quality

- [ ] contributor architecture guide
- [ ] parser writing guide
- [ ] test vector guide
- [ ] ROS2 integration guide refresh for post-`v0.6.0` bringup
- [ ] sanitizer builds
- [ ] clang-format
- [ ] clang-tidy
- [ ] cppcheck
- [ ] coverage report
