# ROS 2 Devcontainer

This repository ships a single ROS 2 devcontainer definition for local
development and validation.

The container is:

- Kilted-first today
- parameterized by `ROS_DISTRO`
- prepared for future `lyrical` validation without duplicating Dockerfiles
- suitable for both the normal CMake/CTest workflow and the ROS 2 `colcon`
  workflow

`MowgliNext` is no longer required for day-to-day ROS 2 builds of
`universal_gnss_ros2`.

## Files

- [`.devcontainer/devcontainer.json`](/home/pepeuch/Documents/vscode/tondeuse/universal-gnss/.devcontainer/devcontainer.json)
- [`.devcontainer/Dockerfile`](/home/pepeuch/Documents/vscode/tondeuse/universal-gnss/.devcontainer/Dockerfile)

## ROS distribution selection

The Dockerfile stays generic on purpose:

```dockerfile
ARG ROS_DISTRO=kilted
FROM ros:${ROS_DISTRO}-ros-base
```

Current defaults:

- `kilted` is the default in `devcontainer.json`
- `lyrical` is prepared as the next target when `ros:lyrical-ros-base` is
  available in your environment

The same devcontainer structure should work for both distros by changing only
the build arg.

## VS Code / Dev Containers usage

Open the repository in VS Code and choose:

1. `Dev Containers: Reopen in Container`
2. Build with the default `kilted` arg from `devcontainer.json`

If you want to switch to a future distro such as `lyrical`, change:

```json
"args": {
  "ROS_DISTRO": "kilted"
}
```

to:

```json
"args": {
  "ROS_DISTRO": "lyrical"
}
```

and rebuild the container.

## Direct Docker build examples

Kilted:

```bash
docker build \
  --build-arg ROS_DISTRO=kilted \
  -f .devcontainer/Dockerfile \
  -t universal-gnss:ros2-kilted \
  .
```

Lyrical:

```bash
docker build \
  --build-arg ROS_DISTRO=lyrical \
  -f .devcontainer/Dockerfile \
  -t universal-gnss:ros2-lyrical \
  .
```

Notes:

- `kilted` is the currently validated target for `universal_gnss_ros2`
- `lyrical` is build-ready in the container definition but should be treated as
  a future validation target until that image is actually available and tested

## Normal CMake / CTest validation

From the repository root inside the container:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## ROS 2 colcon validation

The minimal `colcon` workflow stays:

```bash
mkdir -p /tmp/universal_gnss_ros2_ws/src
ln -s /workspaces/universal-gnss/gnss_ros2 /tmp/universal_gnss_ros2_ws/src/gnss_ros2
ln -s /workspaces/universal-gnss/gnss_core /tmp/universal_gnss_ros2_ws/src/gnss_core
cd /tmp/universal_gnss_ros2_ws
colcon build --packages-select universal_gnss_ros2
colcon test --packages-select universal_gnss_ros2
```

This workflow is now more robust because `gnss_ros2/CMakeLists.txt` resolves
its sibling package paths from the real repository location instead of assuming
the `colcon` workspace contains every low-level package as a peer checkout.

## Serial hardware access

The default devcontainer does not claim host serial devices automatically.
That keeps startup predictable on machines without GNSS hardware.

For hardware tests, add one or more `runArgs` entries in
`.devcontainer/devcontainer.json`.

u-blox / F9P on `/dev/ttyACM0`:

```json
"runArgs": [
  "--init",
  "--device=/dev/ttyACM0"
]
```

USB serial adapters on `/dev/ttyUSB0`:

```json
"runArgs": [
  "--init",
  "--device=/dev/ttyUSB0"
]
```

If you need both:

```json
"runArgs": [
  "--init",
  "--device=/dev/ttyACM0",
  "--device=/dev/ttyUSB0"
]
```

Note:

- on the host used for the current ZED-F9P smoke test, `/dev/ttyACM0` was
  `root:dialout` with mode `0660`
- the final smoke-test host user also had `dialout` membership, which allowed
  host-side serial tools to run directly
- if you are not running the container as `root`, you may still need host
  `dialout` membership in addition to `--device=/dev/ttyACM0`

## Optional privileged mode

For broader manual hardware access during bringup, you may choose one of these
options:

Use Docker privileged mode:

```json
"runArgs": [
  "--init",
  "--privileged"
]
```

Or bind the full `/dev` tree:

```json
"mounts": [
  "source=/dev,target=/dev,type=bind"
]
```

These options are intentionally not enabled by default.

## F9P smoke-test example

With a ZED-F9P exposed as `/dev/ttyACM0` inside the container:

```bash
ros2 launch universal_gnss_ros2 receiver_serial.launch.py \
  serial_device:=/dev/ttyACM0 \
  serial_baud:=115200 \
  receiver_family:=ublox
```

The devcontainer does not run hardware tests automatically.

## Current validation posture

- Kilted: validated target for the ROS 2 package flow
- Lyrical: prepared in the Dockerfile and docs, pending real-image availability
  and explicit validation

See [docs/ros2.md](/home/pepeuch/Documents/vscode/tondeuse/universal-gnss/docs/ros2.md)
for the current ROS 2 node surfaces and launch examples.
