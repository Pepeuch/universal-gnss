# Agent checkpoint

Repository: `/workspaces/universal-gnss`
Branch: `main`
HEAD: `84d4b34b760d150b88f5e41ce3e831a30c4f47bf`

## Objective

Preserve the reusable planning boundary discovered by the BlueOS compatibility
study without making BlueOS a second GNSS implementation.

## Authoritative planning state

`TODO.md` section **Native runtime, API, web, and deployment planning** is the
source of truth for `UG-PLAN-001` through `UG-PLAN-006`. `ROADMAP.md` carries
the release/dependency view. This checkpoint is only a resumption aid.

## Established evidence

- CURRENT: `blueos/README.md` defines the adapter boundary and its physical
  device-grant/hotplug risk; reference it rather than copying its design.
- IMPLEMENTED/PARTIAL: `84d4b34` adds generic, non-ROS/non-BlueOS supervisor
  Phase 1 with one explicit serial device, session/runner lifecycle, bounded
  reconnect, incarnation clearing, snapshots, CLI, and fake-transport tests.
- COMPLETED RESEARCH/PARTIAL: BlueOS compatibility/packaging research, Bazaar
  metadata template, and minimal permission template exist. No Docker image,
  API, GUI, NTRIP supervisor orchestration, or BlueOS runtime is implemented.

## Dependency decisions

1. Finish current Universal GNSS/MowgliNext downstream validation.
2. Complete supervisor Phase 1 physical/configuration delta (`UG-PLAN-001`).
3. Compose supervisor NTRIP/RTCM (`UG-PLAN-002`).
4. Build generic API then GUI (`UG-PLAN-003`, `UG-PLAN-004`).
5. Package standalone Docker (`UG-PLAN-005`).
6. Reuse these layers for BlueOS and perform physical device-grant validation
   before publication (`UG-PLAN-006`).

## Hardware boundary

BlueOS USB hotplug/re-enumeration and Docker device grants are
HARDWARE_REQUIRED. A reopened tty is not proof that the container has a valid
grant for a newly enumerated physical receiver.

## Do not redo

- Do not add ROS2/BlueOS dependencies or duplicate GNSS/parser/runtime/NTRIP
  semantics in the supervisor, API, GUI, Docker, or BlueOS layers.
- Do not create a Dockerfile until the supervisor is production-capable.
- Do not claim API, GUI, Docker, or BlueOS support is implemented.

## Exact next step

Resume the first authorized generic runtime/deployment item from `TODO.md`;
do not start BlueOS implementation ahead of its generic dependencies.
