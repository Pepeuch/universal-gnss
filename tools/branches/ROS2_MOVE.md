# Lot 10 — Atomic ROS2 adapter relocation

Base: `6747add24723934650bfa1dfb6c26004ab84d435`, verified merge of PR 22
at origin/dev. Branch: `refactor/move-gnss-ros2`. Only `gnss_ros2/` moves to
`adapters/ros2/gnss_ros2/`. The independent `universal_gnss_msgs/` package stays
at the repository root (KEEP, lot 9). No functional or version change.

The final local commit is the commit adding this report, titled
`refactor(repo): relocate GNSS ROS2 adapter without behavior changes`:

```bash
git log -1 --diff-filter=A --format='%H %s' -- tools/branches/ROS2_MOVE.md
```

## Pre-edit audit and complete inventory

The clean expected base, PR merge, dependency graph, active references and
calculated root were checked before production edits. Lot 9 evidence applies
to the same source tree. No other pending local work or submodule was present.
The 38 source files all have Git mode 100644. Their original subtree is
`bd1fdfe08e4c2fe35b51f11c0ca657497c17fb99`.

Complete relative inventory (only CMakeLists.txt changes content):

```text
CMakeLists.txt
config/empty_parameters.yaml
include/universal_gnss_ros2/diagnostic_adapter.hpp
include/universal_gnss_ros2/gnss_status_adapter.hpp
include/universal_gnss_ros2/launch_incarnation.hpp
include/universal_gnss_ros2/msg/gnss_status.hpp
include/universal_gnss_ros2/msg/rtcm_frame.hpp
include/universal_gnss_ros2/navsat_fix_adapter.hpp
include/universal_gnss_ros2/ntrip_node.hpp
include/universal_gnss_ros2/receiver_node.hpp
include/universal_gnss_ros2/replay_node.hpp
include/universal_gnss_ros2/srv/get_receiver_snapshot.hpp
launch/ntrip.launch.py
launch/receiver_and_ntrip.launch.py
launch/receiver_serial.launch.py
launch/receiver_tcp.launch.py
launch/replay.launch.py
package.xml
src/diagnostic_adapter.cpp
src/gnss_status_adapter.cpp
src/navsat_fix_adapter.cpp
src/ntrip_node.cpp
src/ntrip_node_main.cpp
src/receiver_node.cpp
src/receiver_node_main.cpp
src/replay_node.cpp
src/replay_node_main.cpp
src/rtcm_diagnostic_projection.hpp
tests/check_build_library_provenance.py
tests/test_combined_launch.py
tests/test_diagnostic_adapter.cpp
tests/test_gnss_status_adapter.cpp
tests/test_interface_aliases.cpp
tests/test_launch_incarnation.cpp
tests/test_navsat_fix_adapter.cpp
tests/test_ntrip_node.cpp
tests/test_receiver_node.cpp
tests/test_replay_node.cpp
```

Exactly 37 files preserve every byte, relative path and mode. This includes
package.xml, all C++ sources/headers, compatibility aliases, tests, five launch
files and the YAML. No internal test, install, dependency or target is edited.
The old physical source directory is absent.

## Exact authorized internal CMake exception

At line 6, replace only the parent calculation:

```diff
 get_filename_component(UNIVERSAL_GNSS_ROS2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)
-get_filename_component(UNIVERSAL_GNSS_SOURCE_ROOT "${UNIVERSAL_GNSS_ROS2_SOURCE_DIR}/.." REALPATH)
+get_filename_component(UNIVERSAL_GNSS_SOURCE_ROOT "${UNIVERSAL_GNSS_ROS2_SOURCE_DIR}/../../.." REALPATH)
```

Line 5 retains canonical REALPATH resolution of the actual package location,
including CI workspace symlinks. Line 6 retains REALPATH and the existing
UNIVERSAL_GNSS_SOURCE_ROOT variable, changing only the number of parents.
Without this edit the calculated root would be adapters/ros2 and the existing
core/protocol header checks would fail. There is no generic root finder,
cache override, fallback, symlink workaround or change to the launch directory.

The relocated subtree is `4c64d805422184ae64173cc31830c5cd91be7410`.
An independent Git-object check substitutes only the original CMake blob into
that tree: git mktree reconstructs exactly the original subtree
`bd1fdfe08e4c2fe35b51f11c0ca657497c17fb99`. This checks the entire remaining
tree and modes, rather than relying on rename labels. The standard diff shows
37 R100 renames and one adapted CMake rename; 38 R100 is not claimed.

## External references and historical boundary

| Active surface | Adaptation |
| --- | --- |
| Root CMake | Only the optional ROS2 add_subdirectory source path |
| Root Dockerfile | Only the explicit colcon ROS2 base path |
| Kilted and Lyrical workflows | Only the source target of the ROS2 workspace symlink; workspace alias/package names stay unchanged |
| Backlog Status workflow and Python provenance tests | Follow the moved provenance checker |
| Formatter | Translate the exact owned root; same 28 ROS2 C/C++ files, no policy broadening to MAVROS/runtime |
| Generator | Only the ROS2 package entry path in generated instructions; no algorithm change |
| Manifest | Translate ROS2 selector and three existing reference origins; explicitly require package.xml as justified below |
| Isolated runner | Translate the base path and assert absence of the old source root |
| Generator tests | Translate synthetic source paths/expectations, retain all existing tests, add eight negative tests |
| AGENTS.md and ROS2 policy | Translate the active policy-trigger path only |
| Current docs | README/ROS2 source pointers, refactor table/progress, generator reconstruction link, devcontainer instructions |

The current devcontainer example now selects both messages and runtime, sources
the new installation, and checks actual test results. Its old root-level core
symlink is removed; common libraries are rebuilt through the real repository
root. This is limited to the current installation instruction and explanation.

Compose, entrypoint, healthcheck, dependency-only Dockerfile, Docker runtime
contract checks and publication workflows use unchanged public identities or
need no physical ROS2 path. They remain byte-identical. Earlier MOVE reports,
validation campaigns, shared checkpoints, baseline audits, release evidence,
ros2_mapping's recorded local audit method, ros2_end_to_end_audit's historical
smoke test/links and auto_configuration's old proposed file set are preserved.
Old paths in those records are historical references, not active build inputs.

## Dependencies and public contracts

Messages remains a pure ament/ROSIDL package: two messages and one service,
with builtin_interfaces/diagnostic_msgs and ROSIDL generator/runtime dependencies.
No message, service, generated-type identity, package version or dependency changes.

ROS2 reconstructs core, protocols, transport, driver, NTRIP and tools when their
targets are absent, temporarily disabling their native tests. Driver depends
on protocols/transport; NTRIP on protocols/transport; tools on driver/NTRIP/
protocols; protocols on core. The native runtime is not part of this graph.

Preserved contracts include:

- Package identities universal_gnss_msgs, universal_gnss_ros2 and
  universal_gnss_mavros, each retaining its existing package version 0.7.0.
- Canonical GnssStatus, RtcmFrame and GetReceiverSnapshot ROSIDL definitions,
  C++/Python imports and the existing universal_gnss_ros2 compatibility aliases.
- Three adapter libraries and three node libraries; existing exports, C++17,
  dependency visibility and Linux conditions.
- receiver_node, replay_node and ntrip_node; destinations under
  lib/universal_gnss_ros2 and share/universal_gnss_ros2, headers and ament resources.
- All five launch files, configuration YAML, node names, parameters, remappings,
  topics and get_snapshot/get_health/step services.
- Coordinates, clocks, observation sequence, provenance, incarnation,
  freshness/invalidation and receiver/replay/NTRIP behavior.
- MAVROS 2.16.0 EXACT, plugin universal_gnss, class/base identifiers, descriptor,
  exported targets and installed library paths. No MAVROS file is modified.

The root native optional ROS2 path changes, but messages still requires its
own package build/installed prefix. This move does not create a native SDK or
qualify combined root ROS2+MAVROS operation.

## Manifest and new negative tests

A pre-edit probe removed only gnss_ros2/package.xml. The default generator
accepted that incomplete package and reported only messages in package_versions;
MAVROS selection rejected it indirectly through its ROS2 dependency. The
relocated package.xml now has an explicit required selector alongside the
precise ROS2 directory selector. The existing longest-match classifier and
path-presence validator reject both cases, with no generator algorithm change,
new broad selector, graph change, duplicate source or payload addition.

All 13 components, dependencies, closures, profiles, options, external dependency
contracts and exclusions remain. MAVROS is optional, messages independent;
native runtime and development scripts stay excluded. Three audited ROS2 edges
still require tools, testdata and the Python healthcheck test. TLS and the seven
tools fixture edges remain unchanged. No global adapters selector is added.

The original 123 generator/CI tests, including all 25 lot 8B corrections, are
preserved with necessary physical-path translations only. Eight new independent
negative tests reject:

1. Reintroduced old ROS2 root.
2. Missing relocated package.xml, both with and without MAVROS.
3. Unclassified neighbor under adapters/ros2.
4. Selected duplicate package name, with a distinct-name acceptance control.
5. Missing explicitly audited healthcheck test input, without relying on a
   missing component selector to reject it.
6. Injection of either native runtime path into generated output.
7. Injection of generator or agent development scripts.
8. MAVROS injection when its option is absent.

The injection tests exercise generated files on disk across the applicable
profiles/options and use the trusted verifier. Previous duplicate/exclusion,
CMake/pluginlib literal/comment/category/type/dynamic-limit, fixture and CI
failure-propagation protections remain active.

## Immutable validation input and isolation

All full suites and builds below actually ran under normal user UID 1000 on
committed disposable input `e7ab8b83a178b442feb335e068b585e57386f112`, root
Git tree `9c38a2ca6f88843b434a51639faaf28457003fab`. Only this excluded report
changes afterward. Final work-branch generation checks the same generator,
manifest, copied sources and inventory hashes, with final source-commit metadata.
No second work-branch commit is created for validation.

The three isolated generated-source builds use previously qualified cached images:

- Kilted: `sha256:0eb6163dd3943411718e8355430a319b86d1baca11d84a0930ef81b73e7ce8cf`.
- Lyrical: `sha256:2c2b6e77472c03a522de6442344aac64ad9a1df1723fd7b506e6a346627f9029`.
- Lyrical/MAVROS: `sha256:8404b5a397f5eeeb98d673184cfc9748324a80672bf18086914aa71cafba12af`.

Only generated trees are streamed into these builds, with no repository mount,
network, credentials or pre-existing UG overlay. No dependency download or
image provisioning occurs. The separate standalone/integrated harness streams
the full Git archive into the cached MAVROS image; it is not presented as a
fourth generated-distribution build.

## Results actually executed

| Validation | Observed result |
| --- | --- |
| Structural inventory, modes and Git-object reconstruction | PASS: 38 files, 37 identical, exact one-line CMake exception |
| Fresh native Release configure/build/install | PASS |
| Full native CTest | 68/68 PASS |
| Fresh base vs candidate native installation | 33 identical files: bytes, paths and full permissions |
| Existing Python tooling tests | 55/55 PASS |
| Generator/integration and CI tests | 131/131 PASS: 102 generator/integration + 29 CI, including eight new tests and the retained 25 lot 8B corrections |
| Eight distributions, each independently generated twice | Integrity, reproducibility and translated source inventory comparison PASS |
| Isolated Kilted standard | Build PASS; 165 tests, zero errors/failures/skips |
| Isolated Lyrical standard | Build PASS; 166 tests, zero errors/failures/skips |
| Isolated Lyrical with MAVROS | Build PASS; 168 tests, zero errors/failures/skips |
| Explicit three-package colcon discovery | PASS: exactly messages, ROS2 and MAVROS at the intended source paths |
| CI symlink discovery and standalone build | PASS: all three packages built through workspace aliases |
| Canonical root through symlinks | PASS: generated driver build rules reference the real root/core/gnss_driver source |
| ROSIDL/Python/C++ identities | Interface show, installed msg/srv source comparisons, Python imports and compiled C++ alias tests PASS |
| Standalone MAVROS | Build and 2/2 tests PASS; actual pluginlib factory loading PASS |
| Integrated MAVROS | Plugin build and 2/2 tests PASS; installed XML/libraries/headers checked; automatic ament discovery NOT QUALIFIED |
| External MAVROS explicitly disabled | Expected REQUIRED-package configure refusal, raw exit 1 |
| Installed package resources, exports and executable dependency resolution | PASS; prefix-relative CMake exports, resolved ELF dependencies, no source/build RPATH/RUNPATH |
| Changed Python, XML, YAML and shell syntax | PASS |
| Formatter, exact 28 ROS2 files and existing full owned-root selection | PASS, no formatting applied |
| Current documentation links, dashboard and checkpoint audit | PASS within active/changed scope; ten indexed shared entries, no problems/warnings, functional counters unchanged |
| Dockerfile source/COPY entry checks and diff whitespace | PASS; production Docker image build not executed |
| Direct MAVROS formatting | Raw exit 1, 111 pre-existing diagnostics; NOT PASS |
| Direct runtime formatting | Raw exit 1, 28 pre-existing diagnostics; NOT PASS |
| Historical campaign scanner | Raw exit 1, exactly three existing TLS-key alerts; NOT PASS |
| Docker Compose | Plugin absent, version command exit 1; configuration NOT EXECUTED |

The fresh native reference was configured/built from the exact base in a
separate temporary source tree. Base build/install:
/tmp/ug-lot10-base-native and /tmp/ug-lot10-base-install. Candidate build/install:
/tmp/ug-lot10-native and /tmp/ug-lot10-install. Their 33-file comparison does
not qualify the native SDK as autonomous.

## Eight distributions and source equivalence

Both profiles retain the same component/package/file counts:

| Options | Components | ROS packages | Copied sources | Total files |
| --- | --- | --- | --- | --- |
| Default | 11 | 2 | 308 | 312 |
| MAVROS | 12 | 3 | 318 | 322 |
| Docker | 12 | 2 | 316 | 320 |
| Both | 13 | 3 | 326 | 330 |

Comparison with the exact base translates only the physical ROS2 prefix.
Within that component the only changed content is the root-calculation line.
Outside it, Docker-selected outputs additionally translate the Dockerfile's
explicit colcon source path, as authorized. All other copied bytes, modes,
ownership and public-contract classifications match the base exactly. Generated
instructions follow the same new source path; package identities/versions,
options and dependency contracts remain identical. Manifest, generator, source
identity and inventory metadata change as expected. This is not a claim that
the new physical inventory hash equals the pre-move hash.

Manifest SHA-256:
`e16c815d2fc8d9b6fb697ea0603f22c3ef98a1dda35f8eb8e25dbf431c01462e`.
Inventory SHA-256 values apply to both profiles:

| Options | Inventory SHA-256 |
| --- | --- |
| Default | `6e96d0105adb28f6dd5af7dab1712f29ee27310cb300d0c590dae6503677b755` |
| MAVROS | `de37aee64dee658ceeb92cb379124d2d2dcf0ddfae083583df080399710eb2e7` |
| Docker | `a78f227871e3840bac423f4e6e50580463fb9514e6c64444d32c1e5edf8c2fc0` |
| Both | `7d79aed7d8fdcbd6a09f07619e734ba83bce843a485addecf75f3f635b3571e8` |

All outputs exclude the old ROS2 root, native runtime at both paths, generator,
agent/CI/development directories and unselected MAVROS. Only the existing
healthcheck test and optional Docker support collector remain selected scripts.
TLS and tools fixture references still resolve within every selected tree.

## Installation, factory and diagnostic path qualification

ROS2 exports retain `_IMPORT_PREFIX`-relative include directories and installed
library locations. Installed package XML/ament resources and msg/srv files are
checked separately. All three installed executables resolve their ELF libraries;
readelf reports no RPATH/RUNPATH on those executables. No source/build path is
found in installed text files under the validated package installations.

The real pluginlib loader discovers universal_gnss and creates its PluginFactory
from that standalone installation's shared library. It does not create a UAS,
live plugin instance or vehicle connection. The integrated root build uses its
installed ROS2 prefix and builds the plugin plus two tests. Its descriptor and
libraries install, but the ament plugin resource is absent, as before.
Compilation and tests do not qualify integrated automatic discovery.

Eight installed binaries/archives still contain source or ROSIDL build filenames
in diagnostic strings: generated FastRTPS type support, the MAVROS plugin, three
node archives and three node executables. CI-alias source names may appear when
building through symlinks. These are distinguished from loader paths and exported
build dependencies; no zero-string requirement, prefix-map change or diagnostic
rewrite is introduced. This is the same pre-existing category found in lot 9,
not a claim of byte-identical ROS2 binaries after a physical source move.

An initial temporary inspection counted only paths under the real source/build
roots and missed the CI symlink aliases. Its assertion was rejected; the harness
was corrected to inspect both spellings and then rerun successfully. No project
source or acceptance control changed. The accepted run contains the two actual
standalone tests, real factory loading, installed-contract checks and two
integrated tests, rather than zero-test discovery.

## Reproduction and final scope

From the final source checkout, use its explicit SHA for generation/builds:

```bash
source_sha="$(git rev-parse HEAD)"
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/branches/tests -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -v
cmake -S . -B /tmp/ug-lot10-native -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/ug-lot10-install
cmake --build /tmp/ug-lot10-native --parallel 3
ctest --test-dir /tmp/ug-lot10-native --output-on-failure
cmake --install /tmp/ug-lot10-native
python3 tools/branches/validate_ci.py --source-ref "$source_sha" --distribution ros2-kilted --build-image sha256:0eb6163dd3943411718e8355430a319b86d1baca11d84a0930ef81b73e7ce8cf
python3 tools/branches/validate_ci.py --source-ref "$source_sha" --distribution ros2-lyrical --build-image sha256:2c2b6e77472c03a522de6442344aac64ad9a1df1723fd7b506e6a346627f9029
python3 tools/branches/validate_ci.py --source-ref "$source_sha" --distribution ros2-lyrical --with mavros --build-image sha256:8404b5a397f5eeeb98d673184cfc9748324a80672bf18086914aa71cafba12af
```

Exhaustive accompaniment list outside the 38 moved files:

```text
.agent/policies/ROS2.md
.github/workflows/backlog-status.yml
.github/workflows/ros2-kilted.yml
.github/workflows/ros2-lyrical.yml
AGENTS.md
CMakeLists.txt
Dockerfile
README.md
TODO_REPO_REFACTOR.md
docs/devcontainer.md
docs/ros2.md
scripts/clang_format_21.sh
scripts/tests/test_build_library_provenance.py
tools/branches/README.md
tools/branches/ROS2_MOVE.md
tools/branches/distribution.py
tools/branches/manifest.yaml
tools/branches/test_isolated.sh
tools/branches/tests/test_distribution.py
```

## Preserved debts, non-executed validation and residual risks

The direct formatter debts, scanner exit 1 and three transport TLS keys are
unchanged; no exception or weakened test is added. The historical scanner's
other checks inspect active changed Markdown/evidence without rewriting old
reports. Docker Compose is absent. Native SDK autonomy and integrated MAVROS
index discovery remain unqualified. Binary diagnostic paths are not repaired.

Not executed: production Docker image builds, Compose configuration, root ROS2
and MAVROS simultaneously enabled, Kilted+MAVROS, new dependency images,
non-Linux/ARM, physical GNSS/serial/reconnect, real caster/UAS/FCU/plugin instance,
Humble, ESP32, GUI, BlueOS, remote CI, publication or protection changes.
Software tests and factory loading are not hardware qualification.

External private scripts may depend on physical source paths; installed public
contracts are preserved but such consumers cannot be exhaustively enumerated.
The fixed three-parent calculation deliberately matches the authorized source
layout; future depth changes require their own audited adjustment. Frozen
historical links may retain old physical names and are not active instructions.

No functional TODO/ROADMAP counter or downstream behavior changes. Local agent
checkpoint disposition: LOCAL_ONLY, ignored and never staged; durable evidence
is in this report. Shared checkpoints are unchanged. Final history is one local
commit from the exact base, no other component move, version or functional edit.
No push, PR, merge, tag, Release or remote distribution is created.
