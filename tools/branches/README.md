# Local source distribution generator

Lot 2 implements local, offline generation for **ROS2 Kilted and Lyrical**.
It preserves the current source layout and all copied file contents/modes.
It creates no Git branch, performs no network operation and never pushes.
The [repository policy](../../TODO_REPO_REFACTOR.md) remains
`dev → main → generated distributions`: development snapshots can be exercised
locally; future publication must use an approved exact commit from `main`.
A successful local generation is not a publication or a hardware qualification.

## Distribution CI and publication preparation

Lot 3 adds a read-only [distribution validation workflow](../../.github/workflows/distributions.yml)
and [operating/design guide](CI.md): generator tests, all option combinations,
reproducibility, mandatory isolated Kilted/Lyrical builds and an aggregate gate.
The publication candidate report remains locked against writes. Official
main-only publication, App authorization, protections and atomic replacement
require a later activation PR; none is enabled here.

## Generate and verify

Requirements: Python 3.10 or newer, Git and Bash. No Python package installation
is needed. `manifest.yaml` uses the JSON subset of YAML 1.2, intentionally parsed
with Python's standard library; YAML-specific tags, aliases and comments are not
accepted. Duplicate keys and unknown schema fields are rejected.

From any working directory:

```bash
bash tools/branches/make_backend.sh --source-ref HEAD --distribution ros2-kilted
bash tools/branches/make_backend.sh --source-ref HEAD --distribution ros2-lyrical --output /tmp/ug-lyrical
bash tools/branches/verify_backend.sh --source-ref HEAD --distribution ros2-lyrical --output /tmp/ug-lyrical
```

`--repo` selects the source Git worktree; the default is this repository.
`--source-ref` is mandatory for generation and resolves to a full immutable
commit SHA. Only committed blobs at that SHA are read, including when the
working tree is dirty. Untracked/modified worktree files are neither copied nor
changed. Classification covers every file in the selected commit, not dirty or
untracked files. No fetch is performed and the ref must already exist locally. Git network
protocols and partial-clone lazy fetching are disabled; missing objects fail.
Git replacement refs are ignored so a SHA cannot silently denote another tree.

Without `--output`, generation prints a newly allocated directory outside the
source tree. With it, the destination must not exist, even if empty or previously
generated. No output inside the source worktree is allowed, including through
symlinks or a `--repo` subdirectory. Source symlinks/submodules are not supported;
selected symlinks and all submodules fail explicitly. Outputs accept only regular
files/directories; additional files, empty directories and symlinks fail verification.
Use a new destination for each generation; nothing is silently overwritten.

Options can be repeated:

```bash
bash tools/branches/make_backend.sh --source-ref HEAD --distribution ros2-lyrical --with mavros --with docker --output /tmp/ug-lyrical-options
bash tools/branches/verify_backend.sh --source-ref HEAD --distribution ros2-lyrical --with mavros --with docker --output /tmp/ug-lyrical-options
```

`--with mavros` includes the optional source plugin and its own external
requirements, including **MAVROS 2.16.0 exactly**. `--with docker` includes the
unchanged Dockerfile, Docker runtime/configuration inputs and support collector.
Without that option only `docker/healthcheck.py` is included, because an existing
ROS2 test imports it. Docker image construction still has its normal external
image/system-package requirements; generation itself is offline.

`--exclude component` is an assertion, not a filter: excluding any necessary
transitive component fails. Unsupported targets such as ESP32, Humble, standalone
Linux and BlueOS are not accepted as distributions.

Verification uses this tool's trusted manifest by default, or a caller-supplied
`--manifest`. It does not trust the output's embedded manifest or editable
inventory. It recomputes the complete expected output from Git, then compares
all bytes and executable modes. Pin `--source-ref`, `--distribution` and
`--with` options to the reviewed values for an acceptance check. If omitted for
verification only, source/profile/options are read from the metadata for
convenience; this alone does not establish that their identities were approved.
To verify an older/custom output, supply the corresponding trusted manifest and
generator implementation explicitly.

## Components and audited dependencies

The graph separates source components from external dependencies. ROS2 source
reconstruction in [gnss_ros2/CMakeLists.txt](../../gnss_ros2/CMakeLists.txt) is the
primary pilot build contract.

| Component | Direct internal dependencies | External requirements / reason |
| --- | --- | --- |
| `legal` | — | LGPL license text retained |
| `core` | — | CMake ≥3.22, C++17; header-only common model at `core/gnss_core/` |
| `protocols` | `core` | NMEA, UBX, Unicore and RTCM library at `protocols/gnss_protocols/` |
| `transport` | — | OpenSSL on the Linux pilot platform |
| `driver` | `protocols`, `transport` | Sessions/configuration/discovery library at `core/gnss_driver/` |
| `ntrip` | `protocols`, `transport` | NTRIP/GGA library at `core/gnss_ntrip/`; TLS tests use transport helpers |
| `fixtures` | — | Existing synthetic/sanitized `testdata/` corpus |
| `tools` | `driver`, `ntrip`, `protocols`, `fixtures` | Library linked by replay and associated CLI/tests |
| `messages` | — | ament, ROSIDL generator/runtime, builtin/diagnostic interfaces, pytest |
| `healthcheck` | — | `rclpy`, `std_srvs`; runtime probe and its existing test |
| `ros2` | `legal`, `core`, `protocols`, `transport`, `driver`, `ntrip`, `tools`, `messages`, `fixtures`, `healthcheck` | rclcpp, standard ROS messages/services, launch, ament/gtest/pytest and Python3 |
| `mavros` (optional) | `core`, `ros2` | MAVROS 2.16.0, libmavconn/MAVLink, Eigen, GeographicLib, pluginlib, ROS dependencies listed in manifest |
| `docker` (optional) | `ros2`, `healthcheck` | BuildKit, ROS base image, colcon, CycloneDDS, tini, OpenSSL and runtime packages |

`gnss_runtime` is not a dependency of the ROS2 package. The full repository's
root CMake entry point includes it, so that entry point is deliberately excluded
rather than rewritten or copied with an unrelated supervisor. Build the pilot
using package-based colcon commands, as in the existing Kilted/Lyrical workflows.
All package-local CMakeLists, package.xml files, include namespaces, ROSIDL
interfaces, aliases, launch/config files, installed names and exports remain
byte-identical to the selected source commit. No package version is rewritten.

## Manifest schema and verification boundary

- `schema_version`, `ug_version`: generator schema and recorded existing UG
  functional identity; package versions are separately preserved from XML.
- `components`: identity, source `paths`, direct internal `requires`, external
  requirements, allowed `platforms`, `frameworks`, `hardware_profiles` and
  `distributions`.
- `distributions`: platform/framework/hardware profile, ROS distro, root
  components, opt-in component mapping and forbidden path prefixes.
- `exclude`: explicit paths/prefixes with reasons. The most specific matching
  rule owns each committed file; ties, unknown files and nonexistent component
  paths fail. A new file inside a declared component prefix is intentionally
  covered; a new top-level component needs an explicit classification.
- `references`: audited cross-component file/directory edges with reasons,
  including computed Python paths and test registration outside a component.

The entire dependency graph is checked for unknown dependencies and cycles,
including optional components. Closure checks platform, framework, hardware
profile and distribution compatibility. Missing source paths, excluded required
components and forbidden selected files fail before writing output.

The verifier also checks supported literal CMake source/root paths, project
includes, internal ROS package dependencies, ROSIDL-generated header inputs,
external `find_package`/package.xml declarations and Docker COPY sources.
It is not a general CMake/Python interpreter: computed cross-component paths
need audited `references` entries and isolated build evidence. Future source
changes introducing such paths require review of the manifest, not an automatic
fallback to copying the full repository. External dependencies are declarations,
not vendored code or proof that a host has them installed.

Every copied file is checked against the trusted source commit. This detects
unauthorized changes in headers, interfaces, launch/configuration, CMake/package
exports and implementation files, even if an output inventory was forged.
It does not decide whether a change already present in the chosen source commit
was approved; that remains a source review/publication responsibility.

The generated tree includes four deterministic control files:

- `.ug-distribution.json`: source commit, functional/package versions, profile,
  selected components/options, external dependencies, generator version/code hash,
  manifest/inventory hashes and explicit local-only status.
- `.ug-inventory.json`: sorted copied-source inventory with owner, SHA256, Git mode
  and public-contract classification. Generated control files are checked separately
  by recomputing their bytes, avoiding a self-referential inventory hash.
- `.ug-manifest.yaml`: exact input manifest bytes.
- `README.distribution.md`: applicable package build/test commands.

No timestamps, local absolute paths or destination identities enter these files.
Identical source commit, manifest, generator implementation and options produce
identical file bytes and modes, independent of option ordering/destination.

## Tests and isolated builds

```bash
# CI policy tests added in lot 3 require PyYAML; generator runtime stays stdlib-only.
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/branches/tests -v
bash tools/branches/test_isolated.sh kilted /tmp/ug-kilted <cached-kilted-build-image>
bash tools/branches/test_isolated.sh lyrical /tmp/ug-lyrical <cached-lyrical-build-image>
```

Use a pre-provisioned local build image containing the appropriate ROS distro,
compiler, CMake, colcon and ament test packages. Optional MAVROS needs its declared
external installation as well. The runner streams only the generated source via
tar/stdin into a fresh container; it mounts no host source, device, credentials or
Docker socket. It disables networking and pulls, masks `/workspaces`, runs as the
normal user's UID/GID and starts with an empty environment before sourcing the
ROS base. An optional `/opt/mavros/local_setup.bash` is used only for MAVROS input.
Build/install/log artifacts stay inside the disposable container. It checks
interface-only packaging, generated ROSIDL visibility, node executable discovery,
colcon build and tests. This is local amd64 software evidence, not ARM, hardware,
registry or distribution publication evidence.

Future ESP-IDF hardware profiles can use the component constraint fields, but
need their own audited platform/framework/distribution validation before being
accepted by the generator. There is no fictitious ESP32 distribution here.
Before any donor migration, audit licenses, notices, dependencies and semantic
compatibility. This lot imports no external code and implements neither BASE /
RELAY / ROVER nor ESP-NOW LR; Linux, BlueOS and Humble plans remain unchanged.

## Lot 2 validation record

Validation source: `5a6f285f9864d924fdfc032e814fdbd1d7bf5eb1` (merged PR 12).
Both default pilots contain 308 copied files plus four generated control files;
MAVROS adds ten copied files. No `gnss_runtime`, agent state, CI, historical
campaign data or unqualified backend source is present. Docker packaging and
MAVROS inclusion/exclusion and option-order independence are covered by generator
integration tests for both pilots.

| Validation | Result |
| --- | --- |
| Generator tests (synthetic Git fixtures, CLI and real manifest) | 44 tests PASS |
| Existing Python tooling regression suite | 55 tests PASS |
| Kilted isolated Release, messages + ROS2 | Build PASS; 165 tests, 0 errors/failures/skips |
| Lyrical isolated Release, messages + ROS2 | Build PASS; 166 tests, 0 errors/failures/skips |
| Lyrical isolated Release with MAVROS 2.16.0 | Three packages built; 168 tests, 0 errors/failures/skips |

These are native amd64 containers with networking disabled. Cached build image
identities were Kilted
`sha256:da9eb8239f0bca0f45644cd6658f22a049c0fa27c0acba76235fce2fe1f448ad`
and Lyrical
`sha256:8404b5a397f5eeeb98d673184cfc9748324a80672bf18086914aa71cafba12af`.
They supply external build dependencies, not excluded UG sources or overlays.
The existing distro-dependent test counts are reported separately; no test was
removed or weakened. The first Kilted run passed its 165 tests but its outer
shell runner exited 127 after the runner file was edited while still executing.
The stabilized runner was rerun with an exit status of zero and the same 165
passing tests; it must not be edited during an active run.

Not executed: Kilted with MAVROS, Docker image construction from generated
packaging, ARM builds, hardware validation, publication CI, registry actions or
any unsupported distribution. Optional packaging is verified by exact source
bytes/COPY closure, not claimed as a newly qualified production image.
