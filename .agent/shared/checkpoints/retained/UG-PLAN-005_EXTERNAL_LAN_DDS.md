# UG-PLAN-005 external-LAN DDS validation

Status: RETAINED — Phase D accepted on the recorded bridge/unicast contract

## Contract

Validate Kilted ROS 2 DDS between the current-UG robot container and an
independent physical arm64 LAN peer, using Docker's default bridge first. Prove
external-host -> container and container -> external-host payload delivery on
the same explicit `ROS_DOMAIN_ID`, then prove a different domain receives no
payload. Domain IDs
are not a security boundary. Do not use host networking as a substitute for the
default-bridge result.

## Established boundary

Same-host host/container, container/container, and domain isolation are already
proven and must not be rerun unless a control is genuinely required. The current
workspace is one Docker namespace (`eth0`, `172.17.0.6`), has no discoverable
second LAN peer, and the ordinary Docker bridge is `172.17.0.0/16`. Kilted uses
`rmw_fastrtps_cpp` with `ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`.

## Execution procedure

Provide Docker access on the container host and a second physical/routed LAN
peer. Record host OS/kernel, interface/IP/prefix, Docker network mode,
RMW/DDS configuration, explicit domain, and inspectable firewall/multicast or
discovery-server configuration. On default bridge, record discovery and payload
flow independently for both directions. Then repeat only the payload check on a
different domain and require no delivery. If discovery or traffic fails, retain
the exact failure and determine multicast/discovery/address-advertisement cause
before testing one minimal alternative. Native arm64, robot, MowgliNext, and
BlueOS remain out of this matrix.

## Evidence and invalidation

The host-daemon inspection was Docker 29.7.2 linux/amd64; its bridge had ICC
and IP masquerade enabled. It cannot establish external-LAN behavior. Re-run
this matrix if the target host/network, Docker mode, RMW configuration, DDS
discovery policy, or firewall policy changes.

## Phase A physical-peer evidence — 2026-09-05

- Robot `Peuchmower2`: native aarch64 Debian 13, Docker 29.8.0 arm64,
  `wlan0=192.168.10.35/24`.
- Second physical RPi `raspberrypi`: native aarch64 Debian 13, Docker 29.8.0
  arm64, `eth0=192.168.10.225/24`.
- Both use default route `192.168.10.1`, are independently SSH-reachable, and
  permit user-level Docker inspection. Their Docker default bridges are
  `172.17.0.0/16`; each also has an existing Compose bridge at `172.18.0.0/16`.
- Neither host has `ufw`, `nft`, or `iptables` CLI tooling installed. This is
  limited visibility, not proof that no kernel or Docker-managed rules exist.
- Existing Mowgli containers on both hosts use Kilted/Cyclone DDS, host
  networking, domain 0, and localhost-only discovery. They are stale baseline
  evidence and must not substitute for the current-UG/Fast DDS/default-bridge
  matrix.
- The former missing-peer/Docker-access unblock condition is satisfied. No DDS
  discovery or payload direction has been tested, so status is ACTIVE/PENDING
  with no acceptance credit.

## Phase D preflight — 2026-09-05 18:35 UTC

- Committed execution baseline is
  `1a9c00dcba03fdbd12e924b4b28207edc594a06e` on `feat/docker`; the local
  worktree was clean before this checkpoint update.
- Robot `Peuchmower2` remains native aarch64 Debian 13 with kernel
  `6.18.39+rpt-rpi-2712`, Docker client/server 29.8.0 arm64, and
  `wlan0=192.168.10.35/24`. The second RPi remains native aarch64 Debian 13
  with kernel `6.18.39+rpt-rpi-v8`, Docker client/server 29.8.0 arm64, and
  `eth0=192.168.10.225/24`.
- Both physical endpoints still route the LAN through `192.168.10.1`. Their
  separate Docker default bridges each use `172.17.0.0/16`, gateway
  `172.17.0.1`, ICC and IP masquerade enabled, IPv6 disabled, and MTU 1500.
  Both `docker0` links were down before a default-bridge test container existed.
- Neither host has a native `ros2` executable. The exact current-UG image is
  available on both with image ID
  `sha256:1b01bb887132dccb425107c55f5d1b5c1cf840312c10cd0dc98d59bc0dd8184b`,
  `linux/arm64`, uid/gid 1000, tini entrypoint, and source revision
  `90325b2a05501341b9e6f88460d8c4b194c7bf25`.
- Existing Mowgli GPS/ROS2 containers remain running, privileged, host
  networked, and configured for Kilted/Cyclone DDS, domain 0, and
  localhost-only discovery. They are baseline-only and will not participate.
  `ufw`, `nft`, and `iptables` CLIs remain absent on both hosts; that is limited
  firewall visibility, not proof of no rules.

Exact next action: create one temporary current-UG test container on each
physical host using Docker's default bridge, no device/mount/privilege, and an
explicit common domain with Fast DDS and subnet discovery. Record the actual
container environment before testing discovery and payload independently.

## Default-bridge endpoint baseline — 2026-09-05

- Robot container `ug-plan-005-dds-robot`, ID
  `a8adccb5ccd4d7d19e779e31dcf495ef5d10d48937e824b186cc57a91c945c4a`,
  and second-RPi container `ug-plan-005-dds-peer`, ID
  `9658cc11fb21670bce7414c5b929564a4cb21b7ee4393779bcb0033f7f826867`,
  run the same inspected current-UG image.
- Both are uid/gid 1000, non-privileged, restart `no`, on Docker network mode
  `bridge`, with no device, bind, mount, or added capability. Existing Mowgli
  containers remain running and uninvolved.
- Both actual test processes have ROS 2 Kilted, `rmw_fastrtps_cpp` package
  9.3.4, `ROS_DOMAIN_ID=117`, `ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`,
  `ROS_LOCALHOST_ONLY=0`, and the ROS CLI daemon disabled. No Fast DDS profile
  file or discovery server is configured.
- Each independent host assigned its test container the overlapping private
  address `172.17.0.2/16` with gateway `172.17.0.1`. This is a directly
  observed address-advertisement/routing risk to classify only if the payload
  matrix fails; it is not itself a DDS failure result.

Exact next action: on domain 117, run a bounded publisher on the second-RPi
endpoint and independently capture robot-side topic discovery and nonce payload;
then reverse publisher/subscriber roles with a distinct topic and nonce.

## Default bridge, domain 117 — second RPi to robot

- The second-RPi publisher ran for its 12-second bound and emitted 15 observed
  instances of nonce `UGP5_DDS_B2R_20260905_1837` on
  `/ug_plan_005_phase_d_peer_to_robot`; timeout exit 124 is the intentional
  publisher bound, not a crash.
- Discovery evidence on the robot was independently negative: `ros2 topic
  info -v` returned `Unknown topic` and exit 1 while the remote publisher was
  active.
- Payload evidence was independently negative: the robot subscriber produced
  no payload and reached its 18-second timeout, exit 124.
- Classification for this direction on unmodified default bridges is FAIL at
  discovery; payload cannot flow without discovery. No firewall, route,
  service, or Docker network setting was changed.

Exact next action: reverse roles on a distinct topic/nonce under the same
domain and unchanged default-bridge configuration, capturing discovery and
payload separately.

## Default bridge, domain 117 — robot to second RPi

- The robot publisher ran for its 14-second bound and emitted at least 15
  observed instances of nonce `UGP5_DDS_R2B_20260905_1840` on
  `/ug_plan_005_phase_d_robot_to_peer`; exit 124 is the intentional bound.
- Second-RPi discovery saw only its local subscriber: topic inspection returned
  publisher count 0 and subscription count 1 while the robot publisher was
  active. It therefore did not discover the remote publisher.
- The second-RPi subscriber produced no payload and expired with exit 124.
  This direction also FAILS at discovery on the unmodified default bridges.

## Default-bridge failure classification

- Both test containers joined Fast DDS's `239.255.0.1` discovery group on
  their local `docker0`; passive host membership inspection showed no such
  membership on robot `wlan0` or second-RPi `eth0`. The discovery multicast is
  therefore confined to the separate default bridges in this observed setup.
- Both separate bridges also assigned `172.17.0.2/16`, so even a learned
  private locator would be ambiguous/unroutable across the physical hosts.
- No packet-capture utility or host firewall CLI is installed. This limits
  deeper ruleset/packet visibility but does not weaken the direct negative DDS
  discovery observations.
- The unmodified default-bridge matrix FAILS in both directions before payload.
  Different-domain non-delivery alone cannot establish isolation against a
  same-domain path that never worked.

One minimal alternative is justified and is the only alternative authorized:
keep Docker bridge mode, select fixed RTPS participant ports, publish only
those UDP ports, and use temporary read-only Fast DDS profiles with explicit
physical-host external locators and initial unicast peers. This targets both
observed causes without host networking or persistent firewall/network change.

Exact next action: recreate only the two validation containers with that
bounded unicast/NAT profile, validate profile loading, then repeat both
same-domain directions and a different-domain negative control. If it fails,
record PARTIAL/FAIL and clean up without another alternative.

## Minimal unicast/NAT alternative — endpoint contract and first direction

- The two validation containers were recreated, still on Docker `bridge`, with
  no devices or privilege. Each exposes only UDP 36662 (fixed discovery) and
  36663 (fixed user data) and mounts its host-specific Fast DDS profile
  read-only from temporary external storage. Profile parsing passed on both.
- The profile preserves domain 117 and Fast DDS but disables builtin multicast,
  points `initialPeersList` at the opposite physical host, and advertises the
  local physical host as the external discovery/data locator. Direct container
  TCP reachability to the opposite physical host was independently successful;
  both UDP ports were unused before Docker published them.
- An initial ROS CLI probe still failed because each CLI invocation left a
  `ros2-daemon` participant and allocated participant ports 36664/36665 beyond
  the deliberately fixed pair. Those daemons were confined to the validation
  containers and were stopped; `docker top` then showed only tini and the idle
  validation process. No Mowgli process was touched.
- With one explicit `rclpy` participant per endpoint, second RPi -> robot PASS:
  the second RPi published 60 instances of nonce
  `UGP5_FINAL_B2R_20260905_1904`; the robot independently reported one remote
  publisher discovered, matched the expected payload once, and exited 0.
  Discovery and payload are therefore both directly positive in this direction.

Exact next action: under the unchanged domain-117 unicast/NAT profile, run the
same mono-participant harness robot -> second RPi with a distinct topic/nonce.

## Minimal unicast/NAT alternative — reverse direction

- Robot -> second RPi PASS on the unchanged profile: the robot published 60
  instances of nonce `UGP5_FINAL_R2B_20260905_1908`; the second RPi
  independently reported one remote publisher discovered, matched the expected
  payload once, and exited 0.
- Same-domain discovery and payload are now directly positive in both physical
  LAN directions. This result requires the recorded explicit external-unicast
  profile and two UDP port publications; it does not convert the failed
  unmodified multicast/default-bridge result into a pass.

Exact next action: retain the proven network/profile path, run a domain-117
publisher against a domain-118 subscriber, and require both zero remote
publisher discovery and zero matching payload for the entire bounded window.

## Different-domain negative control

- On the same proven unicast/NAT path and fixed topic, the robot domain-117
  publisher emitted 48 instances of nonce
  `UGP5_DOMAIN117_ONLY_20260905_1912` while the second-RPi subscriber ran on
  domain 118 for its full 25-second window.
- The subscriber directly reported zero remote publisher discovery, zero
  payload match, zero received samples, and its expected negative-result exit
  1. Different-domain non-delivery is therefore directly observed against a
  same-path/same-profile baseline that had already passed in both directions.
- `ROS_DOMAIN_ID` is an observed DDS domain filter only and is not treated as a
  security or access-control boundary.

Phase D acceptance is satisfied with the explicit bridge/unicast/NAT contract:
both physical-LAN directions have independent positive discovery and payload
evidence, and the different-domain control has independent negative discovery
and payload evidence. The unmodified multicast default-bridge attempt remains
a recorded failure and must not be rewritten as a pass.

Exact next action: stop and remove only the two validation containers, unlink
their two operator-owned temporary profiles after verifying ownership, confirm
the UDP ports are released and all pre-existing Mowgli services remain running,
then promote this checkpoint to RETAINED. Stop before Phase E.

## Cleanup and closure

- Both validation containers stopped through the image SIGINT path, were not
  OOM-killed, had restart count 0, and exited 130 because their intentional
  foreground command was `sleep infinity`. Both containers were then removed.
- The two ownership-checked temporary Fast DDS profiles were unlinked and UDP
  36662/36663 were confirmed released on both hosts.
- All six pre-existing robot Mowgli services and all five pre-existing
  second-RPi Mowgli services remained running. No GNSS device, receiver state,
  robot control, firewall, route, or persistent network setting changed.
- Phase D is COMPLETE. No v0.7 or project-roadmap checkbox changes because this
  external-LAN matrix is explicitly outside the fixed 65-task release list.

Invalidation: repeat this matrix if either physical host/interface, Docker
network mode, published RTPS ports, Fast DDS profile/RMW version, discovery
policy, or LAN/firewall policy changes. Do not generalize the positive result
to unmodified multicast discovery; that path directly failed here.
