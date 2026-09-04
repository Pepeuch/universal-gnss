# UG-PLAN-005 robot + second-RPi validation

Lifecycle: ACTIVE. This is an execution plan, not evidence of completion.

Repository: `/workspaces/universal-gnss`  
Branch: `feat/docker`  
Baseline: `a5b5755` (`ci(docker): harden v0.7 image contract checks`)

## CONTRACT

Use one production-like robot host and one independent arm64 Raspberry Pi on
the same physical LAN to advance only unresolved v0.7 Docker evidence. Do not
run MAVROS/MowgliNext integration, BlueOS, image publication, or receiver
feature work. `TODO.md` remains the release/accounting source of truth.

Complete a gate only from its stated physical evidence. Record PASS, PARTIAL,
or BLOCKED immediately after each test; regenerate release status only for a
fully closed canonical TODO item.

## CURRENT_STATE

- v0.7: 45/65; Project Roadmap: 71/194; UGA: 33/205.
- amd64 Kilted/Lyrical image/runtime, QEMU/BuildKit arm64 packaging, non-root
  serial mapping, tini/SIGINT lifecycle, external read-only configuration,
  same-host bridge DDS/domain isolation, and u-blox/UM982 live
  GNSS/NTRIP/RTCM evidence are established. Reuse the deployment checkpoint.
- QEMU is not native arm64 evidence. External-LAN DDS is blocked only because
  the current workspace has no independent physical peer; see
  `blocked/UG-PLAN-005_EXTERNAL_LAN_DDS.md`.
- Docker USB-loss contract is established: resolve exactly one stable by-id
  path, map only that device, and recreate the container after loss. A same
  tty/major:minor on replug does not prove recovery. UM982 runtime-only
  provisioning must be replayed after USB power loss.
- UGA-126 and UGA-170 remain PARTIAL. Do not give either completion credit by
  inference.

## AVAILABLE_HARDWARE

Expected tomorrow; verify before any state-changing action:

| Resource | Intended role | Gates it can support |
| --- | --- | --- |
| Robot host | Production-like Docker, GNSS receiver, caster/network, controlled restart/reboot | robot runtime, receiver/NTRIP, lifecycle, USB and persistence matrices |
| Second Raspberry Pi | Independent physical arm64 LAN peer/observer | native arm64 runtime and external-LAN DDS peer |
| u-blox / UM982 | Receiver-specific serial/configuration evidence | selected lifecycle, rate, RTK, USB, and profile cases |
| Local NTRIP caster | Non-secret correction/reconnect evidence | DNS/reconnect and correction health only |

Never assume a receiver is mapped to the second Pi or that it can prove a
receiver/caster gate until its physical topology is recorded.

## REMAINING V0.7 GATE CLASSIFICATION

The 20 unchecked items in the fixed 65-item release scope each have one
primary classification:

| Primary classification | Count | Canonical unchecked gates |
| --- | ---: | --- |
| `NATIVE_ARM64_REQUIRED` | 1 | arm64 image validation |
| `PUBLICATION_REQUIRED` | 1 | multi-architecture CI build/publish pipeline |
| `HARDWARE_RECEIVER_REQUIRED` | 6 | receiver-process restart/no stale state; deterministic container configuration reapplication; low-receiver/high-publication rate; high-receiver/low-publication rate; RTK Float/Fixed transition; no stale state across incarnations |
| `USB_PHYSICAL_ACTION_REQUIRED` | 2 | serial renumbering; F9P↔UM982 physical swap/recovery |
| `POWER_CYCLE_REQUIRED` | 1 | persistent receiver/profile configuration |
| `ALREADY_PARTIAL` | 2 | persistent diagnostic/log/export directory; no-receiver healthcheck behavior |
| `DESIGN_CONTRACT_REQUIRED` | 2 | functional Docker healthcheck; structured logs suitable for Docker/Compose/BlueOS |
| `ROBOT_REQUIRED` | 4 | Docker DNS/reconnect; robot Docker validation; container crash/restart; host reboot/autostart |
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
| Physical LAN DDS | blocked external-LAN matrix | Same-domain discovery and payload in both directions, plus different-domain non-delivery, on two machines | Any missing peer/firewall/interface evidence is BLOCKED; direction-only result is PARTIAL | `ROS_DOMAIN_ID` is not security; same-host/host-net tests are not external LAN |
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

Conditional on the exact PASS criteria, tomorrow can close: robot Docker
validation; physical-LAN DDS acceptance record; native arm64 image validation
when an actual aarch64 runtime (and, if required by the gate, selected receiver
and caster) is available; Docker DNS/reconnect; container crash/restart;
host reboot/autostart; and deterministic container configuration reapplication.

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

Tomorrow morning, before creating or restarting any container, run the passive
inventory on both machines beginning with `uname -m`, `git rev-parse --short
HEAD`, and Docker/image identity inspection. Record the selected stable by-id
realpath and major:minor on the robot before any device mapping. Then proceed
to Phase B.
