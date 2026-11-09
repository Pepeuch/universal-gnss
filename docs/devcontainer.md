# ROS 2 Devcontainer

This repository ships a single ROS 2 devcontainer definition for local
development and validation.

The container is:

- configured for Lyrical by default
- parameterized by `ROS_DISTRO`
- usable with the existing Kilted and Lyrical package validation flows
- suitable for both the normal CMake/CTest workflow and the ROS 2 `colcon`
  workflow
- provisioned with the repository-authoritative `clang-format-21`

## C/C++ formatting

The repository-root [`.clang-format`](../.clang-format) is the authoritative style.
Use only `clang-format-21` for authoritative formatting or validation; an unversioned
`clang-format` executable is not an accepted substitute. The devcontainer installs the
required binary from the LLVM 21 repository.

For explicitly touched C/C++ files:

```bash
bash scripts/clang_format_21.sh --apply path/to/file.cpp path/to/file.hpp
bash scripts/clang_format_21.sh --check path/to/file.cpp path/to/file.hpp
```

`MowgliNext` is no longer required for day-to-day ROS 2 builds of
`universal_gnss_ros2`.

## Files

- [`.devcontainer/devcontainer.json`](../.devcontainer/devcontainer.json)
- [`.devcontainer/Dockerfile`](../.devcontainer/Dockerfile)

## ROS distribution selection

The Dockerfile stays generic on purpose:

```dockerfile
ARG ROS_DISTRO=lyrical
FROM ros:${ROS_DISTRO}-ros-base
```

Current defaults:

- `lyrical` is selected in `devcontainer.json` and is the Dockerfile default
- Kilted and Lyrical are covered by the ROS 2 CI matrix

Changing `ROS_DISTRO` selects the corresponding base image. Provisioning still
requires that image and the declared dependencies to be available; existing
package validation does not qualify every newly provisioned environment.

## VS Code / Dev Containers usage

Open the repository in VS Code and choose:

1. `Dev Containers: Reopen in Container`
2. Build with the default `lyrical` arg from `devcontainer.json`

To select Kilted instead, change the current argument:

```json
"args": {
  "ROS_DISTRO": "lyrical"
}
```

to:

```json
"args": {
  "ROS_DISTRO": "kilted"
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

- Both Kilted and Lyrical have existing ROS2 package build/test evidence.
- The configured development environment is Lyrical. Image provisioning,
  package validation and physical-device qualification remain distinct.

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
ln -s /workspaces/universal-gnss/universal_gnss_msgs /tmp/universal_gnss_ros2_ws/src/universal_gnss_msgs
ln -s /workspaces/universal-gnss/adapters/ros2/gnss_ros2 /tmp/universal_gnss_ros2_ws/src/gnss_ros2
cd /tmp/universal_gnss_ros2_ws
colcon build --packages-select universal_gnss_msgs universal_gnss_ros2
source install/setup.bash
colcon test --packages-select universal_gnss_msgs universal_gnss_ros2 --return-code-on-test-failure
colcon test-result --verbose
```

This workflow is now more robust because `adapters/ros2/gnss_ros2/CMakeLists.txt` resolves
the common library source paths from the real repository location instead of assuming
the `colcon` workspace contains every low-level package as a peer checkout.

ROS 2 CTest tests prepend the current build directories for adapters and the
low-level driver, protocols, transport, NTRIP, and tools libraries to
`LD_LIBRARY_PATH`. This also covers indirect dependencies, since
`LD_LIBRARY_PATH` takes precedence over ELF RUNPATH and could otherwise select
a sourced older installation. On Linux shared-library builds, `test_build_library_provenance`
checks the loader's resolved paths for the complete low-level stack.

## Serial hardware access

The checked-in configuration currently uses `--privileged` and binds the host's
entire `/dev` tree at `/dev`. It also mounts `/var/run/docker.sock`. These are
actual defaults, not options left disabled. Processes run as `ubuntu`, but that
user identity does not turn the container into a restricted device sandbox.

If an operator chooses a narrower device configuration, the examples below
are alternatives that require replacing the existing broad `runArgs`, rather
than merely adding a device flag. This guide update changes no configuration,
permissions or mounts. Device visibility alone does not qualify receiver access,
hotplug/reconnect behavior or safe hardware operation.

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

## Privilege and mount implications

The current `runArgs` are:

```json
"runArgs": [
  "--init",
  "--privileged",
  "-v",
  "/dev:/dev"
]
```

Privileged mode broadens access to host facilities, and the `/dev` bind exposes
host device nodes. The Docker socket mount permits interaction with the host
Docker daemon when its permissions allow it. Use this development environment
only with that access understood; it is distinct from the non-root production
image's explicitly selected device grants. No hardware test runs automatically.

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

- Kilted and Lyrical: existing software package validation and CI coverage.
- Lyrical: current devcontainer selection; availability of a newly provisioned
  image and device access remain environment-dependent.
- The post-refactor global CI qualification campaign remains separate and is
  not declared complete by this documentary consolidation.

See [ros2.md](ros2.md)
for the current ROS 2 node surfaces and launch examples.
