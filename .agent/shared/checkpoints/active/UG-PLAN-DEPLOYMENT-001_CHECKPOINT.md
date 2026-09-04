# Agent checkpoint

Repository: `/workspaces/universal-gnss`
Branch: `main`
HEAD: `cf3765a03c83c8858480e786b738fea2eb3d276f` before uncommitted
`UG-PLAN-005` Phase A work

## Objective

Preserve the reusable planning boundary discovered by the BlueOS compatibility
study without making BlueOS a second GNSS implementation.

## Authoritative planning state

`TODO.md` section **Native runtime, API, web, and deployment planning** is the
source of truth for `UG-PLAN-001` through `UG-PLAN-006`. `ROADMAP.md` carries
the release/dependency view. This checkpoint is only a resumption aid.

PROGRESS ACCOUNTING (2026-09-04): the repository's only numeric progress
indicator is the generated UGA dashboard. Its documented formula is the count
of checked/resolved entries among the fixed 205-item UGA baseline; `PARTIAL`,
`BLOCKED`, and hardware evidence do not count as closed. UG-PLAN-005 evidence
therefore advances the concise deployment plan/roadmap without inventing a
second weighted percentage. Before reconciliation the dashboard reported 37
closed/resolved and 168 unchecked (18.05%). The corrected dashboard reports 33
checked/intentionally removed and 172 unchecked (16.10%): four findings
(`UGA-131`, `UGA-160`, `UGA-166`, `UGA-167`) are correctly PARTIAL and excluded
from checked progress. The 33 consists of 21 checked entries, 4 implemented
intentional removals, and 8 duplicate removals; it is not an implementation
count. Its lifecycle mix
also reflects the authoritative UGA-126 PARTIAL classification. No UG-PLAN
item entered the fixed 205-item denominator or its resolved numerator.

PROJECT IMPLEMENTATION INDICATOR (2026-09-04): this reconciliation formalizes
a separate equal-weight count of every current TODO checklist item. It reports
50/194 COMPLETE (25.77%) and 144 NOT_STARTED. The UG-PLAN register separately
reports 1 COMPLETE, 3 PARTIAL, 0 BLOCKED, and 2 NOT_STARTED phases; PARTIAL and
BLOCKED receive no fractional credit. There was no earlier formal overall
implementation indicator, so this is an added baseline rather than a heuristic
increase. It includes the completed UG-PLAN-005 Docker, DDS, u-blox, Unicore,
and replug evidence through their checked TODO contracts, while keeping native
arm64, external-LAN/robot DDS, MowgliNext, renumbering, and later phases open.

CURRENT RELEASE INDICATOR (2026-09-04): `v0.6 -> v0.7` is the 65 equal-weight
checklist tasks under the v0.7 Docker/deployment sections through Documentation;
the later non-ROS API surface, its two Networking API-policy tasks, and v0.8
BlueOS scope are excluded by the roadmap. It reports 25/65 COMPLETE (38.46%) and 40 NOT_STARTED, with no fractional
credit for partial/blocked work. This is a subset of the 50/194 project roadmap
worklist, not an additive metric.

TOOLING INCONSISTENCY (deferred, 2026-09-04):
`python3 scripts/agent/checkpoint_audit.py --check` is not a valid gate for the
current compact `docs/status/uga_backlog.json`: its `load_manifest()` searches
for per-object `id`/`finding_id`/`uga_id` fields and consequently reports
`manifest findings: 0`. Its fallback ownership heuristic then treats the first
`UGA-xxx` mention in each shared checkpoint as that checkpoint's owner, yielding
false duplicate `UGA-126` ownership errors. This does not invalidate the
deployment checkpoint or generated UGA dashboard evidence. It is tracked as a
separate unchecked Documentation/Quality tooling item in `TODO.md`; do not fix
it during this bookkeeping reconciliation.

## Established evidence

- CURRENT: `blueos/README.md` defines the adapter boundary and its physical
  device-grant/hotplug risk; reference it rather than copying its design.
- IMPLEMENTED/PARTIAL: `84d4b34` adds generic, non-ROS/non-BlueOS supervisor
  Phase 1 with one explicit serial device, session/runner lifecycle, bounded
  reconnect, incarnation clearing, snapshots, CLI, and fake-transport tests.
- CURRENT: BlueOS compatibility/packaging research, Bazaar metadata template,
  and minimal permission template exist. No Docker image, API, GUI, or BlueOS
  runtime is implemented.
- IMPLEMENTED: `8c6ac20` completes deterministic native-supervisor
  NTRIP/RTCM orchestration and the transport-neutral/ROS2 writer migration;
  the retained closure evidence below remains subject to physical validation.

## Dependency and priority decision

The authoritative dependency/priority order, recorded in `TODO.md`, is:

1. Complete deterministic `UG-PLAN-002` work.
2. Establish `UG-PLAN-005` Phase A: a ROS2-first production Docker baseline.
3. Reuse the portable runtime/supervisor for a native/headless standalone image.
4. Reuse the production container/runtime contract for `UG-PLAN-006` BlueOS
   skeleton/packaging work.
5. Add the generic HTTP API (`UG-PLAN-003`).
6. Add the independent Tailwind WebUI (`UG-PLAN-004`).
7. Integrate the same WebUI into standalone/BlueOS and optionally ROS2-facing
   deployments.

`UG-PLAN-002` is IMPLEMENTED with HARDWARE_PENDING physical validation; its
deterministic closure is recorded below. BlueOS may begin preparatory/skeleton
work only after the container/runtime contract exists, and may initially be
headless/status-oriented.

## Hardware boundary

BlueOS USB hotplug/re-enumeration and Docker device grants are
HARDWARE_REQUIRED. A reopened tty is not proof that the container has a valid
grant for a newly enumerated physical receiver.

## Do not redo

- Do not add ROS2/BlueOS dependencies or duplicate GNSS/parser/runtime/NTRIP
  semantics in the supervisor, API, GUI, Docker, or BlueOS layers.
- Do not restore API/WebUI as prerequisites for Docker.
- Do not make BlueOS higher priority than ROS2 Docker.
- Do not create separate GNSS semantics for native, ROS2, and BlueOS deployments.
- Do not claim Docker or BlueOS runtime support is implemented.

## Exact next step

Validate the `UG-PLAN-005` image on Kilted and Lyrical (`linux/amd64` first),
including no-hardware startup/help and graceful stop, when Docker is available;
do not start BlueOS implementation ahead of the generic container/runtime
contract.

## UG-PLAN-005 Phase A — partial ROS2 Docker baseline

CURRENT_STATE (2026-09-04, amd64 Docker): classic Docker is available through
the host daemon (client 29.1.3, server 29.7.2, default context, amd64). Kilted
builds locally as `universal-gnss:dev-kilted`. The initial runtime build failure
was a real base-image UID/GID collision (`ros:kilted-ros-base` already owns
1000 as `ubuntu`); runtime now uses the configured numeric non-root UID:GID,
which also preserves bind-mount ownership mapping. ROS setup scripts are sourced
before `nounset` because generated setup code reads optional unset variables.

PROVEN_EVIDENCE (Kilted): package exists at
`/opt/universal_gnss/install`; read-only external config was exercised through a
temporary Docker volume containing `docker/parameters.example.yaml`; both
receiver and NTRIP processes start as UID/GID 1000 and receive that file. The
expected no-device discovery and example-caster errors occurred without a
Docker failure. Image inspect: non-root user `1000:1000`, OCI entrypoint,
process-only healthcheck, and no source/build/log directories below the runtime
prefix. Image size was 937,756,283 bytes before later entrypoint-only rebuilds.

SUPERSEDED SHUTDOWN BLOCKER (resolved below): the requested minimal PID-1 shim forks
`ros2 launch`, translates TERM to INT, waits/reaps, and keeps explicit command
overrides as `exec`; it contains no polling or supervisor. On the real Kilted
container, `docker stop --timeout 12` instead reached SIGKILL (exit 137); no
node shutdown logs appeared. Evidence: Bash defers its TERM trap while blocked
in `wait` for this ROS child, so it cannot perform the translation. A previous
polling/process-group workaround did exit 0, but was removed because it violates
the current explicit no-polling requirement. Do not claim bounded graceful stop,
child reaping, or Lyrical validation. Exact next step: choose/authorize a
minimal signal-wakeup mechanism compatible with the no-polling/no-supervisor
constraint; this was replaced by tini evidence below.

CURRENT_STATE (2026-09-04, PID 1 follow-up): that blocker is resolved with
Ubuntu's `tini` package, not a shell supervisor. Runtime installs `tini`; its
ENTRYPOINT is `/usr/bin/tini -g -- /usr/local/bin/universal-gnss-entrypoint`.
The entrypoint only safely sources ROS/install environments, validates the
default external parameter file, and `exec`s either the default launch or an
explicit user command. `STOPSIGNAL SIGINT` selects the signal ROS launch handles
cleanly; tini still group-forwards externally supplied signals. No polling,
job-control, or heavyweight supervisor was added.

VALIDATION (Kilted amd64): rebuild PASS; `bash -n docker/entrypoint.sh` PASS.
With read-only external config volume, PID 1 was tini, launch and receiver/NTRIP
shared the launch process group, all ran as `1000:1000`, and `docker stop
--timeout 12` returned before the bound with `exit=0`, `OOMKilled=false`.
Logs show launch's SIGINT path and both C++ nodes' SIGINT/SIGTERM handlers;
post-stop inspect confirmed the container was not running (therefore no
container child/orphan process remained). Expected no-device/example-caster
errors remained non-fatal.

REMAINING_DELTA (Lyrical amd64): common Dockerfile build PASS, but no-hardware
run fails before lifecycle validation. Its installed prefix contains neither
`libuniversal_gnss_driver.so` nor `libuniversal_gnss_ntrip.so`, while
`receiver_node`/`ntrip_node` dynamically require them (exit 127). This is not
an `LD_LIBRARY_PATH` issue: setup supplies `/opt/universal_gnss/install/lib`,
but the libraries are absent. Compare Kilted's static dependency result and
Lyrical CMake/link/install configuration; fix only the proven common build
packaging cause, then rebuild/retest Lyrical. Keep arm64, DDS topology, real
hardware, hotplug, and robot validation pending.

RESOLVED (2026-09-04, Lyrical runtime packaging): classification was E,
incomplete CMake install rules. Builder-stage and runtime-stage inspection
proved Docker copies the complete `/workspace/install` tree and that sourced
`LD_LIBRARY_PATH` already includes its `lib/` directory; receiver/ntrip ELF
`DT_NEEDED` entries on Lyrical required driver/protocol/transport/NTRIP shared
objects, but those objects were absent even from the builder install prefix.
Kilted's corresponding dependencies are statically linked, which hid the
missing installs. No relevant executable has RPATH/RUNPATH, so neither loader
configuration nor Docker COPY was the cause.

CHANGE: add distro-neutral `install(TARGETS ... ARCHIVE/LIBRARY/RUNTIME)` rules
for `universal_gnss_protocols`, `universal_gnss_transport`,
`universal_gnss_driver`, and `universal_gnss_ntrip`. No build/source tree is
copied to runtime and no separate Dockerfile was added.

VALIDATION (both linux/amd64): classic `docker build` PASS for Kilted and
Lyrical. Both containers started as UID/GID 1000 with tini PID 1, read-only
external parameters, and launch-managed receiver/NTRIP child processes. On
Lyrical, `ldd receiver_node` resolves `libuniversal_gnss_driver.so` from the
install prefix; nodes stayed running and no exit-127/missing-library error
appeared. On both images, `docker stop --timeout 12` exited 0 without OOM or
SIGKILL and logs show ROS launch SIGINT and both children receiving shutdown.
Final Kilted inspection confirms non-root tini entrypoint and no `src`, `build`,
or `log` directories under `/opt/universal_gnss`; only installed ROS/UG runtime
artifacts remain. `bash -n docker/entrypoint.sh` and `git diff --check` PASS.

REMAINING_DELTA: arm64/buildx, DDS topology, hardware receiver/caster/reconnect,
hotplug/device grants, long-run and robot/MowgliNext validation remain pending.

CURRENT_STATE (2026-09-04, arm64/DDS continuation): Docker client 29.1.3 and
daemon 29.7.2 are amd64. No persistent buildx plugin or arm64 binfmt existed.
Classic `docker build --platform linux/arm64` is unusable here: it installed
amd64 layers and then rejected its own intermediate image as platform-mismatched,
including with `--no-cache`. Do not retry that path.

DEPENDENCIES / PROVEN_EVIDENCE: temporary `tonistiigi/binfmt --install arm64`
registered qemu-aarch64; `docker run --platform linux/arm64 alpine uname -m`
returned `aarch64`. A temporary official buildx binary is under
`/tmp/ug-plan-005-buildx-tTKxyC`, with disposable docker-container builder
`ug-plan-005-arm64`; it advertises linux/arm64. The active Kilted build command
is `DOCKER_CONFIG=/tmp/ug-plan-005-buildx-tTKxyC docker buildx build --builder
ug-plan-005-arm64 --platform linux/arm64 --build-arg ROS_DISTRO=kilted -t
universal-gnss:dev-kilted-arm64 --load .`. BuildKit logs prove arm64 package
repositories/packages; it is still compiling `universal_gnss_ros2` under QEMU.

EXACT NEXT STEP: let/load that Kilted build complete; inspect architecture,
entrypoint/user, installed package and expected libraries/no source-build tree.
Then build Lyrical arm64 with the same builder and inspect the installed dynamic
libraries. Only afterward begin the requested DDS host/container topology tests.

RESOLVED (2026-09-04, arm64 packaging): temporary buildx/QEMU is the
authoritative cross-build path in this environment. Kilted arm64 BuildKit build
completed (QEMU colcon: 4m53s) and loaded locally as
`universal-gnss:dev-kilted-arm64`; inspect reports `linux/arm64`, user
`1000:1000`, and tini group-forwarding entrypoint. QEMU smoke sourced ROS and
the installed package successfully; expected ROS2 libraries are present and no
source/build/log tree exists in the runtime prefix.

Lyrical arm64 BuildKit build completed (QEMU colcon: 5m35s) and loaded locally
as `universal-gnss:dev-lyrical-arm64`; inspect reports the same arm64/non-root/
tini contract. QEMU smoke sourced `universal_gnss_ros2`; `ldd receiver_node`
resolves `libuniversal_gnss_driver.so` from `/opt/universal_gnss/install/lib`.
The dynamic driver, NTRIP, protocols, transport, and tools libraries are all
present, confirming the Lyrical install-rule fix applies on arm64. These are
cross-build plus QEMU-emulated runtime observations only: they do not prove
native arm64 behavior, performance, receiver hardware, serial grants, hotplug,
or real caster/network operation.

DO_NOT_REDO: legacy classic cross-platform build is invalid here; do not retry
it or use `--no-cache` as a workaround. Keep the temporary binfmt/buildx state
host-side, reuse builder `ug-plan-005-arm64` and its cache for any continued
cross-build, and do not alter production Dockerfiles for tooling. Remaining:
DDS topology, native arm64 runtime, hardware/caster/serial/hotplug, and robot
validation.

RESOLVED (2026-09-04, initial DDS topology): Kilted amd64 minimal ROS CLI
tests establish bidirectional host/container discovery and `std_msgs/String`
flow on Docker default bridge with `ROS_DOMAIN_ID=91` and
`ROS_LOCALHOST_ONLY=0`. Reverse direction passed independently. Two containers
on a temporary explicit bridge network exchanged messages bidirectionally on
domain 92; a subscriber on domain 93 received zero bytes and timed out while a
domain-92 peer published, proving observed domain isolation. `docker stop`
remained clean for the image baseline.

PROVEN LIMITATION: on this devcontainer-to-host-daemon topology,
`ROS_LOCALHOST_ONLY=1` still allowed a host/container message exchange; do not
treat it as a container/network isolation or access-control mechanism. Default
bridge was sufficient here, so host networking was neither required nor tested.
`docs/ros2_docker.md` records the evidence-driven initial policy: explicit
domain plus default/explicit bridge for same-host development, explicit bridge
for cooperating multiple containers, validate each real host, and do not claim
external-LAN or robot topology support.

REMAINING_DELTA: native arm64 runtime, external-LAN DDS, real robot topology,
receiver/caster/network reconnect, serial grants, hardware/hotplug, and
MowgliNext validation remain pending. Do not redo the completed amd64/arm64
packaging evidence or return to legacy cross-platform builder.

CURRENT_STATE (2026-09-04, physical baseline): Docker daemon host, not the
devcontainer, exposes distinct stable identities: `usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00`
-> `../../ttyACM0` (character 166:0) and `usb-1a86_USB_Serial-if00-port0`
-> `../../ttyUSB0` (character 188:0). Both are root:gid 20, mode 0660. Do not
select by tty number alone; map one resolved selected host node at a time to
`/dev/gnss-receiver`, adding only group 20; neither privileged mode nor a broad
`/dev` mount was used.

HARDWARE / VALIDATION: u-blox has antenna; Unicore has no antenna. Kilted amd64
receiver-only physical test mapped only `/dev/ttyACM0`, ran as 1000:1000 plus
group 20, and remained running. Live `/status` showed sequence 269, valid fix,
latitude 43.9542903/longitude 2.2023881, 28 satellites used, and 40 visible;
corrections were inactive as expected with NTRIP disabled. `docker stop
--timeout 12` exit 0 and ROS SIGINT handler logged. This proves real u-blox
access/observations and lifecycle only, not correction flow or reconnect.

REMAINING HARDWARE_REQUIRED: external temporary config with redacted local-caster
credentials/address must validate NTRIP connect/accept/RTCM/forwarding; then
controlled NTRIP interruption, u-blox disconnect/re-enumeration mapping behavior,
and Unicore-only non-root transport/protocol test. Never record the supplied
password or bake it into image/checkpoint. Native arm64/hardware remains separate.

RESOLVED (2026-09-04, live u-blox local NTRIP): default-bridge TCP reachability
to the operator-supplied private caster endpoint was proven before use. A
temporary read-only Docker volume held external credentials; its workspace-side
temporary source was removed and no credential is recorded here. The combined
Kilted container mapped only u-blox `ttyACM0` with group 20. Live status showed
valid fix, RTK mode 3, 32 satellites used/41 visible, active differential
corrections, and centimetre-level reported horizontal/vertical accuracy.

PROVEN_EVIDENCE: diagnostics showed NTRIP response streaming, integrity-valid
RTCM correction flow, semantic stream health, receiver forwarding, and 334
published frames. Semantic decoding was valid for 1006, 1077, 1087, 1097,
1127, and 1230, including GPS/GLONASS/Galileo/BeiDou MSM7; observed counts were
consistent with the configured roughly-1 Hz stream and slower 1230. No exact
timing claim beyond this live observation. A controlled Docker bridge disconnect
then reconnect restored NTRIP streaming/correction flow/forwarding; the same
receiver and NTRIP process PIDs remained alive, so no receiver restart was
observed. Combined container stopped with exit 0, OOM false.

REMAINING HARDWARE_REQUIRED: perform physical u-blox unplug/replug only with
operator action, recording post-reenumeration stable identity/node/major-minor
and whether the original Docker grant survives; then Unicore-only non-root
transport/protocol validation. The temporary config volume remains external and
must be deleted after this validation series; never print or checkpoint its
credentials.

CURRENT_STATE (2026-09-04, antenna moved / Unicore): daemon host currently
shows only the stable Unicore identity `usb-1a86_USB_Serial-if00-port0` to
`ttyUSB0` (188:0); u-blox is absent. This prevents a valid before/after
live-container u-blox unplug/replug comparison because its previous container
was already stopped. Do not infer that antenna relocation alone proves USB loss;
reconnect the same u-blox USB receiver for that explicit test.

UNICORE HARDWARE RESULT: Unicore-only Kilted container mapped only `ttyUSB0`
to `/dev/gnss-receiver`, ran non-root 1000:1000 plus group 20, and stopped
cleanly exit 0. At explicit 115200, the selected Unicore parser/session opened
but received zero bytes. A non-destructive automatic-baud retry likewise
reported discovery `no_data`; parser counters remained zero. This proves
wrong-receiver prevention, device mapping, non-root open/lifecycle—not Unicore
protocol byte flow, identification, or antenna-derived solution. Do not alter
receiver configuration or factory-reset it to force evidence.

CORRECTION (2026-09-04, current Unicore transport): the earlier dual-interface
`USB_MIDI` / `ttyUSB0` + `ttyUSB1` enumeration occurred during unstable physical
USB connectivity and is not the normal topology. Operator-provided current udev
evidence is authoritative: CH340 (`1a86:7523`), single interface 00, stable
`usb-1a86_USB_Serial-if00-port0` -> `../../ttyUSB0`, character 188:0,
root:gid 20, mode 0660. The receiver now has an antenna. The Docker daemon can
map `ttyUSB0` but does not expose the host's `/dev/serial/by-id` directory, so
the stable identity is recorded from host udev evidence rather than inferred
from the tty number.

PASSIVE RETRY (2026-09-04, USB extension removed): a disposable container
opened only `/dev/ttyUSB0` as non-root 1000:1000 plus group 20. Local client
termios reads at 9600, 19200, 38400, 57600, 115200, 230400, 460800, and 921600
all completed without device error but received zero bytes. No serial write,
configuration, reset, or factory-reset command was issued. This proves the
USB device remained present through the read matrix, but does not identify an
active baud/protocol or establish a driver defect.

RESOLVED (2026-09-04, likely-reset Unicore recovery): operator reported that
the receiver was likely factory-reset, so the preceding passive silence is
correctly interpreted as no configured periodic output, not a transport or
driver failure. The existing installed `gnss_config_apply` path was reviewed
before use; its initial Unicore control query is `VERSIONA`, routed through the
existing response router, and mutating profiles require a known model. The
established physical UM982 profile was selected explicitly. At 115200, Kilted
mapped only `/dev/ttyUSB0` to `/dev/gnss-receiver` and ran non-root 1000:1000
plus group 20. The existing runtime-only `rover_high_precision` profile
received successful responses for all 13 planned commands: rover mode, NMEA
version, RTK/DGPS policy, and the normal GPGGA/GPGSV/GPGST/PVTSLNA/BESTNAVA/
RTKSTATUSA/RTCMSTATUSA/SATSINFOA outputs. No `FRESET`, `SAVECONFIG`, or other
persistent write was sent.

LIVE PROTOCOL / BAUD EVIDENCE: after the CLI closed, the installed read-only
`gnss_serial_monitor` immediately parsed live Unicore fixes, position,
accuracy, HDOP, satellite and CN0 data at 115200. The existing runtime-only
`CONFIG COM1 921600 8 n 1` workflow then closed/reopened the port, probed old
and target settings for three attempts, and confirmed the target with a real
`VERSIONA` response at 921600 before replaying the remaining 13 profile
commands successfully. A six-second read-only monitor at former 115200
produced no parsed Unicore record; 921600 immediately produced valid fixes,
positions and satellite data. This proves protocol-level old/new baud behavior
and actual close/reopen, but does not measure electrical byte cutoff at the
wrong baud. UGA126 is PARTIAL, not PROVEN.

DOCKER UNICORE RESULT: the production Kilted receiver launch with explicit
`receiver_family:=unicore`, `/dev/gnss-receiver`, and 921600 used only the
resolved ttyUSB0 mapping, user 1000:1000, supplemental GID 20, no privilege and
no broad `/dev` mount. `/status` reached observation sequence 77 with valid
fix, position, HDOP, and 18 used / 24 tracked-visible satellites. Diagnostics
selected `unicore` and showed healthy transport/parser with live Unicore ASCII
records. The process tree was `tini -> ros2 -> receiver_node`; `docker stop
--timeout 12` exited 0, OOM false. No COG field is currently projected by this
Unicore status path; the single-antenna status correctly left heading
unavailable, and no receiver-UTC field beyond normal ROS timestamps was
established by this test.

RESOLVED (2026-09-04, Unicore NTRIP): a prior external NTRIP volume selected
`ublox`, so that run was rejected as Unicore-selection evidence and stopped
cleanly. A separate temporary volume was copied internally and changed only to
`receiver_family: unicore` and `serial_baud: 921600`; credentials were neither
printed nor changed. With explicit operator authorization, the corrected
read-only external-config test ran the Kilted container non-root with only
ttyUSB0 -> `/dev/gnss-receiver` and GID 20. It proved NTRIP response streaming,
integrity-valid RTCM flow, valid semantic 1006/1230 and GPS/GLONASS/Galileo/
BeiDou MSM7 health, and receiver forwarding. Receiver diagnostics recorded
162 forwarded frames / 46011 bytes, zero write errors, 159 Unicore receiver
RTCM-status messages seen, and `selected_session=unicore`. `/status` showed
valid RTK float (`fix_type=3`, `rtk_mode=2`), active differential corrections,
and correction age 1 s. The combined `tini -> ros2 -> receiver_node,ntrip_node`
tree stopped via `docker stop --timeout 12` with exit 0 and OOM false. This
proves current Unicore correction acceptance and RTK float; it does not require
or prove RTK fixed.

FACTORY-RESET FINDING: UGA170 is PARTIAL. This is physical evidence that an
already-reset/unconfigured UM982 can recover through the normal existing
runtime profile and provide live GNSS/corrections. It is not evidence of
executing or recovering from a reset during this session, nor of a saved-profile
persistent recovery.

ACTIVE PHYSICAL TEST (2026-09-04, pre-Unicore unplug): the running production
container is `ug-plan-005-unicore-replug`, PID 319524, with only daemon-host
`/dev/ttyUSB0` -> `/dev/gnss-receiver`, hex major/minor `bc:0` (188:0),
root:gid 20, mode 0660, non-root 1000:1000 plus GID 20, and the existing
read-only Unicore 921600 NTRIP config volume. Stable host udev identity remains
operator-provided `usb-1a86_USB_Serial-if00-port0` -> `../../ttyUSB0`; the
daemon does not expose `/dev/serial/by-id` for independent binding inspection.
The process tree is `tini` 319524 -> `ros2` 319604 -> `receiver_node` 319648
and `ntrip_node` 319649. No receiver-incarnation identifier is exposed by the
current status/diagnostics; selected session is `unicore`. Pre-unplug status is
healthy: valid RTK float, corrections active, 24 used/25 tracked-visible
satellites, correction age 1 s; NTRIP streaming/RTCM semantic health and
receiver forwarding were active. Receiver diagnostics advanced from 253 to 264
forwarded frames (71796 to 74678 bytes) across captures, with zero write errors
and receiver RTCM-status count advancing from 248 to 258. PAUSED awaiting only
operator physical unplug of this Unicore USB receiver; do not restart/recreate
or alter the device mapping before recording loss behavior.

PHYSICAL UNPLUG OBSERVED (2026-09-04): operator unplugged only the Unicore.
The existing container remained running with the same `tini`, `ros2`,
`receiver_node`, and `ntrip_node` PIDs. Receiver logs reported `GNSS transport
closed`. A new no-op Docker grant for exactly `/dev/ttyUSB0` failed with `no
such file or directory`, proving host-node disappearance without touching the
existing container. Receiver diagnostics then correctly reported
`transport_healthy=false` and `stale_data=true`, emitted
`rtcm_forwarding_stale` and `rtcm_forwarding_error: Receiver transport is
closed`; forwarding stopped at 432 frames / 122408 bytes while write errors
increased from 305 to at least 371. No new Unicore runtime observations or
receiver RTCM-status messages appeared after the loss (both held at 1534 and
423 respectively in the observed interval). NTRIP itself correctly remained
streaming/healthy because the caster path was unaffected; it did not falsely
make the receiver transport healthy. No receiver-incarnation field is exposed,
so an incarnation change cannot be observed. PAUSED awaiting operator reconnect
of the same Unicore; do not restart/recreate or remap the existing container
before observing its behavior.

PHYSICAL REPLUG / RECOVERY CONTRACT (2026-09-04): the operator reconnected the
same Unicore. The stable host identity
`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` again resolved to
`/dev/ttyUSB0`, major/minor 188:0 (`bc:0`), root:gid 20, mode 0660: neither the
tty number nor major/minor changed in this trial. A fresh direct Docker grant
using that exact stable by-id path succeeded, so only that device can remain the
least-privilege mapping. The original container kept its original PID/process
tree but did not reopen the receiver: receiver diagnostics remained
`transport_closed`, `runtime_state_stale`, `rtcm_forwarding_stale`, and
`receiver_rtcm_stale`, with runtime observations and Unicore lines held at
1534 and 1957. Its NTRIP caster stream remained active, but its receiver
transport did not recover. Thus an already-running Docker container's device
grant/handle does not recover this physical unplug/replug on this host, even
when the same tty major/minor reappears; the observed contract requires
container recreation. No receiver-incarnation field is exposed, so only the
new process/container incarnation is observable.

The original container stopped cleanly with `docker stop --timeout 12` (exit 0,
OOM false). A first recreated by-id-mapped container could open the transport
and forward NTRIP without write errors, but received no Unicore observations.
This is expected from the receiver's non-persistent runtime configuration after
USB power loss, not a Docker grant or loader failure. The normal project-owned
runtime-only UM982 `rover_high_precision` profile was reapplied (no reset and
no save command): 14/14 configuration commands completed across 115200 to
921600, with `VERSIONA@921600` confirmed. A second newly-created container,
again using only the stable by-id mapping, non-root 1000:1000 plus GID 20 and
the read-only external NTRIP volume, recovered live GNSS and corrections:
runtime observations and receiver RTCM status increased, RTCM forwarding grew
from 87 to 143 frames with zero write errors, and status reported valid fix,
active differential corrections and a 1.4 s correction age. It also stopped
cleanly via `docker stop --timeout 12` (exit 0, OOM false). This is physical
replug evidence under the Qinheng CH340 single-interface transport, not serial
renumbering evidence and not native persistence evidence.

EXACT NEXT STEP: Unicore configuration, live GNSS, Docker lifecycle, NTRIP
correction, and physical loss/replug behavior are recorded. Retain the two
temporary credential-bearing Docker volumes only until this hardware-validation
series is explicitly closed, then remove exactly those volumes after verifying
no container uses them. Remaining hardware delta includes serial-renumbering
and other receiver-family re-enumeration trials, plus a deliberate persistent
receiver-provisioning decision if desired. Credentials remain external/redacted.

FINAL HARDWARE_REQUIRED SWEEP (2026-09-04): this is a classification pass over
the current bench evidence only; it does not repeat physical USB loss/replug or
factory reset. The exact bench is the operator-provided UM982 over a Qinheng
CH340 single USB interface (`1a86:7523`), daemon-host stable identity
`usb-1a86_USB_Serial-if00-port0`, group 20, with Kilted amd64 Docker mapping
only that device, non-root 1000:1000 plus GID 20, and the redacted external
local-caster configuration. It does not generalize to a different receiver,
firmware, kernel, bridge, topology, native arm64 host, or BlueOS.

| Finding / contract | Receiver | Evidence | Classification | Remaining delta |
| --- | --- | --- | --- | --- |
| UGA-126: qualified post-indeterminate transport-incarnation cutoff for same-target command A/B | Unicore / u-blox | Physical USB disappearance, stale state, recreation, and new runtime traffic were observed; current serial abstraction still cannot prove a late A response is excluded after B. | PARTIAL | Inject/observe the prescribed A-timeout-or-cancel, qualified recovery, and same-target B matrix with an end-to-end proven byte cutoff and incarnation token. USB replug alone is insufficient. |
| UGA-170: portable factory-reset capability | UM982 plus u-blox scope | Operator reports this UM982 was previously FRESET on robot; this session proved normal recovery from an already-unconfigured receiver and, after USB power loss, normal runtime-profile replay. No FRESET was repeated. | PARTIAL | The exact u-blox F9/F10 model/firmware/protocol, persistent-setting, reset, reconnect, active-probe, direct-USB and claimed-bridge matrix remains absent. Do not infer it from UM982 evidence. |
| Stable identity / least-privilege device grant | UM982/CH340 | Same by-id identity returned and a fresh Docker `--device` grant using it succeeded; only group 20 and one device were used. | PROVEN | No generic cross-host/bridge claim; document/validate fallback where by-id is absent. |
| Real Docker serial loss and reconnect behavior | UM982/CH340 | Existing container retained PIDs but transport closed and health/RTCM became stale; it did not recover after the same node returned. Recreation recovered transport. | PROVEN | Automatic in-place recovery is disproven for this bench; validate any other intended receiver/USB topology separately. |
| USB re-enumeration / renumbering resilience | UM982/CH340 | The device disappeared then returned with the same by-id, ttyUSB0, and 188:0. | PARTIAL | Force/observe an actual tty-number or major/minor change and prove selection by stable identity; test other claimed interfaces. |
| Wrong-receiver prevention | UM982 | Explicit `receiver_family=unicore`, sole-device mapping, selected-session diagnostics, and live Unicore protocol traffic prevented the prior mismatched u-blox-config selection from being accepted as Unicore evidence. | PROVEN | Does not prove automatic selection/replacement safety across different physical receivers. |
| Receiver-incarnation transition / old-data exclusion | UM982/CH340 | Stale observations and forwarding were explicitly invalidated on loss; recreation created new processes. No receiver-incarnation identifier or physical old-byte cutoff is exposed. | PARTIAL | Add/qualify a real incarnation identity and satisfy UGA-126's causal old-byte exclusion contract before claiming a trusted transition. |
| Real NTRIP reconnect and RTCM forwarding after reconnect | live u-blox local caster; UM982 recovery | Earlier controlled Docker-bridge interruption restored real caster streaming, semantic RTCM health, and receiver forwarding without process restart. UM982 after recreation/profile replay also restored live corrections and zero-error forwarding. | PROVEN | External-LAN/caster, long-duration, and native-arm64 cases remain untested. |
| Live observation sequence and freshness | UM982 | Live observations/satellite/fix/correction state advanced before loss, held after loss with explicit stale diagnostics, then advanced again after recreated/profiled recovery. | PROVEN | No receiver-supplied incarnation field or all-topology freshness claim. |
| Runtime-only configuration recovery after power loss | UM982 | Replugged receiver was silent until the normal 115200-to-921600 runtime-only profile was reapplied; then live GNSS, differential corrections, and RTCM forwarding resumed. | PROVEN | Persistence is intentionally not proven: decide separately whether a saved profile/provisioning workflow is required. |
| Native standalone supervisor / BlueOS grants | not on assembled bench | Docker ROS2 evidence exists only; no native supervisor or BlueOS hardware test was authorized. | NOT_APPLICABLE | Retain their distinct HARDWARE_PENDING/HARDWARE_REQUIRED matrices; do not use Docker evidence as a substitute. |

SWEEP DECISION: current Docker/UM982 physical evidence closes the stated
bench-level operational contracts but leaves UGA-126 hardware evidence PARTIAL
(with automatic recovery still outside the proven contract) and UGA-170 PARTIAL. No
destructive reset, serial-renumbering attempt, BlueOS work, or new
implementation was performed. The deployment checkpoint remains ACTIVE because
the listed hardware matrices are still reusable open work.

Decision: use one container running the existing
`receiver_and_ntrip.launch.py`. It preserves the established ROS launch-managed
receiver/NTRIP process topology and shutdown path; no composition container,
API, WebUI, BlueOS runtime, or duplicate GNSS/NTRIP semantics is introduced.

Implemented:

- root `Dockerfile`: one `ROS_DISTRO` build argument (Kilted default; Lyrical
  supported), multi-stage `colcon --merge-install` build, non-root runtime,
  `linux/amd64` and `linux/arm64` as intended targets, OCI version/revision
  labels, and no `arm/v7` claim;
- `docker/entrypoint.sh`: sources ROS/install environments, requires an
  external parameter file for the default launch, creates/checks writable ROS
  logs, and `exec`s ROS launch for direct signal delivery;
- `docker/parameters.example.yaml`: credential-free external configuration
  template; `gnss_ros2` combined launch now accepts `parameters_file` and ships
  a no-op default so existing direct launch invocations remain usable;
- `docs/ros2_docker.md`: explicit device/group, config/secret, log, DDS, and
  health boundaries. Device mapping is one stable host `/dev/serial/by-id/...`
  path to `/dev/gnss-receiver`, never privileged or all of `/dev`.

Health contract: the Docker healthcheck proves only that `receiver_node` and
`ntrip_node` processes exist. It does not represent receiver transport,
observation freshness, NTRIP/RTCM/correction health, or RTK state.

Validation:

- PASS: Kilted `colcon` Release build of `universal_gnss_ros2` using
  `/tmp/ug-plan-005-kilted-{build,install,log}`.
- PASS: installed Kilted `ros2 launch ... receiver_and_ntrip.launch.py
  --show-args`; validates the packaged launch/config path and
  `parameters_file` argument. `ROS_LOG_DIR` had to be set under `/tmp` because
  the sandbox makes `/home/ubuntu/.ros` read-only.
- PASS: outside the sandbox, a three-second no-hardware Kilted combined launch
  using `docker/parameters.example.yaml` started both nodes, accepted the
  external parameters, and was terminated with SIGTERM. The expected missing
  `/dev/gnss-receiver` and unreachable example caster errors occurred; both
  child PIDs were reaped. This is a launch/shutdown check, not a receiver or
  NTRIP health pass.
- PASS: Kilted Dockerfile ROS apt package names resolve in the current apt
  metadata; `bash -n docker/entrypoint.sh`, Python launch compile,
  `python3 scripts/update_backlog_status.py --check`, and `git diff --check`.
- BLOCKED (environment): Docker CLI/daemon is absent, so Kilted/Lyrical image
  builds, image startup, healthcheck, and SIGTERM tests are unexecuted.

Remaining delta: Docker image builds on both distros; `linux/amd64` and
`linux/arm64` validation; no-hardware launch/help; graceful shutdown; DDS
host/container and cross-container topology; real receiver/caster reconnect,
USB hotplug/re-enumeration, device grants, and robot/MowgliNext validation.

DO_NOT_REDO:

- Do not split the first image into a composable or multiple-container layout
  without evidence that the existing launch-managed layout is insufficient.
- Do not make a process healthcheck claim receiver, GNSS, correction, or RTK
  health.
- Do not bake parameters, NTRIP credentials, receiver paths, logs, DDS domain,
  RMW implementation, or network mode into the image.
- Do not add API, WebUI, BlueOS runtime, or separate deployment GNSS semantics.

## UG-PLAN-002 resumption state

Baseline verified 2026-09-04: `main` at `ab32f673da6e9e6ffa8eac9a57b08656f8843645`, clean before this work.

- IMPLEMENTED: `gnss_ros2::ReceiverNode` now owns
  `universal_gnss_transport::RtcmFrameWriter`, with its subscription QoS using
  the writer's 50-frame default capacity. The duplicate `PendingRtcmWrite`
  deque, capacity constant, and flush-result enum are removed.
- PRESERVED: `ReceiverNode` remains the owner of ROS2-specific diagnostics:
  successful-byte/frame counters, last-forward time/type, error counter,
  failure text, terminal transport close, and abandonment at transport terminal
  state. The writer remains the sole owner of partial-head ordering, bounded
  drop-newest FIFO, zero-progress blocking, and incomplete-frame abandonment.
- PASS: `TestRtcmFrameWriterUga009` in `gnss_transport_test_foundation` covers
  the transport-neutral writer contract (capacity 50, drop-newest, partial and
  would-block ordering, hard failure, and explicit incarnation abandonment).
- PASS: `colcon --log-base /tmp/ug-plan-002-ros2-log build --base-paths .
  --packages-select universal_gnss --build-base /tmp/ug-plan-002-ros2-build
  --install-base /tmp/ug-plan-002-ros2-install --cmake-args
  -DUNIVERSAL_GNSS_BUILD_ROS2=ON --event-handlers console_direct+` completed.
- PASS: outside the sandbox, where Fast DDS may access network interfaces,
  `ROS_LOG_DIR=/tmp/ug-plan-002-ros2-log/ros ctest --test-dir
  /tmp/ug-plan-002-ros2-build/universal_gnss --output-on-failure -R
  '^test_receiver_node$'` passed (25.82 s), including the seven existing UGA009
  queue/lifecycle regressions. Sandboxed direct execution is not evidence: it
  denies Fast DDS network-interface setup and initially lacked the CTest
  `LD_LIBRARY_PATH` configuration.
- PASS: `bash scripts/clang_format_21.sh --check
  gnss_ros2/src/receiver_node.cpp` and `git diff --check`.

Do not redo the ROS2 migration or alter `RtcmFrameWriter` without invalidating
the transport-neutral and ReceiverNode regression evidence above. Do not touch
`gnss_runtime` NTRIP integration, API, WebUI, Docker, or BlueOS runtime here.

Historical continuation direction (now completed by `8c6ac20`): compose
native-supervisor NTRIP/RTCM only through the shared writer contract; do not
reintroduce a consumer-local queue.

## Native supervisor NTRIP/RTCM state — PARTIAL

- PARTIAL: `ReceiverSupervisor` now has an independent NTRIP worker owning an
  existing `NtripClient`; production configuration uses `NtripClient::Connect`.
  It forwards only the complete CRC-valid frames surfaced by the client through
  the shared `RtcmFrameWriter`.
- PARTIAL: a receiver-incarnation `ReceiverLink` owns the writer. Active-link
  replacement and every correction flush share `correction_mutex_`; replacement
  abandons the old writer before its transport is released. NTRIP reconnect and
  receiver reconnect paths are separate. `MaybeInjectGga` is called only when
  the authoritative current receiver runtime state exists.
- PARTIAL: supervisor snapshot exposes NTRIP client state/metrics/flow/reconnect
  state, writer queue/overflow/forwarding counters, and only the bounded
  `NtripClientError` category. CLI accepts NTRIP configuration and never prints
  its credentials.
- PASS: native CMake build of `universal_gnss_runtime` and
  `universal_gnss_supervisor`; existing `gnss_runtime_test_receiver_supervisor`;
  `git diff --check`.
- CURRENT_STATE (2026-09-04): the newly added deterministic socket-pair
  exercise remains deliberately unregistered. The reported final
  `configuration` category was traced; it is not the first adopted-socket
  failure and is not evidence of an invalid supervisor `NtripConfig`.
- PROVEN_EVIDENCE: with temporary, credential-free diagnostics, the first
  socket-factory call returned fd `3`. `NtripClient` was `kDisconnected` (0)
  immediately before `AdoptConnectedSocket(3)` and returned `kNone` with state
  `kConnected` (2) immediately afterward. Its first operation was
  `SendRequest`, whose first `TcpClientTransport::Write` called `send(3, ...)`
  and failed with `errno=EPERM` in the sandbox. `SendRequest` therefore
  returned `NtripClientError::kDisconnected` (6), before `request_sent` or
  `response_received` was set; this is `WriteResult{kError, kWriteFailure}`
  mapped by `MapTransportError`, not an Ntrip configuration validation path.
- PROVEN_EVIDENCE: normal `NtripClient::FailWith(kDisconnected)` closed fd `3`,
  moved the client to `kFailed` (4), and scheduled its configured 20 ms
  reconnect. When that deadline elapsed, the supervisor correctly requested a
  second socket; the factory returned fd `5`, for which the same sandbox
  `send(...)=EPERM` sequence occurred. Thus the supervisor did not consume or
  retire fd `3` before the fixture exchange: NtripClient retired it after the
  sandbox denied the first request write.
- PROVEN_EVIDENCE: when the unbounded fixture factory was allowed to exhaust,
  its later `-1` result reached `TcpClientTransport::AdoptConnectedSocket`'s
  explicit `fd < 0` predicate, returning `kInvalidArgument`; `NtripClient`
  maps that to `NtripClientError::kConfiguration`. That later category replaced
  the earlier disconnected error in the last-error snapshot. Do not diagnose
  `configuration` without first distinguishing this exhausted test descriptor
  from the earlier request-write failure.
- PROVEN_EVIDENCE: outside the sandbox, the same diagnostic fixture produced
  fd `3`, pre-adopt state 0, successful adoption/state 2, and successful
  request (`request=0`, connected). It reached the next lifecycle assertion,
  which currently fails: `replaced receiver transport must receive no old RTCM
  suffix`. This is the next real deterministic forwarding/lifecycle delta; it
  is separate from Ntrip configuration/adoption and must be investigated
  without changing NtripClient public semantics or retry policy.
- VALIDATION: sandbox `cmake --build /tmp/universal-gnss-build --target
  gnss_runtime_test_receiver_supervisor -j2 &&
  /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor`
  yielded the fd-3/fd-5 transition above. Sandboxed
  `gnss_ntrip_test_ntrip_client` independently exhibited `send(fd=3):
  errno=EPERM`, proving the denial is not supervisor-specific. The equivalent
  supervisor fixture was rerun outside the sandbox; it passed adopt/request and
  exposed the separate old-sink assertion. After removing diagnostics, the
  existing unregistered supervisor regression rebuilt and passed outside the
  sandbox; `git diff --check` passed.
- DIAGNOSTIC_HYGIENE: temporary traces were added only to
  `gnss_runtime/src/receiver_supervisor.cpp`,
  `gnss_transport/src/tcp_client_transport.cpp`, and
  `gnss_runtime/tests/test_receiver_supervisor.cpp`; all have been removed.
  No production logging, public API, NtripClient rule, or retry/reconnect
  behaviour was added or changed for this investigation.
- CURRENT_STATE (2026-09-04, follow-up): the next outside-sandbox assertion
  was a deterministic-test defect, not a supervisor lifecycle defect. The test
  retained a raw pointer to the first `FakeTransport` and dereferenced it after
  receiver incarnation replacement had destroyed that `unique_ptr`-owned fake.
  The apparent old-sink byte count of zero was therefore undefined behaviour.
  `FakeTransportWriteRecord`, owned by the test, now retains only a locked copy
  of emitted bytes; the test no longer dereferences a destroyed transport.
- PROVEN_EVIDENCE: the registered
  `TestNtripForwardingAndIndependentReconnects` now passes outside the sandbox.
  It proves initial RTCM forwarding through `RtcmFrameWriter`, partial/zero
  progress completion ordering, abandonment/isolation of the replaced receiver
  sink, forwarding to the new receiver incarnation, and NTRIP reconnect without
  restarting the healthy receiver. This test-only repair did not alter runtime,
  writer, NtripClient, or reconnect code.
- VALIDATION (follow-up): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/tests/test_receiver_supervisor.cpp && cmake --build
  /tmp/universal-gnss-build --target gnss_runtime_test_receiver_supervisor -j2
  && /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor`
  passed outside the sandbox. The NTRIP fixture is now registered in that test
  executable.
- REMAINING_DELTA: add explicit deterministic coverage for GGA cadence and
  unavailable/invalid runtime position, stop cancellation of both reconnect
  paths, credentials absent from status/errors, and the remaining requested
  queue/lifecycle/status assertions. Then run focused runtime/NTRIP/transport/
  driver suites, native build, shared ROS2 regression if needed, formatter, and
  `git diff --check`. Hardware/caster reconnect/hotplug remains
  HARDWARE_REQUIRED.
- CURRENT_STATE (2026-09-04, GGA provenance): `RunNtrip` now records the
  receiver session incarnation and last consumed `position_observations` count.
  It calls `NtripClient::MaybeInjectGga` only for a strictly newer authoritative
  receiver position observation in the current incarnation. On incarnation
  replacement the consumed count resets. NtripClient remains the sole owner of
  enabled/fix/coordinate/cadence policy and GGA formatting; this prevents a
  cached snapshot from being treated as a new position observation.
- VALIDATION (GGA implementation): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/src/receiver_supervisor.cpp && cmake --build
  /tmp/universal-gnss-build --target universal_gnss_runtime -j2` passed outside
  the sandbox. Focused deterministic GGA tests remain the exact next step.
- CURRENT_STATE (2026-09-04, GGA tests): the fresh-observation gate initially
  exposed a real ordering issue: a position could be consumed while NTRIP was
  `kConnected`, before its response was accepted. `RunNtrip` now consumes the
  count and calls `MaybeInjectGga` only once the client is `kStreaming`. This
  preserves the same fresh authoritative observation through the connection
  handshake without treating cached state as a new observation.
- PROVEN_EVIDENCE (GGA tests): `TestGgaUsesFreshAuthoritativePosition` uses an
  adopted socketpair and an actual NMEA receiver observation. A valid fix emits
  exactly one GGA over 50 ms of repeated supervisor polls; an invalid-fix GGA
  emits none. The test observes the caster bytes directly, so the request and
  injected GGA cannot be conflated. NtripClient still owns cadence, coordinate,
  and fix-policy decisions.
- VALIDATION (GGA tests): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/src/receiver_supervisor.cpp gnss_runtime/tests/test_receiver_supervisor.cpp
  && cmake --build /tmp/universal-gnss-build --target
  gnss_runtime_test_receiver_supervisor -j2 &&
  /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor`
  passed outside the sandbox.
- CURRENT_STATE (2026-09-04, lifecycle/status): snapshot now projects the
  existing `NtripClient::BuildCorrectionHealth({})` result alongside, rather
  than in place of, NTRIP connection metrics and correction-flow state. Thus
  connection, accepted response, complete-valid frame flow, semantic health,
  reconnect state, forwarding activity, queue state, and receiver incarnation
  remain independently observable.
- PROVEN_EVIDENCE (lifecycle/status): `TestNtripStopAndRedaction` proves a
  caster disconnect schedules the NtripClient reconnect, then `Stop()` cancels
  it before a second socket-factory call; its pre-existing receiver-only
  backoff test proves receiver reconnect cancellation. The test also supplies
  username/password values and proves only the bounded `configuration` category
  appears in snapshot errors. `TestNtripForwardingAndIndependentReconnects`
  now asserts enabled/connected, response accepted, one valid complete frame,
  forwarding active, empty writer queue after flush, forwarding count, and
  receiver incarnation independently.
- VALIDATION (lifecycle/status): `bash scripts/clang_format_21.sh --apply
  gnss_runtime/include/universal_gnss_runtime/receiver_supervisor.hpp
  gnss_runtime/src/receiver_supervisor.cpp gnss_runtime/tests/test_receiver_supervisor.cpp
  && cmake --build /tmp/universal-gnss-build --target
  gnss_runtime_test_receiver_supervisor -j2 &&
  /tmp/universal-gnss-build/gnss_runtime/gnss_runtime_test_receiver_supervisor
  && git diff --check` passed outside the sandbox.
- DO_NOT_REDO: do not repeat NtripConfig field-by-field analysis, adoption
  ordering analysis, or attribute the final `configuration` snapshot to fd 3.
  The exact first-fd sequence and source predicates above remain valid unless
  `RunNtrip`, `NtripClient::AdoptConnectedSocket`/`SendRequest`, or
  `TcpClientTransport::Write` changes. Do not add retries around configuration
  errors, special-case the fixture, relax NtripClient validation, or bypass
  adopted sockets.

CURRENT_STATE (2026-09-04, deterministic closure): UG-PLAN-002 native
supervisor NTRIP/RTCM orchestration is IMPLEMENTED for deterministic software
evidence. It was committed as `8c6ac20`; this documentation-only change does
not create another commit or push.

VALIDATION (closure):

- PASS: full native `cmake --build /tmp/universal-gnss-build -j2`.
- PASS: outside the sandbox, `ctest --test-dir /tmp/universal-gnss-build
  --output-on-failure -R '^(gnss_runtime|gnss_ntrip|gnss_transport|gnss_driver)_test_'`:
  33/33 runtime, NTRIP, transport, and driver tests.
- PASS: ROS2-enabled `colcon --log-base /tmp/ug-plan-002-ros2-log build
  --base-paths . --packages-select universal_gnss --build-base
  /tmp/ug-plan-002-ros2-build --install-base /tmp/ug-plan-002-ros2-install
  --cmake-args -DUNIVERSAL_GNSS_BUILD_ROS2=ON --event-handlers
  console_direct+`.
- PASS: outside the sandbox, `ROS_LOG_DIR=/tmp/ug-plan-002-ros2-log/ros ctest
  --test-dir /tmp/ug-plan-002-ros2-build/universal_gnss --output-on-failure -R
  '^test_receiver_node$'`: 1/1 (25.86 s), retaining all ReceiverNode UGA009
  regressions.
- PASS: `clang-format-21` check over all touched runtime, ROS2, and transport
  C++ files; `git diff --check`.

REMAINING_DELTA: physical receiver/caster operation, physical receiver
re-enumeration/hotplug, and actual-network reconnect remain HARDWARE_PENDING
for this deterministically implemented plan.
They are the only remaining evidence; do not represent them as deterministically
proven. Docker/API/WebUI/BlueOS are explicitly out of scope and unstarted.

DO_NOT_REDO: all adopted-socket, sandbox EPERM, NtripConfig, old-sink, UGA009,
ROS2 migration, GGA fresh-observation, stop-cancellation, and credential
redaction analysis above is established until its cited runtime/Ntrip/transport
contracts change.

Exact next action: begin `UG-PLAN-005` Phase A ROS2-first production Docker
baseline when authorized. Keep `UG-PLAN-002` physical receiver/caster/hotplug
validation as separate HARDWARE_PENDING evidence.

## v0.7 reconciliation update (2026-09-04)

CURRENT_STATE: `feat/docker` is based directly on current validated `main`
`f974b565100b4f2aa3d522cd9a292f30f905e8bc`. The checked Docker evidence above
is unchanged and was not rerun. `docs/ros2_docker.md` is now the operational
quick-start/deployment/device-permissions/ROS2 guide: it states that Docker
HEALTHCHECK proves only the two launch-managed processes, while receiver
transport, fresh observations, NTRIP connection, RTCM flow/semantic health, and
RTK are independent ROS diagnostics. It also records the proven CH340/UM982
USB-loss response: re-resolve exactly one stable by-id identity, recreate the
container, and replay the volatile runtime-only profile; never infer recovery
from the same tty/major/minor or substitute another receiver.

EXTERNAL-LAN STATUS: not tested. This workspace has no accessible Docker daemon
(the normal project user receives Docker socket permission denial), no LAN
interface, and no second LAN host. Required procedure: two physical LAN hosts,
bidirectional host/container DDS data flow under the intended RMW/domain and
firewall/discovery configuration, plus a different-domain isolation check. The
same-host bridge result remains separate; host networking is not a shortcut.

ARM64 STATUS: Kilted/Lyrical BuildKit/QEMU packaging and smoke remain green;
native arm64 runtime and receiver/caster hardware remain pending. The v0.7
release checklist now records 30/65 complete after proven documentation and
fallback guidance only. Its remaining generic work is classified in `TODO.md`;
the non-ROS API/WebUI, BlueOS/Bazaar, and MowgliNext work remain out of scope.

VALIDATION: `bash -n docker/entrypoint.sh`, generator write/check, Python
compile, focused generator tests (3/3), and `git diff --check` PASS. The former
`checkpoint_audit.py --check` false-positive is resolved: it expands the compact
manifest and treats only a stable ID in a checkpoint filename as ownership, so
incidental UGA evidence references no longer create duplicate UGA-126 owners.

## Local release-readiness increment (2026-09-04)

CURRENT_STATE: deterministic Docker CI now builds Kilted/Lyrical amd64 images
from the production Dockerfile without secrets or hardware and smoke-checks
OCI revision labels, non-root runtime, installed ROS nodes, representative
operator tools, and removal of source/build/log artifacts. It neither publishes
images nor validates native arm64. `docker/compose.yaml` is a one-receiver,
stable-by-id, least-privilege example with an external read-only parameter file,
bounded Docker log rotation, and `unless-stopped` policy; it does not claim USB
grant or receiver-profile recovery. The release docs now state immutable tags,
SBOM/provenance release artifacts, rollback, device-manager, credential,
backup, and manual-support collection contracts.

ACCOUNTING: proven documentation/decision tasks advance v0.7 from 30/65 to
41/65 only; project roadmap from 56/194 to 67/194; UGA remains 33/205. Remaining
v0.7 work is intentionally unclosed where it depends on hardware, native arm64,
external LAN, runtime/API implementation, or release publication.

## Runtime/operations increment (2026-09-04)

IMPLEMENTED: deployment configuration now has a backward-compatible
`UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION` entrypoint boundary. Omission is
v1 for every existing ROS2 parameter file; a non-v1 value exits 2 before ROS
setup/launch with a credential-free stable event. The Compose template sets v1.
This is deliberately not a generic ROS parameter migration framework. The
documented `unless-stopped` policy remains Docker/Compose process recovery;
entrypoint validation and ROS launch own startup, NTRIP retains its independent
reconnect, and no second receiver/device supervisor was introduced. Entrypoint
errors now use stable `universal_gnss_entrypoint event=...` prefixes.

VALIDATION: entrypoint shell syntax and focused schema guard tests pass; Docker
CI adds the same unsupported-schema smoke test. No hardware was used. Runtime
version diagnostics, functional Docker health/no-receiver mode, full structured
node logging, and automated support export remain unclosed because they require
an explicit ROS2/public status or export contract; native arm64, LAN, and
hardware matrices are unchanged.

ACCOUNTING: v0.7 is 43/65; project roadmap 69/194; UGA remains 33/205.

## Short safe hardware reconciliation (2026-09-04)

CLASSIFICATION: wrong-receiver prevention, runtime-only UM982 reprovisioning,
live correction flow, and USB-loss recreation are ALREADY_PROVEN. Serial
renumbering requires an actual changed tty/major-minor; physical swap requires
an intentional receiver swap; persistent profile expectations require a
power-cycle/persistence matrix; UGA-126 requires explicit stale-byte exclusion;
RTK Fixed is opportunistic; native arm64 and external LAN remain separate.

PARTIAL TEST — container crash/restart: existing cached Kilted amd64 image,
credential-free example configuration in a disposable Docker volume, and only
the u-blox stable by-id device were used. The container ran non-root 1000:1000
with restart count 0. After operator-initiated `docker kill`, it was stopped
(exit 137, OOM false, restart count 0) under `unless-stopped`; it did not
restart. The container/config volume were removed. This establishes only the
manual-kill behavior, not unplanned process crash/daemon reboot recovery, so
the v0.7 crash/restart validation checkbox remains open. No receiver write,
reset, power cycle, privilege, broad `/dev` mount, or credential use occurred.

## External-LAN DDS attempt (2026-09-04)

STATUS: BLOCKED_BY_ENVIRONMENT / HARDWARE_OR_TOPOLOGY_REQUIRED / not tested. `feat/docker` is at
`366e3bc15443f9681572c50b81e5e19e19b9bf43`, clean before this attempt. The
validation workspace is itself a Docker network namespace on `eth0`
(`172.17.0.6`); it exposes no discoverable second LAN host or LAN address.
ROS 2 Kilted reports `rmw_fastrtps_cpp` with
`ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`. The normal project user cannot access
`/var/run/docker.sock`; approved read-only host-daemon inspection reports
Docker 29.7.2 on linux/amd64 and its ordinary bridge as `172.17.0.0/16`, with
ICC and IP masquerade enabled. That is a same-host topology only.

DECISION: do not rerun same-host controls or treat a host-network container as
an external LAN peer. No Docker test or production-network mode was changed,
and no release checkbox was closed.

UNBLOCK / REQUIRED BASELINE: provide two physical LAN hosts (or equivalent
independent routed LAN namespaces) plus Docker access on the container host.
Record each host OS/kernel, interface/IP/prefix, Docker network mode, RMW/DDS
configuration, explicit domain, firewall/multicast or discovery-server setup.
On Kilted amd64/default bridge first, with machine A as Dockerized Universal
GNSS and independent physical-LAN machine B as ROS2 peer: prove B -> container
and container -> B on one topic; then prove a different domain receives no
messages. Record Fast DDS interface/discovery/firewall evidence separately from
payload delivery. Only if bridge fails may a bounded alternative be tested;
capture the failure and its multicast/address-advertisement cause before any
host-network or DDS configuration recommendation. This evidence does not
generalize to native arm64, robot, MowgliNext, or BlueOS.

## Runtime identity and OCI release-quality increment (2026-09-04)

IMPLEMENTED: `ReceiverNode` now appends the additive
`universal_gnss/runtime_identity` status to its existing diagnostic array and
`~/get_snapshot` diagnostics response. It reports image-supplied version and
revision, ROS distro, and configured receiver family; it does not change a ROS
message definition, introduce a new API, expose paths/secrets, or make a health
claim. Existing discovery diagnostics remain the owner of receiver
identity/model/firmware when known. Absent environment build metadata reports
as `unknown` in that diagnostic.

IMAGE CONTRACT: the production Dockerfile now exposes OCI `title`,
`description`, `version`, `revision`, `source`, and `created`. `REVISION` and
`CREATED` have no fabricated defaults; CI supplies the checked-out commit SHA
and its `%cI` timestamp respectively. Docker CI asserts every label and exact
runtime `UNIVERSAL_GNSS_VERSION`/`UNIVERSAL_GNSS_REVISION` values, as well as
the earlier non-root/package/final-image/entrypoint contract. Backlog CI also
runs checkpoint-audit and entrypoint regression tests.

VALIDATION: PASS isolated ROS2-enabled build in
`/tmp/ug-v07-identity-build`; PASS exact
`ReceiverNodeTest.PublishesRuntimeIdentityInDiagnostics`; PASS local Kilted
Docker build and exact six-label/runtime-environment inspection using
`VERSION=v0.7.0-test`, revision `10424800cf4d95a680afd7a59581a391d39ec60a`,
and source-commit timestamp `2026-09-04T21:12:40+00:00`; disposable image tag
removed afterward. PASS entrypoint syntax, 9 focused Python tests, status
generation/check, checkpoint audit, Python compile, clang-format check, and
diff check. The broad `test_receiver_node` binary is PARTIAL only: the new test
passed, while an unrelated NTRIP socket test is blocked by this sandbox's
`Operation not permitted`; no NTRIP/network production code was changed.

ACCOUNTING: exact runtime-diagnostics release gate closed; v0.7 advances
43/65 -> 44/65, project roadmap 69/194 -> 70/194, and UGA remains 33/205.
Native arm64, external-LAN, hardware/restart matrices, support-export,
structured-log, functional-health/no-receiver, API/WebUI/BlueOS, and image
publication remain unclosed.

## Non-secret support snapshot increment (2026-09-04)

IMPLEMENTED: `scripts/collect_support_snapshot.py` creates one deterministic,
host-side JSON support artifact with no receiver access, API, upload, raw-log
copy, recursive filesystem scan, Docker environment inspection, or parameter
values. It records runtime version/revision/ROS distro/schema/platform identity;
an optional parameter-file SHA-256 and parameter-key shape; bounded direct-log
metadata; and, when available, only the six standard OCI labels. It omits
credentials, all configuration values, raw logs, Docker environment, absolute
paths, and non-whitelisted labels. Docker absence is recorded as unavailable.

VALIDATION: PASS three focused redaction/OCI-whitelist/bounded-log unit tests;
PASS manual snapshot against `docker/parameters.example.yaml` with explicit
identity values; output contained no parameter values. Backlog CI now compiles
and executes those tests. No receiver, Docker daemon, NTRIP, LAN, or destructive
hardware action was used.

ACCOUNTING: support snapshot/export gate closed; v0.7 44/65 -> 45/65; project
roadmap 70/194 -> 71/194; UGA remains 33/205. The persistent diagnostic/log/
export-directory, structured-node-log, no-receiver-health, functional
healthcheck, and Docker-DNS gates remain independently open.

## Bounded Docker lifecycle/no-device matrix (2026-09-04)

PARTIAL EVIDENCE: a disposable Kilted amd64 production image ran with a
read-only volume containing the committed parameter example and no `--device`
mapping. `receiver_node` and `ntrip_node` stayed alive; Docker process health
became `healthy`; receiver discovery logged `receiver_discovery_failed`; and
NTRIP independently logged disconnected. `docker stop` used the image SIGINT
stop signal and exited `0` (OOM false, restart count 0 under `restart: no`).
Unreadable configuration exited `1` with the stable parameter-file event;
unsupported schema exited `2` with its stable event. The test container,
volume, and image were removed.

LIMITATION: a second ROS CLI process in the constrained container could not
discover the diagnostic topic. The receiver-present/NTRIP-unavailable cell was
not run because the previously known u-blox stable by-id path is not visible in
this workspace namespace; a device-mapping attempt was safety-rejected. Do not
work around that unresolved target. Existing USB-loss and live receiver/NTRIP
evidence remains valid but does not close this exact concurrent-state matrix.

ACCOUNTING: no lifecycle/health/restart checkbox closed. The matrix is useful
for the process-vs-application-health contract only; process crash/reboot,
receiver incarnation, NTRIP-down-with-healthy-receiver, and Compose recovery
remain separate acceptance evidence.

## Deterministic image-contract CI increment (2026-09-04)

IMPLEMENTED: the existing Kilted/Lyrical amd64 Docker CI matrix now enforces
the same final-image contract on both distros: user `1000:1000`, tini
entrypoint, `SIGINT` stop signal, process-only receiver/NTRIP healthcheck,
standard OCI labels, explicit runtime version/revision environment, no
secret-like image environment names, installed receiver/NTRIP and operator
tools, resolved dynamic dependencies for both ROS node executables, no source/
build/log trees, and the two entrypoint rejection exits (missing config 1,
unsupported schema 2). It does not publish or exercise hardware/network paths.

REPRODUCIBILITY LIMIT: source revision, build args, Dockerfile, and
`.dockerignore` are project-owned deterministic inputs. The ROS distribution
base tags and apt indexes are intentionally not snapshot-pinned; their digests/
resolved packages belong in release SBOM/provenance capture. This is traceable
release identity, not a byte-for-byte rebuild claim.

VALIDATION: PASS local execution of the added inspection/runtime assertions
against a production image; the expected tools/libraries were present, no
missing `ldd` dependencies appeared, missing config exited 1, and unsupported
schema exited 2. No metrics changed because this hardens already-complete
build/runtime scope.
