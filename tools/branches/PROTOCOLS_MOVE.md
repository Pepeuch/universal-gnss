# Lot 4 — Protocol source relocation

Base: `55fc6cc2c9c2ecafa70c0025fcce228496271810`, the verified merge of PR 14.
The only component move is `gnss_protocols/` → `protocols/gnss_protocols/`,
performed with `git mv`. All 58 component files keep their original bytes and
Git executable modes, including its CMakeLists, headers, parsers and tests.
No NMEA, UBX, Unicore or RTCM behavior changes.

## Contracts and references

`universal_gnss_protocols` remains the library/target name. Public includes remain
`universal_gnss_protocols/...`; other libraries, ROS2 packages, ROSIDL interfaces,
exports, executables, install paths, launch files and parameters remain unchanged.

| Active reference | Adjustment |
| --- | --- |
| Root `CMakeLists.txt` | New source argument to `add_subdirectory` |
| `gnss_ros2/CMakeLists.txt` | New source/include paths and configuration error text; keep its existing binary directory |
| `scripts/clang_format_21.sh` | Owned component root points at the new location |
| `tools/branches/manifest.yaml` | Protocol component owns only `protocols/gnss_protocols/`; dependency graph unchanged |
| Generator fixtures/tests | Use nested source paths, assert the new files are present/old root absent; reject an unclassified sibling under `protocols/` |
| `testdata/README.md` | Current test-source references point at moved files; corpus bytes untouched |
| Current README/protocol guide/refactor plan/tool guide | Describe the actual new source location and unchanged logical/public identity |

No direct source-path adjustment is needed in MAVROS, Dockerfiles, Compose, CI,
launch files, other scripts or fixtures. MAVROS links existing targets/exports;
Docker copies the repository root; ROS2 CI builds the same packages. The current
isolated runner and generator rules require no rewrite or relaxation. Optional
Docker distributions still contain every original COPY input and the complete
nested protocol component through the unchanged transitive dependency graph.

## Historical references

Do not rewrite `CURRENT_MAIN_AUDIT_DELTA.md`, `UNIVERSAL_GNSS_AUDIT.md`,
`docs/ros2_mapping.md` (dated 2026-06-01), `docs/ros2_end_to_end_audit.md`,
validation campaigns or frozen scripts. Their old `gnss_protocols/...` paths and
workspace examples describe the recorded repository state, not current commands.
A relative link to that old root in a frozen report is historical, not a live
build dependency. Resolve it through that report's recorded commit or the
pre-move base, for example:

```bash
git show 55fc6cc2c9c2ecafa70c0025fcce228496271810:gnss_protocols/include/universal_gnss_protocols/ubx_cfg_builder.hpp
```

Logical component names in architecture diagrams and public include/library
identifiers are not source paths and are deliberately retained. The ROS2 CMake
binary directory `${CMAKE_CURRENT_BINARY_DIR}/gnss_protocols` is also intentionally
retained; only the source directory changed.

## Validation

The candidate-tree checks passed. Builds use a
normal project user, with fresh native output outside the repository. The
immutable generator input for pre-commit tests is a disposable Git repository
exported from the staged tree; the final commit's generated inventories are
compared against the tested candidate. No excluded source is mounted into the
isolated Kilted/Lyrical/MAVROS containers.

The moved subtree must have the same Git tree identity as the original. Only
source references, the manifest selector and the current documentation may
differ outside it. This structural milestone changes no UG version or
UGA/project/release qualification counter and grants no new physical evidence.

| Check | Result |
| --- | --- |
| Moved component identity | All 58 files/modes identical; subtree `2047f1c49f743de5423ce4842e8eb544c7a15511` preserved |
| Native Release configure/build/install | PASS; `lib/libuniversal_gnss_protocols.a` retains its installed name |
| Focused protocol CTests | 18/18 PASS |
| Complete native CTest suite | 68/68 PASS |
| Generator + CI tests | 74 PASS (45 generator tests + 29 CI/policy tests) |
| Existing Python tooling | 55 PASS |
| Kilted isolated distribution | Build PASS; 165 tests, no errors/failures/skips |
| Lyrical isolated distribution | Build PASS; 166 tests, no errors/failures/skips |
| Lyrical + optional MAVROS | Three packages built; 168 tests, no errors/failures/skips |
| All eight profile/option combinations | Generation, integrity and reproducibility PASS |
| Formatting | clang-format-21 PASS on all 57 moved C/C++ files and all tracked owned roots |
| Documentation/status/evidence | Current links, dashboard, checkpoint audit, rc6 frozen evidence and diff whitespace PASS |

Pre-commit isolated input: staged-tree snapshot commit
`087a9b7ace9f9818a4c992a9a16c282e1b2bc473` in a disposable local Git repository.
The protocol subtree is identical to the base's; only outside source references
and current documentation differ. The final commit is checked by regeneration
and comparison of source inventory/manifest hashes with the tested candidate.
This snapshot is validation scratch, not a new development or published branch.

The unchanged lot 3 dependency images were reused: Kilted
`sha256:0eb6163dd3943411718e8355430a319b86d1baca11d84a0930ef81b73e7ce8cf`,
Lyrical `sha256:2c2b6e77472c03a522de6442344aac64ad9a1df1723fd7b506e6a346627f9029`.
Optional MAVROS uses the existing external dependency image
`sha256:8404b5a397f5eeeb98d673184cfc9748324a80672bf18086914aa71cafba12af`.
Builds run as UID1000 without networking or excluded-source mounts.

Not executed: Kilted with MAVROS, ARM, hardware, a fresh production Docker image
build or new GitHub Actions runs. Dockerfile/COPY closure is checked in generated
packaging, while existing Dockerfiles, Compose and workflows remain byte-identical.
No regression was observed in the executed validations. The old paths in frozen
reports are intentionally preserved and resolved through Git, not root aliases.
