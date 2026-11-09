# Lot 5E — Atomic native runtime relocation

Base: `2d6bc25973cf6744561a10e979eb974127a942ba`, verified merge of PR 19
into `origin/dev`. Branch: `refactor/move-gnss-runtime`. Only `gnss_runtime/`
moves to `core/gnss_runtime/`; no functional UG version or distribution policy
changes. Runtime remains native-only and excluded from all ROS2 pilots.

The final local commit is the commit introducing this report, titled
`refactor(repo): relocate GNSS runtime without behavior changes`. Its full SHA
is resolved without embedding a self-referential commit hash:

```bash
git log -1 --diff-filter=A --format='%H %s' -- tools/branches/RUNTIME_MOVE.md
```

## Pre-edit audit and feasibility

The audit completed on the clean expected merge base before any file edits.
There are eight tracked files, all Git mode `100644`, with original subtree
`dcbf2d7583d2b688cff69dc018c23bb7e972025a`:

```text
CMakeLists.txt
include/universal_gnss_runtime/posix_serial_factory.hpp
include/universal_gnss_runtime/receiver_supervisor.hpp
src/posix_serial_factory.cpp
src/receiver_supervisor.cpp
src/universal_gnss_supervisor.cpp
tests/test_posix_receiver_supervisor.cpp
tests/test_receiver_supervisor.cpp
```

| Surface | Verified contract / reference | Treatment |
| --- | --- | --- |
| Root CMake | `add_subdirectory(gnss_runtime)` | Change only the source path |
| Runtime CMake | C++17; PUBLIC driver, NTRIP, transport and Threads | Preserve every byte; all dependencies already reside under `core/` and are linked by target name |
| Transitive dependencies | Core/protocols through driver/NTRIP; OpenSSL through Linux transport | No edge or conditional changes; host OpenSSL 3.5.5 and NPTL 2.43 available during audit |
| Internal paths | `CMAKE_CURRENT_SOURCE_DIR` for include; relative `src/`, `tests/` and install `include/` | Remain valid automatically; no `PROJECT_SOURCE_DIR` or parent-relative path needing repair |
| Shared TLS tests | `${CMAKE_SOURCE_DIR}/core/gnss_transport/tests` and `/fixtures/tls`; silent TLS helper and CA | Root source directory is unchanged; preserve runtime CMake and transport inputs |
| Linux/POSIX | Linux adds serial factory and `universal_gnss_supervisor`; second native test is Linux-only | Preserve source selection, stop/reconnect policies and test timeouts |
| ROS2/MAVROS/tools | No runtime consumer or runtime source path in their build entry points | No change or new dependency |
| Docker/Compose/CI | Production package-based colcon entry points; no explicit runtime source path | No Docker, Compose or workflow edits |
| Generator | One exclusion and two profile forbidden lists name the old root | Move the exclusion and add the new forbidden prefix, retaining old-root guards |
| Isolation runner | Explicit absence assertion for old root | Retain it and add absence assertion for the relocated root |
| Formatter | Runtime is absent from the existing explicit component roots | Keep script/workflow/Makefile and selection policy unchanged |
| Current structural docs | Refactor table/status and distribution exclusion description | Update source locations only |

The internal CMake and all seven C/C++ files can therefore retain their exact
blobs and modes. No relative-path or formatting exception inside the moved
subtree is needed, and no migration blocker was found.

## Distribution boundary

Runtime is **not** added to the 13 components. Every component definition,
dependency edge, option, platform, package entry point and public distribution
format stays unchanged. The existing runtime exclusion moves to
`core/gnss_runtime/`. Both profiles retain the old forbidden prefix and add the
new one. There is no global `core/` selector or exclusion.

The unchanged generator uses explicit selectors and longest-prefix ownership;
the relocated runtime still has no selected owner. The package-based isolated
runner asserts both runtime directories are absent. ROS2 and MAVROS receive no
runtime target, source, header, test or additional dependency.

The real-manifest tests check absence from all eight closures and add negative
cases for:

- accidental runtime selection, even if assigned to an existing component;
- injection of a real runtime source into a generated tree at either prefix;
- a non-classified neighboring component under `core/`;
- reintroduction of an old-root runtime source.

The existing unclassified-file and individual transport TLS input guards remain
active. No generator algorithm, integrity rule or publication workflow changes.

## Pre-existing formatting debt

Before moving, `clang-format-21 --dry-run --Werror` on
`gnss_runtime/src/posix_serial_factory.cpp` returned **1**, with
`clang-format-violations` diagnostics. The file bytes matched the expected base.
No formatter apply command is used.

The formatting script enumerates seven explicit owned roots and does not include
runtime, either at the old root or under `core/`. The C/C++ Format workflow
calls that script's `--changed` selection. This lot neither creates an exclusion
nor broadens the selector. Its root list, workflow and Makefile remain unchanged.
The unchanged file must continue to fail the same direct style audit after the
move; a green owned-root formatting check is not a runtime formatting
qualification. **The debt is not corrected.**

## Contracts and historical evidence

Preserve `universal_gnss_runtime`, `universal_gnss_supervisor`, public includes
`universal_gnss_runtime/...`, library/executable names, destinations and exports.
The native supervisor lifecycle, receiver sessions, serial/transports, NTRIP/RTCM,
timeouts/reconnects, errors, diagnostics, profiles and synchronization do not
change. There is no C++ logic edit and no new ROS2 interface or dependency.

Root/ROS2 audits, campaign data/scripts, prior relocation reports and shared
checkpoints retain their original contents and historical paths. For example:

```bash
git show 2d6bc25973cf6744561a10e979eb974127a942ba:gnss_runtime/src/posix_serial_factory.cpp
```

Before this move, the frozen historical checker already returns **1**, with
exactly three known alerts for transport test keys `ca.key`, `client.key` and
`server.key`. It is not modified, suppressed or described as passing. The
post-move result is recorded separately below. Functional TODO/ROADMAP counters
and hardware gates remain unchanged; this move adds no downstream robot task.

## Validation

Results on 2026-11-09:

| Check | Result |
| --- | --- |
| Relocation | Eight exact renames, identical blobs/modes and subtree `dcbf2d7583d2b688cff69dc018c23bb7e972025a`; original root absent |
| Native Release configure / compile / install | PASS |
| Installation compared with PR 19 base | All 33 files byte-identical, with identical paths/full permissions, including runtime archive, supervisor and runtime headers |
| Targeted runtime CTests | 2/2 PASS |
| NTRIP client TLS CTest | 1/1 PASS |
| Complete native CTest | 68/68 PASS |
| Existing Python tooling tests | 55/55 PASS |
| Generator and CI/policy tests | 85/85 PASS (56 generator/integration + 29 CI/policy) |
| Eight configurations | Generation, integrity and reproducibility PASS |
| Explicit runtime exclusion, eight generated trees on disk | Both prefixes absent; selected components and every copied source byte/mode exactly equal to base; transport TLS inputs retained |
| Isolated ROS2 Kilted | 165 tests, no errors/failures/skips; build and tests PASS |
| Isolated ROS2 Lyrical | 166 tests, no errors/failures/skips; build and tests PASS |
| Isolated ROS2 Lyrical with MAVROS | 168 tests, no errors/failures/skips; build and tests PASS |
| Existing global owned-root formatter | PASS; selector unchanged and runtime outside scope |
| Direct style audit of serial factory | Raw exit 1 before and after; same 28 diagnostics after path normalization; debt not fixed |
| Changed-file formatter selection | Zero modified owned C/C++ files; root list/workflow unchanged, no new exclusion |
| Syntax / active paths / current links / Docker inputs | PASS |
| Dashboard / shared checkpoint audit / frozen evidence identity / diff whitespace | PASS; no functional counter or historical evidence changes |
| Frozen historical campaign checker | Raw exit 1 before and after, exactly the same three known transport fixture-key alerts; not PASS |

The comparison installation is the validated lot 5D native Release output. Its
source `47a46c89ad16aa18e387e79152cb9d0cb59c06a3` has exactly the same Git
root tree as this lot's merge base. Reference artifacts are not altered or
rebuilt during comparison. This lot configures and compiles a fresh native
build. Runtime tests use fake transports, loopback fixtures and PTYs, without
physical receiver access.

## Immutable input and distribution equivalence

The generator reads committed objects. A disposable local repository captures
the staged source candidate as commit
`8642bb0caf710863fec973d2173b4cd3ea5dea26`, root tree
`920713a9450df8a8322f8b9576a089a4f35c211b`. It supplies generation and isolated
build inputs. Only this excluded report changes afterward; final local commit
regeneration verifies the same inventory and manifest hashes as those inputs.

All 13 component definitions remain byte-equivalent as manifest data; no runtime
component is added. For both profiles and all four option sets, the selected
components and copied source bytes/modes are identical to the base. Runtime's
relocation is completely outside their source payloads. Only source identity
metadata, the embedded manifest and its hash change. The distribution schema,
package versions, generator algorithm and inventory format remain unchanged.

Manifest SHA-256: `7dbec237756989e519f7a897642f8766d368d9bced1a0dd4232616ba234cee7e`.
Source inventory SHA-256 values remain equal to the base for both ROS2 profiles:

| Options | Inventory SHA-256 |
| --- | --- |
| Default | `e819b0f586c24022766dcd3cc9abb0835ad365206d638954e24ad8f73e40cb20` |
| MAVROS | `729b22688b235fbb75708c996dbdade4463503ab1a98ca53480d5e07dd7ae74a` |
| Docker | `41a6c1b5e4887c2d1283a41aeb0b762834acfb270e3fcdd617c304e3188e1567` |
| MAVROS+Docker | `bb628b1e9cfdd5c3dc558931d19ced196454b75463f68bf5a10dea7f55824376` |

The negative tests reject accidental selection even when runtime is assigned
to an existing component and its exclusion is removed in memory. They also
reject injected runtime source files at either path in generated output, an
old-root source reintroduction and a new real-manifest unclassified neighbor.
No test weakens the production manifest or changes source data. The isolated
runner checks both runtime paths before colcon sees the generated sources.

All project builds/tests/generation run as the normal user, UID 1000. Isolated
builds use no network, source mounts or inherited UG overlays. They reuse the
three dependency images identified in the [lot 3 validation record](CI.md),
whose identities were rechecked. An initial MAVROS invocation used an incorrect
image tag and stopped before build; it was rerun with the validated immutable
image ID `sha256:8404b5a397f5eeeb98d673184cfc9748324a80672bf18086914aa71cafba12af`.
No dependency image or source change was needed for that command correction.

## Raw audit results kept separate

The direct clang-format-21 audit of the serial factory returns 1 on both the
base and relocated file. The 28 diagnostics, including source line/column
positions, are identical after removing the added `core/` prefix. The original
blob `a74d53a00f80cb9b10d517975b0016e794262c7b` is preserved. The existing
format workflow's unchanged selector remains authoritative for its current
scope; its empty changed-file selection does not correct or qualify runtime.

The frozen campaign checker also retains raw exit **1**. Its exact failures
before and after are:

```text
FAIL: credential pattern requires review: core/gnss_transport/tests/fixtures/tls/ca.key
FAIL: credential pattern requires review: core/gnss_transport/tests/fixtures/tls/client.key
FAIL: credential pattern requires review: core/gnss_transport/tests/fixtures/tls/server.key
```

The file count changes from 264 at the base to 272 after the move and new
report; that expands the paths examined, without introducing another failure.
No key, certificate, scanner, security rule or historical evidence is edited.
The unchanged checker is only syntax-compiled by the existing Backlog Status
workflow; it is not executed there. Its previous credential review remains
documented in the unchanged `TRANSPORT_MOVE.md` and is not converted to a PASS.

## Commands, limits and scope

Relevant native commands:

```bash
cmake -S . -B /tmp/ug-lot5e-native -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ug-lot5e-native --parallel 4
ctest --test-dir /tmp/ug-lot5e-native -R '^gnss_runtime_' --output-on-failure
ctest --test-dir /tmp/ug-lot5e-native -R '^gnss_ntrip_test_ntrip_client$' --output-on-failure
ctest --test-dir /tmp/ug-lot5e-native --output-on-failure
cmake --install /tmp/ug-lot5e-native --prefix /tmp/ug-lot5e-native-install
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/branches/tests -v
```

The generator suite uses the immutable candidate before the local work-branch
commit. Eight combinations execute the existing workflow generation step with
that explicit SHA. Independent generation/verification on disk checks absence
of both runtime directories in every combination. Isolated runs use
`validate_ci.py --build-image`, making compilation and tests mandatory.

Not executed: Kilted+MAVROS compilation, non-Linux/ARM validation, production
Docker image builds, dependency image provisioning, physical serial/receiver
qualification, remote network qualification, Humble, ESP32, GUI, BlueOS, remote
GitHub Actions or publisher/protection trials. Native-only runtime is not
qualified on ROS2 or hardware by these software builds. Its format debt and
the scanner's known nonzero result remain explicit residual limitations.

Outside the eight renamed files, only these seven files change:

```text
CMakeLists.txt
TODO_REPO_REFACTOR.md
tools/branches/README.md
tools/branches/RUNTIME_MOVE.md (new)
tools/branches/manifest.yaml
tools/branches/test_isolated.sh
tools/branches/tests/test_distribution.py
```

There is one local commit. No other component move, C++ edit, version change,
distribution-policy change, formatter-policy change, historical-proof edit,
publication-workflow change, push, PR, merge, tag or Release belongs to this lot.
