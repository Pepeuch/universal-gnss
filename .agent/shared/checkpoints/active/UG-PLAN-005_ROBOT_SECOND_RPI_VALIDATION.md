# UG-PLAN-005 robot + second-RPi validation

Lifecycle: ACTIVE. This contains completed evidence through Phase F and the
bounded resumption plan for later phases.

Repository: `/workspaces/universal-gnss`  
Branch: `feat/docker`  
Baseline: `a5b5755` (`ci(docker): harden v0.7 image contract checks`)
Execution repository HEAD: `7a7ebd012ac238cda74f292e8400a97648be5bae`
Validated image revision: `90325b2a05501341b9e6f88460d8c4b194c7bf25`

## CONTRACT

Use one production-like robot host and one independent arm64 Raspberry Pi on
the same physical LAN to advance only unresolved v0.7 Docker evidence. Do not
run MAVROS/MowgliNext integration, BlueOS, image publication, or receiver
feature work. `TODO.md` remains the release/accounting source of truth.

Complete a gate only from its stated physical evidence. Record PASS, PARTIAL,
or BLOCKED immediately after each test; regenerate release status only for a
fully closed canonical TODO item.

## CURRENT_STATE

- v0.7: 52/65; Project Roadmap: 78/194; UGA: 33/205.
- amd64 Kilted/Lyrical image/runtime, QEMU/BuildKit arm64 packaging, non-root
  serial mapping, tini/SIGINT lifecycle, external read-only configuration,
  same-host bridge DDS/domain isolation, and u-blox/UM982 live
  GNSS/NTRIP/RTCM evidence are established. Reuse the deployment checkpoint.
- Native Kilted arm64 build/runtime and a least-privilege live UM982 serial
  path are proven on the second physical RPi. QEMU remains separate packaging
  evidence; Lyrical-native and caster/network coverage were not inferred.
  External-LAN DDS is proven between the robot and second RPi using the current
  Kilted image on Docker bridge with explicit external-unicast locators and two
  published RTPS UDP ports; unmodified bridge multicast directly failed. See
  `retained/UG-PLAN-005_EXTERNAL_LAN_DDS.md`.
- Docker USB-loss contract is established: resolve exactly one stable by-id
  path, map only that device, and recreate the container after loss. Both the
  UM982 same-number trial and u-blox actual-renumbering trial showed that an
  already-running container did not regain the device grant. UM982 runtime-only
  provisioning must be replayed after USB power loss; the tested u-blox needed
  no provisioning replay. Expected-by-id pinning safely refused an unambiguous
  wrong-receiver substitution.
- UGA-126 and UGA-170 remain PARTIAL. Do not give either completion credit by
  inference.

## PHASE_A_EVIDENCE_2026_09_05

Phase A passive inventory completed at 2026-09-05 13:43–13:48 UTC. No remote
package, service, container, network, repository, configuration, or receiver
state was changed, and no serial port was opened or probed.

### Robot — `192.168.10.35`

- Host `Peuchmower2`; Debian 13.6 (trixie), kernel
  `6.18.39+rpt-rpi-2712`, native `aarch64`.
- Docker client/server `29.8.0 linux/arm64`; overlayfs, systemd cgroup v2.
  Root filesystem `/dev/sda2`: 117 GiB total, 15 GiB used, 98 GiB free.
- LAN is `wlan0` at `192.168.10.35/24`, default route via
  `192.168.10.1`; Docker networks `172.17.0.0/16` and `172.18.0.0/16`.
- Clock synchronized with active NTP; timezone `Europe/Paris`; UTC observation
  `2026-09-05T13:43:49Z`. `ufw`, `nft`, and `iptables` CLIs are absent, so
  Phase A saw no host firewall tooling but did not prove an empty kernel ruleset.
- User `pepeuch` is in `docker` and `dialout` and can inspect Docker without
  elevation.
- MowgliNext worktree: `fix/gnss-downstream` at
  `5bd4e6af37773ce6f44330cdf66362c9fe0a04e6`, with untracked
  `docker/config/mowgli/mowgli_robot.yaml.pre460800test`. Its Universal GNSS
  gitlink is `5281472116669972ae12b9d1997d66b064671cf5`.
- Separate `/home/pepeuch/mowgli-docker` worktree is `v2` at
  `f76800fe8c6ba665e105dbf2bc9e4322c1d09c21`, with pre-existing modified
  `.env` and `config/om/mower_config.sh` plus untracked `.env.bak` and
  `config/db/`. Contents were not read.
- Existing Compose project is `/home/pepeuch/mowglinext/docker/docker-compose.yaml`.
  All six containers were running with restart policy `unless-stopped` and
  restart count 0: GUI `1e79992047af` / image `sha256:1b01abab8324`,
  watchtower `9e26c54da00d` / `sha256:8042c9efdebd`, MQTT
  `63b36e45d55c` / `sha256:6f8d8a947c50`, ROS2 `31ee55f0cac6` /
  `sha256:f440535aae76`, lidar `7828333d2696` / `sha256:beaa5bf17972`,
  and GPS `bf7bfaa9754f` / `sha256:267028885b2f`.
- The stale GPS image is
  `ghcr.io/mowglinext/mowglinext/gps@sha256:267028885b2f63b8e65c76d1705d065d8a89b21352fb0dab00e414bc5e0fb096`,
  OCI revision `6506901497a6df21b5fa633773f46986a9abe9b0`. The stale GPS
  container is privileged, host-networked, runs with an empty Docker `User`,
  and bind-mounts all of `/dev`; it is rollback evidence, never current-UG
  least-privilege evidence.
- Stale ROS/GPS containers declare ROS 2 Kilted, domain 0,
  `rmw_cyclonedds_cpp`, `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`, and
  `CYCLONEDDS_URI=file:///cyclonedds.xml`. The host has no `/opt/ros` tree and
  no ROS/DDS variables in the SSH login environment.
- Rollback configuration identity: `mowgli_robot.yaml` SHA-256
  `f779520a50dadca060ff1082b2f5f93e2677f17c22398297984af25ede91dedc`;
  `cyclonedds.xml` SHA-256
  `4fc1039763f7fea849c8fead011a1bd6febcf3b855301e99bc97a3e1d2d068a1`.
  No credential-bearing values were read.
- Intended GNSS identity is the unambiguous u-blox link
  `/dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00`
  -> `/dev/ttyACM1`, character device `166:1`, `root:dialout`, mode `0660`,
  USB `1546:01a9`, driver `cdc_acm`. Udev identifies the family only as
  `u-blox_GNSS_receiver`; an exact model was not safely observable without
  opening the active serial device.
- Mounted Docker configuration state is preserved by checksum, but the active
  receiver's runtime/persistent profile and firmware were not deterministically
  observable without opening the in-use serial device or reading mixed
  credential-bearing configuration. Neither is inferred from the stale image.
- The distinct Mowgli controller is
  `/dev/serial/by-id/usb-STMicroelectronics_Mowgli_5CDA80483434-if00` ->
  `/dev/ttyACM0`, `166:0`. It must not be substituted for the GNSS receiver.

### Second RPi — `192.168.10.225`

- Host `raspberrypi`; Debian 13.6 (trixie), kernel
  `6.18.39+rpt-rpi-v8`, native `aarch64`.
- Docker client/server `29.8.0 linux/arm64`; overlayfs, systemd cgroup v2.
  Root filesystem `/dev/sda2`: 110 GiB total, 13 GiB used, 94 GiB free.
- LAN is `eth0` at `192.168.10.225/24`, default route via
  `192.168.10.1`; Docker networks `172.17.0.0/16` and `172.18.0.0/16`.
- Clock synchronized with active NTP; timezone `Europe/London`; UTC observation
  `2026-09-05T13:43:46Z`. `ufw`, `nft`, and `iptables` CLIs are absent, with
  the same ruleset-visibility limitation as the robot.
- MowgliNext worktree is clean `dev` at
  `5cb07fabb72f4c1a8a31cc8857eb61814cbe986c`; its Universal GNSS gitlink
  is `ab32f673da6e9e6ffa8eac9a57b08656f8843645`.
- An existing Mowgli Compose stack is running despite this host's disposable
  campaign role. All five containers use `unless-stopped` with restart count 0:
  GUI `6d4a2c8e99c0` / image `sha256:d0e933c0b16c`, MQTT
  `26ed1542d0569` / `sha256:6f8d8a947c50`, watchtower `3fc8f5b2cbc3` /
  `sha256:8042c9efdebd`, ROS2 `f56fe6c13da5` / `sha256:c120330fe87f`,
  and GPS `acd8aa738d0e` / `sha256:ac2ebe0e5042`. Compose path is
  `/home/pepeuch/mowglinext/docker/docker-compose.yaml`.
- The existing dev GPS image is
  `ghcr.io/mowglinext/mowglinext/gps@sha256:ac2ebe0e504202358779ab2cc4c2af660e1cb3de8b875c0aeb9ae726029f6efa`,
  OCI revision `f7f13db6305bc3b69693cfcfc738025d2b157ba4`. Existing ROS/GPS
  containers are privileged, host-networked, broadly mount `/dev`, and declare
  Kilted/Cyclone DDS/domain 0/localhost-only discovery. They are not current-UG
  validation evidence.
- The host has no `/opt/ros` tree, no ROS/DDS variables in the SSH login
  environment, and no `/dev/serial/by-id`; no receiver is mapped or assumed.

### Phase A classification

- Both hosts meet the real native-arm64 prerequisite. Native current-UG
  build/load/run is not yet tested, so the gate remains unchecked.
- The two operator-identified physical hosts are independently reachable on the
  same `/24`, Docker is accessible on both, and their interface roles are
  recorded. This clears the previous peer-availability blocker for the
  external-LAN DDS matrix; no DDS discovery/payload test has run, so the matrix
  is ACTIVE/PENDING and receives no completion credit.
- No canonical TODO gate closed. v0.7, Project Roadmap, and UGA totals remain
  45/65, 71/194, and 33/205 respectively.

### Pre-Phase-B second-RPi GNSS refresh

The first refresh after operator action still returned
`/dev/serial/by-id=absent`. After the operator rechecked physical USB
enumeration, the final passive refresh established exactly one receiver bridge:

- `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` -> `/dev/ttyUSB1`;
- character device `188:1`, `root:dialout`, mode `0666`;
- USB VID:PID `1a86:7523`, udev model `USB_Serial`, driver `ch341`;
- no user-visible host handle and no handle in any running container with a
  `/dev` bind mount was detected.

Udev identifies only the generic QinHeng CH340 bridge. The operator physically
confirms the receiver behind it is a Unicore UM982; do not present that model as
udev-derived identity. The serial device was not opened or configured. Recheck
the by-id realpath and major:minor immediately before any future container
creation because this observation supersedes the earlier absent-device state.

## PHASE_B_NATIVE_ARM64_EVIDENCE_2026_09_05

- Exact build input was `git archive HEAD` from clean tracked source
  `90325b2a05501341b9e6f88460d8c4b194c7bf25`; archive SHA-256
  `57e64b11494ec0d95e9a70272130cb6a87f735f73b910229c48697c224a98a9c`.
  Uncommitted checkpoint state was excluded. Docker contract-file hashes match
  between the local repository and extracted RPi source.
- On native `aarch64` host `raspberrypi` with Docker server
  `29.8.0/linux/arm64`, ordinary `docker build` ran without a `--platform`
  override and compiled `universal_gnss_ros2` natively in 12 min 50 s.
- Kilted base resolved to
  `ros:kilted-ros-base@sha256:0030f32dc8a71ef8401c89470db6003c779f036f532d40195790f58f0001902d`.
  The resulting local tag is
  `universal-gnss:ug-plan-005-kilted-90325b2`. Image inspection and runtime
  acceptance initially remained pending; this build result alone did not close
  the gate.
- Static/runtime-content validation passed for native image ID and local digest
  `sha256:1b01bb887132dccb425107c55f5d1b5c1cf840312c10cd0dc98d59bc0dd8184b`
  (`linux/arm64`, 1,373,185,837 bytes): default user `1000:1000`, tini
  group-forwarding entrypoint, `STOPSIGNAL SIGINT`, process healthcheck,
  expected receiver/NTRIP nodes and operator tools, all dynamic libraries
  resolved, and no final-image source/build/log development trees.
- OCI identity exactly reports version `ug-plan-005-90325b2`, revision
  `90325b2a05501341b9e6f88460d8c4b194c7bf25`, source repository, and source
  commit timestamp `2026-09-04T22:05:54+00:00`; image environment contains no
  secret-like NTRIP/password/token names. Native in-image `uname -m` returned
  `aarch64` and runtime `id` returned uid/gid 1000.
- Unsupported schema v2 exited 2 with the stable schema event; missing default
  parameter mount exited 1 with the stable unreadable-file event.
- External credential-empty parameter copy SHA-256
  `1a5f45af8d435c2ae7f3c47697428c9cd7da08ccc53b151f63f0496b1e60b2ec`
  was operator-owned mode `0600` and mounted read-only. Container
  `ug-plan-005-native-kilted` (`5eae1c174a8c`) ran on default bridge with no
  device grant, no added group, non-root user, and no privileged mode. It became
  Docker-healthy with process tree `tini -> ros2 -> receiver_node,ntrip_node`;
  receiver discovery failed independently while NTRIP reported disconnected.
- `docker stop` used the image SIGINT path and completed in about one second.
  Launch forwarded SIGINT to both nodes; each signal handler ran. Final state:
  exit 0, OOM false, restart count 0. External logs were written as
  `pepeuch:pepeuch`. The stopped evidence container remains available; legacy
  Mowgli containers were not changed.
- Immediately before each device-backed container creation, the selected by-id
  link was re-resolved to `/dev/ttyUSB1`, character device `188:1`, and no
  user-visible host handle was open. The live containers mapped only that by-id
  path to `/dev/gnss-receiver`, added only group 20, ran as uid/gid 1000 on the
  default bridge, and were neither privileged nor given a broad `/dev` mount.
- The first receiver-backed run at the configured 921600 baud produced no
  observations, matching the established volatile-profile behavior after USB
  power loss. Its snapshots remained at sequence 0 and stale. A bounded
  automatic runtime-only apply at that baud returned `transport_unavailable`
  without executing any command; a 115200 passive discovery likewise saw no
  periodic output.
- The exact offline UM982 high-precision plan contained 14 runtime commands,
  zero persistent commands, and no `FRESET` or `SAVECONFIG`. A one-off,
  network-disabled, non-root container then applied those 14 commands from
  current baud 115200 to target baud 921600 with explicit family `unicore`,
  model `UM982`, and confirmation. All 14 completed; the post-switch `VERSIONA`
  response was received at 921600. No persistent or factory-reset operation ran.
- Fresh container `ug-plan-005-native-um982-profiled` (`ccc5c620`) then proved
  advancing live data. Two bounded snapshots progressed from sequence 223 / 351
  runtime observations and updates to sequence 237 / 373. Transport was healthy,
  receiver state was fresh, parser anomalies and unknown records were zero, and
  the selected session was `unicore`. Runtime identity matched the inspected
  image. No position was recorded in the evidence.
- The available sky state did not yield a fix or corrections: fix remained
  invalid, satellites 0, and correction input inactive. These are separate
  receiver/caster acceptance limits, not failures of native image execution or
  serial ingestion. NTRIP was intentionally unused; its test credentials were
  neither accessed nor persisted, and the external live config had empty
  credential fields.
- Both receiver-backed containers stopped through the SIGINT path in about one
  second with exit 0, OOM false, and restart count 0. The selected serial device
  was released. Existing Mowgli containers were not stopped or changed; the
  exact source tree, native image, protected external configs/logs, and stopped
  evidence containers remain on the second RPi for later campaign phases.
- Phase B is PASS and closes only the canonical `arm64 image validation` gate.
  v0.7 advances 45/65 -> 46/65 and Project Roadmap 71/194 -> 72/194; UGA remains
  33/205. External-LAN DDS, robot, restart, persistence, rate, RTK Fixed,
  incarnation, publication, and other hardware gates remain unclosed.
- Tracker validation passed: status regeneration/check, five focused backlog
  tests, checkpoint audit plus four focused checkpoint-audit tests, and
  `git diff --check`.

## AVAILABLE_HARDWARE

Inventoried for this campaign; re-resolve identity before any state-changing action:

| Resource | Intended role | Gates it can support |
| --- | --- | --- |
| Robot host | Production-like Docker, GNSS receiver, caster/network, controlled restart/reboot | robot runtime, receiver/NTRIP, lifecycle, USB and persistence matrices |
| Second Raspberry Pi | Independent physical arm64 LAN peer/observer | native arm64 runtime and external-LAN DDS peer |
| u-blox / UM982 | Receiver-specific serial/configuration evidence | selected lifecycle, rate, RTK, USB, and profile cases |
| Local NTRIP caster | Non-secret correction/reconnect evidence | DNS/reconnect and correction health only |

Never assume a receiver is mapped to the second Pi or that it can prove a
receiver/caster gate until its physical topology is recorded.

## REMAINING V0.7 GATE CLASSIFICATION

The 18 unchecked items in the fixed 65-item release scope each have one
primary classification:

| Primary classification | Count | Canonical unchecked gates |
| --- | ---: | --- |
| `PUBLICATION_REQUIRED` | 1 | multi-architecture CI build/publish pipeline |
| `HARDWARE_RECEIVER_REQUIRED` | 5 | receiver-process restart/no stale state; low-receiver/high-publication rate; high-receiver/low-publication rate; RTK Float/Fixed transition; no stale state across incarnations |
| `USB_PHYSICAL_ACTION_REQUIRED` | 2 | serial renumbering; F9P↔UM982 physical swap/recovery |
| `POWER_CYCLE_REQUIRED` | 1 | persistent receiver/profile configuration |
| `ALREADY_PARTIAL` | 2 | persistent diagnostic/log/export directory; no-receiver healthcheck behavior |
| `DESIGN_CONTRACT_REQUIRED` | 2 | functional Docker healthcheck; structured logs suitable for Docker/Compose/BlueOS |
| `ROBOT_REQUIRED` | 1 | Docker DNS/reconnect |
| `LONG_DURATION_REQUIRED` | 1 | long-run container validation |

External-LAN DDS has no separate unchecked line in the fixed 65-item list. Its
separate acceptance matrix is `ROBOT_PLUS_SECOND_RPI`; do not count it as a
completed v0.7 checkbox until the canonical tracker changes. Non-ROS API,
WebUI, BlueOS/Bazaar, MowgliNext integration, and publication remain
`DEFERRED_BEYOND_V0_7` or `PUBLICATION_REQUIRED` as already scoped.

## TEST_ORDER

1. **Phase A — freeze and inventory.** Record branch/SHA/image digest and OCI
   identity, `uname -m`, Docker client/server, OS/kernel, ROS distro/RMW/Fast
   DDS environment, interfaces/IP/prefix, firewall visibility, explicit
   domain, receiver by-id target/realpath/major:minor, model/firmware if
   read-only available, and configuration/profile state. Establish rollback
   image/config/container identifiers before testing.
2. **Phase B — native arm64 runtime.** On a real `aarch64` robot or second Pi,
   run the existing image with normal least-privilege settings. Confirm image
   architecture and runtime identity; exercise no-device start/clean stop.
   Add live receiver/caster evidence only if a correctly selected physical
   receiver is actually available. Do not use QEMU or infer hardware support
   from a no-device run.
3. **Phase C — robot GNSS/runtime baseline.** On the robot, resolve exactly
   one expected stable by-id identity before creating the container; map only
   that resolved device to `/dev/gnss-receiver`, with its required group. Run
   the existing read-only external configuration and obtain bounded
   receiver/NTRIP/status evidence. This is the control for later lifecycle
   tests, not a repeat of an already-proven campaign.
4. **Phase D — physical-LAN DDS.** Keep default Docker bridge. Machine B and
   the robot container use one explicit `ROS_DOMAIN_ID`; prove discovery and
   payload flow independently in B→container and container→B directions with
   a minimal deterministic topic. Then use a different domain and require no
   delivery. Record Fast DDS/RMW configuration, selected interfaces, and
   inspectable firewall/multicast facts. Do not use host networking as a
   substitute; test a minimal alternative only after recording a real bridge
   failure.
5. **Phase E — bounded lifecycle/network.** With the baseline receiver still
   healthy, test controlled NTRIP interruption/DNS reconnection if it is
   independently observable. Test application process crash, explicit
   container restart, daemon restart, and host reboot separately, restoring
   the known configuration each time. Observe Docker restart policy and status
   after each action; no one result stands for another.
6. **Phase F — USB identity and incarnation.** Capture by-id, tty, and device
   major:minor before/after a controlled unplug/replug. Credit serial
   renumbering only if tty *or* major:minor actually changes. Test a receiver
   swap only if the expected identity can be unambiguously distinguished and
   show that no other receiver is silently attached. Recreate the container
   after device loss as the established contract requires.
7. **Phase G — persistence/power boundary.** Distinguish external container
   configuration persistence, runtime-only receiver provisioning, and actual
   persistent receiver profile behavior. A USB power loss may demonstrate the
   already-known UM982 replay requirement; it cannot establish persistent
   receiver configuration without an approved persistent-write matrix.
8. **Phase H — opportunistic RTK/rate.** While the normal caster/sky state is
   available, record RTK state transitions only if naturally observed. Run
   bounded rate-mismatch sanity checks only with explicit rates, sequence and
   freshness observations, and a clean restore. Do not force RTK Fixed.
9. **Phase I — schedule endurance.** Leave long-run/rate endurance only after
   interactive work is complete and a rollback path is known.

## ACCEPTANCE_CRITERIA

| Test | Gate / UGA | PASS | PARTIAL / BLOCKED | Must not infer |
| --- | --- | --- | --- | --- |
| Native arm64 | arm64 image validation | Actual `aarch64` host runs the inspected arm64 image and required receiver/caster path where available | No-device runtime proves packaging/lifecycle only; no correctly mapped receiver/caster is PARTIAL | QEMU or `docker image inspect` alone proves no native runtime/hardware behavior |
| Robot baseline | robot Docker validation | Robot runs the existing least-privilege image with selected receiver and bounded GNSS/NTRIP evidence | Missing selected identity, serial permission, caster, or status access is BLOCKED with exact reason | A transient tty, broad `/dev`, or a different receiver is not an acceptable substitute |
| Physical LAN DDS | pending external-LAN matrix | Same-domain discovery and payload in both directions, plus different-domain non-delivery, on two machines | Any missing peer/firewall/interface evidence is BLOCKED; direction-only result is PARTIAL | `ROS_DOMAIN_ID` is not security; same-host/host-net tests are not external LAN |
| DNS/NTRIP reconnect | Docker DNS/reconnect | Receiver remains healthy while independently observed resolver/caster interruption reconnects and correction diagnostics recover | No isolated interruption or missing healthy receiver is PARTIAL | Generic NTRIP success is not DNS/reconnect evidence |
| Process crash | container crash/restart; UGA-126 only if qualified | Deliberate receiver-process crash has recorded exit/restart policy/config and fresh post-start status | `docker kill` tests only manual container termination; missing status evidence is PARTIAL | Restart alone does not prove stale-byte cutoff |
| Container/daemon/host restart | config reapply; host reboot/autostart | Each named action separately restores expected image/config and records Docker policy and exit/status | Cannot perform action or ambiguous autostart is BLOCKED | Container restart, daemon restart, and host reboot are different contracts |
| USB renumbering | serial renumbering | Before/after stable identity and tty/major:minor show an actual changed tty or major:minor and selection/recreation remains safe | Unchanged numbers retain prior non-renumbering evidence only | Replug or unchanged tty/major:minor is not renumbering |
| Receiver swap | F9P↔UM982 swap/recovery | Expected identity is selected or startup fails safely; no silent substitution; recovery uses explicit re-resolution/recreation | Identity ambiguity or only one receiver is PARTIAL/BLOCKED | A successful connection to any serial node is not wrong-receiver prevention |
| Incarnation | no stale state; UGA-126 | A test-correlated prior-incarnation byte/response is explicitly excluded after the new trusted boundary | Parser reset/close/open/replug alone is PARTIAL | No stale state may be claimed from status clearing or no observed error |
| Persistence | persistent receiver/profile | Approved persistent profile operation survives documented power boundary and is distinguished from mounted config | Runtime-only replay and config mount persistence remain PARTIAL | UM982 runtime provisioning is not persistent receiver config |
| RTK Fixed | RTK Float/Fixed | Valid naturally observed Float→Fixed state with fresh correction/solution context | Float-only remains PARTIAL | Fixed must not be forced or fabricated |

## SAFETY_BOUNDARIES

Allowed without extra approval: read-only inspection, logs/status, start/stop/
recreate containers, restart ROS processes, controlled host reboot, controlled
USB unplug/replug, network interruption, read-only receiver probing, and the
already-established runtime-only provisioning.

Require explicit approval: FRESET; persistent receiver flash/config writes;
firmware updates; destructive filesystem actions; broad `/dev` mounts;
`--privileged`; robot safety/control behavior changes; or anything affecting
mower/blade actuation. Robot motion/blade actuation is never required here.

Always resolve the stable by-id target before each container creation. Map one
device only; never select a receiver by tty number alone, silently substitute a
device, expose secrets, or record credentials/authorization headers.

## DO_NOT_REDO

- amd64 Docker packaging/lifecycle and QEMU arm64 packaging campaigns;
- same-host DDS or same-host domain-isolation tests;
- prior u-blox/UM982 live GNSS, NTRIP, RTCM, RTK Float, and USB-loss evidence;
- manual `docker kill` result (exit 137/restart count 0), which is not crash
  recovery evidence;
- implementation or test changes for sandbox `Operation not permitted` socket
  limits.

## KNOWN_ENVIRONMENT_LIMITATIONS

The current workspace is a Docker namespace (`172.17.0.6`, bridge
`172.17.0.0/16`) with Fast DDS `rmw_fastrtps_cpp` and
`ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`; it has no independent LAN peer. Its
socket sandbox may report `Operation not permitted` for Fast DDS/NTRIP. These
facts block external-LAN acceptance locally but do not predict robot behavior.

## SECOND-RPI PREPARATION CHECKLIST

- Record OS/kernel and `uname -m` (`aarch64` required for native evidence).
- Have Docker and only the ROS distro/tools needed for a minimal publisher and
  subscriber; source the intended ROS environment.
- Record interface/IP/prefix, peer reachability, clock/time, firewall
  visibility, `RMW_IMPLEMENTATION`, Fast DDS/discovery variables, and explicit
  `ROS_DOMAIN_ID`.
- Obtain the exact immutable image reference/digest or build input and
  repository SHA; do not build from an untracked workspace.
- Ensure the operating user can run Docker and ROS tools. Serial permissions
  are needed only if a deliberately selected receiver is physically mapped.

## ROBOT PREPARATION CHECKLIST

- Record branch/SHA, image digest/OCI identity, architecture, Docker
  client/server, OS/kernel, ROS distro/domain/RMW/Fast DDS environment.
- Record network interface/IP/prefix/firewall visibility and local caster
  reachability without exposing credentials.
- Resolve and record receiver model/family, stable by-id path, real tty,
  major:minor, ownership/group, and existing persistence/runtime-profile state.
- Verify the selected receiver is the intended device before mapping it and
  capture the read-only config identity/checksum and rollback image/config/
  container state.

## EXPECTED_GATES_TO_CLOSE

The native-arm64 image and robot Docker gates are closed by Phases B and C.
Conditional on the exact PASS criteria, subsequent authorized phases can close:
physical-LAN DDS acceptance record; Docker DNS/reconnect; container
crash/restart; host reboot/autostart; and deterministic container configuration
reapplication.

USB renumbering, swap/recovery, receiver/profile persistence, rate mismatch,
RTK Fixed, and UGA-126 may yield useful evidence but close only if their stricter
conditions occur. The multi-architecture publication pipeline, functional
healthcheck design, structured cross-surface logging design, persistent
diagnostic directory completion, and non-ROS/API/BlueOS/MowgliNext work are
not expected to close tomorrow.

Machine allocation: robot-only tests are baseline GNSS/NTRIP, DNS/reconnect,
process/container/daemon/host lifecycle, USB/swap/persistence, rate, and RTK;
the second Pi alone supplies native-aarch64 runtime evidence; both machines are
required for external-LAN DDS. No MowgliNext or MAVROS behavior is in scope.

## LONG_DURATION_FOLLOWUPS

| Follow-up | Recommended duration | Observe | Failure meaning | Unattended |
| --- | --- | --- | --- | --- |
| Long-run container | 8–24 h | process health, receiver freshness, NTRIP/RTCM, resource use, bounded logs, unexpected restarts | needs triage; not automatically a receiver failure | Yes, after baseline/rollback capture |
| Rate-mismatch endurance | 1–2 h after bounded sanity case | explicit input/output rates, sequence/freshness, backlog/drop behavior | rate contract needs evidence or correction | Yes, with non-secret status capture |
| RTK opportunity | 1–4 h during valid sky/caster conditions | correction health and natural Float/Fixed transitions | environment may be insufficient; do not force | Yes, if receiver installation is safe |

## STOP_CONDITIONS

Stop the affected branch and record BLOCKED/PARTIAL if receiver identity is
ambiguous; a test would require broad device access, privilege, persistent
writes, reset, firmware change, or robot actuation; credentials would be
exposed; the two hosts are not physically/routably independent; the selected
receiver/caster is unavailable; or evidence cannot distinguish the requested
contract. Restore the recorded container/configuration rollback point after
each disruptive test. Never substitute a similar-looking result.

## EXACT_NEXT_STEP

Phases B, C, D, and E are complete. Stop before Phase F without fresh
authorization. Preserve the restored robot baseline and the retained DDS and
lifecycle evidence; do not rerun earlier phase evidence without an invalidating
change.

## PHASE_C_PREFLIGHT_2026_09_05

- At 14:42:57Z the robot remained native aarch64 with Docker 29.8.0 arm64.
  The selected u-blox link still resolved to `/dev/ttyACM1`, character device
  `166:1`, USB `1546:01a9`, driver `cdc_acm`, `root:dialout`, mode `0660`.
- Phase A rollback identity was unchanged: running `mowgli-gps` container
  `bf7bfaa9754f4817277359964667b65faf7851f74006551d490c0a7d0ec439b2`,
  image `sha256:267028885b2f63b8e65c76d1705d065d8a89b21352fb0dab00e414bc5e0fb096`,
  restart policy `unless-stopped`, restart count 0, and configuration SHA-256
  `f779520a50dadca060ff1082b2f5f93e2677f17c22398297984af25ede91dedc`.
- `mowgli-gps` alone held `/dev/ttyACM1` at process fd 9. Stopping only this
  related container is strictly necessary for exclusive serial validation;
  the GUI, watchtower, MQTT, ROS2, and lidar containers remain untouched.
- The current image was absent on the robot. Load it before the bounded GPS
  interruption, prepare protected external configuration/log paths, then
  re-resolve the device immediately before container creation.
- The exact Phase-B image was then streamed directly from the second RPi and
  loaded on the robot. Robot inspection matches image ID
  `sha256:1b01bb887132dccb425107c55f5d1b5c1cf840312c10cd0dc98d59bc0dd8184b`,
  `linux/arm64`, uid/gid 1000, revision `90325b2a05501341b9e6f88460d8c4b194c7bf25`,
  version `ug-plan-005-90325b2`, source timestamp
  `2026-09-04T22:05:54+00:00`, SIGINT, and tini group forwarding.
- A credential-empty u-blox config and external log path were prepared beneath
  `/home/pepeuch/ug-plan-005/runtime/kilted-90325b2`. The config is
  operator-owned mode 0600, SHA-256
  `3a2c4622755984c2b5fd07107637b824866140913750710450aa5b9638f9b3df`,
  and will be mounted read-only. Its NTRIP target is a non-listening loopback
  port, so this initial receiver baseline uses no credential or external caster.
- Immediately before the bounded interruption, the selected device and rollback
  tuple still matched. Only `mowgli-gps` was stopped; it exited 0, OOM false,
  restart count 0, and released the receiver. GUI, watchtower, MQTT, ROS2, and
  lidar remained running.
- Current validation container `ug-plan-005-robot-kilted`, ID
  `2a74bd30e1aa3cf70bf318976fa7bc0797443833daad5e7c8c4e8fa9160174b8`,
  is Docker-healthy under the exact image. Inspection proves uid/gid 1000,
  non-privileged default bridge, restart `no`, the sole u-blox by-id grant to
  `/dev/gnss-receiver`, supplemental GID 20, a read-only external config, a
  writable external log path, and bounded Docker `local` logs (10 MiB x 3).
  The process tree is tini -> ros2 launch -> receiver and NTRIP nodes.
- Two location-redacted snapshots four seconds apart progressed from accepted
  position sequence 691 to 731, runtime observations 2172 to 2297, and runtime
  updates 2074 to 2194. Both reported a valid normal GNSS fix, 28 satellites
  used and 40 visible, selected u-blox session, exact runtime image identity,
  and parser-health level OK with zero recent/total anomalies, malformed,
  rejected, or unknown records.
- The deliberately credential-empty loopback baseline had no correction
  availability, differential-correction state, receiver correction activity,
  or RTCM forwarding. No RTK mode beyond `none` is claimed from this cell.
- Two attempted transitions toward a protected NTRIP cell failed before any
  NTRIP container or caster connection existed: first during transition setup,
  then because the expected tmpfs config was absent at preflight. The first
  rollback briefly overlapped the restored legacy GPS and still-running UG
  baseline; this was detected immediately and the UG container stopped cleanly
  with exit 0. The final state is the stopped UG baseline, restored
  `mowgli-gps` with exclusive receiver ownership, and no sensitive tmpfs file.
  No receiver state or durable configuration changed.
- The operator authorizes one actual bounded NTRIP attempt with the known values
  used only in memory. Generate and consume them within one remote transaction,
  never print/hash/checkpoint them, then unlink the tmpfs file before restoring
  the exact legacy tuple. If the outcome is not obvious, record PARTIAL and stop
  Phase C without broad debugging or retrying the caster attempt.
- The ownership preflight initially misclassified an OCI exec error from the
  shell-less watchtower image as an open fd. One narrow read-only check showed
  that the actual unique fd was the legacy GPS process on `/dev/ttyACM1`; no
  second serial owner existed.
- The one real bounded NTRIP cell passed. Its configuration was derived directly
  into operator-owned mode-0600 tmpfs, mounted read-only, and never printed,
  hashed, checkpointed, or persisted. `mowgli-gps` stopped cleanly and released
  the device before the same least-privilege current image started.
- Two location-redacted snapshots progressed from sequence 70 to 195 and
  runtime observations 220 to 613. Both had a valid fix, 27 satellites used and
  41 visible, healthy receiver/transport/parser, fresh state, active
  differential corrections, and naturally observed RTK Fixed. NTRIP reported
  streaming, integrity-valid correction flow, GGA injection, and active RTCM
  forwarding; 138 frames had already been published, with valid decoded 1006,
  1230, and GPS/GLONASS/Galileo/BeiDou MSM7 semantics and zero decode/malformed
  failures in the bounded observation.
- This proves the robot Docker baseline only. No Float-to-Fixed transition,
  DNS/reconnect interruption, old-byte cutoff, or persistent receiver behavior
  was exercised or inferred.
- The NTRIP validation container stopped via SIGINT in one second with exit 0,
  OOM false, restart count 0, and released the device. The tmpfs secret was
  unlinked before exact restoration of legacy container
  `bf7bfaa9754f4817277359964667b65faf7851f74006551d490c0a7d0ec439b2`,
  image `sha256:267028885b2f63b8e65c76d1705d065d8a89b21352fb0dab00e414bc5e0fb096`,
  restart count 0, config checksum unchanged, and `/dev/ttyACM1` reopened at fd
  9. Every other Mowgli service remained running.
- Phase C receiver/NTRIP/runtime is PASS and closes only `robot Docker
  validation`: v0.7 46/65 -> 47/65, Project Roadmap 72/194 -> 73/194, UGA
  unchanged 33/205. Stop before Phase D.
- Tracker regeneration/check, five focused backlog tests, checkpoint audit, and
  `git diff --check` passed after the single gate closure.

## PHASE_E_LIFECYCLE_2026_09_05

Validated baseline: robot `Peuchmower2`, aarch64 kernel
`6.18.39+rpt-rpi-2712`, Docker 29.8.0 with live restore false, current image
`sha256:1b01bb887132dccb425107c55f5d1b5c1cf840312c10cd0dc98d59bc0dd8184b`
at revision `90325b2a`, external credential-empty read-only configuration SHA-256
`3a2c4622755984c2b5fd07107637b824866140913750710450aa5b9638f9b3df`, and
the sole selected u-blox by-id grant to `/dev/gnss-receiver` with GID 20.
Every running validation state was uid/gid 1000, non-privileged, default bridge,
and had no broad `/dev` mount. Docker running/process health was never accepted
without fresh location-redacted receiver snapshots.

- **PASS — clean stop/start.** Exact trigger
  `docker stop --time 10 ug-plan-005-robot-kilted` at `19:28:27Z` exited 0,
  OOM false, restart count 0, policy `unless-stopped`, and released the device.
  Exact legacy GPS ID/image/config was restored during the stopped interval and
  reopened `/dev/ttyACM1` at fd 9. After re-releasing it, exact trigger
  `docker start ug-plan-005-robot-kilted` at `19:28:32Z` was healthy by
  `19:28:38Z`; sequence advanced 46 -> 75 and observations 144 -> 235.
- **FAIL — isolated receiver-process recovery.** Exact trigger at `19:29:41Z`
  was SIGKILL of only `receiver_node` via `docker exec`. ROS recorded exit -9;
  container state remained running/exit 0, OOM false, restart count 0, policy
  `unless-stopped`. Receiver stayed absent and Docker became unhealthy by
  `19:31:08Z`; no automatic recovery occurred. Explicit operator
  `docker restart --time 10` restored healthy fresh GNSS but does not upgrade
  the automatic result.
- **FAIL — explicit Docker kill automatic restart.** Exact trigger
  `docker kill --signal KILL ug-plan-005-robot-kilted` at `19:32:18Z` produced
  signal 9/exit 137, OOM false, restart count 0, policy `unless-stopped`; it
  remained exited through `19:34:02Z`. This is manual container-termination
  behavior, not process-crash, daemon, or reboot evidence. Legacy GPS was
  immediately restored.
- **PASS — Docker restart-policy recovery.** Exact unexpected trigger at
  `19:38:28Z` was SIGKILL of container PID 7, the `ros2 launch` main child of
  tini, via `docker exec`. Docker wait/die recorded exit 137, OOM false; the
  same container automatically started at `19:38:28.431Z`, restart count 0 -> 1,
  policy `unless-stopped`, and was healthy by `19:38:34Z`. Sequence advanced
  56 -> 85 and observations 176 -> 267.
- **PASS — Docker daemon restart.** Exact authorized trigger
  `sudo -n /usr/bin/systemctl restart docker` at `19:52:04.598Z` returned 0 at
  `19:52:17.874Z`. With live restore false, Docker recorded SIGINT/exit 0, OOM
  false; boot ID stayed `3248d0c3-c299-4977-ac2b-3acd3722b02a`, dockerd PID
  changed 46198 -> 49209, and current-UG automatically started at
  `19:52:16.115Z` with restart count 0 and the same policy. Sequence advanced
  55 -> 88 and observations 172 -> 276. All five unrelated running Mowgli
  containers returned with the same IDs; explicitly stopped legacy stayed
  stopped.
- **PASS — host reboot/autostart.** Exact authorized trigger
  `sudo -n /usr/bin/systemctl reboot` at `19:54:01.944Z` returned 0. SSH became
  unavailable at `19:54:22.884Z` and returned at `19:54:38.586Z`; boot ID
  changed to `c6292218-bbc3-4262-9374-877b53a82550`. Current-UG prior exit was
  0/OOM false and it autostarted at `19:54:38.322Z`, restart count 0, policy
  `unless-stopped`. Sequence advanced 312 -> 345 and observations 981 -> 1084.
  All five unrelated Mowgli containers returned with the same IDs; explicitly
  stopped legacy stayed stopped.

PROVISIONING / LIMITS: no operator receiver/runtime provisioning was replayed
for this u-blox across any software/daemon/reboot boundary; the same mounted
configuration restored healthy runtime. This proves only the directly observed
deployment behavior. It is not USB hotplug/renumbering/incarnation evidence,
does not prove receiver-profile persistence, does not qualify UGA-126, and does
not supersede the UM982 power-loss runtime-profile replay requirement. NTRIP
was not needed and no runtime secret was used.

CLEANUP / ACCOUNTING: current-UG stopped cleanly at `19:56:35Z` with exit 0,
OOM false, restart count 0; its test-only policy was restored to `no` and it
released the device. Exact legacy container/image/config was running by
`19:56:38Z`, restart count 0, with `/dev/ttyACM1` at fd 9. All five unrelated
Mowgli containers remained running under their original IDs, and only Phase-E
helpers were removed. `container restart with deterministic configuration
reapplication`, `container crash/restart validation`, and `host reboot/autostart
validation` close: v0.7 47/65 -> 50/65, Project Roadmap 73/194 -> 76/194, UGA
unchanged 33/205. Receiver-child recovery, `docker kill` automatic restart,
DNS/reconnect, USB/incarnation, and persistence remain explicit and separate.

## PHASE_F_USB_IDENTITY_INCARNATION_2026_09_05

Validated baseline: same robot, image, revision, external credential-empty
configuration, stable u-blox by-id, and exact legacy rollback tuple recorded in
Phase E. At `20:10:19Z`, `sudo -n -l` confirmed the temporary Phase-E
NOPASSWD rule had been removed; Phase F used no privileged operation. No motion,
motor/blade action, reset, persistent receiver write, firmware change, broad
`/dev`, privileged container, or unrelated Mowgli service change occurred.
NTRIP was not needed.

- **PASS — physical loss/stale detection.** Immediately before the operator
  unplug, the selected by-id resolved to `/dev/ttyACM1`, 166:1 (hex a6:1),
  VID:PID 1546:01a9, driver `cdc_acm`, root:dialout GID 20 mode 0660, with the
  current-UG receiver process as exclusive holder. Fresh observations advanced
  sequence 45 -> 75 and observations 142 -> 236. The physical unplug made the
  by-id and tty disappear at `20:15:04Z`. The container remained running and
  Docker-healthy with exit 0, OOM false, restart count 0, policy `no`, but
  sequence froze at 1093 and observations/updates at 3436/3280. After the
  freshness window, diagnostics explicitly reported transport unhealthy and
  runtime state stale. The retained numeric valid-fix value was cached stale
  state, not a new observation. RTCM forwarding was idle before/during, so no
  forwarding-stop or NTRIP-connectivity result is claimed.
- **PASS — actual serial renumbering.** The same stable u-blox by-id returned at
  `20:18:31Z` as `/dev/ttyACM2`, 166:2 (hex a6:2), with unchanged USB identity,
  driver, and ownership. This closes the renumbering gate because both tty and
  major:minor actually changed; selection never used either transient value.
- **PASS — same-device recovery by recreation; FAIL — transparent in-place
  recovery.** Before recreation, the existing receiver process retained its old
  `/dev/gnss-receiver` fd while the returned host device had no holder. Three
  post-replug observations remained frozen/stale even though Docker still
  reported healthy. Exact triggers at `20:22:13Z` were `docker stop --time 10`,
  `docker rm`, and `docker run` after a fresh by-id resolution. The old container
  stopped exit 0/OOM false/restart count 0/policy `no`; the recreated container
  started at `20:22:14Z`, also exit 0/OOM false/restart count 0/policy `no`.
  It ran as 1000:1000, non-privileged bridge, with no added caps, GID 20,
  exactly one expected by-id mapping, and no broad `/dev`. Fresh GNSS advanced
  sequence 47 -> 84 and observations 148 -> 264 with healthy/non-stale
  transport/parser and zero anomalies. No receiver provisioning was replayed or
  required for this u-blox.
- **PASS — wrong-receiver swap safety.** After a final fresh u-blox observation,
  exact trigger `docker stop --time 10` at `20:25:19Z` stopped current-UG exit
  0/OOM false/restart count 0/policy `no` and released the device. The operator
  physically replaced it with the second-RPi UM982 USB. The u-blox by-id was
  absent; the distinct UM982 appeared as
  `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` -> `/dev/ttyUSB0`, 188:0,
  VID:PID 1a86:7523, driver `ch341`, with no holder. Exact trigger
  `docker start ug-plan-005-robot-kilted` at `20:27:43Z` returned command code 1
  with an explicit missing-u-blox-device error. The container remained exited
  with exit 128, OOM false, restart count 0, policy `no`; the UM982 stayed
  unopened, proving no silent substitution. After physical restoration, the
  u-blox returned to the robot as `/dev/ttyACM1` / 166:1 and the UM982 returned
  to the second RPi as `/dev/ttyUSB1` / 188:1 under their unchanged by-id/USB
  identities. Recreating against the u-blox by-id restored healthy advancing
  sequence 45 -> 82 and observations 142 -> 258, again under the exact
  least-privilege contract and without provisioning replay.
- **PASS — required UM982 runtime-profile restoration after swap power loss.**
  The physical swap power-cycled the UM982, so its previously established
  volatile profile was explicitly replayed after it returned to the second
  RPi. Immediately before apply at `20:43:49Z`, its stable by-id resolved to
  `/dev/ttyUSB1` / 188:1 and had no holder. Exact trigger was a one-off,
  network-disabled, uid/gid-1000 container running the already-established
  guarded `gnss_config_apply` UM982 high-precision runtime-only plan. All 14
  commands completed; persistent commands 0, factory-reset commands 0, no
  FRESET, and no SAVECONFIG. The stale-grant evidence container was recreated
  against exactly that by-id and credential-empty read-only config; it was
  non-root/non-privileged with no broad `/dev`, and fresh observations advanced
  sequence 49 -> 104 and runtime observations 77 -> 164 with healthy/non-stale
  transport/parser. Exact trigger `docker stop --time 10` at `20:44:13Z`
  stopped it exit 0/OOM false/restart count 0/policy `no` and released the
  device. This proves replay after USB power loss, not profile persistence.
- **PARTIAL — incarnation/UGA-126 old-data exclusion.** This phase did not
  create a specifically identifiable pending A in incarnation N, correlate a
  forced transport boundary with incarnation N+1, issue same-target B, or
  directly exclude late A bytes/responses from B or trusted runtime state.
  Current interfaces expose no response nonce, transport-incarnation token, or
  qualified cutoff provider and cannot inject/delay a correlated old response
  across this physical boundary. Required instrumentation/contract remains a
  sole-owner response router/arbiter with tagged request epochs plus a
  transport/provider attestation that receiver/bridge/driver queues from N
  cannot enter N+1. USB disappearance, stale-state detection, fd replacement,
  parser/process reset, and container recreation do not close UGA-126.

CLEANUP / ACCOUNTING: immediately before cleanup the selected by-id again
resolved to `/dev/ttyACM1` / 166:1. Exact trigger
`docker stop --time 10 ug-plan-005-robot-kilted` at `20:34:46Z` stopped current-
UG exit 0, OOM false, restart count 0, policy `no`, and released the receiver.
Exact trigger `docker start mowgli-gps` at `20:34:46Z` restored the original
container/image/configuration checksum, policy `unless-stopped`, restart count
0, and exclusive `/dev/ttyACM1` fd 9. A redacted one-message `/gps/fix` probe
returned 0; the older image does not expose the current snapshot Python API.
Final audits at `20:45:52Z`/`20:45:54Z` confirmed every unrelated Mowgli
container remained running under its original ID on both hosts. The robot
current-UG evidence container remains stopped exit 0/policy `no`; the recreated
second-RPi UM982 evidence container remains stopped exit 0/policy `no` with the
UM982 unheld. Only the exact Phase-F remote helpers were removed, and none
remain.
Only `USB serial renumbering validation` and `F9P <-> UM982 physical
swap/recovery validation` close: v0.7 50/65 -> 52/65, Project Roadmap 76/194 ->
78/194, UGA unchanged 33/205. UGA-126, transparent in-place device recovery,
receiver/profile persistence, and other topology claims remain open. Stop
before Phase G without fresh authorization.
