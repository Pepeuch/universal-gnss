# Native ESP32 validation

`.github/workflows/esp32.yml` validates pull requests and pushes to `dev`/`main`.
It does not publish firmware, upload binaries, flash devices or change branch
protections. GitHub execution remains unqualified until an authorized push/PR.

| Variant | Profile / board | Local GNSS | RTCM output |
|---|---|---|---|
| `s3-gateway` | gateway / Waveshare S3 W5500 | compiled, explicit runtime wiring | ESP-NOW |
| `c3-relay` | relay-ntrip / SuperMini C3 + explicit W5500 wiring | excluded | ESP-NOW |
| `c3-rover-usb` | rover / SuperMini C3 | excluded | USB Serial/JTAG |
| `c3-rover-uart` | rover / SuperMini C3 | excluded | UART1 TX GPIO4, 115200, compile-only fixture |

GPIO4 and the fictitious compile peers are test inputs, not a hardware wiring or
flash recommendation. C6, other boards, repeater, LoRa and hardware tests are
outside this matrix. This is not a firmware distribution/publication matrix.

Run as the normal project user with Docker, from the repository root:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 embedded/esp32/native/ci.py --source-ref HEAD --variant s3-gateway
```

The runner resolves an immutable Git object and streams `git archive` into its
own disposable SDK container. Local precommit qualification can explicitly use
a staged tree object: the report distinguishes a tree from a commit and leaves
`source_commit` null. Unstaged files, `oldrtkesp/`, local checkpoints and `.git`
are not source inputs. The source archive stays read-only; only a newly generated
project copy receives its replaceable network manifest.

## Provisioning and isolation

The official SDK 6.1 image is digest-pinned in `migration/dependencies.json`.
`build.py --prepare-only` runs IDF reconfiguration/dependency provisioning without
building the firmware. Network roles check the lockfile's exact component set,
target, versions, component hashes and registry against that inventory.

S3 also needs OpenSSL development files for the host test suite. `Dockerfile.ci`
installs only `libssl-dev` over the official SDK, as system dependency setup. Its
stdin Docker build has an empty context, no UG source or installed UG overlay;
the project is streamed afterwards. The resulting container explicitly uses the
runner's non-root UID/GID. The installed Debian version is included in the report.
QuickJS 1.19.4 is installed into a temporary host-test directory, never firmware.

After provisioning, the runner disconnects the network and verifies that no
network attachment, host mount, device or privileged mode is present. Docker
socket and host credentials are never passed to it. Project builds/tests run
without root, with capabilities dropped and `no-new-privileges`. Host loopback
and PTYs remain available for software fixtures. Isolation is checked again at
the end. Temporary containers and image tags are cleaned on success/failure;
cleanup failures cannot turn a failed validation into success. SIGKILL cleanup
cannot be guaranteed by a process handler.

## Checks

S3 runs the native ASan/UBSan suite, browser mock and capture-tool tests before
firmware compilation. Each variant then builds and verifies the actual linked
image, roles, single UG CRC, C17/C++17 settings, web assets and existing partitions.
A second fresh build checks app, bootloader and partition-table byte identity.
The ephemeral compiler cache belongs to that container; no persistent CI cache
or preinstalled UG library is reused.

The matrix does not fail fast. Its `always()` gate accepts only `success` for the
entire matrix: failure, cancellation, skip and missing result remain non-green.
The existing ROS2/distribution gates and Release workflows are unchanged. Merely
adding this gate does not make it required by GitHub branch protections.

Local qualification: 37 native CTests, one browser test, eight capture tests,
211 generator/CI tests and eight native build-policy tests pass. Four actual
variant runs passed their build/verification/reconstruction checks; earlier C3
results were retained where subsequent changes affected only S3 host setup.
SDK dependencies were fetched from their public sources. Current app sizes:
S3 1440512, relay 1294432, USB rover 682816, UART rover 694288 bytes.

Reproducibility is scoped to the declared SDK and normalized CI source/build
paths. Workspace-independent diagnostic/debug strings are not qualified. APT
host dependencies and pip transitives are not fully frozen; public package/image
availability is not guaranteed by this configuration. No hardware execution,
RTK/RF/USB delivery, OTA, production image or Build & Flash portal is qualified.
